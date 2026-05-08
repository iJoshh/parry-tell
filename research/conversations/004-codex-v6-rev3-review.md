# Conversation 004 — Codex Review of v6 Probe Spec Revision 3

**Date:** 2026-05-08
**Codex MCP session ID:** `019e0835-3305-7a22-b8b4-3d2168fc34e3` (continued from 001-003)

## Verdict

"Not green-light yet. Revision 3 fixes the previous five blockers, but it
exposes one larger blocker we should have called earlier."

## Codex's blocker (rev3 → rev4)

**Sampling rate too slow for parry windows.**

Rev3 (and earlier) proposed 10 Hz broad sweep. Codex did the math:

- Parry window typical duration: ~67 ms (2 frames at 30 fps engine)
- 10 Hz period: 100 ms
- Result: probe can completely miss an active parry flag even when
  everything else is correct
- Also: claim of "±1-2 frames at 90 fps qualification tolerance" is
  impossible at 10 Hz (only 100 ms granularity)

Verified empirically in `parry_data.json`:
- Min parry window duration: **33.3 ms** (1 frame)
- Median: 33.3 ms
- 75th percentile: 66.7 ms
- 95th percentile: 100 ms
- **A 10 Hz probe misses >50% of parry windows by Nyquist alone.**

Recommended fix:
- Tier 1 + Tier 2: hook-tick rate (60-90 Hz)
- Broad sweep for FOCUSED enemy (lock-on / boss-bar / qualification target):
  hook-tick rate
- Other top-tier enemies: 10 Hz
- Lesser enemies: 2 Hz

If every-frame broad sweep too expensive, start with ONE focused enemy at
frame rate and keep rest as lower-rate context. Flag search depends on the
focused stream, not on broad mob coverage.

## Buffer pool sizing nit

At 90 Hz, 128 buffers = 1.4s stall tolerance (too tight). Bump to 256
buffers × 256 KB = 64 MB.

## Doc-drift items (minor cleanup)

- Header still says "revision 2" → fix
- "What we already know" still says TimeAct `0x0..0x100` → fix to `0x0..0x2000`
- Test plan smoke section still says "walk in circles at a Grace" without
  the deliberate-action script → fix
- Qualification tolerance "±1-2 frames" not achievable at 10 Hz (downstream
  of sampling-rate blocker)

## What Codex confirmed is fine in rev3

- Buffer model is now coherent (just needs the 90 Hz adjustment)
- Init order is right
- Region-local bases are right
- TimeAct broad range right in main Tier 3 table
- cXXXX qualification no longer asserts a bad ID
- Smoke gate is much better
- Producer emergency degradation is a good addition

## Status

Revision 4 written. All 5 prior blockers + the rate blocker + 4 doc-drift
items addressed. About to send back to Codex for what should be the
green-light turn.

## Codex's full response (verbatim)

Not green-light yet. Revision 3 fixes the previous five blockers, but it
exposes one larger blocker we should have called earlier:

[full text saved above as "Codex's blocker"]

If every-frame broad sweep is too expensive, start with one focused enemy
at frame rate and keep the rest as lower-rate context. The flag search
depends on the focused stream, not on broad mob coverage.

[Buffer pool advice saved above]

[Doc-drift items saved above]

Answer: No green-light yet. Fix the sampling-rate design for short parry
windows, and I'd green-light source after that. This is the last
architectural blocker I see.
