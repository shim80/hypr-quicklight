#include "src/quiklight_hid.hpp"

#include <algorithm>
#include <codecvt>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace wl_quiklight {
namespace {

std::string wide_to_utf8(const wchar_t* w) {
    if (!w) {
        return {};
    }
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(w);
}

uint8_t checksum8(const std::vector<uint8_t>& data) {
    uint32_t sum = 0;
    for (uint8_t b : data) {
        sum += b;
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string h(haystack.size(), '\0');
    std::transform(haystack.begin(), haystack.end(), h.begin(), lower);
    std::string n(needle.size(), '\0');
    std::transform(needle.begin(), needle.end(), n.begin(), lower);
    return h.find(n) != std::string::npos;
}

} // namespace

std::vector<HidDeviceInfo> QuiklightHid::listDevices() {
    std::vector<HidDeviceInfo> out;

    hid_device_info* head = hid_enumerate(0x0, 0x0);
    for (hid_device_info* cur = head; cur; cur = cur->next) {
        HidDeviceInfo d;
        if (cur->path) {
            d.path = cur->path;
        }
        d.vendor_id = cur->vendor_id;
        d.product_id = cur->product_id;
        d.manufacturer = wide_to_utf8(cur->manufacturer_string);
        d.product = wide_to_utf8(cur->product_string);
        d.serial = wide_to_utf8(cur->serial_number);
        out.push_back(std::move(d));
    }
    hid_free_enumeration(head);
    return out;
}

QuiklightHid::QuiklightHid(uint16_t vid,
                           uint16_t pid,
                           uint8_t brightness,
                           const std::string& forced_path)
    : vid_(vid), pid_(pid), brightness_(brightness), forced_path_(forced_path) {
    open();
    initialize();
}

QuiklightHid::~QuiklightHid() {
    if (dev_) {
        hid_close(dev_);
        dev_ = nullptr;
    }
}

void QuiklightHid::open() {
    if (!forced_path_.empty()) {
        dev_ = hid_open_path(forced_path_.c_str());
        if (!dev_) {
            throw std::runtime_error("Failed to open HID device at path: " + forced_path_);
        }
        return;
    }

    const auto devices = listDevices();
    const HidDeviceInfo* selected = nullptr;
    for (const auto& d : devices) {
        if (d.vendor_id != vid_ || d.product_id != pid_) {
            continue;
        }
        if (contains_case_insensitive(d.manufacturer, "ROBOBLOQ") || contains_case_insensitive(d.product, "USBHID")) {
            selected = &d;
            break;
        }
        if (!selected) {
            selected = &d;
        }
    }

    if (selected && !selected->path.empty()) {
        dev_ = hid_open_path(selected->path.c_str());
        if (dev_) {
            return;
        }
    }

    dev_ = hid_open(vid_, pid_, nullptr);
    if (!dev_) {
        std::ostringstream oss;
        oss << "Failed to open HID device vid=0x"
            << std::hex << std::setw(4) << std::setfill('0') << vid_
            << " pid=0x" << std::setw(4) << pid_;
        throw std::runtime_error(oss.str());
    }
}

bool QuiklightHid::sendFrame(const LedFrame& frame) {
    const auto packet = buildSyncScreenPacket(frame);
    return sendPacket(packet);
}

void QuiklightHid::initialize() {
    if (!sendPacket(buildSetOpenUrl())) {
        throw std::runtime_error("Failed to initialize Quiklight: setOpenUrl");
    }
    if (!sendPacket(buildSetBrightness(brightness_))) {
        throw std::runtime_error("Failed to initialize Quiklight: setBrightness");
    }
    if (!sendPacket(buildSetSectionLedDefault())) {
        throw std::runtime_error("Failed to initialize Quiklight: setSectionLED");
    }

    LedFrame black{};
    if (!sendFrame(black)) {
        throw std::runtime_error("Failed to initialize Quiklight: initial black frame");
    }
}

bool QuiklightHid::sendPacket(const std::vector<uint8_t>& packet) {
    std::size_t offset = 0;

    while (offset < packet.size()) {
        unsigned char report[65];
        std::memset(report, 0, sizeof(report));

        const std::size_t chunk = std::min<std::size_t>(64, packet.size() - offset);
        report[0] = 0x00;
        std::memcpy(report + 1, packet.data() + offset, chunk);

        const int rc = hid_write(dev_, report, sizeof(report));
        if (rc < 0) {
            return false;
        }

        offset += chunk;
    }

    return true;
}

std::vector<uint8_t> QuiklightHid::buildSimpleRBCommand(uint8_t msg_id,
                                                        uint8_t action,
                                                        const uint8_t* payload,
                                                        std::size_t payload_len) const {
    const std::size_t total_len = 2 + 1 + 1 + 1 + payload_len + 1;
    std::vector<uint8_t> out;
    out.reserve(total_len);

    out.push_back('R');
    out.push_back('B');
    out.push_back(static_cast<uint8_t>(total_len));
    out.push_back(msg_id);
    out.push_back(action);

    for (std::size_t i = 0; i < payload_len; ++i) {
        out.push_back(payload[i]);
    }

    out.push_back(checksum8(out));
    return out;
}

std::vector<uint8_t> QuiklightHid::buildSetOpenUrl() const {
    const uint8_t payload[1] = {0};
    return buildSimpleRBCommand(next_msg_id_++, 147, payload, sizeof(payload));
}

std::vector<uint8_t> QuiklightHid::buildSetBrightness(uint8_t value) const {
    const uint8_t payload[1] = {value};
    return buildSimpleRBCommand(next_msg_id_++, 135, payload, sizeof(payload));
}

std::vector<uint8_t> QuiklightHid::buildSetSectionLedDefault() const {
    const uint8_t payload[10] = {1, 85, 85, 85, 63, 64, 0, 0, 0, 254};
    return buildSimpleRBCommand(next_msg_id_++, 134, payload, sizeof(payload));
}

std::vector<uint8_t> QuiklightHid::buildSyncScreenPacket(const LedFrame& frame) const {
    const std::size_t payload_len = kLedCount * 5;
    const std::size_t total_len = 2 + 2 + 1 + 1 + payload_len + 1;

    std::vector<uint8_t> out;
    out.reserve(total_len);

    out.push_back('S');
    out.push_back('C');
    out.push_back(static_cast<uint8_t>((total_len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(total_len & 0xFF));
    out.push_back(next_msg_id_++);
    out.push_back(128);

    for (std::size_t i = 0; i < frame.size(); ++i) {
        const uint8_t n = static_cast<uint8_t>(i + 1);
        out.push_back(n);
        out.push_back(frame[i].r);
        out.push_back(frame[i].g);
        out.push_back(frame[i].b);
        out.push_back(n);
    }

    out.push_back(checksum8(out));
    return out;
}

} // namespace wl_quiklight
