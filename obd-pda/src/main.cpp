#include <Arduino.h>

#include "config.h"
#include "console.h"
#include "input.h"
#include "link.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"
#include "vbat.h"

void setup() {
    Serial.begin(config::kSerialBaud);
    Serial.printf("\n=== OBD PDA (%s) === type \"help\" for commands\n", ESP.getChipModel());

    settings::load();
    obdlink::select(settings::get().link);
    input::begin();
    vbat::begin();
    ui::begin();
    // After the display: it starts the SPI bus the card shares. (GPIO8 is the
    // card's chip select; the on-board LED on it now shows card activity.)
    storage::begin();
    storage::event("power-up, firmware built " __DATE__ " " __TIME__);
}

void loop() {
    obdlink::active().service();
    vbat::update();
    console::poll();
    ui::loop();
    storage::service();
    delay(1);  // let the idle task run
}
