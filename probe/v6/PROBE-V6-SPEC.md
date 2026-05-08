# parry-tell-probe v6 — Discovery Probe Specification (revision 4)

**Status:** Revision 4 — addresses sampling-rate blocker from Codex's
revision-3 review. Median parry window in `parry_data.json` is **33.3 ms**;
a 10 Hz probe samples every 100 ms and would miss most windows entirely.
Awaiting Codex green-light before source is written.
**Prior revisions:** rev 1 (initial), rev 2 (post first review), rev 3
(post second review).
**Author:** Claude (Mae) for Josh, 2026-05-08.
**Target:** Elden Ring 2.6.1.0, mods enabled (UnlockTheFps + 4 surgical
patches), running at ~90 fps, separate NVMe drive for output.
**Goal:** Capture enough memory state during ~1 hour of varied gameplay
(mobs + bosses, parryable + non-parryable enemies) to identify the runtime
parry-active flag (and ideally the hyperarmor-active flag) by post-session
correlation against the TAE database and gameplay video.

## Changelog

### Revision 4 changes (post Codex rev-3 review)

In response to `research/conversations/004-codex-v6-rev3-review.md`:

- **R4-B1 (10 Hz too slow for parry windows):** Codex caught that 10 Hz =
  100 ms period misses parry events shorter than that. Verified empirically
  in our database:
  - Min parry window duration: **33.3 ms** (1 frame at 30 fps engine)
  - Median: 33.3 ms
  - 75th percentile: 66.7 ms
  - 95th percentile: 100 ms
  - **A 10 Hz probe misses >50% of parry windows entirely.**

  Revised sampling tiers:
  - **Focused enemy (locked-on, OR boss-bar slot 0, OR qualification target):**
    capture every hook tick (~90 Hz). Tier 1 + Tier 2 + ALL Tier 3 regions.
  - **Other top-tier enemies (up to 7 more):** Tier 1+2 every hook tick;
    Tier 3 broad sweep at 10 Hz.
  - **Lesser-tier enemies (up to 8):** Tier 1+2 at 10 Hz; Tier 3 at 2 Hz.

  At 90 Hz, focused enemy generates: ~17 KB/sample × 90 Hz = ~1.5 MB/s.
  Other 7 top-tier at 10 Hz: 7 × 17 KB × 10 = ~1.2 MB/s. Lesser 8 at 2 Hz:
  ~250 KB/s. **Total peak: ~3 MB/s**. Still fits NVMe with margin.

  1-hour real session estimate revised: 5-10 GB on disk (was 0.8-2.5).
  Still comfortable on a separate NVMe.

- **R4-B2 (buffer pool sized for 90 Hz):** at 90 Hz a 128-buffer pool gives
  only 1.4s stall tolerance. Per Codex: bump to **256 × 256 KB = 64 MB**.

- **R4-N1 (qualification tolerance):** rev3 claimed "±1-2 frames at 90 fps =
  ±22 ms" tolerance. With focused-enemy hook-rate capture (~11 ms period
  at 90 Hz), this is achievable. Tolerance restated: ±1 sample period at
  current hook tick rate.

- **R4-N2 (doc-drift fixes):**
  - Header now says "Revision 4."
  - "What we already know" item 5 says `0x0..0x2000` (not `0x0..0x100`).
  - Test plan smoke section references the deliberate-action script.

### Revision 3 changes (post Codex rev-2 review)

In response to `research/conversations/003-codex-v6-rev2-review.md`:

- **R3-B1 (buffer pool math wrong):** revision 2 said `256 × 17 KB = 4.5 MB`,
  but `17 KB` is per-enemy. A real sample with 8 top-tier enemies is ~200 KB.
  Revised: pool is 128 × `MAX_SAMPLE_BYTES` = 128 × 256 KB = **32 MB**.
  `MAX_SAMPLE_BYTES = 256 KB` is the fixed sample-buffer size; payload that
  exceeds it is truncated on the producer side and a `[truncated]` flag is
  set in the buffer header.
- **R3-B2 (init order writes manifest before its data):** revision 2 wrote
  the manifest at step `f` (after opening files but before sig-scan). The
  manifest contains sig-scan results. Revised: manifest write moves to AFTER
  all init steps complete and BEFORE hook install. Pre-config-load failures
  log to a DLL-dir fallback file (`parry-tell-probe.boot.log` next to the
  DLL, so Josh can debug "config wasn't found" issues).
- **R3-B3 (region-relative offsets need region-local bases):** revision 2
  said "all Tier 3 records use `enemy_chr_ins_abs` as base" — wrong.
  Revised: each region records its OWN `region_base_abs` plus its logical
  chain identifier. Format:
  ```
  region_id (u8): 0=chr_ins_root, 1=module_bag, 2=time_act, 3=time_act_focus,
                  4=time_act_child, 5=ai_struct
  region_base_abs (u64): the absolute address of THIS region's base
  source_chain (u32 packed): identifies how we got here from ChrIns
                  e.g. for time_act: chrIns->+0x190->+0x18
  payload_offset_relative (u16): offset INTO this region
  ```
  Analysis can compute `(region_id, payload_offset_relative)` to compare
  the same logical bytes across enemies and sessions.
- **R3-B4 (TimeAct range inconsistent):** revision 2 said `0x0..0x100`
  in the changelog AND `0x0..0x2000` in the Tier 3 table. Revised:
  unambiguously `0x0..0x2000` (8 KB) for `time_act_module` region. The
  `time_act_focus` region adds `+0xC0..+0xE0` (32 bytes, separately captured
  for emphasis even though it overlaps the broader region). Tier 2 explicit
  decoded fields are `+0x20`, `+0x24`, `+0x28`, `+0x2C` (anim time
  candidates) and `+0xD0` (anim ID).
- **R3-B5 (Banished Knight `c4100` example was wrong):** revision 2 said
  "likely c4100 for Banished Knight." Verified: c4100 has 31 parry windows
  in the database, but I cannot confirm it's actually Banished Knight from
  the data alone. Revised: protocol DOES NOT pre-name the cXXXX. It says
  "Josh fights one parry-eligible enemy whose cXXXX we will identify from
  the qualification capture." Removed unfounded asserts.

Plus minor revision-3 fixes from Codex's nits:
- **R3-N1 (smoke gate too strict):** "walk in circles at a Grace" can have
  locomotion loops < 1s. Revised smoke gate: candidate is f32 in range
  AND positive monotonic during stable anim ID AND rewinds on anim ID
  change AND `max_segment_dur >= 0.3s` (was 1s). Smoke instructions also
  add deliberate actions (one heavy attack, one gesture, one item use, a
  roll).
- **R3-N2 (producer-side emergency degradation):** revision 2 only had
  worker-side adaptive sampling. If the worker stalls (e.g. disk hiccup),
  worker-side adaptation is too slow. Added: producer-side emergency rule
  — if free-pool size < 4 buffers AND has stayed there for 200ms, the
  producer drops broad-sweep regions itself for the next sample
  (regardless of worker-side mode flags). Reset when free-pool > 16.
  Logged via atomic counter.

### Revision 2 changes (kept from prior version)

In response to `research/conversations/002-codex-v6-spec-review.md`:

- **B1 (database join):** added required oracle qualification run between
  smoke test and real session. Also captures candidate character-ID fields
  (`+0x60`, `+0x64`, plus `npc_param_id` from any other path we find) so
  post-session join can pick the right one.
- **B2 (offset collision):** all offsets in the `+0x38..+0x1F0` and
  `+0x6C0..+0x6E0` ranges are captured as RAW BYTES with NEUTRAL LABELS
  (`field_at_0xNN`). No semantic interpretation in the probe. Resolution
  happens post-session.
- **3:** delta compression moved off the game thread. Producer fills a
  fixed-size preallocated sample buffer, enqueues an index. Worker does the
  byte-delta compute on its own thread.
- **4:** TimeAct broad sweep widened. Animation-time candidates
  (`+0x20`, `+0x24`, `+0x28`, `+0x2C`) AND anim ID (`+0xD0`) decoded
  explicitly into Tier 2. Smoke test verifies one candidate is monotonic
  before we trust it.
- **5:** enemy roster enumeration is quarantined behind a 7-check init-time
  validation. If any check fails, fall back to player + boss bars only.
- **6:** init order: load config → validate config → resolve refs →
  validate roster → allocate buffers → write manifest → install hook (last).
  (Order revised again per R3-B2.)
- **7:** `[capture] mode = smoke | qualification | discovery` replaces the
  `broad_sweep_enabled` boolean. Missing config = fail closed.
- **8:** session manifest with build_hash, ER FileVersion, module_base, exact
  config dump, all resolved offsets, schema version. Per R3-B3, regions use
  REGION-LOCAL bases (not chrIns-rooted) for stable cross-session correlation.
- **9:** TimeAct child-pointer follow stricter: source offset 8-byte aligned,
  target 8-byte aligned, cap 8 children per entity, record source offset +
  target address, full snapshot when target changes. NO VirtualQuery in detour.
- **10:** adaptive sampling. Worker-side: drops > 5% over 5s rolling window
  triggers stepdown (10→5 Hz, then top-cap 8→4, then 5→2 Hz). Producer-side
  emergency added in R3-N2.

## Design philosophy

Capture broadly, label neutrally, validate the join key BEFORE the real
session, do delta work on the worker thread, fail closed when config is
missing.

We do not interpret any byte the data hasn't earned. The probe is an
instrument, not a hypothesis.

## What we already know (foundation v6 builds on)

1. **Hook target works.** `UpdateUIBarStructs` per-frame on the game thread.
   33 minutes of v5f runtime without crashing.
2. **Slot 0 = local PlayerIns.** v5e data: `mapX_6C0` changes as Josh walks.
   PlayerIns layout: `+0x6D0 block`, `+0x6C0 mapX`, `+0x6CC mapAngle`.
3. **For boss/enemy data we use a different path.** Boss-bar handles via
   `CSFeManImp::bossHpBars[]` resolved by `GetChrInsFromHandle`, plus
   nearest-N enemy roster via `WorldChrMan.ChrInsByUpdatePrioBegin/End`
   walk. Both paths quarantined-validated at init (see §5 below).
4. **Animation ID chain:** `chrIns + 0x190 → +0x18 → +0xD0`. HIGH confidence
   per Codex offsets research, valid for boss `ChrIns*` (target chain).
5. **Animation time:** unknown. Practice-tool's chain is hook-driven via a
   different `base_anim` symbol we don't have. Probe captures TimeAct
   `0x0..0x2000` raw and decodes 4 explicit candidates (`+0x20`, `+0x24`,
   `+0x28`, `+0x2C`) into Tier 2 for in-session monotonicity validation.
6. **TAE database:** `parry_data.json`, 31 MB, 6,738 parry windows. NOT
   loaded by the DLL. Used only by the post-session analysis tool.

## Field offset uncertainty — deliberate non-interpretation

There is a genuine layout conflict between sources:

- **PROBE-SPEC.md** (lifted from TarnishedTool): `+0x60` npc_param, `+0x68`
  chrType, `+0x6C` block_id, `+0x80` entity_id.
- **probe.cpp v5f** (lifted from PostureBarMod): `+0x038` block_id, `+0x064`
  chrType, `+0x06C` teamType, `+0x1E8` entity_id.

These are incompatible if they describe the same struct at the same level.
v5f's data confirmed slot 0 is PlayerIns, NOT a generic ChrIns — so the
PostureBarMod offsets that worked may only work for PlayerIns. For an enemy
`ChrIns*` resolved via `GetChrInsFromHandle`, we don't know which (if
either) layout is right.

**v6's decision:** capture raw 4-byte values at all candidate offsets with
neutral labels:

```
field_at_0x038, field_at_0x060, field_at_0x064, field_at_0x068,
field_at_0x06C, field_at_0x080, field_at_0x1E8
```

The post-session analysis tool decides which is `chrType`, `teamType`,
`block_id`, `entity_id`, `npc_param_id`, `model_number`. The probe makes
no claim.

## Modes

The probe operates in one of three explicit modes set in the config file.
Missing or invalid `mode` value = fail closed (DLL refuses to install hook,
logs `[init_fail] config mode invalid`).

### `smoke`

60 seconds of testing. Tier 1 + Tier 2 capture only, broad sweep DISABLED.
Goal: confirm hook fires, config loads, log file gets written, no crashes.

### `qualification`

2-3 minute capture against a known parry-eligible enemy (e.g., a Banished
Knight at Stormveil gate). Tier 1 + Tier 2 + broad sweep on the LOCKED-ON
enemy only (other enemies still tracked Tier 1+2 but no broad sweep).

After this run, the analysis tool joins captured `(field_at_0x60,
field_at_0x64, anim_id, anim_time_candidates)` against `parry_data.json`
and proves:
- Some candidate field reliably maps a memory value to a `cXXXX` character ID
- One of the anim_time candidates is monotonic during animations
- Predicted parry windows from the database line up with video timestamps
  (within 1 frame at 90 fps)

If qualification passes: green-light real session. If not: revise probe or
parser, retry.

### `discovery`

Full broad-sweep capture during the real ~1-hour session. Per the rev4
sampling tiers (focused enemy at hook tick, other top-tier at 10 Hz,
lesser at 2 Hz). Adaptive degradation under backpressure. NOT all enemies
at full payload — see Tier 3 table.

## Capture model

Same three-tier as revision 1. Per-tier specifics tightened per Codex feedback.

### Tier 1 — Ground truth state ("what was happening")

Always logged, every sample. ~512 bytes per sample.

| Field | Source | Notes |
|---|---|---|
| `frame` | atomic counter | sequence number |
| `ts_ms_rel` | `steady_clock` ms since session start | u32, 4.3M sec range |
| `wcm_ptr` | `*g_refs.wcmPtrAddr` | sanity check |
| `module_base_eldenring` | resolved at init, cached | for region-relative correlation |
| `player_chr_ins_abs` | slot 0 walk | for cross-reference within a session |
| `player_anim_id` | TimeAct chain off player | u32; validates chain works on player |
| `player_anim_time_candidates[4]` | float at TimeAct +0x20, +0x24, +0x28, +0x2C | monotonicity test, smoke-test gate |
| `player_pos_xyz` | PlayerIns +0x6C0..+0x6CC | f32×3, video correlation |
| `player_lock_on_target_handle` | playerIns +0x6A0 | u64 |
| `boss_bar_handles[3]` | `CSFeManImp + 0x5BF0` slots, +0x8 each | u64, sentinel UINT64_MAX |
| `enemy_count_tracked` | u8 | how many enemy slots populated this sample |

### Tier 2 — Per-enemy state ("what is each tracked enemy doing")

For each tracked enemy (up to N, see §enemy enumeration below):

| Field | Path | Notes |
|---|---|---|
| `enemy_chr_ins_abs` | resolved via boss-bar handle OR roster walk | within-session cross-ref |
| `enemy_handle` | `+0x8` | u64, cross-ref between samples |
| `field_at_0x038` | u32 | maybe block_id (v5f layout) |
| `field_at_0x060` | u32 | maybe npc_param_id (PROBE-SPEC layout) |
| `field_at_0x064` | u32 | maybe chrType OR modelNumber (Codex flagged +0x64 as a likely cXXXX key) |
| `field_at_0x068` | u32 | maybe chrType (PROBE-SPEC layout) |
| `field_at_0x06C` | u32 | maybe teamType OR block_id |
| `field_at_0x080` | u32 | maybe entity_id |
| `field_at_0x1E8` | u32 | maybe entity_id |
| `enemy_anim_id` | TimeAct chain → +0xD0 | u32 |
| `enemy_anim_time_candidates[4]` | float at TimeAct +0x20, +0x24, +0x28, +0x2C | monotonicity in qualification |
| `enemy_target_handle_candidates[8]` | unknown — see Tier 3 broad sweep | post-session correlation |
| `enemy_in_lock_on` | bool, computed | does player_lock_on_target_handle == enemy_handle |
| `enemy_in_boss_bar` | bool, computed | does any boss_bar_handle == enemy_handle |
| `enemy_in_roster` | bool, computed | is this enemy from the WCM prio-list walk |
| `focused_enemy_handle` | u64 (per-sample, not per-enemy) | which enemy is "focused" this sample |
| `focused_reason` | u8 enum | 0=none, 1=lock_on, 2=boss_bar_0, 3=qualification_nearest |

~256 bytes per enemy per sample.

### Tier 3 — Broad sweep (discovery payload)

**Three sampling tiers** (revised in rev4 to match parry window durations):

| Tier | Who | Tier 1+2 rate | Tier 3 rate |
|---|---|---|---|
| **Focused** | locked-on enemy OR boss-bar slot 0 OR qualification target | hook-tick (~90 Hz) | hook-tick (~90 Hz) |
| **Other top-tier** | up to 7 more enemies (boss bars + nearest hostile) | hook-tick (~90 Hz) | 10 Hz |
| **Lesser** | up to 8 more enemies | 10 Hz | 2 Hz |

The producer enforces these rates via per-region sample counters. Since
the hook fires every frame, the tier-1+2 capture is just "every call into
detour." Tier-3 rates are enforced by `if (sample_count % decimation_factor == 0)`.

| Region | Range | Bytes/sample |
|---|---|---|
| `chr_ins_root` | enemy `ChrIns + 0x0..0x800` | 2,048 |
| `module_bag` | enemy `ChrIns + 0x190 → *` first `0x200` | 512 |
| `time_act_module` | `chrIns+0x190 → +0x18 → first 0x2000` (8 KB) | 8,192 |
| `time_act_focus` | TimeAct + `0xC0..0xE0` (32 byte emphasis) | 32 |
| `time_act_child_pointers` | up to 8 child pointers, each child first `0x100` bytes | up to 2,176 |
| `ai_struct` | `chrIns+0x580 → +0xC0 → +0xE000..0xF000` | 4,096 |

**Per-enemy total per sample:** ~17 KB Tier 3 + ~256 B Tier 2 ≈ 17.3 KB.

### "Focused enemy" selection

The focused enemy is determined per sample with this priority (first match wins):
1. Player has lock-on (player_lock_on_target_handle != 0xFFFFFFFFFFFFFFFF)
   → focused = enemy with that handle
2. CSFeManImp boss-bar slot 0 has a valid handle → focused = enemy resolved
   from that handle
3. In `qualification` mode: focused = the nearest enemy to player.
4. Otherwise (no boss bars, no lock-on): no focused enemy this sample;
   nearest hostile enemy gets top-tier instead.

Other top-tier (up to 7 more): boss-bar slots 1-2 if valid, then nearest
roster enemies by distance to player.

### Sample sizing under various conditions

- Solo player, no enemies, idle at Grace: ~512 B/sample × 90 Hz = 46 KB/s.
- 1 focused boss + 0 other: 17 KB × 90 Hz + 512 B × 90 Hz ≈ 1.6 MB/s.
- 1 focused boss + 7 mobs nearby: 1.6 MB/s focused + 7 × 17 KB × 10 Hz =
  ~1.2 MB/s top-tier + Tier 1+2 + lesser ≈ **3 MB/s peak**.
- 8 mobs (no focused): same as previous minus the focused stream ≈ 1.4 MB/s.

**1 hour real session estimate:** 5-10 GB on disk. Comfortable on NVMe.

## Change-delta logging

Per Codex: delta encoding moves OFF the game thread.

### Producer (game thread, in detour)

1. Acquire next free buffer index from SPSC free-pool (atomic CAS).
   If no free buffer: increment `dropped_samples_no_buffer` counter, abort
   sample (Tier 1+2 also dropped this frame).
2. Fill the buffer:
   - Tier 1 fields (always)
   - Tier 2 fields (always)
   - Tier 3 raw byte regions (per mode + rate)
3. Stamp buffer with `frame, ts_ms_rel, mode_at_capture`.
4. Push buffer index to filled-queue (atomic).
5. Return.

The detour does ZERO byte-comparison, ZERO compression, ZERO format work.
Just `SafeReadBytes` into preallocated buffer + memcpy + index push.

### Consumer (worker thread)

1. Pop filled buffer index.
2. For each region: compare against last-known-state for that
   `(enemy_handle, region_id)` pair.
3. Write either `[full_snapshot]` (first time, or when child-pointer target
   changed, or every Nth sample as keyframe) or `[delta]` record.
4. Push buffer index back to free-pool.

If the worker falls behind (free-pool empties), the detour drops samples.
This is logged via atomic counter and dumped at session end.

### Buffer pool sizing (rev4)

**Sample-buffer model.** Each buffer is a fixed `MAX_SAMPLE_BYTES = 256 KB`,
sized to hold one whole sample (Tier 1 + Tier 2 + all enabled Tier 3
regions for all tracked enemies). If a sample would exceed 256 KB, the
producer truncates lesser-tier regions first (preserving Tier 1+2 + focused
enemy broad-sweep) and sets a `truncated:bool` flag in the buffer header.

Pool size: **256 buffers × 256 KB = 64 MB** (revision 4, was 32 MB at 10 Hz).
At 90 Hz focused capture, 256 buffers gives `256 / 90 ≈ 2.8s` of stall
tolerance (was 12.8s at 10 Hz). Acceptable on NVMe — disk hiccups longer
than 2.8s are rare and would also affect game rendering.

Allocated with `_aligned_malloc(256*1024, 64)` at init; freed at session end.

Why 256 KB per buffer: real worst-case sample is roughly `1 focused × 17 KB
+ 7 top-tier × 17 KB (when their decimation lands on this tick) + 8 lesser
× 14 KB (when their decimation lands) + headers + Tier 1+2 ≈ 270 KB`. Most
samples will be much smaller because not all decimation periods coincide
on every tick. Truncation flag handles edge cases.

### Adaptive sampling (revision 3)

Two-tier degradation: worker-driven (slow) and producer-driven (emergency).

#### Worker-driven (slow, normal case)

Worker monitors `dropped_samples_no_buffer` + `budget_skips` over a rolling
5-second window. If `(drops + skips) / attempts > 5%`:

- Step 1: reduce broad-sweep rate from 10 Hz to 5 Hz on top tier.
- Step 2: reduce top-tier enemy cap from 8 to 4.
- Step 3: reduce broad-sweep rate to 2 Hz.
- Recovery: if drops < 1% for 30s, step UP one level.

Each step writes atomic mode flags read by the producer.

#### Producer-driven (emergency, worker-stalled case)

The producer also watches the free-pool size atomically each frame:

- If `free_pool_size < 4` AND has been below 4 for 200ms:
  → producer drops broad-sweep regions for the NEXT sample (regardless of
  worker-side mode flags).
- Reset when `free_pool_size > 16`.

This guards against the case where the worker stalls (disk hiccup, etc.)
and worker-side adaptation is too slow to react. Logged via separate atomic
counter `producer_emergency_drops`.

Tier 1+2 always best-effort. If even Tier 1+2 can't fit (i.e., NO free
buffer at all), the sample is fully dropped, counter incremented.

## Hook safety

Same model as v5f.

- `UpdateUIBarStructs` hooked via MinHook.
- All reads SEH-wrapped via `SafeRead<T>` and new `SafeReadBytes(addr, len, dst)`.
- `LooksLikeUserPtrFast` (no syscalls) before any deref. NEVER VirtualQuery
  on the hot path (worker thread can use it for init-time validation).
- Module pinned via `GET_MODULE_HANDLE_EX_FLAG_PIN`.
- Hook stays installed until process exit (loader-lock-safe teardown).
- PostureBarMod compatibility: fail loudly with diagnostic if sig-scan
  returns 0 hits. Josh's mod stack does NOT include PostureBarMod
  (verified via `/mnt/station-mods/`), so this is not blocking.

## Time budget (rev4)

**Hard ceiling: 3 ms per detour call.** Soft target: 2 ms.

Producer ordering (in detour, every hook tick):

1. Capture Tier 1 (always; cheap, ~50 µs).
2. Capture focused enemy Tier 2 + Tier 3 (always; ~150 µs).
3. Check elapsed via `QueryPerformanceCounter`. If > 2 ms: skip non-focused
   Tier 3, jump to step 5.
4. Capture other top-tier Tier 2 (always) + Tier 3 (when decimation lands).
5. Check elapsed. If approaching 3 ms: abort lesser-tier; log `budget_skip`.
6. Capture lesser-tier when budget remains.
7. Push buffer index to filled-queue and chain to original.

At 90 Hz hook rate, even worst-case 3 ms is 27% of one core dedicated to
the probe. For an internal discovery tool that's acceptable. Focused-enemy
capture is the data we cannot lose — soft 2 ms target guarantees focused
capture even when broader work would push us over.

QPC overhead is ~100 ns per query, negligible.

## Decimation phase staggering (rev4)

10 Hz and 2 Hz tiers use decimation counters: emit Tier 3 every Nth tick
(N=9 for 10 Hz at 90 Hz hook rate; N=45 for 2 Hz). To prevent all enemies
landing on the same tick (which would maximize per-sample bytes and trigger
truncation):

- Each enemy is assigned a phase offset at first observation:
  `phase = hash(enemy_handle) % N`
- Enemy emits Tier 3 when `(tick_count + phase) % N == 0`.

This spreads broad-sweep work evenly across hook ticks. Average per-tick
load stays the same; peak per-tick load is much lower.

## Init order (revision 3)

1. DllMain attach: pin module, spawn worker thread, RETURN. Detour NOT
   installed yet.
2. Worker thread, in this exact order:
   a. **Boot-log fallback**: open `<DLL_DIR>/parry-tell-probe.boot.log` for
      append. All pre-config-load messages go here. (Once config is loaded
      successfully, subsequent diagnostics go to `<log_dir>/<session>.log.txt`.)
   b. **Load config file** from `<DLL_DIR>/parry-tell-probe.ini`. If
      missing/invalid: log error to boot.log, sleep forever (no hook).
   c. **Validate config**: `mode` is one of `smoke|qualification|discovery`,
      `log_dir` exists and is writable, numeric values in valid ranges.
      Fail closed otherwise.
   d. **Open session log files** at `<log_dir>/<session_name>-<ts>.{csv,bin,log.txt}`.
      From here, diagnostics go to `.log.txt` not boot.log.
   e. **Check ER FileVersion**. Fail closed if not 2.6.1.0.
   f. **Sig-scan** WCM, GetChrInsFromHandle, UpdateUIBarStructs, CSFeManImp.
      Fail closed if any signature finds != 1 unique hit.
   g. **Validate enemy roster** via the 7-check quarantine (§roster). If
      validation fails, set `roster_enabled=false` (fall back to player +
      boss-bars). NOT a fail-closed condition.
   h. **Allocate buffer pool** (256 × 256 KB = 64 MB; rev4).
   i. **Write session manifest** as first record in `.bin`. Manifest now
      has all the data it needs (FileVersion, sig-scan results, roster
      status, config dump).
   j. **Install hook.** Detour can now fire (but `g_armed=false` by default,
      so it chains to original without sampling).
   k. **Spawn F11 watcher** (toggles `armed`).
   l. **Enter steady-state loop**: drain filled-queue, do delta encoding,
      flush periodically, monitor adaptive-sampling stepdowns.

## Enemy roster quarantine (Codex's 7 checks)

Per Codex revision: `WCM + 0x1F1B8/+0x1F1C0` are PROVISIONAL until
validated at init by all 7 checks.

```
1. read begin = *(WCM + 0x1F1B8) as ChrIns**
   read end   = *(WCM + 0x1F1C0) as ChrIns**
2. begin <= end
3. (end - begin) % 8 == 0   (8-byte aligned span)
4. count = (end - begin) / 8
   count >= 0 && count < 2048
5. for each candidate ptr in [begin, end):
   a. *ptr is LooksLikeUserPtrFast
   b. (*ptr + 0x8) yields a u64 that is nonzero AND not UINT64_MAX
   c. GetChrInsFromHandle(wcm, &(*ptr + 0x8)) returns either *ptr or
      another canonical pointer (not garbage)
6. for at least one candidate:
   a. (*ptr + 0x190) is LooksLikeUserPtrFast
   b. (*(*ptr + 0x190) + 0x18) is LooksLikeUserPtrFast (TimeAct module ptr)
7. (deferred — runs in qualification mode against a known boss):
   At least one boss-bar enemy's ChrIns appears in the roster span at
   least once during the first 30 sec of capture. (This is a runtime
   confirmation, not an init-time check.)
```

If checks 1-6 pass at init, `roster_enabled = true`.
Check 7 is a runtime confirmation; if it fails after 30 sec of
qualification mode, log warning and reduce confidence in roster data.

## TimeAct child-pointer follow (stricter per Codex)

Inside the captured `time_act_module` first 2KB, scan for pointer-shaped
qwords:

- Source offset must be 8-byte aligned (offsets `0x000, 0x008, 0x010, ...`).
- Target value must be 8-byte aligned (`*ptr & 0x7 == 0`).
- Target value must pass `LooksLikeUserPtrFast`.
- Cap: first 8 valid pointers found (was 16 in revision 1).
- For each, capture target's first 0x100 bytes via `SafeReadBytes`.
- Record format: `(source_offset_in_time_act:u16, target_addr_abs:u64, payload:0x100 bytes)`.
- When `target_addr_abs` changes for a given `source_offset_in_time_act`,
  next sample is forced to be a full snapshot (not a delta) so we don't
  delta-encode against an unrelated child.

## Animation-time monotonicity validation (smoke-test gate, revision 3)

In `smoke` mode (60 sec test), the worker tracks `player_anim_time_candidates`
each frame. The smoke instructions tell Josh to perform a few deliberate
actions during the 60 sec so we capture animations longer than locomotion
loops:

```
Smoke test gameplay instructions (60 sec at a Grace, after pressing F11):
  - Walk in circles for 10 sec (locomotion, short loops)
  - One light attack (short anim, ~0.5-1.0s)
  - One heavy attack (longer anim, ~1.5-2.5s)
  - One gesture (very long anim, ~3-5s)
  - One item use (medium anim)
  - One roll (~0.6s)
  - One sprint (variable)
  - Walk in circles 10 more sec
```

After session end, the worker writes `.calibration.txt`:

```
animation-time candidate analysis (smoke run):
  +0x20: monotonic_segments=14 max_segment_dur=2.3s f32_in_range=true
                              rewinds_on_anim_id_change=true
  +0x24: monotonic_segments=87 max_segment_dur=4.8s f32_in_range=true
                              rewinds_on_anim_id_change=true              ← winner
  +0x28: monotonic_segments=6  max_segment_dur=0.4s f32_in_range=false   (negative values)
  +0x2C: monotonic_segments=2  max_segment_dur=0.1s f32_in_range=false   (NaN)
```

Smoke gate (revision 3, relaxed per Codex):
- f32 finite + in plausible range (0..600s)
- positive monotonic during stable anim ID
- rewinds/resets when anim ID changes
- `max_segment_dur >= 0.3s` (was 1s)
- `monotonic_segments >= 3` (was 5)

If no candidate passes the gate, smoke test FAILS — we can't trust the
anim-time field even for qualification. Re-research before continuing.

The qualification run later proves the same field works on enemy attacks
and matches database timestamps.

## Database / oracle qualification

Per Codex blocker 1: cannot trust the database join until proven.

### Qualification protocol

1. Josh runs the probe in `qualification` mode (config: `mode = qualification`).
2. Josh fights ONE parry-eligible enemy (NOT pre-named in the spec; the
   analysis identifies which `cXXXX` based on captured fields). Recommended
   target: a Banished Knight at Stormveil entrance — confirmed parry-eligible
   per general knowledge of the game, common, easy to reset, lots of
   parryable attacks per encounter. The actual `cXXXX` for Banished Knight
   is identified by the qualification analysis itself, NOT asserted up
   front.
3. Josh saves the .csv + .bin to a known dir on the separate NVMe.
4. Josh confirms session is done.
5. Claude runs the analysis tool (`tools/qualify_oracle.py`) over the
   capture:
   - For each Tier-2 row, list captured `field_at_0xNN` values.
   - Cross-reference with the set of cXXXX in `parry_data.json` (281 chars
     have data, 107 with parry windows).
   - Find which `field_at_0xNN` consistently equals one specific cXXXX
     value across all rows for the same enemy ChrIns. THAT field is the
     character-ID join key. THAT value identifies which character we were
     fighting.
   - Cross-reference observed `enemy_anim_id` with parry windows in the
     database for the identified cXXXX. Compute predicted parry-window
     timestamps.
   - Verify those timestamps match observed `enemy_anim_time_candidates`
     within tolerance (±1 focused sample period — at 90 Hz, ±11 ms).
   - If all checks pass: emit `qualification PASSED`, output which
     `field_at_0xNN` is the character-ID key, which TimeAct offset is the
     monotonic anim-time, and which cXXXX the captured enemy was.
   - If fails: emit `qualification FAILED`, dump diagnostic (which check
     failed, what observed vs expected).

### What "passed" means concretely

```
QUALIFICATION REPORT — stormveil-knight 2026-05-09 14:32

Join key: field_at_0x064 (consistent value 4380 across 1342 rows for
                          enemy ChrIns 0x...; 4380 -> "c4380" in DB)
Identified character: c4380 (53 parry windows in DB)

Anim time field: TimeAct + 0x24 (monotonic during animations, range 0..6s,
                                 87 monotonic segments observed)
Anim ID field: TimeAct + 0xD0 (matches database keys)

Enemy attacks observed: 23
Database lookups attempted: 23
Database lookups succeeded: 21 (91%)
  2 lookups failed: anim_id 3457 not in database (known: enemy occasionally
  uses a non-parryable variant we filter out)

Predicted parry windows: 19
Confirmed parry windows (video review): 19/19 within ±1 focused sample period (~11 ms at 90 Hz)
False parry-window predictions: 0
Missed parry windows: 0

Verdict: PASSED. Real discovery session OK to run.
```

### What "failed" means concretely

Examples:
- No `field_at_0xNN` consistently matches any character ID in database.
  → Probe captures wrong byte for character ID. Add candidates and retry.
- Multiple anim_time candidates fail monotonicity. → Probe walks wrong
  TimeAct path. Re-research.
- Predicted windows don't match video. → Database is wrong, OR our
  anim_time interpretation is wrong, OR the join key is wrong.

In any failure case, do NOT run the discovery session until qualification
passes.

## Session manifest (Codex blocker 8)

First record in every `.bin` file is a manifest:

```
schema_version:        1
build_hash:            (sha256 of probe.cpp + probe.vcxproj at build time)
build_date:            (compile-time __DATE__ __TIME__)
er_file_version:       2.6.1.0
er_module_base:        0x00007FF7E5B10000  (capture session value; can vary across launches)
config_path:           D:\parry-tell-logs\config.ini
config_dump:           (full config file contents inline)
sig_scan_results:
  wcm_ptr_addr:        0x...
  get_chr_ins_fn:      0x...
  update_ui_bar_fn:    0x...
  cs_fe_man_imp:       0x...
roster_enabled:        true | false
roster_validation:
  check_1: pass | fail (begin <= end)
  check_2: pass | fail (8-byte aligned)
  ...
mode:                  smoke | qualification | discovery
session_start_ts_ms:   (steady_clock value at start)
```

At session end, the worker writes a session-end manifest with drop counters.

## Region-relative offsets (revision 3)

Each Tier 3 region records its OWN region-local base address plus the
logical chain that got us there. Format per Tier 3 record:

```
region_id:           u8    (0..5; see table)
region_base_abs:     u64   (absolute address of this region's base)
source_chain:        u32   (packed identifier of how we walked here)
payload_offset:      u16   (offset INTO this region)
payload_len:         u16   (bytes captured)
payload:             ...   (raw bytes OR delta-encoded changes)
```

Region IDs:

| ID | Region | Base = | Source chain |
|---|---|---|---|
| 0 | `chr_ins_root` | `enemy_chr_ins_abs` | direct |
| 1 | `module_bag` | `*(chr_ins + 0x190)` | chrIns→+0x190 |
| 2 | `time_act_module` | `*(*(chr_ins + 0x190) + 0x18)` | chrIns→+0x190→+0x18 |
| 3 | `time_act_focus` | same as `time_act_module` | (same; just different range) |
| 4 | `time_act_child` | varies (target of a child pointer) | logged per record |
| 5 | `ai_struct` | `*(*(chr_ins + 0x580) + 0xC0)` | chrIns→+0x580→+0xC0 |

For region 4 (TimeAct child pointers), an additional field is included:
```
child_source_offset_in_time_act: u16   (where the pointer was found in TimeAct)
```

Why this matters: the absolute addresses change across sessions (and even
across allocations within a session as enemies despawn/respawn). The
`(region_id, payload_offset)` pair is stable across all sessions for the
same logical byte. The analysis tool correlates by region+offset, not by
absolute address.

## Logging output

### Files

In `<config.log_dir>/`:

1. `<session_name>-<timestamp>.csv` — Tier 1 + Tier 2 fields, one row per
   sample. Pandas-friendly. ~50-100 MB/hour.
2. `<session_name>-<timestamp>.bin` — Tier 3 records (snapshots, deltas,
   manifest). ~5-10 GB / hour at the rev4 sampling rates.
3. `<session_name>-<timestamp>.log.txt` — diagnostics, init banners, errors.
   Small.
4. `<session_name>-<timestamp>.calibration.txt` — written at end of smoke
   mode only. Animation-time candidate report.

### File rotation

If `.bin` exceeds 2 GB, start `.bin.001`, `.bin.002`, etc.

## Config file

`<DLL_DIR>/parry-tell-probe.ini`:

```ini
[output]
log_dir = D:\parry-tell-logs\
session_name = stormveil-mob-test-1

[capture]
; mode is REQUIRED. one of: smoke | qualification | discovery
mode = discovery

; sample rate for top-tier enemies (Hz). Default 10. Reduced
; adaptively under backpressure.
sample_rate_hz = 10

; max enemies tracked per sample (Tier 1+2 + broad-sweep). Default 16
; (8 top-tier broad-sweep + 8 lesser).
max_enemies_tracked = 16

; max top-tier enemies (broad-sweep at full sample_rate_hz). Default 8.
top_tier_enemies = 8

; lesser-tier broad-sweep rate (Hz). Default 2.
lesser_tier_rate_hz = 2

; per-sample game-thread time budget (ms). Default 3.
budget_ms_per_sample = 3.0

[hotkeys]
; F11 toggles armed/disarmed during a session. Detour is installed
; either way; armed gates whether samples are emitted.
arm_toggle = F11

[diagnostics]
; verbose logging in .log.txt
verbose = true
```

Missing config = fail closed. Invalid `mode` = fail closed. Unknown keys
ignored with warning.

## Build target

- Visual Studio 2022, v143 toolset, x64 Release.
- Statically linked CRT (`/MT`).
- Single source: `probe/probe.cpp` (~1500-1800 lines expected for v6).
- MinHook (vendored, BSD-2). No new third-party deps.
- No JSON dependency (DLL doesn't load `parry_data.json`).
- Build hash: SHA256 of `probe.cpp + probe.vcxproj` baked into binary at
  compile time via a generated header.

## Test plan

In strict order:

1. **Smoke test (60 sec).** Mode = `smoke`. Per the deliberate-action
   script in §smoke-gate above (walk + light attack + heavy attack +
   gesture + item use + roll + sprint). Expected: hook fires, log files
   appear, no crashes, calibration report identifies a monotonic anim-time
   candidate at TimeAct +0x20/+0x24/+0x28/+0x2C, no broad-sweep records
   in `.bin` (smoke disables Tier 3).

2. **Qualification (2-3 min).** Mode = `qualification`. Fight ONE
   parry-eligible enemy whose `cXXXX` we'll identify from capture
   (recommended: a Banished Knight at Stormveil entrance — common,
   parry-eligible, easy to reset, the actual cXXXX is identified by the
   analyzer). Expected: qualification report PASSED, join key identified
   (which `field_at_0xNN` maps to character ID), anim-time field
   confirmed against video, database predictions match enemy attacks
   within tolerance.

3. **Real discovery session (~1 hr).** Mode = `discovery`. Stormveil + mobs
   route per session run-book. Expected: ~5-10 GB of capture data, low
   backpressure-drop counter, low budget-skip counter, all tracked
   enemies have diverse character IDs across multiple `cXXXX` values.

If smoke fails: stop, fix.
If qualification fails: stop, fix.
If discovery has high drops: ship the data we got, analyze the
backpressure pattern, decide whether retry is needed.

## Estimated implementation time (revised)

- Source extension v5f → v6: 6-8 hours (was 4-6; spec is bigger now).
- Codex final review of revised spec: 30 min (this turn).
- Codex review of source: 1 hour.
- Build + smoke test cycle on station: 30 min if clean, up to a few hours
  on iteration.
- Josh's smoke test: 5 min.
- Josh's qualification run: 5 min (fight + analysis).
- Josh's real session: 1 hour.
- Analysis tool (Python): 4-6 hours.

**Total to "Josh has run the real session and we have data":** ~14-18 hours
of my work over 2-3 calendar days.

## End of revision 4

All blockers from rev1, rev2, rev3 reviews addressed. Sending to Codex
for green-light confirmation. After Codex green-lights, write source.
