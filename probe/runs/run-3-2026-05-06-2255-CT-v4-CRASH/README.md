# Run 3 — probe v4 — 2026-05-06 ~22:55 CT — CRASH

**Probe version:** v4 (md5 of DLL: `342f22ce1ed2cb5f83021671989da620`, debug_id: `E9D6207EB1B741C38A034A6D295ABCC21`)
**Game:** Elden Ring 2.6.1.0, eldenring.exe base = `0x00007FF7048C0000`
**Test type:** Josh launched into open world, played briefly, game crashed at +280s (~4.7 min)
**Operator:** Josh (Windows, station)

## Files

- `STATION.log` — DebugView export, ~157KB
- `parry-tell-probe.csv` — probe output, 3.5MB, 776 lines
- `parry-tell-probe-wcm-dump.bin` — first 0x20000 bytes of WCM struct dumped at probe init (NEW v4 feature)

## Critical finding: probe is causing the crashes

**Empirical evidence (Josh, 2026-05-06 ~23:30 CT):**
- Normal ER baseline: 1-2 crashes per 5-hour session
- Tonight with probe loaded: 5 crashes in 73 minutes
- Since renaming `parry-tell-probe.dll` to `parry-tell-probe.dll.old` (probe NOT loaded): zero crashes

**Crash dump analysis** (7 dumps total, lived in `C:\Users\Josh\AppData\Local\CrashDumps\` on station):
- Two crash signatures recurred: `STATUS_HEAP_CORRUPTION` (3 dumps) and `FAST_FAIL_INVALID_REFERENCE_COUNT` (3 dumps)
- All crashes were in `ntdll.dll` / `KERNELBASE.dll` — no probe symbols on crashing thread
- One dump (18216, 22:55 v4 install moment) showed our worker thread sitting in a Sleep/Wait call with last-touched WCM addresses in registers

**Why the dump analysis was misleading:**
- `STATUS_HEAP_CORRUPTION` is detected by the next thread to call malloc/free, NOT the thread that wrote the bad value
- `FAST_FAIL_INVALID_REFERENCE_COUNT` similarly: refcount could have been corrupted by a stray write hours earlier
- "Probe not on crashing stack" only proves we weren't the immediate caller, not that we weren't the cause
- Field evidence (rename → no crashes) is dispositive

## What the probe was doing that's likely problematic

1. **WCM memory dump at startup** (NEW in v4) — 131072 bytes read in a tight loop right at game-init time when state is fragile
2. **Aggressive prio queue walk** — v4 changed `return std::nullopt` to `continue` on read failures, multiplying read volume per frame by 10-100x
3. **High-frequency CSV writes** — 958 event rows + ~340 comment lines in 4.7 minutes vs 6 ticks in 12 minutes for v3
4. **Reading garbage offsets** — `WCM + 0x1E508` returned valid-but-wrong values (likely overlapping with kernel-managed pages); SEH catches faults but not "valid read of corrupted-state memory"

## Data salvageable for future work

- 958 event rows (boss_target_field_changed, boss_animation_changed, boss_appeared, boss_disappeared, tick) confirm prio queue walk surfaces real activity when not exiting early
- WCM memory dump (131KB binary) is offline-analyzable for finding correct player slot offset
- 20 reads of player chain at chr=`0x00007FF3FA5AA5B0` returned `entity=0x00000000` consistently — either `0x1E8` is wrong for entity_id in 2.6.1 OR that pointer wasn't the player ChrIns
