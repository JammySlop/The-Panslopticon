# Glossary

Terms, part numbers, and acronyms used in these documents, for anyone who does
not already have them loaded.

## Parts

**GY-521** — A small breakout board carrying an MPU-6050. The names get used
interchangeably; GY-521 is the board, MPU-6050 is the chip on it.

**MPU-6050** — InvenSense 6-axis IMU: 3-axis accelerometer plus 3-axis
gyroscope, over I2C. Old, cheap, everywhere, well documented. **No
magnetometer**, which is why heading has to come from GPS.

**ESP32** — Espressif dual-core microcontroller with WiFi, Bluetooth, and a
CAN controller built in. 3.3V logic and **not** 5V tolerant.

**NEO-M8N / NEO-6M** — u-blox GPS receiver modules. The M8N manages 10 Hz
updates; the 6M is 1–5 Hz and cheaper.

**TCA9548A** — An I2C multiplexer. Needed only if more than two MPU-6050s share
a bus, since the chip offers just two addresses.

**Buck converter** — A step-down switching power supply. Here, the bike's ~14V
down to 5V, efficiently enough not to need a heatsink.

**TVS diode** — Transient voltage suppressor. Clamps voltage spikes. On a
vehicle's 12V rail, not optional.

## Concepts

**IMU** — Inertial Measurement Unit. Accelerometer plus gyroscope, sometimes a
magnetometer.

**DOF** — Degrees of freedom. The MPU-6050 is "6-DOF": three axes of
acceleration, three of rotation.

**Roll / pitch / yaw** — Rotation about the three axes. On a bike: roll is lean,
pitch is nose up/down under acceleration and braking, yaw is which way it points.

**Sensor fusion** — Combining sensors so each covers the others' weaknesses.
Accelerometers are noisy but have an absolute gravity reference; gyroscopes are
smooth but drift. Fusing them gives smooth *and* stable.

**Drift** — A gyroscope measures rate of rotation, so getting an angle means
integrating it, and any small bias accumulates without bound. Unchecked, a
resting gyro reports the bike slowly rotating forever.

**Zero-rate offset** — The nonzero reading a gyroscope gives while perfectly
still. Measured during calibration and subtracted thereafter.

**Complementary filter** — The simplest useful fusion: trust the gyro over the
short term, the accelerometer over the long term, blended by one constant.

**Kalman filter** — Statistically optimal fusion that tracks its own
uncertainty. Better in principle, more to tune, harder to debug.

**Madgwick / Mahony filter** — Fusion algorithms designed for IMUs on
microcontrollers. Cheap, well tested, one gain to tune.

**DLPF** — Digital Low-Pass Filter, built into the MPU-6050. Without it, engine
and road vibration *aliases* — high-frequency shake masquerading as slow
movement in the data. Not fixable after the fact.

**Aliasing** — What happens when a signal changes faster than the sample rate
can capture: it reappears disguised as a slower signal. The reason to filter
*before* sampling rather than after.

**Clipping** — A reading beyond the sensor's configured range, recorded at the
maximum. The real value is gone and cannot be recovered, which is why the accel
range is set to ±8g rather than the ±2g default.

**Sprung / unsprung mass** — Sprung is everything the suspension carries
(frame, engine, rider). Unsprung is everything below it (wheels, brakes, lower
fork). Comparing an IMU on each is how suspension behavior becomes visible.

**Dead reckoning** — Estimating position from heading and speed when GPS is
unavailable. Drifts quickly; mentioned here only to note it is *not* being done.

## Protocols and platform

**I2C** — Two-wire bus (SDA data, SCL clock) for short-range chip-to-chip
communication. Devices are addressed, so several share one pair of wires.

**SPI** — Faster four-wire bus (SCK, MOSI, MISO, CS) used here for the SD card.
Each device needs its own chip-select line.

**UART** — Plain asynchronous serial, one wire each direction. The GPS speaks
NMEA over it.

**NMEA 0183** — The text sentence format GPS receivers emit, e.g.
`$GPRMC,...`. Verbose but trivially parseable.

**AD0** — The MPU-6050 pin that picks its I2C address: low = `0x68`, high =
`0x69`. Two sensors per bus, no extra parts.

**WHO_AM_I** — A register returning a fixed known value (`0x68`), used to
confirm the right chip is present and talking before trusting anything else.

**TWAI** — "Two-Wire Automotive Interface", Espressif's name for the ESP32's
CAN controller. Trademark caution, not a different protocol.

**CAN bus** — The vehicle network most modern bikes use for ECU, dash, and ABS
data. Needs a transceiver chip; reading it means learning the specific bike's
message IDs.

**ADC1 / ADC2** — The ESP32's two analog-to-digital converters. **ADC2 stops
working while WiFi is active**, so every analog input here is on ADC1.

**Strapping pins** — GPIOs the ESP32 samples at boot to decide how to start. A
wrong level on one stops the board booting; GPIO12 is the usual culprit.

**FreeRTOS** — The real-time operating system already running beneath Arduino
on the ESP32. Provides the tasks and queues this design uses openly.

**Task** — A thread under FreeRTOS, with its own priority and optionally pinned
to a specific core.

**Ring buffer** — A fixed-size queue that wraps. Decouples a fast producer from
a bursty consumer with no memory allocation at runtime.

**Jitter** — Variation in the timing of something meant to be regular. Sampling
jitter puts error into every rate and angle computed from the data.

**Load dump** — The large voltage spike when a vehicle's alternator is
disconnected from its load. Worst case on the 12V rail, and why the input needs
protection.

## Project conventions

**ADR** — Architecture Decision Record. One numbered entry per decision, with
its rationale and what would make it wrong. In [DECISIONS.md](DECISIONS.md).

**Session** — One continuous recording, from motion start to stop. One
directory on the card.

**Source / sink** — This project's terms for a data producer (sensor) and a
data consumer (card, display). See [ARCHITECTURE.md](ARCHITECTURE.md).

**Schema version** — The integer in `meta.json` saying which log layout a
session uses, so decoders can refuse files they do not understand.
