// Pins, bus settings and UI timing. Change behaviour here, not in the modules.
// Every pin here is mirrored in docs/HARDWARE.md; keep the two in step.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace config {

// --- Pins (ESP32-C3 Super Mini) ---------------------------------------------
// Left header:  5V  G  3V3  4  3  2  1  0
// Right header: 5  6  7  8  9  10  20  21
//
// Strapping pins are GPIO2, GPIO8 and GPIO9. GPIO8 (on-board LED) and GPIO9
// (on-board BOOT button) are used for what the board already wires them to,
// and GPIO2 is a button input that idles high, so nothing pulls a strapping
// pin the wrong way at reset.

// 2" 240x320 ST7789 display (silkscreen: GND VCC SCL SDA RES DC CS BLK).
// SCL/SDA on these modules are the SPI clock and MOSI, not I2C.
constexpr int kPinLcdSclk = 6;
constexpr int kPinLcdMosi = 7;
constexpr int kPinLcdCs = 10;
constexpr int kPinLcdDc = 5;
// GPIO21 is UART0 TX, so the ROM boot log wiggles it for a moment at power-on.
// That only resets the display, which setup() initialises afterwards anyway.
constexpr int kPinLcdRst = 21;
// GPIO20 (UART0 RX) is an input until setup() runs; the module's own pull-up
// keeps the backlight on until then.
constexpr int kPinLcdBacklight = 20;

// CAN transceiver (SN65HVD230 or similar 3.3 V part). The C3's built-in TWAI
// controller speaks CAN 2.0; the transceiver turns it into CAN-H / CAN-L.
constexpr int kPinCanTx = 0;
constexpr int kPinCanRx = 1;

// Vehicle battery voltage through a 100k / 18k divider (ADC1 channel 3).
constexpr int kPinVbatSense = 3;
constexpr float kVbatDividerRatio = (100.0f + 18.0f) / 18.0f;
// Calculated, not measured: trim against a multimeter once built.
constexpr float kVbatCalibration = 1.0f;

// Buttons, active low with the internal pull-up. SELECT is the BOOT button
// already on the board (GPIO9); an external button can be wired in parallel.
constexpr int kPinButtonUp = 4;
constexpr int kPinButtonDown = 2;
constexpr int kPinButtonSelect = 9;

// On-board blue LED, active low. Blinks on CAN traffic.
constexpr int kPinStatusLed = 8;
constexpr bool kStatusLedActiveLow = true;

// --- Display -----------------------------------------------------------------
constexpr uint32_t kLcdSpiHz = 40000000;  // 80 MHz often works too; 40 is safe.
// Most 2" ST7789 modules need colour inversion; flip this if colours look wrong.
constexpr bool kLcdInvert = true;
constexpr uint8_t kBacklightDefault = 200;  // 0-255

// --- CAN / OBD-II -----------------------------------------------------------
// OBD-II over CAN (ISO 15765-4) runs at 500 kbit/s on almost every car since
// 2008; some older or non-US vehicles use 250 kbit/s. Auto-detect tries both.
constexpr uint32_t kCanBitrates[] = {500000, 250000};
// Listen-only window per bitrate during auto-detect.
constexpr uint32_t kAutodetectListenMs = 400;

// The sniffer never transmits, so it runs the controller in listen-only mode:
// it does not even acknowledge frames. OBD queries need normal mode.
constexpr uint32_t kCanRxQueueLen = 64;
constexpr uint32_t kCanTxQueueLen = 8;

// 11-bit OBD addressing. 0x7DF reaches every emission ECU; replies come from
// 0x7E8-0x7EF, and each ECU listens for flow control on (reply id - 8).
constexpr uint32_t kObdBroadcastId = 0x7DF;
constexpr uint32_t kObdReplyIdFirst = 0x7E8;
constexpr uint32_t kObdReplyIdLast = 0x7EF;
// ISO 15765-4 allows ECUs 50 ms (P2) to answer; give them a little slack.
constexpr uint32_t kObdResponseTimeoutMs = 100;
// Multi-frame (ISO-TP) replies such as a long DTC list.
constexpr uint32_t kIsoTpFrameTimeoutMs = 150;

// Live data: minimum gap between PID requests, to stay polite on the bus.
constexpr uint32_t kLivePollGapMs = 20;

// --- ELM327 WiFi dongle -------------------------------------------------------
// Almost every cheap WiFi ELM327 clone is an open access point called
// something like "WiFi_OBDII", listening on 192.168.0.10 port 35000. The
// network can be picked on the Settings screen, so this is just the default.
constexpr char kElmDefaultSsid[] = "WiFi_OBDII";
// Used only if DHCP does not give us a gateway address (the dongle itself).
constexpr char kElmFallbackHost[] = "192.168.0.10";
// Tried in order when no port is configured.
constexpr uint16_t kElmPorts[] = {35000, 23};

constexpr uint32_t kWifiConnectTimeoutMs = 12000;
constexpr uint32_t kElmTcpConnectTimeoutMs = 3000;
// ATZ reboots the ELM; clones take up to ~1.5 s to print their banner.
constexpr uint32_t kElmResetTimeoutMs = 3000;
constexpr uint32_t kElmCommandTimeoutMs = 1000;
// The first request after ATSP0 makes the ELM try every protocol in turn.
constexpr uint32_t kElmSearchTimeoutMs = 12000;
// One OBD request: WiFi round trip plus the ELM's own ~200 ms ECU timeout.
constexpr uint32_t kElmResponseTimeoutMs = 1500;
// Append the "expected responses" digit to single-frame PID requests (e.g.
// "010C1") so the ELM answers as soon as one ECU replies instead of waiting
// out its timeout. Roughly triples the live-data rate. Some very old fake
// "v2.1" clones choke on it; set false if live data shows only "--".
constexpr bool kElmUseResponseCount = true;
// How often the header refreshes battery voltage via ATRV.
constexpr uint32_t kElmVoltsPollMs = 3000;

// --- Sniffer ------------------------------------------------------------------
// Unique arbitration IDs tracked. A busy powertrain bus has 50-150.
constexpr size_t kSnifferMaxIds = 160;
// How long a changed byte stays highlighted.
constexpr uint32_t kSnifferHighlightMs = 600;
// Row redraw period. Busy IDs change every few ms; the eye can't follow faster.
constexpr uint32_t kSnifferRedrawMs = 100;

// --- UI ---------------------------------------------------------------------
constexpr uint32_t kButtonDebounceMs = 25;
constexpr uint32_t kButtonLongPressMs = 600;
constexpr uint32_t kButtonRepeatMs = 120;  // auto-repeat while UP/DOWN held
constexpr uint32_t kUiFrameMs = 50;        // screen refresh period
constexpr uint32_t kVbatSampleMs = 500;

constexpr uint32_t kSerialBaud = 115200;

}  // namespace config
