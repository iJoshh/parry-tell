# Phase 3 Offset Research — Codex

**Date:** 2026-05-07 ~10:30 CT
**Model:** gpt-5.3-codex (high reasoning effort, 7.7M input tokens through cache)
**Sandbox:** read-only against project root
**Goal:** Find 5 offset chains for production mod work.

---

## 1) Lock-on target handle

### Recommended chain
1. Install/use lock-target hook at `eldenring.exe + 0x717372` (2.6.1), which writes current target `ChrIns*` into a code-cave at `+0x560`.
2. Read handle from that target `ChrIns*` at `+0x8`.

**Cites:**
- `tarnishedtool/.../Memory/Offsets.cs:2151-2171` — lock-target hook offset
- `tarnishedtool/.../Memory/CodeCaveOffsets.cs:27-28` — code-cave layout
- `tarnishedtool/.../Services/TargetService.cs:22-35,43-44` — usage pattern
- `tarnishedtool/.../Memory/Pattern.cs:375-380` — sig pattern for the hook
- `tarnishedtool/.../Properties/Resources.resx:807-810` — hook bytes
- `tarnishedtool/.../Memory/Offsets.cs:136-139` — handle at +0x8 confirmation
- `tarnishedtool/.../Services/ChrInsService.cs:53-55`

### Alternate direct-field candidate
- `playerChrInsLike + 0x6A0` (`targetHandle`) from inferred `ChrIns` layout.
- Cites: `posturebarmod/Source/Main/Hooking.hpp:140-153`, `erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:205-212`

**Confidence:** High for hook chain, Medium for direct +0x6A0 read on our PlayerIns.

**PlayerIns vs ChrIns:** hook chain gives `ChrIns*` directly; no `GetChrInsFromHandle` needed there. Direct +0x6A0 chain has not been verified on our PlayerIns slot.

---

## 2) Target animation ID + animation time

### Recommended ID chain (target ChrIns*)
- `chrIns + 0x190` (module bag) → `+0x18` (TimeAct module) → `+0xD0` (AnimationId).

**Cites:**
- `tarnishedtool/.../Memory/Offsets.cs:173,205,238-241`
- `tarnishedtool/.../Services/ChrInsService.cs:221-223`

### Animation time
- **Not found** in PostureBarMod or TarnishedTool relative to a target ChrIns.
- Practice-tool has a separate hook-driven chain: `base_anim → +0x0 → +0x190 → +0x18 → {+0x20, +0x24, +0x2C}`.
- Cites: `practice-tool/lib/libeldenring/src/pointers.rs:394-396`, `.../codegen/base_addresses.rs:1262-1266`

### What PostureBarMod uses
- Defines `animModule` and `currentAnimation` (+0x20 inside module +0x18) but does not use animation-time telemetry in shipping code.
- Cites: `posturebarmod/Source/Main/Hooking.hpp:63-67,128-133`

**Confidence:** High for animation ID chain, Low/Unknown for target animation time offset.

**PlayerIns vs ChrIns:** use target `ChrIns*` (from lock hook or from handle resolution).

---

## 3) `chrModuleBag` pointer

### Recommended chain
- `playerIns = *( *(WorldChrMan.Base) + 0x1E508 )`, then `chrModuleBag = *(playerIns + 0x190)`.

**Cites:**
- `tarnishedtool/.../Memory/Offsets.cs:78-84` — PlayerIns offset by version (0x1E508 for 2.6.1)
- `tarnishedtool/.../Services/PlayerService.cs:223-224` — usage pattern
- `tarnishedtool/.../Memory/Offsets.cs:173` — chrModuleBag at +0x190
- `practice-tool/lib/libeldenring/src/pointers.rs:409` — cross-check
- `tga-cheat-table/.../Get functions.cea:97-109` — independent confirmation
- `tga-cheat-table/.../PlayAnimation_code.cea:10-13` — independent confirmation

### Canonical PostureBarMod path
- `playerArray[0] → handle → GetChrInsFromHandle(...) → chrIns->chrModuleBag`.
- Cites: `posturebarmod/Source/Main/Hooking.hpp:155-158`, `posturebarmod/Source/Main/PostureBarUI.cpp:452`

**Confidence:** High.

**PlayerIns vs ChrIns:** both observed; PostureBarMod prefers resolving to `ChrIns` first.

**Note (Claude):** Our probe data confirms `playerArray[0]` IS a PlayerIns at the same struct layout as TarnishedTool's `WCM + 0x1E508` PlayerIns. So `slot0_chrInsPtr + 0x190` should reach the same module bag. However, since we want consistency with PostureBarMod's pattern (which we already mirror for the hook), use the GetChrInsFromHandle round-trip and then read +0x190 off that.

---

## 4) Player HP / stagger module pointer

### Recommended chains
- `playerIns + 0x190 + 0x0` => HP/stat module (`Health=0x138`, `MaxHealth=0x13C`).
- `playerIns + 0x190 + 0x40` => stagger/poise module (`+0x10/+0x14/+0x1C`).

**Cites:**
- `tarnishedtool/.../Memory/Offsets.cs:204-216,208,269-274`
- `tarnishedtool/.../Services/ChrInsService.cs:122-129,131-138`
- `posturebarmod/Source/Main/Hooking.hpp:48-53,106-116,135`

**Confidence:** High.

**PlayerIns vs ChrIns:** direct from `PlayerIns` works in multiple sources; PostureBarMod still resolves via handle first.

---

## 5) Boss-bar singleton (`CSFeManImp::bossHpBars[3]`)

### Singleton offset from `eldenring.exe` base (2.6.1.0)
- **Most likely:** `GLOBAL_CSFeMan = moduleBase + 0x3D6B880` (from 64403584 decimal).
- Cite: `liber/symbols/singletons.csv:56`
- Sanity check: same file gives `GLOBAL_WorldChrMan = 64380808 (0x3D65F88)`, matching the known 2.6.1 WCM base from TarnishedTool and practice-tool.
- Cites: `liber/symbols/singletons.csv:214`, `tarnishedtool/.../Memory/Offsets.cs:949`, `practice-tool/.../codegen/base_addresses.rs:1254`

### `bossHpBars` layout
- `CSFeManImp` has `undefined[0x59F0]`, `entityHpBars[8]` (each 0x40), `bossHpBars[3]` (each 0x20).
- Cites: `posturebarmod/Source/Main/Hooking.hpp:8-12,19,39,41-46`, `posturebarmod/Source/Common.hpp:8-9`
- **Therefore `bossHpBars` starts at `CSFeManImp + 0x5BF0`** (computed: `0x59F0 + 8*0x40`), slot size 0x20.
- Per-slot `bossHandle` is at +0x8; empty-slot sentinel is `0xFFFFFFFFFFFFFFFF`.
- Cites: `posturebarmod/Source/Main/PostureBarUI.cpp:494`

**Confidence:** Medium for fixed RVA (`0x3D6B880`, single explicit source), High for layout/sentinel.

**Recommendation:** signature-scan the singleton (matching PostureBarMod and erd-tools-cpp's pattern) rather than hardcode `0x3D6B880`. We already do this for WCM and the GetChrInsFromHandle function in v5f.

**PlayerIns vs ChrIns:** boss-bar walk is independent; it uses boss handles from `CSFeManImp`, then resolves to `ChrIns` via `GetChrInsFromHandle`.
- Cites: `posturebarmod/Source/Main/PostureBarUI.cpp:494-497`

---

## Explicit unknowns

- **No trustworthy target-`ChrIns` animation TIME offset** in the provided sources. Practice-tool has a chain through a different hook-driven `base_anim` symbol; we'd need to chase that or accept that we time the parry window via animation-id transitions + frame counting against extracted TAE data.
- **No second independent source** besides `liber/singletons.csv` that hardcodes `CSFeManImp` RVA for 2.6.1; PostureBarMod and erd-tools-cpp both signature-scan instead. Use sig scan in production.

---

## Synthesis (Claude)

**For Phase 3.1 (offset-hunting probe v6):**

The single biggest open question for the parry-tell mod is animation TIME (or, equivalently: how do we know when the parry window opens within an attack animation?). Options:

1. **Read animation time from TimeAct module** — practice-tool says it lives at `chrIns+0x190 → +0x18 → +0x24` or +0x2C. Probe confirms by walking timestamp values during a known attack.
2. **Frame-counting against animation-ID transitions** — when animation ID flips to a parryable value, start a counter. Look up parry-window-start frame from extracted TAE data. No need to read live animation time.
3. **Hook a TAE-event dispatch function** — would let us listen for the actual "parry window opened" event emitted by the engine. More invasive; not yet researched.

Option 2 is the simplest and most decoupled. Animation time tracking is nice-to-have. The probe v6 should attempt option 1 (read +0x24/+0x2C) but the production mod can ship with option 2 if option 1 doesn't pan out.

**For boss-bar identification:**

The PostureBarMod path is fully documented and well-understood: walk `CSFeManImp::bossHpBars[3]`, skip slots where handle == UINT64_MAX, resolve each handle via `GetChrInsFromHandle` to a ChrIns, read animation/state off that ChrIns. We already have GetChrInsFromHandle scanned. We need to add a signature scan for CSFeManImp.

**For target-of-boss (the "is this boss attacking me?" question, called Gate 0.B in PHASE1-PLAN.md):**

Codex's research did NOT specifically address Gate 0.B (the AI-struct field that says "this enemy is currently targeting player X"). PHASE1-PLAN.md proposed `aiThink + SpEffectObserveEntry.Target` as first try, with a fallback of dumping `aiThink + 0xE000..0xF000` and diffing during target-switch events. This is still the path. Probe v6 should add this.

**For player-to-boss target (lock-on):**

Codex found a hook chain via TarnishedTool — install a hook at `eldenring.exe + 0x717372` that captures the target ChrIns* into a code-cave. This is invasive (writes a code-cave, hooks a game function we haven't analyzed). The alternate `playerIns + 0x6A0` approach is less invasive — read-only, our existing memory-read pattern. Probe v6 should test +0x6A0 first; fall back to the lock-target hook only if +0x6A0 doesn't pan out.
