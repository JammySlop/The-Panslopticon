# Decisions

Every choice with a real alternative, why it went the way it did, and what
would make it wrong. Newest decisions go at the bottom. Reversed decisions are
marked `Superseded` and kept — never deleted.

Status values: `Accepted` · `Proposed` · `Superseded by ADR-XXXX`

---

## ADR-0001: ESP32 as the MCU

**Status:** Accepted (2026-09-22) — given, not chosen

Specified up front. Worth recording what it buys and costs anyway.

**Buys:** two cores (sampling and storage genuinely run in parallel), WiFi for
offload with no extra hardware, a built-in CAN controller for the future engine
bus, plenty of RAM for buffering, and a mature Arduino ecosystem.

**Costs:** 3.3V-only I/O with no 5V tolerance, ADC2 unusable while WiFi is on,
and appreciable current draw — fine on a bike, irrelevant next to the headlight.

---

## ADR-0002: Source/sink architecture rather than a monolithic loop

**Status:** Accepted (2026-09-22)

Sensors are *sources*, storage and display are *sinks*, and a queue sits between
them. See [ARCHITECTURE.md](ARCHITECTURE.md).

**Why:** Two reasons, one immediate and one strategic. Immediately, SD cards
stall for tens of milliseconds during internal block erase; with a single loop
that stall lands in the sample timing and corrupts the time base. Strategically,
the stated goal includes a display, CAN, a clutch input, and brake pressure —
four additions that in a monolithic loop each mean surgery on the logging path,
and here each mean one new file.

**Cost:** More structure than a first-cut logger needs. A queue, an interface,
and task setup exist before a single byte is logged.

**Wrong if:** The project stays a single IMU writing to a card forever, in
which case this is overhead. The extensibility requirement is what settles it.

---

## ADR-0003: SD card as primary storage, WiFi for offload

**Status:** Accepted (2026-09-22) — chosen by the repo owner

Log to microSD during the ride; raise a WiFi access point when parked to
download sessions over HTTP.

**Why:** A card cannot drop out at 70mph the way a radio link can, and it keeps
whole rides rather than minutes. WiFi offload means the card never has to be
physically removed, which matters because the unit will be in a sealed
enclosure bolted to a vibrating frame.

**Rejected:** Live BLE streaming (loses data on dropouts, needs a phone app),
onboard flash only (a few MB — minutes, not rides), bare SD with card removal
(opening a sealed enclosure after every ride).

---

## ADR-0004: PlatformIO, Arduino framework

**Status:** Accepted (2026-09-22) — chosen by the repo owner

**Why:** Library versions are pinned in `platformio.ini`, so the build is
reproducible on another machine or in CI, and it builds from the command line —
which means an agent can compile and verify its own work instead of guessing.
The Arduino framework underneath keeps the ESP32/Arduino library ecosystem
available.

**Rejected:** Arduino IDE (implicit global library state, no CLI verification),
ESP-IDF (more control than this needs, no Arduino library reuse).

---

## ADR-0005: Logging and WiFi never run simultaneously

**Status:** Accepted (2026-09-22)

The state machine treats `LOGGING` and `OFFLOAD` as mutually exclusive.

**Why:** WiFi on the ESP32 causes current spikes and long non-deterministic
interrupt latencies, and it disables the ADC2 pins. Running it during a ride
would put jitter into the sample timing to serve a use case — live telemetry —
that has been explicitly deferred.

**Cost:** No live streaming while riding. Accepted deliberately.

**Wrong if:** Live telemetry to a phone becomes a requirement. Revisit then,
with measurements rather than assumptions.

---

## ADR-0006: CSV log format, not binary

**Status:** Accepted (2026-09-22)

**Why:** Debuggability during development beats efficiency at this scale. A CSV
opens in any tool and a half-written one is still readable — which matters
because power loss mid-write is the expected shutdown path, not an edge case.
Packing binary records costs ~3× less storage, and storage is not scarce here.

**Revisit if:** sample rate exceeds ~200 Hz, channel count roughly doubles, or
profiling shows formatting cost eating the sampler's headroom. The schema is
versioned specifically so this can change later. See
[DATA-FORMAT.md](DATA-FORMAT.md).

---

## ADR-0007: Orientation filter

**Status:** Proposed (2026-09-22) — decide in Phase 3, with data

Which filter turns accelerometer and gyro readings into a lean angle.

**The motorcycle-specific problem:** In a steady corner a bike leans until
gravity plus cornering force points through the tires, so a frame-mounted
accelerometer reads near-upright regardless of actual lean angle. The
accelerometer cannot observe steady-state lean. Any filter that trusts it the
way a self-balancing robot does will report a bike that barely leans.

**Candidates:**
- *Complementary filter* — a few lines, one tunable constant, easy to reason
  about. Weakest handling of the problem above, but the weighting is right
  there to tune.
- *Kalman (TKJ Electronics)* — the existing sketch in the workspace. Better
  in principle; the workspace copy is AVR-only (writes `TWBR` directly) and
  tuned for a stationary-reference use case.
- *Madgwick/Mahony* — well-tested, cheap, designed for 6-DOF IMUs, one gain.

**Leaning toward:** start with a complementary filter because its single
constant makes the accelerometer-trust trade-off explicit and tunable against
real data, then compare against Madgwick on the *same logged session* — which
is possible precisely because raw `ax…gz` are logged alongside the fused
output. Decide with recorded data, not on the bench.

---

## ADR-0008: Session directories numbered, not timestamped

**Status:** Accepted (2026-09-22)

**Why:** The logger has no real-time clock and does not know the date at boot.
GPS supplies wall time eventually — possibly minutes in, possibly never in a
garage. Naming files by a time that is not yet known is not possible; naming
them by a wrong time is worse than numbering them. Real start time lands in
`meta.json` once known.

**Rejected:** Adding a DS3231 RTC module. It would solve this, and may be worth
it later, but it is another part, another I2C device, and another battery for a
problem that metadata solves.

---

## ADR-0009: CAN deferred, but designed for

**Status:** Accepted (2026-09-22)

Pins reserved, source interface shaped to accept it, no implementation.

**Why:** The ESP32's built-in CAN (TWAI) controller means the hardware cost is
one transceiver chip. But reading a specific motorcycle's bus means identifying
its message IDs and scaling factors, which is reverse engineering with its own
timeline and is not a prerequisite for lean angle or braking data.

---

# Open questions

Unresolved. Do not quietly answer one of these in code — raise it, or write the
code so it stays open, and say which in the ADR.

### Q1 — How many GY-521s, and where on the bike?

**Status:** Deliberately deferred by the repo owner, to be worked out against
their needs.

**What it affects:** I2C addressing (`AD0` gives two per bus; a third needs the
second I2C peripheral or a TCA9548A multiplexer), wiring and enclosure design,
record width in the log schema, and whether the sample rate must rise.

**How the design stays open:** the source interface is written for N sensors
rather than one, and the CSV schema is versioned so columns can be added.

**What would settle it:** deciding whether suspension behavior is in scope. One
chassis sensor answers lean, braking, and cornering completely. A second,
unsprung sensor is only worth its wiring if suspension and road input are
questions being asked — and it pushes the useful sample rate to 200 Hz+.

### Q2 — What ends a session?

Ignition-off, a timeout after motion stops, or a button? Affects how many
one-minute junk sessions accumulate from stop lights, and how aggressive
power-loss handling must be. Phase 2.

### Q3 — Is power-loss flush achievable?

The plan is to sense the falling supply rail and flush using the buck
converter's bulk capacitance. Whether there is enough energy for an SD flush is
an empirical question about a specific converter. Untested and unverified.

### Q4 — Which ESP32 board variant?

A plain WROOM devkit is assumed. An S3 has more RAM and native USB; a board
with a battery connector changes the power design. Cheap to settle later, but
it does affect the pin table.
