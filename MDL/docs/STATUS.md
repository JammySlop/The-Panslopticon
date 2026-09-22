# Status

**The one file to read first.** If you are picking this project up, everything
you need to resume is here. Update it every session — see
[../CLAUDE.md](../CLAUDE.md).

---

**Phase:** 0 — Planning and documentation
**Last updated:** 2026-09-22
**Branch:** `mdl/planning-docs`
**Hardware built:** none
**Firmware written:** none

## Where things stand

The project is documented but not started. There is no `platformio.ini`, no
source, and nothing wired. Every number in these documents comes from
datasheets and reasoning — **nothing has been measured on hardware.**

Settled so far (details in [DECISIONS.md](DECISIONS.md)):

- ESP32 + GY-521 over I2C, microSD over SPI, GPS over UART
- Log to SD during the ride, offload over a WiFi AP when parked
- PlatformIO with the Arduino framework
- Source → bus → sink architecture, sampler and writer on separate cores
- CSV log format, versioned, with GPS carry-forward and staleness marking
- Primary goals: lean angle, acceleration and braking, GPS lap/session logging
- Built to extend later to a display, CAN, clutch switch, brake pressure

## Next actions

1. **Settle axis and sign conventions** — the `TODO(human)` in
   [HARDWARE.md](HARDWARE.md#axis-and-sign-conventions). Blocks Phase 1, since
   the orientation code and column meanings depend on it.
2. Open the Phase 0 PR against `main`.
3. Start Phase 1: `platformio.ini`, `config.h`, I2C bring-up, `WHO_AM_I` check.

## Open questions

Full detail in [DECISIONS.md](DECISIONS.md#open-questions).

| | Question | Blocks |
|---|---|---|
| Q1 | How many IMUs and where? Deferred by the owner. | Wiring, schema width, sample rate |
| Q2 | What ends a session? | Phase 2 |
| Q3 | Is a power-loss flush achievable? | Phase 6 |
| Q4 | Which ESP32 board variant? | Pin table |

## Known risks

- **Steady-state lean is not observable by the accelerometer.** The primary
  goal depends on getting this right, and it cannot be validated on a bench —
  only on a moving bike. See [ARCHITECTURE.md](ARCHITECTURE.md) and ADR-0007.
- **Vibration kills unsoldered connections.** Fails on the road, not in the
  garage, producing data that is fine until it is interesting.
- **5V on the GY-521 can kill the ESP32** through the breakout's I2C pull-ups.
  See the traps in [HARDWARE.md](HARDWARE.md#traps).
- **Power-loss flush may not be possible** with the chosen converter's
  capacitance. Unknown until measured.

## Session log

Newest first. One or two lines each: what changed, and what the next session
should know.

### 2026-09-22 — Project created
Set up `MDL/` and the document system on branch `mdl/planning-docs`. Settled
storage, toolchain, goals, and the source/sink architecture with the owner;
sensor count deliberately left open (Q1). Found prior art at
`~/OneDrive/Documents/Arduino/Motorcycle Data Logging/MPU6050_Kalman/` — the
stock TKJ Kalman example, unmodified and AVR-only, useful as filter reference
but not portable as-is. Left one `TODO(human)` for axis conventions.
