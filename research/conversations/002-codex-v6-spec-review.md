# Conversation 002 — Codex Review of v6 Probe Spec

**Date:** 2026-05-08
**Codex MCP session ID:** `019e0835-3305-7a22-b8b4-3d2168fc34e3` (continued from conversation 001)
**Tokens:** 126,541

## Topic

Claude drafted `probe/v6/PROBE-V6-SPEC.md` and asked Codex for adversarial review.
Josh's instruction: "Do it right. My arrival timeline isn't a concern. Ever.
Do it right. Build the probe right."

## Codex verdict

"I would not implement this spec as-is. The direction is good, but there are
several ways it can produce a beautiful hour-long dataset that is not labelable."

## Codex's 10 substantive issues

### Blocker 1 — Database join key not validated

Spec proposes capturing `npc_param_id` and joining to `parry_data.json` keyed
by `cXXXX` post-session. These are not automatically the same key. Need an
**oracle qualification run** (2-3 min capture against known enemy with known
parry windows, prove the live join works against video) BEFORE the real hour.

### Blocker 2 — Offset collision at +0x6C

Spec uses `+0x6C` for both `enemy_block_id` and `teamType`. These conflict.
Need resolution before code is written.

### 3 — Delta compression off the game thread

The detour should fill a preallocated fixed-size buffer and enqueue an index.
Worker does byte-delta computation. Don't do compare/encode inline in the
detour.

### 4 — TimeAct capture too narrow

Spec says `0x20..0x40` (32 bytes). Codex wants `0x0..0x100` (256 bytes) PLUS
explicit `0xC0..0xE0` capture. AND monotonicity validation DURING smoke test,
not post-session.

### 5 — Roster enumeration must be quarantined

`WCM + 0x1F1B8/+0x1F1C0` for `ChrInsByUpdatePrioBegin/End` must validate before
capture: begin<end, 8-byte aligned, count<2048, each ptr user-shaped, handle
nonzero, GetChrInsFromHandle round-trips, anim ID sane and changes, boss-bar
enemy appears in roster at least once. If any check fails, disable roster,
capture only player + boss bars.

### 6 — Config load before hook install

Currently config loads after hook install. Wrong. Order: load config →
allocate buffers → resolve refs → install hook (last). Avoid any state where
detour can run before settings/buffers are immutable.

### 7 — Explicit mode, not boolean

Replace `broad_sweep_enabled = true|false` with `mode = smoke|discovery`.
No silent default for the real run.

### 8 — Session manifest

Add: build hash, ER version, module base, exact config, offsets used,
schema version, dropped sample counts, budget skip counts. Correlate by
region-relative offsets, NOT absolute (ASLR + allocator churn).

### 9 — Pointer-following stricter

One-level TimeAct child capture acceptable IF: 8-byte aligned source AND
target, target not in stack-ish ranges, cap 8 child pointers per entity (not
16), record source offset + target address in record, full snapshot on child
target change. NO VirtualQuery in detour.

### 10 — Adaptive sampling

Drop broad sweep first (already in spec). ALSO: if drops exceed threshold
over 5s, reduce broad rate to 5 Hz, then reduce enemy cap. Keep Tier 1/2
best-effort.

## What's solid in the spec (Codex didn't push back on)

- Three-tier capture model
- 270 KB per sample × 10 Hz target rate
- SPSC queue architecture (just refined the unit to fixed-size buffers)
- Separate-NVMe output
- Module pinning + SafeRead<T> + LooksLikeUserPtrFast
- Hook target (UpdateUIBarStructs) via sig-scan
- Time budget cap with graceful degrade
- nlohmann/json removed; JSON join is post-session

## Field offset capture list per Codex

Capture all of these as raw u32/u8 fields per enemy, label neutrally,
validate post-session:
- `+0x60` (npc param id)
- `+0x64` (modelNumber — possibly the actual key for parry_data.json cXXXX)
- `+0x68` (chrType)
- `+0x6C` (teamType per PostureBarMod)
- `+0x80` (entity_id per PROBE-SPEC.md)
- `+0x1E8` (entity_id per Codex offsets research)

Don't claim block_id at +0x6C until resolved.

## Required pre-real-session steps per Codex

1. Build v6 with revisions
2. Smoke test (60 sec, walking circles)
3. **Oracle qualification run** (2-3 min, known enemy with known parry windows)
4. Prove live `(modelNumber, anim_id, anim_time_candidate)` joins to
   `parry_data.json` and predicts windows that line up with video
5. THEN run the real 1-hour session

## Status

Spec needs revision. Claude addressing now. After revision, send back to
Codex for one more turn to confirm blockers are resolved.
