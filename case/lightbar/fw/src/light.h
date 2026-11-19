// Pure light logic (no Arduino, no BLE): state, steps, colour -> five PWM duties.
// Tested on the host: fw/test/test_light.cpp
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lb {

constexpr int CCT_MIN = 2700, CCT_MAX = 6500;
constexpr int PWM_BITS = 10, PWM_MAX = (1 << PWM_BITS) - 1;
constexpr float SUM_MAX = 2.0f;  // channel-equivalents on at once, protects the rail

struct State {
    uint8_t on = 1, mode = 0, brightness = 80, sat = 255;
    uint16_t hue = 30, cct = 3000;
};
struct Config {
    uint8_t limit = 100;      // % ceiling on brightness
    uint16_t fade_ms = 250;
    uint8_t gamma_x10 = 22;
};
struct Out { float r = 0, g = 0, b = 0, ww = 0, cw = 0; };  // 0..1, already gamma-corrected

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

// Target output for a state. Linear light first, then gamma, so 10 % brightness looks like 10 %.
inline Out compute(const State& s, const Config& c) {
    Out o;
    if (!s.on) return o;
    float master = s.brightness / 255.0f * c.limit / 100.0f;
    float lin[5] = {0, 0, 0, 0, 0};
    if (s.mode == 1) {
        hsv((float)s.hue, s.sat / 255.0f, lin[0], lin[1], lin[2]);
    } else {
        float t = (float)(s.cct - CCT_MIN) / (CCT_MAX - CCT_MIN);
        lin[3] = std::min(1.0f, 2.0f * (1.0f - t));  // warm
        lin[4] = std::min(1.0f, 2.0f * t);           // cool
    }
    float sum = lin[0] + lin[1] + lin[2] + lin[3] + lin[4];
    float k = master * (sum * master > SUM_MAX ? SUM_MAX / (sum * master) : 1.0f);
    float gm = c.gamma_x10 / 10.0f;
    float* dst[5] = {&o.r, &o.g, &o.b, &o.ww, &o.cw};
    for (int i = 0; i < 5; i++) *dst[i] = std::pow(std::clamp(lin[i] * k, 0.0f, 1.0f), gm);
    return o;
}

inline uint32_t duty(float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * PWM_MAX); }

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
