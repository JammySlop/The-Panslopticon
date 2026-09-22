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

**Specific force** — What an accelerometer actually measures: acceleration
minus gravity. A device at rest reads 1g upward, not zero. Distinguishing this
from "acceleration" is the whole basis of the centripetal correction below.

**Centripetal acceleration** — The inward acceleration of anything moving on a
curve, magnitude `v·ψ̇`. On a leaned bike it points through the machine toward
the corner's inside and is indistinguishable, to the accelerometer, from
gravity being in a different place.

**Centripetal correction** — Subtracting the centripetal term from the
accelerometer using externally supplied speed: `a_gravity = a_measured − ω × v`.
Restores the accelerometer as a valid vertical reference mid-corner. This
project takes `v` from GPS. See ADR-0010.

**Coordinated turn** — A turn in which the vehicle is banked exactly enough
that occupants feel no sideways force — the resultant points straight "down"
through the vehicle. A motorcycle in a steady corner is always in one, because
anything else falls over. Gives `tan(lean) = v·ψ̇/g`.

**Force-vector angle** — Lean angle computed from the resultant of gravity and
cornering force. A few degrees less than actual chassis lean, because the
contact patch shifts toward the inside of the tire as it rolls onto its
shoulder. What this project logs, labeled as such.

**Doppler speed** — How a GPS receiver measures velocity: from the frequency
shift of satellite carriers, not by differencing positions. Around 0.05 m/s
accurate, and far more trustworthy than the same receiver's position.

**GPS-aided INS** — The general technique of using satellite navigation to
bound the drift of an inertial system. What ADR-0010 is a narrow, cheap
instance of.

**Scale factor** — A multiplicative correction between what a sensor reports
and the truth. Wheel speed has one (rolling radius); GPS speed does not. A
noisy-but-unbiased sensor can teach a quiet-but-biased one its scale factor,
which is the whole idea behind ADR-0011.

**Rolling radius** — The effective radius converting wheel rotation into
distance travelled. Not a constant: it shrinks with tire wear and pressure
loss, and shrinks *with lean angle* on a motorcycle, because the bike rides on
the tire's smaller-radius shoulder. `r_eff = R − r_c(1 − cos θ)`.

**Wheel slip** — Any mismatch between wheel rotation and ground speed: spin
under power, lock under braking. Makes wheel speed briefly fiction, which is
why the IMU is used to cross-check it.

**ABS / traction control** — Systems that modulate braking or power at the edge
of grip. Both indicate slip is happening. If the bus reports their state, it is
a free and authoritative slip flag.

**Gating** — Only updating an estimate when conditions make it trustworthy.
Here, the wheel scale factor adapts solely when the bike is upright, at speed,
not slipping, and GPS is healthy — and is otherwise frozen and used as-is.

**Latency compensation** — Correcting an estimate using a measurement that
describes a moment already past. Requires keeping a short history to compare
against, rather than applying an old measurement to the present.

**UBX** — u-blox's binary protocol, an alternative to NMEA text. More compact,
and it reports accuracy estimates NMEA simply does not carry.

**NAV-PVT** — The UBX message giving Position, Velocity and Time together, plus
fix status and accuracy estimates. One message replaces several NMEA sentences.

**sAcc** — The speed accuracy estimate in `NAV-PVT`: the receiver's own
statement of how much to trust its velocity. Drives the ADR-0011 gating.

**PPS** — Pulse Per Second. A GNSS receiver's hardware timing output, accurate
to tens of nanoseconds, marking the exact top of each second.

**TTFF** — Time To First Fix. Cold from nothing is ~30 s; hot, with retained
ephemeris from a backup battery, ~1 s.

**Ephemeris** — The orbital data a receiver needs to compute a fix. Retaining
it across power cycles is what a backup battery buys.

**HDOP** — Horizontal Dilution of Precision. A measure of how favorably the
visible satellites are arranged. Low is good; a high value means the fix is
geometrically weak even with plenty of satellites.

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
