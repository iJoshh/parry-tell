# Conversation 003 — Codex Review of v6 Probe Spec Revision 2

**Date:** 2026-05-08
**Codex MCP session ID:** `019e0835-3305-7a22-b8b4-3d2168fc34e3` (continued from 001, 002)
**Tokens:** 141,716

## Verdict

"Not green-light yet. Revision 2 fixes the conceptual blockers, but it
introduces a few spec-level contradictions that will become implementation
bugs if source starts now."

## Codex's 5 remaining blockers (rev2 → rev3)

### B1 — Buffer pool math wrong

Rev2 said `256 buffers × 17 KB = 4.5 MB`. But `17 KB` is per-enemy, not
per-sample. Real worst case: `8 enemies × 17 KB = 136 KB top + 8 × 14 KB
lesser = 112 KB + Tier 1+2 + headers ≈ 250 KB per sample`.

Real pool: `128 × 256 KB = 32 MB`.

### B2 — Manifest written before its data exists

Rev2 init step `f` wrote manifest BEFORE FileVersion + sig-scan + roster
validation. Manifest contains those results. Reorder so manifest is
written AFTER all init data is collected, BEFORE hook install.

Also: pre-config-load logging needs a fallback path
(`<DLL_DIR>/parry-tell-probe.boot.log`) since the configured log_dir
doesn't exist yet.

### B3 — Region-relative offsets need region-local bases

Rev2 said "all Tier 3 records use enemy_chr_ins_abs as base." Wrong. Each
region (chr_ins_root, module_bag, time_act, ai_struct, time_act_child)
has its own logical base. Records must include `(region_id,
region_base_abs, source_chain, payload_offset, payload_len, payload)` so
analysis can correlate by `(region_id, payload_offset)` across sessions.

### B4 — TimeAct range inconsistent

Rev2 said `0x0..0x100` in changelog AND `0x0..0x2000` in Tier 3 table.
Pick one. (Picked 0x0..0x2000.)

### B5 — Banished Knight `c4100` example wrong

Rev2 said "likely c4100 for Banished Knight." `c4100` was the Crucible
Knight sentinel in prior notes. Verified: c4100 has 31 parry windows in
the database, but the actual character mapping isn't documented in
sources we have. Don't pre-name. The qualification analysis itself is
supposed to identify the cXXXX from captured fields.

## Codex's nits (rev2 → rev3)

### N1 — Smoke gate too strict

`monotonic_segments >= 5 AND max_segment_dur >= 1s` is too strict for
"walk in circles at a Grace" — locomotion loops are < 1s. Either:
- Add deliberate longer-anim instructions (attack, gesture, item use), OR
- Relax: `f32 in range AND positive monotonic during stable anim_id AND
  rewinds on anim_id change AND max_segment_dur >= 0.3s`

Picked: BOTH. Add longer-anim instructions AND relax threshold.

### N2 — Producer-side emergency degradation

Worker-side adaptive sampling can't react while the worker is stalled.
Add producer-side rule: if free-pool < 4 buffers for 200ms, producer
drops broad-sweep itself for next sample.

## What Codex confirmed is fine

- Database/c0000 concern: conceptually resolved IF qualification is
  mandatory and uses a verified-correct character ID
- Offset collision: resolved by neutral `field_at_0xNN` capture
- Delta off game thread: right architecture
- Roster quarantine: right shape, fallback to player + boss-bars
- Init-order PRINCIPLE (just need the manifest fix)
- Explicit `mode`: yes
- Pointer-following stricter: cap 8 fine
- Adaptive sampling: good enough with N2 added

## Status

Spec revision 3 written. All 5 blockers addressed plus both nits. About
to send back to Codex for final green-light turn before source is written.

## Codex's full response (verbatim)

[See task tool output for the full text. Saved here for completeness:]

Not green-light yet. Revision 2 fixes the conceptual blockers, but it
introduces a few spec-level contradictions that will become implementation
bugs if source starts now.

[5 blockers + 2 nits as detailed above]

Verdict: Do not write source until the five blockers above are corrected
in the spec. After those fixes, the design is green-lightable.
