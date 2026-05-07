# Elden Ring 2.6.1.0 — Local Player ChrIns Offset from WCM (Independent Analysis)

## 1) Headline finding
Use **`WCM + 0x10EF8`** as the local-player slot root in ER 2.6.1.0.

More precise chain (from production code semantics):
- `WCM + 0x10EF8` gives slot-0 pointer metadata (`ChrIns**`-style root), not a guaranteed final `ChrIns*` by itself.
- Resolve final `ChrIns*` via handle lookup (`GetChrInsFromHandle`) from `&(*slot0)->handle`.

I do **not** recommend `WCM + 0x1E508` as a direct local-player `ChrIns*` source.

## 2) Evidence trail
### A. Live WCM dump bytes (same run folder)
Dump file: `/home/joshua.blattner/claude/elden-ring/probe/runs/run-3-2026-05-06-2255-CT-v4-CRASH/parry-tell-probe-wcm-dump.bin`

At `WCM + 0x10EF8`, the bytes decode to a heap-shaped pointer:

```text
00010ef0: 0600000018112b21 00dea45af47f0000
00010f00: ffffffff58d9c715 58181921f67f0000
```

- `0x10EF8` raw 8 bytes: `00 de a4 5a f4 7f 00 00`
- decoded qword: `0x00007FF45AA4DE00` (heap-like)

At `WCM + 0x1E508`, bytes are zero in this dump:

```text
0001e500: 0000000000000000 0000000000000000
0001e510: 0000000000000000 00f48c1effffffff
```

- `0x1E508` raw 8 bytes: `00 00 00 00 00 00 00 00`
- decoded qword: `0x0000000000000000`

Nearby `0x1E53x` region looks like mixed pointers + float-ish data (`BF800000` repeats), i.e. not an obvious stable “local ChrIns pointer slot” layout.

Audit artifacts saved:
- `/home/joshua.blattner/claude/elden-ring/research/codex-workspace/wcm-xxd-0x10EE0-0x120.txt`
- `/home/joshua.blattner/claude/elden-ring/research/codex-workspace/wcm-xxd-0x1E500-0x1A0.txt`
- `/home/joshua.blattner/claude/elden-ring/research/codex-workspace/wcm-qword-neighborhoods.txt`

### B. PostureBarMod source (production/shipping)
PostureBarMod models `WorldChrMan` as:
- `unk[0x10EF8]`
- then `ChrIns** playerArray[0x4]`

Source:
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.hpp:155-158`

It accesses local player via slot 0 and resolves through game function:
- `GetChrInsFromHandleFunc(worldChar, &(*worldChar->playerArray[0])->handle)`

Source:
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/PostureBarUI.cpp:452`

`GetChrInsFromHandle` signature scan:
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.cpp:49-51`

### C. TarnishedTool source (2.6.1 table + accessor)
TarnishedTool offset table sets `WorldChrMan.PlayerIns = 0x1E508` for 2.6.1-era versions:
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:78-84`

Accessor is a direct pointer chain:
- `Read(Read(WorldChrMan.Base) + WorldChrMan.PlayerIns)`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Services/PlayerService.cs:223-224`

So TarnishedTool’s `0x1E508` is definitely used as a **player-ins chain root** in its own model, but that does not prove it is a stable direct `ChrIns*` slot for your use-case.

### D. CSV context from same run
Comments repeatedly show the probe’s `+0x1E508` chain yielding pointers whose `entity_id` reads as 0, then later obvious garbage/non-canonical values.

Examples:
- `/home/joshua.blattner/claude/elden-ring/probe/runs/run-3-2026-05-06-2255-CT-v4-CRASH/parry-tell-probe.csv:13-14`
- `/home/joshua.blattner/claude/elden-ring/probe/runs/run-3-2026-05-06-2255-CT-v4-CRASH/parry-tell-probe.csv:593-627`
- `/home/joshua.blattner/claude/elden-ring/probe/runs/run-3-2026-05-06-2255-CT-v4-CRASH/parry-tell-probe.csv:1399-1403`

That behavior is consistent with “wrong object / unstable chain root for this purpose.”

### E. Additional cross-reference (Hexinton + Erd-Tools-CPP)
Hexinton contains both patterns (mixed ecosystem evidence):
- Defines `LocalPlayerOffset = 10EF8`
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/hexinton/eldenring_all-in-one_Hexinton_v2.93_ce7.5.CT:508`
- Uses `mov rcx,[rcx+000010EF8]`
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/hexinton/eldenring_all-in-one_Hexinton_v2.93_ce7.5.CT:1056-1058`
- Also has scripts using `+1E508`
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/hexinton/eldenring_all-in-one_Hexinton_v2.93_ce7.5.CT:14190`

Erd-Tools-CPP independently mirrors PostureBarMod’s `0x10EF8` `playerArray` layout:
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:262-265`

## 3) Reconciliation of `0x10EF8` vs `0x1E508`
Best-fit explanation from sources:
- `0x10EF8` is the **player slot array root** used with handle-based resolution (`GetChrInsFromHandle`). This is what multiple production mods agree on.
- `0x1E508` is likely a **different player-related structure root** (TarnishedTool calls it `PlayerIns`) that may overlap with some Chr-like fields but is not a robust direct `ChrIns*` slot for your probe’s assumptions.

Why your probe saw heap-looking values at `+1E508` but bad `entity_id`:
- A valid pointer can still be the wrong object type.
- CSV shows intermittent garbage/null transitions for that chain.
- In this dump snapshot, `+1E508` is literally zero while `+0x10EF8` is non-zero pointer-shaped.

So these are very likely **different things**, not two equivalent “local player ChrIns pointer” offsets.

## 4) ChrIns layout for 2.6.1 (verify v4 offsets)
From TarnishedTool 2.6.1 tables:
- `entity_id` at `+0x1E8`: **matches**
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:146-152`
- `block_id` at `+0x38`: **matches**
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:139`
- `chr_type` at `+0x64`: **offset matches**, but TarnishedTool names it `ChrId` (semantic label differs)
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:142`

## 5) Safety patterns to adopt (from real access code)
### PostureBarMod patterns
- Resolve globals/functions by signature once; avoid hardcoded absolute addresses.
  - `Hooking.cpp:29-55`
- In hot path, gate all roots before deref (`worldChar`, `feMan`, function ptr).
  - `PostureBarUI.cpp:441-447`
- Resolve ChrIns via **game function** (`GetChrInsFromHandle`) instead of deep manual pointer chains.
  - `PostureBarUI.cpp:452`
- Use slot/handle validity checks (`!= UINT64_MAX`) and null checks before module reads.
  - `PostureBarUI.cpp:494-503`
- Execute off an in-engine callback (`UpdateUIBarStructs` hook), not an aggressive external scanner loop.
  - `Hooking.cpp:53-59`, `PostureBarUI.cpp:428-430`

### TarnishedTool patterns
- For generic character resolution, prefer deterministic handle->pool->slot lookup.
  - `ChrInsService.cs:230-239`
- Uses periodic polling (`GameTickService` 64ms interval), not per-render-frame spin.
  - `GameTickService.cs:20`, `PlayerViewModel.cs:739,859-874`
- BUT pointer-chain helper has minimal internal guarding (no canonicality/null-path policy beyond read success).
  - `MemoryService.cs:176-191`

### Practical adoption for parry-indicator
- Anchor local player on `WCM+0x10EF8` slot 0 path, then resolve through handle.
- Keep strict guards per hop: null, canonical user-range, alignment, and expected field sanity.
- Prefer event-driven/hook-driven sampling for combat state; avoid broad high-frequency memory walks.

## 6) Confidence and uncertainty
Confidence: **medium-high** on `0x10EF8` being the correct local-player slot root for your purpose.

Why not absolute high:
- Your provided binary dump is captured at probe init (`README.md:12`), not guaranteed steady-state gameplay.
- `0x1E508` is still actively used by TarnishedTool’s `PlayerIns` model, so it is not random noise; it is likely a different object path.

What would change my mind:
- A controlled live capture showing `WCM+0x10EF8` path failing while `+0x1E508` consistently yields a valid ChrIns with stable expected fields during gameplay.

Best disambiguation experiments:
1. In one live session, sample both chains side-by-side at fixed cadence and log canonicality + `block_id` + `chr_id` + `entity_id` stability.
2. Compare `GetChrInsFromHandle(world, &(*slot0)->handle)` result against each chain result every sample.
3. Add a type-sanity gate: reject candidates where key module pointers/flags are implausible for ChrIns.

## 7) Open questions
- Exact runtime type at `WCM+0x1E508` in 2.6.1 (PlayerIns/session object layout) remains unresolved from these artifacts alone.
- Whether local player `entity_id == 0` is expected in some contexts or always a red flag in your environment.
- Whether `playerArray[1..3]` semantics (host/phantom slots vs other roles) matter for your multiplayer target model.

---

## Audit output location
All raw outputs used here were written to:
`/home/joshua.blattner/claude/elden-ring/research/codex-workspace/`
