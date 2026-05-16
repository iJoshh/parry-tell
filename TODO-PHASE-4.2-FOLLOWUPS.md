# Phase 4.2 follow-ups — rolled back on 2026-05-15 20:35 CDT

Two work streams that started after Phase 4.2 base wiring (commit `61f6981`)
were unfinished + over-scoped for this session. Rolled back on Josh's call;
preserved here for next-session pickup.

Rollback span: ~32 minutes of work between 20:02–20:34 CDT. Four commits
discarded (`635b90e`, `69c32b2`, `502cb03`, `acb45cd`) plus uncommitted
in-flight edits to `probe/probe.cpp` and `probe/audio.cpp`. None of the
discarded work was pushed to GitHub.

Current state of the project: **Phase 4.2 base audio wiring is intact and
ready for first live build.** All this doc captures is OPTIONAL follow-up
work that can land after the first audible smoke test passes.

---

## TODO 1 — Pick the production audio cue (Josh)

### Decision Josh landed on (preserve)

> "Sword one when we come back."

Josh picked **`swordclash-80ms-faded.wav`** (or one of its siblings — 120ms /
200ms duration variants) as the production cue. Decision still soft — Josh
hadn't finalized between 80ms (sharpest, just the strike), 120ms (strike +
start of resonance), and 200ms (full hit with decay hint). To finalize,
preview all three in-game and pick by feel.

### Where the audio files live

THREE independent backups, all 28 files preserved through the rollback:

| Location | Survives | Purpose |
|---|---|---|
| `~/parry-tell-audio-archive/candidates/` | reboot, git ops | Primary on-VM backup |
| `/tmp/parry-tell-rollback-stash/` | this session only | Session-temp safety |
| `/mnt/station-mods/parry-tell-audio-candidates/` | independent of VM | Windows-side, double-click to preview |

The exact swordclash variant Josh would pick:
- `swordclash-80ms-faded.wav` (7.1 KB) — sharpest, just the strike transient
- `swordclash-120ms-faded.wav` (10.6 KB) — strike + start of resonance
- `swordclash-200ms-faded.wav` (17.7 KB) — full hit with decay hint

All are mono, 44.1 kHz, 16-bit PCM, already spec-compliant. Drop chosen file
at `probe/assets/audio_cue.wav` (replace the current 50ms silent placeholder).

### Source attribution (required for release)

SwordClash01 by 32cheeseman32 — `https://freesound.org/people/32cheeseman32/sounds/180820/`
— licensed CC-BY 4.0 (`https://creativecommons.org/licenses/by/4.0/`). Trim +
fade + format-conversion is a derivative work; CC-BY 4.0 still applies.

For v0.1.0 release notes / LICENSES.md, add:

```
parry-tell-probe audio cue
==========================
Derived from "SwordClash01" by 32cheeseman32
  (https://freesound.org/people/32cheeseman32/sounds/180820/),
  licensed under CC BY 4.0
  (https://creativecommons.org/licenses/by/4.0/).
Modifications: trimmed to <CHOSEN_DURATION> ms, applied 5 ms fade-in /
  15 ms fade-out, converted to mono 44.1 kHz 16-bit PCM WAV.
```

### Other candidates also preserved

If swordclash doesn't land well in-game (gets lost in actual sword-clang
ambient — a real risk we'd only discover in live test), the 19 alert
candidates in `~/parry-tell-audio-archive/candidates/alerts/` are
alternatives. Best alternatives ranked:

1. `synth-mgs-ascend-sine-160ms.wav` — clean ascending fifth, NO attribution
   needed (we synthesized it from scratch in this session)
2. `alert-dslr-double-130ms.wav` — sharp DSLR-shutter double beep, CC0
3. `alert-beep-up-287ms.wav` — two rapid ascending beeps, CC0

The picking-guidance README is at
`~/parry-tell-audio-archive/candidates/alerts/README.md` — full attribution
templates per license type, recommended audition order, picking rationale.

### Resume-from-here steps (next session)

1. Tell Claude which file you picked from the archive
2. Claude runs:
   ```bash
   cp ~/parry-tell-audio-archive/candidates/<chosen>.wav \
      ~/claude/elden-ring/probe/assets/audio_cue.wav
   ```
3. Rebuild v8.2.0 DLL via SSH MSBuild
4. Deploy via SMB, live smoke test
5. If it sounds good: commit `feat(probe): ship swordclash audio cue` + add
   LICENSES.md with the CC-BY attribution block above
6. If it doesn't cut through ambient: try alternative from list above

---

## TODO 2 — Two-cue mode (`audio_cue_parry_now`)

### What Josh wants

A second audio cue at `window_open_s` that pairs with the existing predictive
cue at `window_open_s - audio_cue_lead_ms`. Inter-cue gap = `audio_cue_lead_ms`
(single knob, not two). Tunable per-fight without re-rendering audio.

Naming: `audio_cue_parry_now` (the INI key for the second cue). Cue-1 is
"predict", cue-2 is "parry NOW".

### Why this is a real design

A two-fire pattern creates rhythm — and the *interval* between two notes
is faster to perceive through game ambient than the *amplitude* of one
transient. Standard alert-design wisdom (MGS `!`, Pokemon level-up tones,
etc.). And it lets Josh tune the rhythm via the existing `audio_cue_lead_ms`
knob without re-rendering audio. Better than baking a two-note WAV.

### Design landed during session (preserve verbatim)

**Single knob.** `audio_cue_lead_ms` doubles as inter-cue gap. If lead =
120ms, notes land 120ms apart. Auto-disable two-cue when `lead_ms <= 0`
(the two fires would collide or invert order).

**Timing from START of file playback** (PlaySoundW fires immediately,
asynchronously; we don't wait for the WAV to finish).

**Latch-based scheduling, not timer-based.** Each poll checks "is anim_time
past `window_open_s` AND we haven't fired cue-2 yet?". This self-corrects
when the boss cancels the animation between cue-1 and cue-2 — the latch
state machine sees the anim_id change and resets, so cue-2 doesn't trigger
phantom-style.

**Independent latch from cue-1.** Two state bits per window:
- `consumed_windows` — cue-1 ("predict") latch (existing)
- `consumed_parry_now_windows` — cue-2 ("parry NOW") latch (new)

Both cleared together on every instance reset; they don't share state
otherwise. Both can fire on the same poll when `lead_ms = 0` or when the
boss caught us late.

### Implementation plan (verbatim from rolled-back work)

1. **Config struct** (`probe.cpp:557` area): add
   `bool audio_cue_parry_now = false;` — **default OFF** so existing INIs
   with positive `audio_cue_lead_ms` don't silently switch behavior on DLL
   upgrade. Opt-in via the new smoke INI line below.
2. **INI parser** (`probe.cpp:694, 720` area): parse the new key in both
   `[prediction]` (backward compat) and `[audio]` (canonical) sections
3. **`PredictionConfig` struct** (`probe.cpp:1242`): add the same field
4. **`BossPredictState` struct** (`probe.cpp:1146`): add
   `uint32_t consumed_parry_now_windows = 0;` next to `consumed_windows`
5. **`BumpInstanceSeqIfNeeded`** (`probe.cpp:1280-1354`): reset
   `consumed_parry_now_windows = 0` in both the ever_seen=false init path
   AND the reset path (mirror the existing `consumed_windows = 0` lines
   at 1290 and 1348)
6. **`PredictionAction` enum** (`probe.cpp:1174`): add
   `ACTION_FIRE_PARRY_NOW = 10`
7. **`PredictionActionName`** (`probe.cpp:1187`): add case returning
   `"fire_parry_now"`
8. **`EvaluatePredictionTick`** per-window loop (`probe.cpp:1559-1584`):
   after the existing `EvaluatePredictionWindow` + `WritePredictionDecision`
   call, add a second evaluation block that:
   - Checks `cfg.audio_cue_parry_now == true`
   - **Checks `cfg.audio_cue_lead_ms > 0`** — two-cue mode auto-disables
     for non-positive lead (lead=0 would put both cues on the same poll;
     lead<0 would invert the rhythm). This guard belongs INSIDE step 8,
     not deferred to runtime config validation.
   - Checks `i < MAX_WINDOWS_PER_ANIM_LATCH`
   - Computes `bit = 1u << i`
   - Checks `(st.consumed_parry_now_windows & bit) == 0` (not already fired)
   - Checks `anim_time_s` is in `[window.open_s, window.close_s]` (NaN-safe)
   - Checks target filter (if enabled, requires `target_known && target_match`)
   - On all-true: set the bit, emit a `PredictionDecision` with
     `action = ACTION_FIRE_PARRY_NOW`, call `WritePredictionDecision`,
     increment emitted count
9. **`WritePredictionDecision`** (`probe.cpp:~1700`): extend the FireAudioCue
   predicate to include `ACTION_FIRE_PARRY_NOW` alongside `ACTION_FIRE`,
   `ACTION_LATE_INSIDE_WINDOW`, `ACTION_LATE_TARGET_SWITCH`
10. **`audio.cpp::FireAudioCue` — DECIDE BEFORE IMPLEMENTING:**

    There's a real tradeoff here the critic caught. The chosen swordclash
    WAVs are 80/120/200ms. The smoke INI ships `audio_cue_lead_ms = 50`.
    With `SND_NOSTOP`, cue-2 (firing 50ms after cue-1) lands while cue-1
    is still mid-playback → cue-2 silently drops → two-cue mode collapses
    to one audible cue. That defeats the feature.

    Three options, pick before coding:

    **A. Don't use SND_NOSTOP.** Accept that cue-2 will clip cue-1 mid-
    playback. With short WAVs (60-80ms) and `lead_ms = 80+`, clipping is
    barely audible. Cleanest code, predictable rhythm. **Recommended.**

    **B. Use SND_NOSTOP + require `audio_cue_lead_ms >= wav_duration_ms`.**
    Adds startup validation: when `audio_cue_parry_now=true`, refuse
    config where `lead_ms < ceil(wav_duration_ms)`. WAV duration is
    knowable from the RIFF header at Init time. Heavier; predictable but
    coupled.

    **C. Use a real mixer (WASAPI / XAudio2) instead of PlaySoundW.**
    Right answer architecturally but probably overkill for v0.1.0. Defer
    to v0.2.0.

    **My pick: A.** Do NOT add SND_NOSTOP. Cue-2 clipping cue-1 mid-tone
    is the *intended* rhythm anyway — the perceived experience is "ding-
    DING" not "ding, ding" with two complete tones. Test in-game first;
    only revisit if Josh hates the sound.

    Whichever option lands: revisit the "disable audio on first FALSE"
    behavior in audio.cpp. With option A, the current logic stays correct
    (FALSE = genuinely broken). With B or C, FALSE becomes ambiguous and
    needs the `g_audio_first_fire_done` flag described below.
11. **Wire `g_cfg.audio_cue_parry_now` → `pred_engine_cfg`** (`probe.cpp:5244`
    area): add the field copy alongside the existing
    `pred_engine_cfg.audio_cue_lead_ms = g_cfg.audio_cue_lead_ms;`

### Unfinished subtlety (the reason we rolled back)

The `SND_NOSTOP` decision in step 10 above interacts with audio.cpp's
"disable audio on first FALSE" logic. Summary:

- **Option A** (recommended, no SND_NOSTOP): FALSE keeps meaning "genuinely
  failed". Existing log-once-then-disable behavior stays correct. **No
  audio.cpp change needed.**
- **Option B/C** (with SND_NOSTOP or real mixer): FALSE becomes ambiguous.
  Need either (a) `g_audio_first_fire_done` flag so only the very first
  FALSE disables, OR (b) drop the disable-on-FALSE behavior entirely and
  let PlaySoundW retry forever (microseconds per call; not worth tracking
  state).

If we end up picking B/C in step 10, do (b) — simpler, fewer state bits,
the disable behavior was a defensive belt-and-suspenders that we don't
strictly need.

### Edge cases the design handles (preserve thinking)

1. **Short animation where window opens before lead-time elapses**:
   By the time we observe `anim_id` and look up the window, `anim_time_s`
   may already be ≥ `window_open_s - lead_ms / 1000`. Cue-1 emits
   `ACTION_LATE_INSIDE_WINDOW` (already exists). Cue-2 emits
   `ACTION_FIRE_PARRY_NOW` when anim_time reaches window_open as normal.
   Net effect: single fire (cue-2 only) when there's no time for a
   warning. **Correct behavior — no code change needed.**

2. **Two consecutive parry windows in one animation (combo)**:
   Per-window latch bits (bit 0 vs bit 1) keep them independent.
   `consumed_parry_now_windows` mirrors `consumed_windows`. Each window
   fires its own (cue-1, cue-2) pair. No interference.

3. **Tight windows where cue-2-of-W0 and cue-1-of-W1 overlap**:
   Possible if W0 closes < `lead_ms` before W1 opens. Three fires within
   `lead_ms`. PlaySoundW with `SND_NOSTOP` silently drops the overlapping
   second. JSONL still logs the intended fires for offline review. **<1%
   of windows are this tight per the parry DB** — acceptable tradeoff.

4. **`audio_cue_lead_ms = 0`**: cue-1 and cue-2 land on the same poll.
   Two FireAudioCue calls 0ms apart. `SND_NOSTOP` drops the second — net
   effect: single audible fire. Acceptable; if Josh wants the two-cue
   pattern, `lead_ms` should be ≥ 60ms (perceptual gap minimum).

5. **`audio_cue_lead_ms < 0`** (cue fires AFTER window_open, "confirmation
   tone" mode): two-cue mode SHOULD AUTO-DISABLE. Cue-1 fires after
   window_open, cue-2 fires at window_open → order inverts. Not what
   anyone wants. Add a guard in `EvaluatePredictionTick`:
   `if (cfg.audio_cue_parry_now && cfg.audio_cue_lead_ms > 0)`.

### Smoke INI changes for Phase 4.2 follow-up

When this lands, update `probe/v6/parry-tell-probe.ini.smoke`:

```ini
[audio]
audio_cue_enabled = true
audio_cue_wav_path =
audio_cue_parry_now = true   ; NEW — two-cue mode
```

Document `audio_cue_parry_now` in PHASE4-PLAN.md INI knobs table
(currently at lines 711-720).

### Resume-from-here steps (next session)

1. Audio file in place (TODO 1 done first)
2. Apply the 11 numbered edits above to `probe.cpp` + 1 to `audio.cpp`
3. Codex review pass via `codex exec --model gpt-5.3-codex --sandbox read-only`
   (NOT the MCP wrapper — it times out; CLI works fine)
4. Address findings, commit as `feat(probe): add audio_cue_parry_now two-cue mode`
5. Build v8.2.1 (or whatever the next version number is) + live smoke test
6. Listen for the rhythm — should be two notes ~`audio_cue_lead_ms` apart
   per parry window. If `lead_ms = 50`, that's a quick double-tap. Tune
   `lead_ms` to taste afterward.

---

## What WAS preserved by the rollback

Two-commit chain (both intact, working tree clean at `61f6981`):

| Commit | What it landed |
|---|---|
| `bd08154` | Phase 4.2 base — PlaySoundW wiring, resource embedding, DllMain g_dllModule capture, FireAudioCue call in WritePredictionDecision |
| `61f6981` | Phase 4.2 INI — `[audio]` section in parser, smoke INI bumped to `audio_cue_lead_ms = 50` |

Together those two commits give us:

- `audio.h` / `audio.cpp` — `InitAudioCue` / `FireAudioCue` / `ShutdownAudioCue`
- `probe/resource.h` + `probe/parry-tell-probe.rc` — embedded resource glue
- `probe/assets/audio_cue.wav` — 50ms silent placeholder (will be replaced
  with swordclash variant per TODO 1)
- `probe/assets/README.md` — WAV spec + replacement docs
- `probe/probe.vcxproj` — `winmm.lib`, `<ResourceCompile>`, audio.cpp/.h
- All `probe.cpp` edits (Config fields, INI parser for `[audio]` section,
  `FireAudioCue` call in `WritePredictionDecision`, Init/Shutdown wiring,
  DllMain `g_dllModule` capture)
- `[audio]` INI section in `probe/v6/parry-tell-probe.ini.smoke` with
  `audio_cue_lead_ms = 50`

**Phase 4.2 is shippable as single-cue mode right now.** When TODO 1 lands
(real WAV in place), v8.2.0 can be built and live-tested as a single audible
cue per parry window. TODO 2 is a pure enhancement that can land later.

---

## Pickup prompt for next session

> Open `~/claude/elden-ring/TODO-PHASE-4.2-FOLLOWUPS.md` for full context.
> Phase 4.2 base wiring is complete at commit `61f6981`. Two follow-ups:
> (1) drop a real WAV in place of the silent placeholder — Josh picked
> swordclash, files preserved at `~/parry-tell-audio-archive/` — then build
> v8.2.0 and live-test; (2) add `audio_cue_parry_now` two-cue mode per the
> design spec above. TODO 1 is the prerequisite for the first audible smoke
> test; TODO 2 is enhancement-only. Start with TODO 1.
