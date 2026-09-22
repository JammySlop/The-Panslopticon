# Working agreement — MDL

Instructions for any AI agent (or automated tool) doing work in `MDL/`. A human
picking this up can read it too; nothing here is agent-only magic.

## The one rule

**The documents in `docs/` are the project's memory. Update them in the same
change as the code, never afterward.** A session that writes firmware and leaves
`STATUS.md` stale has left the project harder to pick up than it found it, which
is the single failure this structure exists to prevent.

## What to update, when

| When you… | Update |
|---|---|
| Finish or partly finish any work | `docs/STATUS.md` — always, every session |
| Choose between real alternatives | `docs/DECISIONS.md` — add an ADR, don't bury the reasoning in a commit message |
| Change wiring, pins, or parts | `docs/HARDWARE.md` — and check the pin table for conflicts |
| Learn something about the bike itself | `docs/BIKE.md` — and mark how confident you are |
| Change what gets logged or how | `docs/DATA-FORMAT.md` — and bump the schema version |
| Change module structure or tasks | `docs/ARCHITECTURE.md` |
| Complete a phase | `docs/ROADMAP.md` (tick it) and `docs/STATUS.md` (advance the phase) |
| Introduce a term a newcomer would not know | `docs/GLOSSARY.md` |

If a decision is made and then *reversed* later, do not delete the old ADR.
Mark it `Superseded by ADR-XXXX` and write a new one. The reasoning that was
wrong is worth as much as the reasoning that was right.

## Open questions are first-class

Unresolved design questions live in the "Open questions" section of
`docs/DECISIONS.md`, each with what it blocks and what would settle it. Do not
quietly pick an answer to an open question in code. Either raise it, or write
the code so the question stays open (a `#define`, a config field, an
abstraction) and say so in the ADR.

## Git workflow

- Branch for every piece of work. Never commit to `main`.
- Branch names: `mdl/<short-topic>`, e.g. `mdl/imu-sampling`.
- When the work is complete, push and open a **pull request** against `main`.
  Stop there. The repo owner reviews and merges; agents do not merge.
- Keep the PR body honest about what is untested. Firmware here is written
  without hardware in the loop most of the time — say when something has only
  been compiled, not run.

## Code conventions

- PlatformIO + Arduino framework, C++. Firmware lives in `firmware/`.
- No magic numbers for pins or rates. They go in `firmware/include/config.h`
  and are mirrored in `docs/HARDWARE.md`.
- Anything that touches the SD card must be safe to lose power at any instant.
  Assume the ignition can be cut mid-write, because it can.
- Sampling code must not block. If you are about to call something that can
  stall (SD write, WiFi, `Serial.print` on a full buffer) inside the sampling
  path, that is a bug even if it appears to work on the bench.

## Honesty about hardware

Much of this will be written before it is tested on a bike. Mark untested
claims as untested — in the PR, in `STATUS.md`, and in comments where a value
was calculated rather than measured. A datasheet number and a number observed
at 7000rpm on a vibrating frame are not the same thing.
