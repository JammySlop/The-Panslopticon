# obd-pda

A pocket OBD-II / CAN bus tool: an ESP32-C3 Super Mini (Tenstar Robot) with a
2" 240x320 SPI display and a 5-way navigation switch. It talks to the car through a
**generic ELM327 WiFi dongle** (the cheap "WiFi_OBDII" kind) plugged into the
OBD-II port, so the handheld needs no wiring to the car at all: just USB
power.

It can also run with its own CAN transceiver wired straight to the OBD-II port
instead of a dongle ("direct CAN"). That is optional hardware, chosen in
Settings; see [Direct CAN (optional)](#direct-can-optional).

> **Status: compiled, not yet run on hardware.** Every pin, timing and formula
> below comes from datasheets and the OBD-II spec, not from a bench test. Treat
> the first power-up as a test, ideally with USB only and the car not connected.

## What it does

| Screen | What you get | Transmits? |
|---|---|---|
| **Live data** | Tiles for up to 18 standard PIDs (RPM, speed, coolant, load, throttle, MAF/MAP, fuel trims, ECU volts...). Only PIDs the car reports as supported are shown. Up/down or left/right page through them. | Yes: OBD service 01 requests |
| **CAN sniffer** | Every arbitration ID on the bus, sorted, with its latest data, changed bytes highlighted in yellow, and a frames/s rate. SELECT pauses. Through an ELM327 this is `ATMA` and is *lossy* (see below). | **No**: silent monitoring, does not even ACK |
| **Trouble codes** | Check-engine light state, stored (service 03) and pending (service 07) codes, and which ECU reported each. SELECT re-reads. | Yes: read-only requests |
| **Vehicle info** | VIN, connection details (ELM version, protocol, dongle IP, WiFi signal), how many ECUs answer, supported PID count, battery voltage at the port. | Yes: read-only requests |
| **Settings** | Connection type (ELM327 WiFi / direct CAN), dongle WiFi network (scan and pick), CAN bitrate for direct mode, backlight, reconnect. Saved to flash. | No |

Nothing here writes to the car: there is no "clear codes" (service 04) yet, on
purpose. Every request is a standard OBD-II read.

## How the ELM327 link works

1. The C3 joins the dongle's access point (default `WiFi_OBDII`, open; pick
   another on **Settings → Dongle WiFi network**, which scans and lists likely
   dongles first).
2. It opens a TCP connection to the dongle. The address is the WiFi gateway the
   dongle hands out over DHCP (almost always `192.168.0.10`), on port 35000,
   then 23.
3. It initialises the ELM: `ATZ`, echo off, headers **on**, spaces on,
   adaptive timing, protocol auto (`ATSP0`), then sends `0100` so the ELM
   finds the car's protocol, and reads it back with `ATDPN`.
4. With headers on, the ELM prints every reply as a raw CAN frame
   (`7E8 06 41 00 BE 3F A8 13`, PCI byte included), so the same ISO-TP
   reassembly code serves both links, and replies from several ECUs are kept
   apart. The ELM sends the ISO-TP flow control itself.
5. Live-data requests carry the ELM "expected responses" digit (`010C1`), so
   the ELM answers as soon as one ECU replies instead of waiting out its
   timeout. If an old clone shows only `--` on the live screen, set
   `kElmUseResponseCount = false` in `config.h`.
6. Battery voltage in the header comes from the dongle's `ATRV`.

The connection opens the first time a screen needs the car, and reopens after
a dropout (SELECT on the error message retries).

**Supported:** cars on ISO 15765-4 CAN (ELM protocols 6-9: 11/29-bit, 500/250k).
That is every US car from 2008, and most EU/Asian cars from the mid-2000s.
Older K-line and J1850 cars (protocols 1-5) are detected and reported as not
supported yet.

**The sniffer over an ELM327 is lossy.** `ATMA` prints every frame as text over
the dongle's internal UART, which tops out at a few hundred frames/s, while a
busy powertrain bus carries 1,000-2,000. The ELM then reports `BUFFER FULL`;
we count those and restart monitoring. The footer shows the drop count and
the header shows `lossy`. Fine for spotting which IDs exist and watching slow
ones; for a complete capture use direct CAN. Monitoring uses `ATCSM1`
(silent mode), so the ELM does not acknowledge frames. Clones older than
v1.4b may not support that; the sniffer still works, but the ELM then ACKs.

## Configuring over USB

Typing a WiFi password with a 5-way switch is miserable, so text settings are
also available on the USB serial console (`pio device monitor`, 115200):

```
show                 current settings
ssid <name>          dongle WiFi network
pass <password>      dongle WiFi password ("pass" alone clears it)
host <ip>|auto       dongle address (auto = WiFi gateway)
port <n>|auto        dongle TCP port
link elm|can         ELM327 over WiFi, or direct CAN transceiver
nav                  which switch direction is held (and the ladder voltage)
```

## Controls: 5-way switch

One 5-way tactile switch (up / down / left / right / push) does everything. The
board's BOOT button also works as the push.

| Input | Menus and lists | Live data | Sniffer | Settings |
|---|---|---|---|---|
| Up / down | Move | Previous / next page | Scroll a row | Move |
| Left | Back (trouble codes, info, WiFi list) | Previous page | Up a screenful | Previous value |
| Right | Open (main menu) | Next page | Down a screenful | Next value / open |
| Push | Open / do it | Next page | Pause | Next value / open |
| Hold push (0.6 s) | Back to the menu | ← | ← | ← |

Directions auto-repeat when held. A held push becomes Back, so the push acts
on release.

## Parts

### ELM327 build (default)

| Part | Notes |
|---|---|
| ESP32-C3 Super Mini (Tenstar Robot) | WiFi 2.4 GHz, native USB, 4 MB flash |
| 2" 240x320 ST7789 SPI display | 8 pins: GND VCC SCL SDA RES DC CS BLK |
| 5-way tactile switch | A bare 5-way switch (e.g. Alps SKQUCAA010 style) or a "5-way navigation button module". Ignore any SET/RST pins on modules. |
| ELM327 WiFi dongle | Any generic "WiFi_OBDII" style. Bluetooth-only dongles won't work: the C3 has BLE but not Bluetooth Classic, which most of those use. |
| USB-C power | The car's USB socket or a power bank |

Some Super Mini batches have a weak WiFi antenna. With the dongle a metre away
in the same car that rarely matters; the info screen shows the signal (dBm).

### Wiring

```
                 ESP32-C3 Super Mini (USB-C at the top)
                  ┌─────────────┐
            5V  ──┤             ├── 5    LCD DC
 NAV COM    G   ──┤             ├── 6    LCD SCL (SPI clock)
            3V3 ──┤             ├── 7    LCD SDA (SPI MOSI)
 NAV UP     4   ──┤             ├── 8    (on-board LED: activity)
 NAV DOWN   3   ──┤             ├── 9    (on-board BOOT button: also PUSH)
 NAV LEFT   2   ──┤             ├── 10   LCD CS
 NAV RIGHT  1   ──┤             ├── 20   LCD BLK (backlight PWM)
 NAV PUSH   0   ──┤             ├── 21   LCD RES
                  └─────────────┘
```

The switch's five contacts and common line run straight down the left header:
common to **G**, then up/down/left/right/push to **GPIO 4, 3, 2, 1, 0**. Each
contact just shorts its pin to ground; the C3's internal pull-ups do the rest,
so no other parts are needed. Most module boards label the pins `COM UP DWN
LFT RHT MID`; on a bare switch, find which pin is common with a multimeter in
continuity mode.

| GPIO | Connects to | Why this pin |
|---|---|---|
| 0 | Switch **push / centre** | |
| 1 | Switch **right** | |
| 2 | Switch **left** | Strapping pin; fine as a pulled-up input (just don't hold LEFT while powering on). |
| 3 | Switch **down** | |
| 4 | Switch **up** | |
| 5 | LCD **DC** | |
| 6 | LCD **SCL** | SPI clock (the "SCL" label is not I2C). |
| 7 | LCD **SDA** | SPI MOSI. |
| 8 | On-board LED | Strapping pin, already wired to the LED; flashes on received frames. |
| 9 | On-board BOOT button | Strapping pin, already wired to the button; a second push button. |
| 10 | LCD **CS** | |
| 20 | LCD **BLK** | UART0 RX; free because serial runs over native USB. PWM dimming. |
| 21 | LCD **RES** | UART0 TX; the boot ROM log toggles it, which just resets the display before it is initialised. |

Display power: **VCC → 3V3**, **GND → G**. All pins are also in
[`include/config.h`](include/config.h); change them there.

This uses every free GPIO, which is why the direct-CAN option below needs the
other way of wiring the switch.

## Direct CAN (optional)

Instead of a dongle, the C3's built-in CAN controller (TWAI) can sit on the
bus itself through a transceiver. It's faster, and the sniffer sees every
frame, but it means wiring into the OBD-II port.

The transceiver needs GPIO 0 and 1 and the battery sense needs GPIO 3, which
the switch uses in the default wiring. So this build moves the whole switch
onto **one** pin through a resistor ladder, and is flashed from its own
environment:

```
pio run -e c3_supermini_ladder -t upload
```

Then choose **Settings → Connection → Direct CAN transceiver** (or `link can`
on the console). The default build refuses direct CAN, since its switch sits
on the CAN pins.

### 5-way switch on a resistor ladder

```
 3V3 ──[10k]──┬──────────────► GPIO4 (ADC)
              ├── PUSH  ─────────────── G        0 V
              ├── UP    ──[1k]───────── G     0.30 V
              ├── DOWN  ──[3.3k]─────── G     0.82 V
              ├── LEFT  ──[6.8k]─────── G     1.34 V
              └── RIGHT ──[15k]──────── G     1.98 V
                                   (released: 3.3 V)
```

The switch's common goes to the GPIO4 node, and each direction goes to ground
through its own resistor. Use 1% resistors. The voltages are calculated, not
measured; hold each direction and type `nav` on the serial console to see the
reading, then adjust `kNavLadderThresholdsMv` in `config.h` if a direction
lands in the wrong band. A ladder can only report one direction at a time,
which is all the UI needs.

### Transceiver and power parts

| Part | Notes |
|---|---|
| SN65HVD230 CAN transceiver module | Must be **3.3 V**. A 5 V TJA1050/MCP2551 module will not work from 3.3 V logic without level shifting. |
| 12 V to 5 V buck converter | Mini-360, MP1584 or similar, set to 5.0 V **before** connecting the board. |
| Resistors | 100k + 18k (battery sense), 10k (CAN TX pull-up), 10k + 1k + 3.3k + 6.8k + 15k (switch ladder) |
| Diodes | SS14 / 1N5819 Schottky (buck output to 5V pin); SMBJ18A TVS recommended |
| Fuse | 1 A, inline on OBD pin 16 |
| OBD-II male plug / pigtail | Pins 4, 5, 6, 14 and 16 |

| GPIO | Connects to |
|---|---|
| 4 | Switch ladder (above) |
| 2 | Free |
| 0 | Transceiver **CTX / TXD**, with a 10k pull-up to 3V3 so the bus stays recessive while the chip boots |
| 1 | Transceiver **CRX / RXD** |
| 3 | Battery divider midpoint (ADC1) |

### OBD-II port

```
   OBD-II female socket on the car (looking into it)
    ┌───────────────────────────────┐
    │  1   2   3   4   5   6   7   8 │
     \ 9  10  11  12  13  14  15  16/
      └───────────────────────────┘
   4 = chassis ground    6 = CAN-H    14 = CAN-L    16 = +12 V (always on)
   5 = signal ground
```

| OBD-II pin | Goes to |
|---|---|
| 16 (+12 V battery, always live) | 1 A fuse → buck IN+, and → 100k → GPIO3 |
| 4 + 5 (grounds) | buck IN−, board G, transceiver GND |
| 6 (CAN-H) | transceiver CANH |
| 14 (CAN-L) | transceiver CANL |

### Power

```
 OBD 16 ──[1A fuse]──┬──────────────► buck IN+ ── buck OUT+ ──|>|── board 5V
                     │                                       SS14
                   [TVS SMBJ18A]                 buck OUT− ───────── board G
                     │
 OBD 4/5 ────────────┴──────────────► buck IN−

 OBD 16 (after fuse) ──[100k]──┬──[18k]── G
                               ├──[100nF]── G
                               └──► GPIO3
```

- The Schottky diode stops the buck converter and USB from back-feeding each
  other, so USB can stay plugged in for the serial log.
- The 100k/18k divider maps 0-16 V to 0-2.44 V, inside the C3 ADC's accurate
  range. Trim `kVbatCalibration` in `config.h` against a multimeter.
- Pin 16 is always live; unplug when parked (no sleep mode yet).

### Transceiver gotcha: the 120 Ω terminator

Most SN65HVD230 breakout boards have a **120 Ω resistor between CANH and
CANL**. A car's bus is already terminated at both ends; a third terminator
drops it from 60 Ω to 40 Ω and can cause errors. **Remove it** for use on a
car. Also check the module's **Rs** pin is tied to GND (high-speed mode).

In direct mode the bitrate is auto-detected: it listens passively at 500k and
250k, and only if the bus is silent at both does it send an OBD request at
each rate.

## Build and flash

```
cd obd-pda
pio run -t upload
pio device monitor
```

If the board doesn't show up as a serial port, hold BOOT, tap RESET, release
BOOT, and upload again.

Uses the same pinned [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform as `esp32-recon` (Arduino core 3.x) and
[LovyanGFX](https://github.com/lovyan03/LovyanGFX) for the display.

### First power-up checklist

1. USB only, no dongle. The menu should appear. If the colours are inverted,
   flip `kLcdInvert`; if the image is shifted, the module may need an offset
   in `src/display.cpp`.
2. Switch: up/down move the menu highlight, push (or BOOT) opens, holding
   push goes back. If a direction does the wrong thing, type `nav` on the
   serial console while holding it to see which key the firmware thinks it is.
3. Dongle in the car, ignition on. **Settings → Dongle WiFi network**: the
   dongle's network should be listed in cyan near the top. Pick it.
4. **Vehicle info**: it should connect, show the ELM version, protocol and VIN.
   If it stops at "Joining...", check the network name and password
   (`show` on the serial console); at "Connecting...", the dongle may use a
   different address or port (`host` / `port`).

No car handy? The [ELM327-emulator](https://github.com/Ircama/ELM327-emulator)
Python package can pretend to be a dongle on a laptop (`elm -n 35000`): put
the laptop on a hotspot, point `ssid`, `pass` and `host` at it over the
console. (Untested with this firmware.)

## Code layout

| File | Role |
|---|---|
| `include/config.h` | Pins, timeouts, ELM defaults, UI timing |
| `src/link.*` | The link interface every screen talks to, and which link is active |
| `src/elm327_link.*` | ELM327 over WiFi: join, TCP, AT init, line parsing into CAN frames, `ATMA` monitor |
| `src/direct_can_link.*`, `src/can_bus.*` | Direct CAN: TWAI controller, listen-only vs normal, bitrate auto-detect, ISO-TP flow control |
| `src/obd.*` | OBD-II: ISO-TP reassembly (multi-frame, multiple ECUs, 11/29-bit), PID decoding, DTCs, VIN |
| `src/settings.*` | Persisted settings (NVS) |
| `src/console.*` | USB serial console for text settings |
| `src/display.*` | LovyanGFX ST7789 setup |
| `src/buttons.*` | 5-way switch (digital or ladder): debounce, hold-for-back, auto-repeat |
| `src/vbat.*` | Battery voltage divider (direct-CAN build) |
| `src/ui.*` | Screens and navigation |

## Roadmap

- [ ] Bench test: display, 5-way switch (both wirings)
- [ ] Car test with an ELM327 WiFi dongle: connect, live data, trouble codes, VIN, sniffer
- [ ] Car test in direct-CAN mode
- [ ] K-line / J1850 cars through the ELM (protocols 1-5: different header format, no ISO-TP)
- [ ] Direct CAN with 29-bit OBD addressing (requests to 18DB33F1); the ELM link already handles it
- [ ] DTC descriptions (an on-flash table of the common P0xxx codes)
- [ ] Clear codes (service 04), behind a confirmation screen
- [ ] Sleep (and drop WiFi) when the battery voltage says the engine has been off for a while
- [ ] Stream sniffed frames over USB in a SavvyCAN-compatible format (GVRET)
- [ ] Log to SD card, or to the C3's flash
- [ ] Graphs for live PIDs
