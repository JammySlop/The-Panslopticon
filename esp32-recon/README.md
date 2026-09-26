# esp32-recon

PlatformIO project for a passive WiFi and Bluetooth LE survey tool. It alternates
a WiFi sweep and a BLE sweep and sends what it finds over USB, where a local web
page displays it. It is receive-only: it never joins a network, connects to a
device, or transmits.

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
  5 GHz channel, which takes about 17 s at the default dwell time. The **Flag**
  column marks weak security: `OPEN`, `WEP`, `WPA1`, and `WPA1-MIX` (WPA/WPA2
  mixed mode, which keeps TKIP enabled).
- **BLE:** address and type, signal, name, manufacturer, TX power, appearance,
  decoded product family (AirPods, Find My, iBeacon…), a rough distance
  estimate, and advertised services. Passive by default (`kBleActiveScan`), so
  only names carried in advertisements show up; active scanning would transmit
  scan requests.
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

## Touch display (C5 only)

The C5 build drives a 2.8" 240x320 SPI touch display (ILI9341 panel, XPT2046
touch controller: the common 14-pin board) as a standalone dashboard. It works
with or without the host service running. Four pages: **Summary**, **WiFi**
(strongest networks, 5 GHz channels in blue, weak security in red),
**Bluetooth LE** (best available name, signal, rough distance), and
**Monitor** (packets per channel, deauth alerts, probing clients). Tap
anywhere for the next page.

Wiring to the ESP32-C5-DevKitC-1 (display pin -> DevKit header label):

| Display | DevKit | Notes |
|---------|--------|-------|
| VCC     | 5V     | The display board has its own 3.3 V regulator. 3V3 also works on most boards. |
| GND     | G      | |
| CS      | 6      | |
| RESET   | 4      | |
| DC      | 5      | |
| SDI (MOSI) | 8   | Shared with T_DIN. |
| SCK     | 10     | Shared with T_CLK. |
| LED     | 1      | Backlight. If your board has no transistor on LED (it gets dim or the board resets), wire LED to 3V3 instead. |
| SDO (MISO) | *not connected* | This panel doesn't release the line, which corrupts touch reads. The firmware never reads the panel. |
| T_CLK   | 10     | Same pin as SCK. |
| T_CS    | 0      | |
| T_DIN   | 8      | Same pin as SDI. |
| T_DO    | 9      | |
| T_IRQ   | 24     | |

These avoid the C5's strapping pins (2, 3, 7, 25-28), the console UART (11,
12), USB (13, 14), and the RGB LED (27). Pins are set in `include/config.h`.
To build without the display, remove `-DRECON_DISPLAY=1` from `platformio.ini`.

The frame is drawn into a buffer in PSRAM and sent in one go, so pages don't
flicker. Drawing and touch run in their own task, so taps respond during the
17 s WiFi scan.

## Host service and web interface

`host/recon_service.py` runs on the computer the board is plugged into. It
finds the board by USB ID, reads its JSON lines, records everything in a SQLite
database, and serves the web page plus a small JSON API on localhost. It only
reads from the serial port and never sends anything to the board. Recording
continues whether or not a browser is open.

One-time setup (needs Python 3.10+):

```powershell
cd esp32-recon   # from the repository root
python -m venv host\.venv
host\.venv\Scripts\python.exe -m pip install -r host\requirements.txt
```

Run it:

```powershell
host\.venv\Scripts\python.exe host\recon_service.py            # auto-detects the board
host\.venv\Scripts\python.exe host\recon_service.py --port COM8 # or name the port
```

Then open http://localhost:8000 in any browser. Use `localhost`, not
`127.0.0.1`: the page's origin must match the one the old Web Serial page used
so it can offer to import aliases saved there.

- **The service holds the COM port.** Stop it (Ctrl+C) before
  `pio run -t upload` or `pio device monitor`, then start it again. It
  reconnects on its own if the board is unplugged and plugged back in.
- On the C5 DevKit, use the USB port wired to the CH343 bridge
  ("USB-Enhanced-SERIAL CH343" in Device Manager).
- The page shows every device in the database. It loads everything once, then
  fetches only devices updated since the last sweep it has. Rows missing from
  the latest sweep are dimmed; "Last seen" says how long ago they were heard.
  Use the filter box to narrow things down.
- The API and page answer only to `Host: localhost` / `127.0.0.1` on the
  service's port, and the service binds to 127.0.0.1.

### Database

`data/recon.db` (change with `--db`; git-ignored). History is never deleted,
which is roughly 20-30 MB per day of continuous scanning.

| Table              | Contents                                                          |
|--------------------|-------------------------------------------------------------------|
| `sweeps`           | One row per message from the board: kind, host time, board cycle and uptime |
| `devices`          | One row per device per table (`wifi`, `ble`, `clients`, `aps`): first/last seen and the latest record as JSON |
| `sightings`        | Every appearance of a device in a sweep, with RSSI and channel    |
| `channel_activity` | Monitor-mode packet counts per channel per sweep                  |
| `alerts`           | Deauth-flood alerts                                               |
| `annotations`      | Your aliases, notes, pins and hidden flags, keyed by address      |

Times are milliseconds since the Unix epoch, host clock. The file is plain
SQLite, so it can be queried directly, e.g.:

```sql
SELECT addr, datetime(first_seen / 1000, 'unixepoch', 'localtime') AS first,
       json_extract(data, '$.ssid') AS ssid
FROM devices WHERE kind = 'wifi' ORDER BY first_seen DESC LIMIT 20;
```

Tests: `host\.venv\Scripts\python.exe -m unittest discover -s host -v`.

### Investigating

Tools for working through what's nearby. Aliases, notes, pins and hidden flags
are stored in the database, keyed by the normalized (uppercased) address, so a
name you give a device shows up wherever that MAC appears, including across the
WiFi, BLE, and monitor tables. The first time the page loads it offers to import
anything saved by the older, browser-only version (kept in `localStorage`);
existing database entries are never overwritten.

- **Alias** a MAC/BSSID to a human name; the badge then follows that address
  everywhere.
- **Note** free text against a device.
- **Pin** (★) a device to keep it at the top and highlighted.
- **Hide** devices you've identified as your own; "Show hidden" reveals them.
- **Filter** box matches alias, address, SSID, name, vendor, note, and probed
  network names across every table at once.
- **Signal sparkline** per device shows its last 40 RSSI readings from the
  database, so it survives page reloads and service restarts. A rising line
  means you're getting closer, which helps physically locate a device. A
  ▲ closer / ▼ farther badge appears when the signal has moved by 6 dB or more
  over the last five minutes.
- **NEW** badge marks devices first seen in the last 20 s. The details panel
  shows the date a device was first ever recorded.
- **Freeze** pauses updates so rows stop moving while you inspect.
- **Export** downloads everything on screen (plus your annotations) as a
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
