#include <Arduino.h>

#include "ble_recon.h"
#include "config.h"
#include "display.h"
#include "monitor.h"
#include "report.h"
#include "storage.h"
#include "wifi_export.h"
#include "wifi_recon.h"

namespace {

uint32_t cycle = 0;

void waitForSerial() {
    const uint32_t start = millis();
    while (!Serial && (millis() - start) < config::kSerialReadyTimeoutMs) {
        delay(10);
    }
}

// Host-issued control commands, all prefixed with '!'. Everything else on the
// serial line is ignored: the scanner never needs input to do its job.
void runCommand(const String& cmd) {
    if (cmd == "!ls") {
        storage::list(Serial);
    } else if (cmd.startsWith("!cat ")) {
        storage::cat(Serial, cmd.substring(5));
    } else if (cmd == "!sd-status") {
        storage::status(Serial);
    } else if (cmd == "!wifi-export") {
        wifi_export::run(Serial);
    } else {
        Serial.printf("!unknown %s\n", cmd.c_str());
    }
}

// Reads any pending serial input a line at a time without blocking the scan.
void pollCommands() {
    static String line;
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            line.trim();
            if (line.startsWith("!")) runCommand(line);
            line = "";
        } else if (line.length() < 128) {
            line += c;
        }
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
    // Mount the SD card before the display starts its task, so the SD init has
    // the shared SPI bus to itself.
    storage::begin();
    wifi_recon::begin();
    ble_recon::begin();
    monitor::begin();
    display::begin();
}

void loop() {
    pollCommands();
    ++cycle;
    report::cycleStart(cycle);

    // WiFi and BLE share one radio, so run them back to back, not together.
    // The LED stays lit while a sweep is in progress.
    digitalWrite(LED_BUILTIN, HIGH);
    const auto networks = wifi_recon::scan();
    report::wifi(cycle, networks);
    display::wifi(cycle, networks);

    const auto devices = ble_recon::scan();
    report::ble(cycle, devices);
    display::ble(cycle, devices);

    // Monitor mode reconfigures the radio, so it runs last, after the scans.
    if (config::kMonitorEnabled) {
        const MonitorReport mon = monitor::sweep();
        report::monitorReport(cycle, mon);
        display::monitor(cycle, mon);
    }
    digitalWrite(LED_BUILTIN, LOW);

    delay(config::kPauseBetweenCyclesMs);
}
