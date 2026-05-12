## 2026-05-11 — Probe v6.4 production; research-006/007/008 + three offset questions resolved

### Added
- `probe/v6.2/` — instrumentation build (schema v2). 48-byte Tier 1 player
  block (vtable, phys-chain world pos, `+0x6B0` lock-on candidate, `+0x6B4`
  target area); 40-byte+pad enemy header (anim_id path B/C candidates,
  action_request + phys_module absolute addresses). Three new region IDs:
  REGION_PHYS_MODULE (6), REGION_ACTION_REQUEST (7),
  REGION_TIME_ACT_CHILD_BODY (8). Codex deep-critic pre-deploy: caught and
  fixed CSV header drift (P1), region 4/8 overlap (P1), 40-vs-44-byte comment
  drift (P2).
- `probe/v6.3/` — module-bag-wide instrumentation. REGION_MODULE_BAG_MEMBER (9)
  wide-scans ChrModuleBag[0..0x100] at 8-byte stride, capturing 512B body of
  every valid pointer. Switched `in_lock_on` derivation from dead `+0x6A0` to
  `+0x6B0` via `playerLockHandleEffective` — fixed `focus_reason=3-always` bug
  AND silently-broken boss-bar gating. Codex deep-critic caught: analyzer
  scanned only regions 6/7/8 not 9 (P1), `source_chain` missing from hit-keys
  causing bag-slot collisions (P1).
- `probe/v6.4/` — production cleanup for tonight's multi-boss co-op session.
  Drops instrumentation regions 6/7/8/9 (purpose done; region IDs reserved in
  enum so v6.2/v6.3 captures stay parseable). Co-op safety: scans 8
  WCM_PLAYER_ARRAY slots (4 base + 4 Seamless Coop extension) and excludes all
  valid friendly `chr_ins` pointers from both priority-pass and fill-pass roster
  sweeps. Audible F11 feedback via `Beep()`: ARMED = 660 Hz × 2 × 100 ms (low
  double-tap); DISARMED = 1320 Hz × 1 × 400 ms (long high beep). All v6.2/v6.3
  wire-format additions retained. Deployed to `/mnt/station-mods/`.
- `tools/probe-status.ps1` — PowerShell tailer for the station Windows box.
  Watches the latest `.log.txt`, prints ARMED/DISARMED transitions with
  timestamps. Handles same-name file truncation and partial-line edge cases
  (both caught by Codex deep-critic before deploy). Pushed to
  `C:\Projects\elden-ring\probe-status.ps1`.
- `tools/archive_session.sh` — copies session `.bin` (and all rotated
  `.bin.NNN` shards), `.csv`, and `.log.txt` from SMB to
  `captures/sessions/YYYYMMDD/`. Path-traversal-safe (session name regex),
  atomic-rename via `.partial`. Codex caught: long sessions crossing 2 GB
  would silently miss rotated shards without the glob pattern.
- `tools/segment_by_f11.py` — parses `.bin` + `.log.txt` to produce per-F11-
  cycle segment manifests. Translates between log-side ms (probe-init epoch)
  and bin-side ms (session_start_ms epoch). Implicit close of arm-without-
  disarm at next-arm timestamp. Uses `probe_bin.read_session` to handle rotated
  bin shards. Codex caught: missing implicit-close would have eaten boss 2 data
  if Josh forgot to disarm between bosses.
- `tools/analyze_v62_capture.py` — research-007 analyzer for v6.2 captures;
  extended in v6.3 for region 9 + `source_chain` hit-key deduplication.
- `tools/scan_for_anim_ids.py` — research-006 brute-force byte scanner.
- `captures/.gitignore` — keeps raw `.bin` files out of git; manifests,
  `.log.txt`, and `segments.json` stay tracked.
- `research/006-SYNTHESIS.md` — cross-vendor consensus from vswarte/eldenring-
  rs, TGA Cheat Engine Table v1.17, Erd-Tools, TarnishedTool, and Mordrog
  PostureBarMod on three ER 2.6.1 ChrIns offset questions.
- `research/006-claude-deep-research.findings.jsonl`, `006-codex-research.md`,
  `006-fixture-verification.md` — research-006 raw artifacts.
- `research/007-v62-capture-analysis-codex.md` — v6.2 capture analysis:
  8,773 focused rows of c4382 Godrick Knight at Stormveil Gatefront. Q1 and Q3
  resolved; Q2 (enemy anim_id) was a dead end due to stationary-enemy sample.
- `research/008-v63-capture-analysis-codex.md` — v6.3 capture analysis:
  12,467 focused rows (~144 s). Q2 resolved: `TimeAct + 0xD0` (path A, the
  original v6.1.1 offset) produces 9,265 nonzero anim_id reads with 89
  transitions and clean anim_time monotonicity. v6.2 zero-reads were a
  stationary-enemy artifact.
- `probe/releases/parry-tell-probe-v6.{2,3,4}.dll` + `.tar.gz` — build
  artifacts committed for audit trail.
- `probe/v6.{2,3,4}/CHANGES.md` + `.patch` — per-version change records.
- `captures/sessions/20260511/` — v6.3 capture archived with `segments.json`.

### Changed
- `probe/probe.cpp` — iterated v6.1.1 → v6.2 → v6.3 → v6.4 (see above).
- `tools/probe_bin.py` — added schema-v2 parser support for v6.2 wire format;
  v6.4 region name labels.
- `HANDOFF.md` — rewritten with "TONIGHT'S CO-OP SESSION — operating manual"
  at top: Josh's gameplay flow, Claude-side flow per boss-done report,
  multi-boss-in-one-.bin explanation, audio feedback reference, and what-if
  notes.

### Resolved (research)
Three ER 2.6.1 ChrIns offset questions closed with HIGH confidence:

| Field | Path | Verdict |
|---|---|---|
| World position | `bag→+0x68→+0x70` (phys-chain) | v6.3 byte-verified |
| Enemy active anim_id | `TimeAct + 0xD0` (path A, original v6.1.1) | 9,265 nonzero reads, 89 transitions |
| Player lock-on target | `PlayerIns + 0x6B0` (FieldInsHandle u64) | 20 transitions vs 0 for `+0x6A0` |

**The probe was never the bug.** Research-006's false alarm was caused by a
stationary-enemy fixture sample in v6.2. v6.3 with an actively-fighting enemy
confirmed the original offsets are correct.

## 2026-05-08 — Probe v6 built, staged, and analysis pipeline written

### Added
- `probe/probe.cpp` rewritten v5f → v6 (~3,076 lines). New vs v5f: INI config
  parser with fail-closed validation; 64 MB SPSC ring buffer (256 × 256 KB)
  between detour and worker thread; worker drains queue and writes binary
  records + CSV summary + diagnostics; CSFeManImp sig-scan + boss-bar
  enumeration; WCM ChrInsByUpdatePrioBegin/End enemy roster behind 7-check
  init quarantine; TimeAct chain walk per enemy; ai_struct walk; three-tier
  sampling rates (focused ~90 Hz / top 10 Hz / lesser 2 Hz); decimation phase
  staggering via `hash(handle) % 90`; producer-side emergency drop when
  `free_pool < 4` for 200 ms; worker-side adaptive stepdown via 5 s rolling
  window of 1 s buckets; session manifest with full `config_dump` inlined;
  smoke calibration report with anim-time gate.
- `probe/v6/RUNBOOK.md` — step-by-step test session guide.
- `probe/v6/parry-tell-probe.ini.{smoke,qualification,discovery}` — three INI
  configs pointing at `C:\Projects\elden-ring\logs\`.
- `probe/v6/GAMEPLAY-{smoke,qualification,discovery}.txt` — plain-text
  gameplay scripts for phone reference.
- `probe/v6/swap-mode.bat` — Windows batch script for self-service INI swaps
  between smoke / qualification / discovery modes; whitelist-validated input.
- `probe/releases/probe-v6.tar.gz` — build artifact.
- `tools/probe_bin.py` — `.bin`/`.csv` reader library, wire-format symmetric
  with C++ writer.
- `tools/probe_status.py` — quick capture health report with top-line VERDICT.
- `tools/qualify_oracle.py` — full qualification analyzer: join key,
  anim-time field, predicted-vs-observed parry windows.
- `tools/analyze_discovery.py` — discovery scaffolding; ranks bytes by
  in-window vs out-of-window mutation rate.
- `tools/probe_diag.py` — aggregates `boot.log` + latest `.log.txt` for
  "DLL didn't load" troubleshooting.
- `tools/rebuild-and-stage.sh` — one-command rebuild cycle; syncs source +
  vendor, byte-compares staged vs built DLL.
- `tools/test_probe_bin.py` — synthetic `.bin` round-trip self-test (PASSES).
- `tools/test_qualify_oracle.py` — synthetic c2130 fight against 8 real DB
  parry windows (PASSES).

### Changed
- `HANDOFF.md` — rewritten to reflect v6 build complete + analysis pipeline
  ready; next action is Josh's smoke test.

### Deployed
- `Game\mods\parry-tell-probe.dll` replaced with v6 build; smoke INI loaded.
- `Game\mods\parry-tell-probe.dll.disabled` — v5f preserved as audit trail.
- `Game\mods\parry-tell-probe.csv.v5f-leftover` — old v5f CSV renamed.
- `C:\Projects\elden-ring\logs\` created and SMB-visible.
- `C:\Projects\elden-ring\probe\stage\` — DLL + INIs + scripts staged.

### Codex review (v6 source)
- Verdict: `block` with 2 blockers + 6 fixes + 2 nits.
- All 6 fixes applied: adaptive level-3 rate math corrected (1.11 Hz → 2 Hz);
  `ts_ms_rel` made relative (was absolute); fixed-bucket → 5 s rolling window;
  check-7 timeout warning added; smoke f32 range gate corrected to [0, 600 s];
  manifest inlines full `config_dump`.
- Blocker 1 fixed: boss-bar/lock-on roster pass split into priority + fill so
  entries land in `work[]` regardless of roster size.
- Blocker 2 (detour does compute) declined with documented reasoning — spec's
  "ZERO compute" rule applies to delta/format work, not enemy selection.
- TODO deferred to v6.1: worker-side delta encoding (not blocking; produces
  3–5× larger `.bin` without it).

### Commits
- `9af84e8` feat(probe): v6 source — discovery probe per locked spec
- `7c4827e` feat(tools): post-capture analysis pipeline + runbook
- `6db35ca` feat: walk-through self-service tooling for the test session
