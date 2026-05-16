# Phase 4.3 Data Gather Session — Plan (v4)

**Status:** ready to execute (pending DLL deployment + smoke verification)
**Created:** 2026-05-15 evening
**Owner:** Josh runs the session, Claude analyzes after
**Probe version required:** v8.3.0+ (Phase 4.3 instrumentation, built 2026-05-16
00:10 CDT, SHA `6ee25016`). The probe now writes wall-clock metadata into the
JSONL itself, eliminating the user-action anchor protocol that earlier drafts
needed.

---

## What changed vs v1-v3

Earlier drafts of this plan tried to align the JSONL predictor log to OBS
video via a user-action anchor (spoken phrase + F11 keypress + audio-track
keyclick detection). Codex and the deep critic blocked every draft because
the anchor protocol was either error-prone (human speech-to-press lag),
incompatible with the actual log schema (no `f11_armed` event in JSONL),
or fragile (OBS file mtime gets clobbered on copy).

The correct fix was upstream: the probe itself now writes a self-aligning
wall-clock anchor into the JSONL. Specifically:

- **First line of every `.predictions.jsonl`** is now a `session_open`
  event row: `{"event":"session_open","schema_version":1,"probe_version":...,
  "wall_clock_ms":<UTC epoch ms at session start>,"session_start_ms":<steady_clock>,
  "cfg_audio_cue_lead_ms":...,...}`.
- **Every prediction row** now has a `wall_clock_ms` field equal to
  `(session_start_wall + ts_ms_rel)`. So each row knows its own wall-clock
  time without any join.
- **All wall_clock_ms values are UTC** — matches Matroska/MKV `creation_time`
  which is also UTC, so the cross-reference math doesn't have timezone
  bugs.

Result: OBS recording wall-clock (UTC from MKV creation_time) → JSONL
wall-clock (UTC) is a direct comparison. No anchor keypress, no verbal cue,
no keyclick audio detection.

This shrinks the plan from ~500 lines to ~150.

---

## Why we're doing this

Two open questions need ground truth:

1. **Coverage**: Does the cue fire on enemies *other* than cid 4311? Run 3 of
   last session had 4 fires, all on cid 4311. Other enemies engaged silently.
   Either Josh only swung at 4311, or other cids never enter the predictor's
   firing path. The data alone can't tell us which.

2. **Stagger-class**: When the cue fires and Josh L2s in the window, does the
   enemy stagger (riposte-ready) or just block? Three observed-but-unclassified
   failure modes: grab/throw, heavy-attack-with-no-stagger, anim variant
   mismatch.

---

## Pre-session task: target selection

Run this BEFORE recording. We pick targets from the DB so we know in advance
which moves should cue. "Missed cue" then becomes a positive signal that the
predictor failed, not a "we don't know if we should have expected a cue."

**Critical finding from DB audit (2026-05-16):** the cid 4311 we fired on
successfully last session has **ZERO** entries in `parry_data.json`. The 4
fires we observed must have come through some other code path
(resolved_cid != raw_cid?) — this is itself a question this session needs
to answer. Note the `resolved_cid` field on every fire row carefully.

**Pre-session preflight gate (Claude runs, plan blocks if any cid is 0):**

```bash
cd ~/claude/elden-ring && python3 -c "
import json, sys
with open('data/parry_data.json') as f: db = json.load(f)
# Per-session candidate list — every cid here is a planned test target.
# This list MUST be finalized + pre-verified before recording starts.
candidates = {
    'c4310': 'Crucible Knight (Stormhill / Limgrave Tunnels)',
    'c2130': 'TBD humanoid (Claude maps in-game name pre-session)',
    'c3010': 'TBD non-humanoid (Claude maps in-game name pre-session)',
    # Test 4 boss cid MUST be added here BEFORE recording. Examples:
    # 'c3700': 'Tree Sentinel',
    # Margit's cid is TBD — verify via DB walk before assuming it's coverable.
}
fail = False
for cid, name in candidates.items():
    anims = db['characters'].get(cid, {}).get('animations', {})
    n = sum(1 for a in anims.values() if a.get('parry_windows'))
    status = 'OK' if n > 0 else 'BLOCK (no parry windows in DB)'
    print(f'{cid} ({name}): {n} animations — {status}')
    if n == 0: fail = True
if fail:
    print('\\n*** PREFLIGHT FAILED — fix candidate list before recording ***')
    sys.exit(1)
print('\\nAll candidates have DB coverage — preflight OK.')
"
```

**If any cid in the list reports 0 windows, the session does NOT proceed
until the candidate is swapped or the DB is rebuilt for that cid.**

**Candidate pool (Claude maps cid → in-game enemy name pre-session):**

- `c4310` Crucible Knight — Stormhill / Limgrave Tunnels; well-documented
  parry behavior (sword=parryable, shield-bash/tail/stomp=not). **45 anims
  with parry windows in DB — VERIFIED.**
- `c2130` (37 anims) — humanoid; Claude maps name before session.
- `c3010` (28 anims) — non-humanoid skeleton variety; Claude maps name.
- **Test 4 boss cid is TBD.** Claude proposes 1-2 options pre-session,
  Josh confirms one is reachable in his save, then preflight gate verifies
  it has DB coverage.

Hard rule: **every enemy fought in the session must be in `parry_data.json`
with a non-zero count of parry windows.** No "find a random mob and see what
happens."

---

## Frozen settings for the session

Before recording, confirm:

| Setting | Value | Lives at |
|---|---|---|
| `audio_cue_lead_ms` | **150** | `/mnt/station-mods/parry-tell-probe.ini` |
| `target_filter_enabled` | **true** | same |
| `audio_cue_enabled` | **true** | same |
| `prediction_decision_log_enabled` | **true** | same |
| DLL version | **v8.3.0** (Phase 4.3 instrumentation) | `/mnt/station-mods/parry-tell-probe.dll` |

Frozen for the whole session. If something feels off, stop the recording, log
the change, start a NEW recording with new settings — never edit mid-session.

---

## Session procedure (Josh)

### Recording setup

- [ ] OBS configured: 1080p60 game capture. **Two audio tracks** — mic on its
      own track, desktop audio on its own track. (OBS Settings → Audio →
      Mic on Track 2, Desktop on Track 1, OR use the Advanced Audio
      Properties dialog to route them separately.) Output to MKV (multi-
      track works cleanly in MKV; MP4 multi-track is fragile).
- [ ] System volume confirmed ≥ 50%. The 18%-volume bug from last session.
- [ ] **Note the OBS recording start time** — write down the system clock as
      shown on Windows taskbar when you hit Record (or just trust OBS's own
      filename timestamp; both are wall-clock and either works).

### In-game session

**Step 0 — Boot the game with the new DLL.**
- Start Elden Ring. The mod loads automatically.
- Confirm `parry-tell-probe.boot.log` shows the new probe version.

**Step 1 — Test segments (no F11 needed for any of these):**

Run each test segment as a separate, contiguous combat session. Between
test segments, return to grace / rest at a site. Each test segment gets a
SHORT verbal label spoken to the mic at its start so the post-session
transcript can chapter the recording.

Verbal callout protocol — say the bare minimum, leave game audio loud.

| Situation | Say this |
|---|---|
| Starting a test segment | "Test [N], [enemy name]" |
| You L2-parry and the enemy STAGGERS | "stagger" |
| You L2 but the enemy keeps attacking (no stagger) | "no stagger" |
| Cue fires on an obviously-unparryable move (grab/jump/etc) | "false positive" |
| A clear swing happens with NO cue (you'd have expected one) | "missed cue" |
| End of a test segment | "end test [N]" |

That's six phrases. Everything else can stay quiet — the JSONL captures
the rest.

**Test segments (all cids verified by preflight gate above):**

1. **Test 1: c2130 humanoid** (37 anims with parry windows; specific in-game
   name confirmed pre-session). Quota: 8-12 swings observed, mixed L2 attempts.
2. **Test 2: Crucible Knight** (c4310, 45 anims). High-value segment.
   Crucible Knights have a varied moveset where wiki ground truth says some
   moves are parryable (sword swings) and others aren't (shield bash, stomp,
   tail). Quota: 15+ swings observed.
3. **Test 3: c3010 non-humanoid** (28 anims; specific in-game name confirmed
   pre-session). Validates predictor on non-humanoid skeleton. Quota: 8-12
   swings.
4. **Test 4: Boss** — cid pre-selected and verified non-zero before this
   session. Quota: 25+ swings observed (run back if you die before quota; if
   you win before quota, that's fine, we note it).

**Step 2 — Wrap.** Return to grace. Stop OBS recording.

**Save the MKV with a TIMESTAMP in the filename**, not just the date, to
preserve OBS start wall-clock as a filename fallback (and to avoid clobbering
if multiple sessions run on the same day):

```
/srv/shared/ER Mod/session-YYYYMMDD-HHMMSS.mkv
```

Where `YYYYMMDD-HHMMSS` matches the OBS recording start time. If OBS's
default filename already has this format, just rename to add the `session-`
prefix without dropping the timestamp.

That's it. No F11. No anchor protocol. The JSONL self-documents.

---

## Post-session (Claude)

### Step A — Pull captures

1. ffprobe the MKV at `/srv/shared/ER Mod/` to confirm duration + track
   layout (verify there really are two audio tracks).
2. Per SMB perf rule: copy the JSONL + log files for THIS session from
   `/mnt/station-projects/elden-ring/logs/` to local `/tmp/` before any
   Python parsing. The session tag is `smoke-<YYYYMMDD>-<HHMMSS>` (probe-side
   naming pattern) — note this is DIFFERENT from the OBS recording filename
   pattern `session-YYYYMMDD-HHMMSS.mkv`. Match the two only by the
   `YYYYMMDD-HHMMSS` timestamp portion (probe and OBS both use local-time
   timestamps for filenames, recorded within a few seconds of each other).
   `ls -lt /mnt/station-projects/elden-ring/logs/ | head` to pick the most
   recent set if the timestamps don't line up exactly.
3. Extract the two audio tracks: `ffmpeg -i session.mkv -map 0:a:0 desktop.wav
   -map 0:a:1 mic.wav` (verify track index assignment first via ffprobe).
4. Transcribe `mic.wav` with Whisper. Game audio is on the other track so
   transcription has clean voice input.

### Step B — Build the timeline

All wall-clock values are UTC end-to-end. Display conversion to local time
happens in the alignment markdown output, not in the math.

1. **Read the session_open line** (first line of JSONL): extract `wall_clock_ms`
   (UTC epoch ms) and `session_start_ms`. This is the JSONL's wall-clock anchor.
2. **Read the OBS recording start wall-clock**: try in order:
   a. `ffprobe -v error -show_entries format_tags=creation_time -of
      default=nw=1:nk=1 session.mkv` — Matroska creation_time is UTC ISO 8601.
      Parse with `datetime.fromisoformat(s.replace('Z','+00:00'))` and convert
      to UTC epoch ms.
   b. If `creation_time` is empty: parse the filename pattern
      `session-YYYYMMDD-HHMMSS.mkv` — note this is LOCAL TIME from OBS, so
      it needs to be converted to UTC via the system's timezone offset
      (Chicago = -5h in CDT, -6h in CST).
   c. If both fail: stop, log the failure, fix recording naming for next
      session.
3. **Every JSONL row already has `wall_clock_ms` in UTC**. To get its position
   in the recording: `video_offset_seconds = (row_wall_clock_ms -
   obs_start_wall_clock_ms_utc) / 1000`. Sanity check: this should fall
   between 0 and the MKV duration; flag rows outside that range.
4. **Verbal callouts from Whisper** come with their own offset within the mic
   track, which IS the video timeline (extracted from the same MKV). No UTC
   conversion needed for these — Whisper offsets are seconds-since-track-start,
   which is seconds-since-recording-start.

### Step C — Cross-reference

Produce `runs/session-<date>-alignment.md`:

1. **Coverage table**: For each cid Josh engaged, count fires + suppressions +
   no-key actions. Build the matrix of "we expected fires here per DB" vs
   "did we get fires."
2. **Stagger outcome**: For each fire near a "stagger" or "no stagger" verbal
   callout, classify the (cid, anim_id) → outcome.
3. **False positives**: Fires near "false positive" callouts — what move was
   it, anim_id, why did predictor match.
4. **Missed cues**: "missed cue" callouts — was the anim in the DB? Did the
   predictor see it at all (any action row in JSONL for that timeframe)?
5. **Quota check**: Per-cid swing counts vs quota. Flag any segment as
   inconclusive if quota not met.

### Step D — Decide next phase

Pre-specified branches (same as v3):

**(1) Cue works broadly, over-firing dominated by grabs** → Phase 4.3.2:
extend TAE parser to capture `ThrowAttackBehavior` event type 304, filter
windows on grab animations.

**(2) Cue works broadly, over-firing dominated by heavies/multi-parry** →
Phase 4.3.3: regulation.bin extraction + AtkParam join. Full session of work.

**(3) Cue fires on ≤2 distinct cids despite engaging more** → STOP filter
work. Coverage diagnosis becomes priority. Specific diagnostics:
- Check `action` field on silent cids — predictor seeing them at all?
- Check `anim_id` — reading 0 (memory fail) or a real ID missing from DB?
- Check `resolved_cid` vs `raw_cid` — resolver mapping silent cids correctly?

**(4) Cue fires on multiple cids but timing varies** → Phase 4.3.4 per-cid
lead calibration tool.

**(5) Mixed / quotas not met** → second focused gather session.

---

## Co-op safety

- Phase 4.3 changes added LOG output fields; no game-memory writes.
- No new game-process interaction beyond what v8.2.1 already had.
- Paramdex clone (not in this session, but if it comes up): pure GitHub
  schema download, read-only research only.
- OBS Game Capture mode does use process-attach hooks; if you want to be
  maximally cautious, switch to Window Capture or Display Capture (slightly
  worse image quality, no process interaction). Codex flagged this in v3
  review. Up to Josh; co-op-safety baseline of the mod itself is unchanged.

---

## What this does NOT do

- Does not extract regulation.bin.
- Does not change predictor or audio logic (Phase 4.3 changes are
  observation-only).
- Does not commit to any filter approach (throw / hyperarmor / Paramdex)
  yet — those decisions fall out of the data.

---

## Open items not solved by this plan

1. **`c0000` (player skeleton) has 4116 parry windows** in the DB — those
   are player-side events, the mod should ignore them. Verify post-session
   that no `c0000` fires happened during play; if any did, that's a bug.

2. **CRITICAL: cid 4311 (last session's test target) has zero parry windows
   in `parry_data.json`.** Yet the predictor fired 4 times against cid 4311
   in run 3. The probe must be matching on `resolved_cid` rather than
   `raw_cid` for that case, and the resolved_cid is something else with DB
   coverage. Use this session's data to confirm: check the `raw_cid` and
   `resolved_cid` fields on every fire row, see whether resolved_cid points
   to a cid that IS in the DB. This is a real architectural question, not
   a session-procedure issue.

3. **Smoke verification of v8.3.0 DLL not yet done.** Before this session
   counts as "real" data: load the new DLL, fire it once, eyeball the
   `.predictions.jsonl` first line to confirm session_open lands correctly
   and rows have `wall_clock_ms` populated. ~2 minute task; covered by the
   "Smoke test locally" todo. Josh runs this manually next time he boots
   ER with the new DLL deployed.
