#include <Arduino.h>

#include "ble_recon.h"
#include "config.h"
#include "report.h"
#include "wifi_recon.h"

namespace {

uint32_t cycle = 0;

void waitForSerial() {
    const uint32_t start = millis();
    while (!Serial && (millis() - start) < config::kSerialReadyTimeoutMs) {
        delay(10);
    }
}

}  // namespace

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(config::kSerialBaud);
    waitForSerial();

    Serial.println("\n=== Nano ESP32 WiFi/BLE recon ===");
    wifi_recon::begin();
    ble_recon::begin();
}

void loop() {
    ++cycle;
    report::cycleStart(cycle);

    // WiFi and BLE share one radio, so run them back to back, not together.
    // The LED stays lit while a sweep is in progress.
    digitalWrite(LED_BUILTIN, HIGH);
    report::wifi(cycle, wifi_recon::scan());
    report::ble(cycle, ble_recon::scan());
    digitalWrite(LED_BUILTIN, LOW);

    delay(config::kPauseBetweenCyclesMs);
}
