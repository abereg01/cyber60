// g++ -std=c++17 -I../src test_light.cpp && ./a.out
#include <cstdio>
#include "light.h"
using namespace lb;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
    Config c; State s;
    // steps: clamp, wrap, toggle, auto-on
    s.brightness = 250; apply_step(s, 0, 20); CHECK(s.brightness == 255);
    s.brightness = 3; apply_step(s, 0, -20); CHECK(s.brightness == 0);
    s.on = 0; apply_step(s, 0, 4); CHECK(s.on == 1);
    s.on = 0; apply_step(s, 0, -4); CHECK(s.on == 0);
    s.hue = 355; apply_step(s, 2, 10); CHECK(s.hue == 5);
    s.hue = 3; apply_step(s, 2, -10); CHECK(s.hue == 353);
    s.cct = 6400; apply_step(s, 1, 500); CHECK(s.cct == 6500);
    s.cct = 2800; apply_step(s, 1, -500); CHECK(s.cct == 2700);
    s.on = 1; apply_step(s, 4, 0); CHECK(s.on == 0); apply_step(s, 4, 0); CHECK(s.on == 1);
    // white: warm end only warm, cool end only cool, middle both full-ish but capped by SUM_MAX
    s = State(); s.brightness = 255; s.cct = 2700;
    Out o = compute(s, c); CHECK(o.ww > 0.99f && o.cw == 0 && o.r == 0 && o.g == 0 && o.b == 0);
    s.cct = 6500; o = compute(s, c); CHECK(o.cw > 0.99f && o.ww == 0);
    s.cct = 4600; o = compute(s, c); CHECK(o.ww > 0.9f && o.cw > 0.9f);
    // colour: red, green, blue, never white LEDs
    s.mode = 1; s.sat = 255; s.hue = 0; o = compute(s, c); CHECK(o.r > 0.99f && o.g < 0.01f && o.b < 0.01f && o.ww == 0 && o.cw == 0);
    s.hue = 120; o = compute(s, c); CHECK(o.g > 0.99f && o.r < 0.01f && o.b < 0.01f);
    s.hue = 240; o = compute(s, c); CHECK(o.b > 0.99f && o.r < 0.01f && o.g < 0.01f);
    s.hue = 60; o = compute(s, c); CHECK(o.r > 0.99f && o.g > 0.99f && o.b < 0.01f);
    s.sat = 0; o = compute(s, c); CHECK(std::fabs(o.r - o.g) < 1e-4f && std::fabs(o.g - o.b) < 1e-4f && o.r > 0.3f);  // equal, scaled by the sum guard
    // sum guard: full RGB white = 3 channel-equivalents, must scale to <= SUM_MAX (in linear light)
    {
        Out w = compute(s, c);
        float lin = std::pow(w.r, 1 / 2.2f) + std::pow(w.g, 1 / 2.2f) + std::pow(w.b, 1 / 2.2f);
        CHECK(lin <= SUM_MAX + 0.01f);
    }
    // off, brightness 0, limit
    s = State(); s.on = 0; o = compute(s, c); CHECK(o.ww == 0 && o.cw == 0);
    s.on = 1; s.brightness = 0; o = compute(s, c); CHECK(o.ww == 0 && o.cw == 0);
    s.brightness = 255; s.cct = 2700; c.limit = 50; o = compute(s, c);
    CHECK(std::fabs(o.ww - std::pow(0.5f, 2.2f)) < 0.01f);
    // gamma makes half brightness dim in duty terms
    c = Config(); s.brightness = 128; o = compute(s, c); CHECK(o.ww > 0.15f && o.ww < 0.30f);
    CHECK(duty(1.0f) == 1023 && duty(0) == 0 && duty(2) == 1023);
    // wire format round trip and rejection
    State a; a.on = 1; a.mode = 1; a.brightness = 200; a.sat = 99; a.hue = 300; a.cct = 5000;
    uint8_t buf[8]; pack_state(a, buf);
    State b; CHECK(unpack_state(buf, 8, b)); CHECK(b.hue == 300 && b.cct == 5000 && b.brightness == 200 && b.sat == 99 && b.mode == 1);
    CHECK(!unpack_state(buf, 7, b)); CHECK(!unpack_state(buf, 9, b));
    buf[4] = 0x20; buf[5] = 0x03; buf[6] = 0x00; buf[7] = 0x00;  // hue 800, cct 0: clamped
    CHECK(unpack_state(buf, 8, b)); CHECK(b.hue == 800 % 360 && b.cct == 2700);
    Config d; d.limit = 5; d.fade_ms = 9999; d.gamma_x10 = 99; uint8_t cb[4]; pack_config(d, cb);
    Config e; CHECK(unpack_config(cb, 4, e)); CHECK(e.limit == 10 && e.fade_ms == 2000 && e.gamma_x10 == 30);
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
