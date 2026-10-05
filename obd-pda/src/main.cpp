#include <Arduino.h>

#include "buttons.h"
#include "can_bus.h"
#include "config.h"
#include "ui.h"
#include "vbat.h"

namespace {

uint32_t gLastRx = 0;
uint32_t gLedOffAt = 0;

void setLed(bool on) {
    digitalWrite(config::kPinStatusLed, on != config::kStatusLedActiveLow ? HIGH : LOW);
}

// Flash the on-board LED briefly whenever frames arrive.
void updateActivityLed() {
    const uint32_t rx = can_bus::stats().rxFrames;
    const uint32_t now = millis();
    if (rx != gLastRx) {
        gLastRx = rx;
        setLed(true);
        gLedOffAt = now + 30;
    } else if (static_cast<int32_t>(now - gLedOffAt) >= 0) {
        setLed(false);
    }
}

}  // namespace

void setup() {
    pinMode(config::kPinStatusLed, OUTPUT);
    setLed(false);
    Serial.begin(config::kSerialBaud);
    Serial.printf("\n=== OBD PDA (%s) ===\n", ESP.getChipModel());

    buttons::begin();
    vbat::begin();
    ui::begin();
}

void loop() {
    can_bus::service();
    vbat::update();
    ui::loop();
    updateActivityLed();
    delay(1);  // let the idle task run
}
