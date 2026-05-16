# HANDOFF — parry-tell-probe

**Date:** 2026-05-15 (America/Chicago)
**Session tag:** session-close/2026-05-15-183948
**Branch:** main, working tree clean, 10 unpushed commits (nightly cron pushes
at 00:00 CDT)

---

## Where we are

**Phase 4.0** — DONE (target field at `ai_struct +0xC988`, prior session)
**Phase 4.1** — DONE (predictor pipeline end-to-end proven in-game 2026-05-15)
**Phase 4.2** — NEXT (wire audio cue via `PlaySoundW`)

### Phase 4.1 proof

Smoke run 4 (`smoke-20260515-183544`) with v8.1.3 DLL produced the first real
fire decisions on a live boss fight:

- 4× `fire_late_inside_window` decisions on cid 4311 → family-fallback to c4310
- 689 `no_key` decisions (anims without parry windows — expected, working)
- 615 `suppressed_post_window` + 63 `latched` (state machine working)
- Target field +0xC988 read `target_match=true` on every fire row
- Co-op safety contract held — zero crashes, zero writes, zero game disruption

This means **every piece of the pipeline is proven working** except the actual
audio. The predictor knows when to fire; we just need to make it audible.

---

## Phase 4.1 work shipped this session

Four station roundtrips, each landing working code:

| Version | DLL SHA | What changed | Smoke result |
|---|---|---|---|
| v8.0 | `39d769a9` | Initial Phase 4.1 with boss-bar enum | 0 useful decisions — bug |
| v8.1 | `860168d3` | Switched to WCM roster enumeration (4.1.1) | 1,709 dec, all no_key — bug |
| v8.1.2 | `1b747a22` | Diagnostic counters + 1Hz enemy snapshot (4.1.2) | Diagnosed root cause |
| v8.1.3 | `90068636` | Priority handles + lock-on pin + two-bucket merge (4.1.3) | **4 fire decisions** |

### Bugs discovered + fixed in iteration

1. **Phase 4.1.1**: Original `EnumerateActiveBossHandles` read from
   `CSFeManager`'s 3-slot boss-bar UI. Only healthbar-bosses appear there.
   Field enemies (Stormveil knights, Godrick Soldiers) never populate. Fix:
   walk the WCM roster (same path the capture pipeline uses).

2. **Phase 4.1.2 diagnostic insight**: Roster is NOT sorted "near-the-player
   first" — empirically falsified across 156 1Hz snapshot lines. Locked-on
   enemy was never in the first 16 entries. The 16-cap was shadowing the right
   target every tick. Stage-breakdown counters made this obvious in ~30 lines
   of code; ruled out the transient-TimeAct-pointer hypothesis that we'd
   otherwise have wasted a session on.

3. **Phase 4.1.3**: Priority-handles redesign took 4 Codex review passes. The
   in-place eviction algorithm had three subtle index bookkeeping bugs
   (priorities overwriting each other, contiguity invariant violated).
   **Solution: scrap in-place placement entirely.** Two-bucket merge
   (collect priorities + fills separately, merge with priority-first at end).
   Codex pass 4 came back clean. **Lesson: when an algorithm needs three
   review passes to fix index bookkeeping, the algorithm shape is wrong.**

### Carry-forward findings (do not re-investigate)

- `ai_struct +0xC988` target field GENERALIZES from healthbar bosses to field
  enemies (proven on cid 4311 — 1,152 target_known reads per 5s window,
  target_match=true on every fire row)
- `TIME_ACT_ANIM_ID = 0xD0` comment claims "reads 0 for enemies" — false.
  Reads valid anim_ids (4001100, 4003008, etc.) on field enemies. Stale
  comment from v6.1.1; low-priority cleanup.
- Family fallback (raw_cid → raw_cid/10*10) works: c4311 → c4310 hits 689
  times in JSONL.

---

## Phase 4.2 plan (NEXT — start here)

**Goal:** make the cue audible. The predictor already emits ACTION_FIRE /
ACTION_LATE_INSIDE_WINDOW / ACTION_LATE_TARGET_SWITCH decisions. Wire them
to `PlaySoundW`.

**Reference:** PHASE4-PLAN.md lines 557-657 (Phase 4.2 spec).

### What needs to change

1. **probe.cpp Config struct (line ~552-557)**: add
   `bool audio_cue_enabled = true;` (currently missing — only the lead_ms
   field exists). Wire the INI parser (line ~681) to read
   `audio_cue_enabled` and `audio_cue_wav_path` (already in PHASE4-PLAN
   knobs table).

2. **probe.vcxproj**: add `winmm.lib` to `AdditionalDependencies` (currently
   only `version.lib`). Add a `<ResourceCompile>` entry for the new `.rc`
   file. New `ClCompile` and `ClInclude` for the audio module.

3. **New files**:
   - `probe/parry-tell-probe.rc` — resource script
   - `probe/resource.h` — resource IDs
   - `probe/assets/audio_cue.wav` — short PCM WAV (50-100ms, mono, 44.1kHz,
     16-bit, <32 KB target)
   - Optionally: `probe/audio.h` + `probe/audio.cpp` for the AudioCue module

4. **probe.cpp `WritePredictionDecision` (line 1610)**: this is the **single
   chokepoint** for every emitted decision. Add a `MaybeFireCue(d, cfg)` call
   inside the critical section AFTER `PredictionLogShouldEmitLocked(d)` returns
   true (so rate-limit applies to audio too, preventing 250Hz audio spam if a
   bug ever lets a same-window decision re-emit). Fire when:
   - `cfg.audio_cue_enabled == true`
   - `d.action` is ACTION_FIRE, ACTION_LATE_INSIDE_WINDOW, or
     ACTION_LATE_TARGET_SWITCH
   - (target filter is already applied upstream in EvaluatePredictionTick)

5. **AudioCue module surface** (per PHASE4-PLAN.md lines 612-621):
   ```
   bool InitAudioCue(const Config& cfg);   // called from worker init
   void FireAudioCue();                    // called from WritePredictionDecision
   void ShutdownAudioCue();                // called from worker shutdown
   ```
   - Load WAV from embedded resource via `FindResourceW` / `LoadResource` /
     `LockResource` / `SizeofResource`. Validate RIFF/WAVE header.
   - If `audio_cue_wav_path` is set and valid, load that file into a heap
     buffer instead.
   - Keep buffer alive process-lifetime (PlaySoundW SND_ASYNC must not see
     freed memory).
   - `FireAudioCue` = `PlaySoundW(g_audio_buf, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)`.
   - Failure: log once, set a flag, never crash.

### After code lands

6. **Bump `audio_cue_lead_ms` to `50` in smoke INI** (currently 0). At 0,
   we get `fire_late_inside_window` because the 4ms poll lands ~30-50ms
   after the open frame. With lead=50, the threshold check fires earlier
   and the decision becomes `fire` (proper). PHASE4-PLAN.md confirms this
   tuning is part of Phase 4.2.

7. **Build + deploy v8.2.0**. Smoke test pattern same as 4.1.3:
   - Build via SSH MSBuild
   - SCP DLL to `/mnt/station-mods/parry-tell-probe.dll`
   - Delete stale `parry-tell-probe.csv`
   - Josh launches game, fights an enemy with parryable anims (cid 4311 works)
   - Josh saves DebugView log to `C:\Projects\elden-ring\STATION-v8.2.log`
   - Pull artifacts via SMB, analyze JSONL for `fire` (not `_late`) decisions
   - **Listen to the audio in-game** — first audible cue is the moment we
     have a real product

### Pair-writer assignment

Phase 4.1 = Claude wrote, Codex reviewed (3-4 passes). Per the rule, Phase
4.2 first draft = **Codex writes, Claude reviews**. Codex is also the
preferred default for first drafts of audio/WAV/resource code per the
smart-defaults table in CLAUDE.md (not technically Playwright/OAuth but
adjacent — system-API surface, has subtle Windows-version compat
considerations).

---

## Files modified this session (all committed in SHA `53855685`)

- `probe/probe.cpp` (+446 / -40 lines): Phase 4.1.1 + 4.1.2 + 4.1.3
- `probe/releases/probe-v8.0-phase41.tar.gz` (213 KB) — Phase 4.1 initial
- `probe/releases/probe-v8.1-phase411.tar.gz` (216 KB) — Phase 4.1.1
- `probe/releases/probe-v8.1.3-phase413.tar.gz` (221 KB) — Phase 4.1.3 (working)

### Probe.cpp anchor points for Phase 4.2

| Symbol | Line | Purpose |
|---|---|---|
| `Config` struct | 540-560 | Add `audio_cue_enabled`, `audio_cue_wav_path` |
| INI parser block | 681+ | Wire new keys |
| Config validation | 742+ | Validate WAV path length |
| `PredictionDecision` | 1316 | (read-only — already has all fields we need) |
| `ACTION_FIRE` family | 1140-1158 | (read-only — fire actions enumerated) |
| `WritePredictionDecision` | 1610-1659 | **Chokepoint: add `MaybeFireCue(d, cfg)` here** |
| Worker init | 5004+ | Call `InitAudioCue(g_cfg)` after `LoadParryDb()` |
| Worker shutdown | ~5349 | Call `ShutdownAudioCue()` near `PredictionLogClose()` |

---

## Station-side state at session end

- `/mnt/station-mods/parry-tell-probe.dll` = v8.1.3 (SHA `90068636`, 274 KB)
- `/mnt/station-mods/parry-tell-probe.dll.v8.1.2-backup` = pre-priority-handles
- `/mnt/station-mods/parry-tell-probe.dll.v8.1-backup` = pre-diagnostics
- `/mnt/station-mods/parry-tell-probe.dll.v8.0-backup` = pre-roster-fix
- `/mnt/station-mods/parry-tell-probe.dll.v7.3-backup` = Gate 0.B baseline
- `/mnt/station-mods/parry_data.bin` = 37 KB (unchanged)
- `/mnt/station-mods/parry-tell-probe.ini` = smoke config:
  - `target_filter_enabled=false`
  - `audio_cue_lead_ms=0` (will bump to 50 in Phase 4.2)
  - `prediction_decision_log_enabled=true`
- **SSH service: status unknown at session end.** Josh should `Stop-Service sshd`
  if still running. Start it again at next session: `Start-Service sshd`
  in elevated PowerShell.

## SMB mounts

`/mnt/station-projects/` (RO) and `/mnt/station-mods/` (RW) should be live
via Tailscale automount. Verify with `mount | grep station` at session start.

## SSH to station

Password auth (NOT key). Credentials at `/etc/ssh-credentials-station`:
```
SSHPASS='Plane$our192' sshpass -e ssh -o StrictHostKeyChecking=no claude@station '<cmd>'
```
**Do NOT use `-o BatchMode=yes`** — disables interactive password auth.
Default shell is cmd.exe; use `certutil -hashfile <path> SHA256` not
`Get-FileHash` for verification.

---

## Refuted hypotheses (preserve for next agent — do not re-investigate)

| Hypothesis | Verdict | Evidence |
|---|---|---|
| `ChrIns +0x6A0` as enemy targetHandle | REFUTED | 100% zero on enemies in v7.2 region 0 |
| ChrIns* pointer-equality as target field shape | REFUTED | 0% across 5M+ u64 slots in v7.0+v7.2 |
| `ActionRequest +0x08` as target candidate | REFUTED | Self-reference (owner pointer), not target |
| `TimeAct +0x20 + read_idx*16` as enemy anim queue | REFUTED | Sentinel in v6.2/v6.3 |
| `ActionRequest +0x90` as enemy anim_id | REFUTED | Sentinel / non-winner |
| `max_segment_dur` as primary anim_time tiebreak | REFUTED | `+0x2C` wins on real data |
| Transient TimeAct pointer / async incoherence | REFUTED | `read_fail=0` in v8.1.2 diagnostic run |
| Stale chr_ins from worker-thread roster walk | REFUTED | Anim data is valid; problem was wrong enemy enumerated |
| "anim_id offset 0xD0 reads 0 for enemies" comment | REFUTED | Reads valid anim_ids on field enemies |
| `PlayerIns +0x6A0` as player lock-on target | REFUTED | Dead-always; `+0x6B0` is live |
| Roster sorted near-the-player first | REFUTED | Locked enemy was never in first 16 across 156 snapshots |

---

## Pickup prompt for next session

> Phase 4.1 is functionally complete — predictor end-to-end pipeline proven
> in-game on 2026-05-15 18:35 smoke test (4 `fire_late_inside_window`
> decisions, target field works on field enemies, latch state machine prevents
> re-fires). v8.1.3 DLL is deployed on station. Now wire audio:
>
> Phase 4.2 first draft → **Codex** (writer-pairing rule: Claude wrote 4.1).
> Spec lives in PHASE4-PLAN.md lines 557-657. Key insertion point:
> `WritePredictionDecision` at probe.cpp:1610 is the single chokepoint —
> add `MaybeFireCue(d, cfg)` inside the critical section after the rate-limit
> check so audio inherits the per-window rate limit for free.
>
> Need a short WAV (50-100ms, mono, 44.1kHz, 16-bit, <32KB) at
> `probe/assets/audio_cue.wav`. Embed via `.rc`, link `winmm.lib`, load via
> `FindResourceW`. PlaySoundW canonical call is
> `PlaySoundW(buf, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)`.
>
> After code lands, bump `audio_cue_lead_ms=50` in smoke INI so decisions
> become `fire` (not `_late`). Build v8.2.0, deploy via the standard
> SCP/MSBuild/SMB cycle, Josh tests in-game.
