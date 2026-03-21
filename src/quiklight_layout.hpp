#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wl_quiklight {

struct ColorRgb {
    uint8_t r{};
    uint8_t g{};
    uint8_t b{};
};

constexpr std::size_t kTopCount   = 29;
constexpr std::size_t kRightCount = 17;
constexpr std::size_t kLeftCount  = 17;
constexpr std::size_t kLedCount   = kTopCount + kRightCount + kLeftCount;

using LedFrame = std::array<ColorRgb, kLedCount>;

struct MappingConfig {
    bool reverse_top = true;
    bool reverse_right = true;
    bool reverse_left = false;
    bool swap_left_right = false;
    int top_offset = 0;
    int right_offset = 0;
    int left_offset = 0;
};

struct ColorEnhancementConfig {
    float saturation_boost = 1.60f;
    float value_boost = 1.14f;
    float gamma = 0.93f;
    float min_saturation = 0.18f;
    float hue_shift_degrees = 0.0f;
};

inline ColorEnhancementConfig& color_enhancement_config_storage() {
    static ColorEnhancementConfig cfg{};
    return cfg;
}

inline void set_color_enhancement_config(const ColorEnhancementConfig& cfg) {
    color_enhancement_config_storage() = cfg;
}

inline const ColorEnhancementConfig& get_color_enhancement_config() {
    return color_enhancement_config_storage();
}

inline bool frames_equal(const LedFrame& a, const LedFrame& b) {
    return std::memcmp(a.data(), b.data(), sizeof(LedFrame)) == 0;
}

inline int positive_mod(int value, int mod) {
    if (mod <= 0) {
        return 0;
    }
    int r = value % mod;
    return r < 0 ? r + mod : r;
}

template <std::size_t N>
inline void reverse_segment(std::array<ColorRgb, N>& seg) {
    std::reverse(seg.begin(), seg.end());
}

template <std::size_t N>
inline std::array<ColorRgb, N> rotate_segment(const std::array<ColorRgb, N>& seg, int offset) {
    std::array<ColorRgb, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const int src_idx = positive_mod(static_cast<int>(i) - offset, static_cast<int>(N));
        out[i] = seg[static_cast<std::size_t>(src_idx)];
    }
    return out;
}

inline float clamp01(float x) {
    return std::max(0.0f, std::min(1.0f, x));
}

inline uint8_t clamp_u8(float x) {
    if (x < 0.0f) return 0;
    if (x > 255.0f) return 255;
    return static_cast<uint8_t>(x + 0.5f);
}

inline ColorRgb blend_color(const ColorRgb& prev, const ColorRgb& next, float smoothing) {
    const float alpha = clamp01(1.0f - smoothing);
    return ColorRgb{
        clamp_u8(prev.r + (next.r - prev.r) * alpha),
        clamp_u8(prev.g + (next.g - prev.g) * alpha),
        clamp_u8(prev.b + (next.b - prev.b) * alpha)
    };
}

inline LedFrame smooth_frame(const LedFrame& prev, const LedFrame& next, float smoothing) {
    if (smoothing <= 0.0f) {
        return next;
    }
    LedFrame out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = blend_color(prev[i], next[i], smoothing);
    }
    return out;
}

inline ColorRgb enhance_color(ColorRgb in) {
    float r = in.r / 255.0f;
    float g = in.g / 255.0f;
    float b = in.b / 255.0f;

    const float maxc = std::max({r, g, b});
    const float minc = std::min({r, g, b});
    const float delta = maxc - minc;

    float h = 0.0f;
    float s = (maxc <= 0.0001f) ? 0.0f : (delta / maxc);
    float v = maxc;

    if (delta > 0.0001f) {
        if (maxc == r) {
            h = std::fmod(((g - b) / delta), 6.0f);
        } else if (maxc == g) {
            h = ((b - r) / delta) + 2.0f;
        } else {
            h = ((r - g) / delta) + 4.0f;
        }
        h /= 6.0f;
        if (h < 0.0f) {
            h += 1.0f;
        }
    }

    const auto& cfg = get_color_enhancement_config();

    s = clamp01(std::max(s, cfg.min_saturation * s) * cfg.saturation_boost);
    v = clamp01(std::pow(v, cfg.gamma) * cfg.value_boost);
    h += cfg.hue_shift_degrees / 360.0f;
    h -= std::floor(h);

    float rr = 0.0f, gg = 0.0f, bb = 0.0f;

    if (s <= 0.0001f) {
        rr = gg = bb = v;
    } else {
        const float hh = h * 6.0f;
        const int i = static_cast<int>(std::floor(hh)) % 6;
        const float f = hh - std::floor(hh);
        const float p = v * (1.0f - s);
        const float q = v * (1.0f - s * f);
        const float t = v * (1.0f - s * (1.0f - f));

        switch (i) {
            case 0: rr = v; gg = t; bb = p; break;
            case 1: rr = q; gg = v; bb = p; break;
            case 2: rr = p; gg = v; bb = t; break;
            case 3: rr = p; gg = q; bb = v; break;
            case 4: rr = t; gg = p; bb = v; break;
            default: rr = v; gg = p; bb = q; break;
        }
    }

    return ColorRgb{
        clamp_u8(rr * 255.0f),
        clamp_u8(gg * 255.0f),
        clamp_u8(bb * 255.0f)
    };
}

inline ColorRgb average_region(
    uint32_t width,
    uint32_t height,
    const uint32_t* pixels,
    uint32_t x0,
    uint32_t y0,
    uint32_t x1,
    uint32_t y1)
{
    x0 = std::min(x0, width);
    x1 = std::min(x1, width);
    y0 = std::min(y0, height);
    y1 = std::min(y1, height);

    if (x0 >= x1 || y0 >= y1) {
        return {};
    }

    uint64_t sum_r = 0;
    uint64_t sum_g = 0;
    uint64_t sum_b = 0;
    uint64_t count = 0;

    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            const uint32_t p = pixels[static_cast<std::size_t>(y) * width + x];
            const uint8_t b = static_cast<uint8_t>((p >> 0)  & 0xFF);
            const uint8_t g = static_cast<uint8_t>((p >> 8)  & 0xFF);
            const uint8_t r = static_cast<uint8_t>((p >> 16) & 0xFF);
            sum_r += r;
            sum_g += g;
            sum_b += b;
            ++count;
        }
    }

    if (count == 0) {
        return {};
    }

    ColorRgb avg{
        static_cast<uint8_t>(sum_r / count),
        static_cast<uint8_t>(sum_g / count),
        static_cast<uint8_t>(sum_b / count)
    };

    return enhance_color(avg);
}

inline void compute_colors(
    uint32_t width,
    uint32_t height,
    const uint32_t* pixels,
    LedFrame& out)
{
    if (!pixels || width == 0 || height == 0) {
        out.fill({});
        return;
    }

    const uint32_t depth = std::max<uint32_t>(1, static_cast<uint32_t>(0.02 * (width + height)));

    std::array<ColorRgb, kRightCount> right{};
    std::array<ColorRgb, kTopCount> top{};
    std::array<ColorRgb, kLeftCount> left{};

    for (std::size_t i = 0; i < kRightCount; ++i) {
        const uint32_t y0 = static_cast<uint32_t>((static_cast<uint64_t>(i) * height) / kRightCount);
        const uint32_t y1 = static_cast<uint32_t>((static_cast<uint64_t>(i + 1) * height) / kRightCount);
        right[i] = average_region(width, height, pixels,
                                  width > depth ? width - depth : 0, y0,
                                  width, y1);
    }

    for (std::size_t i = 0; i < kTopCount; ++i) {
        const uint32_t x0 = static_cast<uint32_t>((static_cast<uint64_t>(i) * width) / kTopCount);
        const uint32_t x1 = static_cast<uint32_t>((static_cast<uint64_t>(i + 1) * width) / kTopCount);
        top[i] = average_region(width, height, pixels,
                                x0, 0,
                                x1, std::min(depth, height));
    }

    for (std::size_t i = 0; i < kLeftCount; ++i) {
        const uint32_t y0 = static_cast<uint32_t>((static_cast<uint64_t>(i) * height) / kLeftCount);
        const uint32_t y1 = static_cast<uint32_t>((static_cast<uint64_t>(i + 1) * height) / kLeftCount);
        left[i] = average_region(width, height, pixels,
                                 0, y0,
                                 std::min(depth, width), y1);
    }

    for (std::size_t i = 0; i < kRightCount; ++i) {
        out[i] = right[i];
    }
    for (std::size_t i = 0; i < kTopCount; ++i) {
        out[kRightCount + i] = top[i];
    }
    for (std::size_t i = 0; i < kLeftCount; ++i) {
        out[kRightCount + kTopCount + i] = left[i];
    }
}

inline LedFrame remap_frame(const LedFrame& in, const MappingConfig& cfg) {
    std::array<ColorRgb, kRightCount> right{};
    std::array<ColorRgb, kTopCount> top{};
    std::array<ColorRgb, kLeftCount> left{};

    for (std::size_t i = 0; i < kRightCount; ++i) {
        right[i] = in[i];
    }
    for (std::size_t i = 0; i < kTopCount; ++i) {
        top[i] = in[kRightCount + i];
    }
    for (std::size_t i = 0; i < kLeftCount; ++i) {
        left[i] = in[kRightCount + kTopCount + i];
    }

    if (cfg.reverse_right) {
        reverse_segment(right);
    }
    if (cfg.reverse_top) {
        reverse_segment(top);
    }
    if (cfg.reverse_left) {
        reverse_segment(left);
    }

    const auto right_rot = rotate_segment(right, cfg.right_offset);
    const auto top_rot   = rotate_segment(top, cfg.top_offset);
    const auto left_rot  = rotate_segment(left, cfg.left_offset);

    LedFrame out{};

    if (!cfg.swap_left_right) {
        for (std::size_t i = 0; i < kRightCount; ++i) {
            out[i] = right_rot[i];
        }
        for (std::size_t i = 0; i < kTopCount; ++i) {
            out[kRightCount + i] = top_rot[i];
        }
        for (std::size_t i = 0; i < kLeftCount; ++i) {
            out[kRightCount + kTopCount + i] = left_rot[i];
        }
    } else {
        for (std::size_t i = 0; i < kLeftCount; ++i) {
            out[i] = left_rot[i];
        }
        for (std::size_t i = 0; i < kTopCount; ++i) {
            out[kLeftCount + i] = top_rot[i];
        }
        for (std::size_t i = 0; i < kRightCount; ++i) {
            out[kLeftCount + kTopCount + i] = right_rot[i];
        }
    }

    return out;
}

} // namespace wl_quiklight
