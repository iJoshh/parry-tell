# probe v6.4 — production build for multi-boss capture sessions

**Date:** 2026-05-11 (America/Chicago)
**Inputs:** HANDOFF.md plan, research/007/008 capture analysis, deep-critic feedback on supporting scripts

## Why

v6.3 capture (qualification-20260511-195759.bin, 12,467 focused rows of
c4311 + c4382 at Gatefront) resolved all three research-006/007 offset
questions with high confidence. The probe is functionally correct. v6.4
is a cleanup + production hardening pass ahead of Josh's multi-boss
co-op play session tonight.

## What changed

### Drops (instrumentation we don't need anymore)

- **Region 6** (phys_module_body, 512B) — no longer emitted. The world-pos
  values are already in the v6.2 enemy/player header block.
- **Region 7** (action_request_body, 512B) — no longer emitted. anim_id_path_c
  in the header is enough if we ever want to revisit.
- **Region 8** (time_act_child_body, 512B × 3) — no longer emitted.
  Research-007 confirmed the queue was dead and the children weren't the
  answer; v6.3 anim_id path A confirmed correct so this is moot.
- **Region 9** (module_bag_member, 512B × 16) — no longer emitted. Caught
  no anim_id signal; only stable hits were noise structural fields.

**Important:** Region IDs 6/7/8/9 stay reserved in the `RegionId` enum so
v6.2 and v6.3 captures still parse cleanly with the v6.4 parser. The
parser's REGION_NAMES map keeps the labels for backward compatibility.

### Kept (still useful going forward)

- **Region 0-5** (chr_ins_root, module_bag, time_act_module, time_act_focus,
  time_act_child, ai_struct) — all unchanged. These are stable production
  capture regions.
- **v6.2 enemy header block** (40 bytes + 20 pad = 64 bytes of per-enemy
  metadata): anim_id_path_b, read_idx, anim_id_path_c, action_request_abs,
  phys_module_abs, world_pos_phys[3]. STILL EMITTED. The path_b/c reads
  cost ~12 SafeReads per focused row; harmless overhead, useful debug data.
- **v6.2 Tier 1 player block** (48 bytes): player_chr_ins_vtable,
  player_pos_phys, player_phys_module_abs, player_lock_new (+0x6B0),
  player_lock_area_new (+0x6B4). STILL EMITTED. Critical fields.
- **Legacy `+0x6A0` lock-on** wire-format field. KEPT. Cheap to retain;
  expensive to lose for cross-capture comparability.

### Co-op safety fix

**v6.4 excludes ALL WCM_PLAYER_ARRAY slots** from the roster sweep, not
just slot 0 (local player). Scans **8 slots** (FRIENDLY_SCAN_SLOTS):

- Slots 0-3 = base game's WCM_PLAYER_ARRAY (4 slots).
- Slots 4-7 = Seamless Coop extension that follows the base array.
  Each slot is independently validated via LooksLikeUserPtrFast so
  out-of-bounds reads (when Seamless isn't installed, or fewer players)
  are safely ignored.
- De-dup against local player and other discovered slots.

For tonight's 6-player lobby (Josh + 5 friends), 8 slots provides
comfortable headroom. If Seamless ever exposes more than 8 player slots
in one lobby, FRIENDLY_SCAN_SLOTS needs to grow.

Side effect of friendly exclusion: when `focus_reason=qualification_nearest`,
the probe picks the closest non-player non-friendly. For boss-bar enemies
the boss-bar path takes priority and works regardless.

### Audible F11 feedback

Two distinct tones emitted via `Beep()` (kernel32 PC speaker — works
without sound scheme + audible even over game music):

| Event | Tone | Duration | Pattern |
|---|---|---|---|
| ARMED | 660 Hz | 2 × 100 ms | low double-tap |
| DISARMED | 1320 Hz (octave higher) | 1 × 400 ms | long high beep |

`Beep()` blocks the calling thread (F11Thread is dedicated, 50 ms poll
loop tolerates the extra latency). Cannot be disabled at runtime in v6.4;
if Josh wants to mute it, drop the v6.3 DLL in place — they're wire-format
compatible.

### Supporting tools (Claude-side and station-side)

- **`tools/probe-status.ps1`** — PowerShell tailer for the station box.
  Watches the most recent .log.txt in C:\Projects\elden-ring\logs and
  prints ARMED / DISARMED transitions with timestamps. Handles
  same-name log truncation (re-opened files reset position) and avoids
  duplicate F11 transition reports (deep-critic findings applied).
  Run on a second monitor / sidebar window.
- **`tools/archive_session.sh`** — Copy session bins from the SMB mount
  to `captures/sessions/YYYYMMDD/`. Validates session name pattern
  (path-traversal safe), uses size + mtime to skip already-archived
  files, atomic-rename via .partial to avoid mid-copy corruption.
  **Copies all rotated bin shards** (`<session>.bin`, `.bin.001`, `.002`,
  ...) — bin rotates at 2 GB per RotateBinIfNeeded. A long armed session
  could cross that boundary, and missing shards would silently lose
  boss data.
- **`tools/segment_by_f11.py`** — Parse .bin + .log.txt, produce a
  per-F11-cycle segment manifest (`<session>.segments.json`). Handles
  arm-without-disarm by implicitly closing the prior interval at the
  new arm time (was a bug in the initial version — would have eaten
  boss 2 if Josh forgot to disarm). Translates log-side ms (probe-init
  epoch) to bin-side ms (session_start_ms epoch) via the manifest's
  session_start_ms field. Reports c-id breakdown per segment.

## What's NOT in v6.4

- Per-segment .bin emission (`--emit-bins` flag exists but returns
  non-zero with a clear error). Implementing requires byte-level record
  offsets we deliberately don't expose in probe_bin.read_bin. The
  segment metadata (ts_ms_rel ranges) is enough — analyzer can filter
  in memory.
- DB join-key fuzzy mapping (c4382 → c4380 family) — deferred to v6.5
  analytical work, not a probe issue.
- PlayerIns vtable RVA check — captured in the wire format (v6.2 field)
  but not consulted by the probe at runtime. The vtable RVA `0x02A7CB40`
  is documented in HANDOFF.md and matches PlayerIns subclass per
  research-007.

## Co-op session capture workflow

See HANDOFF.md "Multi-boss capture workflow" for the full play-side
instructions. Short version for Claude:

1. Josh starts game; probe attaches automatically.
2. Josh walks to boss; F11 → low double-beep = ARMED.
3. Combat (multiple wipes OK — probe stays armed through wipes).
4. Boss dies; F11 → long high beep = DISARMED.
5. Josh says "done with <boss name>".
6. Claude runs `tools/archive_session.sh <session-name>` → bin lands in
   `captures/sessions/YYYYMMDD/`.
7. Claude runs `tools/segment_by_f11.py captures/sessions/YYYYMMDD/<session>`
   → produces segment manifest. Each F11 cycle = one segment.
8. Repeat for next boss.

## Build provenance

- Source: probe/probe.cpp (3,395 lines after v6.4 diff, ~252-line diff
  vs v6.3)
- Built: MSBuild Release x64 on station
- DLL size: ~228 KB
- Schema: v2 (unchanged from v6.2). Parser is backward-compatible.

## Safety review

- No new memory writes. All v6.4 changes are read-only fields + the
  friendly-PC collection which adds 4 more SafeReads per sample.
- Per-sample payload shrinks by ~10 KB (the dropped instrumentation
  regions). Net positive for buffer pool headroom.
- Beep() at 660/1320 Hz is well within audible range and doesn't
  interfere with the probe's worker thread (separate thread).
- friendlyPCs[] is a stack-local array of 4 uintptr_t; constant-size,
  no heap allocation in the detour path.
