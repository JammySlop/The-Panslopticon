# Roadmap

Phases, in order. Each phase ends with something demonstrable — not a layer
that only pays off later. Tick the boxes as work lands, and advance the phase in
[STATUS.md](STATUS.md).

Phases are sequential because each depends on the last, with one exception
noted at Phase 7.

---

## Phase 0 — Planning and documentation ◀ current

- [x] Decide storage strategy, toolchain, and primary goals
- [x] Write the document system (`README`, `CLAUDE.md`, `docs/`)
- [x] Record initial architecture and decisions
- [x] Open the first PR

**Done when:** a stranger can read `MDL/` and understand the project, the
decisions, and what is unresolved.

Axis and sign conventions are *deliberately* not part of this phase — they are
deferred with Q1, since mounting decides them. They block the end of Phase 1,
not the start of it.

---

## Phase 1 — Read one sensor

- [ ] Decide official `platform-espressif32` vs the `pioarduino` fork, and pin it
- [ ] `platformio.ini` targeting `esp32-s3-devkitc-1`, Arduino framework, PSRAM enabled
- [ ] `config.h` with every pin and rate from [HARDWARE.md](HARDWARE.md)
- [ ] I2C bring-up: scan the bus, confirm `WHO_AM_I` returns `0x68`
- [ ] Configure ranges: ±8g, ±500°/s, 44 Hz DLPF, 100 Hz sample rate
- [ ] `ImuSource` producing timestamped raw samples
- [ ] `SerialSink` printing them
- [ ] Verify the tick is actually 100 Hz and jitter is bounded — measure, don't assume

**Done when:** raw accel and gyro stream over USB at a verified 100 Hz, and
tilting the board by hand moves the numbers the way the axis conventions say
they should.

---

## Phase 2 — Log to card

- [ ] `SampleBus` ring buffer between sampler and writer
- [ ] Sampler and writer as separate FreeRTOS tasks pinned to different cores
- [ ] `SdSink`: session directories, `data.csv`, `meta.json`
- [ ] Buffered writes with periodic flush
- [ ] Motion-triggered session start; session end per Q2
- [ ] Status LED: booting / idle / logging / error
- [ ] **Stall test** — confirm a slow SD write does not disturb sample timing

**Done when:** a bench session produces a valid CSV, pulling power mid-write
loses only a bounded tail, and the timing test proves the decoupling works.

---

## Phase 3 — Orientation, IMU-only baseline

> **Order note (ADR-0010):** GPS speed is an *input* to good lean angle, not a
> later luxury. This phase deliberately builds the IMU-only version first — it
> is the fallback path that has to exist anyway for dropouts and low speed, and
> having it makes the improvement from GPS aiding measurable rather than
> assumed. Lean angle is **not** finished at the end of this phase.

- [ ] Calibration routine: gyro zero-rate offset, accel bias, stored in `calib.json`
- [ ] Orientation filter behind an interface so implementations are swappable
- [ ] Fusion emits `fmode` from the start, even with only mode `0` implemented
- [ ] Validate static tilt against a known reference (protractor, phone, anything measurable)

**Done when:** static and slow-speed angles are correct, and the drift rate over
several minutes is *measured and written down* — that number is the baseline
Phase 4 has to beat.

---

## Phase 4 — Position, time, and GPS-aided lean

- [ ] Verify the module is genuine u-blox via `UBX-MON-VER` on arrival
- [ ] Configure and **persist**: UBX protocol, 115200 baud, 10 Hz, PPS enabled
- [ ] `GpsSource`: UBX `NAV-PVT` parsing on UART1
- [ ] Carry-forward with `gage` staleness per [DATA-FORMAT.md](DATA-FORMAT.md)
- [ ] Wall-clock time into `meta.json` at first fix
- [ ] Sanity-check position against a known route
- [ ] **Centripetal correction:** feed speed into fusion, `a − (ω × v)` (ADR-0010)
- [ ] Mode switching with hysteresis: GPS-aided ↔ low-speed ↔ IMU-only, logged in `fmode`
- [ ] Settle ADR-0007 by comparing filters on the *same recorded session*
- [ ] PPS interrupt to timestamp when each fix was *valid* (bears on Q6)
- [ ] Measure whether GPS latency biases the estimate under hard braking (Q6)

**Done when:** a session carries a track matching the road ridden, and lean
angle no longer drifts over a ride — compared against the Phase 3 baseline on
recorded data, not by eye.

---

## Phase 5 — Get the data off

- [ ] Offload mode: button or no-motion-at-boot raises a WiFi AP
- [ ] Web page listing sessions with sizes and dates
- [ ] Download and delete, with deletion confirmed
- [ ] Verify a downloaded file is byte-identical to the one on the card

**Done when:** a full session moves to a phone without touching the enclosure.

---

## Phase 6 — Put it on the bike

- [ ] Protected 12V supply (reverse polarity, TVS, bulk capacitance)
- [ ] Ignition-switched power
- [ ] Soldered build — no breadboard, no dupont connectors
- [ ] Sealed, vibration-isolated enclosure
- [ ] Rigid documented IMU mount
- [ ] Power-loss flush (settles Q3)
- [ ] First real ride, then re-check everything validated on the bench
- [ ] Confirm steady-state cornering lean on a real ride — the one claim that
      cannot be tested stationary ([ARCHITECTURE.md](ARCHITECTURE.md))
- [ ] Measure the tire-width offset against a reference, settling Q5

**Done when:** it survives a full ride and the data is trustworthy afterward.

---

## Phase 7 — Analysis tooling

Can run in parallel from Phase 2 onward, as soon as real files exist.

- [ ] Python decoder honoring the schema-version and partial-row rules
- [ ] Plots: lean angle over time, g-g diagram, speed and lean over a track map
- [ ] Offline re-fusion: recompute lean from logged raw channels, so filter
      changes can be evaluated against past rides without re-riding them
- [ ] Session summary: max lean each way, hardest braking, distance, duration

**Done when:** a card becomes a set of charts in one command.

---

## Phase 8 — Extensibility, as wanted

Independent of each other; pick by interest. The architecture exists so these
are additive.

- [ ] Clutch switch — digital source, debounced
- [ ] Brake pressure — analog source on ADC1, needs a transducer and calibration
- [ ] Onboard display — a sink showing live values and session state
- [ ] Second IMU, if Q1 resolves that way

CAN has outgrown this list and has its own phase below.

---

## Phase 9 — CAN wheel speed and speed fusion

Designed in ADR-0011, built here. **Entirely gated on Q7** — whether the bike
exposes usable wheel speed on a readable bus. Answer that before any of the
rest, because a negative answer ends the phase.

*Reconnaissance*
- [ ] Identify the bike, confirm it has CAN, find a non-destructive tap point
- [ ] SN65HVD230 (or TJA1051T/3) transceiver on the reserved TWAI pins
- [ ] Passive sniff: log raw frames to the card, ride, then analyze offline
- [ ] Identify the wheel-speed message ID, byte layout, scaling and endianness
- [ ] Determine whether the value carries the speedo's optimistic bias —
      measure against GPS, do not assume
- [ ] Settle Q8: is front, rear, or both available?

*Fusion*
- [ ] `CanSource` decoding wheel speed into the sample bus
- [ ] Online scale-factor estimator for `k`, logged as `kwhl`
- [ ] Gating state machine — fix quality, speed, lean, accel, ABS/TC
- [ ] Latency compensation via a ring buffer of past estimates
- [ ] Degradation ladder with `vsrc` logged per row
- [ ] Slip, lock-up and wheelie detection by cross-checking against the IMU

*Validation*
- [ ] Confirm `k` converges and then stays put over a ride
- [ ] Confirm tunnel or tree-cover transitions are seamless in `vfus`
- [ ] Compare lean angle from GPS-only against fused speed on the same session
- [ ] Decide Q9: is lean-angle rolling-radius compensation worth the coupling?

**Done when:** speed is continuous and accurate through GPS dropouts, `k` is
stable, and the lean estimate measurably improves under braking — verified
against a recorded session, not impressions.
