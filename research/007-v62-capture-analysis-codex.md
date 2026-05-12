# v6.2 Capture Analysis (q62.bin)

## 1. Executive summary: 3-row verdict table

| Question | Verdict | Confidence | Most important number |
|---|---|---|---|
| Q1 World position | **WORLD POS WINNER = phys** | high | enemy legacy-ish X range=0 vs enemy phys X range=103.594 |
| Q2 Enemy anim_id | **ENEMY ANIM_ID WINNER = NONE** | high | total u32 hits=22 over 8773 focused rows |
| Q3 Lock-on handle | **LOCK-ON WINNER = new_6B0** | high | transitions new=17 vs legacy=0; on/off boundaries=12 |

Capture metadata:
- bin: `/tmp/v62-capture/q62.bin`
- schema_version: `2`, mode: `2`, samples: `8773`, focused rows: `8773`
- parse_errors: `0`
- manifest session_start_ms: `380174807`
- first_arm_ms: `380235456`
- last_arm_ms: `380482950`
- last_disarm_ms: `380587352`
- last arm window relative to session_start: `308143..412545` ms (duration `104.402` s)

## 2. Q1 world pos detailed

Player position (legacy `+0x6C0` vs phys-chain `+0x190→+0x68→+0x70`):
- legacy axis ranges: x `14.056547..64.244850` (Δ `50.188303`), y `99.626129..110.606056` (Δ `10.979927`), z `81.111717..116.380852` (Δ `35.269135`)
- phys axis ranges: x `-32.088295..32.020519` (Δ `64.108814`), y `-4.373874..6.606057` (Δ `10.979931`), z `-32.050690..13.695330` (Δ `45.746019`)
- legacy speed profile: median `3.747` m/s, p95 `4.947`, max `11.721`, `>12m/s` `0/8772`
- phys speed profile: median `3.747` m/s, p95 `4.966`, max `1776.767`, `>12m/s` `3/8772`
- legacy-minus-phys offsets (median): x `32.000000`, y `104.000000`, z `96.000000`
- legacy-minus-phys min/max: x `31.999998..64.000004`, y `103.999996..104.000004`, z `87.999996..120.000004`
- phys spike events (`speed > 50 m/s`): `3`
  - idx `3962`, ts `353056`: `1597.939` m/s, from `(-4.983745, 1.714456, -32.05069)` to `(-4.991261, 1.710295, -0.091906)`
  - idx `5118`, ts `367067`: `1776.767` m/s, from `(32.020519, -4.351404, -2.952748)` to `(0.038723, -4.353221, -2.944829)`
  - idx `7638`, ts `398888`: `1568.209` m/s, from `(-32.088295, 1.786621, 9.784197)` to `(-0.139097, 1.797704, 1.797274)`

Focused enemy position comparison (`world_pos_phys` vs legacy-ish reinterpret `(0x068,0x06C,0x080)` as f32):
- focused phys ranges: x Δ `103.594002`, y Δ `18.506996`, z Δ `233.729996`
- focused legacy-ish ranges: x Δ `0`, y Δ `162.410248`, z Δ `103.594002`
- focused phys speed: median `0.000` m/s, p95 `0.000`, max `1777.778`
- focused legacy-ish speed: median `0.000` m/s, p95 `0.000`, max `1777.778`

Byte-level evidence (first focused row): region-6 `phys_module_body` at `+0x70` decodes directly to captured `world_pos_phys`; region-7 `+0x90` and region-2 queue fields shown in Q2.
- WORLD POS WINNER = **phys** (high)
- rationale: Enemy legacy-ish reinterpret is structurally dead (near-zero constant), while phys-chain vectors vary and decode cleanly from region-6 +0x70 bytes.

## 3. Q2 enemy anim_id detailed

Per-path stats across focused rows:
- path_a: non-zero `0/8773` (0.000%), match(c4380 IDs) `0/8773` (0.000%), distinct `1`, transitions `0`, top `0x00000000:8773`
- path_b: non-zero `8773/8773` (100.000%), match(c4380 IDs) `0/8773` (0.000%), distinct `1`, transitions `0`, top `0xFFFFFFFF:8773`
- path_c: non-zero `8773/8773` (100.000%), match(c4380 IDs) `0/8773` (0.000%), distinct `1`, transitions `0`, top `0xFFFFFFFF:8773`
- read_idx distribution: 0x00000000:8773
- region presence counts (focused rows): R6 `8773`, R7 `8773`, R8 `26319`
- rows with any scan hit (u32/u16/u32-be): `897/8773`
- u32-aligned hits total keys: `3` (top below)
  - u32le R8+0xEC = `3002` count `9` (0.1026% rows), example bytes `ba0b0000`
  - u32le R8+0x18C = `3010` count `9` (0.1026% rows), example bytes `c20b0000`
  - u32le R8+0x98 = `8190` count `4` (0.0456% rows), example bytes `fe1f0000`
- region7 `action_request_body +0x90` u32 distribution: 0xFFFFFFFF:8773
- region2 `time_act_module +0x20` (queue[0].anim_id) i32 distribution: -1:8773
- region2 write/read idx pairs (`+0xC0/+0xC4`): (0,0):8773
- byte example action_request: ts `308145`, handle `0x2001000014A0000F`, base `0x00007FF3FE0EC240`, bytes@0x90 `ffffffff` => `0xFFFFFFFF`
- byte example time_act: ts `308145`, handle `0x2001000014A0000F`, base `0x00007FF3FE4E3000`, bytes@0x20 `ffffffff` => `-1`, bytes@0xC4 `00000000` => `0`
- ENEMY ANIM_ID WINNER = **NONE** (high)
- rationale: All direct paths are static sentinels/zeros with 0 target-ID matches; scan hits are sparse collisions (mostly u16), not a stable anim field.

## 4. Q3 lock-on detailed

- distinct values: legacy `1`, new `5`, area `2`
- transition counts: legacy `0`, new `17`, area `12`
- legacy top values: 0x00007FF2D879BB60:8773
- new top values: 0xFFFFFFFFFFFFFFFF:4389, 0x3C2A250017300018:3726, 0x3C2A25001730001A:429, 0x3C2A25001730002B:200, 0x3C2A250017300023:29
- area top values: 0xFFFFFFFF:4389, 0x3C2A2500:4384
- new on/off boundaries (non-sentinel vs sentinel): `12`; within-on target changes: `5`
- same-sample enemy-handle matches: legacy `0/8773`, new `0/8773`
- module base(s): 0x00007FF6F85F0000:8773
- player vtable(s): 0x00007FF6FB06CB40:8773
- player vtable RVA(s): 0x2A7CB40:8773
- focused enemy vtable(s) from region0+0x0: 0x00007FF6FB034010:8773
- focused enemy vtable RVA(s): 0x2A44010:8773
- vswarte cross-reference (from local research/006 findings): `PlayerIns` has `player_menu_ctrl` at `+0x6A0`, `unk6a8[8]`, then `locked_on_enemy` at `+0x6B0`; capture behavior matches this layout (`+0x6A0` pointer-like constant, `+0x6B0/+0x6B4` toggle-paired values).
- new-field transition events (first 10):
  - idx `213` ts `310765`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF2D879BB60`
  - idx `313` ts `311880`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF2D879BB60`
  - idx `1129` ts `321273`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF2D879BB60`
  - idx `2336` ts `334532`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF2D879BB60`
  - idx `3125` ts `343810`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF2D879BB60`
  - idx `4575` ts `360057`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF2D879BB60`
  - idx `5265` ts `368908`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF2D879BB60`
  - idx `5907` ts `376814`: 0x3C2A250017300018 -> 0xFFFFFFFFFFFFFFFF, area `0xFFFFFFFF`, legacy `0x00007FF2D879BB60`
  - idx `6716` ts `387182`: 0xFFFFFFFFFFFFFFFF -> 0x3C2A250017300018, area `0x3C2A2500`, legacy `0x00007FF2D879BB60`
  - idx `6960` ts `390093`: 0x3C2A250017300018 -> 0x3C2A25001730001A, area `0x3C2A2500`, legacy `0x00007FF2D879BB60`
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
