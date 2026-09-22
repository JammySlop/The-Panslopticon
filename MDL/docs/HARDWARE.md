# Hardware

Parts, wiring, pin budget, and the electrical traps specific to putting
electronics on a running motorcycle.

> **Read the "Traps" section before wiring anything.** Two of them destroy an
> ESP32 and one of them destroys data.

## Bill of materials

| Part | Choice | Notes |
|---|---|---|
| MCU | **ESP32-S3-DevKitC-1 (N16R8)** | Dual core, WiFi, TWAI/CAN, 16MB flash, 8MB PSRAM, native USB. 3.3V logic, **not 5V tolerant**. See ADR-0012. |
| IMU | GY-521 breakout (MPU-6050) | Count and placement still open — see [DECISIONS.md](DECISIONS.md#open-questions). |
| Storage | 3.3V-native microSD breakout, SPI | Adafruit or SparkFun. See trap #2 and ADR-0014. |
| Card | **Samsung PRO Endurance 32GB**, FAT32 | High-endurance class is the real decision — see below. |
| GPS | **Drone-style u-blox M10 module** (Holybro M10 / Beitian class) | Antenna integrated and potted — built for vibration. 10 Hz, UBX capable. See ADR-0013. |
| Power | 12V→5V buck, 2A, >=40V input rating | Pololu D24V22F5 / Recom R-78E / TPS54360-based. ADR-0015. |
| Power protection | 2A fuse, P-MOSFET, TVS, 1000-2200uF | See the Power section - the fuse is not optional. |
| Enclosure | Sealed, vibration-isolated | Phase 6 concern, but decide mounting early (it affects axis conventions). |

Future, not yet specified: brake pressure transducer, clutch switch tap,
display.

**CAN transceiver** (SN65HVD230 or TJA1051T/3) has a concrete purpose now —
wheel speed for the speed estimate (ADR-0011), not just eventual engine data.
The ESP32's TWAI controller does the protocol work, so the transceiver is the
only part needed. Choose a **3.3V** part: the SN65HVD230 runs natively at 3.3V,
while many common CAN breakouts are 5V and reintroduce trap #1. Tap the bus at
a diagnostic connector where possible rather than splicing into harness wiring
— a motorcycle's CAN bus carries braking and engine management, and a bad
splice there is a safety issue, not just a data one.

## Pin budget

Assignments are provisional until Phase 1 puts them in
`firmware/include/config.h`. **That file and this table must agree** — if you
change one, change the other.

| Pin | Use | Notes |
|---|---|---|
| GPIO17 | I2C SDA | Shared by all IMUs and any future I2C device |
| GPIO18 | I2C SCL | |
| GPIO12 | SD SCK | SPI |
| GPIO11 | SD MOSI | |
| GPIO13 | SD MISO | |
| GPIO14 | SD CS | |
| GPIO16 | GPS RX (ESP32 ← GPS TX) | UART1 |
| GPIO15 | GPS TX (ESP32 → GPS RX) | UART1 |
| GPIO21 | GPS PPS | 1 pulse/second timing reference — interrupt input (ADR-0013) |
| GPIO48 | Status LED | Onboard addressable RGB on the DevKitC-1 |
| GPIO47 | Offload button | Input, pull-up, debounced. The onboard BOOT button on GPIO0 is a fallback. |
| GPIO41 | *reserved* CAN TX | TWAI is remappable; these are convention |
| GPIO42 | *reserved* CAN RX | |
| GPIO40 | *reserved* clutch switch | Digital in, pull-up |
| GPIO4 | *reserved* brake pressure | **ADC1** |
| GPIO5 | Supply voltage sense | ADC1, via divider - power-loss detection and low-voltage cutoff |
| GPIO6 | Ignition sense | Divided from a switched circuit. Input, the shutdown trigger. |

**Avoid on the ESP32-S3:**

| Pins | Why |
|---|---|
| GPIO26–32 | SPI flash. Using them stops the board booting. |
| **GPIO35, 36, 37** | **Consumed by octal PSRAM on the N16R8.** Free on paper, unusable in practice — the single most common S3 pin-budget mistake. |
| GPIO19, 20 | Native USB D− / D+. Available only if USB is given up. |
| GPIO43, 44 | UART0 — the serial console. |
| GPIO45, 46 | Strapping pins; GPIO46 is additionally input-only. |
| GPIO22–25 | Do not exist on the S3. |

**ADC1 vs ADC2:** ADC2 stops working while WiFi is active. On the S3, **ADC1 is
GPIO1–10** (not GPIO32–39 as on the original ESP32), so every analog input above
sits there. Silicon limitation, not a driver bug.

> **This table changed wholesale when the board was settled (ADR-0012).** Pin
> numbering does not carry over from the original ESP32 — anything written
> against the old table is wrong.

### Two GY-521s on one bus

The MPU-6050's I2C address is set by the `AD0` pin: low = `0x68`, high = `0x69`.
So two sensors share one bus with no extra hardware. A third needs either the
ESP32's second I2C peripheral or a TCA9548A multiplexer. Worth knowing while
the sensor-count question is still open.

## Power

Fed from the **battery directly**, switched by an ignition-sense line. Full
reasoning in ADR-0015.

> **Work on the battery safely.** Disconnect the negative terminal first and
> reconnect it last. Do not tap ABS, ECU or ignition-critical circuits for the
> sense line - use an accessory or lighting circuit.

### Chain, from the battery outward

| # | Stage | Part | Why |
|---|---|---|---|
| 1 | **Fuse** | 2 A inline, waterproof, **at the battery terminal** | A short anywhere downstream must blow this rather than melt the harness. Closest possible to the positive post. |
| 2 | Reverse polarity | P-MOSFET (or Schottky, ~0.4 V drop) | Install mistakes happen once |
| 3 | Transient clamp | TVS, SMCJ24A class | Load dump, inductive kickback from horn, solenoids, starter |
| 4 | Bulk capacitance | 1000-2200 uF, **on the 12 V side** | Cranking ride-through and power-loss holdup - see below |
| 5 | Load switch | High-side MOSFET, gated by ignition sense | True zero drain when parked |
| 6 | Converter | 5 V 2 A buck, >=40 V input rating | Feeds the DevKitC-1's 5V pin |

### Why the logger cannot simply sit on the battery

Running draw is roughly 200 mA at 5 V, about **100 mA at 12 V**. Against a
typical 8-12 Ah motorcycle battery:

- **~2 days parked** - below ~50% charge, likely will not crank
- ~4 days - flat

Hence the ignition-sense line. It is a thin wire from any switched accessory
circuit, divided down to 3.3 V, doing two jobs: gating the high-side load
switch, and giving firmware advance warning to close the session cleanly before
the rails collapse.

### Holdup capacitance goes on the 12V side

Stored energy is E = 1/2 C V^2, so the same joules cost far less capacitance at
a higher voltage. To hold ~500 mW for ~100 ms (about 50 mJ):

| Placement | Usable swing | Capacitance needed |
|---|---|---|
| **12 V input** | 12 V -> 9 V | **~1600 uF** |
| 5 V output | 5 V -> 3.5 V | ~7800 uF |

Nearly 5x less capacitor for the same flush window. This is what makes Q3
practical with an ordinary electrolytic instead of a supercapacitor.

### Cranking

Starting drags the battery to **6-8 V** for a few hundred milliseconds. The
converter must either tolerate that input or ride through on the bulk cap. A
brownout mid-write is exactly the case the power-loss flush exists for, so
this is a good thing to test deliberately rather than discover.

### Budget

| Load | Draw |
|---|---|
| ESP32-S3, logging, WiFi off | ~100 mA @ 3.3 V |
| GPS module | ~40 mA |
| SD card | ~30 mA average, **100-200 mA bursts** |
| ESP32-S3 WiFi TX (offload only) | up to ~500 mA peak |

A 2 A converter leaves comfortable margin, including WiFi peaks during offload.
Feed 5 V into the DevKitC-1's 5V pin and let its onboard regulator make 3.3 V.
**Do not back-power over USB and the 5V pin simultaneously** while debugging.

## Storage

### The card matters far more than the breakout

Continuous-write endurance at 32GB differs by roughly 7x across cards that
look identical on a shelf:

| Card | Rated continuous recording |
|---|---|
| **Samsung PRO Endurance 32GB** | **17,520 h** |
| SanDisk Max Endurance 32GB | 15,000 h |
| SanDisk High Endurance 32GB | 2,500 h |
| Generic consumer card | unrated, and stalls unpredictably |

A logger writing continuously in a vehicle is exactly the dashcam workload
these cards exist for. Consumer cards are built for bursty camera use and do
their garbage collection whenever they feel like it - which is the 100 ms stall
the entire buffering architecture exists to absorb. A high-endurance card does
not eliminate stalls, it makes them shorter and rarer.

### Why SPI, and the escape hatch

SPI needs four pins and is trivially reliable. The requirement is ~14 KB/s;
SPI comfortably delivers a hundred times that, so throughput is not the
constraint - stall latency is, and that is a property of the card, not the bus.

The ESP32-S3 also supports **SD_MMC in 4-bit mode**, which is far faster and
pin-flexible on the S3 (unlike the original ESP32's fixed pins). Held in
reserve: if the log format ever moves to binary at high rate, or a stall
profile turns out to need deeper pipelining, it is available for two more
pins. Not needed now.

### Wiring notes

- **Format FAT32.** The ESP32 SD library's exFAT support is poor, and cards
  above 32GB ship exFAT by default. 32GB avoids the whole question.
- **Decoupling capacitance at the socket.** Cards draw 100-200 mA bursts while
  writing. Without local bulk capacitance this browns out the 3.3V rail and
  produces "random" resets that look like firmware bugs. 10 uF plus 100 nF.
- **Keep SPI runs short.** Long dupont leads cause mount failures that look
  exactly like a bad card. Start at 4 MHz, raise once stable.
- **Vibration:** a push-push socket can lose contact on a bike. Bench work on a
  breakout is fine; the final build wants a soldered socket, strain-relieved
  (trap #4).

## GPS configuration

The module must be configured once and the settings **saved to battery-backed
RAM**, or every power cycle resets it. On a logger that power-cycles at every
stop, that is not a minor annoyance.

| Setting | Default | Use | Why |
|---|---|---|---|
| Protocol | NMEA | **UBX `NAV-PVT`** | One binary message carries speed, heading, fix status *and* `sAcc`. See below. |
| Baud | 9600 | **115200** | 9600 cannot carry 10 Hz. Silently drops messages if left alone. |
| Update rate | 1 Hz | **10 Hz** | Matches the design; 1 Hz makes the speed input uselessly stale |
| Backup power | — | **battery/supercap fitted** | Retains ephemeris: time-to-first-fix drops from ~30 s to ~1 s |

### Why UBX rather than NMEA

NMEA gives speed (`VTG`, `RMC`) but no statement of how good it is. UBX
`NAV-PVT` includes **`sAcc`, a per-fix speed accuracy estimate**, which is
exactly what ADR-0011's gating needs — the receiver reporting its own
confidence beats inferring it from satellite count and HDOP. It is also more
compact, and one message replaces parsing several sentences.

### PPS

The module's 1-pulse-per-second output is accurate to roughly 30 ns. Wired to
an interrupt pin, it pins down *when* a fix was actually valid rather than when
its bytes finished arriving over UART — which is the hard half of the latency
problem in Q6. One GPIO, and it can be ignored until Phase 4.

### Antenna placement

The antenna needs sky. Under a plastic fairing or tailpiece is fine; under
metal or carbon is not. This constrains where the enclosure goes as much as
the IMU mounting does, and the two requirements can conflict — the IMU wants
rigid frame mounting, the GPS wants an unobstructed view upward. Separating the
GPS module from the main enclosure on a cable is the usual resolution.

## Sensor configuration

Defaults on the MPU-6050 are tuned for gentle indoor demos and are wrong for a
motorcycle:

| Setting | Default | Use | Why |
|---|---|---|---|
| Accel range | ±2 g | **±8 g** | Road bumps and braking clip ±2g constantly. Clipped data is unrecoverable. |
| Gyro range | ±250 °/s | **±500 °/s** | A quick flick side-to-side exceeds 250 °/s. |
| DLPF | off | **~44 Hz** | Engine and road vibration alias into the signal without low-pass filtering. |
| Sample rate | 8 kHz | **100 Hz** | Matches the logging rate; see [ARCHITECTURE.md](ARCHITECTURE.md). |

Wider ranges cost resolution, so these are a trade, not a free upgrade. ±8g
over a 16-bit signed range is about 0.24 mg per count — still far finer than
anything the bike does.

## Axis and sign conventions

**Not yet defined — deferred as part of Q1** (how many IMUs, where, and for
what). Mounting position and orientation decide the conventions, and mounting
cannot be planned until the sensor count and purpose are settled. Answering Q1
resolves this at the same time.

Everything downstream depends on it being written down once and obeyed: the
orientation filter, the sign of the `ω × v` centripetal correction, the log
column meanings, the desktop plots, and anyone reading a chart six months from
now. **Phase 1 cannot be finished without it** — verifying that tilting the
board moves the numbers correctly presumes a definition of "correctly".

When settled, define here: which vehicle direction each of +X, +Y, +Z points;
whether a left lean is positive or negative (vehicle-dynamics and aviation
conventions disagree — pick one and name it); whether positive pitch is nose-up
or nose-down; and the intended physical mounting orientation that makes the
mapping real.

## Traps

**1. Power the GY-521 from 3.3V, never 5V.**
The breakout has a regulator, so 5V on `VCC` appears safe — but its I2C
pull-up resistors connect to `VCC`, *after* the regulator input. Feed it 5V and
`SDA`/`SCL` get pulled to 5V, straight into ESP32 pins rated for 3.3V. This
kills boards slowly and confusingly (they work, then get flaky, then die).
Use the ESP32's 3V3 rail.

**2. Not every microSD module works at 3.3V.**
Many popular breakouts include a 5V→3.3V regulator and level shifters designed
around a 5V supply; run them from 3.3V and the shifters sit in an undefined
region. Symptoms are intermittent mount failures that look like a bad card. Use
a module explicitly sold as 3.3V-native, or one with a jumper.

**3. A motorcycle's 12V rail is not 12V.**
Charging systems run 13–14.5V, and switching inductive loads produces transient
spikes far above that. A bare buck converter fed straight from the bike will
eventually fail. The input needs, at minimum, a reverse-polarity diode, a TVS
diode, and bulk capacitance. Take power from an ignition-switched circuit, not
permanent 12V, so the logger cannot flatten the battery.

**4. Vibration destroys anything that is not soldered.**
Breadboards and dupont jumpers do not survive an engine. They work in the
garage and fail at 5000rpm, which produces the worst kind of bug: data that is
fine until it is interesting. Perfboard or a PCB, soldered, strain-relieved,
before any real ride.

**5. Mount the IMU to the frame, rigidly, and record how.**
A sensor on a rubber-mounted panel measures the panel. A sensor that shifts
between sessions makes every session incomparable. Rigid, repeatable, and
documented — the calibration routine assumes the sensor has not moved since it
was calibrated.

## Mounting and calibration

Calibration measures the gyro's zero-rate offset and the accelerometer's bias
with the bike stationary and upright, and stores the result so it survives a
reboot. It must be re-run whenever the sensor is physically moved. (The stock
Kalman sketch in the workspace has `// TODO: Make calibration routine` at the
top and never got one — this project needs it, Phase 3.)

Because a bike does not stand upright on its own, "upright" during calibration
means held level by the rider or on a paddock stand — and that tilt error
becomes a permanent offset in every lean angle number. Consider recording the
calibration attitude as metadata so it can be corrected in post rather than
baked in.
