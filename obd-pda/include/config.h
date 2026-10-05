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
// Strapping pins are GPIO2, GPIO8 and GPIO9. GPIO9 is the on-board BOOT
// button, GPIO8 (on-board LED) is the SD card's chip select, which idles high,
// and GPIO2 is a switch input that idles high, so nothing pulls a strapping
// pin the wrong way at reset (just don't hold LEFT while powering on).

// One SPI bus, shared by the display and the micro SD card.
// SCL/SDA on the display module are the SPI clock and MOSI, not I2C.
constexpr int kPinSpiSclk = 6;
constexpr int kPinSpiMosi = 7;
// Only the SD card talks back. GPIO20 is UART0 RX, an input at boot, so the
// ROM boot log never drives it against the card.
constexpr int kPinSpiMiso = 20;

// 2" 240x320 ST7789 display (silkscreen: GND VCC SCL SDA RES DC CS BLK).
constexpr int kPinLcdCs = 10;
constexpr int kPinLcdDc = 5;
// GPIO21 is UART0 TX, so the ROM boot log wiggles it for a moment at power-on.
// That only resets the display, which setup() initialises afterwards anyway.
constexpr int kPinLcdRst = 21;
// BLK is wired to 3V3: the backlight is on whenever the device is, which
// frees the GPIO that dimming would need.

// 6-pin micro SD module (GND VCC MISO MOSI SCK CS). GPIO8 also drives the
// board's blue LED (active low), so the LED lights while the card is accessed.
constexpr int kPinSdCs = 8;
// 20 MHz is safe through the level shifter on the common blue modules; the
// card itself does 25.
constexpr uint32_t kSdSpiHz = 20000000;

// --- 5-way navigation switch ---------------------------------------------------
// A 5-way tactile switch (up/down/left/right/centre push) drives the UI. It can
// be wired two ways; pick the matching build environment (see platformio.ini)
// and wire as in README.md.
enum class NavWiring : uint8_t {
    // One GPIO per direction, each switch to GND, internal pull-ups. The most
    // reliable, and the switch lands on the five left-header pins in order.
    // Uses GPIO0-4, so the direct-CAN link (which needs 0, 1 and 3) is not
    // available in this build.
    Digital,
    // All five directions on one ADC pin through a resistor ladder, leaving
    // GPIO0, 1 and 3 for the CAN transceiver and battery sense.
    Ladder,
};
// Set by the build environment: `c3_supermini` (default) is digital,
// `c3_supermini_ladder` defines OBD_PDA_NAV_LADDER.
#ifdef OBD_PDA_NAV_LADDER
constexpr NavWiring kNavWiring = NavWiring::Ladder;
#else
constexpr NavWiring kNavWiring = NavWiring::Digital;
#endif

// Digital wiring: module pin -> GPIO, in left-header order.
constexpr int kPinNavUp = 4;
constexpr int kPinNavDown = 3;
constexpr int kPinNavLeft = 2;   // strapping pin: idles high, fine as an input
constexpr int kPinNavRight = 1;
constexpr int kPinNavCenter = 0;

// Ladder wiring: 10k pull-up from the pin to 3V3; each direction switches a
// different resistor to GND, giving V = 3.3 * R / (R + 10k):
//   centre 0R -> 0.00 V, up 1k -> 0.30 V, down 3.3k -> 0.82 V,
//   left 6.8k -> 1.34 V, right 15k -> 1.98 V, released -> 3.3 V.
// Thresholds sit halfway between neighbours. Calculated, not measured: check
// the readings with the "nav" console command and adjust if needed.
constexpr int kPinNavLadder = 4;
constexpr uint16_t kNavLadderThresholdsMv[] = {150, 560, 1080, 1660, 2400};

// The board's BOOT button (GPIO9) always works as an extra centre/select.
constexpr int kPinBootButton = 9;

// The direct-CAN link needs GPIO0, 1 and 3, which digital nav wiring uses.
constexpr bool kDirectCanAvailable = kNavWiring == NavWiring::Ladder;

// CAN transceiver (SN65HVD230 or similar 3.3 V part), ladder builds only.
// The C3's built-in TWAI controller speaks CAN 2.0; the transceiver turns it
// into CAN-H / CAN-L.
constexpr int kPinCanTx = 0;
constexpr int kPinCanRx = 1;

// Vehicle battery voltage through a 100k / 18k divider (ADC1 channel 3),
// direct-CAN builds only. In ELM mode the dongle reports it (ATRV).
constexpr int kPinVbatSense = 3;
constexpr float kVbatDividerRatio = (100.0f + 18.0f) / 18.0f;
// Calculated, not measured: trim against a multimeter once built.
constexpr float kVbatCalibration = 1.0f;

// --- Display -----------------------------------------------------------------
constexpr uint32_t kLcdSpiHz = 40000000;  // 80 MHz often works too; 40 is safe.
// Most 2" ST7789 modules need colour inversion; flip this if colours look wrong.
constexpr bool kLcdInvert = true;

// --- CAN / OBD-II -----------------------------------------------------------
// OBD-II over CAN (ISO 15765-4) runs at 500 kbit/s on almost every car since
// 2008; some older or non-US vehicles use 250 kbit/s. Auto-detect tries both.
constexpr uint32_t kCanBitrates[] = {500000, 250000};
// Listen-only window per bitrate during auto-detect.
constexpr uint32_t kAutodetectListenMs = 400;

// The sniffer never transmits, so it runs the controller in listen-only mode:
// it does not even acknowledge frames. OBD queries need normal mode.
// Big enough to ride out a slow SD card write while logging (~130 ms at a
// busy 2000 frames/s); frames are ~20 bytes of RAM each.
constexpr uint32_t kCanRxQueueLen = 256;
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

// --- SD card logging --------------------------------------------------------
// Everything goes under /obdpda/<session>/, one numbered folder per power-up
// that writes anything (there is no clock, so no dates).
constexpr char kSdRootDir[] = "/obdpda";
// Log lines collect in RAM and are written in chunks, then flushed to the
// card at least this often. Bounds what a power cut can lose.
constexpr size_t kLogBufferBytes = 4096;
constexpr uint32_t kLogFlushMs = 1000;

// --- UI ---------------------------------------------------------------------
constexpr uint32_t kButtonDebounceMs = 25;
constexpr uint32_t kButtonLongPressMs = 600;
constexpr uint32_t kButtonRepeatMs = 120;  // auto-repeat while a direction is held
constexpr uint32_t kUiFrameMs = 50;        // screen refresh period
constexpr uint32_t kVbatSampleMs = 500;

constexpr uint32_t kSerialBaud = 115200;

}  // namespace config
