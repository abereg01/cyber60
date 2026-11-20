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
    // white at the strip's own temperature: only W, nothing else
    s = State(); s.brightness = 255; s.cct = LB_WHITE_K;
    Out o = compute(s, c); CHECK(o.w > 0.99f && o.r < 0.01f && o.g < 0.01f && o.b < 0.01f);
    // warmer: W plus red/green tint, no blue; cooler: W plus blue, less red
    s.cct = 2700; o = compute(s, c); CHECK(o.w > 0.1f && o.r > 0.05f && o.b < 0.01f);
    Out warm = o;
    s.cct = 6500; o = compute(s, c); CHECK(o.w > 0.5f && o.b > 0.02f && o.r < 0.01f);
    CHECK(warm.r > o.r && o.b > warm.b);
    // colour: red, green, blue, never the white chip
    s.mode = 1; s.sat = 255; s.hue = 0; o = compute(s, c); CHECK(o.r > 0.99f && o.g < 0.01f && o.b < 0.01f && o.w == 0);
    s.hue = 120; o = compute(s, c); CHECK(o.g > 0.99f && o.r < 0.01f && o.b < 0.01f);
    s.hue = 240; o = compute(s, c); CHECK(o.b > 0.99f && o.r < 0.01f && o.g < 0.01f);
    s.hue = 60; o = compute(s, c); CHECK(o.r > 0.99f && o.g > 0.99f && o.b < 0.01f);
    s.sat = 0; o = compute(s, c); CHECK(std::fabs(o.r - o.g) < 1e-4f && std::fabs(o.g - o.b) < 1e-4f && o.r > 0.3f);
    // power guard: full RGB white (3 channels) stays within the current budget, in linear light
    {
        float lin = std::pow(o.r, 1 / 2.2f) + std::pow(o.g, 1 / 2.2f) + std::pow(o.b, 1 / 2.2f);
        CHECK(lin <= SUM_MAX + 0.01f);
        float ma = lin * LB_LEDS * LB_MA_PER_CH;
        CHECK(ma <= LB_BUDGET_MA + 20);
        printf("full RGB white: %.0f mA of %d mA budget (sum cap %.2f)\n", ma, LB_BUDGET_MA, SUM_MAX);
    }
    // the default state is neutral white, half brightness
    { State d; CHECK(d.on && d.mode == 0 && d.cct == LB_WHITE_K && d.brightness == 128); }
    // off, brightness 0, limit
    s = State(); s.on = 0; o = compute(s, c); CHECK(o.w == 0);
    s.on = 1; s.brightness = 0; o = compute(s, c); CHECK(o.w == 0);
    s.mode = 0; s.brightness = 255; s.cct = LB_WHITE_K; c.limit = 50; o = compute(s, c);
    CHECK(std::fabs(o.w - std::pow(0.5f, 2.2f)) < 0.01f);
    // gamma makes half brightness dim in duty terms
    c = Config(); s.brightness = 128; o = compute(s, c); CHECK(o.w > 0.15f && o.w < 0.30f);
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
