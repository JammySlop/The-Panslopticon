# obd-pda

A pocket OBD-II / CAN bus tool: an ESP32-C3 Super Mini (Tenstar Robot) with a
2" 240x320 SPI display, three buttons and a CAN transceiver, powered from the
car's OBD-II port.

> **Status: compiled, not yet run on hardware.** Every pin, timing and formula
> below comes from datasheets and the OBD-II spec, not from a bench test. Treat
> the first power-up as a test, ideally with USB only and the car not connected.

## What it does

| Screen | What you get | Transmits? |
|---|---|---|
| **Live data** | Tiles for up to 18 standard PIDs (RPM, speed, coolant, load, throttle, MAF/MAP, fuel trims, ECU volts...). Only PIDs the car reports as supported are shown. UP/DOWN page through them. | Yes: OBD service 01 requests |
| **CAN sniffer** | Every arbitration ID on the bus, sorted, with its latest data, changed bytes highlighted in yellow, and a frames/s rate. SELECT pauses. | **No**: listen-only mode, does not even ACK |
| **Trouble codes** | Check-engine light state, stored (service 03) and pending (service 07) codes, and which ECU reported each. SELECT re-reads. | Yes: read-only requests |
| **Vehicle info** | VIN, detected bitrate, how many ECUs answer, supported PID count, battery voltage at the port. | Yes: read-only requests |
| **Settings** | CAN bitrate (auto / 500k / 250k / 125k), backlight, re-detect bus. Saved to flash. | Only when re-detecting |

Nothing here writes to the car: there is no "clear codes" (service 04) yet, on
purpose. Every request is a standard OBD-II read.

The bitrate is auto-detected the first time it is needed: it listens passively
at 500k and 250k, and only if the bus is silent at both (common: many cars hide
the powertrain bus behind a gateway that only speaks when spoken to) does it
send an OBD "supported PIDs" request at each rate.

### Buttons

| Button | Short press | Hold (0.6 s) |
|---|---|---|
| UP / DOWN | Move / scroll / page | Auto-repeat |
| SELECT (the board's BOOT button, plus an optional external one) | Open / action | Back to the menu |

## Parts

| Part | Notes |
|---|---|
| ESP32-C3 Super Mini (Tenstar Robot) | Native USB, 4 MB flash. The C3 has a built-in CAN 2.0 controller (TWAI). |
| 2" 240x320 ST7789 SPI display | 8 pins: GND VCC SCL SDA RES DC CS BLK |
| SN65HVD230 CAN transceiver module | Must be a **3.3 V** transceiver. A 5 V TJA1050/MCP2551 module will not work from 3.3 V logic without level shifting. |
| 12 V to 5 V buck converter | Mini-360, MP1584 or similar, set to 5.0 V **before** connecting the board. 500 mA is plenty. |
| 2x tactile buttons | UP and DOWN. SELECT is the board's BOOT button. |
| Resistors | 100k + 18k (battery sense), 10k (CAN TX pull-up) |
| Diodes | 1N5819 / SS14 Schottky (buck output to 5V pin); SMBJ18A TVS (12 V input) recommended |
| Fuse | 1 A, inline on OBD pin 16 |
| OBD-II male plug / pigtail | Only pins 4, 5, 6, 14 and 16 are needed |

## Wiring

### Pin map

```
                 ESP32-C3 Super Mini (USB-C at the top)
                  ┌─────────────┐
            5V  ──┤             ├── 5    LCD DC
            G   ──┤             ├── 6    LCD SCL (SPI clock)
            3V3 ──┤             ├── 7    LCD SDA (SPI MOSI)
 BTN UP     4   ──┤             ├── 8    (on-board LED: CAN activity)
 VBAT sense 3   ──┤             ├── 9    (on-board BOOT button: SELECT)
 BTN DOWN   2   ──┤             ├── 10   LCD CS
 CAN RX     1   ──┤             ├── 20   LCD BLK (backlight PWM)
 CAN TX     0   ──┤             ├── 21   LCD RES
                  └─────────────┘
```

| GPIO | Connects to | Why this pin |
|---|---|---|
| 0 | Transceiver **CTX / TXD** (+10k pull-up to 3V3) | Not a strapping pin. The pull-up holds the bus recessive while the chip boots. |
| 1 | Transceiver **CRX / RXD** | |
| 2 | Button DOWN → GND | Strapping pin; fine as a pulled-up button (just don't hold DOWN while powering on). |
| 3 | Battery divider midpoint | ADC1 channel. |
| 4 | Button UP → GND | |
| 5 | LCD **DC** | |
| 6 | LCD **SCL** | SPI clock (the "SCL" label is not I2C). |
| 7 | LCD **SDA** | SPI MOSI. |
| 8 | On-board LED | Strapping pin, already wired to the LED; used as an activity light. |
| 9 | On-board BOOT button | Strapping pin, already wired to the button; used as SELECT. |
| 10 | LCD **CS** | |
| 20 | LCD **BLK** | UART0 RX; free because serial runs over native USB. PWM dimming. |
| 21 | LCD **RES** | UART0 TX; the boot ROM log toggles it, which just resets the display before it is initialised. |

Display power: **VCC → 3V3**, **GND → G**. Transceiver power: **3V3 → 3V3**,
**GND → G**.

All pins are also in [`include/config.h`](include/config.h); change them there.

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
  other when both are connected, so you can leave USB plugged in while it is in
  the car (useful for the serial log).
- The 100k/18k divider maps 0-16 V to 0-2.44 V, inside the C3 ADC's accurate
  range. Trim `kVbatCalibration` in `config.h` against a multimeter.
- Pin 16 is always live, so the tool draws current with the engine off. There
  is no sleep mode yet (see roadmap): unplug it when you park.

### Transceiver module gotcha: the 120 Ω terminator

Most SN65HVD230 breakout boards have a **120 Ω resistor between CANH and
CANL** (often labelled R2 or "120"). A car's bus is already terminated at both
ends; a third terminator drops it from 60 Ω to 40 Ω and can cause errors.
**Remove or cut that resistor** for use on a car. Keep it only for a bench
setup with just two nodes.

Also check the module's **Rs** pin is tied to GND (high-speed mode); most
modules do this already.

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

1. USB only, nothing connected to the car. The menu should appear. If the
   colours are inverted, flip `kLcdInvert`; if the image is shifted, the module
   may need an offset in `src/display.cpp`.
2. Buttons: UP/DOWN move the menu highlight; BOOT opens; holding BOOT goes back.
3. Header shows `USB` (no 12 V). Connect a bench supply to the 12 V input: the
   header should show its voltage.
4. In the car, ignition on: open **CAN sniffer** first. It only listens, so it
   is the safe way to confirm the bus wiring and bitrate.

## Code layout

| File | Role |
|---|---|
| `include/config.h` | Pins, bitrates, timeouts, UI timing |
| `src/display.*` | LovyanGFX ST7789 setup |
| `src/can_bus.*` | TWAI controller: start/stop, listen-only vs normal, bitrate auto-detect, bus-off recovery |
| `src/obd.*` | OBD-II over CAN: ISO-TP reassembly (multi-frame, multiple ECUs), PID decoding, DTCs, VIN |
| `src/buttons.*` | Debounce, long press, auto-repeat |
| `src/vbat.*` | Battery voltage from the divider |
| `src/ui.*` | Screens and navigation |

## Roadmap

- [ ] Bench test: display, buttons, voltage sense
- [ ] Car test: sniffer, then live data, then trouble codes
- [ ] DTC descriptions (an on-flash table of the common P0xxx codes)
- [ ] Clear codes (service 04), behind a confirmation screen
- [ ] Sleep when the battery voltage says the engine has been off for a while
- [ ] Stream sniffed frames over USB in a SavvyCAN-compatible format (GVRET)
- [ ] Log to SD card, or to the C3's flash
- [ ] Graphs for live PIDs
- [ ] 29-bit OBD addressing (some vehicles use 0x18DB33F1)
