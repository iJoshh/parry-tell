# Phase 4.3 Data Gather Session — Plan (v2)

**Status:** ready to execute
**Created:** 2026-05-15 evening (post session-close)
**Revised:** 2026-05-15 evening, post Codex review
**Owner:** Josh runs the test session, Claude analyzes after
**Prerequisite:** v8.2.1 DLL deployed, smoke INI tuned to `audio_cue_lead_ms=150`, `target_filter_enabled=true`

---

## Why we're doing this

After session 2026-05-15 evening, two open questions need ground-truth evidence
before we go further on filters or feature work:

1. **Coverage question:** Does the cue fire on enemies *other* than the cid 4311
   field grunt we tested against? The 4 fires in run-3 were all the same cid.
   If we run a broader engagement and only one cid ever produces cues, we have
   a coverage gap — which is worse than over-firing because it's silent.

2. **Stagger-class question:** When the cue fires and Josh presses L2 within
   the window, does the enemy actually stagger (riposte-ready) or just block
   the damage? Three observed-but-not-classified failure modes:
   - **A. Grab/throw cued anyway** — Josh L2s, gets grabbed, no parry possible.
   - **B. Heavy attack cued, L2 catches damage but no stagger** — possible
     hyperarmor or multi-parry-required boss.
   - **C. Animation variant mismatch** — anim_id in DB but the actual variant
     in-game doesn't honor the window (anim 4003103 mystery).

Plain English: we need video of what's happening on screen, synced with the
audio cue and the predictor's own log, so we can see exactly which attack
fired which cue, and what happened next.

---

## Test target selection (do this BEFORE recording)

We are NOT going to "find any enemy and hope." We're picking targets from
`data/parry_data.json` so we know in advance which moves SHOULD cue. This
turns "missed cue" from a noisy null into a positive signal that the
predictor failed to fire on a known-DB-covered animation.

**Pre-session task (Claude runs this):**

```bash
cd ~/claude/elden-ring && python3 -c "
import json
with open('data/parry_data.json') as f: db = json.load(f)
targets = ['c4310','c2130','c3010','c3660']  # initial pick: see notes below
for cid in targets:
    print(f'\\n=== {cid} ===')
    anims = db['characters'].get(cid, {}).get('animations', {})
    n = 0
    for aid, a in anims.items():
        pw = a.get('parry_windows', [])
        if pw:
            n += 1
            if n <= 8:
                for w in pw:
                    print(f'  {aid}: parry {w[\"start_time\"]:.3f}-{w[\"end_time\"]:.3f}')
    print(f'  total animations with parry windows: {n}')
"
```

**Pick rationale:**

- **c4310 (49 windows)** — appears to be a Crucible Knight family variant.
  Wiki-documented as parryable on sword attacks, NOT parryable on shield bash
  / stomp / tail. Gives us mixed-move ground truth.
- **c2130 (79 windows)** — a knight-class enemy (need to confirm; based on
  numbering position it's likely a humanoid knight). Validates the read works
  on a different humanoid skeleton variant.
- **c3010 (63 windows)** — a beast/animal family enemy. Validates the read
  works on a non-humanoid skeleton.
- **c3660 (70 windows)** — picked for sample diversity.

**If Claude's pre-session research shows a target is unreachable in Josh's
current save / Limgrave-range progression, swap it.** A Soldier of Godrick
(`c2200` or similar — confirm pre-session) is the always-available fallback
even if its window count is small.

**Hard rule:** every enemy fought in the session must be cross-referenced
to a known cid in `parry_data.json` BEFORE recording. We log the (chr_name
→ cid) mapping at the top of the session notes.

---

## The signals we need to align

There are FOUR independent timestreams. They all need a shared clock.

| Stream | Source | Timestamp shape | Captured how |
|---|---|---|---|
| Video frame | OBS recording | Wall-clock + frame number | OBS scene |
| Mod's predictions | `WritePredictionDecision` | `ts_ms` = session-relative milliseconds since DLL load | `predictions.jsonl` |
| Mod's status events | `BootLog` / `LogF` | Wall-clock string per line | `parry-tell-probe.boot.log` |
| Josh's verbal narration | Mic on a SEPARATE OBS audio track | Wall-clock in audio | OBS Mic track |

**Key correction from v1 of this plan**: there is NO `f11_armed` event in
JSONL. JSONL fields are: `ts_ms, boss_handle, boss_chr_ins, raw_cid,
resolved_cid, anim_id, anim_time_s, window_ord, window_open_s,
window_close_s, lead_ms, cfg_lead_ms, target_filter, target_match,
inst_seq, action`. F11 events go to the boot log, which has wall-clock
timestamps. So the anchor strategy uses the boot log, NOT JSONL.

**Clock-alignment plan:**

The mod's `ts_ms` is monotonic milliseconds since the DLL session started.
The boot log timestamps lines as wall-clock. So:

- Boot log has wall-clock for each major event (boot banner, F11 toggle).
- JSONL has session-relative `ts_ms` for each prediction.
- The boot log's `=== boot ===` banner line ties `ts_ms=0` to a wall-clock
  moment.
- OBS records its own wall-clock (system time) + frame number.

We use the boot log as the bridge between JSONL session-time and OBS
wall-clock. F11 keypresses ALSO appear in the boot log with wall-clock
timestamps — those become our verification anchors.

Result: we don't need an audio-keyclick anchor. The mod's own logs +
OBS wall-clock are sufficient. Removing the keyclick dependency removes
mechanical-keyboard-required assumption, removes mic-quality dependence,
and removes one whole failure mode.

---

## Procedure (what Josh does in-game)

### Pre-session checklist (Claude does, before Josh starts)

- [ ] Run the target-selection script above; confirm cid → name mapping
- [ ] Verify station-mods has v8.2.1 DLL with audio_cue_lead_ms=150 INI
- [ ] Clear stale `parry-tell-probe.csv` (per CLAUDE.md, OK to delete)
- [ ] Note current line count of `parry-tell-probe.boot.log` — save as
      `runs/session-<date>-bootlog-start-line.txt`
- [ ] Note current line count of the `*.predictions.jsonl` file (which one
      is configured) — save as `runs/session-<date>-jsonl-start-line.txt`
- [ ] Confirm OBS Mic track is on a SEPARATE audio track from desktop audio
      (OBS → Settings → Audio → Track 1 = desktop, Track 2 = mic, OR use
      "Audio Output Capture" + "Audio Input Capture" separated)
- [ ] **Clone Paramdex schema** (read-only research, no game-file extraction):
      `git clone https://github.com/soulsmods/Paramdex.git /tmp/paramdex` —
      read `/tmp/paramdex/ER/Defs/AtkParam.xml` myself to verify or refute
      the Codex hyperarmor claim from earlier today.

### Recording setup (Josh does)

- [ ] OBS configured: 1080p60 capture of Elden Ring window. **Two audio
      tracks**: mic on its own track, desktop audio on its own track. Saves
      mixed output but keeps the source tracks separable (OBS Settings →
      Advanced → File path: MKV recommended for multi-track; convertible to
      MP4 after the fact). The reason: Whisper transcription of "L2 now" and
      "stagger" gets badly confused when boss roars / sword clashes step on
      Josh's voice on the same track.
- [ ] System volume confirmed ≥ 50%
- [ ] **One single recording for the whole session.** Don't split files —
      fragments the alignment work.

### In-game session

**Step 0 — Session boot anchor (no F11 toggles in Step 0):**
- [ ] Start OBS recording. Wait 5 seconds for it to stabilize.
- [ ] Say aloud the wall-clock time you see on screen / phone:
      "Recording start, fifteen forty-two on May fifteenth" (whatever it
      actually is). Be clear and slow.
- [ ] This is the human-readable wall-clock anchor. The actual cross-reference
      anchor comes from the boot log's most recent boot banner line, which
      has a precise timestamp — we use that, but the spoken time is a sanity
      check.
- [ ] **Do NOT press F11 here.** F11 is for combat captures (Step 2 only).

**Step 1 — Solo grunt sample (default-target: a low-tier humanoid enemy with
known DB coverage, e.g., Soldier of Godrick, confirmed in pre-session):**
- [ ] Find the chosen enemy. Stand still and let them attack you.
- [ ] Say aloud: **"Test 1, [enemy name], no F11."**
- [ ] **Quota:** observe 6-10 swings minimum. Per-swing protocol:
      1. Before swing: say nothing (they're idling).
      2. As swing starts: say **"swing"** to mark the visual start.
      3. If a cue fires: it fires; you don't need to call it (cue is on the
         desktop audio track).
      4. After swing resolves: say **"hit me"** (you took damage no L2) /
         **"missed me"** (whiff) / **"L2 stagger"** (parry landed) /
         **"L2 no stagger"** (caught damage no riposte) /
         **"L2 too late"** / **"L2 too early"**.
- [ ] Mix the L2 attempts: at least 3 swings let-them-hit-you (no L2) and at
      least 3 swings with deliberate L2.
- [ ] Say **"end test 1"** when done.

**Step 2 — Medium enemy with deliberate F11 capture (Crucible Knight or
chosen mid-tier from DB-confirmed list):**
- [ ] Find the target.
- [ ] Say aloud: **"Test 2, [enemy name]. F11 arming now."** Then press F11
      ONCE to arm. The boot log will record "F11: armed" with a wall-clock
      timestamp — that's our high-precision anchor for this segment.
- [ ] Run a full engagement (~3-5 minutes). Per-swing protocol same as Step 1.
- [ ] **Quota for this segment**: at least 15 swings observed. We need
      multiple attempts per move type because some moves only appear every
      30-60 seconds in the moveset rotation. If the enemy dies before quota
      hit, run back to spawn and re-engage; the F11 stays armed.
- [ ] If a cue fires on a non-sword move (shield bash, tail, stomp,
      jumping attack, grab): say **"false positive [move name]"**.
- [ ] If a sword swing (or any move you'd expect to be parryable per the
      wiki) happens with NO cue: say **"missed cue, [move]"**.
- [ ] When done: say **"end test 2, F11 disarming"** and press F11 ONCE to
      disarm. Bin capture stops.

**Step 3 — Beast/non-humanoid validation (c3010 family or equivalent
non-humanoid from the DB):**
- [ ] Find the target. Say **"Test 3, [enemy name], no F11."**
- [ ] Quota: 6-10 swings, mixed L2 attempts.
- [ ] Same per-swing callouts.
- [ ] Say **"end test 3"** when done.

**Step 4 — Boss attempt (your choice — Margit, Tree Sentinel, or whoever
matches your save state):**
- [ ] Travel to boss. Say **"Test 4, [boss name], F11 arming now"** and press
      F11 ONCE.
- [ ] Run the fight. **Quota: at least 25 swings observed across the fight.
      If you die first, run back; if you kill before quota, that's fine and
      we note it.**
- [ ] Per-swing callouts same as Step 2.
- [ ] When done (win or quit): say **"end test 4, F11 disarming"** and press
      F11 ONCE.

**Step 5 — Wrap:**
- [ ] Return to Roundtable / Grace.
- [ ] Say aloud: "Session end, [current wall-clock time]."
- [ ] Stop OBS recording.
- [ ] Save the recording to `/srv/shared/ER Mod/session-<date>.mkv` (or .mp4
      after conversion).

### How long is this session?

Realistic estimate: **45-75 minutes of recording** for the four test steps.
Set aside ~2 hours total including setup, travel between targets, and any
re-engagement after dying. **You can split the session into "Tests 1-3"
and "Test 4 (boss)" as two separate recordings if needed** — each recording
must START with its own wall-clock anchor (Step 0 callout). Multiple
recordings is fine; mid-recording stop-and-resume is NOT — that breaks the
ts_ms timeline.

---

## Frozen settings for the session

These do not change during the session. Lock them in BEFORE Step 0:

| Setting | Value | Lives at |
|---|---|---|
| `audio_cue_lead_ms` | **150** | `/mnt/station-mods/parry-tell-probe.ini` |
| `target_filter_enabled` | **true** | same |
| `audio_cue_enabled` | **true** | same |
| `prediction_decision_log_enabled` | **true** | same |
| DLL version | **v8.2.1** | `/mnt/station-mods/parry-tell-probe.dll` |
| Audio cue WAV | **diagnostic Pop Click** (embedded in DLL) | embedded |

Document these in `runs/session-<date>-config.md` at session start.
**Do NOT change settings mid-session.** If something feels off, stop the
recording, log the change, start a new recording with new settings.

---

## What Claude does after the session

### Step A — Pull captures

- [ ] Read the OBS file from `/srv/shared/ER Mod/` — note duration via
      `ffprobe`. Don't load whole file.
- [ ] **SMB perf rule**: copy `predictions.jsonl` from
      `/mnt/station-projects/elden-ring/logs/` (or wherever the smoke INI
      pointed `log_dir`) to local `/tmp/` before any Python parsing.
- [ ] Copy `parry-tell-probe.boot.log` (full file — it's small) and
      `parry-tell-probe.csv` (if F11 captures landed any) to `/tmp/`.
- [ ] Extract the two audio tracks separately:
      `ffmpeg -i session.mkv -map 0:a:0 desktop.wav -map 0:a:1 mic.wav`
- [ ] Slice JSONL and boot log to session-start line offsets recorded
      pre-session.

### Step B — Build the alignment timeline

- [ ] **Anchor strategy (corrected):**
      1. The boot log has wall-clock timestamps. Find the most recent
         `=== boot ===` line after the session-start offset. Its
         wall-clock time = `ts_ms=0` point.
      2. JSONL events have `ts_ms` (session-relative). Add to the boot
         wall-clock to get absolute wall-clock for each JSONL event.
      3. OBS recording has wall-clock (file mtime = recording end; OBS
         filename timestamp = recording start). Convert OBS frame → wall-
         clock via the recording start.
      4. Common clock = wall-clock. Tie everything to that.
- [ ] **Verification anchors:** find F11 boot-log events. Each F11 press
      has wall-clock. Cross-check: F11-press timestamp in boot log should
      match within ±1 second to the spoken "F11 arming/disarming now"
      moment in the mic track. If drift > 1s, flag clock skew.
- [ ] Build a CSV: every cue fire event from JSONL (action contains "fire"),
      with computed wall-clock + video offset, target cid, target_match,
      anim_id, window timing.
- [ ] Transcribe the mic track ONLY (clean audio, no game-noise overlap)
      with Whisper. The two-track recording is the reason this works:
      desktop audio with game sounds is on its own track, never mixed in.

### Step C — Cross-reference

Produce `runs/session-<date>-alignment.md` with:

1. **Coverage table:** For each distinct (cid, anim_id) Josh engaged, did
   the predictor fire? Did it suppress (target_filter or other)? Did it
   silently skip?
2. **Stagger outcome table:** For each fire where Josh said "L2 stagger"
   or "L2 no stagger", classify by move type (grab / heavy / light /
   jumping / unknown).
3. **False positive list:** Fires where Josh said "false positive [move]" —
   what was the move, what anim_id did the predictor read, why did it
   match.
4. **Missed cue list:** Swings where Josh said "missed cue [move]" — was
   the attack absent from `parry_data.json`? Did the predictor see the
   anim_id but suppress? Did the read just fail (predictor saw nothing
   for that ts range)?
5. **Quota check:** Per-cid swing counts vs target. If quotas not met,
   flag which outcomes are inconclusive and explicitly say so.

### Step D — Decide next phase

The four-outcome decision tree (next section) governs what we do next.

---

## Pre-specified outcomes and follow-up procedure

Reading the data should drop us into one of these branches. Each branch
has a defined next step.

**(1) Cue works broadly, over-firing dominated by grabs.**
- Definition: Cues fire on multiple cids (≥3 distinct cids with fires).
  False positives concentrated in moves Josh tagged as grabs.
- Next: **Phase 4.3.2** — extend TAE parser to capture event type 304
  (ThrowAttackBehavior). Add filter at `tools/build_parry_db.py` stage to
  exclude windows on animations that contain ThrowAttackBehavior events.
- Time: ~2-3 hour task.

**(2) Cue works broadly, over-firing dominated by heavies/multi-parry.**
- Definition: Cues fire on multiple cids. False positives are NOT grabs;
  they're heavy attacks or "L2 caught damage but no stagger" on bosses
  that wiki says require 2-3 parries.
- Next: **Phase 4.3.3** — regulation.bin extraction + AtkParam join.
  Sized as a full session.

**(3) Cue only works on 1-2 cids, others silent.**
- Definition: Despite Josh engaging multiple DB-covered cids, fires
  concentrate on one or two with the rest receiving zero fires even when
  they swung.
- Next: **STOP filter work.** Coverage investigation becomes priority.
  **Specific follow-up diagnostics:**
  - Check JSONL `action` field on the silent cids — is the predictor
    seeing them at all? (Should see ACTION_NO_KEY, ACTION_BEFORE_LEAD,
    etc. even when no fire.)
  - Check `anim_id` field — is it reading 0 (memory read fail) or a real
    anim_id that's missing from the DB?
  - Check `resolved_cid` vs `raw_cid` — is the resolver mapping silent
    cids correctly?
  - If anim_id reads as 0 on silent cids: read-side bug, return to Phase
    4.0/4.1 territory.
  - If anim_id reads correctly but doesn't match DB: DB coverage gap,
    re-extract TAE for those specific cids.
  - If anim_id matches DB but no fire: predictor logic bug.

**(4) Cue fires on multiple cids but timing varies.**
- Definition: Cue fires correctly on multiple cids but lead time feels
  systematically off (early on some, late on others).
- Next: **Phase 4.3.4** — per-cid-family lead-time calibration. Probably
  build a small calibration tool to estimate optimal lead per cid from
  the collected JSONL.

**(5) Mixed results / quotas not met.**
- Definition: data inconclusive across the decision tree.
- Next: a second data-gather session focused on the specific cids/moves
  that didn't get enough data.

---

## Whisper transcription details (pre-empt the risk)

Codex flagged Whisper risk in mixed-audio. The two-track separation
removes the obvious failure mode (game audio bleed). But Whisper still
has known weaknesses:

- **Short utterances (1-2 words like "swing", "stagger") get dropped
  or misrecognized.** Mitigation: Josh enunciates and adds a beat between
  callouts. We'll find out post-session if this is a problem.
- **Phrase consistency**: the cheat sheet (next section) keeps callout
  phrases SHORT but unique so transcription accuracy stays high.
- **Fallback if Whisper fails on the session**: I can transcribe by hand
  from the mic.wav track. A 60-min session at 6-8 callouts per minute
  is 360-480 callouts, each ~1-3 seconds. Manual transcription is ~2-3
  hours. Acceptable backstop.

---

## Verbal callout cheat sheet (Josh prints/reads this)

**Minimum viable set — 6 callouts:**

| Situation | Say this |
|---|---|
| Starting a test segment | "Test [N], [enemy name]" |
| Each visible swing starts | "swing" |
| You let it hit you (no L2) | "hit me" or "missed me" (whiff) |
| You tried L2 and the enemy staggered | "L2 stagger" |
| You tried L2 but caught damage no stagger | "L2 no stagger" |
| End of a test segment | "end test [N]" |

**Optional but useful:**

| Cue fired on a non-parryable move | "false positive [move]" |
| Visible parryable swing with no cue | "missed cue [move]" |
| Arming F11 (only in Test 2 / Test 4) | "F11 arming now" / "F11 disarming" |

That's it. 6 phrases minimum, 9 total. Forget a callout? Narrate it
1-2 seconds late — Whisper alignment can handle a few-second slop on
phrase timing.

---

## Co-op safety / risk review

Per CLAUDE.md, project rules:

- Recording happens with the mod already loaded — no change to mod attach
  surface, no new memory writes, no new code paths. **Co-op safety
  unchanged.**
- Paramdex clone is purely a GitHub schema download — does not extract
  or modify game files. **Safe.**
- F11 already in the mod; this session uses it as designed. **Safe.**
- OBS is a standard recording tool with no game-process interaction
  (game capture in OBS uses GPU-side hooks, no memory access to game
  process). **Safe.**

No new co-op or anti-cheat surface area introduced.

---

## What this does NOT do

- Does not extract regulation.bin. We use existing DB + new behavioral
  ground truth to figure out where to invest extraction effort next.
- Does not change predictor or audio code. Pure measurement session.
- Does not commit to throw / hyperarmor / Paramdex paths yet.

---

## Open items not solved by this plan

1. **F11 doesn't fire on `c0000` (player skeleton).** The DB has 4116
   windows on c0000 but these are for the PLAYER side. Future work:
   distinguish "this is a player anim, ignore" vs "this is a boss anim
   I should cue on" — but this is not in scope for the session itself.

2. **What if no Crucible Knight is reachable in Josh's save?** Pick
   another DB-covered humanoid mid-tier (the pre-session script will
   list options). Falling back to Soldier of Godrick only is acceptable
   if necessary — we lose the "varied moveset" data point but keep
   coverage data.

3. **Audio cue fires from PlaySoundW on a Windows System sounds channel,
   not in the game's audio session.** This means it WILL appear on OBS
   desktop audio capture (good), but won't be in the game's own audio
   mixer (irrelevant for this session).
