#include "input.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "display.h"

namespace input {
namespace {

// Calibration lives in its own NVS namespace, apart from the settings.
constexpr char kNvsNamespace[] = "touch";
constexpr char kNvsKey[] = "cal";

// Touch panel: debounced in polls, since a light press flickers.
bool gDown = false;
uint8_t gHits = 0;
uint8_t gMisses = 0;
int16_t gX = 0;
int16_t gY = 0;
uint32_t gLastPoll = 0;
uint32_t gPressedAt = 0;
uint32_t gLastRepeat = 0;

// BOOT button.
bool gBootRaw = false;
bool gBootDown = false;
bool gBootLongFired = false;
uint32_t gBootChangedAt = 0;
uint32_t gBootPressedAt = 0;

bool gEcho = false;

Event make(Event::Kind kind, bool repeat = false) {
    Event ev;
    ev.kind = kind;
    ev.x = gX;
    ev.y = gY;
    ev.repeat = repeat;
    return ev;
}

Event pollBoot(uint32_t now) {
    const bool raw = digitalRead(config::kPinBootButton) == LOW;
    if (raw != gBootRaw) {
        gBootRaw = raw;
        gBootChangedAt = now;
    }
    if (raw != gBootDown && now - gBootChangedAt >= config::kButtonDebounceMs) {
        gBootDown = raw;
        if (raw) {
            gBootPressedAt = now;
            gBootLongFired = false;
            return make(Event::Kind::Back);
        }
    }
    if (gBootDown && !gBootLongFired && now - gBootPressedAt >= config::kBootCalibrateMs) {
        gBootLongFired = true;
        return make(Event::Kind::Calibrate);
    }
    return Event();
}

Event pollTouch(uint32_t now) {
    if (now - gLastPoll < config::kTouchPollMs) return Event();
    gLastPoll = now;
    int32_t x = 0, y = 0;
    const bool touched = lcd.getTouch(&x, &y) > 0;
    if (touched) {
        gMisses = 0;
        if (gHits < UINT8_MAX) ++gHits;
        gX = static_cast<int16_t>(constrain(x, 0, lcd.width() - 1));
        gY = static_cast<int16_t>(constrain(y, 0, lcd.height() - 1));
    } else {
        gHits = 0;
        if (gMisses < UINT8_MAX) ++gMisses;
    }
    if (!gDown && gHits >= config::kTouchPressPolls) {
        gDown = true;
        gPressedAt = gLastRepeat = now;
        if (gEcho) Serial.printf("[touch] tap %d,%d\n", gX, gY);
        return make(Event::Kind::Tap);
    }
    if (gDown && gMisses >= config::kTouchReleasePolls) {
        gDown = false;
    } else if (gDown && touched && now - gPressedAt >= config::kTouchRepeatDelayMs &&
               now - gLastRepeat >= config::kTouchRepeatMs) {
        gLastRepeat = now;
        return make(Event::Kind::Tap, true);
    }
    return Event();
}

}  // namespace

void begin() { pinMode(config::kPinBootButton, INPUT_PULLUP); }

Event poll() {
    const uint32_t now = millis();
    const Event boot = pollBoot(now);
    if (boot.kind != Event::Kind::None) return boot;
    return pollTouch(now);
}

bool loadCalibration() {
    Preferences prefs;
    uint16_t cal[8];
    // Read-write, so the namespace is created on first boot instead of
    // logging an error.
    prefs.begin(kNvsNamespace, false);
    const bool ok = prefs.getBytesLength(kNvsKey) == sizeof(cal) && prefs.getBytes(kNvsKey, cal, sizeof(cal)) == sizeof(cal);
    prefs.end();
    if (ok) lcd.setTouchCalibrate(cal);
    return ok;
}

void calibrate(uint16_t fg, uint16_t bg) {
    uint16_t cal[8];
    lcd.calibrateTouch(cal, fg, bg, 20);
    lcd.setTouchCalibrate(cal);
    Preferences prefs;
    prefs.begin(kNvsNamespace, false);
    prefs.putBytes(kNvsKey, cal, sizeof(cal));
    prefs.end();
    // Whatever was held during calibration is not a tap.
    gDown = true;
    gHits = 0;
}

void setEcho(bool on) { gEcho = on; }
bool echo() { return gEcho; }

}  // namespace input
