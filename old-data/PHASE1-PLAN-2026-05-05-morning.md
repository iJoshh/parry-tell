---
status: draft
phase: 1
started: 2026-05-05
revised: 2026-05-05
project: elden-ring-parry-indicator-mod
codename: parry-tell (working name; final TBD)
---

# Phase 1 Plan — Elden Ring Parry Indicator Mod (Seamless Co-op safe, guest-side)

## Context (one paragraph for any future reader)

Josh wants a client-side mod for Elden Ring + Shadow of the Erdtree that fires an audio cue when an enemy is performing a parryable attack. Must be Seamless Co-op safe for non-host (guest) players. Prior research (artifacts 001–006 in `research/`) established: (a) no such mod exists today; (b) the data layer is strongly indicated — `regulation.bin` carries `AtkParam.isDisableParry` as a per-attack Boolean bit, with libER's `SoloParamRepository` providing typed runtime access; (c) the architectural template for ME2/Seamless DLL injection is PostureBarMod (Mordrog, MIT, Nexus 3405); (d) **the runtime path from a `ChrIns` (entity) to its currently-executing `AtkParamId` is NOT typed by libER** — only `param/` is typed; `GLOBAL_CSBehavior` is a raw symbol that requires manual struct-offset traversal; (e) EAC ban risk is low-but-non-zero through the documented ME2/Seamless launch path — both bypass `start_protected_game.exe`. Updated honest confidence on shipping v1 in 3 weeks: 55%; on shipping eventually: 75%. The remaining uncertainty is engineering effort, dominated by reverse-engineering the `ChrIns → currentAtkParamId` path that no public typed API exposes.

**This is a commissioned build.** Josh is the customer; orchestrator-Claude (running on a Linux code-server VM) is the builder, dispatching Codex MCP for Windows/C++/RE-heavy code generation. Josh's role is build runner + tester + product-decision-maker, not co-implementer. Optimization target is "minimize Josh's keyboard time," not "maximize Josh's learning."

## Goals

**Primary deliverable (v1):** A Windows DLL (`parry-tell.dll` working name) that:

1. Loads via Seamless Co-op's `external_dlls` mechanism (guest-side only; host doesn't need it).
2. For each enemy NPC in render distance, detects when the NPC begins an attack animation (state-change detection — Option A foundation).
3. **If Gate 0 (see below) succeeds:** filters those attacks by reading `AtkParam_Npc[id].isDisableParry`, emitting the cue only for parryable attacks (Option B upgrade).
4. **If Gate 0 fails:** ships Option A as v1 — fires the cue on every attack windup, with a documented limitation in the README. Still useful as a parry-timing trainer; less precise than Option B.
5. Audio-only v1: a short, distinct WASAPI-played tone (or embedded WAV) when a parryable attack starts. No visual overlay in v1.
6. Operates exclusively in `eldenring.exe` memory — never touches `regulation.bin`, never modifies game data, never sends anything over the network.

**Stretch deliverable (v2):** Visual ring/glyph overlay above enemy heads, layered on top of v1's audio cue. Adds ImGui + D3D12 hook + world-to-screen projection — the foreign-stack complexity we deferred from v1.

**Stretch deliverable (v3):** Read `parryForwardOffset` and color/tone-shift when the player is outside the parry-arc — distinguishes "parryable in principle" from "parryable right now from your position."

**Quality bar for v1 ship (GitHub release, Nexus stretch):**
- Builds cleanly against ER 1.16.x with MSVC 2022 + libER pinned to a tagged release.
- ME2/Seamless integration verified end-to-end (loads, runs, no crashes, no anti-cheat alerts in 30 min play session).
- Doesn't crash mid-co-op-session; if it does, Josh's ER session survives without disconnecting from the host.
- README that another modder could read and rebuild from source.
- LICENSE file (MIT, matching PostureBarMod's license, license-compatible with libER's Apache-2.0).
- GitHub release tag `v0.1.0` with prebuilt DLL + source.
- **Nexus release is a stretch goal**, not a quality gate. Decided post-v1 ship based on Josh's read of "is this polished enough."

## Non-goals (scope discipline)

- **No `regulation.bin` editing.** Read-only memory inspection of the running game's loaded params.
- **No mechanic changes.** Do not widen parry windows, change parry frame data, or alter combat in any way. Indicator-only.
- **No ban-risky launch paths.** Mod must work via Seamless's `ersc_launcher.exe` or a normal ME2 launch. Never load on top of `start_protected_game.exe`.
- **No PvP-favoring features.** Indicator fires on PvE enemies' parryable attacks only; do not surface this for player-controlled entities (invaders, summoned phantoms).
- **No host-side requirement.** Guest-only utility. Host running vanilla Seamless or running this mod themselves is fine; nothing in the protocol changes.
- **No telemetry, no network, no auto-update.** Drop-in DLL, fully offline.
- **Not a "parry trainer" beyond the indicator.** No timing meter, no "you missed by N ms" overlay. Save for v3+.
- **Not a learning project.** Josh is paying me (in time + judgment trust) to do this so he doesn't have to. Where I'd otherwise have him compile or run a probe to learn how it works, I dispatch Codex and produce a working artifact instead.

## Architecture (one-page mental model)

```
eldenring.exe
├── EAC bypassed by Seamless's ersc_launcher.exe
├── SeamlessCoop/ersc.dll  (host coordination, party logic)
└── parry-tell.dll  ← what we're building
    ├── DllMain → spawn worker thread on attach
    ├── Hook: lightweight per-frame poll (no D3D12 hook in v1)
    │   └── Worker thread polls every ~33ms (30Hz):
    │       walk enemy ChrIns list → resolve currentAtkParamId →
    │       lookup isDisableParry → fire audio cue on 1→0 transition
    ├── State (libER + manual offsets):
    │   ├── GLOBAL_WorldChrMan (player + NPC roster) — libER typed
    │   ├── ChrIns → currentAtkParamId (UNTYPED — manual offsets)
    │   └── SoloParamRepository.AtkParam_Npc — libER typed
    ├── Audio: WASAPI or PlaySound() w/ embedded WAV (~80 LOC)
    └── Crash insulation: __try/__except wrapping all memory reads
```

**v2 (deferred):** ImGui + D3D12 hook + world-to-screen projection lifted-and-adapted from PostureBarMod.

The thing we are NOT inventing in v1: how to inject into ER, how to find the player and NPC list in memory, how to read params via `SoloParamRepository`. All of that is published, MIT-licensed PostureBarMod / Apache-2.0 libER territory.

The thing we ARE inventing in v1: the `ChrIns → currentAtkParamId` resolution path (the load-bearing reverse-engineering work), the audio cue, and the crash-safety wrapper.

## Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **No typed `ChrIns → currentAtkParamId` path in libER (CONFIRMED gap)** | High | Adds 1–2 weeks of manual struct-offset RE | **Gate 0** below; ship Option A as v1 if Gate 0 fails |
| ER patch mid-build invalidates AOB signatures | Medium | Adds 0.5–1 day to refresh per patch | Pin libER to tagged release; track ER version in build metadata |
| PostureBarMod AOBs (last bumped 1.10.1) are stale for 1.16+ | High | Hook entry points + struct offsets need refresh, not just lift-verbatim | Step 0 archaeology produces a refreshed offset/AOB inventory before Step 1 |
| Per-attack `isDisableParry=0` but attack still un-parryable in practice (`parryAttack > player parryDefence`, big bosses) | Medium | False positives in indicator | Document in README; v3 reads `parryAttack`/`parryDefence` |
| Parry window is 1–2 frames; audio cue may fire "after" the parry-able moment | Medium | Cue arrives late on fastest attacks | Fire on windup-START, not hit-active-start. Indicator means "begin-parry-now," not "parry-this-frame" |
| EAC behavioral change bans the launch path | Low | Catastrophic | Don't be the first mod tested post-EAC-update. Monitor PostureBarMod / Seamless ban reports for 7 days post-each-ER-patch |
| Seamless host integrity-checks `external_dlls` against an allowlist | Low | Mod won't load for guests | **Verified in Step 0 preflight**; prior research found no evidence of this |
| Co-op desync makes parry inputs unreliable for guest even with perfect cue | Medium | Mod works but doesn't help in actual play | Document in README; recommend single-player practice runs to drill timing |
| Windows Defender quarantines unsigned DLL | Medium | Mod doesn't load until Josh adds folder exception | **Verified in Step 0 preflight**; document Defender exclusion in README |
| Crash inside our DLL kills Josh's co-op session | Medium | Bad UX, but recoverable | `__try/__except` around all memory reads; if hook fails, log and skip frame |
| Nexus moderator rejects the mod (information-hiding-defeating) | Medium | Can't ship to Nexus — but Nexus is a stretch goal | GitHub-only release is the win condition. Nexus framing decided post-v1-ship |

## Build plan

### Step 0: Preflight + archaeology (1–2 days, mostly my time, ~1–2 hours of Josh's time)

Three Codex archaeology dispatches in parallel + four preflight verifications by Josh.

#### 0.1 — Codex archaeology (parallel, my time)

- **0.1a: PostureBarMod source read.** Map `Source/Main/Hooking.cpp`, `Hooking.hpp`, `PostureBarUI.cpp`, `D3DRenderer.cpp`. Identify (1) the AOB signatures and their target functions; (2) struct offsets and their version-stability; (3) the architectural patterns (MinHook usage, ImGui background draw list, ChrIns walking) that ARE version-portable. Output: `BORROW-MAP.md` with file:line citations, classifying each finding as "lift architecture only" (the pattern), "lift and refresh" (the AOB/offset, requires re-derivation for 1.16), or "reference only" (AGPL or otherwise unliftable).

- **0.1b: libER API surface read.** Read `include/param/`, `include/coresystem/`, `include/fd4/`. Confirm (1) `SoloParamRepository::instance()` access pattern; (2) `AtkParam_Npc[id].isDisableParry` typed bitfield access; (3) `GLOBAL_WorldChrMan` typed accessor. Document explicitly what is NOT typed: `GLOBAL_CSBehavior`, `GLOBAL_AnimThreadMan`, `ChrIns` internals beyond what libER exposes. Output: `LIBER-API.md` with the typed signatures we'll use, plus a "manual offset work required" section listing what we have to RE ourselves.

- **0.1c: Veeenu Practice Tool technique read** (AGPL — read for technique only, no code copying). Specifically: how does it walk the enemy roster, resolve "the entity I'm targeting right now," and read live game state? Output: `PRACTICE-TOOL-NOTES.md` with technique notes — file:line citations to specific patterns we want to learn from (no code copying).

These run as three parallel Codex dispatches (high reasoning effort, read-only sandbox). Cost: ~30 min wall clock for all three.

#### 0.2 — Josh's preflight gates (~1–2 hours of Josh's time, ANY day this week)

Four small verifications, each blocking later work. **Josh has to do these on his Windows PC; orchestrator-Claude can't do them remotely.**

- **0.2a: Toolchain install.** Visual Studio 2022 Community (free, ~6GB download). Workloads needed: "Desktop development with C++" + Windows 11 SDK. Time: ~30 min mostly waiting on download. Test it works: open VS, File → New → Project → "Console App" → build the default Hello World. If it compiles and prints, toolchain is good.

- **0.2b: Defender exclusion.** Add `C:\src\parry-tell\` (or wherever the project will live) to Windows Defender exclusions. Settings → Privacy & Security → Windows Security → Virus & threat protection → Manage settings → Exclusions → Add or remove exclusions → Add a folder. This prevents Defender from quarantining the unsigned DLL we'll build.

- **0.2c: Hello-world DLL load test.** I write a 20-line `hello.dll` that just calls `OutputDebugStringA("[parry-tell] hello")` on attach. Josh drops it in Seamless's `external_dlls` slot, launches Seamless via `ersc_launcher.exe`, opens DebugView (free Sysinternals tool — `learn.microsoft.com/sysinternals/downloads/debugview`). If "hello" appears in DebugView, our DLL load path works. **This is the single most important preflight gate.** If this fails, nothing else matters and we have to debug ME2/Seamless config before any real code.

- **0.2d: GitHub repo creation.** Josh creates `iJoshh/parry-tell` (private at first; flips to public when v1 ships). Adds orchestrator-Claude as a deploy-key user OR Josh acts as the push proxy. Decision: **Josh as push proxy is fine** — orchestrator-Claude writes code locally on the Linux VM, Josh fetches via `git pull` on Windows. Avoids credential management.

#### 0.3 — Synthesis output

After 0.1 archaeology + 0.2 preflight complete, I produce a single-page `STEP-0-SUMMARY.md`:
- All four preflight gates: PASS / FAIL with notes
- Refreshed AOB/offset inventory for ER 1.16+ (or honest "we couldn't find this and need to RE it")
- Final go/no-go on Step 1 entry
- If any preflight FAILED, the plan stops here for diagnosis before Step 1

### Step 1: Gate 0 spike — `ChrIns → currentAtkParamId` (1–3 days, mostly my time, ~1 hour Josh's time)

**This is the load-bearing technical risk.** Per Codex review: this should be the first hard gate, not a step-1 sub-task.

#### What we build

A minimal probe DLL (no UI, no audio, no overlay):

1. On DLL load, spawn worker thread.
2. Worker polls every 100ms.
3. Walks `GLOBAL_WorldChrMan` → enumerates NPC `ChrIns` entities in render distance.
4. **The hard part:** for each NPC, resolves its `currentAtkParamId` via manual struct-offset traversal of the animation/behavior module. PostureBarMod's `Module0x18` widening pattern is the starting reference.
5. Looks up `AtkParam_Npc[currentAtkParamId].isDisableParry` from `SoloParamRepository` (typed via libER — this part IS one-line).
6. Writes one line to `parry-tell-probe.log` whenever an NPC's attack state changes:
   `[timestamp] NPC <handle> began attack <param_id> isDisableParry=<0|1>`

#### Success criterion

Log fills with sensible numbers when fighting an enemy. Specifically: against a Crucible Knight (canonical parryable-heavy enemy), the log should show some attacks with `isDisableParry=0` (the parryable ones — kick, sword overhead, certain combo enders) and most with `isDisableParry=1` (everything else).

#### Time budget for Gate 0: 3 days max

If we can't get reliable `currentAtkParamId` reads in 3 calendar days of effort, we **stop and ship Option A as v1** instead of pushing further. Option A foundation is the same DLL minus the `isDisableParry` lookup — it fires on every NPC attack windup. Less precise, still useful as a parry-timing trainer.

This is NOT a sunk-cost trap because Option A and Option B share the entire scaffold (DLL load, ChrIns walking, audio cue, crash insulation). Option A drops one specific RE step; everything else carries forward.

#### Josh's role in Step 1

- Pull latest from GitHub, build in VS (paste me errors if any)
- Drop probe DLL into Seamless `external_dlls`, launch ER, fight a Crucible Knight for 10 min
- Send me `parry-tell-probe.log`
- I read, declare success or pivot to Option A

### Step 2: Audio cue + production-quality v1 (3–5 days, mostly my time, ~2–3 hours Josh's time across testing sessions)

Once Gate 0 passes (or we've pivoted to Option A), build the actual v1.

#### What we build on top of probe

- **Audio cue.** Short distinct tone via WASAPI, or embedded WAV played via `PlaySound()`. Two configurable presets in v1: "tick" (subtle metronome click) and "ping" (brighter, more attention-grabbing). Volume configurable via INI.
- **Cue trigger logic.** Fire on `isDisableParry` 1→0 transition (Option B) OR `currentAnimation` 0→nonzero transition for known attack-class animations (Option A). NOT on every tick — only on state edges.
- **Cue debouncing.** If two NPCs both start parryable attacks within 50ms, fire once with slight intensity bump. Prevents audio chaos in group fights.
- **Crash insulation.** `__try/__except` wrapping every memory read. If a read AVs (access violation), log it and skip the frame. The mod must NEVER take down Josh's co-op session.
- **PvE-only filter.** Skip ChrIns entries whose `chrType` indicates a player character (invader / phantom / host). Prior research found the relevant field; we resolve it during Step 0 archaeology.
- **INI config file.** `parry-tell.ini` adjacent to the DLL with: `cue_preset=`, `volume=`, `enable_pve_only_filter=`, `log_level=`.
- **Compatibility check with PostureBarMod.** Run alongside PostureBarMod for 30 min in test fights. Both should display, no crashes, no FPS drop.

#### Quality gates before declaring v1 done

- [ ] 30 min play session against varied enemies without a crash
- [ ] Audio cue fires for known parryable attacks (Crucible Knight kick, Banished Knight sword, Godrick Soldier spear thrust per community lists) and does NOT fire for known un-parryable attacks (any boss grab, any explosion, any spell)
- [ ] No EAC alerts (visual confirmation of no EAC splash on launch)
- [ ] Co-op session held for 30 min without disconnect
- [ ] Compatibility verified with PostureBarMod loaded simultaneously

#### Josh's role in Step 2

- Run builds (every 1–2 days as I push commits)
- Test in 30-min sessions, capture logs/screenshots
- Make UX calls: cue volume defaults, tick vs ping default, anything that needs taste

### Step 3: GitHub release + optional Nexus polish (0.5–1 day)

- README with install instructions, Defender exclusion note, screenshot/audio sample
- LICENSE (MIT)
- Build instructions for source rebuild
- `version.json` with ER version compatibility metadata
- GitHub release `v0.1.0` with prebuilt DLL + source tag
- **Decision point: Nexus release.** Josh decides post-v1: is the polish level enough to risk Nexus moderator rejection? If yes, +0.5 day for demo video and Nexus mod page. If no, GitHub-only and we're done.

### Realistic timeline

Two estimates:

- **Optimistic (Gate 0 passes day 1, no surprises):** 5–7 days calendar, ~5 hours Josh time at keyboard
- **Realistic (Gate 0 takes 2–3 days, one MSVC linker round burns an evening, one ER patch mid-build):** 2–3 weeks calendar, ~12–15 hours Josh time at keyboard

Weighted expected: **~10–12 days calendar, ~8 hours Josh time at keyboard.**

This matches Codex's read of "data layer solved, engineering 2–3 weeks for someone in your position." Honest confidence on shipping v1 in 3 weeks: **55%.** On shipping eventually: **75%.** The 25% no-ship tail is dominated by Gate 0 failing AND Option A also having unforeseen blockers — unlikely but not zero.

## Open questions for Josh

These are the only decisions I want from you before kicking off Step 0:

1. **Working name.** I've been calling it `parry-tell` (a "tell" is what fighters call a body cue that betrays an incoming attack — feels apt). Other options: `parry-cue`, `parrygleam`, `tell.dll`. You'll live with this on GitHub. **Default if you don't care: `parry-tell`.**

2. **GitHub repo name and visibility.** Default: `iJoshh/parry-tell`, private until v1 ships, public on release. **Default if you don't care: that exact setup.**

3. **Audio cue style preference, blind.** "Tick" (subtle metronome click) vs "ping" (brighter, more attention-grabbing) vs "both, configurable, no preference." **Default if you don't care: "both, configurable, default to tick."**

4. **Repo handoff style.** Option A — Josh acts as push proxy: I write to a folder on the Linux VM, Josh `git pull`s on Windows. Option B — orchestrator-Claude pushes directly to GitHub via deploy key. **Default: Option A (simpler, no credential management).**

Everything else — license, architectural choices, audio implementation tech, INI schema — I'll make the call. Push back if you hate any of those defaults.

## Approval checklist before kicking off Step 0

- [ ] Josh accepts the plan top-line (commissioned build, audio-only v1, GitHub-first)
- [ ] Open questions 1–4 answered (or "use the defaults")
- [ ] Plan acknowledges: 55% confidence on v1 shipping in 3 weeks, 75% eventual
- [ ] Plan acknowledges: Gate 0 (`ChrIns → currentAtkParamId`) is the load-bearing risk, with clean Option A pivot
- [ ] Plan acknowledges: Nexus release is a stretch goal, GitHub release is the win condition
- [ ] Josh accepts ~8 hours of his keyboard time spread across 2–3 weeks (mostly running builds + testing in 30-min sessions)

Once all six checked, Step 0 dispatches three parallel Codex archaeology reads + we hand Josh his four preflight gates.
