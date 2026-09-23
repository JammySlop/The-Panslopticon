# nano-esp32

PlatformIO project for the **Arduino Nano ESP32** (ESP32-S3): a passive WiFi and
Bluetooth LE survey tool. It alternates a WiFi sweep and a BLE sweep and sends
what it finds over USB, where a local web page displays it. It never joins a
network or connects to a device.

- **WiFi (scan):** SSID, BSSID, signal (RSSI), channel, security, cipher, WPS,
  PHY modes, advertised country, and BSSID vendor. Listen-only (passive) by
  default.
- **BLE:** address and type, signal, name, manufacturer, TX power, appearance,
  decoded product family (AirPods, Find My, iBeacon…), a rough distance
  estimate, and advertised services.
- **Monitor mode (Tier 2, receive-only):** hops channels 1/6/11 in promiscuous
  mode and parses 802.11 *frame headers only* to report devices probing for
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

Open http://localhost:8000, click **Connect**, and pick the Nano ESP32. After
the first approval the page reconnects on its own, including after reflashing.

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

PlatformIO lives in an isolated venv and is not on `PATH` by default. Either add
`C:\Users\jerre\.platformio\penv\Scripts` to `PATH`, or use the full path:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

& $pio run                  # compile
& $pio run -t upload        # flash over USB (DFU)
& $pio device monitor       # serial console, Ctrl+C to exit
```

## If upload fails

The Nano ESP32 uses native USB, so the COM port drops and re-appears on reset.
If upload reports "no DFU device found", **double-tap the RESET button** to force
the bootloader (the LED pulses green), then upload again.
