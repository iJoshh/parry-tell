# parry-tell — HANDOFF

**Last updated:** 2026-05-08, mid-session compact. Phase 3 Step 0 (TAE
database) DONE. Phase 3 Step 1 (probe v6) — **spec locked at revision 4,
Codex green-lit, source NOT YET WRITTEN**. The next thing the post-compact
me should do is write `probe/probe.cpp` v6 source per the locked spec.

## Read this first (post-compact orientation)

You are Claude (Mae). Josh commissioned parry-tell, an Elden Ring + SotE
mod that gives audio + visual cues for parryable boss attacks during
Seamless Co-op as a guest. Read-only memory inspection, MIT licensed,
ships on GitHub. Josh delegates routing/coding decisions; you drive.

**The mod's product strategy** has converged on a layered ship plan
written in `PHASE3-PLAN.md`:
- Week 1: MVP audio-only via animation_id + animation_time + database lookup (path A)
- Mid-stream: discovery probe runs in the same DLL to hunt for a runtime
  parry-active flag (path B). If found, swap implementation in week 2.
- Week 3-4: L2 hue, L1 target filter, lock-on, INI config

**This session's main work** has been designing the discovery probe v6.
The spec went through 4 rounds of adversarial Codex review (each turn
caught real blockers I missed). Final spec is GREEN-LIT and ready to code.

## Where we are RIGHT NOW

### What's locked and committed

- ✅ **TAE database** — `data/parry_data.json` (30 MB), 6,738 parry windows
  across 107 characters. Frozen. Used as the post-session oracle for the
  probe.
- ✅ **chr/ raw data** — `data/raw/chr/` (6.9 GB, 119,712 files, 100% match
  with station source). Local re-runs of the parser take ~60 sec instead
  of 51 min over SMB. Gitignored.
- ✅ **v6 spec** — `probe/v6/PROBE-V6-SPEC.md` (856 lines, revision 4,
  Codex green-lit). All 13 issues from 4 review rounds addressed.
- ✅ **Build pipeline** — SSH service on station is UP (verified
  `SSH_OK`). MSBuild path: `C:\Program Files\Microsoft Visual Studio\18\
  Community\MSBuild\Current\Bin\MSBuild.exe`. SCP/SSH workflow already
  used and known-good for prior probe versions.
- ✅ **Conversation logs** at `research/conversations/001-005` — record
  of every Codex review turn with verbatim responses. Read these for
  context if you need to reconstruct WHY a design decision was made.

### What's NOT done

- ❌ **`probe/probe.cpp` v6 source** — not written. v5f source (865 lines)
  is still in tree but is the prior generation; v6 supersedes it. Don't
  delete v5f yet — it's reference. The build pipeline targets `probe.cpp`
  so v6 either replaces it or you bump vcxproj. Recommend replacing.
- ❌ **Smoke test on station** — depends on source written + compiled
- ❌ **Oracle qualification run** — 2-3 min Banished Knight test, depends
  on smoke passing
- ❌ **Real ~1 hr discovery session** — depends on qualification passing
- ❌ **Analysis tool (Python)** — `tools/qualify_oracle.py` and
  `tools/analyze_discovery.py`, ~4-6 hours of work, post-session

## Probe v6 — what the post-compact me needs to know

**The spec is at `probe/v6/PROBE-V6-SPEC.md` (856 lines, rev4 final).**
Read it cover to cover before writing source. Key elements:

### Architecture summary

- **Hook target:** `UpdateUIBarStructs` (per-frame on game thread) — same
  as v5f. Sig-scan via PE-section-filtered scanner. PostureBarMod
  conflict mitigated by failing loud if 0 hits found.
- **Three modes** (config-driven, fail-closed if invalid):
  - `smoke` — 60 sec, Tier 1+2 only, validates anim-time monotonicity
  - `qualification` — 2-3 min, Tier 1+2+3 on locked-on enemy only,
    proves the database join key
  - `discovery` — full ~1 hr session, three-tier sampling rates
- **Sampling rates** (rev4 critical fix — median parry window is 33.3 ms):
  - Focused enemy: hook-tick (~90 Hz)
  - Other top-tier: Tier 1+2 hook-tick, Tier 3 at 10 Hz
  - Lesser: Tier 1+2 at 10 Hz, Tier 3 at 2 Hz
- **Buffer pool:** 256 buffers × 256 KB = 64 MB. SPSC ring buffer model
  with detour as producer (memcpy-only, no compute), worker thread does
  delta encoding + disk writes.
- **Capture regions** (Tier 3): chr_ins_root (0..0x800), module_bag
  (first 0x200), time_act_module (0..0x2000), time_act_focus
  (+0xC0..+0xE0), time_act_child_pointers (8 strict-aligned children
  × 0x100), ai_struct (+0xE000..+0xF000)
- **Field offsets:** all of `+0x038, +0x060, +0x064, +0x068, +0x06C,
  +0x080, +0x1E8` are captured as RAW NEUTRAL `field_at_0xNN` — no
  semantic interpretation in the probe (sources disagree on what's
  what; analysis decides post-session)

### Init order (12 steps, exact)

1. DllMain attach: pin module, spawn worker, RETURN
2. Worker thread:
   - a. Boot-log fallback: `<DLL_DIR>/parry-tell-probe.boot.log`
   - b. Load config from `<DLL_DIR>/parry-tell-probe.ini` (fail closed)
   - c. Validate config (fail closed)
   - d. Open session log files
   - e. Check ER FileVersion 2.6.1.0 (fail closed)
   - f. Sig-scan WCM, GetChrInsFromHandle, UpdateUIBarStructs, CSFeManImp (fail closed)
   - g. Validate enemy roster (7 checks; fall back if fails — NOT closed)
   - h. Allocate 64 MB buffer pool
   - i. Write session manifest to `.bin`
   - j. Install hook (last step before runtime)
   - k. Spawn F11 watcher
   - l. Steady-state: drain queue, delta-encode, flush

### Region records (Tier 3) — region-relative

Each record: `(region_id:u8, region_base_abs:u64, source_chain:u32,
payload_offset:u16, payload_len:u16, payload:bytes)`. For region 4
(time_act_child), also `child_source_offset_in_time_act:u16`. Analysis
correlates by `(region_id, payload_offset)` across sessions, NOT
absolute address.

### CPU budget

3 ms hard ceiling, 2 ms soft target. Order: Tier 1 → focused → check
elapsed → other top-tier → check elapsed → lesser. Focused-first means
we never lose the data we care most about even when budget is tight.

### Decimation phase staggering

10 Hz (N=9 ticks at 90 Hz) and 2 Hz (N=45) tiers stagger via
`phase = hash(enemy_handle) % N`. Prevents all enemies from emitting
Tier 3 on the same hook tick (which would peg the 256 KB buffer
ceiling and trigger truncation).

### Adaptive sampling

Worker-driven (slow): `(drops + skips) / attempts > 5%` over 5s rolling
window → step 1 reduce 10 Hz to 5 Hz, step 2 cap 8→4, step 3 5→2 Hz.
Recovery if drops < 1% for 30s.

Producer-driven (emergency): `free_pool < 4 for 200ms` → drop broad
sweep next sample. Reset when `free_pool > 16`.

### Things you (post-compact me) WILL hit

1. **The +0x6C offset conflict:** PROBE-SPEC.md says "block_id at +0x6C",
   v5f says "teamType at +0x6C". They can't both be right. The spec
   solution is: **don't interpret**. Capture `field_at_0x06C` as raw u32,
   let the analysis tool figure out what it is. Don't try to be clever
   in source — match the spec literally.
2. **The +0x190 → +0x18 → TimeAct chain:** Codex's offsets research has
   this at HIGH confidence for boss `ChrIns*`. v5f doesn't walk this
   yet for enemies — only for player slot 0. v6 walks it for every
   tracked enemy.
3. **`CSFeManImp` sig-scan is NEW in v6.** v5f doesn't scan for it.
   The `bossHpBars[3]` array starts at `CSFeManImp + 0x5BF0`, slot
   size 0x20, handle at +0x8 each, sentinel UINT64_MAX. PostureBarMod
   has the sig pattern — borrow it. Source citations in
   `research/phase3-offsets-codex.md` section 5.
4. **Enemy roster enumeration via `WCM + 0x1F1B8/+0x1F1C0`** — also
   new in v6. The 7-check quarantine in the spec is mandatory before
   trusting it. If checks 1-6 pass, set `roster_enabled=true` and
   capture roster enemies. If any check fails, fall back to player +
   boss-bars only and log a warning.
5. **No JSON loading in the DLL.** The DLL does NOT parse
   `parry_data.json` at runtime. The qualification analysis is
   post-session in Python. The DLL just captures fields neutrally.

### Source size estimate

v5f is 865 lines. v6 will be ~1500-1800 lines. Major additions:
- INI config parser (~150 lines, hand-rolled or simpleini-headerlib)
- Buffer pool + SPSC queue (~200 lines)
- Worker thread delta encoder + disk writer (~250 lines)
- Tier 1/2/3 capture functions (~400 lines)
- Region records + serialization (~200 lines)
- Roster quarantine + boss-bar enumeration (~150 lines)
- Adaptive sampling + decimation staggering (~100 lines)
- Session manifest writer (~50 lines)
- Calibration report writer for smoke mode (~100 lines)

Plus cleanup of v5f's now-redundant single-slot probe logic.

### Build target (unchanged from v5f)

- VS 2022, v143 toolset, x64 Release, /MT
- vendored MinHook (BSD-2) at `probe/vendor/MinHook/`
- vcxproj path: `probe/probe.vcxproj` — should NOT need changes
- Output: `parry-tell-probe.dll` at `bin/Release/parry-tell-probe.dll`

### After source is written, sequence:

1. Self-review (read your own source as a critic)
2. Codex critic dispatch (`@codex` agent with the source path)
3. Address findings
4. SCP source to station: `scp -i ~/.ssh/station_key probe/probe.cpp claude@station:C:/Projects/elden-ring/probe/probe.cpp`
5. Trigger MSBuild via SSH (see `CLAUDE.md` for command line)
6. Read build output via `/mnt/station-projects/elden-ring/probe/bin/Release/`
7. Drop DLL into `/mnt/station-mods/` (the mods folder is RW-mounted)
8. Wait for Josh to confirm he's ready to test

## What's pushed to GitHub

`origin/main` HEAD: `97fecbc fix(parser): default to data/raw/chr; sentinel
run never clobbers canonical artifacts`. In sync with local `HEAD`.

Recent commits:
- `97fecbc` — parser fix (sentinel doesn't clobber canonical, default
  source updated to data/raw/chr)
- `1753f2c` — HANDOFF refresh after chr/ transfer completed
- `6e2da6f` — Phase 3 Step 0 (TAE database)
- `d43f0fb` — TAE parser source

NOT yet pushed (because not yet committed):
- `probe/v6/` directory (spec + future source)
- `research/conversations/001-005` (Codex review logs)

These should be committed in batch when the v6 source is ready.

## Noteworthy Codex review findings (across 4 turns)

Saved fully at `research/conversations/`:

1. **Turn 1 (001):** Codex agreed path B is worth pursuing. Pushed back
   on "single boolean" framing — most likely shape is a multi-word
   bitset inside the TimeAct module two pointer hops in. Recommended
   building MVP path A with discovery probe alongside as `#ifdef`.

2. **Turn 2 (002):** Codex flagged 10 issues in v6 spec rev1 including
   the database-join blocker (probe captures npc_param_id but database
   is keyed by cXXXX — not the same key) and the offset collision at
   +0x6C.

3. **Turn 3 (003):** Codex flagged 5 NEW issues introduced by
   over-correction in rev2: buffer math wrong (4.5 MB pool was per-
   enemy not per-sample), init order writes manifest before its data,
   region-relative offsets need region-LOCAL bases (not all chrIns-
   rooted), TimeAct range inconsistent in the spec, c4100 wasn't
   verified to be Banished Knight.

4. **Turn 4 (004):** Codex caught the killer architectural blocker —
   10 Hz sampling rate misses 50% of parry windows by Nyquist alone
   (median window = 33.3 ms = 1 frame at 30 fps engine).

5. **Turn 5 (005):** GREEN-LIGHT after addressing all rev3 issues +
   rate fix in rev4.

## Workflow gotchas (still relevant)

- **SMB perf is brutal.** All parser work should now use `data/raw/chr/`
  (local) not `/mnt/station-projects/...` (SMB). Parser default is
  updated.
- **`opencode` Bash truncation:** avoid `ps -ef --forest` and `pgrep -af`
  while Codex is running. Use `ps -p PID -o pid,etime,stat`.
- **opencode MCP timeout** is bumped to 30 min in
  `~/.config/opencode/opencode.json` line 124.
- **Station SSH service** is started by Josh manually at session start.
  Verify it's up before assuming. Currently UP as of compact time.

## Critical files (re-read for full context)

- `probe/v6/PROBE-V6-SPEC.md` — THE spec. 856 lines. Read it all.
- `research/conversations/001-005` — Codex review history. Read at
  least 005 (green-light) and 004 (rate blocker) to understand WHY
  the spec is shaped the way it is.
- `probe/probe.cpp` — v5f source (865 lines). v6 builds on these
  patterns: SafeRead<T>, LooksLikeUserPtrFast, sig-scan, MinHook
  installation, worker thread, F11 watcher, module pinning.
- `research/phase3-offsets-codex.md` — 5 offset chain citations,
  specifically section 5 (CSFeManImp boss-bar walk) which v6 needs
  and v5f didn't.
- `research/phase3-architecture-codex.md` — for D3D12 + audio context
  (relevant to the production mod, not the discovery probe directly).
- `CLAUDE.md` — project conventions including the SCP/MSBuild build
  pipeline.

## Mode-specific test plan

When source is built:

1. **Smoke (60 sec):** drop DLL with `mode = smoke` config. Josh runs
   the deliberate-action script (walk, light/heavy attack, gesture,
   item use, roll, sprint). Calibration report identifies which
   TimeAct offset is monotonic anim-time.

2. **Qualification (2-3 min):** `mode = qualification`. Josh fights
   ONE Banished Knight at Stormveil entrance. Analysis tool
   `tools/qualify_oracle.py` joins captured fields against
   `parry_data.json`, identifies the cXXXX → field_at_0xNN join key,
   confirms predicted parry windows match within ±11 ms.

3. **Discovery (~1 hr):** `mode = discovery`. Stormveil mob route +
   Roundtable + maybe Crucible Knight. Analysis tool
   `tools/analyze_discovery.py` finds the runtime parry-active flag
   (or hyperarmor flag) by correlating memory deltas with database-
   predicted windows.

## What this is NOT

- This is NOT the production mod. The production mod is a thinner DLL
  that uses whatever the discovery probe found.
- This DLL captures EVERYTHING (broad-sweep) safely. Production
  captures only the relevant offsets it needs.
- The 30 MB `parry_data.json` is the discovery oracle, not a runtime
  dependency for production.

## Confidence

- v6 spec is correct: HIGH (4 rounds of adversarial review)
- v6 source will compile first try: MEDIUM (always iteration on
  Windows-specific details)
- Smoke test passes: HIGH (we already validated v5f works at the
  hook layer)
- Qualification passes: MEDIUM-HIGH (we verified the database is
  parseable; uncertainty is in field-offset mapping which the test
  is designed to discover)
- Discovery finds the parry-active flag: MEDIUM (Codex's prior:
  75-85% it exists somewhere reachable; lower confidence we'll find
  it in 1 hour of capture)
- MVP audio-only ships: HIGH (path A as fallback is well-understood)

## Resume command for post-compact me

```
Read probe/v6/PROBE-V6-SPEC.md (entire spec), then write
probe/probe.cpp as v6 per spec (replace v5f's content). After source
is written:
1. Self-review pass
2. Dispatch @codex critic on the source
3. Address findings
4. Push to station, MSBuild, verify DLL produced
5. Stop and check with Josh before any test session

Spec is GREEN-LIT — no more spec iterations needed unless you find
a real implementation contradiction. If you do, surface to Josh
before changing the spec.

Josh's standing instruction: "Do it right. My arrival timeline isn't
a concern. Ever. Do it right."
```
