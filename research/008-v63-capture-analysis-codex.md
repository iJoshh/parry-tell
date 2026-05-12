# v6.2 Capture Analysis (q62.bin)

## 1. Executive summary: 3-row verdict table

| Question | Verdict | Confidence | Most important number |
|---|---|---|---|
| Q1 World position | **WORLD POS WINNER = phys** | high | enemy legacy-ish X range=0 vs enemy phys X range=205.958 |
| Q2 Enemy anim_id | **ENEMY ANIM_ID WINNER = path_a** | medium | path_a matches=1897/12467 |
| Q3 Lock-on handle | **LOCK-ON WINNER = new_6B0** | high | transitions new=20 vs legacy=0; on/off boundaries=14 |

Capture metadata:
- bin: `/tmp/v63-capture/q63.bin`
- schema_version: `2`, mode: `2`, samples: `12467`, focused rows: `12467`
- parse_errors: `0`
- manifest session_start_ms: `382839778`
- first_arm_ms: `380235456`
- last_arm_ms: `380482950`
- last_disarm_ms: `380587352`
- last arm window relative to session_start: `-2356828..-2252426` ms (duration `104.402` s)

## 2. Q1 world pos detailed

Player position (legacy `+0x6C0` vs phys-chain `+0x190→+0x68→+0x70`):
- legacy axis ranges: x `3.310939..43.087650` (Δ `39.776711`), y `102.649666..112.311668` (Δ `9.662003`), z `70.343697..116.649269` (Δ `46.305573`)
- phys axis ranges: x `-32.045780..32.039597` (Δ `64.085377`), y `-1.350332..8.311670` (Δ `9.662002`), z `-30.258224..29.613503` (Δ `59.871727`)
- legacy speed profile: median `3.604` m/s, p95 `4.545`, max `11.290`, `>12m/s` `0/12466`
- phys speed profile: median `3.604` m/s, p95 `4.546`, max `2102.803`, `>12m/s` `2/12466`
- legacy-minus-phys offsets (median): x `8.000000`, y `104.000000`, z `96.000000`
- legacy-minus-phys min/max: x `7.999998..40.000002`, y `103.999996..104.000004`, z `79.999996..104.000004`
- phys spike events (`speed > 50 m/s`): `2`
  - idx `4666`, ts `547535`: `2102.803` m/s, from `(32.039597, -1.105905, -30.258224)` to `(0.071485, -1.109092, -6.293621)`
  - idx `9252`, ts `601687`: `1788.238` m/s, from `(-32.04578, 6.766831, 20.719069)` to `(-0.076431, 6.770187, 4.68541)`

Focused enemy position comparison (`world_pos_phys` vs legacy-ish reinterpret `(0x068,0x06C,0x080)` as f32):
- focused phys ranges: x Δ `205.958282`, y Δ `188.384602`, z Δ `164.742996`
- focused legacy-ish ranges: x Δ `0`, y Δ `1.000000`, z Δ `205.958282`
- focused phys speed: median `0.316` m/s, p95 `4.059`, max `2105.263`
- focused legacy-ish speed: median `0.000` m/s, p95 `0.000`, max `1684.211`

Byte-level evidence (first focused row): region-6 `phys_module_body` at `+0x70` decodes directly to captured `world_pos_phys`; region-7 `+0x90` and region-2 queue fields shown in Q2.
- WORLD POS WINNER = **phys** (high)
- rationale: Enemy legacy-ish reinterpret is structurally dead (near-zero constant), while phys-chain vectors vary and decode cleanly from region-6 +0x70 bytes.

## 3. Q2 enemy anim_id detailed

Per-path stats across focused rows:
- path_a: non-zero `10010/12467` (80.292%), match(c4380 IDs) `1897/12467` (15.216%), distinct `33`, transitions `531`, top `0x00000000:2457, 0x003D14C0:1367, 0x003D0D4C:978`
- path_b: non-zero `12467/12467` (100.000%), match(c4380 IDs) `0/12467` (0.000%), distinct `4`, transitions `453`, top `0x003DB0F8:6814, 0xFFFFFFFF:2457, 0x000FEA38:2451`
- path_c: non-zero `12467/12467` (100.000%), match(c4380 IDs) `1854/12467` (14.871%), distinct `34`, transitions `535`, top `0xFFFFFFFF:2457, 0x003D14C0:1300, 0x003D0D4C:1084`
- read_idx distribution: 0x00000000:2465, 0x00000007:2023, 0x00000009:2011, 0x00000005:1989, 0x00000001:1973
- region presence counts (focused rows): R6 `12467`, R7 `12467`, R8 `37401`, R9 `199472`
- rows with any scan hit (u32/u16/u32-be): `12467/12467`
- u32-aligned hits total keys: `98` (top below)
  - u32le R9(bag+0x18)+0x1F0 = `703` count `6814` (54.6563% rows), example bytes `bf020000`
  - u32le R9(bag+0x58)+0x9C = `703` count `6814` (54.6563% rows), example bytes `bf020000`
  - u32le R9(bag+0x58)+0xA0 = `703` count `6814` (54.6563% rows), example bytes `bf020000`
  - u32le R9(bag+0x58)+0x18 = `30002` count `1217` (9.7618% rows), example bytes `32750000`
  - u32le R9(bag+0x58)+0x18C = `7010` count `1217` (9.7618% rows), example bytes `621b0000`
  - u32le R9(bag+0x50)+0xF0 = `3003` count `850` (6.8180% rows), example bytes `bb0b0000`
  - u32le R9(bag+0x18)+0x40 = `1003003` count `678` (5.4384% rows), example bytes `fb4d0f00`
  - u32le R9(bag+0x18)+0x20 = `1003003` count `677` (5.4303% rows), example bytes `fb4d0f00`
  - u32le R9(bag+0x50)+0xF0 = `3002` count `677` (5.4303% rows), example bytes `ba0b0000`
  - u32le R9(bag+0x8)+0x14 = `3003` count `671` (5.3822% rows), example bytes `bb0b0000`
- region7 `action_request_body +0x90` u32 distribution: 0xFFFFFFFF:2457, 0x003D14C0:1300, 0x003D0D4C:1084
- region2 `time_act_module +0x20` (queue[0].anim_id) i32 distribution: -1:2457, 4003008:1368, 4001100:975
- region2 write/read idx pairs (`+0xC0/+0xC4`): (0,0):2457, (9,7):2015, (1,9):2002
- byte example action_request: ts `493745`, handle `0x3C2A250017300000`, base `0x00007FF45FD49B00`, bytes@0x90 `14000000` => `0x00000014`
- byte example time_act: ts `493745`, handle `0x3C2A250017300000`, base `0x00007FF45E787D20`, bytes@0x20 `14000000` => `20`, bytes@0xC4 `05000000` => `5`
- ENEMY ANIM_ID WINNER = **path_a** (medium)
- rationale: Path A produced matching anim IDs with temporal transitions.

## 4. Q3 lock-on detailed

- distinct values: legacy `1`, new `3`, area `2`
- transition counts: legacy `0`, new `20`, area `14`
- legacy top values: 0x00007FF35329BB60:12467
- new top values: 0x3C2A250017300018:6814, 0xFFFFFFFFFFFFFFFF:3202, 0x3C2A25001730001C:2451
- area top values: 0x3C2A2500:9265, 0xFFFFFFFF:3202
- new on/off boundaries (non-sentinel vs sentinel): `14`; within-on target changes: `6`
- same-sample enemy-handle matches: legacy `0/12467`, new `9265/12467`
- module base(s): 0x00007FF620F60000:12467
- player vtable(s): 0x00007FF6239DCB40:12467
- player vtable RVA(s): 0x2A7CB40:12467
- focused enemy vtable(s) from region0+0x0: 0x00007FF6239A4010:12467
- focused enemy vtable RVA(s): 0x2A44010:12467
- vswarte cross-reference (from local research/006 findings): `PlayerIns` has `player_menu_ctrl` at `+0x6A0`, `unk6a8[8]`, then `locked_on_enemy` at `+0x6B0`; capture behavior matches this layout (`+0x6A0` pointer-like constant, `+0x6B0/+0x6B4` toggle-paired values).
- new-field transition events (first 10):
  - idx `91` ts `494922`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF35329BB60`
  - idx `115` ts `495229`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF35329BB60`
  - idx `236` ts `496716`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF35329BB60`
  - idx `329` ts `497900`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF35329BB60`
  - idx `456` ts `499542`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF35329BB60`
  - idx `1133` ts `507629`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF35329BB60`
  - idx `1454` ts `511030`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF35329BB60`
  - idx `3800` ts `536755`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF35329BB60`
  - idx `5172` ts `553879`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF35329BB60`
  - idx `5809` ts `561867`: 0x3C2A250017300018 -> 0x3C2A25001730001C, area `0x3C2A2500`, legacy `0x00007FF35329BB60`
- LOCK-ON WINNER = **new_6B0** (high)
- rationale: `+0x6B0` toggles between sentinel and handle-like values with expected cadence; `+0x6A0` is a single stable pointer-like constant.

## 5. Side findings (unexpected in data)

- Focus reason is always `3` (`qualification_nearest`) across all samples/focused rows; `in_lock_on` flags are never set.
- Focused handle includes a non-canonical value `0xFFFFFFFF17100000` for 1,781 rows; enemy vtable stays valid during these rows.
- Manifest says `sample_rate_hz=10`, but observed sample delta is ~12 ms (about 83 Hz), indicating the writer is capturing near-hook cadence.
- Capture spans only the second arm window: sample ts_rel `308145..412540` maps to absolute `380482952..380587347` ms (about 104.4 s), matching the `F11: armed` re-arm at `380482950`.

## 6. Recommended probe v6.3 patch

Keep:
- Keep `player_lock_on_target_handle_new` (`+0x6B0`) and `player_lock_on_target_area_new` (`+0x6B4`).
- Keep phys-chain world position (`module_bag +0x68 -> phys +0x70`) for both player and enemy paths.
- Keep player/enemy vtable capture at least for one more release to guard object-type drift.

Remove or demote:
- Remove legacy lock-on read `+0x6A0` from primary analytics path (retain debug-only one release if desired).
- Remove direct enemy anim candidates `time_act+0xD0`, path-B queue read, and path-C `action_request+0x90` as active signals (all dead in this capture).
- Treat current region-6/7/8 scan hits as noise unless a future capture yields stable high-coverage u32 hits at one offset.

Anim-ID next step:
- Add one wider/adjacent instrumentation pass for enemy animation (beyond current 512B windows), or hook-based oracle path, before promoting any enemy anim offset in qualification logic.

## 7. Q2 verdict — module bag slot identification

Method note:
- Region-9 bag-slot identity in this v6.3 wire image is in `child_source_offset_in_time_act` (u16); `source_chain` is constant `9` for region 9 records.
- All counts below are recomputed with corrected bag-slot extraction from q63 (`12467` focused rows).

vswarte cross-reference for `ChrInsModuleContainer` bag slots (from local research/006 source captures of `vswarte/eldenring-rs`):
- `+0x00 data`, `+0x08 action_flag`, `+0x10 behavior_script`, `+0x18 time_act`, `+0x20 resist`, `+0x28 behavior`, `+0x30 behavior_sync`, `+0x38 ai`, `+0x40 super_armor`, `+0x48 toughness`, `+0x50 talk`, `+0x58 event`, `+0x60 magic`, `+0x68 physics`.
- Captured rows also include valid pointers at `+0x70` and `+0x78` (not named in that struct snapshot; treat as unknown/extra slots).
- Region-9 cap is 16 members, so the sweep consistently stops at `+0x78`; known `ActionRequest` at `+0x80` is not present in region 9 (still captured separately as region 7).

Top stable `(bag_offset, body_offset, value)` hits:

| Rank | Encoding | Bag slot | Slot name | Body off | Value | Rows | Hit rate |
|---|---|---:|---|---:|---:|---:|---:|
| 1 | u32le | `0x18` | `time_act` | `0x1F0` | `703` | `6814` | `54.66%` |
| 2 | u32le | `0x58` | `event` | `0x9C` | `703` | `6814` | `54.66%` |
| 3 | u32le | `0x58` | `event` | `0xA0` | `703` | `6814` | `54.66%` |
| 4 | u16lo | `0x30` | `behavior_sync` | `0xF0` | `13360` | `2451` | `19.66%` |
| 5 | u16lo | `0x70` | `unknown` | `0xD0` | `13360` | `2451` | `19.66%` |
| 6 | u32le | `0x58` | `event` | `0x18` | `30002` | `1217` | `9.76%` |
| 7 | u32le | `0x58` | `event` | `0x18C` | `7010` | `1217` | `9.76%` |
| 8 | u32le | `0x50` | `talk` | `0xF0` | `3003` | `850` | `6.82%` |
| 9 | u32le | `0x18` | `time_act` | `0x40` | `1003003` | `678` | `5.44%` |
| 10 | u32le | `0x18` | `time_act` | `0xD0` | `1003003` | `670` | `5.37%` |

WINNER:
- `bag+0x18 (time_act) + 0xD0`, encoding `u32le`.
- This location equals probe path-A exactly (`12467/12467` row equality with `enemy.anim_id`), and its value transitions (`531`) track combat-state changes rather than lock-on sentinels.
- Target-set hit-rate at this location (any c4380 ID match) is `1897/12467 = 15.22%` => **medium confidence by hit-rate rubric** (`5–50%`), but structural confidence is high because of exact 1:1 equality with path-A.

Multi-encoding check:
- `u32be`: no matches.
- `u16` has a high baseline and many stable collisions; treat `u16` hits as supportive/noisy unless they also line up with a validated `u32` location.

Cross-check vs lock-on:
- Lock-on toggles (`14` boundaries) do not uniquely drive the winner field. The winner changes far more often than lock transitions and carries non-sentinel anim-like values in both lock-on and non-lock windows.
- Conclusion: this is animation-state data, not lock-on handle state.
