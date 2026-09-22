# Architecture

How the firmware is put together, and the reasoning behind the shape of it.
Decisions referenced as ADR-XXXX are recorded in [DECISIONS.md](DECISIONS.md).

## The core idea: sources → bus → sinks

The naive logger looks like this:

```cpp
void loop() {
  readIMU();
  writeToSD();   // <-- stalls for 20-100ms whenever the card erases a block
}
```

That works on a bench and falls apart on a bike. SD cards periodically block
for tens of milliseconds doing internal housekeeping, and that delay lands
directly in the sample timing. The data ends up with holes and uneven
timestamps exactly when the bike is doing something interesting.

So MDL splits into three layers with a queue in the middle:

```
   SOURCES                    SAMPLE BUS                   SINKS
 (produce data)           (decouples timing)          (consume data)

 ┌──────────────┐
 │ IMU (I2C)    │──┐
 ├──────────────┤  │      ┌──────────────────┐      ┌─────────────────┐
 │ GPS (UART)   │──┼─────▶│  ring buffer /   │─────▶│ SD writer       │
 ├──────────────┤  │      │  FreeRTOS queue  │   ├─▶│ (buffered CSV)  │
 │ Digital in   │──┤      │                  │   │  ├─────────────────┤
 │ (clutch etc) │  │      │  fixed-size      │   ├─▶│ Serial monitor  │
 ├──────────────┤  │      │  Sample records  │   │  ├─────────────────┤
 │ Analog in    │──┤      └──────────────────┘   └─▶│ Display (future)│
 │ (brake pres) │  │                                └─────────────────┘
 ├──────────────┤  │
 │ CAN / TWAI   │──┘
 │ (future)     │
 └──────────────┘
```

A **source** knows how to produce readings and nothing about storage. A **sink**
knows how to consume records and nothing about sensors. Neither knows the other
exists. Adding the display, the clutch switch, the brake pressure sensor, or
CAN means writing one new source or sink — not touching the logging path
(ADR-0002).

**One deliberate exception:** orientation fusion sits between the sources and
the bus, and it consumes *two* sources — the IMU and GPS speed — because lean
angle on a motorcycle is not computable from the IMU alone. See "Centripetal
correction" below. This is the only place where two sources are coupled, and it
is coupled through a single scalar (speed), not a general dependency.

```
 IMU ──┬──────────────────────────────▶ (raw channels straight to the bus)
       │
       └──▶ ┌─────────────────┐
            │ orientation     │──▶ roll, pitch ──▶ bus
 GPS ──speed▶│ fusion          │
       │    └─────────────────┘
       └──────────────────────────────▶ (position channels to the bus)
```

Raw accelerometer and gyro values are always logged alongside the fused output,
so a different filter can be evaluated later against an already-recorded ride
rather than needing a new one.

## Tasks and cores

The ESP32 has two cores. FreeRTOS is already running underneath Arduino, so we
use it deliberately rather than pretending `loop()` is all there is.

| Task | Core | Priority | Job |
|---|---|---|---|
| `sampler` | 0 | high | Timer-driven. Reads sources on a fixed tick, stamps, pushes to the bus. Must never block. |
| `writer` | 1 | normal | Drains the bus, batches into large writes, hands them to the SD card. Allowed to block. |
| `service` | 1 | low | WiFi AP, web server, button handling, status LED. Only fully active when parked. |

Splitting across cores is what makes the guarantee real: a stalled SD write on
core 1 cannot delay the sampler on core 0.

**Timestamps are applied in the sampler, at the moment of read** — never in the
writer. A record's time must reflect when the world was measured, not when the
card got around to accepting it. This is the whole reason for the split; if you
find yourself timestamping downstream, stop.

## Session lifecycle

```
  BOOT ──▶ IDLE ──motion detected──▶ LOGGING
             ▲                          │
             │                          │ stop detected / ignition off
             │◀─────────────────────────┘
             │
             └──button or no-motion boot──▶ OFFLOAD (WiFi AP + web server)
```

- **BOOT** — mount SD, probe sensors, load calibration. Any failure is
  announced on the status LED and serial, and the logger degrades rather than
  halting where it reasonably can (no GPS fix is not a reason to refuse to log
  accelerometer data).
- **IDLE** — sampling runs, nothing is written. Motion detection watches the
  IMU for sustained movement above a threshold.
- **LOGGING** — a session file is open and records are flowing.
- **OFFLOAD** — WiFi access point up, sessions downloadable over HTTP. Logging
  is disabled in this state; the two never run at once (ADR-0005).

Power can be cut at any instant in any state. Everything in the writer path is
designed around that — see "Power loss" below.

## Module layout (planned)

```
firmware/
├── platformio.ini
├── include/
│   └── config.h            all pins, rates, thresholds — single source of truth
└── src/
    ├── main.cpp            boot, task creation, state machine
    ├── bus/
    │   ├── Sample.h        the record struct + schema version
    │   └── SampleBus.cpp   ring buffer / queue
    ├── sources/
    │   ├── Source.h        interface: begin(), read(Sample&), name()
    │   ├── ImuSource.cpp   GY-521 over I2C
    │   └── GpsSource.cpp   NMEA over UART
    ├── sinks/
    │   ├── Sink.h          interface: begin(), write(const Sample&), flush()
    │   ├── SdSink.cpp      buffered session writer
    │   └── SerialSink.cpp  live debug output
    ├── fusion/
    │   └── Orientation.cpp accel+gyro+GPS speed → roll/pitch (ADR-0010)
    └── service/
        ├── WifiOffload.cpp AP + web server
        └── Status.cpp      LED patterns, error reporting
```

Nothing here is built yet. This is the target the phases in
[ROADMAP.md](ROADMAP.md) build toward.

## Sampling rate

Target **100 Hz** for the IMU, which is comfortably enough for lean angle and
braking (a fast flick of the bars is a few hundred milliseconds; at 100 Hz that
is dozens of samples). GPS runs far slower — 10 Hz at best — so GPS fields are
carried forward between fixes and flagged as stale rather than interpolated.

If suspension or vibration analysis is added later, that wants 200 Hz or more
and may need a rethink of the record size. Recorded as an open question.

## Orientation: what the sensors can and cannot tell us

The MPU-6050 has an accelerometer and a gyroscope, and **no magnetometer**.
That has a hard consequence worth stating plainly:

- **Roll and pitch are observable.** Gravity gives an absolute reference the
  filter can correct drift against. Lean angle is achievable.
- **Yaw (heading) is not.** With gyro only, it drifts without bound and nothing
  on the board can correct it. Heading must come from GPS course-over-ground,
  which is only valid while actually moving.

There is a second complication specific to motorcycles: in a steady corner, a
bike leans until the *combined* gravity and cornering force points straight
through the tires. An accelerometer bolted to the frame therefore reads close
to "upright" mid-corner, no matter how far over the bike is. The accelerometer
cannot see steady-state lean at all — so a filter tuned like a
self-balancing-robot example will report a bike that barely leans. This is the
single most important thing to get right for the primary goal, and it is why
the stock Kalman example cannot simply be ported and trusted.

**GPS solves this, and is the reason GPS is not merely a logged channel.**
See ADR-0010; the short version follows.

### Centripetal correction

The accelerometer is not broken mid-corner — it is measuring exactly what it
should. It reports *specific force*, which mid-corner is gravity plus
centripetal acceleration. If the centripetal part can be computed and removed,
what remains is gravity, and gravity is a valid vertical reference again:

```
a_gravity = a_measured − (ω × v_body)
```

`ω` is the gyro, already sampled at 100 Hz. `v_body` is velocity in the body
frame — approximately `(v, 0, 0)`, forward. So the cross product reduces to:

```
ω × v = (0, r·v, −q·v)          where ω = (p, q, r)
```

which **needs only the speed scalar `v` — no attitude at all.** That matters:
there is no circular dependency where the lean estimate is required to compute
its own correction. Feed in speed, get back a usable gravity vector, run an
ordinary complementary or Kalman filter on it.

Speed comes from GPS, which derives it from carrier Doppler shift rather than
by differencing positions — accurate to roughly 0.05 m/s, and far better than
the receiver's position quality would suggest.

Equivalently, in a steady coordinated turn, `tan(lean) = v·ψ̇/g`. Both
formulations agree; the correction above is preferred because it also behaves
during transients, not just in steady state.

### Where this breaks, and what covers it

| Condition | Effect | Fallback |
|---|---|---|
| Speed below ~3 m/s | Correction is negligible and GPS speed is noisy | Raw accelerometer is trustworthy here — centripetal force is near zero |
| GPS dropout (tunnel, tree cover) | No speed | IMU-only; **flag it in the log**, never degrade silently |
| GPS latency (50–200 ms) | Correction lags fast transients | The gyro owns transients regardless; GPS only corrects slow drift |

The two sensors fail in opposite regimes, which is what makes the pairing work
rather than merely help.

**Tire width caveat:** this yields the *force-vector* angle. A real chassis
leans several degrees further, because the contact patch migrates toward the
inside of the tire as it rolls onto the shoulder. Log both the estimate and its
inputs and resolve the offset empirically — it depends on the specific tire.

**Consequence for the architecture:** GPS is an *input to fusion*, not only a
sink-bound channel. Speed must reach the orientation filter inside the sampler
path. Fusion degrades gracefully when it is absent and records which mode it
was in. A wheel-speed source over CAN would be strictly better — higher rate,
no dropouts, no latency — and is the strongest argument for adding CAN later
(ADR-0009).

## Power loss

The ignition can be switched off mid-write. The design assumes it:

1. The writer flushes to the card on a fixed interval and at every session
   boundary, so the worst case loses a bounded, known amount of data.
2. Files are appended to and never rewritten, so a truncated file loses only
   its tail — earlier data stays readable.
3. The record format is line-oriented with self-contained rows, so a partial
   final row is discarded by the decoder without corrupting what came before
   ([DATA-FORMAT.md](DATA-FORMAT.md)).
4. Planned: detect falling supply voltage and use the buck converter's
   capacitance to flush and close before the rails collapse. Untested.

## What is deliberately *not* here

- **No real-time analysis on the bike.** Sampling and storing is the job;
  analysis happens on a desktop where it is easy to iterate. A display, when
  added, shows live values and session state — not computed insights.
- **No cloud, no accounts, no app.** Files on a card, downloadable over a local
  WiFi AP. It works in a garage with no signal.
- **No engine data yet.** CAN is planned as a source, but the bike's bus
  layout is unknown territory and is its own project (ADR-0009).
