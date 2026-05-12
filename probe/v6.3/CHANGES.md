# probe v6.3 — Research 007 follow-up

**Date:** 2026-05-11 (America/Chicago)
**Inputs:** research/006-SYNTHESIS.md, research/007-v62-capture-analysis-codex.md, /tmp/v62-capture/q62.bin (v6.2 capture)

## Why

The v6.2 capture (qualification-20260511-191334.bin, 8,773 focused rows of a confirmed c4382 Godrick Knight at Stormveil Gatefront) produced clean answers for two of the three offset questions:

- **Q1 World position:** phys-chain (`module_bag → +0x68 → +0x70`) tracks player movement coherently. Legacy `+0x6C0` also works but has chunk-boundary wrap discontinuities.
- **Q3 Lock-on target:** `+0x6B0` wins decisively (17 transitions vs 0 for legacy `+0x6A0`); player vtable RVA `0x02A7CB40` confirms PlayerIns subclass.
- **Q2 Enemy anim_id:** ALL THREE proposed paths (A=TimeAct+0xD0, B=anim_queue[read_idx], C=ActionRequest+0x90) are sentinels/zero in 100% of focused rows. The c4380 anim ID we expect for an actively-fighting Knight is **not anywhere in the captured wire format**.

v6.3 keeps the Q1+Q3 winners locked in AND widens the Q2 capture surface so the next capture can finally find where the anim_id lives.

## What

### Probe-side changes (probe/probe.cpp)

- `PROBE_VERSION_STR` = `"v6.3"`. `PROBE_SCHEMA_VERSION` stays at **2u** (only additive region IDs).
- New region ID **9 = `REGION_MODULE_BAG_MEMBER`**: wide-scan capture. Iterates `ChrModuleBag + [0..0x100]` at 8-byte stride, captures 512B body of every valid pointer. Cap 16 modules per focused enemy. No dedup against existing regions 1/2/6/7 — this is the canonical "what's in the module bag" sweep. `source_chain` offset tells the analyzer which bag slot each captured body came from.
- **Lock-on derivation rewired to `+0x6B0`.** Introduced `playerLockHandleEffective` = `playerLockHandleNew` if valid, else sentinel. Three analytics call sites switched (priority-pass roster scan, isLock detection, focus-selection lock-on branch).  Side effect: `in_lock_on` flag and `focus_reason=1 (FOCUS_LOCK_ON)` finally fire correctly. This also re-enables boss-bar gating that was silently broken in v6.2.
- The legacy `+0x6A0` read is RETAINED in wire format (probe still writes it, parser still reads it) so v6.3 captures are byte-comparable with v6.2 for verification. The value is just not used for analytics decisions.

### Parser-side changes (tools/probe_bin.py)

- `REGION_NAMES` map adds `9: "module_bag_member"`.
- No schema changes — v6.3 captures parse identically to v6.2 captures because the only additions are new region records (already schema-versioned at the region-record level, not the sample-header level).

## What v6.3 captures will show

Per focused enemy (c4382 Knight expected):
- Up to 16 module-bag member regions, one per valid pointer in `bag[0..0x100]`. Each region's `source_chain` says which bag offset that module came from (0x18 = TimeAct, 0x68 = Phys, 0x80 = ActionRequest already known; the others are the targets of investigation).
- For each captured module body, the analyzer (`tools/analyze_v62_capture.py` extended in v6.3 to scan region 9 alongside regions 6/7/8) brute-force scans for any u32 matching the 244 c4380 anim IDs. A stable hit at (bag_offset, body_offset) across multiple focused rows = THE ANSWER.
- The deep-critic note from research/007: brute-force u32 scans against a 244-ID target set have a noise floor of <1 expected hit per 13M u32s. So even ONE consistent hit across 8K focused rows is unambiguous.

Per player:
- `in_lock_on` flag finally toggles correctly during lock-on cycles.
- `focus_reason` finally takes value 1 (FOCUS_LOCK_ON) when the player has a target. Boss-bar gating becomes functional again — if the player fights a boss (e.g. Margit), the boss-bar handle correlation will now work.

## What v6.4 will do

After analysis of the v6.3 capture:
- If a stable c4380 anim_id hit is found at (bag_offset, body_offset): v6.4 ships with `ENEMY_ANIM_ID` read at that path and removes the dead path-A/B/C captures.
- If no stable hit: the anim_id is encoded (XOR'd, packed, indirect). At that point we escalate to actual reverse-engineering — disassemble `CSChrTimeActModule::UpdateAnim` and similar functions in the binary. Out-of-scope for v6.3.

## File manifest

- `probe/probe.cpp` — modified (~80 line diff vs v6.2)
- `tools/probe_bin.py` — modified (2 line diff)
- `probe/v6.3/probe-v6.3.patch` — unified diff vs pre-v6.3 checkpoint
- `probe/v6.3/CHANGES.md` — this file

## Safety review

- No new memory writes — all v6.3 reads are SafeRead<T> through SafeRead + LooksLikeUserPtrFast guards.
- Region 9's iteration is identical in shape to existing Region 4/8 scans (8-byte aligned pointer table walk with cap); no new attack surface.
- Per-sample max payload growth: 16 modules × 512B = 8KB additional Tier 3 per focused enemy. Existing 256KB sample buffer absorbs this comfortably (was using ~30KB per sample in v6.2).
- Lock-on derivation change is conservative: prefers `+0x6B0` ONLY when its value is non-sentinel. Falls through to "no target" if `+0x6B0` reads garbage; never falls back to the dead `+0x6A0`.
