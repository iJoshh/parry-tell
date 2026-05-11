# probe v6.2 — Research 006 instrumentation build

**Date:** 2026-05-11 (America/Chicago)
**Inputs:** research/006-SYNTHESIS.md, research/006-fixture-verification.md, research/006-codex-research.md

## Why

Probe v6.1.1 has three offset bugs identified by deep research:

1. World position reads at `ChrIns + 0x6C0` produce noise — actual position is via `module_bag → +0x68 → +0x70` (Vector3)
2. Enemy active anim_id reads at `TimeActModule + 0xD0` return 0 — fixture refutes all proposed alternatives; needs widened capture surface
3. Player lock-on at `PlayerIns + 0x6A0` returns pointer-shaped value — correct offset is `+0x6B0` per 4-vendor consensus (Erd-Tools, vswarte, TGA CT, PostureBarMod)

v6.2 is **an instrumentation build, not a fix build.** It dual-reads everything ambiguous + expands the capture surface so the next live capture can definitively resolve all three offsets in one session, then v6.3 ships with the dead reads removed.

## What

**Schema bumped to v2.** Backward-compatible parser (`tools/probe_bin.py`) handles both v1 and v2 captures.

### Probe-side changes (probe/probe.cpp)

- `PROBE_VERSION_STR` = `"v6.2"`, `PROBE_SCHEMA_VERSION` = `2u`
- New offset constants:
  - `CHR_INS_VTABLE_PTR = 0x000` — vtable for PlayerIns* vs ChrIns* discriminator
  - `PLAYER_INS_LOCK_HANDLE_NEW = 0x6B0` — Erd-Tools lock-on FieldInsHandle
  - `PLAYER_INS_LOCK_AREA_NEW   = 0x6B4` — TargetArea sanity-check
  - `MODULE_BAG_PHYS_PTR        = 0x68` — CSChrPhysicsModule pointer
  - `MODULE_BAG_ACTION_REQ_PTR  = 0x80` — ActionRequest module pointer (enemy anim alt)
  - `TIME_ACT_READ_IDX          = 0xC4` — u32 index into anim_queue
  - `TIME_ACT_ANIM_QUEUE_BEGIN  = 0x20` — vswarte 10×16B circular buffer
  - `ACTION_REQ_ANIM_ID         = 0x90` — Erd-Tools enemy anim offset
  - `PHYS_MODULE_POS_X/Y/Z      = 0x70/0x74/0x78` — Vector3 world pos at phys leaf
- New region IDs (schema v2):
  - `REGION_PHYS_MODULE         = 6` — 512B body of CSChrPhysicsModule (world pos leaf)
  - `REGION_ACTION_REQUEST      = 7` — 512B body of ActionRequest module (enemy anim leaf)
  - `REGION_TIME_ACT_CHILD_BODY = 8` — 512B deep-bodies of first 3 TimeActChild structs
- `EnemySnapshot` extended with `anim_id_path_b`, `read_idx`, `anim_id_path_c`, `action_request`, `phys_module`, `world_pos_phys[3]`
- `EnemyReadTier2()` now populates all v6.2 fields per focused enemy
- `EnemyHeader` wire size: 96 → 136 bytes (40 new bytes for v6.2 block; pad shrinks 24 → 20 to align)
- Tier 1 player area: adds 48-byte v6.2 instrumentation block after `playerLockHandle`:
  - 8 bytes `playerChrInsVtable`
  - 12 bytes `playerPosPhys[3]` (Vector3 via phys chain)
  - 8 bytes `playerPhysModule` (absolute addr for region cross-ref)
  - 8 bytes `playerLockHandleNew` (`+0x6B0`)
  - 4 bytes `playerLockAreaNew` (`+0x6B4`)
  - 8 bytes reserved
- `WriteTier3ForEnemy()` emits 3 new region payloads conditional on pointer validity
- CSV emitter (`EmitCsvEnemyRow`) extended with v6.2 columns; `ParseHeader` skips 48-byte v6.2 player block when `schema_version >= 2`

### Parser-side changes (tools/probe_bin.py)

- `Sample` dataclass: 5 new v6.2 fields (vtable, phys_pos, phys_module_abs, lock_new, area_new)
- `EnemyRecord` dataclass: 6 new v6.2 fields (anim_id_path_b, read_idx, anim_id_path_c, action_request_abs, phys_module_abs, world_pos_phys)
- `_parse_sample`: gated 48-byte v6.2 player block read by `schema_version >= 2`
- `_parse_enemy`: takes `schema_version` argument; reads 40-byte v6.2 block + 20-byte pad on v2; 24-byte pad on v1 (backward-compat)

## What v6.2 captures will show

Per focused row:

- `anim_id` (path A, legacy): expected to remain 0 for enemies (confirmed via fixture)
- `anim_id_path_b` (vswarte anim_queue): if `read_idx < 10` AND entry is a real anim ID → BUG 2 SOLVED via path B
- `anim_id_path_c` (Erd-Tools ActionRequest): if non-zero AND matches a c4380 anim ID → BUG 2 SOLVED via path C
- `action_request` region body (region 7): if path C is wrong, the surrounding 512B will show where the real anim_id field sits
- `time_act_child_body` regions (region 8): wider 512B captures vs the previous 256B scan; gives the analyzer room to find anim_id if it lives inside a TimeActChild instead

Per player:

- `player_chr_ins_vtable`: identifies whether the player pointer is PlayerIns* (= `+0x6B0` likely correct) or ChrIns* (= `+0x6A0` likely correct)
- `player_lock_legacy` vs `player_lock_new`: toggle correlation against intentional lock-on/off cycles during capture identifies the right offset
- `player_pos_phys` vs `player_pos` (legacy): the new value should be a sensible world coord that tracks player movement; legacy stays as noise

## What the next live capture needs to produce

A ~60s qualification-mode capture with:

1. Player intentionally locks ON / OFF / cycles targets 6-8 times during the fight (toggle correlation for lock-on offset)
2. Sustained combat with one Godrick Knight (c4380 family) so anim_id has many transitions
3. Player moves around (walking, dodging) so phys-chain position changes are visible

## What v6.3 will do

After analysis of the v6.2 capture, v6.3 will:

- Remove the dead reads (whichever paths A/B/C lost)
- Remove the dual-read player lock fields
- Remove the v6.2 instrumentation regions that didn't pan out
- KEEP the v6.2 schema bump if any new field is retained

## File manifest

- `probe/probe.cpp` — modified (246 line diff)
- `tools/probe_bin.py` — modified (62 line diff)
- `probe/v6.2/probe-v6.2.patch` — unified diff vs the v6.1.1 baseline (this directory)
- `probe/v6.2/CHANGES.md` — this file
- `probe/v6.2/apply-and-build.sh` — convenience SCP+MSBuild trigger (TBD if needed; otherwise use `tools/rebuild-and-stage.sh`)

## Safety review

- No new memory writes — all v6.2 reads are SafeRead<T> through existing pointer chain helpers
- All new pointer dereferences are gated by `LooksLikeUserPtrFast()` (the same guard the rest of the probe uses)
- Vtable read is on `playerChrIns + 0x000`, which is a one-deref guarded by the existing `playerChrIns` null/ptr-shape checks
- Region 6/7/8 sizes (512B each) increase max per-sample payload by ~1.5KB worst case (3 regions × 512B); well within the 256KB sample buffer
- Schema-versioned parser is backward-compatible: v1 captures still parse correctly with the updated `probe_bin.py`
