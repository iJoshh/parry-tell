---
status: draft
phase: 1
started: 2026-05-05
revised: 2026-05-05 (evening — full rewrite after archaeology + product spec lock)
project: elden-ring-parry-indicator-mod
codename: parry-tell (working name; final TBD)
---

# Phase 1 Plan — Elden Ring Parry Indicator Mod (Boss-Aware, Co-op Safe)

## Context (one paragraph for any future reader)

Josh is building a client-side Elden Ring + SotE mod that helps him parry boss attacks during Seamless Co-op sessions where he is a guest (non-host). The mod displays a screen-edge color hue when a boss is performing a parryable attack AND currently targeting Josh, then transitions to silence + a short audio cue at the parry-window-open frame. Triggered only during boss fights (when ER is drawing a boss health bar). This is a commissioned build — orchestrator-Claude on a Linux code-server VM drives all coding via Codex MCP; Josh's role is build runner, in-game tester, and product-decision-maker. Josh is a 2-month vibecoder with no C++/RE background. Optimization target is "minimize Josh's keyboard time," not "maximize Josh's learning."

The data layer is solved: `AtkParam.isDisableParry` is a triple-confirmed per-attack engine bit at byte offset `0x18A` (libER typed bitfield, Nordgaren Erd-Tools, TGA cheat table all agree). The runtime memory layout is documented through ER 1.16.1: TarnishedTool (MIT-licensed, patch-current) ships hardcoded module-relative offsets for `WorldChrMan`, `SoloParamRepositoryImp`, `ChrInsByHandle`, the NPC roster walk, the AI struct, and the animation module. The boss-bar trigger is documented in Erd-Tools-CPP (`CSFeManImp::bossHpBars[3]` with per-slot `bossHandle`). The TAE event data for parry-window timing per animation lives in the game's `.anibnd` files on disk and will be extracted offline once via WitchyBND. The remaining unknowns are scoped: a single AI struct field for "this enemy is currently targeting player X," and the exact runtime path to read `currentAtkParamId` from a ChrIns (which we sidestep by mapping animation IDs to AtkParam rows offline through BehaviorParam).

## Goals

**Primary deliverable (v1):** A Windows DLL (`parry-tell.dll` working name) that:

1. Loads via Seamless Co-op's `external_dlls` mechanism (guest-side only; host doesn't need it).
2. Activates only when the game is drawing a boss health bar (read from `CSFeManImp::bossHpBars[i].bossHandle`). Multi-boss aware — handles up to 3 simultaneous boss bars.
3. While active, every frame:
   - Identifies which `ChrIns` corresponds to each active boss bar.
   - Reads each boss's current animation ID and animation time.
   - Cross-references an embedded lookup table (`parry_data.json`, built offline from extracted TAE data) to determine: is this animation parryable? When does the parry window open?
   - Reads each boss's "current target" field (Gate 0.B — see below; first-try offset is `SpEffectObserveEntry.Target` from TarnishedTool's AI service).
   - Reads the player's lock-on target field (Practice Tool documents this for ER 1.16.1).
4. Triggers cues based on this state machine:
   - Boss starts parryable attack, target = Josh, Josh is locked onto THAT boss (or no lock-on) → screen-edge hue ON, **primary color**.
   - Boss starts parryable attack, target = Josh, Josh is locked onto a DIFFERENT boss → screen-edge hue ON, **alert color** (signals "switch focus").
   - Boss starts parryable attack, target ≠ Josh → silent. Don't care.
   - Mid-attack target switches: not-Josh → Josh → hue ON (color picked by lock-on state at that moment).
   - Mid-attack target switches: Josh → not-Josh → hue OFF.
   - Mid-attack lock-on switches: Josh changes which boss he's locked onto → hue color updates in real-time without resetting timer.
   - Animation reaches parry-window-open frame, target = Josh at that frame → hue OFF, audio cue plays. Audio cue fires regardless of lock-on color (the timing is the timing; lock-on is just visual signal of "right or wrong focus").
   - Animation cancels or ends before parry-window opens → hue OFF, no audio.
5. Operates exclusively in `eldenring.exe` memory — never touches `regulation.bin`, never modifies game data, never sends anything over the network.

**Stretch deliverables (post-v1):**
- v2 visual: per-attack hue color (green = front-parryable, yellow = directional-parryable, etc., reading `parryForwardOffset`).
- v3 timing precision: parry-window-end audio fade-out so you can hear "the window is closing."
- v4 expanded enemy coverage: world bosses, mini-bosses with bars beyond the initial extraction set.

**Quality bar for v1 ship (GitHub release; Nexus stretch):**
- Builds against ER 1.16.1 with MSVC 2022 + libER pinned to a tagged release.
- ME2/Seamless integration verified — DLL loads, runs 30+ min combat session without crash.
- Doesn't crash mid-co-op-session; if internal logic fails, Josh's session survives.
- Boss bar appears → mod activates within 100ms. Boss bar disappears → mod deactivates.
- Hue + audio fire correctly for at least the canonical parryable bosses we extract data for.
- README, MIT LICENSE, GitHub release `v0.1.0` with prebuilt DLL + source tag.
- Nexus release decided post-v1 ship; not a quality gate.

## Non-goals (scope discipline)

- No `regulation.bin` editing.
- No mechanic changes — indicator-only.
- No ban-risky launch paths — must work via Seamless's `ersc_launcher.exe` or normal ME2 launch.
- No PvP-favoring features — cue suppressed when target is a player entity (invader, summoned phantom, host of a co-op session — though Josh is the guest).
- No host-side requirement — guest-only utility.
- No telemetry, no network, no auto-update — drop-in DLL, fully offline.
- Not a parry trainer with timing feedback — no "you missed by N ms" overlay. Save for v3+.
- Not for non-boss combat — explicit design decision; the boss-bar trigger is the activation gate.

## Architecture (one-page mental model)

```
eldenring.exe
├── EAC bypassed by Seamless's ersc_launcher.exe
├── SeamlessCoop/ersc.dll      (host coordination, party logic)
└── parry-tell.dll              ← what we're building
    ├── DllMain → spawn worker thread on attach
    ├── Worker thread, ~30Hz polling:
    │   1. Read CSFeManImp.bossHpBars[3] — any active bosses?
    │      No → sleep, repeat
    │   2. For each active bossHandle, resolve to ChrIns
    │   3. For each boss ChrIns:
    │       a. Read currentAnimation (ChrIns +0x190 -> ChrModuleBag +0x18 -> +0xD0)
    │       b. Read currentAnimationTime (adjacent field)
    │       c. Look up animation in parry_data.json:
    │          - is_parryable bool
    │          - parry_window_start_frame (if parryable)
    │       d. Read enemyCurrentTarget (Gate 0 — TBD offset, first-try SpEffectObserveEntry.Target)
    │       e. Compare enemyCurrentTarget against playerEntityId
    │   4. Read player.lockOnTarget (Practice Tool documents this; offset for 1.16.1)
    │   5. Apply state machine (3-input: target_is_me, attack_is_parryable, lock_on_matches_attacker)
    │      Fire cues with appropriate color (primary vs alert) and audio
    │
    ├── State (lifted MIT from TarnishedTool offsets):
    │   - WorldChrMan.Base = moduleBase + 0x3D65F88
    │   - SoloParamRepositoryImp.Base = moduleBase + 0x3D81EE8
    │   - Functions.ChrInsByHandle = moduleBase + 0x507C70
    │   - WorldChrMan.ChrInsByUpdatePrioBegin = 0x1F1B8
    │   - ChrIns + 0x190 -> ChrModuleBag
    │   - ChrModuleBag + 0x18 -> TimeAct module
    │   - TimeAct + 0xD0 -> currentAnimation
    │
    ├── Boss-bar identification (lifted concept from Erd-Tools-CPP, MIT re-implementation):
    │   - CSFeManImp singleton (offset TBD — Codex finds in Step 0)
    │   - +bossHpBars array, 3 slots
    │   - Each slot has bossHandle at +0x8
    │   - Skip slots where bossHandle == UINT64_MAX
    │
    ├── Audio cue (~50 LOC):
    │   - WASAPI single-tone synthesis OR embedded WAV via PlaySound()
    │   - Configurable volume, two presets (tick / ping)
    │
    ├── Visual cue (~80 LOC):
    │   - D3D12 Present hook (lifted-and-adapted MIT from PostureBarMod)
    │   - ImGui: full-screen rectangle border draw with configurable hue
    │   - Hue color configurable in INI
    │
    └── Crash insulation:
        - __try/__except wrapping every memory read
        - On any read failure, log and skip frame (never take down Josh's co-op)
```

## Risks and mitigations (updated post-archaeology)

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **Gate 0.A — Animation-to-AtkParam mapping is ambiguous (one animation maps to multiple AtkParam rows)** | Medium | Need to disambiguate per-frame via behavior-state read | Probe DLL extracts both `currentAnimation` and any active behavior-state fields; if 1:1 mapping doesn't hold, expand lookup to `(animation_id, behavior_state) -> atkParamId` |
| **Gate 0.B — Enemy "current target" field offset is wrong on first try** | Medium-high | Adds 0.5–1 day for one more probe-and-diff session | First-try `SpEffectObserveEntry.Target`. Fallback: dump `aiThink + 0xE000..0xF000` during a fight where Josh records boss target switches; diff to find the field |
| TAE extraction misses a parryable enemy (Margit's hammer slam isn't tagged "parryable" in TAE somehow) | Low-medium | Mod doesn't cue for that attack; user reports as a bug | Document known-unsupported attacks in README; v2 expands lookup table |
| Per-attack `isDisableParry=0` but attack still un-parryable (`parryAttack > player parryDefence`) | Medium | False positives | Document; v3 reads `parryAttack`/`parryDefence` |
| ER patch invalidates TarnishedTool offsets mid-build | Medium | 0.5–1 day to refresh after each patch | Pin to current ER 1.16.1; monitor TarnishedTool repo for offset bumps |
| EAC behavioral change bans the launch path | Low | Catastrophic | Don't be the first mod tested post-EAC-update; monitor PostureBarMod / Seamless for 7 days post-each-patch |
| Seamless host integrity-checks `external_dlls` against an allowlist | Low | Mod won't load for guests | Verified in Step 0 preflight (hello-world DLL test). Prior research: no evidence this happens |
| Co-op desync makes parry inputs unreliable for guest even with perfect cue | Medium | Mod works but doesn't help in actual play | Document; recommend solo-practice runs to drill timing |
| Windows Defender quarantines unsigned DLL | Medium | Mod doesn't load until Defender exclusion added | Step 0.2b — Josh adds project folder to exclusions |
| Crash inside our DLL kills Josh's co-op session | Medium | Bad UX, recoverable | `__try/__except` around all memory reads |
| Boss bar shown but AI is unusual (Godrick phase 2 grafted-arm sequence) — animations don't match our extracted data | Low-medium | Mod silent during that phase | Coverage gaps documented; v2 expands |
| Nexus moderator rejects mod (info-hiding-defeating) | Medium | Can't ship Nexus — but it's stretch goal | GitHub release is the win condition |
| TAE extraction is more work than 30 min per character | Low | Adds time to extraction step | One-and-done — Josh runs UXM once, extracts everything we'd ever want, then verifies install back to vanilla |

## Build plan

### Step 0: Preflight + final archaeology (1–2 days, mostly my time, ~3–4 hours of Josh's time spread across the week)

**0.1 — Final Codex archaeology dispatches (parallel, my time, already partially complete)**

- ✅ PostureBarMod borrow map (`archaeology/01-...`)
- ✅ libER API surface (`archaeology/02-...`)
- ✅ Practice Tool techniques (`archaeology/03-...`)
- ✅ TGA techniques (`archaeology/04-...`)
- ✅ Gate 0 attack plan / synthesis (`archaeology/05-...`)
- ✅ TarnishedTool borrow map (`archaeology/06-...`) — patch-current 1.16.1 offsets
- ✅ Erd-Tools borrow map (`archaeology/07-...`)
- ✅ Hexinton CT techniques (`archaeology/08-...`)
- ✅ Targeting + boss-bar identification (`archaeology/09-...`)
- ⏳ STILL TO RUN: CSFeManImp singleton offset for ER 1.16.1 — needs one focused dispatch to find (TarnishedTool likely has it; if not, lift from Erd-Tools-CPP's GPLv3 source as documented fact)
- ⏳ STILL TO RUN: Probe-DLL code generation — Codex writes the C++ probe based on the spec at `GATE-0-PROBE-SPEC.md`

**0.2 — Josh's preflight gates (~1–2 hours of Josh's time)**

These are setup tasks Josh does on his Windows machine before the build phase starts:

- **0.2a: VS 2022 install** — Visual Studio 2022 Community + "Desktop development with C++" workload + Windows 11 SDK. Verify with a Hello World console app build.
- **0.2b: Defender exclusion** — add project folder to Defender exclusions before building any DLL.
- **0.2c: Hello-world DLL load test** — I write a 20-line `hello.dll`. Josh drops it in Seamless `external_dlls`, launches via `ersc_launcher.exe`, verifies "hello" appears in DebugView. If this fails, ME2/Seamless config is broken and nothing else matters.
- **0.2d: GitHub repo creation** — Josh creates `iJoshh/parry-tell` (private). Repo handoff: I write code on the Linux VM in a folder, Josh `git pull` on Windows. No deploy keys.

**0.3 — TAE extraction (~3 hours of Josh's time, ONCE, batched)**

This is the one-and-done game-data extraction. Detailed checklist in `EXTRACTION-PLAN.md` (separate document so it's a clean reference). Summary:

- Install UXM Selective Unpacker; run against ER install (~15 min)
- Install WitchyBND; unpack relevant `.anibnd` archives (~30 min for full enemy roster)
- Zip + send back the unpacked TAE XML data (Josh) → I parse offline into `parry_data.json` (my time)
- Josh runs Steam "verify integrity of game files" to restore vanilla install before any future online play

Goal: extract everything we might ever want in one session so Josh never has to re-unpack. Files to grab listed in `EXTRACTION-PLAN.md`.

### Step 1: Gate 0 spike — runtime data extraction probe (1–3 days, mostly my time, ~2 hours Josh's time)

**Goal:** Verify all our memory reads work in 1.16.1 and resolve Gate 0.A and Gate 0.B.

**What we build:** A minimal probe DLL that does no UI, no audio, just data extraction:

1. On DLL load, spawn worker thread.
2. Worker polls every 100ms.
3. Reads CSFeManImp.bossHpBars[]. If any active, logs `[time] boss N: handle=0x... ChrIns=0x...`.
4. For each boss ChrIns, logs:
   - `currentAnimation`, `currentAnimationTime` (TimeAct module fields)
   - Candidate `currentTarget` fields: `aiThink + SpEffectObserveComp + 0x18` and a sweep of `aiThink + 0xE000..0xF000` 4-byte values
   - Player's own entity ID
5. Logs to `parry-tell-probe.log` adjacent to the DLL.

**What we learn from the probe:**

- **Gate 0.A test:** Does `currentAnimation` reliably change at attack-start? Cross-reference against the extracted TAE data — is the animation ID we read the same one we extracted?
- **Gate 0.B test:** Among the candidate target fields, which one flips at the moment Josh visibly observes the boss switch targets? (Josh fights solo or in co-op; mentions in chat / log "boss switched to me at 14:22"; we diff log against ground truth.)
- **Boss-bar test:** Does `CSFeManImp.bossHpBars` correctly populate when a boss bar appears, and clear when it disappears?

**Success criterion:** Within 3 days of probe iteration, we have:
- Working currentAnimation read (already high-confidence from TarnishedTool) — verified in live game
- Working `currentTarget` field identified — confidence determined by probe
- Working boss-bar entity resolution — verified with at least Margit, Crucible Knight (Stormveil)

**Failure mode and off-ramp:** If `currentTarget` can't be resolved cleanly in 3 days, drop to **Option A** for v1 — show hue any time the boss does a parryable attack, regardless of target. Less precise (you'll get hue when the boss attacks your friend), but ships clean. Defer Gate 0.B to v2.

**Josh's role:** Pull from GitHub, build probe, run 1-2 boss fights solo + 1-2 in co-op, send me the log. ~2 hours total across testing sessions.

### Step 2: Production v1 build (3–5 days, mostly my time, ~3–4 hours Josh's time)

Once Gate 0 passes, build the actual mod on top of probe scaffold:

- **Audio cue:** WASAPI tone OR PlaySound() with embedded WAV. Two presets (tick / ping). Config in INI.
- **Visual cue:** D3D12 Present hook (lift from PostureBarMod). ImGui screen-edge rectangle border. Configurable hue color in INI.
- **State machine:** Implement the 6-state cue logic from Goals section #4.
- **PvE-only filter:** Skip when target ChrIns has chrType indicating player.
- **Crash insulation:** `__try/__except` everywhere memory is read.
- **Compatibility check:** Run alongside PostureBarMod for 30+ min in a co-op fight.

**Quality gates:**
- 30+ min combat session without crash (solo + co-op)
- Hue + audio fire for known parryable boss attacks (Crucible Knight kick, Margit's parryable, Banished Knight sword)
- No false positives during boss-bar-up but non-parryable attacks
- No EAC alerts
- Compatibility verified with PostureBarMod loaded

**Josh's role:** Run builds every 1-2 days as I push commits. Test in 30-min sessions. Make UX calls (hue color, audio volume, etc.).

### Step 3: GitHub release + optional Nexus polish (0.5–1 day)

- README with: install instructions, Defender exclusion note, screenshot/video, supported bosses list
- LICENSE (MIT)
- Build instructions for source rebuild
- `version.json` with ER version compatibility
- GitHub release `v0.1.0` with prebuilt DLL + source tag
- Nexus decision post-ship: is polish + boss coverage enough to risk moderator rejection? If yes, +0.5 day for demo video and Nexus mod page.

### Realistic timeline (revised post-archaeology)

Two estimates:

- **Optimistic (Gate 0 both pass day 1, no surprises, TAE extraction clean):** 8–10 days calendar, ~8 hours Josh time at keyboard.
- **Realistic (Gate 0.B takes 1-2 days probe iteration, one MSVC linker round burns an evening, one ER patch mid-build):** 2–3 weeks calendar, ~12–15 hours Josh time.

Weighted expected: **~12–14 days calendar, ~10 hours Josh time at keyboard.**

**Updated honest confidence:**
- Shipping v1 in 3 weeks: **~70%** (up from 60% pre-archaeology; up from 65% before today's targeting/boss-bar finding)
- Shipping v1 eventually: **~85%**
- The remaining 15% no-ship tail is dominated by Gate 0.B failing AND Option A having unforeseen blockers — unlikely combination but not zero.

## Open questions for Josh (very few left)

These are the only decisions I want from you before kicking off Step 1:

1. **Working name confirmation.** `parry-tell` still feels right? Default if no answer: keep it.
2. **TAE extraction timing.** When do you want to do the ~3-hour UXM/WitchyBND session? (Doesn't need to be before Step 1; Step 1 only needs the probe DLL. TAE data is needed for Step 2.)
3. **Co-op test partner availability.** For Gate 0.B (the targeting probe), it's easier to verify "boss switched to me" if you're in a co-op session with a friend. If that's hard to coordinate, we can do solo testing where you summon a Mimic Tear or NPC ally and use them as the boss's other target.

Everything else (license MIT, INI schema, audio implementation, hue color defaults, etc.) — I'll pick. Push back if any default is wrong.

## Approval checklist before kicking off Step 1

- [ ] Josh accepts the plan top-line (v1 = boss-aware, target-aware, audio + visual cue)
- [ ] Open questions 1–3 answered (or "use the defaults")
- [ ] Plan acknowledges: 70% confidence on v1 shipping in 3 weeks, 85% eventual
- [ ] Plan acknowledges: Gate 0.B (targeting field) is the remaining open RE question
- [ ] Plan acknowledges: TAE extraction is one-and-done, batched, ~3 hours Josh time
- [ ] Plan acknowledges: Nexus release is stretch goal, GitHub release is win condition
- [ ] Josh accepts ~10 hours of his keyboard time spread across 2–3 weeks

Once all six checked: I write the probe DLL spec, dispatch Codex to write the actual probe code, write the EXTRACTION-PLAN.md document, and send Josh his preflight gate checklist.
