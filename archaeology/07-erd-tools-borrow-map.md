# 07 - Erd-Tools Borrow Map

## Scope
- Repos reviewed:
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools/`
  - `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/`
- Focus: attack/behavior/AtkParam access, FD4 singleton strategy, versioning, and safety patterns for an injected C++ DLL build.

## 1) License Check (Can We Lift?)

### Erd-Tools (C#)
- I found **no top-level LICENSE/COPYING file** in this repo.
- The README describes the project and dependencies but does not state an OSS license grant (`erd-tools/README.md:1-21`).
- Solution includes vendored/submodule projects `PropertyHook` and `SoulsFormats` (`erd-tools/Erd-Tools.sln:8-10`, `erd-tools/.gitmodules:1-8`).

**Verdict:** direct code lifting from `erd-tools` is **not clearly licensed** from this checkout alone.

### Erd-Tools-CPP (C++)
- Top-level `LICENSE` is GNU GPL v3 (`erd-tools-cpp/LICENSE:1-2`).
- Copyright notice and grant are explicit GPLv3-or-later (`erd-tools-cpp/LICENSE:634-640`).
- README says “Feel free to use this in your mod” (`erd-tools-cpp/README.md:13-14`), but the repo’s formal license file is GPLv3.

**Verdict:** direct code lifting into an MIT project is **not compatible** unless you relicense your project to GPL-compatible terms or obtain separate permission.

## 2) Quick Tree Walk + Entry Points

### Erd-Tools (C#)
- Solution projects:
  - `Erd-Tools` (main)
  - `PropertyHook`
  - `SoulsFormats` (`erd-tools/Erd-Tools.sln:6-10`)
- `Erd-Tools` builds as a **library** (`<OutputType>Library</OutputType>`) (`erd-tools/src/Erd-Tools/Erd-Tools.csproj:3`).
- No `Program.cs`/`Main` entry; effective operational init is `ErdHook` constructor wiring AOB pointers (`erd-tools/src/Erd-Tools/Hook/ErdHook.cs:89-154`).

Core modules:
- Runtime hook + pointers: `Hook/ErdHook.cs`, `Hook/Offsets.cs`.
- Entity/runtime state model: `Models/Entities/Chr.cs`.
- Event-flag runtime access: `Models/CSFD4/CSFD4VirtualMemoryFlag.cs`.
- Param decoding and pointer maps: `Models/Params/Param.cs`, `Resources/Params/Pointers/ParamOffsets.txt`.

### Erd-Tools-CPP (C++)
- Solution has one DLL project (`erd-tools-cpp/Erd-Tools-CPP.sln:6`).
- Project type is dynamic library (`<ConfigurationType>DynamicLibrary</ConfigurationType>`) (`erd-tools-cpp/Erd-Tools-CPP/Erd-Tools-CPP.vcxproj:30`, `:43`, `:49`).
- Entry path:
  - `DllMain` spawns worker thread (`erd-tools-cpp/Erd-Tools-CPP/dllmain.cpp:10-19`).
  - Worker runs `CreateHook()` -> `HookEldenRing()` (`erd-tools-cpp/Erd-Tools-CPP/ErdToolsMain.cpp:7-15`, `:39-51`).
  - Hook bootstrap is `ErdHook::CreateMemoryEdits()` (`erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:10-25`).

Core modules:
- Signature discovery + singleton pointer resolution: `Hook/ErdHook.cpp`, `Include/Signature.h`.
- Feature hooks: `Hook/FeHook.cpp`, `Hook/DebugHook.cpp`, `Hook/EventHook.cpp`, `Hook/ParamHook.cpp`.
- Runtime structs: `Include/ErdTools_globals.h`.
- Param runtime editing: `Util/ParamEditor.h`.

## 3) What’s Unique Here vs PostureBarMod-era Coverage

1. **Large param offset registry in one file** (C#):
- `ParamOffsets.txt` maps many params directly by SoloParamRepository index offset (e.g. `AtkParam_Npc=0x280`, `AtkParam_Pc=0x2C8`, `BehaviorParam=0x3E8`, `BehaviorParam_PC=0x430`) (`erd-tools/src/Erd-Tools/Resources/Params/Pointers/ParamOffsets.txt:7-13`).

2. **C# event-flag fast path over `CSFD4VirtualMemoryFlag`**:
- Uses tree traversal + cache of group locations for faster repeated flag checks (`erd-tools/src/Erd-Tools/Models/CSFD4/CSFD4VirtualMemoryFlag.cs:95-147`).

3. **C++ reusable CE-pattern scanner implementation**:
- Parses CE-style patterns and scans executable PE sections via XOR+AND masked compare (`erd-tools-cpp/Erd-Tools-CPP/Include/Signature.h:6-8`, `:46-64`, `:145-171`).

## 4) Attack-State / Behavior / AtkParam Search Results

Search terms requested: `AtkParam`, `atkParamId`, `BehaviorJudge`, `behaviorJudge`, `currentAtk`, `behavior`.

### Erd-Tools (C#) hits
- Project includes Atk/Behavior resource files in csproj (schema/resource wiring, not runtime attack state):
  - `AtkParam*`, `BehaviorParam*` defs/names/pointers (`erd-tools/src/Erd-Tools/Erd-Tools.csproj:139`, `:151`, `:679`, `:682`, `:694`, `:697`, `:1258`, `:1261`, `:1273`, `:1276`).
- Param pointer registry includes attack/behavior param roots:
  - `AtkParam_Npc: 280`, `AtkParam_Pc: 2C8`, `BehaviorParam: 3E8`, `BehaviorParam_PC: 430` (`erd-tools/src/Erd-Tools/Resources/Params/Pointers/ParamOffsets.txt:7-13`).
- Runtime entity path exposes current animation only:
  - `Chr -> moduleBase(+0x190) -> actionRequest(+0x80) -> CurrentAnimation(+0x90)` via model code/offsets (`erd-tools/src/Erd-Tools/Models/Entities/Chr.cs:17-22`, `:101-103`; `erd-tools/src/Erd-Tools/Hook/Offsets.cs:271-305`, `:535-541`).

### Erd-Tools-CPP (C++) hits
- Runtime code (`Hook/`, `Include/`, `Util/`) has **no `currentAtk`/`atkParamId`/`behaviorJudge` runtime reader**.
- Atk/Behavior appear in generated param wrappers:
  - Includes in aggregate header (`erd-tools-cpp/Erd-Tools-CPP/param/params.h:11-12`, `:16-17`).
  - `AtkParam_Npc`/`AtkParam_Pc` wrappers (`erd-tools-cpp/Erd-Tools-CPP/param/AtkParam_Npc.h:8-11`, `param/AtkParam_Pc.h:8-11`).
  - `BehaviorParam`/`BehaviorParam_PC` wrappers (`erd-tools-cpp/Erd-Tools-CPP/param/BehaviorParam.h:8-11`, `param/BehaviorParam_PC.h:8-11`).
- Relevant schema fields exist in generated defs:
  - `behaviorJudgeId` in behavior schema (`erd-tools-cpp/Erd-Tools-CPP/param/defs/BEHAVIOR_PARAM_ST.h:17-20`).
  - `atkBehaviorId` and `isDisableParry` in attack schema (`erd-tools-cpp/Erd-Tools-CPP/param/defs/ATK_PARAM_ST.h:335-337`, `:739-741`).

### Definitive answer for runtime “what attack is this entity executing?”
- **No direct runtime read path found** in either repo for `ChrIns -> currentAtkParamId` or equivalent.
- What exists is:
  - runtime current animation-style reads (C# side)
  - static/generated Atk/Behavior param schemas
  - static param root offsets (SoloParamRepository indexed paths)

## 5) FD4 Singleton Finder Pattern

Expected pattern (from TGA) is not present as a generic finder/registrar in these repos.

- No `FD4Singleton Finder` / symbol registerer implementation found.
- C++ instead does explicit per-target signature scans for needed globals/singletons:
  - `SoloParamRepositoryAddress` (`erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:57-59`)
  - `WorldChrManIns` (`erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:115-117`)
  - `SoundIns` (`erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:118-121`)
- C# similarly uses explicit AOB patterns for `WorldChrMan`, `SoloParamRepository`, `CSFD4VirtualMemoryFlag` (`erd-tools/src/Erd-Tools/Hook/ErdHook.cs:104-108`, `:125-127`, `:134-135`; `erd-tools/src/Erd-Tools/Hook/Offsets.cs:66`, `:208`, `:569`).

**Conclusion:** Erd-Tools does **not** supply the TGA-style generic FD4 singleton finder; it remains AOB/signature-driven.

## 6) Memory Safety Patterns (C++ critical)

Observed protective patterns:
- Scanner validates module + PE headers + scans executable sections only (`erd-tools-cpp/Erd-Tools-CPP/Include/Signature.h:48-64`).
- Null checks on resolved entity pointers before bar writes (`erd-tools-cpp/Erd-Tools-CPP/Hook/FeHook.cpp:96`, `:119`).
- Wait loops for param repo pointer/load-state before param edits (`erd-tools-cpp/Erd-Tools-CPP/Hook/ParamHook.cpp:28-35`).

Missing/weak for crash containment:
- No `__try/__except` SEH wrappers found.
- No guarded RPM-style access layer; code frequently raw-dereferences deep pointer chains (e.g., `chrIns->chrModulelBag->staggerModule`) (`erd-tools-cpp/Erd-Tools-CPP/Hook/FeHook.cpp:97-100`, `:120-127`).

High-risk bug found:
- `ParamEditor::getParamResCap()` loops with `i < sizeof(_soloParamRepository->repositoryEntries)` (bytes, not count) (`erd-tools-cpp/Erd-Tools-CPP/Util/ParamEditor.h:272-275`).
- Actual entry count is 186 (`erd-tools-cpp/Erd-Tools-CPP/Util/ParamEditor.h:61`).
- This can read far past valid entries and is a potential memory-safety fault source.

## 7) Per-Version Offset Management

### Erd-Tools (C#)
- Centralized constants for offsets and AOB strings in one class (`erd-tools/src/Erd-Tools/Hook/Offsets.cs:3-17`, `:66`, `:208`, `:543`).
- No runtime game-version detection/gating in this repo’s hook entry path.

### Erd-Tools-CPP (C++)
- Uses many signatures to resolve function/global addresses at runtime (`erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:35-147`).
- Still depends on fixed struct layouts/field offsets in headers (e.g., `WorldChrMan`, `ChrIns`, module bag) (`erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:200-212`, `:262-265`).
- No explicit version enum/table/unsupported-version guard.
- Update practice is changelog/manual compatibility bumps (`erd-tools-cpp/README.md:47-58`, `:71-83`).

## 8) Comparison vs Existing Archaeology

| Pattern from Erd-Tools | Class | Why |
|---|---|---|
| `ChrIns -> module bag -> stagger/currentAnimation neighborhood` | DUPLICATE | Already captured from PostureBarMod and TGA (`archaeology/01-posturebarmod-borrow-map.md:127-136`, `archaeology/04-tga-cheat-table-techniques.md:51-63`). |
| SoloParamRepository param roots (`280/2C8`, `...->80->80`) | DUPLICATE | Already documented from TGA (`archaeology/04-tga-cheat-table-techniques.md:88-91`, `:120-121`). |
| AtkParam fields (`atkBehaviorId`, `isDisableParry`) | DUPLICATE | Already documented from TGA/libER passes (`archaeology/04-tga-cheat-table-techniques.md:72-74`, `:122-123`). |
| CE-style signature parser scanning executable sections | INCREMENTAL | Cleaner reusable scanner implementation than ad-hoc signature snippets; still AOB-dependent (`erd-tools-cpp/Erd-Tools-CPP/Include/Signature.h:6-8`, `:145-171`). |
| C# `CSFD4VirtualMemoryFlag` cached tree lookup | INCREMENTAL | Useful event-flag access optimization, but not the target `currentAtkParamId` path (`erd-tools/src/Erd-Tools/Models/CSFD4/CSFD4VirtualMemoryFlag.cs:95-147`). |
| Central `ParamOffsets.txt` mapping for many param roots | UNIQUE | Single compact index for many SoloParamRepository offsets (includes Atk/Behavior), convenient for tool bootstrap (`erd-tools/src/Erd-Tools/Resources/Params/Pointers/ParamOffsets.txt:1-13`). |
| Generic FD4 singleton finder/registerer | UNIQUE (negative) | Unique finding is absence: this repo does not contain TGA’s generic finder pattern (`erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:57-59`, `:115-117`; `archaeology/04-tga-cheat-table-techniques.md:33-35`). |

## 9) Concrete Findings for Our v1 Build

1. **Should we lift code directly from Erd-Tools-CPP?**
- **No, not directly into MIT**. Repository license is GPLv3 (`erd-tools-cpp/LICENSE:1-2`, `:637-640`).
- Use it as reference only, then implement clean-room equivalents.

2. **Is Erd-Tools FD4 singleton handling cleaner/more transferable than TGA’s CE/Lua finder?**
- **No for the generic finder goal**. Erd-Tools uses many explicit signatures, not a generic singleton finder/registrar (`erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:35-147`).
- For “skip stale AOBs,” TGA’s FD4 singleton workflow remains the stronger pattern source (`archaeology/04-tga-cheat-table-techniques.md:33-35`).

3. **Does Erd-Tools-CPP have the attack-state read we need?**
- **Definitive: No.**
- It has typed Atk/Behavior param schemas (`erd-tools-cpp/Erd-Tools-CPP/param/defs/ATK_PARAM_ST.h:335-337`, `:739-741`; `param/defs/BEHAVIOR_PARAM_ST.h:17-20`) but no runtime `ChrIns -> currentAtkParamId` reader in hook/runtime modules.

4. **One thing we should definitely take from Erd-Tools**
- Take the **param root offset registry idea** (`ParamOffsets.txt`) as a project-local source of truth for repository-indexed params (`erd-tools/src/Erd-Tools/Resources/Params/Pointers/ParamOffsets.txt:1-13`).

## Gate 0 Impact
- **Does this change Gate 0 plan?** **No material change.**
- It reinforces current plan points:
  - We still need a runtime bridge discovery for `currentAtkParamId`.
  - We still benefit from FD4-singleton-first root resolution (from TGA, not from Erd-Tools).
  - We still need version-conscious offset management.
- This aligns with existing Gate 0 assumptions (`archaeology/05-gate0-attack-plan.md:29-31`, `:51-54`).

