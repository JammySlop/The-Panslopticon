#include "vbat.h"

#include <Arduino.h>

#include "config.h"

namespace vbat {
namespace {

float gVolts = 0;
uint32_t gLastSample = 0;
bool gHaveSample = false;

float sample() {
    uint32_t mv = 0;
    constexpr int kReads = 8;
    for (int i = 0; i < kReads; ++i) mv += analogReadMilliVolts(config::kPinVbatSense);
    return (mv / float(kReads)) / 1000.0f * config::kVbatDividerRatio * config::kVbatCalibration;
}

}  // namespace

void begin() {
    pinMode(config::kPinVbatSense, INPUT);
    analogSetPinAttenuation(config::kPinVbatSense, ADC_11db);
}

void update() {
    const uint32_t now = millis();
    if (gHaveSample && now - gLastSample < config::kVbatSampleMs) return;
    gLastSample = now;
    const float v = sample();
    // Light smoothing; the alternator ripple is not interesting here.
    gVolts = gHaveSample ? gVolts * 0.7f + v * 0.3f : v;
    gHaveSample = true;
}

float volts() { return gVolts < 1.0f ? 0.0f : gVolts; }

}  // namespace vbat
