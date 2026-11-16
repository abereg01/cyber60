/*
 * Cyber60 buzzer: short sounds for the things worth hearing on a keyboard
 * with no LEDs (this PCB has none fitted):
 *   - the Bluetooth link to the Prospector dongle comes up / is lost
 *     (CONFIG_CYBER60_BUZZER_LINK, split peripheral builds only)
 *   - the battery drops to CONFIG_CYBER60_BUZZER_BATTERY_LOW and to
 *     CONFIG_CYBER60_BUZZER_BATTERY_CRITICAL percent
 *
 * Hardware (4pplet's rev E schematic): BZ401, a Fengming YS-SBZ1230 passive
 * magnetic buzzer, 12 x 12 mm, loudest at its 4 kHz resonance and faint far
 * below it. P0.07 drives it through a 2N7002; a 680 ohm resistor in series
 * sets the loudest it can be, so CONFIG_CYBER60_BUZZER_VOLUME only turns it
 * down. (The comment "P0,27" in boards/4pplet/cyber60/cyber60_rev_e-pinctrl.dtsi
 * is a typo: the schematic and the psel agree on P0.07.)
 *
 * Replaces 4pplet's cyber60_buzzer.c, which beeped on Bluetooth profile
 * changes: those happen on the dongle, never on this keyboard. Sounds play
 * from the system work queue one note at a time, so nothing waits on them.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/events/battery_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_CYBER60_BUZZER_LINK)
#include <zmk/events/split_peripheral_status_changed.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(DT_NODELABEL(buzzer));

// A pitch in Hz (0 = rest) for ms milliseconds; {0, 0} ends a sound. Each
// sound ends on a rest, so two sounds in a row stay apart.
struct note {
    uint16_t hz;
    uint16_t ms;
};

#define E7 2637
#define B7 3951
#define RESONANCE 4000

// Link up: a quick rising "da-ding". Link lost: the same falling, slower.
static const struct note link_up[] = {{E7, 70}, {0, 30}, {B7, 110}, {0, 250}, {0, 0}};
static const struct note link_lost[] = {{B7, 110}, {0, 40}, {E7, 220}, {0, 250}, {0, 0}};
// Battery: beeps on the buzzer's own pitch (the loudest), two for low, four
// for critical.
static const struct note battery_low[] = {
    {RESONANCE, 60}, {0, 90}, {RESONANCE, 60}, {0, 250}, {0, 0}};
static const struct note battery_critical[] = {
    {RESONANCE, 60}, {0, 90}, {RESONANCE, 60}, {0, 90},
    {RESONANCE, 60}, {0, 90}, {RESONANCE, 60}, {0, 250}, {0, 0}};

enum sound { SOUND_LINK_UP, SOUND_LINK_LOST, SOUND_BATTERY_LOW, SOUND_BATTERY_CRITICAL };

static const struct note *const sounds[] = {
    [SOUND_LINK_UP] = link_up,
    [SOUND_LINK_LOST] = link_lost,
    [SOUND_BATTERY_LOW] = battery_low,
    [SOUND_BATTERY_CRITICAL] = battery_critical,
};

static atomic_t pending;         // one bit per sound waiting its turn
static const struct note *note;  // the note playing now (work queue only)

static void set_tone(uint16_t hz) {
    int err;

    if (hz == 0) {
        err = pwm_set_pulse_dt(&buzzer, 0); // pin low, PWM stops: no current
    } else {
        const uint32_t period = NSEC_PER_SEC / hz;
        // Volume 100 = a 50 % square wave, the loudest.
        err = pwm_set_dt(&buzzer, period, period / 200 * CONFIG_CYBER60_BUZZER_VOLUME);
    }
    if (err) {
        LOG_WRN("buzzer: PWM error %d", err);
    }
}

static void play_next(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(play_work, play_next);

static void play_next(struct k_work *work) {
    if (note == NULL || note->ms == 0) {
        note = NULL;
        for (int i = 0; i < ARRAY_SIZE(sounds) && note == NULL; i++) {
            if (atomic_test_and_clear_bit(&pending, i)) {
                note = sounds[i];
            }
        }
        if (note == NULL) {
            set_tone(0);
            return;
        }
    }
    set_tone(note->hz);
    k_work_reschedule(&play_work, K_MSEC(note->ms));
    note++;
}

static void play(enum sound s) {
    if (!device_is_ready(buzzer.dev)) {
        return;
    }
    atomic_set_bit(&pending, s);
    k_work_schedule(&play_work, K_NO_WAIT); // no-op while a sound is playing
}

#if IS_ENABLED(CONFIG_CYBER60_BUZZER_LINK)
// Short drops (radio noise, the dongle rebooting) stay quiet: "lost" plays once
// the link has been down for LINK_LOST_DELAY, and "up" plays after a "lost" or
// on the first connection after power-on. Waking from deep sleep is a
// power-on, so the keyboard chimes when it's back with the dongle.
#define LINK_LOST_DELAY K_SECONDS(3)

static bool link_up_now, link_was_up, lost_played;

static void link_lost_check(struct k_work *work) {
    if (!link_up_now) {
        lost_played = true;
        play(SOUND_LINK_LOST);
    }
}

static K_WORK_DELAYABLE_DEFINE(link_lost_work, link_lost_check);

static void on_link(bool connected) {
    link_up_now = connected;
    if (connected) {
        k_work_cancel_delayable(&link_lost_work);
        if (!link_was_up || lost_played) {
            play(SOUND_LINK_UP);
        }
        link_was_up = true;
        lost_played = false;
    } else if (link_was_up && !lost_played) {
        k_work_schedule(&link_lost_work, LINK_LOST_DELAY); // keeps an earlier countdown
    }
}
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
// One warning per threshold on the way down. Charging 5 points past a
// threshold arms it again.
#define REARM 5

static bool low_warned, critical_warned;

static void on_battery(uint8_t percent) {
    if (percent >= CONFIG_CYBER60_BUZZER_BATTERY_LOW + REARM) {
        low_warned = false;
    }
    if (percent >= CONFIG_CYBER60_BUZZER_BATTERY_CRITICAL + REARM) {
        critical_warned = false;
    }
    if (percent <= CONFIG_CYBER60_BUZZER_BATTERY_CRITICAL && !critical_warned) {
        critical_warned = low_warned = true;
        play(SOUND_BATTERY_CRITICAL);
    } else if (percent <= CONFIG_CYBER60_BUZZER_BATTERY_LOW && !low_warned) {
        low_warned = true;
        play(SOUND_BATTERY_LOW);
    }
}
#endif

static int buzzer_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_CYBER60_BUZZER_LINK)
    const struct zmk_split_peripheral_status_changed *link =
        as_zmk_split_peripheral_status_changed(eh);
    if (link) {
        on_link(link->connected);
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    const struct zmk_battery_state_changed *battery = as_zmk_battery_state_changed(eh);
    if (battery) {
        on_battery(battery->state_of_charge);
    }
#endif
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cyber60_buzzer, buzzer_listener);
#if IS_ENABLED(CONFIG_CYBER60_BUZZER_LINK)
ZMK_SUBSCRIPTION(cyber60_buzzer, zmk_split_peripheral_status_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(cyber60_buzzer, zmk_battery_state_changed);
#endif
