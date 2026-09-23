#include <Arduino.h>

#include "AppScheduler.h"
#include "config.h"

namespace {

void heartbeat();
void reportStatus();

// The task table. Add a row here and the scheduler picks it up; nothing in
// loop() needs to change.
app::PeriodicTask tasks[] = {
    {"heartbeat", config::kHeartbeatIntervalMs, heartbeat, 0, 0},
    {"status", config::kStatusIntervalMs, reportStatus, 0, 0},
};

app::AppScheduler scheduler(tasks, sizeof(tasks) / sizeof(tasks[0]));

void heartbeat() {
    static bool on = false;
    on = !on;
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

void reportStatus() {
    Serial.printf("[%8lu ms] env=%s heap=%u/%u psram=%u rssi=n/a\n",
                  static_cast<unsigned long>(millis()), APP_ENV,
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getHeapSize()),
                  static_cast<unsigned>(ESP.getFreePsram()));

    for (size_t i = 0; i < scheduler.count(); ++i) {
        const app::PeriodicTask& task = scheduler.at(i);
        if (task.missedDeadlines > 0) {
            Serial.printf("  warn: task '%s' missed %lu deadline(s)\n",
                          task.name,
                          static_cast<unsigned long>(task.missedDeadlines));
        }
    }
}

void waitForSerial() {
    const uint32_t start = millis();
    // Subtracting timestamps (rather than comparing them) stays correct when
    // millis() wraps.
    while (!Serial && (millis() - start) < config::kSerialReadyTimeoutMs) {
        delay(10);
    }
}

}  // namespace

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.begin(config::kSerialBaud);
    waitForSerial();

    Serial.println();
    Serial.println("=== Arduino Nano ESP32 ===");
    Serial.printf("build   : %s %s (%s)\n", __DATE__, __TIME__, APP_ENV);
    Serial.printf("chip    : %s rev %d, %d core(s) @ %lu MHz\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  static_cast<unsigned long>(getCpuFrequencyMhz()));
    Serial.printf("flash   : %u bytes\n",
                  static_cast<unsigned>(ESP.getFlashChipSize()));
    Serial.printf("reset   : %d\n", static_cast<int>(esp_reset_reason()));
    Serial.println();

    scheduler.begin(millis());
}

void loop() {
    scheduler.tick(millis());

    // No delay() here on purpose: every task owns its own cadence, so the loop
    // stays free to service whatever you add next (WiFi, BLE, sensor reads).
}
