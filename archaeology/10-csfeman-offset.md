# CSFeManImp Offset for ER 1.16.1

## Findings

### TarnishedTool (`borgCode/TarnishedTool` @ `887a0d4b2ba4b48fd42c235cebab04e068368353`)
- Found? **No direct CSFeMan/FE-manager singleton symbol in `Offsets.cs`.**
- Evidence:
  - `Offsets.cs` defines many module-relative manager bases for `Version2_6_1`, but no `CSFeMan`, `FeMan`, `BossHpBar`, or equivalent FE manager entry appears in this file’s type/field set. See class definitions and base init region: `WorldChrMan`, `FieldArea`, `MenuMan`, etc. ([Offsets.cs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:60), [Offsets.cs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:931)).
- Offset value: **Not available from this source** for CSFeManImp.
- Sub-struct layout: **Not provided** in this source.
- Confidence: **High** (for “not present in this table”).

### Erd-Tools-CPP (`Nordgaren/Erd-Tools-CPP` @ `6d42204a88a25f8d73ed922d3280c6c075d639e5`)
- Found? **Yes, via AOB scan (not fixed module-relative constant).**
- Evidence:
  - AOB used to resolve `CSFeMan` global pointer location in hook init ([ErdHook.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:100), [ErdHook.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:102)).
  - Runtime usage dereferences pointer-to-pointer as singleton (`CSFeManImp* feMan = *...`) ([FeHook.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/FeHook.cpp:91)).
- AOB pattern (factual):
  - `48 8B 0D ?? ?? ?? ?? 8B DA 48 85 C9 75 ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B C8 4C 8D 05 ?? ?? ?? ?? BA B4 00 00 00 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 8B D3 E8 ?? ?? ?? ?? 48 8B D8`
  - Their resolver uses displacement extraction from the first `48 8B 0D` (`scan(..., 0x3, 0x7)`) to produce the absolute address of the global pointer storage.
- Offset value: **No fixed module-relative constant; scan-derived each run/build.**
- Sub-struct layout:
  - `CSFeManImp::bossHpBars` starts at `0x59F0 + 8*0x40 = 0x5BF0` from `CSFeManImp` base ([ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:251), [ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:253), [ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:254)).
  - `sizeof(BossHpBar) = 0x20` ([ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:234)).
  - `bossHandle` offset in `BossHpBar` is `+0x8` ([ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:226)).
  - Slot count = 3 (`bossHpBars[3]`) ([ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:254)).
- Confidence: **High**.

### Practice Tool (`veeenu/eldenring-practice-tool` @ `db9fbb1dd67ce92c8463b7e282c2687c16508ae1`)
- Found? **No direct CSFeMan/FE-manager singleton in generated base-address table.**
- Evidence:
  - `BaseAddresses` fields include many CS/world managers, but no `cs_fe_man` / FE HUD manager field ([base_addresses.rs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/lib/libeldenring/src/codegen/base_addresses.rs:5)).
  - ER `V2_06_1` table exists but still has no FE manager singleton field ([base_addresses.rs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/lib/libeldenring/src/codegen/base_addresses.rs:1232)).
- Offset value: **Not available from this source** for CSFeManImp.
- Sub-struct layout: **Not provided** in this source.
- Confidence: **High** (for “not present in this table”).

### PostureBarMod (`Mordrog/EldenRing-PostureBarMod` @ `0a26e52dc97c5ebc590007868e422529605d65ff`)
- Found? **Yes, via AOB scan + RIP-relative decode (older game baseline, but method matches Erd-Tools).**
- Evidence:
  - Same CSFeMan signature pattern and explicit `.Add(3).Rip()` decode path ([Hooking.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.cpp:34)).
  - Runtime dereference of scanned global pointer as `CSFeManImp*` ([PostureBarUI.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/PostureBarUI.cpp:438)).
  - Struct model matches Erd-Tools layout (`BossHpBar` size 0x20; `CSFeManImp` with `undefined[0x59F0]`, entity bars then boss bars) ([Hooking.hpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.hpp:19), [Hooking.hpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.hpp:41), [Hooking.hpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.hpp:45)).
- Offset value: **No fixed module-relative constant; scan-derived.**
- Sub-struct layout:
  - Same derived offset for `bossHpBars`: `0x5BF0` from `CSFeManImp` base.
  - Slot stride `0x20`, `bossHandle` at `+0x8`, slots = 3.
- Confidence: **Medium** for ER 1.16.1 direct applicability (repo targets older patch), **high** for structural corroboration.

## Recommended approach for our probe DLL
Use **AOB scan + RIP-relative decode** for the CSFeMan singleton pointer (same strategy as Erd-Tools-CPP), not a hardcoded module-relative constant.

Concrete approach:
1. Scan `eldenring.exe` `.text` for:
   - `48 8B 0D ?? ?? ?? ?? 8B DA 48 85 C9 75 ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B C8 4C 8D 05 ?? ?? ?? ?? BA B4 00 00 00 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 8B D3 E8 ?? ?? ?? ?? 48 8B D8`
2. Interpret match at first instruction `48 8B 0D disp32`:
   - Read `disp32` at `match+3`.
   - Compute `global_ptr_addr = match + 7 + disp32` (RIP-relative target).
3. Read `CSFeManImp* fe = *(CSFeManImp**)global_ptr_addr`.
4. Read current boss slots at `fe + 0x5BF0`, 3 slots, stride `0x20`, `bossHandle` at `slot + 0x8`.

Reasoning:
- No reliable ER 1.16.1 module-relative CSFeMan constant was found in the four source sets.
- Two independent tools converge on the same signature for this singleton.
- This avoids brittle patch-specific hardcoding.

## Sources

### Local file citations
- TarnishedTool:
  - [Offsets.cs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:60)
  - [Offsets.cs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:931)
- Erd-Tools-CPP:
  - [ErdHook.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:100)
  - [ErdHook.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:102)
  - [FeHook.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/FeHook.cpp:91)
  - [ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:223)
  - [ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:234)
  - [ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:251)
  - [ErdTools_globals.h](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:254)
- Practice Tool:
  - [base_addresses.rs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/lib/libeldenring/src/codegen/base_addresses.rs:5)
  - [base_addresses.rs](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/lib/libeldenring/src/codegen/base_addresses.rs:1232)
- PostureBarMod:
  - [Hooking.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.cpp:34)
  - [PostureBarUI.cpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/PostureBarUI.cpp:438)
  - [Hooking.hpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.hpp:19)
  - [Hooking.hpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.hpp:41)
  - [Hooking.hpp](/home/joshua.blattner/claude/elden-ring/.archaeology-sources/posturebarmod/Source/Main/Hooking.hpp:45)

### GitHub permalinks (commit-pinned)
- TarnishedTool (`887a0d4b2ba4b48fd42c235cebab04e068368353`):
  - https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/Offsets.cs
- Erd-Tools-CPP (`6d42204a88a25f8d73ed922d3280c6c075d639e5`):
  - https://github.com/Nordgaren/Erd-Tools-CPP/blob/6d42204a88a25f8d73ed922d3280c6c075d639e5/Erd-Tools-CPP/Hook/ErdHook.cpp
  - https://github.com/Nordgaren/Erd-Tools-CPP/blob/6d42204a88a25f8d73ed922d3280c6c075d639e5/Erd-Tools-CPP/Hook/FeHook.cpp
  - https://github.com/Nordgaren/Erd-Tools-CPP/blob/6d42204a88a25f8d73ed922d3280c6c075d639e5/Erd-Tools-CPP/Include/ErdTools_globals.h
- Practice Tool (`db9fbb1dd67ce92c8463b7e282c2687c16508ae1`):
  - https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/base_addresses.rs
- PostureBarMod (`0a26e52dc97c5ebc590007868e422529605d65ff`):
  - https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp
  - https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp
  - https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp

## Open issues
- **Module-relative offset for `CSFeManImp` in ER 1.16.1 was not found in these four sources.**
- If the above AOB fails on a future patch, fallback for first probe run:
  - Sweep candidate global pointers in `eldenring.exe` data sections for non-null pointers to writable regions.
  - For each candidate `p`, test `p + 0x5BF0 + i*0x20 + 0x8` (`i=0..2`) for plausible `bossHandle` behavior (often `0xFFFFFFFFFFFFFFFF` when empty; otherwise stable-looking handle-like values while boss bar is shown).
  - Correlate with on-screen boss bar transitions to lock the correct singleton.
