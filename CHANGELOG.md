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
