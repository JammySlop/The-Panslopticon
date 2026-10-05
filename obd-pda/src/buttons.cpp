#include "buttons.h"

#include <Arduino.h>

#include "config.h"

namespace buttons {
namespace {

enum Key : uint8_t { kUp, kDown, kLeft, kRight, kCenter, kKeyCount };

const char* const kKeyNames[kKeyCount] = {"up", "down", "left", "right", "centre"};
constexpr Event kKeyEvents[kKeyCount] = {Event::Up, Event::Down, Event::Left, Event::Right, Event::Select};

struct KeyState {
    bool stable = false;  // debounced, true = pressed
    bool lastRaw = false;
    uint32_t changedAt = 0;
    uint32_t pressedAt = 0;
    uint32_t lastRepeat = 0;
    bool longFired = false;
};

KeyState gKeys[kKeyCount];
uint16_t gLadderMv = 0;

// Which keys are physically down right now (before debouncing).
void readRaw(bool raw[kKeyCount]) {
    if (config::kNavWiring == config::NavWiring::Digital) {
        raw[kUp] = digitalRead(config::kPinNavUp) == LOW;
        raw[kDown] = digitalRead(config::kPinNavDown) == LOW;
        raw[kLeft] = digitalRead(config::kPinNavLeft) == LOW;
        raw[kRight] = digitalRead(config::kPinNavRight) == LOW;
        raw[kCenter] = digitalRead(config::kPinNavCenter) == LOW;
    } else {
        // The ladder can only report one direction at a time; the lowest
        // voltage band wins. Bands, low to high: centre, up, down, left,
        // right, then released.
        gLadderMv = analogReadMilliVolts(config::kPinNavLadder);
        static constexpr Key kBandKeys[] = {kCenter, kUp, kDown, kLeft, kRight};
        for (int i = 0; i < kKeyCount; ++i) raw[i] = false;
        for (size_t band = 0; band < sizeof(kBandKeys) / sizeof(kBandKeys[0]); ++band) {
            if (gLadderMv < config::kNavLadderThresholdsMv[band]) {
                raw[kBandKeys[band]] = true;
                break;
            }
        }
    }
    // BOOT is always a second centre button.
    raw[kCenter] = raw[kCenter] || digitalRead(config::kPinBootButton) == LOW;
}

// Updates the debounced state. Returns +1 on press, -1 on release, 0 otherwise.
int debounce(KeyState& k, bool raw, uint32_t now) {
    if (raw != k.lastRaw) {
        k.lastRaw = raw;
        k.changedAt = now;
    }
    if (raw == k.stable || now - k.changedAt < config::kButtonDebounceMs) return 0;
    k.stable = raw;
    return raw ? 1 : -1;
}

}  // namespace

void begin() {
    if (config::kNavWiring == config::NavWiring::Digital) {
        for (int pin : {config::kPinNavUp, config::kPinNavDown, config::kPinNavLeft, config::kPinNavRight,
                        config::kPinNavCenter}) {
            pinMode(pin, INPUT_PULLUP);
        }
    } else {
        // External 10k pull-up; the internal ~45k one is too loose for a ladder.
        pinMode(config::kPinNavLadder, INPUT);
        analogSetPinAttenuation(config::kPinNavLadder, ADC_11db);
    }
    pinMode(config::kPinBootButton, INPUT_PULLUP);
}

Event poll() {
    const uint32_t now = millis();
    bool raw[kKeyCount];
    readRaw(raw);

    Event result = Event::None;
    for (int i = 0; i < kKeyCount; ++i) {
        KeyState& k = gKeys[i];
        const int edge = debounce(k, raw[i], now);
        Event ev = Event::None;
        if (i == kCenter) {
            // Centre fires on release, so a long hold can become Back instead.
            if (edge > 0) {
                k.pressedAt = now;
                k.longFired = false;
            } else if (k.stable && !k.longFired && now - k.pressedAt >= config::kButtonLongPressMs) {
                k.longFired = true;
                ev = Event::Back;
            } else if (edge < 0 && !k.longFired) {
                ev = Event::Select;
            }
        } else if (edge > 0) {
            k.pressedAt = k.lastRepeat = now;
            ev = kKeyEvents[i];
        } else if (k.stable && now - k.pressedAt >= config::kButtonLongPressMs &&
                   now - k.lastRepeat >= config::kButtonRepeatMs) {
            k.lastRepeat = now;
            ev = kKeyEvents[i];
        }
        // Keep debouncing every key, but report only the first event.
        if (result == Event::None) result = ev;
    }
    return result;
}

uint16_t ladderMillivolts() { return gLadderMv; }

const char* heldKeyName() {
    for (int i = 0; i < kKeyCount; ++i) {
        if (gKeys[i].stable) return kKeyNames[i];
    }
    return "none";
}

}  // namespace buttons
