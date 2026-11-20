// Monitor light bar, ESP32-C3. SK6812 RGBW strip on one data pin (via a 74AHCT125 level shifter), BLE peripheral.
// Protocol: ../../BLE-SPEC.md. Pure logic (state, steps, colour): light.h (host-tested).
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "light.h"

#define STR2(x) #x
#define STR(x) STR2(x)
#define FW_VERSION "0.2.0"
#define UUID(n) "6c7b" n "-8a3e-4f5e-9d2b-4c1ee1b0a001"
static const char* SVC_UUID = UUID("0001");
static const char* STATE_UUID = UUID("0002");
static const char* STEP_UUID = UUID("0003");
static const char* CONFIG_UUID = UUID("0004");
static const char* INFO_UUID = UUID("0005");

// Data pin to the level shifter (74AHCT125) and on to the strip's DIN. GPIO0 is not a strapping pin.
#ifndef LB_DATA_PIN
#define LB_DATA_PIN 0
#endif
#ifndef LB_ORDER
#define LB_ORDER (NEO_GRBW + NEO_KHZ800)   // SK6812 RGBW is normally GRBW; if red and green swap, use NEO_RGBW
#endif
static Adafruit_NeoPixel strip(LB_LEDS, LB_DATA_PIN, LB_ORDER);

static lb::State state;
static lb::Config config;
static float cur[4] = {0, 0, 0, 0};   // what the pins show now (0..1), eased towards the target
static lb::Out target;
static Preferences prefs;
static NimBLECharacteristic *chState, *chConfig;
static volatile bool stateDirty = false, notifyDirty = false;
static uint32_t lastChangeMs = 0, lastNotifyMs = 0;
static bool savePending = false;

static void retarget() { target = lb::compute(state, config); }

static void changed() {  // call after any state change, from the loop task only
    retarget();
    notifyDirty = true;
    savePending = true;
    lastChangeMs = millis();
}

static void publishState() {
    uint8_t b[8];
    lb::pack_state(state, b);
    chState->setValue(b, 8);
}

// BLE callbacks run on the NimBLE task: only queue work here, apply it in loop().
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static lb::State pendingState; static bool havePendingState = false;
static int32_t pendingDelta[4] = {0, 0, 0, 0}; static bool pendingToggle = false;
static lb::Config pendingConfig; static bool havePendingConfig = false;

class StateCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        auto v = c->getValue();
        lb::State s;
        if (!lb::unpack_state(v.data(), v.length(), s)) return;
        portENTER_CRITICAL(&mux); pendingState = s; havePendingState = true; portEXIT_CRITICAL(&mux);
    }
};
class StepCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        auto v = c->getValue();
        if (v.length() != 3) return;
        uint8_t t = v.data()[0];
        int16_t d = (int16_t)(v.data()[1] | v.data()[2] << 8);
        portENTER_CRITICAL(&mux);
        if (t == 4) pendingToggle = !pendingToggle;
        else if (t < 4) pendingDelta[t] += d;
        portEXIT_CRITICAL(&mux);
    }
};
class ConfigCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        auto v = c->getValue();
        lb::Config n;
        if (!lb::unpack_config(v.data(), v.length(), n)) return;
        portENTER_CRITICAL(&mux); pendingConfig = n; havePendingConfig = true; portEXIT_CRITICAL(&mux);
    }
};
class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo&) override {
        if (s->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) NimBLEDevice::startAdvertising();
        notifyDirty = true;  // give the new subscriber the current state
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { NimBLEDevice::startAdvertising(); }
};

static void loadSaved() {
    prefs.begin("lightbar", true);
    uint8_t b[8]; uint8_t cb[4];
    if (prefs.getBytes("state", b, 8) == 8) lb::unpack_state(b, 8, state);
    if (prefs.getBytes("config", cb, 4) == 4) lb::unpack_config(cb, 4, config);
    prefs.end();
}
static void save() {
    uint8_t b[8], cb[4];
    lb::pack_state(state, b); lb::pack_config(config, cb);
    prefs.begin("lightbar", false);
    prefs.putBytes("state", b, 8); prefs.putBytes("config", cb, 4);
    prefs.end();
}

void setup() {
    pinMode(LB_DATA_PIN, OUTPUT); digitalWrite(LB_DATA_PIN, LOW);
    strip.begin(); strip.clear(); strip.show();
    Serial.begin(115200);
    loadSaved();
    retarget();

    NimBLEDevice::init("Lightbar");
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCb());
    NimBLEService* svc = server->createService(SVC_UUID);
    chState = svc->createCharacteristic(STATE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    chState->setCallbacks(new StateCb());
    NimBLECharacteristic* chStep = svc->createCharacteristic(STEP_UUID, NIMBLE_PROPERTY::WRITE_NR);
    chStep->setCallbacks(new StepCb());
    chConfig = svc->createCharacteristic(CONFIG_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    chConfig->setCallbacks(new ConfigCb());
    NimBLECharacteristic* chInfo = svc->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ);
    chInfo->setValue(std::string("lightbar " FW_VERSION " sk6812-rgbw n=" STR(LB_LEDS)));
    publishState();
    uint8_t cb[4]; lb::pack_config(config, cb); chConfig->setValue(cb, 4);
    svc->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SVC_UUID);
    adv->setName("Lightbar");
    NimBLEDevice::startAdvertising();
    Serial.println("lightbar " FW_VERSION " up");
}

void loop() {
    static uint32_t lastTick = 0;
    uint32_t now = millis();
    if (now - lastTick < 10) { delay(1); return; }   // 100 Hz
    uint32_t dt = now - lastTick; lastTick = now;

    // 1. apply what BLE queued
    lb::State ps; int32_t dl[4]; bool tog, hs, hc; lb::Config pc;
    portENTER_CRITICAL(&mux);
    ps = pendingState; hs = havePendingState; havePendingState = false;
    memcpy(dl, pendingDelta, sizeof dl); memset(pendingDelta, 0, sizeof pendingDelta);
    tog = pendingToggle; pendingToggle = false;
    pc = pendingConfig; hc = havePendingConfig; havePendingConfig = false;
    portEXIT_CRITICAL(&mux);
    bool any = false;
    if (hs) { state = ps; any = true; }
    for (uint8_t t = 0; t < 4; t++)
        if (dl[t]) { lb::apply_step(state, t, (int16_t)constrain(dl[t], -32768, 32767)); any = true; }
    if (tog) { lb::apply_step(state, 4, 0); any = true; }
    if (hc) { config = pc; uint8_t cb[4]; lb::pack_config(config, cb); chConfig->setValue(cb, 4); any = true; }
    if (any) changed();

    // 2. ease towards the target (fade_ms to cross the full range), then draw
    float step = config.fade_ms ? (float)dt / config.fade_ms : 1.0f;
    const float tg[4] = {target.r, target.g, target.b, target.w};
    static float err[4] = {0, 0, 0, 0};   // temporal dithering: the strip has only 8 bits
    static uint8_t shown[4] = {0, 0, 0, 0};
    uint8_t px[4];
    for (int i = 0; i < 4; i++) {
        float d = tg[i] - cur[i];
        if (fabsf(d) <= step) cur[i] = tg[i]; else cur[i] += d > 0 ? step : -step;
        float v = cur[i] * 255.0f + err[i];
        float q = floorf(v + 0.5f);
        if (q > 255) q = 255;
        if (q < 0) q = 0;
        err[i] = (cur[i] == 0.0f) ? 0.0f : constrain(v - q, -0.5f, 0.5f);
        px[i] = (uint8_t)q;
    }
    if (memcmp(px, shown, 4) != 0) {     // only talk to the strip when something changed
        memcpy(shown, px, 4);
        strip.fill(strip.Color(px[0], px[1], px[2], px[3]));
        strip.show();
    }

    // 3. notify subscribers, at most 20 per second
    if (notifyDirty && now - lastNotifyMs >= 50) {
        notifyDirty = false; lastNotifyMs = now;
        publishState();
        chState->notify();
    }
    // 4. save 2 s after the last change
    if (savePending && now - lastChangeMs >= 2000) { savePending = false; save(); }
}
