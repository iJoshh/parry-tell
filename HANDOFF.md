# parry-tell-probe — HANDOFF

**Last updated:** 2026-05-11 21:05 CDT (v6.4 production deployed for tonight's co-op session)

## TONIGHT'S CO-OP SESSION — operating manual

Probe v6.4 is deployed at `/mnt/station-mods/parry-tell-probe.dll`. The
PowerShell tailer is at `claude@station:C:\Projects\elden-ring\probe-status.ps1`.

### Josh's gameplay flow

1. Optional: open a PowerShell window on the station box, run
   `.\probe-status.ps1` (or `pwsh ./probe-status.ps1`). Shows ARMED/DISARMED
   live. Audible F11 beeps are the primary feedback though, so the
   tailer is just a visual confirm.
2. Boot Elden Ring. Probe attaches automatically.
3. Walk to first boss arena. Press **F11** → low double-beep = **ARMED**.
4. Fight. Wipe as many times as you want — probe stays armed through
   wipes. Each wipe is just more data in the same .bin.
5. Boss dies. Press **F11** → long high beep = **DISARMED**.
6. Tell Claude "done with <boss name>" (or text via DM).
7. Walk to next boss. Repeat F11 → fight → F11 → tell Claude.

### Claude-side flow per boss-done report

```
tools/archive_session.sh                  # archive all today's sessions
tools/segment_by_f11.py captures/sessions/YYYYMMDD/qualification-YYYYMMDD-HHMMSS
```

The archiver pulls the .bin (and any rotated .bin.NNN shards), .csv,
.log.txt locally. The segmenter produces a `<session>.segments.json`
listing each F11 arm/disarm cycle with sample counts and dominant c-ids
per segment.

### Multi-boss in one .bin

The probe opens its files ONCE per game session and keeps them open
until game exit. F11 toggling doesn't open/close files — it just controls
sample emission. So all the night's bosses end up in the SAME .bin file
unless Josh restarts the game. The segmenter handles that — each F11
arm/disarm pair becomes one segment.

### Audio feedback reference

- **ARMED** = two quick LOW beeps (660 Hz × 2 × 100 ms)
- **DISARMED** = one long HIGH beep (1320 Hz × 400 ms)

### What if probe goes wrong

- If F11 produces no beep at all: probe didn't load. Check
  `parry-tell-probe.boot.log` in the mods folder.
- If beeps fire but no .bin produced: roster init failed AND fallback
  failed. Check `.log.txt` for `F11: roster recheck FAILED`.
- If Josh forgets to disarm before quitting: probe writes a final
  "session terminating" record, .bin file closes cleanly. Last F11 arm
  with no disarm pairs into a single open-interval segment in the
  segmenter manifest.

---

## Where we left off

Probe v6.4 is deployed and ready for tonight's multi-boss co-op session.
This session resolved all three ER 2.6.1 ChrIns offset questions that
research-006 flagged as bugs — they were never bugs. The v6.2 capture
used a stationary enemy; v6.3 with an actively-fighting enemy confirmed
the original v6.1.1 offsets are correct.

**The probe was never the bug.**

### Resolved offsets (HIGH confidence)

| Field | Offset / Path | Source of truth |
|---|---|---|
| Enemy + player world pos | `bag→+0x68→+0x70` (phys-chain, Vector3) | v6.3 byte-verified; player legacy `+0x6C0` also works but has chunk wraps |
| Enemy active anim_id | `TimeAct + 0xD0` (path A, original v6.1.1) | v6.3: 9,265 nonzero reads, 89 transitions, clean anim_time monotonicity |
| Enemy anim_time | `TimeAct + 0x24` AND `+0x28` | v6.3: clean monotonic playback, ~16 resets on anim_id transitions |
| Player lock-on target | `PlayerIns + 0x6B0` (FieldInsHandle u64) | 20 transitions vs 0 for `+0x6A0`; FromSoft handle pattern confirmed |
| Player lock-on target area | `PlayerIns + 0x6B4` (u32) | Toggle-paired with `+0x6B0` |

### What v6.3/v6.4 also fixed

- `in_lock_on` flag now fires correctly (was derived from dead `+0x6A0` in
  v6.1.1/v6.2). v6.3 introduced `playerLockHandleEffective` that prefers
  `+0x6B0`. `focus_reason=FOCUS_LOCK_ON (1)` fires in 74% of v6.3 samples,
  was 0% in v6.2.
- Boss-bar gating (was silently broken because it gated on `in_lock_on`).
- Co-op safety: v6.4 scans 8 WCM_PLAYER_ARRAY slots (4 base + 4 Seamless
  Coop extension) and excludes all valid friendly `chr_ins` pointers from
  both roster passes.

## Accomplishments this session

1. **Research-006 dispatched + completed.** Dual deep-research (Claude skill +
   Codex CLI) across five vendor sources. Three ER 2.6.1 ChrIns offset
   questions investigated. Synthesis in `research/006-SYNTHESIS.md`.

2. **Fixture verification refuted the vswarte anim_queue model** for c4382
   Knight. Bundle-fix approach abandoned; instrumentation build commissioned.

3. **Probe v6.2 instrumentation build.** Schema v2. 48-byte Tier 1 player
   block + 40-byte enemy header; three new region IDs (6/7/8). Codex deep-
   critic pre-deploy caught CSV header drift (P1), region 4/8 overlap (P1),
   comment drift (P2) — all fixed.

4. **Research-007 (v6.2 capture analysis).** 8,773 focused rows of c4382
   Knight. Q1 (world pos) → phys-chain wins. Q3 (lock-on) → `+0x6B0` wins.
   Q2 (enemy anim_id) → dead end; stationary-enemy artifact.

5. **Probe v6.3 module-bag-wide instrumentation.** REGION_MODULE_BAG_MEMBER (9).
   Lock-on derivation fixed. Codex deep-critic caught two P1 analyzer bugs.

6. **Research-008 (v6.3 capture analysis) — Q2 SOLVED.** 12,467 focused rows,
   ~144 s. Path A (`TimeAct + 0xD0`) confirmed correct. The probe was right
   all along.

7. **Probe v6.4 production build** deployed to `/mnt/station-mods/`. Drops
   instrumentation regions 6/7/8/9. Co-op safety. Audible F11 feedback.

8. **Supporting tools shipped:** `tools/probe-status.ps1` (deployed to
   station), `tools/archive_session.sh`, `tools/segment_by_f11.py`. All had
   Codex deep-critic passes; P1 findings fixed before deploy.

9. **HANDOFF.md** rewritten with full tonight-session operating manual.

**Deep-critic gatekeeping:** 8 findings across v6.2/v6.3/v6.4 tooling (5 P1,
3 P2) — all caught and fixed before deploy/capture.

## Next steps (priority order)

1. **Tonight:** Josh plays multi-boss co-op session. Per boss-done report:
   run `tools/archive_session.sh` then `tools/segment_by_f11.py` on the
   archived session path.
2. **Next session:** implement DB join-key fuzzy mapping in
   `qualify_oracle.py`. Individual variant c-id (e.g. c4382) → parent family
   (e.g. c4380). ~30-line Python change. This is the only blocker before
   qualification can PASS.
3. **Once join-key works:** achieve qualification PASS on c2130 Banished Knight
   (79 parry windows in DB) or c4380 Knight (53 windows).
4. **Then:** build the actual parry-prediction analyzer.
5. **(Lower priority)** If Seamless Coop ever exposes >8 player slots in one
   lobby, bump `FRIENDLY_SCAN_SLOTS` in `probe.cpp`.

## Open questions for Josh

- None blocking tonight.
- Tomorrow: worth discussing whether to extract parry data for c4311 (Godrick
  Soldier) — it's a frequent fight enemy with no DB entries. Not blocking
  anything, just a data-coverage question.

## Tried and ruled out

- `TimeActModule + 0x20 + read_idx*16` (vswarte anim_queue model) for enemy
  anim_id: refuted by v6.2 fixture (all sentinels), confirmed v6.3 (still
  sentinels — queue not used for AI-controlled enemies; player reads from
  there OK).
- `ActionRequestModule + 0x90` (Erd-Tools path) for enemy anim_id: sentinel
  in both v6.2 and v6.3.
- Module-bag-wide brute-force scan for c4380 anim IDs: v6.3 found only stable
  structural fields at `bag+0x18+0x1F0` and `bag+0x58+0x9C` — not anim_id.
- Path A "TimeAct + 0xD0" tentatively concluded WRONG in research-006/v6.2 —
  that conclusion was wrong. v6.2 sample was a stationary enemy. v6.3 with
  active combat confirmed path A is correct (the original v6.1.1 offset was
  right all along).

## Files modified this session

- `probe/probe.cpp` — v6.1.1 → v6.2 → v6.3 → v6.4
- `tools/probe_bin.py` — schema-v2 parser support; v6.4 region name labels
- `tools/analyze_v62_capture.py` — NEW; extended in v6.3 for region 9 + source_chain
- `tools/scan_for_anim_ids.py` — NEW (research-006 brute-force scanner)
- `tools/archive_session.sh` — NEW
- `tools/probe-status.ps1` — NEW; deployed to `C:\Projects\elden-ring\`
- `tools/segment_by_f11.py` — NEW
- `probe/v6.2/{CHANGES.md, probe-v6.2.patch}` — NEW
- `probe/v6.3/{CHANGES.md, probe-v6.3.patch}` — NEW
- `probe/v6.4/{CHANGES.md, probe-v6.4.patch}` — NEW
- `probe/releases/parry-tell-probe-v6.{2,3,4}.dll` + `.tar.gz` — NEW
- `research/006-SYNTHESIS.md`, `006-claude-deep-research.findings.jsonl`,
  `006-codex-research.md`, `006-fixture-verification.md` — NEW
- `research/007-v62-capture-analysis-codex.md` — NEW
- `research/008-v63-capture-analysis-codex.md` — NEW
- `captures/sessions/20260511/` — v6.3 capture archived + `segments.json`
- `captures/.gitignore` — NEW (raw .bin excluded; manifests + log.txt tracked)
- `HANDOFF.md` — rewritten

## Services / processes

- No Claude-side services restarted.
- SSH from Linux VM to Windows station ran MSBuild three times (v6.2, v6.3,
  v6.4). Probe DLL deployed three times to `/mnt/station-mods/` between game
  restarts on station.

## Git state at session close

- **Branch:** `main`
- **Unpushed commits:** 19+ (post-session-close tag will add ~2 more)
- **Recent commits (last 10):**
  - `63885eb` docs(handoff): document tonight's co-op session operating manual
  - `7cc9a45` feat(probe-v6.4): production cleanup + co-op safety + audible F11 + tooling
  - `6016681` chore(cp): v6.4 plan logged in HANDOFF.md; starting cleanup
  - `9d1ce95` chore(cp): bundle v6.3 DLL artifact
  - `b8222ef` feat(probe-v6.3): module-bag-wide enemy anim_id instrumentation + lock-on derivation fix
  - `023ad0e` chore(cp): pre-v6.3 module-bag-wide instrumentation
  - `ec4a140` fix(probe-v6.2): apply Codex deep-review P1 findings before capture session
  - `f074059` chore(cp): probe v6.2 instrumentation build — dual-read three offset candidates per research 006
  - `7803947` chore(cp): pre-v6.2 instrumentation build research 006 synthesis locked
  - `97d7e29` research: ER 2.6.1 ChrIns offset investigation prompt + post-compact pickup
- **Session-close tag:** `session-close/2026-05-11-<HHMMSS>` (created at step 5
  of the session-close runbook; check `git tag --list | tail` after close).

## Pickup prompt for next session

```
Resume parry-tell-probe. Read HANDOFF.md first.

Tonight's co-op session may have produced new captures in
/mnt/station-mods/ (or already archived to captures/sessions/20260511/).
If Josh reports "done with <boss>", run:
  tools/archive_session.sh
  tools/segment_by_f11.py captures/sessions/20260511/<session-name>

Primary next task: implement DB join-key fuzzy mapping in
qualify_oracle.py so c4382 (individual variant) maps to c4380 (parent
family). This is the only blocker before qualification can PASS.
~30-line Python change. Then run qualify_oracle.py against a c2130
Banished Knight or c4380 Knight capture.

Probe v6.4 is deployed and correct. No probe changes needed unless
tonight's session surfaces a new issue.
```
