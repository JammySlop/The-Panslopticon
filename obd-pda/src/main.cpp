#include <Arduino.h>

#include "buttons.h"
#include "config.h"
#include "console.h"
#include "link.h"
#include "settings.h"
#include "ui.h"
#include "vbat.h"

namespace {

uint32_t gLastFrames = 0;
uint32_t gLedOffAt = 0;

void setLed(bool on) {
    digitalWrite(config::kPinStatusLed, on != config::kStatusLedActiveLow ? HIGH : LOW);
}

// Flash the on-board LED briefly whenever frames arrive.
void updateActivityLed() {
    const uint32_t frames = obdlink::active().frameCount();
    const uint32_t now = millis();
    if (frames != gLastFrames) {
        gLastFrames = frames;
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
    Serial.printf("\n=== OBD PDA (%s) === type \"help\" for commands\n", ESP.getChipModel());

    settings::load();
    obdlink::select(settings::get().link);
    buttons::begin();
    // The battery divider shares GPIO3 with the digital nav switch.
    if (config::kDirectCanAvailable) vbat::begin();
    ui::begin();
}

void loop() {
    obdlink::active().service();
    if (config::kDirectCanAvailable) vbat::update();
    console::poll();
    ui::loop();
    updateActivityLed();
    delay(1);  // let the idle task run
}
