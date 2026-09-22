# MDL — Motorcycle Data Logger

A lightweight, ride-along data logger for a motorcycle. An ESP32 samples one or
more GY-521 (MPU-6050) inertial sensors plus GPS, writes every session to a
microSD card, and hands the files over WiFi when the bike is parked.

The point is to answer questions about how the bike is actually ridden: how far
it leans, how hard it accelerates and brakes, and where on a lap or a road that
happened.

GPS does double duty here. Besides position, its speed reading is what makes
lean angle measurable at all — a frame-mounted accelerometer cannot see
steady-state lean on a motorcycle, and speed is what lets the cornering force
be subtracted back out (see
[ADR-0010](docs/DECISIONS.md#adr-0010-gps-speed-aids-the-orientation-estimate-centripetal-correction)).

**Status:** Phase 0 — planning. No firmware written yet. See
[docs/STATUS.md](docs/STATUS.md) for exactly where things stand.

---

## Start here

If you are a new person, session, or agent picking this project up, read in
this order. Each file is short and has one job.

| # | File | What it answers |
|---|------|-----------------|
| 1 | [docs/STATUS.md](docs/STATUS.md) | What is done, what is in progress, what to do next. **Always current.** |
| 2 | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the firmware is structured and why. |
| 3 | [docs/HARDWARE.md](docs/HARDWARE.md) | Parts, wiring, pin assignments, electrical hazards. |
| 3b | [docs/BIKE.md](docs/BIKE.md) | The 2006 CBR600RR specifically — what the vehicle constrains. |
| 4 | [docs/DATA-FORMAT.md](docs/DATA-FORMAT.md) | What a session on the SD card looks like. |
| 5 | [docs/ROADMAP.md](docs/ROADMAP.md) | The phased build plan and what counts as "done" per phase. |
| 6 | [docs/DECISIONS.md](docs/DECISIONS.md) | Every design decision, its rationale, and the questions still open. |
| 7 | [docs/GLOSSARY.md](docs/GLOSSARY.md) | Jargon, part numbers, and acronyms decoded. |

[CLAUDE.md](CLAUDE.md) holds the working agreement for AI agents on this
project — chiefly, *keep these documents current as you go*.

## What it does (target behavior)

1. Bike ignition on → logger boots, mounts the SD card, finds the sensors.
2. Detects motion → opens a new session file and logs at a fixed rate.
3. Bike stops and ignition goes off → flushes and closes the file cleanly.
4. Parked → hold a button (or trigger on boot-with-no-motion) to raise a WiFi
   access point; connect from a phone and download the sessions from a web page.
5. Desktop tooling turns a session into plots and summary numbers.

## Hardware, in one line

ESP32-S3-DevKitC-1 + GY-521 (MPU-6050) IMU over I2C + microSD over SPI + GPS over
UART, powered from the bike's 12V through a protected buck converter. Full
detail and the *known electrical gotchas* are in
[docs/HARDWARE.md](docs/HARDWARE.md) — read that before wiring anything.

## Build

Toolchain is [PlatformIO](https://platformio.org/) targeting the Arduino
framework on ESP32. Once firmware exists (Phase 1):

```bash
pio run                 # compile
pio run -t upload       # flash over USB
pio device monitor       # serial console at 115200
```

`platformio.ini` does not exist yet. Creating it is the first Phase 1 task.

## Layout

```
MDL/
├── README.md            you are here
├── CLAUDE.md            working agreement for AI agents
├── docs/                the living design documents (see table above)
├── firmware/            PlatformIO project            (not yet created)
└── tools/               desktop analysis scripts      (not yet created)
```

## Prior art in this workspace

`~/OneDrive/Documents/Arduino/Motorcycle Data Logging/MPU6050_Kalman/` holds the
stock TKJ Electronics Kalman filter example for the MPU-6050. It is unmodified
and **AVR-only** — it writes the `TWBR` register directly, which does not exist
on an ESP32. Treat it as a reference for the filtering math, not as code to
copy. See [ADR-0007](docs/DECISIONS.md#adr-0007-orientation-filter).
