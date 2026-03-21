#pragma once

#include "src/quiklight_layout.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <hidapi/hidapi.h>

namespace wl_quiklight {

struct HidDeviceInfo {
    std::string path;
    uint16_t vendor_id{};
    uint16_t product_id{};
    std::string manufacturer;
    std::string product;
    std::string serial;
};

class QuiklightHid {
public:
    QuiklightHid(uint16_t vid,
                 uint16_t pid,
                 uint8_t brightness,
                 const std::string& forced_path = {});
    ~QuiklightHid();

    QuiklightHid(const QuiklightHid&) = delete;
    QuiklightHid& operator=(const QuiklightHid&) = delete;

    static std::vector<HidDeviceInfo> listDevices();

    bool sendFrame(const LedFrame& frame);

private:
    void open();
    void initialize();

    bool sendPacket(const std::vector<uint8_t>& packet);

    std::vector<uint8_t> buildSimpleRBCommand(uint8_t msg_id,
                                              uint8_t action,
                                              const uint8_t* payload,
                                              std::size_t payload_len) const;
    std::vector<uint8_t> buildSetOpenUrl() const;
    std::vector<uint8_t> buildSetBrightness(uint8_t value) const;
    std::vector<uint8_t> buildSetSectionLedDefault() const;
    std::vector<uint8_t> buildSyncScreenPacket(const LedFrame& frame) const;

private:
    hid_device* dev_{nullptr};
    uint16_t vid_{0x1A86};
    uint16_t pid_{0xFE07};
    uint8_t brightness_{255};
    std::string forced_path_;
    mutable uint8_t next_msg_id_{1};
};

} // namespace wl_quiklight
