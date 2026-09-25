#include <Arduino.h>

#include "ble_recon.h"
#include "config.h"
#include "monitor.h"
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

    Serial.printf("\n=== %s WiFi/BLE recon (%s) ===\n", ESP.getChipModel(),
                  config::kDualBand ? "2.4 + 5 GHz" : "2.4 GHz");
    Serial.printf("PSRAM: %lu KB\n", static_cast<unsigned long>(ESP.getPsramSize() / 1024));
    wifi_recon::begin();
    ble_recon::begin();
    monitor::begin();
}

void loop() {
    ++cycle;
    report::cycleStart(cycle);

    // WiFi and BLE share one radio, so run them back to back, not together.
    // The LED stays lit while a sweep is in progress.
    digitalWrite(LED_BUILTIN, HIGH);
    report::wifi(cycle, wifi_recon::scan());
    report::ble(cycle, ble_recon::scan());
    // Monitor mode reconfigures the radio, so it runs last, after the scans.
    if (config::kMonitorEnabled) report::monitorReport(cycle, monitor::sweep());
    digitalWrite(LED_BUILTIN, LOW);

    delay(config::kPauseBetweenCyclesMs);
}
