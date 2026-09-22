# Hardware

Parts, wiring, pin budget, and the electrical traps specific to putting
electronics on a running motorcycle.

> **Read the "Traps" section before wiring anything.** Two of them destroy an
> ESP32 and one of them destroys data.

## Bill of materials

| Part | Choice | Notes |
|---|---|---|
| MCU | ESP32-WROOM-32 devkit (30 or 38 pin) | Dual core, WiFi, built-in CAN controller. 3.3V logic, **not 5V tolerant**. |
| IMU | GY-521 breakout (MPU-6050) | Count and placement still open — see [DECISIONS.md](DECISIONS.md#open-questions). |
| Storage | microSD breakout, 3.3V native, SPI | See trap #2. Card: 8–32GB, FAT32, name-brand. |
| GPS | u-blox NEO-M8N (or NEO-6M) | M8N does 10Hz; the 6M is 1–5Hz and cheaper. Phase 4. |
| Power | 12V→5V buck converter, automotive rated | Plus protection — see trap #3. |
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
| GPIO21 | I2C SDA | Default ESP32 I2C pins |
| GPIO22 | I2C SCL | Shared by all IMUs and any future I2C device |
| GPIO18 | SD SCK | VSPI |
| GPIO19 | SD MISO | VSPI |
| GPIO23 | SD MOSI | VSPI |
| GPIO5 | SD CS | Strapping pin, must be high at boot — SPI CS idles high, so OK |
| GPIO16 | GPS RX (ESP32 ← GPS TX) | UART2 |
| GPIO17 | GPS TX (ESP32 → GPS RX) | UART2 |
| GPIO2 | Status LED | Onboard LED on most devkits |
| GPIO4 | Offload button | Input, pull-up, debounced |
| GPIO25 | *reserved* CAN TX | TWAI is remappable; these are convention |
| GPIO26 | *reserved* CAN RX | |
| GPIO27 | *reserved* clutch switch | Digital in, pull-up |
| GPIO34 | *reserved* brake pressure | **ADC1**, input-only, no internal pull-up |
| GPIO35 | *reserved* supply voltage sense | ADC1, via divider — for power-loss detection |

**Avoid:** GPIO6–11 (connected to the internal flash — using them bricks the
boot), GPIO12 (strapping pin, must be LOW at boot; pulling it high stops the
board booting).

**ADC1 vs ADC2:** ADC2 pins stop working while WiFi is active. Every analog
input above is on ADC1 (GPIO32–39) for that reason. This is not optional — it
is a silicon limitation, not a driver bug.

### Two GY-521s on one bus

The MPU-6050's I2C address is set by the `AD0` pin: low = `0x68`, high = `0x69`.
So two sensors share one bus with no extra hardware. A third needs either the
ESP32's second I2C peripheral or a TCA9548A multiplexer. Worth knowing while
the sensor-count question is still open.

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
