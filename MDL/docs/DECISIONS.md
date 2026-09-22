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

**Update (2026-09-22):** ADR-0010 changes the terms of this decision. With
centripetal correction applied, the accelerometer becomes a valid gravity
reference mid-corner, so the filter no longer has to be detuned to compensate
for a reference it cannot trust. Any of the three candidates becomes viable on
corrected input. Choose on ordinary merits — tunability and debuggability —
rather than on which one copes best with a broken reference.

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

**Update (2026-09-22):** ADR-0011 gives CAN a concrete first target — wheel
speed, to improve the speed estimate that lean angle depends on. That raises
its value above "engine data would be interesting", but does not make it a
prerequisite. Still sequenced after the GPS-only path works end to end.

---

## ADR-0010: GPS speed aids the orientation estimate (centripetal correction)

**Status:** Accepted (2026-09-22)

GPS is promoted from a logged channel to an **input of the orientation filter**.
Speed is used to remove centripetal acceleration from the accelerometer before
fusion, restoring it as a valid gravity reference.

**The problem it solves:** a frame-mounted accelerometer cannot observe
steady-state lean, because the bike leans until the resultant of gravity and
cornering force points through the contact patches. Without a fix for this,
lean angle rests entirely on integrating the gyro, and gyro integration drifts
without bound. The project's primary goal would degrade over the course of a
ride with nothing to arrest it.

**The mechanism:**

```
a_gravity = a_measured − (ω × v_body)
```

With velocity approximately forward in the body frame, `v_body ≈ (v, 0, 0)`, so
`ω × v = (0, r·v, −q·v)`. The correction therefore **requires only the speed
scalar — no attitude term.** There is no circular dependency, which is what
makes this cheap rather than delicate. Worked through for a steady coordinated
turn, the corrected components recover the true lean angle exactly.

The equivalent closed form is `tan(lean) = v·ψ̇/g`. The subtraction form is
preferred because it remains valid during transients, not only in steady state.

**Why GPS speed specifically:** receivers derive speed from carrier Doppler
shift, not by differencing positions, giving roughly 0.05 m/s accuracy. GPS
speed can be trusted well past the point where GPS *position* can be.

**Limits, and why they are tolerable:**

| Condition | Effect | Why it is survivable |
|---|---|---|
| < ~3 m/s | Correction negligible, GPS speed noisy | Centripetal force is near zero there, so the raw accelerometer is already correct |
| GPS dropout | No speed available | Degrade to IMU-only and flag the mode in the log |
| 50–200 ms latency | Lags fast transients | The gyro owns transients; GPS corrects only slow drift |

The failure regimes of the two sensors are complementary — the accelerometer is
reliable exactly where GPS is not, and vice versa. That is what makes this a
solution rather than a mitigation.

**Known residual error:** this produces the force-vector angle. Actual chassis
lean is several degrees greater, because the contact patch migrates toward the
inside of the tire as it rolls onto its shoulder — an effect that grows with
lean and depends on tire profile. Not corrected in firmware. Log the estimate
and its inputs; resolve the offset empirically in analysis.

**Cost:** GPS moves from a nice-to-have in Phase 4 to a dependency of good lean
data, and fusion gains a mode flag plus a degradation path. Accepted, because
the alternative is a primary goal that drifts.

**Better alternative, later:** wheel speed over CAN — higher rate, no dropouts,
no latency. The correction is agnostic about where `v` comes from, so this is a
drop-in upgrade and is now the strongest reason to pursue CAN (ADR-0009).

**Credit:** raised by the repo owner, asking whether GPS could address gyro
drift. It can, and more directly than by correcting the drift itself.

---

## ADR-0011: Speed fusion — GPS Doppler plus CAN wheel speed

**Status:** Proposed (2026-09-22) — designed now, built in Phase 9

Speed becomes its own fusion stage feeding orientation fusion (ADR-0010),
combining GPS Doppler speed with wheel speed read from the bike's CAN bus.

**Why not just pick one:** they are good at opposite things. GPS is unbiased
but slow, laggy, and occasionally absent. Wheel speed is fast and always
present but carries a scale error from rolling radius. Averaging them yields
the worst of both — GPS's lag *and* the wheel's bias.

**The approach — estimate the bias, then use the quiet sensor:**

```
v ≈ k · v_wheel          k = slowly-varying scale factor, learned from GPS
```

GPS continuously corrects `k`; the fused output then runs at wheel rate and
wheel latency with GPS-grade accuracy. `k` adapts on a time constant of tens of
seconds, because rolling radius changes with tire wear, pressure and
temperature — over hours, not milliseconds. **A fast-adapting `k` would absorb
genuine wheel slip as "scale change" and hide exactly the events worth seeing.**

### Gating: when it is safe to learn `k`

Adaptation runs only when all hold. Otherwise `k` is frozen and simply used.

| Condition | Threshold (provisional) | Reason |
|---|---|---|
| GPS fix quality | **`sAcc` below threshold** (UBX `NAV-PVT`) | The receiver's own speed-accuracy estimate — better than inferring from sats/HDOP. ADR-0013. |
| Speed | > 5 m/s | GPS speed is noisy near standstill |
| Lean | \|θ\| < 10° | Avoids the lean/rolling-radius coupling |
| Longitudinal accel | small | Drive slip and braking slip both corrupt wheel speed |
| ABS / traction control | inactive, if CAN reports it | Explicit slip signal, free if available |

### Latency compensation

A GPS fix is 50–200 ms old on arrival. It must be compared against the fused
estimate *as it was at the fix's timestamp*, not the current one — which means
keeping a short ring buffer of past estimates and correcting retrospectively.
Without this, every hard braking or acceleration event injects a correction
error proportional to how fast speed was changing.

### Degradation ladder

Speed estimation is expected to lose inputs, and says so rather than hiding it:

| Available | Behavior | `vsrc` |
|---|---|---|
| GPS + wheel | Full fusion, `k` adapting when gated conditions allow | `3` |
| Wheel only (tunnel) | `k · v_wheel` with the last learned `k`, frozen | `2` |
| GPS only (no CAN, or bus fault) | GPS speed carried forward, as today | `1` |
| Neither | Orientation fusion falls back to IMU-only | `0` |

### Slip and lift detection

Wheel speed lies in specific, detectable ways. These are logged as events, not
silently smoothed away — a rider *wants* to see wheelspin and stoppies:

- **Drive slip** — wheel acceleration far exceeds longitudinal accelerometer
  reading. Cross-check against the IMU, which cannot be fooled by a spinning
  wheel.
- **Lock-up / ABS** — wheel decelerates faster than physically plausible.
- **Wheelie** — front wheel speed collapses while the accelerometer and rear
  wheel disagree. A front-wheel-only feed is worthless here.
- **Front vs rear divergence** — the cheapest slip detector of all, available
  free if the bus exposes both.

### Cost and honesty

This is a meaningful chunk of work: CAN reverse engineering, a scale estimator,
a gating state machine, a latency buffer, and slip detection. **GPS alone is
sufficient for the project's primary goals.** The gains are no dropouts, lower
latency under braking (bearing on Q6), and higher rate. That is an upgrade, not
a prerequisite, and it is sequenced accordingly.

**Depends entirely on:** the bike exposing usable wheel speed on a readable
bus (Q7), which is unverified.

**Rejected:** using wheel speed *instead* of GPS (inherits an unbounded,
unobservable scale error), and a fixed calibration constant instead of an
online estimate (correct on the day it is measured, wrong as the tire wears —
and silently so).

---

## ADR-0012: ESP32-S3-DevKitC-1 (N16R8) as the board

**Status:** Accepted (2026-09-22) — settles Q4

**Hard requirements filtered the field fast.** Dual core (ADR-0002's
sampler/writer split), WiFi (ADR-0003 offload), and TWAI/CAN (ADR-0011) between
them eliminate the S2, C3 and C6 (single core) and the H2 and P4 (no WiFi).
That leaves the original ESP32 and the S3.

| | ESP32-WROOM-32 DevKitC | **ESP32-S3-DevKitC-1 N16R8** |
|---|---|---|
| Cost | ~$6.50 | ~$8–15 |
| Cores | 2 × LX6 @ 240MHz | 2 × LX7 @ 240MHz |
| PSRAM | none (~320KB usable SRAM) | **8 MB** |
| USB | CH340/CP2102 bridge chip | **native** |
| CAN / TWAI | yes | yes |
| Ecosystem | most examples, oldest | the 2026 default recommendation |

**Why the S3, for this project specifically:**

1. **PSRAM is the deciding factor.** The bottleneck here is not compute, it is
   surviving an SD card that disappears for 100 ms without dropping samples.
   Buffer depth is the entire defense, and it is the one thing the original
   ESP32 cannot scale — 8 MB buys buffering measured in seconds rather than
   milliseconds. This is ADR-0002's whole premise, made robust.
2. **Native USB removes the bridge chip** — one fewer component to shake loose
   on a vibrating frame, and it opens USB mass-storage as a second offload path
   beside WiFi.
3. Cost difference is a few dollars against a build that will cost far more in
   connectors and enclosure.

**Costs accepted:**

- **PlatformIO friction.** Espressif and PlatformIO fell out, and the official
  `platform-espressif32` stalled at Arduino core 2.x. Core 3.x lives in the
  community [`pioarduino` fork](https://github.com/pioarduino/platform-espressif32).
  The S3 does work on the official platform; the fork is where maintenance
  actually happens. Decide in Phase 1 and pin it in `platformio.ini`.
- **The pin table was rewritten, not adjusted.** S3 numbering does not map onto
  the original ESP32's — ADC1 is GPIO1–10, and native USB occupies GPIO19/20,
  which previously held SD MISO.
- **Octal PSRAM eats GPIO35–37.** They look free and are not.

**Rejected:** the original ESP32 (cheapest and best-documented, but RAM caps
buffer depth and the UART bridge is an extra failure point), and buying both to
decide later (defers a decision that the buffering argument already settles).

**Wrong if:** the SD stall test in Phase 2 shows modest buffers suffice and
PSRAM goes unused, at which point the original ESP32 becomes the cheaper equal.

---

## ADR-0013: GPS — drone-style u-blox M10 module, read over UBX

**Status:** Accepted (2026-09-22)

A potted drone-type GNSS module (Holybro M10 / Beitian class) with integrated
antenna, read as **UBX `NAV-PVT`** at 10 Hz, with PPS wired to an interrupt.

**Why this module class:** it is the only option already built for the
environment. Antenna, receiver and shielding are potted into one vibration-
tolerant unit, because quadcopters shake too. A bare breakout means sourcing an
antenna, a ground plane and weatherproofing separately, and hand-assembled RF
on a motorcycle is a poor bet. All u-blox M8/M9/M10 parts spec **~0.05 m/s
velocity accuracy**, which is the figure ADR-0010 already assumes — so the
expensive options buy position quality this project does not need.

**Rejected:** NEO-6M class (1–5 Hz, GPS-only, saturated with counterfeits),
SparkFun/Adafruit MAX-M10S breakout (genuine and well documented, but leaves
antenna and mounting as an exercise), NEO-M9N (25 Hz and superb multipath
rejection, for a 10 Hz requirement).

**Known risk:** chipset provenance on cheap modules is variable and counterfeit
u-blox parts are common. Verify on arrival by querying `UBX-MON-VER` and
confirming the reported chip matches what was sold.

### UBX instead of NMEA

NMEA reports speed but never says how good it is. UBX `NAV-PVT` carries
position, velocity, heading, fix status and **`sAcc` — a per-fix speed accuracy
estimate — in one binary message.** ADR-0011's gating was going to infer fix
quality from satellite count and HDOP; `sAcc` is the receiver stating its own
confidence directly, which is strictly better and simpler. ADR-0011's gating
table is updated accordingly.

### PPS

The 1 Hz timing pulse is accurate to ~30 ns. On an interrupt pin it establishes
when a fix was *valid*, as distinct from when its bytes arrived over UART —
the hard half of Q6. Costs one GPIO. Ignorable until Phase 4, wired now because
adding a wire to a potted enclosure later is worse.

**Consequence:** the GPS module likely lives outside the main enclosure, on a
cable. The IMU wants rigid frame mounting; the antenna wants sky. Those two
requirements conflict, and separating them is the usual resolution — this feeds
into Q1's mounting plan.

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

**Also blocks the axis and sign conventions** (2026-09-22). Mounting position
and orientation determine the conventions, and mounting cannot be planned until
the count and purpose are known. The two are settled together, and Phase 1
cannot complete without them — see [HARDWARE.md](HARDWARE.md#axis-and-sign-conventions).

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

### Q5 — How much does tire width offset the lean estimate?

ADR-0010 yields the force-vector angle; true chassis lean is greater by a few
degrees, growing with lean angle and dependent on tire profile. Whether to
correct it, and with what, is an empirical question needing real rides and a
reference measurement. Until then, logged lean is understood to be the force
angle and is labeled as such.

### Q6 — Does GPS speed latency need explicit compensation?

The fix is 50–200 ms old when used. At a steady speed that is harmless; under
hard braking, speed is changing fast enough that a stale value biases the
correction. It may be worth propagating speed forward using logged longitudinal
acceleration between fixes. Do not build this speculatively — measure the error
on a real session first. Phase 4.

### Q7 — Does the bike expose usable wheel speed on CAN?

**Gates all of ADR-0011.** Unknown and unverified. Needs answering in this
order: which model and year; whether it has a CAN bus at all (pre-CAN bikes
simply do not); where it can be tapped non-destructively (a diagnostic
connector is far preferable to splicing); whether wheel speed appears on it;
whether that value is raw or carries the speedometer's legally-mandated
optimistic bias. Many markets require a speedo to never under-read, so dash
speed typically runs high — whether the CAN value shares that bias must be
measured against GPS, not assumed.

Until answered, treat CAN wheel speed as a design target, not a plan.

### Q8 — Front wheel, rear wheel, or both?

Front is the better ground-speed reference — it is undriven, so it does not
spin up under power — but it locks under braking and reads zero during a
wheelie. Rear is corrupted by drive slip but stays on the ground more. Both
together make slip directly observable by divergence, which is the cheapest
detector available. Depends on what the bus actually exposes (Q7).

### Q9 — Compensate rolling radius for lean angle?

Wheel speed reads roughly 6% high at 45° of lean because the tire rolls on its
shoulder. Gating adaptation to near-upright conditions avoids *learning* the
wrong scale, but the fused speed is still biased while leaned. Compensating
`r_eff` with the live lean estimate would correct it, at the cost of coupling
two estimates that are currently independent. Needs real data to justify.
