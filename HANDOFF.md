# HANDOFF — parry-tell-probe

**Date:** 2026-05-17 (America/Chicago)
**Session tag:** session-close/2026-05-17-HHMMSS (created by commit-and-tag step)
**Branch:** main, working tree clean
**Unpushed:** 17 commits (nightly cron at 00:00 CDT)

---

## Where we left off

**Phase 4.3 instrumentation is compiled and archived but NOT yet deployed.**

The big win this session was changing the substrate: instead of building a
fragile keyclick-anchor protocol on top of stale data (3 critic-blocked drafts),
we instrumented the probe itself to emit UTC wall-clock timestamps in every
`.predictions.jsonl` row. The DLL now self-aligns to OBS recording wall-clock
without any user-action anchor. v8.3.0 is built, SHA-verified, and waiting for
Josh's "ready to reload" signal.

Phase 4.2 audio cue is unchanged and still live on station (v8.2.1). The only
INI change this session was `audio_cue_lead_ms` 200 → 150 after Josh reported
200ms felt too early relative to the boss's visual commit.

**CRITICAL (carry forward from last session):** Parry in Elden Ring is **L2**
(weapon art), NOT L1 (block). The DB windows are L2-active-frames windows.
All future sessions must use L2 terminology.

---

## Accomplishments this session

### INI tune — audio_cue_lead_ms 200 → 150

- Josh reported the 200ms lead felt too early relative to the boss's visual
  commit. Bumped down to 150ms in `probe/v6/parry-tell-probe.ini.smoke` and
  deployed to `/mnt/station-mods/parry-tell-probe.ini`.
- Folded into checkpoint `d1d3d908`.

### Domain research — parry vs stagger semantics (Codex consultation)

- Josh reported "cue fires on attacks that catch damage but don't stagger" —
  L2 catches like a block but doesn't open a riposte.
- Codex confirmed: no single TAE flag governs parry-staggerability. Full chain
  is TAE → BehaviorParam → AtkParam → NpcParam.
- Cheapest filter: TAE event type 304 (throw detection, no extraction needed).
  Harder cases require `regulation.bin` extraction + AtkParam join (full
  session of work).
- Hyperarmor (FlagType=24) evaluated and withdrawn.

### Phase 4.3 probe.cpp instrumentation (commit `d1d3d908`)

- `CaptureSessionStartClocks()` — captures `steady_clock` + UTC wall-clock at
  session start. UTC chosen (not local) to match Matroska `creation_time`
  epoch; avoids 5-hour CDT offset bug.
- `JsonEscapeString()` — safe JSON-string helper covering `"`, `\`, control
  chars, `\u00XX` for codepoints < 0x20. Prevents JSON injection via
  user-controlled INI fields (session_name, wav_path, etc.).
- `PredictionLogOpen` emits a `session_open` JSON header as the first line of
  every `.predictions.jsonl`: `wall_clock_ms`, `session_start_ms`,
  `probe_version`, full config snapshot.
- `WritePredictionDecision` adds `wall_clock_ms` to every prediction row
  (`session_start_wall + ts_ms_rel`). Self-aligns to OBS recording wall-clock
  without any user-action anchor protocol.
- `PROBE_VERSION_STR` → `v8.3.0-phase4.3-timing`.
- Three rounds of Codex review caught real bugs before ship: uninitialized `tzi`
  read, JSON injection via `session_name`, `cfg_mode` field writing wrong value,
  buffer sizing wrong for worst-case 6x JSON escape expansion. All fixed.

### Build + archive (NOT yet deployed)

- v8.3.0 DLL built via SSH+MSBuild on station.
  SHA: `eb96cd749e977d96039a228447611c6edcf35a6489c6b5939fecff7e2d988c38`
  Size: 284,672 bytes.
- Archived to `probe/releases/parry-tell-probe-v8.3.0.dll` (gitignored).
- v8.2.1 backed up on station as
  `/mnt/station-mods/parry-tell-probe.dll.v8.2.1-backup`.
- **NOT yet copied to `/mnt/station-mods/parry-tell-probe.dll`.**
  Awaiting Josh's "ready to reload" signal.
- No smoke test run yet. Instrumentation is compile-verified only.

### Data-gather plan (TODO-PHASE-4.3-DATA-GATHER.md, v4)

- Four drafts, all critic-reviewed. First three blocked on structural issues:
  - v1: spoken-phrase anchor — human speech-to-press lag (150-400ms) larger
    than the 150ms cue window; would invalidate all timing.
  - v2: keyclick-anchor — Codex caught 7 issues including wrong JSONL field
    names and missing F11 event handling.
  - v3: corrected anchor — deep critic blocked on clock model (boot log uses
    `steady_clock` not wall-clock), stale artifact names, OBS safety overclaim.
  - Pivot to Option B: instrument the probe instead of building fragile
    alignment on top of stale data.
  - v4: written after instrumentation landed. Codex caught 4 more issues
    (cid 4311 zero DB coverage, UTC/local mismatch). All fixed.
- Final v4: ~250 lines, critic-clean. Covers preflight gate, Test 1-4
  sequence, JSONL verification, five pre-specified outcome branches.

### Critical finding — cid 4311 zero DB coverage

- cid 4311 (the enemy we fired against last session — 4 confirmed fires) has
  **zero entries** in `data/parry_data.json`.
- The fires must have routed through `resolved_cid ≠ raw_cid` family fallback.
  The parent/family cid is unidentified.
- Flagged as an open item in the data-gather plan for the next session's data
  to answer.

---

## Next steps (priority order)

1. **Deploy v8.3.0 DLL.** Copy `probe/releases/parry-tell-probe-v8.3.0.dll`
   to `/mnt/station-mods/parry-tell-probe.dll` once Josh gives the "ready to
   reload" signal (game must not be running).
2. **Smoke test.** ~30 seconds of game launch. Verify `.predictions.jsonl`
   first line is a `session_open` event with `wall_clock_ms` populated and
   that prediction rows carry the new `wall_clock_ms` field.
3. **Run data-gather session** per `TODO-PHASE-4.3-DATA-GATHER.md` (v4,
   critic-clean). Tests 1-4 in order; per-test JSONL verification.
4. **Post-session analysis.** Claude builds `alignment.md`, classifies into
   one of five pre-specified outcome branches, picks next phase:
   - 4.3.2 throw filter (TAE event type 304)
   - 4.3.3 regulation.bin extraction + AtkParam join
   - 4.3.5 coverage diagnosis (cid 4311 family fallback mystery)
   - 4.3.4 per-cid timing calibration
   - Second gather session

---

## Open questions for Josh

- **Test 4 boss choice.** Re-confirm which boss (Margit, Tree Sentinel,
  other?) and that the cid is reachable in current save.
- **OBS recording mode.** Game Capture (convenient, uses process hooks) vs
  Window Capture (safer for co-op-safety paranoia). Pick before the
  data-gather session.
- **Cheat Engine.** Install for post-session investigation? Recommended yes,
  offline use only, from cheatengine.org installer with bundled-software
  offers declined.
- **Lead time re-confirm.** 150ms is the new live setting. Does it feel
  better than 200ms after a few fights, or should it move again?

---

## Tried and ruled out (this session)

| Approach | Verdict | Reason |
|---|---|---|
| Keyclick-audio-anchor protocol | REPLACED | Human speech-to-press lag (150-400ms) > 150ms cue window; structurally invalidates timing data |
| Local-time `wall_clock_ms` in JSONL | REPLACED | `ffprobe creation_time` is UTC; local time would introduce 5-hour CDT offset bug |
| Hyperarmor (FlagType=24) as parry-stagger filter | WITHDRAWN | No single TAE flag governs parry-staggerability; Codex confirmed |
| Plan-from-memory drafting | REPLACED | 3 critic blocks in a row; switched to read-code-then-plan |
| `audio_cue_lead_ms = 200` | TUNED DOWN | Josh reported 200ms felt too early relative to boss visual commit; now 150ms |

Carry-forward refuted hypotheses from prior sessions remain valid.

---

## Files modified this session

| File | Change |
|---|---|
| `probe/probe.cpp` | Phase 4.3 instrumentation: `CaptureSessionStartClocks`, `JsonEscapeString`, `session_open` header, `wall_clock_ms` in prediction rows, version bump to v8.3.0-phase4.3-timing (~141 lines changed) |
| `probe/v6/parry-tell-probe.ini.smoke` | `audio_cue_lead_ms` 200 → 150 |
| `TODO-PHASE-4.3-DATA-GATHER.md` | Rewritten from scratch four times; v4 is critic-clean, ~250 lines |
| `probe/releases/parry-tell-probe-v8.3.0.dll` | NEW — built artifact, gitignored, not yet deployed |
| `CHANGELOG.md` | Phase 4.3 entry prepended |
| `PHASE4-PLAN.md` | Session Log appended |
| `HANDOFF.md` | Overwritten (this file) |

---

## Services / processes

- **SSH service on station:** Status unknown at session close. Josh can stop
  with `Stop-Service sshd` in elevated PowerShell when done.
- **SMB mounts:** `/mnt/station-projects/` (RO) and `/mnt/station-mods/` (RW)
  live via Tailscale automount. Verify with `mount | grep station`.
- **Probe on station:** v8.2.1 DLL still at `/mnt/station-mods/parry-tell-probe.dll`.
  v8.3.0 is NOT yet deployed.
- **DLL backup chain on station:** v8.2.1 backed up as
  `/mnt/station-mods/parry-tell-probe.dll.v8.2.1-backup`. Earlier backups
  (v7.3, v8.0, v8.1, v8.1.2, v8.1.3, v8.2.0) preserved as `.dll.*-backup`.

---

## Git state at session close

- Branch: `main`
- Working tree: clean
- Unpushed commits: 17 (nightly cron pushes at 00:00 CDT)
- Last commit: `d1d3d908` — chore(cp): Phase 4.3 instrumentation: self-aligning UTC wall-clock in JSONL + v4 data-gather plan
- Session tag: `session-close/2026-05-17-HHMMSS` (created by commit-and-tag)

Recent commits:

```
d1d3d908  chore(cp): Phase 4.3 instrumentation: self-aligning UTC wall-clock in JSONL + v4 data-gather plan
34e1640   Phase 4.2 session-close
5385568   Phase 4.1 session-close
```

---

## Pickup prompt for next session

> Phase 4.3 instrumentation is compiled and archived but NOT yet deployed.
> v8.3.0 DLL is at `probe/releases/parry-tell-probe-v8.3.0.dll`
> (SHA `eb96cd74...`, 284,672 bytes). Station still runs v8.2.1.
>
> **First action:** ask Josh if he's ready to reload. If yes, copy DLL to
> `/mnt/station-mods/parry-tell-probe.dll`, then smoke test: launch game,
> verify `.predictions.jsonl` first line is a `session_open` JSON event with
> `wall_clock_ms` populated and that prediction rows carry `wall_clock_ms`.
>
> **After smoke test passes:** run the data-gather session per
> `TODO-PHASE-4.3-DATA-GATHER.md` (v4, critic-clean). Tests 1-4 in order.
> Post-session: Claude builds `alignment.md`, classifies into one of five
> pre-specified outcome branches, picks next phase.
>
> **Critical finding to keep in mind:** cid 4311 has zero entries in
> `data/parry_data.json`. The Phase 4.2 fires routed through a family fallback
> (`resolved_cid ≠ raw_cid`). The parent cid is unknown. The data-gather
> session should answer this.
>
> **Open questions before the data-gather session:** (1) Test 4 boss choice
> and save reachability, (2) OBS recording mode (Game Capture vs Window
> Capture), (3) Cheat Engine install decision.
>
> **CRITICAL (binding all sessions):** Parry in Elden Ring is **L2** (weapon
> art), NOT L1 (block). The DB windows are L2-active-frames windows. The mod
> tells Josh WHEN to press L2.
>
> INI on station: `audio_cue_lead_ms = 150` (tuned down from 200 this session).
> Audio cue (Phase 4.2) is unchanged and still live.
