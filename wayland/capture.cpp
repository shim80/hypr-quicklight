#include "wayland/capture.hpp"

#include "wayland-client-ext-image-copy-capture.hpp"
#include "wayland-client-ext-image-capture-source.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace wl_quiklight {

namespace {
int create_anonymous_file(off_t size) {
#ifdef SYS_memfd_create
    int fd = static_cast<int>(syscall(SYS_memfd_create, "hypr-quiklight-capture", MFD_CLOEXEC));
    if (fd >= 0) {
        if (ftruncate(fd, size) == 0) {
            return fd;
        }
        close(fd);
    }
#endif

    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        throw std::runtime_error("XDG_RUNTIME_DIR is not set");
    }

    std::string path = std::string(runtime_dir) + "/hypr-quiklight-shm-XXXXXX";
    std::vector<char> tmp(path.begin(), path.end());
    tmp.push_back('\0');

    fd = mkstemp(tmp.data());
    if (fd < 0) {
        throw std::runtime_error("mkstemp failed for shm file");
    }

    unlink(tmp.data());

    if (ftruncate(fd, size) != 0) {
        close(fd);
        throw std::runtime_error("ftruncate failed for shm file");
    }

    return fd;
}
} // namespace

class Capture::Impl {
public:
    explicit Impl(const std::string& output_description_substring, bool verbose)
        : verbose_(verbose), output_match_(output_description_substring) {
        raw_display_ = wl_display_connect(nullptr);
        if (!raw_display_) {
            throw std::runtime_error("Failed to connect to Wayland display");
        }

        display_ = std::make_unique<wayland::display_t>(raw_display_);
        registry_ = display_->get_registry();

        registry_.on_global() = [&](uint32_t name, const std::string& interface, uint32_t version) {
            if (interface == wayland::output_t::interface_name) {
                outputs_.emplace_back();
                registry_.bind(name, outputs_.back(), version);
            } else if (interface == wayland::shm_t::interface_name) {
                registry_.bind(name, shm_, version);
            } else if (interface == wayland::ext_output_image_capture_source_manager_v1_t::interface_name) {
                registry_.bind(name, output_source_manager_, version);
            } else if (interface == wayland::ext_image_copy_capture_manager_v1_t::interface_name) {
                registry_.bind(name, copy_manager_, version);
            }
        };

        display_->roundtrip();

        if (!output_source_manager_) {
            throw std::runtime_error("Interface not supported: ext_output_image_capture_source_manager_v1");
        }
        if (!copy_manager_) {
            throw std::runtime_error("Interface not supported: ext_image_copy_capture_manager_v1");
        }
        if (!shm_) {
            throw std::runtime_error("Interface not supported: wl_shm");
        }
        if (outputs_.empty()) {
            throw std::runtime_error("No wl_output objects found");
        }

        for (auto& output : outputs_) {
            auto* output_ptr = &output;
            output_ptr->on_description() = [this, output_ptr](const std::string& description) {
                if (verbose_) {
                    std::cerr << "Found output with description: " << description << std::endl;
                }
                if (!selected_output_ && description.find(output_match_) != std::string::npos) {
                    selected_output_ = output_ptr;
                    if (verbose_) {
                        std::cerr << "Selected output: " << description << std::endl;
                    }
                }
            };
        }

        display_->roundtrip();

        if (!selected_output_) {
            throw std::runtime_error("Requested output not found: " + output_match_);
        }

        source_ = output_source_manager_.create_source(*selected_output_);
        session_ = copy_manager_.create_session(source_, 0);

        if (!session_) {
            throw std::runtime_error("Failed to create ext image capture session");
        }

        session_.on_buffer_size() = [&](uint32_t width, uint32_t height) {
            width_ = width;
            height_ = height;
        };

        session_.on_shm_format() = [&](wayland::shm_format format) {
            shm_formats_.push_back(static_cast<uint32_t>(format));
        };

        session_.on_done() = [&]() {
            std::lock_guard<std::mutex> lock(mutex_);
            constraints_ready_ = true;
            cv_.notify_one();
        };

        session_.on_stopped() = [&]() {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
            cv_.notify_one();
        };

        wait_for_constraints();
        setup_shm_buffer();
    }

    ~Impl() {
        destroy_buffer();
        session_ = {};
        source_ = {};
        copy_manager_ = {};
        output_source_manager_ = {};
        shm_ = {};
        registry_ = {};
        outputs_.clear();
        selected_output_ = nullptr;
        display_.reset();
        raw_display_ = nullptr;
    }

    bool capture(const Capture::Callback& callback) {
        if (stopped_) {
            return false;
        }

        bool ready = false;
        bool failed = false;

        auto frame = session_.create_frame();
        if (!frame) {
            throw std::runtime_error("Failed to create capture frame");
        }

        frame.on_ready() = [&]() {
            std::lock_guard<std::mutex> lock(mutex_);
            ready = true;
            cv_.notify_one();
        };

        frame.on_failed() = [&](wayland::ext_image_copy_capture_frame_v1_failure_reason reason) {
            std::lock_guard<std::mutex> lock(mutex_);
            failed = true;
            last_failure_reason_ = static_cast<uint32_t>(reason);
            cv_.notify_one();
        };

        frame.attach_buffer(buffer_);
        frame.damage_buffer(0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_));
        frame.capture();

        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!ready && !failed && !stopped_) {
                lock.unlock();
                if (display_->dispatch() < 0) {
                    throw std::runtime_error("Wayland dispatch failed during capture");
                }
                lock.lock();
            }
        }

        if (failed || stopped_) {
            if (verbose_) {
                std::cerr << "Capture failed, reason=" << last_failure_reason_ << std::endl;
            }
            return false;
        }

        callback(width_, height_, static_cast<uint32_t>(shm_format_), static_cast<const uint8_t*>(mapped_data_));
        return true;
    }

private:
    void wait_for_constraints() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!constraints_ready_ && !stopped_) {
            lock.unlock();
            if (display_->dispatch() < 0) {
                throw std::runtime_error("Wayland dispatch failed while waiting for constraints");
            }
            lock.lock();
        }

        if (stopped_) {
            throw std::runtime_error("Capture session stopped before constraints were received");
        }
        if (width_ == 0 || height_ == 0) {
            throw std::runtime_error("Capture session did not provide a valid buffer size");
        }
    }

    void setup_shm_buffer() {
        bool have_xrgb = false;
        bool have_argb = false;
        for (uint32_t fmt : shm_formats_) {
            if (fmt == static_cast<uint32_t>(wayland::shm_format::xrgb8888)) {
                have_xrgb = true;
            }
            if (fmt == static_cast<uint32_t>(wayland::shm_format::argb8888)) {
                have_argb = true;
            }
        }

        if (have_xrgb) {
            shm_format_ = wayland::shm_format::xrgb8888;
        } else if (have_argb) {
            shm_format_ = wayland::shm_format::argb8888;
        } else {
            throw std::runtime_error("Compositor did not advertise wl_shm XRGB8888/ARGB8888 for image copy capture");
        }

        stride_ = width_ * 4;
        buffer_size_ = stride_ * height_;

        shm_fd_ = create_anonymous_file(static_cast<off_t>(buffer_size_));
        mapped_data_ = mmap(nullptr, buffer_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (mapped_data_ == MAP_FAILED) {
            mapped_data_ = nullptr;
            close(shm_fd_);
            shm_fd_ = -1;
            throw std::runtime_error("mmap failed for capture buffer");
        }
        std::memset(mapped_data_, 0, buffer_size_);

        shm_pool_ = shm_.create_pool(shm_fd_, static_cast<int32_t>(buffer_size_));
        buffer_ = shm_pool_.create_buffer(0,
                                          static_cast<int32_t>(width_),
                                          static_cast<int32_t>(height_),
                                          static_cast<int32_t>(stride_),
                                          shm_format_);
    }

    void destroy_buffer() {
        buffer_ = {};
        shm_pool_ = {};
        if (mapped_data_) {
            munmap(mapped_data_, buffer_size_);
            mapped_data_ = nullptr;
        }
        if (shm_fd_ >= 0) {
            close(shm_fd_);
            shm_fd_ = -1;
        }
    }

private:
    bool verbose_{false};
    std::string output_match_;

    wl_display* raw_display_{nullptr};
    std::unique_ptr<wayland::display_t> display_;
    wayland::registry_t registry_;

    std::vector<wayland::output_t> outputs_;
    wayland::output_t* selected_output_{nullptr};
    wayland::shm_t shm_;

    wayland::ext_output_image_capture_source_manager_v1_t output_source_manager_;
    wayland::ext_image_copy_capture_manager_v1_t copy_manager_;
    wayland::ext_image_capture_source_v1_t source_;
    wayland::ext_image_copy_capture_session_v1_t session_;

    std::vector<uint32_t> shm_formats_;
    uint32_t width_{0};
    uint32_t height_{0};
    std::size_t stride_{0};
    std::size_t buffer_size_{0};
    void* mapped_data_{nullptr};
    int shm_fd_{-1};
    wayland::shm_pool_t shm_pool_;
    wayland::buffer_t buffer_;
    wayland::shm_format shm_format_{wayland::shm_format::xrgb8888};

    bool constraints_ready_{false};
    bool stopped_{false};
    uint32_t last_failure_reason_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
};

Capture::Capture(const std::string& output_description_substring, bool verbose)
    : impl_(std::make_unique<Impl>(output_description_substring, verbose)) {}

Capture::~Capture() = default;

bool Capture::capture(const Callback& callback) {
    return impl_->capture(callback);
}

} // namespace wl_quiklight
