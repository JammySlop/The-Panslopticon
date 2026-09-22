# The bike — 2006 Honda CBR600RR (PC37)

Vehicle-specific facts that drive design decisions. Kept separate from
[HARDWARE.md](HARDWARE.md) so the logger design stays portable to another bike,
and so it is obvious which assumptions are about *this* machine.

> **Confidence is marked per item.** Anything tagged *unverified* should be
> checked against the service manual before parts are bought or holes drilled.

## Identity

| | |
|---|---|
| Model | Honda CBR600RR, 2006 (PC37, second generation) |
| Generation | 2005–2006 — new frame and bodywork vs 2003–2004 |
| Engine | 599 cc inline-four |
| Frame | Aluminium twin-spar |
| ABS | **None** (arrived on the CBR600RR in 2009) |
| Traction control | None |

## Electrical

### No CAN bus — confirmed

The PC37 has a **4-pin DLC** (Data Link Connector) under the seat, near the
battery. It carries **K-line**: ISO 9141-2 at the physical layer with Honda's
own message structure, not OBD-II and not CAN.

**This closes Q7 with a no**, and retires the CAN wheel-speed path in ADR-0011
as originally designed. See ADR-0016 for what replaced it.

K-line remains available for *engine* data — RPM, throttle position, coolant
temperature — and the protocol has been reverse-engineered by the community
(pgmfi.org, RaceChrono). It is a 10.4 kbaud request/response protocol, so
realistically 5–10 Hz for a handful of parameters: fine for engine data,
too slow and too laggy to serve as a speed source.

### Battery

**YTZ10S, 12 V, 8.6 Ah, ~190 CCA** *(fitted to this bike — confirmed by the
owner)*. The stock part for the PC37 is the smaller YTZ7S (6 Ah); the YTZ10S is
a capacity upgrade.

Drain arithmetic at the logger's ~100 mA from 12 V:

| | YTZ7S (stock, 6 Ah) | **YTZ10S (fitted, 8.6 Ah)** |
|---|---|---|
| To ~50% — likely won't crank | ~30 h | **~43 h (~1.8 days)** |
| To flat | ~60 h | ~86 h (~3.6 days) |

Better than stock, and **still under two days before the bike won't start.**
The conclusion is unchanged: ADR-0015's ignition-sense line is required, not a
refinement.

The extra ~60 CCA also means slightly less voltage sag while cranking, which
marginally eases the 6–8 V brownout case — but not enough to design around.

**Physical note:** the YTZ10S is larger than the YTZ7S it replaces. Confirm
what space remains around the battery box before counting it as a mounting
location.

### No wheel speed sensors

No ABS means no wheel-speed sensors to tap. The speedometer is driven from a
gearbox/countershaft sensor — which reads *driven* speed, behind the clutch and
before final drive, and is therefore corrupted by rear wheel slip and dependent
on sprocket choice.

Fitting an independent front-wheel sensor is both easier and better. ADR-0016.

## Thermal — the under-seat exhaust

**The 2003–2006 CBR600RR routes its exhaust up under the tail**, with a
catalytic converter added in 2005. *(Well established for this generation;
confirm the exact routing on the actual bike.)*

The consequence is blunt: **the obvious place to mount electronics is occupied
by the hottest part of the motorcycle.** This is a hard constraint, not a
preference.

- Electrolytic capacitor life **halves for every 10 °C** — and ADR-0015 puts
  1000–2200 µF in this system. Use 105 °C-rated parts at minimum, and keep
  them away from the tail.
- SD cards and the ESP32 both have operating limits well below what an
  undertail space reaches on a hot day in traffic.

Candidate locations, none yet chosen (feeds into Q1):

| Location | For | Against |
|---|---|---|
| Under the fuel tank | Cool, central, rigid frame nearby | Cramped, awkward access |
| Tail, ahead of the exhaust | Accessible, plastic above for GPS | Heat, needs a shield |
| Pillion seat area | Easy access, plastic for GPS antenna | Loses pillion use, higher on the bike |

## Tyres and rolling radius

Stock fitment: **120/70-ZR17 front, 180/55-ZR17 rear.**

Reworking ADR-0011's lean/rolling-radius bias with real sizes, using
`r_eff = R − r_c(1 − cos θ)`:

| | Radius R | Arc r_c *(est)* | r_eff at 45° | Speed reads |
|---|---|---|---|---|
| Front 120/70-17 | 0.300 m | ~0.055 m | 0.284 m | **+5.7%** |
| Rear 180/55-17 | 0.315 m | ~0.065 m | 0.296 m | **+6.4%** |

The front is marginally less lean-biased as well as being undriven — a second,
smaller reason to prefer it. `r_c` is estimated from tyre profile, not measured;
Q9 should use a measured value.

## What this bike is for

A track-capable supersport. Lean angle, braking and cornering — the project's
primary goals — are the interesting questions on this machine, which is why
they were chosen. Suspension and vibration analysis remain optional (Q1).

## Still to verify

- [ ] Exhaust routing and actual undertail temperatures, measured
- [ ] Speedometer sensor type and location, from the service manual
- [ ] A switched accessory circuit suitable for the ignition-sense tap
      (**not** ABS, ECU, or ignition-critical wiring — ADR-0015)
- [ ] Front rotor bolt pattern and caliper clearance for the Hall sensor
- [ ] Physical space at each candidate mounting location
