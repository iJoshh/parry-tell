## 2026-05-15 — Phase 4.0 Gate 0.B SOLVED: target-of-attention field at ai_struct +0xC988

### Research

- **Gate 0.B resolved.** Boss target-of-attention field confirmed at
  `ChrIns +0x580 (AIBag*) → +0xC0 (AIStruct*) → +0xC988`.
  Type is `FieldInsHandle` (u64), NOT a `ChrIns*` pointer. Sentinel is
  `0xFFFFFFFFFFFFFFFF` (no current target). Validated on 5,521 real-boss
  samples from v7.3 capture: 63.6% match against `player_handle` when boss
  was targeting Josh, 3 distinct handle values matching exactly what was on
  screen (player, jellyfish summon, sentinel), 9 clean transitions, zero
  false positives. Aligns with TarnishedTool prior art: `+0xC480` =
  TargetingSystem base, `+0xC988` = `+0x508` into that sub-struct,
  plausibly `currentTarget`.

### Added

- `probe/probe.cpp` v7.0–v7.3 — four probe iterations this session:
  - v7.0: added regions `ai_bag_head`, `ai_struct_head`, `module_bag_head`
    on focused enemies; established 0% ChrIns* match across 5.27M slots,
    confirming field is handle-shaped.
  - v7.1: added player's own `FieldInsHandle` to sample header (reused
    v6.2 reserved slot); wire-format backward-compatible with old captures
    (`player_handle = 0` on parse).
  - v7.2: added regions `ai_struct_mid` (+0x1000..+0x4000),
    `action_req_head` (+0x000..+0x200), `player_chr_ins`
    (+0x000..+0x800); threaded `player_chr_ins` through
    `WriteTier3ForEnemy` / `WriteEnemyRecord` for sample-scoped region 15.
  - v7.3: added regions `ai_struct_far`, `ai_struct_deep`, `ai_struct_tgt`
    covering the +0x4000..+0xE000 gap (TarnishedTool TargetingSystem
    range); re-enabled `REGION_MODULE_BAG_MEMBER` (module body capture);
    fixed friendly-exclusion bug where `playerChrIns` was excluded from
    `friendlyPCs[]` causing player to be selected as "nearest enemy" in
    ~21% of samples.
- `tools/probe_bin.py` — added region names for IDs 10–18; added
  `player_handle` field to `Sample`; reads v7.1's previously-reserved
  8-byte slot.
- `tools/analyze_target_field.py` — NEW. Scans 8-byte aligned slots in
  target regions for player `ChrIns*` OR player handle equality.
  Coverage-weighted scoring. Skips player-as-focused samples.
  Self-reference detection + penalty. Iteratively hardened through two
  Codex adversarial critic passes (caught: nonexistent import, missing
  offset normalization, broken `with_suffix` on version-numbered paths,
  missing coverage gate, broken handle-shape predicate, missing handle
  equality testing, missing self-ref classification).
- `probe/releases/` — 4 new DLL archives:
  `parry-tell-probe-v7.{0,1,2,3}-target-scan.dll` + tarballs.
- `probe/releases/v7.3-target-field-report.md` — analyzer output pinning
  the Gate 0.B discovery.

### Fixed

- Friendly-exclusion bug in qualification nearest-enemy selection:
  `if (friendlyChr == playerChrIns) already = true` was excluding the
  player from `friendlyPCs[]`, so the downstream exclusion check never
  matched Josh. This caused the player to be selected as the "nearest
  enemy" in ~21% of v7.2 samples, producing a false-positive
  `action_req +0x08` candidate (the action's owner pointer, not a target
  field). Fixed in v7.3; zero player-as-focused samples in the v7.3
  capture.

### Refuted (do not re-investigate)

- `ChrIns +0x6A0` as enemy `targetHandle` (Erd-Tools-CPP layout claim).
  Verified 100% zero on enemies in v7.2 region 0 data — the
  +0x6A0..+0x6C0 range is player-specific lock-on storage.
- `ChrIns*` pointer-equality as the target field shape. 0% across 5M+
  u64 slots in v7.0 + v7.2 combined.
- `ActionRequest +0x08` as target candidate. False positive from
  friendly-exclusion bug + self-reference (owner pointer, not target).

## 2026-05-11 (evening, second session-close) — qualify_oracle end-to-end PASS; launch-monitor tooling; Bundle A plan decision

### Fixed
- `tools/qualify_oracle.py::find_join_key` — DB family-fallback and junk-cid
  filter shipped in commit `b269d7f` (see earlier entry today). Qualification
  PASS confirmed on real combat footage: `qualification-20260511-195759`
  (12,467 samples, 144 s Godrick Soldier fight), join key `field_at_0x064 =
  4311 → c4310` via family fallback, anim_time `+0x24` (1760 forward
  progressions), 5/8 windows matched within ±11 ms (62.5% vs 60% threshold).
  **First end-to-end PASS on real combat footage.**
- `tools/qualify_oracle.py::find_anim_time_field` — duration-slot rejection
  shipped in commit `b739a82` (see earlier entry today). PASS verdict now
  correctly selects `+0x24` over `+0x2C` on the live capture.

### Added
- `tools/launch-monitor.ps1` — NEW. ETW kernel-process trace + 500 ms process
  snapshot loop + auto-extract DLL load timeline via `tracerpt`. Deployed to
  `C:\Projects\elden-ring\launch-monitor.ps1` on station. A/B test (probe
  enabled vs disabled) showed 4–5 min launch WITHOUT probe, confirming probe
  is not the cause of Josh's reported 6-minute launches. DebugView capture
  revealed a 222.5 s silent gap between early init events and first audio
  activity — likely EOS init / EAC stub / DRM check / asset preload. Not
  blocking; tracked for future investigation.

### Fixed (launch-monitor.ps1 pre-deploy)
- `$LastEldenEnd` not latched — eldenring_exit branch would emit every 500 ms
  and prematurely auto-stop. Fixed before deploy.
- CSV row builder used naive comma-join without quoting — embedded commas /
  quotes / newlines could corrupt the CSV. Fixed before deploy.
- Em-dash characters in the .ps1 source broke Windows PowerShell 5.1 parsing.
  Replaced all em-dashes with `--`; saved as UTF-8-with-BOM.

### Decision
- **Bundle A ship strategy locked.** MVP audio + L1 target filter will ship
  together as `v0.1.0` (skipping the separate MVP-only ship). L2 hue overlay
  deferred to Bundle B after real-play feedback. Risk #1 (data pipeline) is
  fully eliminated by tonight's PASS; risks #2 (D3D12 hook) and #3
  (target-of-boss field) still exist and will be addressed in PHASE4-PLAN.md.

## 2026-05-11 (evening, post-session-close) — qualify_oracle anim_time duration-slot rejection

### Fixed
- `tools/qualify_oracle.py::find_anim_time_field` — gate now requires
  `forward_progressions >= 50` (count of strictly-positive within-anim
  deltas) AND ranks passing candidates by `forward_progressions` instead
  of `max_segment_dur`. Duration-style fields (`TimeAct + 0x2C` on the
  v6.3 live capture: discrete values like 1.000 / 2.500 held constant
  within an anim, jumping between anims) used to slip through the
  `monotonic_segments + max_segment_dur + rewind` gate because sequences
  of identical values satisfy `val + 1e-6 >= prev_val` and the rare
  cross-anim jumps inflate max_segment_dur. The new discriminator is
  two orders of magnitude wide on real data: +0x24 = 1292 forward
  progressions, +0x2C = 20. Research-008 already pinned +0x24 as the
  correct anim_time field; this aligns the oracle's tiebreak with the
  research conclusion.
- Synthetic test fixture in `test_qualify_oracle.py` previously left
  slot 3 (+0x2C) at the auto-failing value `-1.0`, which short-circuited
  the new rejection logic by failing the range check first. Fixture now
  populates +0x2C with realistic duration-decoy values (cycles through
  1.0, 2.5, 1.5, 3.0 across anims, held constant within each anim) so
  the duration-rejection logic is actually exercised by the regression
  test. New assertion locks the winner at +0x24.

### Result on the v6.3 live capture
- Verdict: PASSED (5/8 windows matched within ±11ms vs c4310 family
  parry data — over the 60% threshold). End-to-end data flow validated
  on real combat footage for the first time.

## 2026-05-11 (evening, post-session-close) — qualify_oracle DB family-fallback + junk-cid filter

### Fixed
- `tools/qualify_oracle.py::find_join_key` — captured c-ids that don't have
  their own DB row (e.g. `c4311` Godrick Soldier) now fall back to the parent
  family entry (`c4310`, round-down-to-nearest-10). The DB is keyed at the
  family-parent level for 262/281 c-ids; the 19 unique-entity exceptions
  (e.g. `c3251` Erdtree Avatar) still take exact-match precedence so we
  don't silently round their data off. Verdict includes a new
  `matched_via_family_fallback` flag for honest reporting.
- `tools/qualify_oracle.py::find_join_key` — also fixed a latent bug where
  the 7-field candidate scan would match junk `field_at_0x1E8 = 0` against
  the player-skeleton row `c0000` (4039 player parry windows). Two new
  filters: candidates with value < 1000 are skipped (real enemy c-ids are
  always >= 1000), and candidates whose matched character has zero parry
  windows are skipped (a join-key field that resolves to a parry-less row
  is structurally not what we want). Caught when the v6.3 capture's
  oracle run picked `field_at_0x1E8 = 0 → c0000` instead of the correct
  `field_at_0x064 = 4311 → c4310-via-fallback`.

### Added
- `tools/test_qualify_oracle.py::test_family_fallback_lookup` — five-case
  unit test covering exact match (c2130), family fallback (c4311 → c4310),
  exact-takes-precedence (c3251 has its own row AND c3250 exists), absent
  c-id (c9999), and already-multiple-of-10-but-absent (c9990, must not
  loop). End-to-end test gains a regression check that c2130 exact
  matches do NOT get flagged as family fallbacks.

### Known follow-up (not blocking the join-key fix)
- `qualify_oracle::find_anim_time_field` tiebreak picks `TimeAct + 0x2C`
  on live v6.3 data because the gate uses `max_segment_dur` as the
  primary key — but `+0x2C` is the per-anim duration slot (discrete
  values like 1.000, 2.500), not animation time. The smaller, simpler
  `calibrate_smoke` gate correctly rejects `+0x2C` as "constant-per-anim
  duration"; `qualify_oracle` needs the same rejection. Research-008
  pinned `+0x24` as the canonical anim_time field — qualify_oracle just
  picks the wrong winner. Separate fix; tracked for the next session.

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
