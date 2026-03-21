#include "src/quiklight_hid.hpp"
#include "wayland/capture.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using wl_quiklight::Capture;
using wl_quiklight::ColorEnhancementConfig;
using wl_quiklight::ColorRgb;
using wl_quiklight::LedFrame;
using wl_quiklight::MappingConfig;
using wl_quiklight::QuiklightHid;
using wl_quiklight::frames_equal;
using wl_quiklight::remap_frame;
using wl_quiklight::set_color_enhancement_config;
using wl_quiklight::smooth_frame;

volatile std::sig_atomic_t g_running = 1;

struct NamedColor {
    const char* name;
    const char* abbr;
    ColorRgb rgb;
};

const std::vector<NamedColor> kPalette = {
    {"red", "R", {255, 0, 0}},
    {"green", "G", {0, 255, 0}},
    {"blue", "B", {0, 0, 255}},
    {"yellow", "Y", {255, 255, 0}},
    {"cyan", "C", {0, 255, 255}},
    {"magenta", "M", {255, 0, 255}},
    {"white", "W", {255, 255, 255}},
    {"orange", "O", {255, 128, 0}},
    {"pink", "P", {255, 105, 180}},
    {"lime", "L", {128, 255, 0}},
};

void on_signal(int) {
    g_running = 0;
}

void usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " -o OUTPUT [options]\n\n"
        << "General:\n"
        << "  -o, --output NAME          Wayland output description substring\n"
        << "      --config PATH          Read configuration file (launcher handles it)\n"
        << "      --dump-default-config  Print a sample config file and exit\n"
        << "      --list-devices         List HID devices and exit\n"
        << "      --device PATH          Use a specific HID path, e.g. /dev/hidraw0\n"
        << "      --vid HEX              USB VID (default 0x1A86)\n"
        << "      --pid HEX              USB PID (default 0xFE07)\n"
        << "      --brightness N         0..255 (default 255)\n"
        << "      --fps N                Capture/send rate cap, 1..240 (default 60)\n"
        << "      --smoothing F          Frame smoothing, 0.0..0.99 (default 0.35)\n"
        << "      --verbose              Verbose logs\n"
        << "  -h, --help                 Show help\n\n"
        << "Mapping:\n"
        << "      --reverse-top 0|1      Reverse top segment (default 1)\n"
        << "      --reverse-right 0|1    Reverse right segment (default 1)\n"
        << "      --reverse-left 0|1     Reverse left segment (default 0)\n"
        << "      --swap-left-right 0|1  Swap right/left output segments (default 0)\n"
        << "      --top-offset N         Top rotation offset (default 0)\n"
        << "      --right-offset N       Right rotation offset (default 0)\n"
        << "      --left-offset N        Left rotation offset (default 0)\n\n"
        << "Capture:\n"
        << "      --idle-sleep-ms N      Optional extra sleep after each loop (default 0)\n\n"
        << "Color enhancement:\n"
        << "      --saturation-boost F   Saturation multiplier (default 1.60)\n"
        << "      --value-boost F        Brightness multiplier (default 1.14)\n"
        << "      --gamma F              Gamma correction (default 0.93)\n"
        << "      --min-saturation F     Minimum saturation floor (default 0.18)\n"
        << "      --hue-shift F          Hue rotation in degrees, -180..180 (default 0)\n\n"
        << "Calibration:\n"
        << "      --random-sequence      Send one static random color sequence to the whole strip and keep it displayed\n"
        << "      --random-seed N        Seed used with --random-sequence (default: random)\n";
}

int parse_int(const char* s) {
    char* end = nullptr;
    long v = std::strtol(s, &end, 0);
    if (!s || *s == '\0' || !end || *end != '\0') {
        throw std::runtime_error(std::string("Invalid integer: ") + (s ? s : "<null>"));
    }
    return static_cast<int>(v);
}

float parse_float(const char* s) {
    char* end = nullptr;
    float v = std::strtof(s, &end);
    if (!s || *s == '\0' || !end || *end != '\0') {
        throw std::runtime_error(std::string("Invalid float: ") + (s ? s : "<null>"));
    }
    return v;
}

void print_devices() {
    const auto devices = QuiklightHid::listDevices();
    if (devices.empty()) {
        std::cout << "No HID devices found.\n";
        return;
    }

    for (const auto& d : devices) {
        std::cout
            << d.path
            << "  vid=0x" << std::hex << std::setw(4) << std::setfill('0') << d.vendor_id
            << " pid=0x" << std::setw(4) << d.product_id << std::dec;
        if (!d.manufacturer.empty()) {
            std::cout << "  manufacturer=\"" << d.manufacturer << "\"";
        }
        if (!d.product.empty()) {
            std::cout << "  product=\"" << d.product << "\"";
        }
        if (!d.serial.empty()) {
            std::cout << "  serial=\"" << d.serial << "\"";
        }
        std::cout << "\n";
    }
}

LedFrame build_random_sequence(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, kPalette.size() - 1);

    LedFrame frame{};
    for (auto& led : frame) {
        led = kPalette[pick(rng)].rgb;
    }
    return frame;
}

const NamedColor* find_named(const ColorRgb& c) {
    for (const auto& nc : kPalette) {
        if (nc.rgb.r == c.r && nc.rgb.g == c.g && nc.rgb.b == c.b) {
            return &nc;
        }
    }
    return nullptr;
}

void print_random_sequence(const LedFrame& frame, uint32_t seed) {
    std::cout << "Random sequence seed: " << seed << "\n";
    std::cout << "Compact sequence (LED 1 -> " << frame.size() << "):" << "\n";
    for (const auto& c : frame) {
        const auto* nc = find_named(c);
        std::cout << (nc ? nc->abbr : "?");
    }
    std::cout << "\n\nLegend:\n";
    for (const auto& nc : kPalette) {
        std::cout << "  " << nc.abbr << " = " << nc.name << "\n";
    }
    std::cout << "\nDetailed list:\n";
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const auto* nc = find_named(frame[i]);
        std::cout << "  LED " << (i + 1) << ": " << (nc ? nc->name : "unknown")
                  << " (" << static_cast<int>(frame[i].r) << ","
                  << static_cast<int>(frame[i].g) << ","
                  << static_cast<int>(frame[i].b) << ")\n";
    }
    std::cout << std::flush;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::string output_match;
    std::string device_path;
    uint16_t vid = 0x1A86;
    uint16_t pid = 0xFE07;
    int brightness = 255;
    int idle_sleep_ms = 0;
    int fps = 60;
    float smoothing = 0.35f;
    bool verbose = false;
    bool list_devices = false;
    bool random_sequence = false;
    bool have_random_seed = false;
    uint32_t random_seed = 0;
    MappingConfig mapping;
    ColorEnhancementConfig color_cfg;

    static option long_options[] = {
        {"output", required_argument, nullptr, 'o'},
        {"config", required_argument, nullptr, 994},
        {"dump-default-config", no_argument, nullptr, 993},
        {"list-devices", no_argument, nullptr, 995},
        {"device", required_argument, nullptr, 996},
        {"vid", required_argument, nullptr, 1000},
        {"pid", required_argument, nullptr, 1001},
        {"brightness", required_argument, nullptr, 1002},
        {"reverse-top", required_argument, nullptr, 1003},
        {"reverse-right", required_argument, nullptr, 1004},
        {"reverse-left", required_argument, nullptr, 1005},
        {"swap-left-right", required_argument, nullptr, 1006},
        {"top-offset", required_argument, nullptr, 1007},
        {"right-offset", required_argument, nullptr, 1008},
        {"left-offset", required_argument, nullptr, 1009},
        {"idle-sleep-ms", required_argument, nullptr, 1010},
        {"verbose", no_argument, nullptr, 1011},
        {"random-sequence", no_argument, nullptr, 1012},
        {"random-seed", required_argument, nullptr, 1013},
        {"saturation-boost", required_argument, nullptr, 1014},
        {"value-boost", required_argument, nullptr, 1015},
        {"gamma", required_argument, nullptr, 1016},
        {"min-saturation", required_argument, nullptr, 1017},
        {"fps", required_argument, nullptr, 1018},
        {"smoothing", required_argument, nullptr, 1019},
        {"hue-shift", required_argument, nullptr, 1020},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    while (true) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "ho:", long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'o': output_match = optarg; break;
        case 993:
            std::cout
                << "# hypr-quiklight default config\n"
                << "# This file is read by the launcher script, not by the C++ binary directly.\n\n"
                << "output=Xiaomi\n"
                << "device=\n"
                << "brightness=255\n"
                << "fps=60\n"
                << "smoothing=0.35\n"
                << "reverse_top=1\n"
                << "reverse_right=1\n"
                << "reverse_left=0\n"
                << "swap_left_right=0\n"
                << "top_offset=0\n"
                << "right_offset=0\n"
                << "left_offset=0\n"
                << "idle_sleep_ms=0\n"
                << "verbose=0\n\n"
                << "# Color enhancement\n"
                << "saturation_boost=1.60\n"
                << "value_boost=1.14\n"
                << "gamma=0.93\n"
                << "min_saturation=0.18\n"
                << "hue_shift=0\n";
            return 0;
        case 994:
            break; // launcher handles config file
        case 995: list_devices = true; break;
        case 996: device_path = optarg; break;
        case 1000: vid = static_cast<uint16_t>(parse_int(optarg)); break;
        case 1001: pid = static_cast<uint16_t>(parse_int(optarg)); break;
        case 1002: brightness = parse_int(optarg); break;
        case 1003: mapping.reverse_top = parse_int(optarg) != 0; break;
        case 1004: mapping.reverse_right = parse_int(optarg) != 0; break;
        case 1005: mapping.reverse_left = parse_int(optarg) != 0; break;
        case 1006: mapping.swap_left_right = parse_int(optarg) != 0; break;
        case 1007: mapping.top_offset = parse_int(optarg); break;
        case 1008: mapping.right_offset = parse_int(optarg); break;
        case 1009: mapping.left_offset = parse_int(optarg); break;
        case 1010: idle_sleep_ms = parse_int(optarg); break;
        case 1011: verbose = true; break;
        case 1012: random_sequence = true; break;
        case 1013: random_seed = static_cast<uint32_t>(parse_int(optarg)); have_random_seed = true; break;
        case 1014: color_cfg.saturation_boost = parse_float(optarg); break;
        case 1015: color_cfg.value_boost = parse_float(optarg); break;
        case 1016: color_cfg.gamma = parse_float(optarg); break;
        case 1017: color_cfg.min_saturation = parse_float(optarg); break;
        case 1018: fps = parse_int(optarg); break;
        case 1019: smoothing = parse_float(optarg); break;
        case 1020: color_cfg.hue_shift_degrees = parse_float(optarg); break;
        case 'h':
        default:
            usage(argv[0]);
            return c == 'h' ? 0 : 1;
        }
    }

    if (brightness < 0 || brightness > 255) {
        std::cerr << "brightness must be in 0..255\n";
        return 1;
    }
    if (fps < 1 || fps > 240) {
        std::cerr << "fps must be in 1..240\n";
        return 1;
    }
    if (smoothing < 0.0f || smoothing >= 1.0f) {
        std::cerr << "smoothing must be in 0.0..0.99\n";
        return 1;
    }

    if (hid_init() != 0) {
        std::cerr << "hid_init failed\n";
        return 1;
    }

    set_color_enhancement_config(color_cfg);

    if (list_devices) {
        print_devices();
        hid_exit();
        return 0;
    }

    try {
        QuiklightHid hid(vid, pid, static_cast<uint8_t>(brightness), device_path);

        if (random_sequence) {
            if (!have_random_seed) {
                random_seed = std::random_device{}();
            }
            LedFrame frame = build_random_sequence(random_seed);
            print_random_sequence(frame, random_seed);

            std::cout << "\nSequence displayed. Observe the strip, then stop with Ctrl+C.\n";

            while (g_running) {
                if (!hid.sendFrame(frame)) {
                    std::cerr << "Failed to send random sequence to Quiklight HID device\n";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            hid_exit();
            return 0;
        }

        if (output_match.empty()) {
            usage(argv[0]);
            hid_exit();
            return 1;
        }

        Capture capture(output_match, verbose);
        LedFrame raw{};
        LedFrame remapped{};
        LedFrame smoothed{};
        LedFrame last_sent{};
        bool have_smoothed = false;
        bool have_last_sent = false;
        const auto frame_period = std::chrono::milliseconds(1000 / fps);

        while (g_running) {
            const auto frame_start = std::chrono::steady_clock::now();
            const bool ok = capture.capture([&](uint32_t width, uint32_t height, uint32_t /*format*/, const uint8_t* data) {
                wl_quiklight::compute_colors(width, height, reinterpret_cast<const uint32_t*>(data), raw);
                remapped = remap_frame(raw, mapping);
            });

            if (!ok) {
                if (verbose) {
                    std::cerr << "Capture returned false; retrying\n";
                }
            } else {
                if (!have_smoothed) {
                    smoothed = remapped;
                    have_smoothed = true;
                } else {
                    smoothed = smooth_frame(smoothed, remapped, smoothing);
                }

                if (!have_last_sent || !frames_equal(smoothed, last_sent)) {
                    if (!hid.sendFrame(smoothed)) {
                        std::cerr << "Failed to send frame to Quiklight HID device\n";
                    } else {
                        last_sent = smoothed;
                        have_last_sent = true;
                    }
                }
            }

            const auto elapsed = std::chrono::steady_clock::now() - frame_start;
            if (elapsed < frame_period) {
                std::this_thread::sleep_for(frame_period - elapsed);
            }
            if (idle_sleep_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(idle_sleep_ms));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        hid_exit();
        return 1;
    }

    hid_exit();
    return 0;
}
