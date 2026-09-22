# Data format

What a session looks like on the SD card, and the rules a decoder must follow.

**Current schema version: 1** (proposed — nothing has been written yet).

## On-card layout

```
/MDL/
├── SESS0001/
│   ├── meta.json      written at session open, updated at close
│   └── data.csv       one row per sample, appended, never rewritten
├── SESS0002/
│   └── …
└── calib.json         sensor calibration, survives reboots, not per-session
```

Session directories are numbered, not timestamped, because the logger does not
know the date until GPS gets a fix — which may be minutes into a ride or never.
The real start time, once known, goes into `meta.json`.

## `data.csv`

Plain CSV with a header row. Human readable on purpose: it opens in anything,
it is greppable, and a half-written file is still usable (ADR-0006).

| Column | Unit | Notes |
|---|---|---|
| `t_ms` | ms | Milliseconds since logger boot. Monotonic. The authoritative time base. |
| `ax`, `ay`, `az` | g | Accelerometer, calibration applied |
| `gx`, `gy`, `gz` | °/s | Gyroscope, zero-rate offset removed |
| `roll` | ° | Fused lean — sign per HARDWARE.md conventions. This is the **force-vector** angle; see below. |
| `pitch` | ° | Fused orientation |
| `fmode` | enum | How `roll` was produced: `0` IMU-only, `1` GPS-aided, `2` low-speed (correction not applied) |
| `temp` | °C | MPU-6050 die temperature. Not ambient; useful for drift correlation. |
| `fix` | 0/1 | GPS has a valid fix |
| `lat`, `lon` | ° | WGS84, 7 decimal places (~1 cm — well past what the receiver delivers, but cheap) |
| `vgps` | m/s | GPS Doppler speed over ground. Raw, never fused. |
| `vwhl` | m/s | Wheel speed as reported on CAN. Raw, uncorrected. Empty if no CAN. |
| `vfus` | m/s | **Fused speed** — what orientation fusion actually used (ADR-0011) |
| `vsrc` | enum | `0` none, `1` GPS only, `2` wheel only, `3` both fused |
| `kwhl` | ratio | Current wheel scale-factor estimate. A diagnostic: drift here means tire wear, pressure change, or a bad gate. |
| `crs` | ° | Course over ground, 0–360. Meaningless at a standstill. |
| `sats` | count | Satellites used |
| `sacc` | m/s | UBX `sAcc` — the receiver's own speed accuracy estimate. Drives ADR-0011 gating. |
| `gage` | ms | Age of the GPS fix at this row. `0` means this row carries a fresh fix. |

Example:

```csv
t_ms,ax,ay,az,gx,gy,gz,roll,pitch,fmode,temp,fix,lat,lon,vgps,vwhl,vfus,vsrc,kwhl,crs,sats,sacc,gage
12500,0.021,-0.412,0.908,1.2,-0.4,15.7,-24.3,1.8,1,31.4,1,39.7392000,-104.9903000,22.40,23.71,22.43,3,0.9448,178.2,9,0.042,40
12510,0.019,-0.418,0.905,0.9,-0.3,16.1,-24.5,1.8,1,31.4,1,39.7392000,-104.9903000,22.40,23.75,22.47,3,0.9448,178.2,9,0.042,50
```

### Why `fmode` exists, and why `roll` is not the whole story

Lean angle is computed with GPS speed removing centripetal acceleration from
the accelerometer (ADR-0010). When GPS drops out, or the bike is below about
3 m/s, that correction is unavailable or meaningless and the filter falls back.
The fallback is *recorded, not hidden* — `fmode` says which regime produced
every single row, so analysis can weight or discard accordingly instead of
silently comparing values of different quality.

Note in the example rows that `vwhl` reads high against `vgps` — the bike is
leaned ~24°, so the tire is rolling on its shoulder at a reduced radius, and
`kwhl` below 1.0 is the estimator compensating. Logging the scale factor makes
that visible rather than buried inside the filter.

`roll` is the **force-vector angle**: the tilt of combined gravity and
cornering force. Actual chassis lean is a few degrees greater, because the
contact patch migrates toward the inside of the tire as it rolls onto its
shoulder. That offset is tire-dependent and is *not* corrected in firmware
(Q5). Raw `ax…gz` and `spd` are all logged, so a better estimate can be
recomputed later from an existing session without re-riding it.

### The GPS-rate problem

The IMU runs at 100 Hz; GPS delivers at 10 Hz at best. Rather than leave 90% of
the GPS columns blank or invent interpolated positions, **the last known fix is
carried forward** and `gage` records how stale it is. A decoder can then choose:
drop rows where `gage` is large, or interpolate deliberately, with the
information needed to do either. Values are never silently invented.

At 22 m/s, a 100 ms old fix is 2.2 m behind the bike. For lap timing that
matters; for correlating lean angle with a corner it does not. Knowing which is
which is the decoder's job, and `gage` is what makes it possible.

## `meta.json`

```json
{
  "schema": 1,
  "session": 1,
  "boot_utc": "2026-09-22T18:04:11Z",
  "boot_utc_source": "gps",
  "firmware": "0.1.0",
  "sample_hz": 100,
  "imu": { "model": "MPU6050", "addr": "0x68", "accel_g": 8, "gyro_dps": 500, "dlpf_hz": 44 },
  "calib": { "applied": true, "at": "2026-09-20T15:02:00Z" },
  "closed_cleanly": true,
  "rows": 184320,
  "notes": ""
}
```

`closed_cleanly` is written only at a clean shutdown. **Its absence is the
signal that the ignition was cut mid-session** — which is normal, not an error,
and simply means the last few rows may be short.

## Rules for decoders

Any tool reading these files must:

1. **Check `schema` first** and refuse politely on a version it does not know.
2. **Discard a malformed final row.** Power loss truncates mid-line. A trailing
   partial row is expected, not corruption.
3. **Treat `t_ms` as the only reliable clock.** Wall time comes from GPS, may be
   absent, and may jump when the first fix arrives.
4. **Never assume rows are evenly spaced.** They should be, but a dropped sample
   is possible; use `t_ms` deltas rather than row index × period.
5. **Use `vfus` for analysis, `vgps`/`vwhl` for diagnosis.** The raw inputs are
   logged so a fusion bug can be found — and so speed can be re-fused offline
   with a better filter — but they are not the answer to "how fast was I going".
6. **Respect the mode flags.** `fmode` and `vsrc` mark rows produced by degraded
   fallbacks. Comparing them against full-quality rows without weighting is the
   easiest way to draw a confident wrong conclusion from this data.

## Size

About 141 bytes per row at 100 Hz ≈ 14 KB/s ≈ **49 MB per riding hour**. A 32GB
card holds hundreds of hours. Storage is not a constraint; write *throughput*
during a stall is the thing to watch.

## Why CSV, and when to abandon it

CSV costs roughly 3× the bytes of a packed binary record and costs CPU to
format. It is chosen anyway because at this scale neither cost matters and
being able to open a file and *see* what the bike did is worth more during
development than efficiency is (ADR-0006).

**Revisit if:** the sample rate goes above ~200 Hz, channel count roughly
doubles (CAN would do it), or measurement shows formatting/writing eating the
sampler's headroom. The escape route is a packed fixed-size binary record plus
a desktop decoder — which is why the header is versioned and why tools must
check it.
