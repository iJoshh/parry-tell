# Conversation 005 — Codex GREEN-LIGHT for v6 Probe Spec Revision 4

**Date:** 2026-05-08
**Codex MCP session ID:** `019e0835-3305-7a22-b8b4-3d2168fc34e3` (continued from 001-004)

## Verdict

> "Revision 4 is architecturally green-lightable. The sampling-rate blocker
> is resolved: focused enemy Tier 3 at hook tick is the right move for 33ms
> windows."

> "Verdict: Green-light after the doc-drift cleanup above. No remaining
> design blockers."

## Doc-drift items (5) — fixed in spec revision 4 final

1. ~~Title says "revision 2"~~ → fixed to "revision 4"
2. ~~Init order says `128 × 256 KB = 32 MB`~~ → fixed to `256 × 256 KB = 64 MB`
3. ~~Logging output says `500 MB - 1.5 GB / hour`~~ → fixed to `5-10 GB / hour`
4. ~~"Within 1 frame / ±2 frames" tolerance~~ → standardized to "±1 focused
   sample period (~11 ms at 90 Hz)"
5. ~~Discovery mode prose implies all enemies get full payload~~ → fixed
   to reference rev4 tier table

## Design additions Codex specified (2) — added in rev4 final

1. **Per-sample focused enemy fields:**
   - `focused_enemy_handle: u64`
   - `focused_reason: u8 enum` (0=none, 1=lock_on, 2=boss_bar_0, 3=qualification_nearest)

2. **Decimation phase staggering:** each enemy gets `phase = hash(handle) % N`
   so 10 Hz / 2 Hz emissions don't bunch on the same hook tick.

3. **CPU budget soft target (2 ms) inside hard ceiling (3 ms):** focused
   capture first, then check elapsed, then non-focused Tier 3, then check
   elapsed, then lesser. Budget skip aborts lesser-tier first.

## Codex's qualification recommendation (not a blocker)

For qualification mode, recommend Josh lock on before engaging the test
enemy and stay locked. Not for data validity — for keeping qualification
scope narrow.

## Status

**Spec revision 4 final is GREEN-LIT.** Source can now be written.

Total Codex review iterations: 4 turns over one MCP session.
- Turn 1: discovery probe vs database mod conversation
- Turn 2: rev1 review → 10 substantive issues, 2 blockers
- Turn 3: rev2 review → 5 new blockers from over-correction + nits
- Turn 4: rev3 review → sampling rate blocker (10 Hz misses 33 ms windows)
- Turn 5: rev4 green-light + 5 doc-drift + 2 design additions

The spec is now sound. Source next.
