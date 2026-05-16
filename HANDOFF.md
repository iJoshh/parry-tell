# HANDOFF — parry-tell-probe

**Date:** 2026-05-15 evening (America/Chicago)
**Session tag:** session-close/2026-05-15-<HHMMSS> (created in step 5)
**Branch:** main, working tree clean except `runs/` (new dir from this session)
**Unpushed:** 15+ commits (nightly cron at 00:00 CDT)

---

## Where we left off

**Phase 4.2 audio cue is FUNCTIONALLY COMPLETE and AUDIBLE in-game.**
Three smoke runs this session, two of them audible to Josh, one with
tuned INI confirming the right design knobs. The mod now produces a
real-world parry cue that Josh hears during combat. Months of work
converged tonight.

Current settings (live on station):

- DLL: v8.2.1 (SHA `71fb0f3b...`), embedded WAV is the diagnostic-loud
  Pop Click (Pixabay/SoundReality CC0, +200% gain, 60ms)
- INI: `audio_cue_lead_ms = 200`, `target_filter_enabled = true`,
  `audio_cue_enabled = true`
- Hook: data_collection_mode=true (so F11-arm + bin/csv capture still
  works for offline analysis)

---

## Accomplishments this session

### Phase 4.2 base wiring (committed `bd08154` + `61f6981`)

- `probe/audio.h` + `probe/audio.cpp` — AudioCue module
- `probe/resource.h` + `probe/parry-tell-probe.rc` — embedded WAV resource
- `probe/probe.vcxproj` — winmm.lib + audio.cpp + resource compile entry
- `probe/probe.cpp` — INI parsing, FireAudioCue call in
  WritePredictionDecision, Init/Shutdown plumbing, g_dllModule capture,
  BootLog/LogF linkage adjustment for cross-TU calls
- `probe/v6/parry-tell-probe.ini.smoke` — `[audio]` section added

### First audible run (committed `823c299`)

- v8.2.0 built and deployed: 23 cue decisions fired correctly on cid 4311
  per JSONL, but Josh heard nothing — Windows master volume at 18% +
  sword-sample cue blending with sword ambient.
- Diagnosed via mixer screenshot + JSONL action distribution.
- Generated diagnostic Pop Click WAV: trimmed Pixabay source to 40-100ms
  span, normalized, +200% gain, 5/15ms fades, mono 44.1kHz 16-bit PCM.
- v8.2.1 deployed at 20:58 CDT. **First audible cue confirmed.**

### Phase 4.2 tuning (committed `1f28aa6`)

- `audio_cue_lead_ms`: 50 → 200ms (50ms minus Windows audio latency felt
  too late to react)
- `target_filter_enabled`: false → true (eliminated 3 spurious cues from
  non-targeting cid 4070 enemy)
- Third smoke run confirmed: 4 fires, all `target_match=True`, lead times
  178-199ms (vs 200ms target), zero spurious cues, 2,822 non-targeted
  decisions correctly suppressed by the filter.

### Documentation work

- `TODO-PHASE-4.2-FOLLOWUPS.md` — captures ~32 minutes of rolled-back
  work: two-cue mode design + audio file selection that was scope-creeping
  beyond session bounds. Full 11-step implementation plan + SND_NOSTOP
  tradeoff analysis preserved for next session.
- `runs/v8.2.1-tuned-observations.md` — per-run analysis + L1/L2
  terminology correction binding on future sessions.
- `~/.claude/CLAUDE.md` — added documentation of the
  `\\192.168.30.10\shared` ↔ `/srv/shared` file-drop share (Josh's
  primary path for handing files into the VM from Windows).

### Audio file archive (3 independent backups)

- `~/parry-tell-audio-archive/candidates/` — 28 candidate WAV/MP3 files
  (swordclash variants, alert variants, synth variants, clean + diagnostic
  Pop Click, source MP3s)
- `/mnt/station-mods/parry-tell-audio-candidates/` — Windows-side mirror
- `/tmp/parry-tell-rollback-stash/` — session-temp safety (will clear on
  reboot, not load-bearing)

---

## CRITICAL terminology correction (binding all future sessions)

**Parry in Elden Ring = L2 (weapon art), NOT L1 (block).**

- L1 raises shield. Mitigates damage. Does NOT stagger. Will always
  "graze and chip damage" against any incoming attack.
- L2 with a parry-capable weapon art (Buckler Parry, Carian Retaliation,
  Storm Wall) catches the attack during its active frames, staggers the
  enemy, opens R1 riposte.

The 33-67ms parry windows in `data/parry_data.json` are L2-active-frames
windows, not L1-block windows. The mod tells you WHEN to press L2.

Previous Claude conversations consistently said "press L1 to the cue" —
this was wrong. Documented in `runs/v8.2.1-tuned-observations.md`.

---

## Next steps (priority order)

1. **Gather more parry-success data.** Josh fights different enemies
   (more cid families than just 4311) with deliberate L2 attempts.
   Per-anim "cue heard → boss staggered y/n" data. Especially important:
   re-test anim 4003103 with intentional L2 timing to distinguish
   "Josh-timing-was-off" from "DB has a false positive."
2. **Decide on production audio.** Diagnostic Pop Click is intentionally
   aggressive; consider swapping to the un-amplified
   `popclick-pixabay-clean-60ms.wav` (preserved in archive) now that
   audibility is proven. Or pick something else from the archive
   entirely.
3. **Two-cue mode (`audio_cue_parry_now`).** Per `TODO-PHASE-4.2-FOLLOWUPS.md`.
   Adds a second cue fire at window_open paired with the existing
   predictive fire. Design fully captured; ready for implementation
   when Josh decides to pick it up.
4. **Phase 4.4 regression harness.** Build `tools/verify_predictions.py`
   to replay JSONL + binary captures and validate predictor decisions
   offline. Lets us audit anim 4003103 (and any other suspect anims)
   from data rather than gameplay.

---

## Open questions for Josh

- **Does anim 4003103 have a real parry window?** Subjective gameplay
  this session was ambiguous (Josh's L1/L2 confusion was a factor; might
  not have been pressing parry button consistently). Next-run goal: try
  L2 deliberately and record per-anim outcomes.
- **Production audio file final?** Pop Click diagnostic is loud and
  aggressive — fine for testing but might be annoying long-term. Quieter
  variant in archive, or pick a different sound entirely.
- **Lead time final?** 200ms felt "much better" this session. Worth
  trying 300 or 400 once Josh has more reps, especially for slow attacks
  where additional lead is essentially free.

---

## Tried and ruled out (this session)

| Approach | Verdict | Reason |
|---|---|---|
| 200ms swordclash WAV as production cue | REFUTED | Blended with sword ambient; inaudible during combat |
| Single-knob `audio_cue_parry_now` two-cue mode | DEFERRED | Implementation scope-creeping; rolled back mid-session, design preserved in TODO doc |
| `audio_cue_lead_ms = 50` | REFUTED | After ~25ms Windows audio latency, cue lands ~25ms before window-open — too late to react |
| `target_filter_enabled = false` in normal use | REFUTED | 3 spurious cues from non-targeting enemy in first audible run; filter is now default-on |

Carry-forward refuted hypotheses from prior sessions all still valid.
See `runs/v8.2.1-tuned-observations.md` for additional refutations
specific to this run.

---

## Files modified this session

| File | Change |
|---|---|
| `probe/probe.cpp` | Phase 4.2 wiring (Config fields, INI parser, FireAudioCue call site, Init/Shutdown plumbing, DllMain g_dllModule capture, BootLog/LogF linkage) |
| `probe/audio.h` | NEW — AudioCue module interface |
| `probe/audio.cpp` | NEW — PlaySoundW implementation with embedded-resource + override-file paths |
| `probe/resource.h` | NEW — IDR_AUDIO_CUE_WAV definition |
| `probe/parry-tell-probe.rc` | NEW — resource script embedding the WAV |
| `probe/assets/audio_cue.wav` | NEW — embedded cue (Pop Click diagnostic, +200% gain, 60ms) |
| `probe/assets/README.md` | NEW — WAV spec |
| `probe/probe.vcxproj` | winmm.lib + audio.cpp + resource compile |
| `probe/v6/parry-tell-probe.ini.smoke` | `[audio]` section, lead 50→200, target_filter false→true |
| `TODO-PHASE-4.2-FOLLOWUPS.md` | NEW — rolled-back work archive |
| `runs/v8.2.1-tuned-observations.md` | NEW — per-run analysis + L1/L2 terminology correction |
| `CHANGELOG.md` | Phase 4.2 entry prepended |
| `PHASE4-PLAN.md` | Session Log appended |
| `~/.claude/CLAUDE.md` | `/srv/shared/` documentation (different repo) |

---

## Services / processes

- **SSH service on station:** RUNNING at session end. Josh can stop with
  `Stop-Service sshd` in elevated PowerShell when done. Not needed for
  VM-side work between sessions.
- **SMB mounts:** `/mnt/station-projects/` (RO) and `/mnt/station-mods/`
  (RW) live via Tailscale automount. Verify with `mount | grep station`.
- **Probe on station:** v8.2.1 DLL at `/mnt/station-mods/parry-tell-probe.dll`.
- **DLL backup chain on station:** v7.3, v8.0, v8.1, v8.1.2, v8.1.3, v8.2.0
  preserved as `.dll.*-backup` files for one-cp rollback.

---

## Git state at session close

- Branch: `main`
- Working tree: clean (after this session-close run)
- Unpushed commits: 15+ (nightly cron pushes at 00:00 CDT)
- Latest tag: `session-close/2026-05-15-<HHMMSS>` (this session's tag)
- Previous session-close: `5385568` (afternoon — Phase 4.1 end-to-end proven)

Recent commits:
```
1f28aa6  phase 4.2 tune — lead 50→200ms, target_filter on
823c299  phase 4.2 first audible run — diagnostic pop click
9f6d128  phase 4.2 — TODO doc for rolled-back audio + parry-now
61f6981  phase 4.2 — [audio] INI section + smoke lead=50
bd08154  phase 4.2 — wire PlaySoundW audio cue
5385568  Phase 4.1 end-to-end proven (afternoon session)
```

---

## Pickup prompt for next session

> Phase 4.2 audio cue is functionally complete and audible. Mod produces
> real-world parry tells in combat. Tunings settled this session:
> `audio_cue_lead_ms = 200`, `target_filter_enabled = true`, Pop Click
> diagnostic WAV embedded.
>
> **CRITICAL**: parry in Elden Ring is L2 (weapon art), NOT L1 (block).
> All future conversations must use L2 terminology when discussing
> player response to the cue.
>
> Next: gather per-anim parry-success data across more enemies. The
> ambiguous anim 4003103 needs deliberate L2 attempts to confirm whether
> its DB window is real. Two-cue mode (`audio_cue_parry_now`) design
> ready in `TODO-PHASE-4.2-FOLLOWUPS.md` whenever Josh wants to ship it.
> Phase 4.4 regression harness still pending; helps audit DB suspects
> from data instead of gameplay.
>
> Audio archive at `~/parry-tell-audio-archive/` has 28 candidate files
> for any future sound swap. Source MP3 for the production Pop Click is
> there too in case re-trimming or re-amplification is desired.
