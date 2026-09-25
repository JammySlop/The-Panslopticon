# nano-esp32

PlatformIO project for a passive WiFi and Bluetooth LE survey tool. It alternates
a WiFi sweep and a BLE sweep and sends what it finds over USB, where a local web
page displays it. It never joins a network or connects to a device.

Two boards are supported from the same source:

| Environment  | Board                                    | WiFi bands     | USB                    |
|--------------|------------------------------------------|----------------|------------------------|
| `esp32c5` (default) | ESP32-C5-DevKitC-1, WROOM-1 MCN16R8 (16 MB flash, 8 MB PSRAM) | 2.4 + 5 GHz | CH343 USB-UART bridge |
| `nano_esp32` | Arduino Nano ESP32 (ESP32-S3)            | 2.4 GHz        | Native USB (DFU)       |

The C5 needs Arduino core 3.3+, so the project uses the community
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform
(pinned in `platformio.ini`) instead of PlatformIO's official `espressif32`.

- **WiFi (scan):** SSID, BSSID, signal (RSSI), channel and band, security,
  cipher, WPS, PHY modes (a/b/g/n/ac/ax), advertised country, and BSSID vendor.
  Listen-only (passive) by default. On the C5 a sweep covers every 2.4 and
  5 GHz channel, which takes about 17 s at the default dwell time.
- **BLE:** address and type, signal, name, manufacturer, TX power, appearance,
  decoded product family (AirPods, Find My, iBeacon…), a rough distance
  estimate, and advertised services.
- **Monitor mode (Tier 2, receive-only):** hops channels 1/6/11 (plus the
  non-DFS 5 GHz channels 36-48 and 149-165 on the C5) in promiscuous mode and
  parses 802.11 *frame headers only* to report devices probing for
  networks (and the network names they ask for), which clients are associated
  with which AP, per-channel activity, and deauth/disassoc bursts (a nearby
  attack signature). Set `kMonitorEnabled = false` in `include/config.h` for a
  scan-only build. This phase parses headers the radio already receives; it
  transmits nothing and never inspects frame contents.

Tuning (scan mode, dwell time, BLE window, output format) lives in
`include/config.h`.

## Web interface

`web/index.html` reads the board directly over USB with the Web Serial API. No
server-side code and no network traffic from the board. It needs Chrome or
Edge, and Web Serial only works on a secure origin, so serve it from
localhost:

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory C:\dev\nano-esp32\web
```

Open http://localhost:8000, click **Connect**, and pick the board. After the
first approval the page reconnects on its own, including after reflashing. On
the C5 DevKit, use the USB port wired to the CH343 bridge ("USB-Enhanced-SERIAL
CH343" in Device Manager).

- Only one program can hold the COM port. Close `pio device monitor` before
  connecting, and vice versa.
- Rows from earlier sweeps stay on screen, dimmed, for 60 s. A single sweep
  regularly misses weak transmitters.

### Investigating

Client-side tools for working through what's nearby. Everything except live
scan data is stored in the browser's `localStorage`, keyed by the normalized
(uppercased) address, so a name you give a device shows up wherever that MAC
appears — including across the WiFi, BLE, and monitor tables.

- **Alias** a MAC/BSSID to a human name; the badge then follows that address
  everywhere.
- **Note** free text against a device.
- **Pin** (★) a device to keep it at the top and highlighted.
- **Hide** devices you've identified as your own; "Show hidden" reveals them.
- **Filter** box matches alias, address, SSID, name, vendor, note, and probed
  network names across every table at once.
- **Signal sparkline** per device shows RSSI over recent sweeps — a rising line
  means you're getting closer, which helps physically locate a device.
- **NEW** badge marks devices first seen in the last 20 s.
- **Freeze** pauses updates so rows stop moving while you inspect.
- **Export** downloads everything on screen (plus your aliases and notes) as a
  timestamped JSON file for a report.

Per-device actions live behind the ▸ toggle on each row.

With `kJsonOutput = true` (the default) the serial output is one JSON object per
line. Set it to `false` for readable tables in `pio device monitor`.

## Commands

`pio` is on the user `PATH`. In a shell that predates that, use
`$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`.

```powershell
pio run                                  # compile the default board (C5)
pio run -e esp32c5 -e nano_esp32         # compile both boards (what CI does)
pio run -e esp32c5 -t upload             # flash the C5 over its UART bridge
pio run -e nano_esp32 -t upload          # flash the Nano over DFU
pio device monitor                       # serial console, Ctrl+C to exit
```

The C5's boot log shows `E (...) MSPI Timing: Failed to allocate dummy
cacheline for PSRAM memory barrier!`. It is harmless: the firmware prints
`PSRAM: 8192 KB` right after, confirming the PSRAM works.

## If upload fails

**C5:** the DevKit resets into the bootloader automatically through the bridge's
DTR/RTS lines. If that fails, hold **BOOT**, tap **RST**, release **BOOT**, and
upload again.

**Nano:** the Nano ESP32 uses native USB, so the COM port drops and re-appears on reset.
If upload reports "no DFU device found", **double-tap the RESET button** to force
the bootloader (the LED pulses green), then upload again.
