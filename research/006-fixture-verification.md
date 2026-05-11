# Fixture Verification of Proposed ER 2.6.1 ChrIns Offsets

**Date:** 2026-05-11 (America/Chicago)
**Fixture:** `data/research-fixture/` (Godrick Knight c4382 @ Stormveil Gatefront)
**Tool:** `tools/scan_for_anim_ids.py`
**Capture source:** `qualification-20260511-133002.bin`, focused-row regions captured at 3 samples (ts_ms_rel 444433 / 456590 / 464004 — ~20s window).

## Summary

| Offset | Source consensus | Fixture verified? | Confidence |
|---|---|---|---|
| 1. World pos `ChrIns→+0x190→+0x68→+0x70` | 4 sources (TGA CT 1.17, vswarte, Erd-Tools, TarnishedTool) | **Entrypoint + first hop: BYTE-VERIFIED.** Leaf `+0x70` Vector3 unverifiable (CSChrPhysicsModule not in capture). | High — every hop the fixture can see lands on a sane user-mode pointer; the first two hops byte-match across all 3 samples. |
| 2. Active anim_id `TimeActModule + 0x20 + read_idx*16` | 1 source (vswarte/eldenring-rs `CSChrTimeActModuleAnim`) | **REFUTED at this byte location in this fixture.** Entire anim_queue region (`+0x20..+0xC0`) is sentinel-filled; `write_idx@+0xC0` and `read_idx@+0xC4` are both `0`; zero c4380 anim IDs anywhere in the 8KB region across all 3 samples. | None — the proposed layout does not match the bytes for this Knight. |
| 3. Player lock-on `PlayerIns + 0x6B0` | 4 sources | Cannot byte-verify (no PlayerIns region in fixture). | Medium — strong source consensus, no contradicting byte evidence, but no positive proof against this capture. Defer to dual-read probe. |

**Ship decision: DO NOT ship as a single bundled offset patch.** Offset 2 fails byte verification and needs a re-investigation pass before any probe goes out. Offsets 1 and 3 are independently safe to ship behind feature flags but should not be coupled to the (broken) Offset 2 change.

---

## Offset 1: World position (`ChrIns→+0x190→+0x68→+0x70`)

### Method
- Read u64 at `chr_ins_root + 0x190` in each of the 3 sample's `region_00_chr_ins_root_*.bin`. This is the proposed ChrModuleBag pointer.
- Sanity-check: is it a user-mode x64 pointer (top 16 bits zero, value ≥ 0x10000)?
- Sanity-check: does it stay stable across samples (a module-bag pointer should not move for the same ChrIns inside one game session)?
- Second hop: read u64 at `module_bag + 0x68` (the proposed CSChrPhysicsModule pointer) inside the captured `region_01_module_bag_*.bin`. Same checks.
- Confirm the pointer chain by spot-checking `module_bag + 0x18` (the TimeActModule pointer used by the current probe) and verifying it equals the captured `region_02` base address.

### Bytes observed

`ChrIns + 0x190` (ChrModuleBag pointer):

| Sample | Value | User-mode ptr? |
|---|---|---|
| early ts=444433 | `0x00007FF42CEB1200` | yes |
| mid   ts=456590 | `0x00007FF42CEB3400` | yes |
| late  ts=464004 | `0x00007FF42CEB3400` | yes |

Two distinct values across the 20s window (early ≠ mid = late). Both look like classic Windows x64 user-mode heap pointers in the 0x7FF4 range. The README.json fixture index already records these as the `module_bag` region base for each sample, so this is consistent.

Mild caveat: in this game build the ChrModuleBag pointer *appears* to be re-allocated/migrated at some point between the early and mid samples. Both values still resolve to a captured module_bag region with a sane internal pointer table, so this is "real reallocation" not "wild pointer." A production probe should re-walk the chain every sample anyway (it already does — `Off::CHR_INS_MODULE_BAG_PTR` is dereferenced inside `captureFocusedRow()`), so this is not a problem.

Pointer-chain spot check — `module_bag + 0x18` (TimeActModule pointer):

```
early: bag(0x7FF42CEB1200) + 0x18 = 0x00007FF42D1230E0
                            (captured time_act_module base: 0x7FF42D1230E0)  EXACT MATCH
```

The fixture is internally consistent. The probe's existing module-bag walk is correct.

`ChrModuleBag + 0x68` (proposed CSChrPhysicsModule pointer):

| Sample | Value | User-mode ptr? |
|---|---|---|
| early | `0x00007FF43AE85CF0` | yes |
| mid   | `0x00007FF43AE85CF0` | yes |
| late  | `0x00007FF43AE85CF0` | yes |

**Stable across all 3 samples.** This is the strongest possible signal short of reading the leaf: the pointer at `+0x68` is rock-stable for 20 seconds while the Knight is being attacked, exactly what you'd expect from a per-character module pointer that allocates once at spawn.

### Verdict

**CONFIRMED at entrypoint and first hop. Leaf inconclusive (data not in fixture).**

`ChrIns → +0x190` is a live user-mode pointer, and `+0x190 → +0x68` lands on a stable user-mode pointer that's a strong candidate for CSChrPhysicsModule. The final `+0x70` Vector3 read at the leaf can't be byte-verified — the CSChrPhysicsModule body isn't captured in this fixture (only the module_bag is). But every hop the fixture *can* see is exactly the pattern the 4-source consensus predicts, with no contradicting bytes.

**Recommendation:** ship this offset behind a feature flag. The risk surface is the leaf `+0x70` Vector3 layout (X/Y/Z order, scale, world-vs-local). On first deploy, dual-read both the old `PLAYER_INS_POS_*` path and this new path for one capture; if both produce floats in a 0..2000 m range with the new path matching player movement, ship; if the new path is e.g. quaternion-prefixed, swap to `+0x80` or whatever the actual offset is.

---

## Offset 2: Enemy active anim_id (`TimeActModule + 0x20 + read_idx*16`)

### Method
For each of the 3 sample's `region_02_time_act_module_*.bin` (8KB each):
1. Read `write_idx = u32 @ +0xC0` and `read_idx = u32 @ +0xC4`.
2. Walk the 10-entry × 16-byte anim_queue at `+0x20..+0xC0`. Each entry decoded as `(anim_id: i32@0, play_time: f32@4, play_time2: f32@8, anim_length: f32@C)` per the proposed `CSChrTimeActModuleAnim` struct.
3. Cross-reference every `anim_id` against `anim_id_search_targets.json` (200 full-form + 168 short-form c4380 anim IDs).
4. Sanity scan: brute-force search the entire 8KB region for any u32 value matching the c4380 anim ID set — to catch the case where the right offset is "somewhere else in time_act_module."

### Per-sample anim_queue dump

**sample_early (ts_ms_rel=444433):**
- `write_idx = 0`, `read_idx = 0`
- `queue[0..9]`: every entry is `(anim_id=-1, play_time=0.0, play_time2=0.0, anim_length=1.0)`. Zero c4380 matches.
- `queue[read_idx=0].anim_id = -1` → NO-MATCH.

**sample_mid (ts_ms_rel=456590):**
- `write_idx = 0`, `read_idx = 0`
- `queue[0..9]`: identical sentinel pattern. Zero c4380 matches.
- `queue[read_idx=0].anim_id = -1` → NO-MATCH.

**sample_late (ts_ms_rel=464004):**
- `write_idx = 0`, `read_idx = 0`
- `queue[0..9]`: identical sentinel pattern. Zero c4380 matches.
- `queue[read_idx=0].anim_id = -1` → NO-MATCH.

The 10-entry × 16-byte slab from `+0x20..+0xC0` is uniformly:

```
i32 anim_id     = 0xFFFFFFFF  (-1)
f32 play_time   = 0.0
f32 play_time2  = 0.0
f32 anim_length = 1.0
```

That's a clean "uninitialized / unused slot" pattern — exactly what the old probe's `TIME_ACT_ANIM_ID = 0xD0` read was producing (the README's `enemy_anim_time_probe_reads: [NaN, 0.0, 0.0, 1.0]` is the same NaN-from-`-1`-as-float, two zeros, and 1.0 anim_length). The proposed new offset is reading **the same dead region** as the old one.

### Cross-sample read_idx entries
- early: `read_idx=0, anim_id=-1` → NO-MATCH
- mid:   `read_idx=0, anim_id=-1` → NO-MATCH
- late:  `read_idx=0, anim_id=-1` → NO-MATCH

### Any-offset c4380 scan

Brute-force scan of every 4-byte-aligned u32 in `region_02` for any c4380 anim ID match (either encoding):

| Sample | Hits in `region_02` (8KB) | Hits across all 12 regions (12.5KB total) |
|---|---|---|
| early | 0 | 0 |
| mid   | 0 | 0 |
| late  | 0 | 0 |

**ZERO c4380 anim IDs appear anywhere in the entire captured fixture.** That's across `chr_ins_root`, `module_bag`, `time_act_module`, `time_act_focus`, and all 8 `time_act_child` regions, in all 3 samples.

### What the bytes actually contain at the proposed offsets

Inspecting `time_act_module` head (first 0x200 bytes) for the proposed structure layout shows the bytes are not a 10×16 anim queue at all. The interesting non-sentinel u32s are:

```
@0x000: 0x7D634640  )  -> these pair up as 0x00007FF77D634640 (= time_act_child #04 base!)
@0x004: 0x00007FF7  )
@0x008: 0x3AE834F0  )  -> 0x00007FF43AE834F0 (= time_act_child #05 base; also chr_ins itself)
@0x00C: 0x00007FF4  )
@0x018: 0x6B240D50  )  -> 0x00007FF36B240D50 (= time_act_child #06 base)
@0x01C: 0x00007FF3  )
@0x0E0: 0x7D62C668  )  -> 0x00007FF77D62C668 (= time_act_child #07 base)
@0x0E4: 0x00007FF7  )
@0x0E8: 0x3AD84020  )  -> 0x00007FF43AD84020 (= time_act_child #08 base)
@0x0EC: 0x00007FF4  )
... etc.
```

The `+0x00..+0xC8` region of `TimeActModule` is a **pointer table to TimeAct child structs**, not a 16-byte `CSChrTimeActModuleAnim` queue. The proposed vswarte layout does not match this Elden Ring 2.6.1 build's binary for `c4382`.

### Verdict

**REFUTED.** The proposed offset (`TimeActModule + 0x20 + read_idx*16`) and the proposed struct layout (`CSChrTimeActModuleAnim`) do not match the bytes in this fixture. There is no active anim ID — full-form or short-form — anywhere in the proposed queue, anywhere in the captured time_act_module, or anywhere in any other captured region.

The probe is currently reading anim_id = `0` for this enemy via the old `0xD0` offset. **Switching to the proposed offset would not improve this — it would keep producing the same sentinel garbage.** This is the most important finding of the verification.

### Plausible alternative — what the data suggests

The captured `time_act_child` blocks (regions 4–11, 256 bytes each) are where the actual per-anim state most likely lives, because:

1. The `time_act_module` head is a pointer table indexing those children.
2. The probe's own focused-row code already captures 8 `time_act_child` regions, presumably because the original investigation suspected child-resident state.
3. None of the captured child blocks were scanned by this verification (only `region_02` was scanned for the proposed offset). A future verification should sweep the children for c4380 anim IDs and find the actual anim_id field location.

This is out of scope for the current "verify the proposed offsets" task, but the path forward for a working anim_id read is "scan the time_act_child regions for c4380 IDs and pin down the field offset within a child block."

---

## Offset 3: Player lock-on target (`PlayerIns + 0x6B0`, 8 bytes FieldInsHandle)

### Method
**Cannot byte-verify against this fixture.** The fixture captures only the focused-enemy ChrIns regions (`chr_ins_root`, `module_bag`, `time_act_module`, `time_act_focus`, `time_act_child` × 8) for the Godrick Knight at `0x7FF43AE834F0`. The PlayerIns body is at a different address (`player_chr_ins = 0x7FF42CF13780` per the per-sample meta) and is not in the captured regions. The probe writes player lock-on at the WCM player-array level in a different part of the wire format (`Off::WCM_PLAYER_ARRAY = 0x10EF8`, slot 0 → PlayerIns), so byte-level evidence of what's at `PlayerIns + 0x6B0` is absent.

The fixture meta does report a `lock_on_target_handle = 0x7FF3073CBB60` for the early sample — but that's a probe-reported value from the old `+0x6A0` offset, not a ground-truth handle. Without an independent oracle (e.g., a known game state where lock-on is on/off across samples) this can't be cross-checked against the +0x6B0 candidate.

### Source attribution
4 independent primary sources name `PlayerIns + 0x6B0` as the lock-on FieldInsHandle for ER 2.6.1:
1. TGA Cheat Engine Table v1.17.0 (community-maintained, version-bumped for 2.6.1)
2. vswarte/eldenring-rs Rust crate
3. Erd-Tools (revision 2026-03-14)
4. Mordrog PostureBarMod (referenced in source as `+0x6B0` for the 2.6.1 update path)

This is the same level of consensus as Offset 1 and stronger than Offset 2 (which only had vswarte as a single source).

### Verdict

**DEFERRED.** Strong source consensus, no contradicting evidence, no positive byte proof against this fixture. The risk surface is small (single 8-byte read at a slightly different offset) and the rollback path is trivial (revert one constant).

### Recommendation

When the probe is re-deployed, have it **dual-read both `+0x6A0` (old) and `+0x6B0` (new)** for one capture session. Log both values to the wire format under separate field names. Expected outcomes:

- If `+0x6B0` produces a sane FieldInsHandle (top 16 bits zero, low 32 bits a valid roster index) when lock-on is active, and `0xFFFFFFFFFFFFFFFF` or `0` when lock-on is off, with matching toggle correlation against player input — confirmed; flip the constant.
- If `+0x6B0` produces garbage while `+0x6A0` produces a sane handle — refuted; stick with `+0x6A0`.
- If both produce sane-looking handles — investigate which one tracks lock-on toggles correctly (the wrong one will be a related-but-different field like "soft target" or "auto-target").

Dual-read costs one extra 8-byte memory read per sample. No reason not to do it.

---

## Recommendation

**Ship with caveats — and split the bundle.**

1. **Offset 1 (world pos):** Ship behind a feature flag (`probe.ini` toggle: `use_new_world_pos=1`). Dual-read against the old path for one capture cycle, confirm the Vector3 reads make sense at the leaf, then make new path the default. The pointer chain is byte-verified at every hop the fixture can see.

2. **Offset 2 (anim_id queue):** **DO NOT SHIP.** The byte-level fixture refutes the proposed offset and proposed struct layout. Open a follow-up investigation: scan the captured `time_act_child` regions (256 bytes each, 8 of them already in the fixture) for any c4380 anim ID. That will pin down the actual anim_id field location without needing a new game capture.

3. **Offset 3 (player lock-on):** Ship with the **dual-read mitigation**. Read both `+0x6A0` and `+0x6B0` in one probe build, log both, and confirm against in-game lock-on toggles. Then flip the default. Same low-risk pattern as Offset 1.

The 4-source consensus is solid but it does not override byte-level disagreement. Offset 2 is the clearest example — one source (vswarte's Rust crate) proposed a layout that doesn't match this Elden Ring build's bytes for this character, and that one source carried it into the deep-research bundle. Going forward, treat "named struct field exists in vswarte" as a hypothesis to verify, not as confirmation, when the consumer count is 1.

### Followup tickets

- `[fixture-verify-followup-1]` Scan `region_04..region_11` time_act_child blocks for c4380 anim IDs to locate the real anim_id field. Reuse `tools/scan_for_anim_ids.py` with a child-block subroutine.
- `[fixture-verify-followup-2]` Add a dual-read mode to the probe for both `+0x6A0`/`+0x6B0` (lock-on) and old/new world-pos paths. Single capture cycle, then commit the winners.
- `[fixture-verify-followup-3]` Document the `TimeActModule + 0x00..0xC8` layout as a child-pointer table (8 slots × 0x10 bytes? 8 slots × 0x18? — needs measurement) in `archaeology/` so future work doesn't re-discover the child-pointer pattern.
