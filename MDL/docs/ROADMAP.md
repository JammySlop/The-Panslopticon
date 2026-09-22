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
- [ ] Settle axis and sign conventions
- [ ] Open the first PR

**Done when:** a stranger can read `MDL/` and understand the project, the
decisions, and what is unresolved.

---

## Phase 1 — Read one sensor

- [ ] `platformio.ini` targeting `esp32dev`, Arduino framework, pinned libraries
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

## Phase 3 — Make the angles mean something

- [ ] Calibration routine: gyro zero-rate offset, accel bias, stored in `calib.json`
- [ ] Orientation filter behind an interface so implementations are swappable
- [ ] Settle ADR-0007 by comparing filters on the *same recorded session*
- [ ] Validate against a known reference (protractor, phone, anything measurable)

**Done when:** a logged lean angle matches a physically measured one within a
few degrees, *and* the steady-state-cornering behavior from
[ARCHITECTURE.md](ARCHITECTURE.md) has been checked on a real ride rather than
assumed.

---

## Phase 4 — Position and time

- [ ] `GpsSource`: NMEA parsing on UART2
- [ ] Carry-forward with `gage` staleness per [DATA-FORMAT.md](DATA-FORMAT.md)
- [ ] Wall-clock time into `meta.json` at first fix
- [ ] Sanity-check position against a known route

**Done when:** a session carries a track that looks like the road actually
ridden.

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
- [ ] First real ride, then re-check everything Phase 3 validated on the bench

**Done when:** it survives a full ride and the data is trustworthy afterward.

---

## Phase 7 — Analysis tooling

Can run in parallel from Phase 2 onward, as soon as real files exist.

- [ ] Python decoder honoring the schema-version and partial-row rules
- [ ] Plots: lean angle over time, g-g diagram, speed and lean over a track map
- [ ] Session summary: max lean each way, hardest braking, distance, duration

**Done when:** a card becomes a set of charts in one command.

---

## Phase 8 — Extensibility, as wanted

Independent of each other; pick by interest. The architecture exists so these
are additive.

- [ ] Clutch switch — digital source, debounced
- [ ] Brake pressure — analog source on ADC1, needs a transducer and calibration
- [ ] Onboard display — a sink showing live values and session state
- [ ] CAN / TWAI — transceiver plus reverse-engineering the bike's message IDs (ADR-0009)
- [ ] Second IMU, if Q1 resolves that way
