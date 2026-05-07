# Phase 2 Research — Synthesis (Claude + Codex)

**Date:** 2026-05-07 ~00:35 CT
**Inputs:** `phase2-research-claude.md` (Claude independent), `phase2-research-codex.md` (Codex independent), TarnishedTool source verified directly post-synthesis.
**Status:** READY FOR JOSH SIGNOFF before any v5 code is written.

---

## Where we agree (high confidence)

1. **Use `WCM + 0x10EF8` as the local-player slot root, not `0x1E508`.**
2. **The slot is `ChrIns**` — a pointer-to-pointer, requiring two derefs (or, preferably, handle-based resolution).**
3. **Reading `playerArray[0]` directly as a `ChrIns*` is wrong. Production code (PostureBarMod) extracts the handle and resolves through the game's own `GetChrInsFromHandle` function.**
4. **The v4 probe's `0x1E508` reads weren't random — they were hitting a real game struct, just not a ChrIns. The "valid pointer, garbage entity_id" pattern is the signature of "right object location, wrong object type assumption."**
5. **Safety patterns to adopt for v5: signature-scanned globals, hooked-not-polled execution, null-guard every chain step, prefer game's own resolution functions over manual pointer chains.**

## Where we disagreed (and what resolved it)

**Claude's blind spot:** I claimed `0x1E508` was just wrong / a copy-paste error and dismissed it. Codex correctly noted that TarnishedTool DOES use `0x1E508` as `PlayerIns` for 2.6.1 — it's a real, intentional offset for a real game struct. I was hand-waving without reading TarnishedTool's source.

**Codex's gap:** Codex correctly identified `0x1E508` as `PlayerIns` but didn't fully spell out what `PlayerIns` IS structurally vs `ChrIns`. After verifying TarnishedTool's `Offsets.cs:104-133` directly post-synthesis:

**`PlayerIns` is NOT `ChrIns`.** TarnishedTool defines them as distinct types with distinct fields:

| Field | PlayerIns offset | ChrIns offset |
|---|---|---|
| Handle | +0x8 | +0x8 |
| CurrentBlockId | +0x6D0 (2.6.1) | — |
| EntityId | — | +0x1E8 (2.6.1) |
| BlockId | — | +0x38 |
| ChrId | — | +0x64 |
| TeamType | — | +0x6C |

`PlayerIns` is a **player-specific wrapper struct** at `WCM + 0x1E508` with a different field layout than ChrIns. It has its own way to access player state (BlockId is at +0x6D0, NOT +0x38).

**This is exactly why v4's reads at `0x1E508 + 0x1E8` (treating it as ChrIns) returned `entity_id = 0` for 20 frames straight. There's no entity_id at +0x1E8 of a PlayerIns. The probe was reading whatever game data lives at that offset of a PlayerIns and labeling it "entity_id." That's not a bug in the offset — it's a bug in the type assumption.**

## Reconciliation summary

The two offsets are **not competing alternatives** for the same value. They're roots into two different structures, both of which can lead to the local player:

```
Path A (PostureBarMod):  WCM + 0x10EF8 → ChrIns** → ChrIns* → ChrIns
Path B (TarnishedTool):  WCM + 0x1E508 → PlayerIns* → PlayerIns
                         (PlayerIns has its own player-relevant fields,
                          and contains a Handle at +0x8 that resolves to ChrIns
                          via GetChrInsFromHandle if needed)
```

Both paths reach the player. PostureBarMod uses Path A because it needs deep ChrIns access (stagger module, target handle). TarnishedTool uses Path B because it operates on player-level fields like map coords, current block, teleport state — things PlayerIns exposes directly without a ChrIns deref.

**For our parry-tell mod, we want Path A.** We need ChrIns-level fields (target handle, animation state, stagger module) that aren't exposed at PlayerIns level. PostureBarMod's pattern is the right reference because we're doing the same kind of work it does.

## ChrIns offset verification (2.6.1)

Codex verified directly against TarnishedTool source. All three of v4's offsets are CORRECT for 2.6.1:

- `entity_id` at +0x1E8: confirmed (TarnishedTool `Offsets.cs:146-152`)
- `block_id` at +0x38: confirmed (`Offsets.cs:139`)
- `chr_type` at +0x64: confirmed (`Offsets.cs:142`, named ChrId)

**The offset fixes in v4 were correct. The bug was the access path before reaching ChrIns — using `WCM + 0x1E508` and treating it as ChrIns directly meant we never reached a ChrIns at all.**

## Safety patterns — combined recommendation

From PostureBarMod (in-engine hook, slot-0-via-handle pattern):
- Resolve all globals/functions via signature scan once at startup
- Gate every chain step: `if (!worldChar || !feMan || !GetChrInsFromHandleFunc) return;`
- Use the game's own `GetChrInsFromHandle` function — handle is stable, raw pointers are not
- Run inside a hooked function (UI render path), not a separate polling thread

From TarnishedTool (external memory-reader pattern):
- Periodic polling at ~64ms cadence (15Hz), not per-render-frame
- Re-resolve all roots on every poll cycle — no caching
- Strict null/canonicality checks per hop
- Generic handle→pool→slot lookup for arbitrary character resolution

**For v5, take the PostureBarMod pattern**: hook-driven, slot-0, handle-resolved. The polling thread approach is structurally riskier (race with game thread writes) and we already know our v4 polling thread caused crashes.

## What's locked for v5 design

1. **Player chain access:** `WCM + 0x10EF8` → `ChrIns** slot0` → `(*slot0)->handle` → `GetChrInsFromHandle(world, &handle)` → `ChrIns*`
2. **ChrIns offsets (2.6.1):** entity_id +0x1E8, block_id +0x38, chr_type +0x64, handle +0x08, modules +0x190, target_handle +0x6A0
3. **Execution context:** hook a frequently-called game function (PostureBarMod hooks UI update). Do NOT use a separate polling thread.
4. **Null-guards everywhere.** Every deref. Including `GetChrInsFromHandle`'s return.
5. **No high-frequency reads of unrelated memory.** No WCM dump, no prio queue walk in v5.
6. **Hotkey gating during testing:** even with hook-based execution, only sample on hotkey press during validation runs. Add steady-state sampling later once we trust the chain.

## Open question for Josh

**The Seamless Co-op slot question is unresolved.**

PostureBarMod assumes `playerArray[0]` is the local player. That's true for vanilla solo and host. **It may be wrong for guest in Seamless Co-op** — the guest might be at slot 1, 2, or 3.

Options:
- **A:** Test with Josh as host first to validate the pattern works at all, then re-test as guest. Two test sessions instead of one.
- **B:** Have v5 probe all 4 slots and log which one matches (compare entity_id with what we know is Josh).
- **C:** Skip the question for now — write v5 assuming slot 0, test as host, ship guest support as a follow-up.

I recommend **B** — probing all 4 slots adds maybe 20 lines of code, gives us a definitive answer in one test session, and is read-only / safe.

## Disambiguation experiments (for v5)

If we want belt-and-suspenders confidence:

1. **Side-by-side capture:** v5 reads BOTH paths (Path A via 0x10EF8 + handle resolution AND Path B via 0x1E508 + handle resolution). Compare ChrIns* results. Should match if both paths are valid for the local player.
2. **Slot probe:** read all 4 entries of `playerArray` (slots 0-3), log handle for each. The one matching Josh's known handle is his slot.
3. **Stability over time:** sample once per second for 60 seconds. ChrIns pointer should be stable (cached by the game) or, if it changes, the handle should be stable across changes.

## Confidence

**Very high** that `0x10EF8` + handle-based resolution is the right pattern. PostureBarMod, Erd-Tools-CPP, and Hexinton all converge on it. TarnishedTool uses a different but valid path for different purposes.

**Medium** on Seamless Co-op slot — needs an empirical test to settle.

**High** that v5 with this design will not crash. We've identified the v4 failure mode (wrong type assumption causing reads of arbitrary memory at +0x1E8 offsets), and the new design avoids that entirely.

---

## Recommendation: ready to design v5

We have enough to lock the v5 design. Two follow-ups before code:

1. **Josh signoff** on this synthesis (especially: Path A vs Path B choice, slot 0 vs slot probing).
2. **Optional: fresh Codex pass** to adversarially review v5 source before it ships, per writer-pairing rule. Highest priority would be checking the SEH wraps and the `GetChrInsFromHandle` signature scan logic.

If green-light, I draft `PHASE2-PLAN.md` with the v5 spec next.

---

## Appendix: file references for verification

**PostureBarMod:**
- `posturebarmod/Source/Main/Hooking.hpp:140-159` — ChrIns + WorldChrMan struct
- `posturebarmod/Source/Main/PostureBarUI.cpp:435-475` — access pattern with guards
- `posturebarmod/Source/Main/Hooking.cpp:29-55` — signature scan setup

**TarnishedTool:**
- `tarnishedtool/TarnishedTool/Memory/Offsets.cs:78-84` — PlayerIns offset by version (0x1E508 for 2.6.1)
- `tarnishedtool/TarnishedTool/Memory/Offsets.cs:104-133` — PlayerInsOffsets (Handle at +0x8, CurrentBlockId at +0x6D0)
- `tarnishedtool/TarnishedTool/Memory/Offsets.cs:136-152` — ChrIns offsets (EntityId 0x1E8, BlockId 0x38, ChrId 0x64)
- `tarnishedtool/TarnishedTool/Services/PlayerService.cs:223-224` — PlayerService access pattern

**Erd-Tools-CPP:** confirms 0x10EF8 playerArray (`Erd-Tools-CPP/Include/ErdTools_globals.h:262-265`)

**Hexinton:** uses both 0x10EF8 (line 508, 1056-1058) and 0x1E508 (line 14190) — different scripts for different purposes, supporting the "two valid paths" reconciliation.

**Live evidence:** WCM dump shows 0x10EF8 populated with heap pointer, 0x1E508 zero at probe-init. v4 CSV shows 0x1E508 path returning entity_id=0 for 20 reads (consistent with PlayerIns having no field at +0x1E8).
