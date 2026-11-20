// Pure light logic (no Arduino, no BLE): state, steps, colour -> R, G, B, W levels for an SK6812 RGBW strip.
// Tested on the host: fw/test/test_light.cpp
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lb {

#ifndef LB_LEDS
#define LB_LEDS 42        // 60 LED/m x 0.7 m
#endif
#ifndef LB_WHITE_K
#define LB_WHITE_K 4500   // colour temperature of the strip's white chip (WW ~3000, NW ~4500, CW ~6500)
#endif
#ifndef LB_MA_PER_CH
#define LB_MA_PER_CH 15   // per colour chip at full (SK6812: about 12-18 mA)
#endif
#ifndef LB_BUDGET_MA
#define LB_BUDGET_MA 1800 // keep the strip under this; a 3 A USB-C charger has headroom for the C3
#endif

constexpr int CCT_MIN = 2700, CCT_MAX = 6500;
constexpr float SUM_MAX = (float)LB_BUDGET_MA / (LB_LEDS * LB_MA_PER_CH);  // channels at full that fit the budget (2.86 here)

struct State {
    uint8_t on = 1, mode = 0, brightness = 128, sat = 255;
    uint16_t hue = 30, cct = LB_WHITE_K;  // first boot: neutral white at half brightness
};
struct Config {
    uint8_t limit = 100;      // % ceiling on brightness
    uint16_t fade_ms = 250;
    uint8_t gamma_x10 = 22;
};
struct Out { float r = 0, g = 0, b = 0, w = 0; };  // 0..1, already gamma-corrected

inline void clamp_state(State& s) {
    s.on = s.on ? 1 : 0;
    s.mode = s.mode ? 1 : 0;
    s.hue %= 360;
    s.cct = (uint16_t)std::clamp<int>(s.cct, CCT_MIN, CCT_MAX);
}
inline void clamp_config(Config& c) {
    c.limit = (uint8_t)std::clamp<int>(c.limit, 10, 100);
    c.fade_ms = (uint16_t)std::min<int>(c.fade_ms, 2000);
    c.gamma_x10 = (uint8_t)std::clamp<int>(c.gamma_x10, 10, 30);
}

// Relative change. target: 0 brightness, 1 cct, 2 hue, 3 sat, 4 power toggle.
inline void apply_step(State& s, uint8_t target, int16_t d) {
    switch (target) {
        case 0:
            s.brightness = (uint8_t)std::clamp<int>(s.brightness + d, 0, 255);
            if (d > 0) s.on = 1;
            break;
        case 1: s.cct = (uint16_t)std::clamp<int>(s.cct + d, CCT_MIN, CCT_MAX); break;
        case 2: s.hue = (uint16_t)(((s.hue + d) % 360 + 360) % 360); break;
        case 3: s.sat = (uint8_t)std::clamp<int>(s.sat + d, 0, 255); break;
        case 4: s.on = !s.on; break;
        default: break;
    }
}

inline void hsv(float h, float sat, float& r, float& g, float& b) {  // v = 1
    float hh = std::fmod(h, 360.0f) / 60.0f;
    int i = (int)hh;
    float f = hh - i, p = 1 - sat, q = 1 - sat * f, t = 1 - sat * (1 - f);
    switch (i % 6) {
        case 0: r = 1; g = t; b = p; break;
        case 1: r = q; g = 1; b = p; break;
        case 2: r = p; g = 1; b = t; break;
        case 3: r = p; g = q; b = 1; break;
        case 4: r = t; g = p; b = 1; break;
        default: r = 1; g = p; b = q; break;
    }
}

// Black-body colour for a temperature, as linear-ish RGB with the largest channel = 1
// (Tanner Helland's approximation; good enough to tint a white LED).
inline void blackbody(float k, float& r, float& g, float& b) {
    float t = k / 100.0f;
    float R = t <= 66 ? 255.0f : 329.698727446f * std::pow(t - 60, -0.1332047592f);
    float G = t <= 66 ? 99.4708025861f * std::log(t) - 161.1195681661f
                      : 288.1221695283f * std::pow(t - 60, -0.0755148492f);
    float B = t >= 66 ? 255.0f : (t <= 19 ? 0.0f : 138.5177312231f * std::log(t - 10) - 305.0447927307f);
    r = std::clamp(R, 0.0f, 255.0f) / 255.0f; g = std::clamp(G, 0.0f, 255.0f) / 255.0f;
    b = std::clamp(B, 0.0f, 255.0f) / 255.0f;
}

// Target output for a state. Linear light first, then gamma, so 10 % brightness looks like 10 %.
// White mode: the strip has ONE white chip (LB_WHITE_K). At that temperature only W is lit; for any other
// temperature as much W as fits is used and R/G/B add the missing tint (warmer: red, cooler: blue).
// Colour mode: R, G, B only.
inline Out compute(const State& s, const Config& c) {
    Out o;
    if (!s.on) return o;
    float master = s.brightness / 255.0f * c.limit / 100.0f;
    float lin[4] = {0, 0, 0, 0};  // r g b w
    if (s.mode == 1) {
        hsv((float)s.hue, s.sat / 255.0f, lin[0], lin[1], lin[2]);
    } else {
        float T[3], N[3];
        blackbody((float)s.cct, T[0], T[1], T[2]);
        blackbody((float)LB_WHITE_K, N[0], N[1], N[2]);
        float a = 1.0f;
        for (int i = 0; i < 3; i++) a = std::min(a, T[i] / std::max(N[i], 1e-3f));
        lin[3] = a;
        for (int i = 0; i < 3; i++) lin[i] = std::max(0.0f, T[i] - a * N[i]);
    }
    float sum = lin[0] + lin[1] + lin[2] + lin[3];
    float k = master * (sum * master > SUM_MAX ? SUM_MAX / (sum * master) : 1.0f);
    float gm = c.gamma_x10 / 10.0f;
    float* dst[4] = {&o.r, &o.g, &o.b, &o.w};
    for (int i = 0; i < 4; i++) *dst[i] = std::pow(std::clamp(lin[i] * k, 0.0f, 1.0f), gm);
    return o;
}

// Little-endian wire format
inline void pack_state(const State& s, uint8_t* p) {
    p[0] = s.on; p[1] = s.mode; p[2] = s.brightness; p[3] = s.sat;
    p[4] = s.hue & 0xff; p[5] = s.hue >> 8; p[6] = s.cct & 0xff; p[7] = s.cct >> 8;
}
inline bool unpack_state(const uint8_t* p, size_t n, State& s) {
    if (n != 8) return false;
    s.on = p[0]; s.mode = p[1]; s.brightness = p[2]; s.sat = p[3];
    s.hue = (uint16_t)(p[4] | p[5] << 8); s.cct = (uint16_t)(p[6] | p[7] << 8);
    clamp_state(s);
    return true;
}
inline void pack_config(const Config& c, uint8_t* p) {
    p[0] = c.limit; p[1] = c.fade_ms & 0xff; p[2] = c.fade_ms >> 8; p[3] = c.gamma_x10;
}
inline bool unpack_config(const uint8_t* p, size_t n, Config& c) {
    if (n != 4) return false;
    c.limit = p[0]; c.fade_ms = (uint16_t)(p[1] | p[2] << 8); c.gamma_x10 = p[3];
    clamp_config(c);
    return true;
}

}  // namespace lb
