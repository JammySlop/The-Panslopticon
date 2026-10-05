#include "buttons.h"

#include <Arduino.h>

#include "config.h"

namespace buttons {
namespace {

struct Button {
    int pin;
    bool repeats;  // auto-repeat while held (UP/DOWN)
    bool stable = false;  // debounced state, true = pressed
    bool lastRaw = false;
    uint32_t changedAt = 0;
    uint32_t pressedAt = 0;
    uint32_t lastRepeat = 0;
    bool longFired = false;
};

Button gUp{config::kPinButtonUp, true};
Button gDown{config::kPinButtonDown, true};
Button gSelect{config::kPinButtonSelect, false};

// Updates the debounced state. Returns +1 on press, -1 on release, 0 otherwise.
int debounce(Button& b, uint32_t now) {
    const bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.lastRaw) {
        b.lastRaw = raw;
        b.changedAt = now;
    }
    if (raw == b.stable || now - b.changedAt < config::kButtonDebounceMs) return 0;
    b.stable = raw;
    return raw ? 1 : -1;
}

Event pollRepeating(Button& b, Event ev, uint32_t now) {
    const int edge = debounce(b, now);
    if (edge > 0) {
        b.pressedAt = b.lastRepeat = now;
        return ev;
    }
    if (b.stable && now - b.pressedAt >= config::kButtonLongPressMs &&
        now - b.lastRepeat >= config::kButtonRepeatMs) {
        b.lastRepeat = now;
        return ev;
    }
    return Event::None;
}

}  // namespace

void begin() {
    pinMode(config::kPinButtonUp, INPUT_PULLUP);
    pinMode(config::kPinButtonDown, INPUT_PULLUP);
    pinMode(config::kPinButtonSelect, INPUT_PULLUP);
}

Event poll() {
    const uint32_t now = millis();
    Event ev = pollRepeating(gUp, Event::Up, now);
    if (ev != Event::None) return ev;
    ev = pollRepeating(gDown, Event::Down, now);
    if (ev != Event::None) return ev;

    // SELECT fires on release so a long press can become BACK instead.
    const int edge = debounce(gSelect, now);
    if (edge > 0) {
        gSelect.pressedAt = now;
        gSelect.longFired = false;
    } else if (gSelect.stable && !gSelect.longFired &&
               now - gSelect.pressedAt >= config::kButtonLongPressMs) {
        gSelect.longFired = true;
        return Event::Back;
    } else if (edge < 0 && !gSelect.longFired) {
        return Event::Select;
    }
    return Event::None;
}

}  // namespace buttons
