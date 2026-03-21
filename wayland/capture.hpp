#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace wl_quiklight {

class Capture {
public:
    using Callback = std::function<void(uint32_t width, uint32_t height, uint32_t format, const uint8_t* data)>;

    explicit Capture(const std::string& output_description_substring, bool verbose = false);
    ~Capture();

    bool capture(const Callback& callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wl_quiklight
