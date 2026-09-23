#include <Arduino.h>

#include "ble_recon.h"
#include "config.h"
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
    Serial.printf("\n=========== scan #%lu  (uptime %lus) ===========\n",
                  static_cast<unsigned long>(cycle),
                  static_cast<unsigned long>(millis() / 1000));

    // WiFi and BLE share one radio, so run them back to back, not together.
    // The LED stays lit while a sweep is in progress.
    digitalWrite(LED_BUILTIN, HIGH);
    wifi_recon::scanAndReport();
    ble_recon::scanAndReport();
    digitalWrite(LED_BUILTIN, LOW);

    delay(config::kPauseBetweenCyclesMs);
}
