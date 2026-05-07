# TarnishedTool Borrow Map

## Repo summary
- **Repo**: `https://github.com/borgCode/TarnishedTool`
- **Local source path**: `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/`
- **Local HEAD commit**: `887a0d4b2ba4b48fd42c235cebab04e068368353` (`887a0d4 torrent anywhere fix for abyssal woods (#97)`)
- **License**: MIT (`LICENSE:1-21`)
- **Language/runtime**: C# WPF on .NET Framework 4.8 (`TarnishedTool.csproj:13`), C# LangVersion 12 (`TarnishedTool.csproj:18`)
- **Patch targets**: README states support from 1.02 to 1.16.1 (`README.md:6`); enum maps `Version2_6_1` to ER 1.16.1 (`Enums/GameVersion.cs:31`)

## License note
MIT is confirmed in-repo (`LICENSE:1-21`). Relevant grant text:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files ... to deal in the
> Software without restriction ...

`LICENSE:5-9,12-13` explicitly allows copying/modifying/distributing with attribution notice retained.

For our use case (porting ideas/offset chains to C++):
- Offset numbers/pointer-chain facts are factual data, not expressive code.
- Still credit TarnishedTool in our docs/README for provenance and maintenance goodwill.

## File-by-file map of the attack-reading pipeline

### Layer 1: Memory primitive

**Conclusion**: TarnishedTool is an **external process** that attaches to `eldenring.exe` via Win32 process APIs, then writes remote code and installs inline hooks.

- Process attach + handle acquisition:
  - `OpenProcess(PROCESS_VM_READ|WRITE|OPERATION|QUERY_INFORMATION)` (`Services/MemoryService.cs:257-260`)
  - `Process.GetProcessesByName("eldenring")` (`Services/MemoryService.cs:253-255`)
- Raw memory IO:
  - `ReadProcessMemory` wrapper (`Services/MemoryService.cs:64-69`, `Memory/Kernel32.cs:27-33`)
  - `WriteProcessMemory` wrapper (`Services/MemoryService.cs:130-133`, `Memory/Kernel32.cs:31-33`)
- Remote execution/injection primitives:
  - `VirtualAllocEx` + `CreateRemoteThread` + `WaitForSingleObject` (`Services/MemoryService.cs:193-205`, `154-159`, `161-174`, `207-222`)
- Hooking mechanism:
  - Inline `E9` detour written at origin (`Memory/HookManager.cs:26-53`), restored on uninstall (`55-62`)

**Injected-DLL delta for us**:
- We should not use `ReadProcessMemory` on self; we can direct-deref (with safety guards).
- We can keep pointer-chain logic and offsets exactly, but replace remote-thread shellcode orchestration with in-process calls/hooks.

### Layer 2: WorldChrMan resolution

TarnishedTool supports two address-resolution modes:

1. **Per-version static table (primary path)**
- Patch version read from process file version (`Utilities/PatchManager.cs:15-21`)
- File-version -> internal enum mapping via `Offsets.Initialize(...)` (`Memory/Offsets.cs:15-45`)
- `WorldChrMan.Base = moduleBase + versionSpecificOffset` (`Memory/Offsets.cs:933-951`)

2. **AOB fallback scan (unknown patch)**
- If version mapping fails, UI loop calls fallback scanner (`MainWindow.xaml.cs:204-209`)
- AOB scans include `Pattern.WorldChrMan` -> `WorldChrMan.Base` (`Memory/AobScanner.cs:35`, `Memory/Pattern.cs:18-28`)

**Per-version handling** is explicit and broad (1.02 -> 1.16.1) in `Offsets.cs` tables and `GameVersion` enum.

### Layer 3: ChrIns enumeration

Core enumeration path is in `ChrInsService.GetNearbyChrInsEntries()`:

1. Resolve world manager object pointer:
- `worldChrMan = Read<nint>(WorldChrMan.Base)` (`Services/ChrInsService.cs:25`)

2. Read begin/end pointers of update-priority ChrIns array:
- `begin = Read<nint>(worldChrMan + WorldChrMan.ChrInsByUpdatePrioBegin)` (`Services/ChrInsService.cs:26`)
- `end = Read<nint>(worldChrMan + WorldChrMan.ChrInsByUpdatePrioEnd)` (`Services/ChrInsService.cs:27`)

3. Iterate contiguous 8-byte entries:
- `ChrInsEntrySize = 0x8` (`Services/ChrInsService.cs:18`)
- Buffer parse -> each entry is a `chrIns` pointer (`Services/ChrInsService.cs:31-36`, `276-281`)

4. Filter invalid block id:
- Skip if `*(chrIns + ChrIns.BlockId)` equals `0xFFFFFFFF` (`Services/ChrInsService.cs:37-39`, `Memory/Offsets.cs:139`)

**Relevant offsets (1.16+ path):**
- `WorldChrMan.ChrInsByUpdatePrioBegin = 0x1F1B8` (`Offsets.cs:86-93`)
- `WorldChrMan.ChrInsByUpdatePrioEnd = 0x1F1C0` (`Offsets.cs:95-102`)

### Layer 4: ChrIns -> active attack/behavior resolution (THE LOAD-BEARING PIECE)

## Critical result
TarnishedTool does **not** expose a `ChrIns -> currentAtkParamId` read path in C# services/viewmodels.

What it *does* expose live from ChrIns/AI:

1. **Current animation id**
- Chain: `chrIns -> [Modules(+0x190)] -> [ChrTimeAct module(+0x18)] -> +0xD0`
- Definitions: `ChrTimeActModule=[0x190,0x18]`, `AnimationId=0xD0` (`Memory/Offsets.cs:173,205,238-241`)
- Read site: `GetCurrentAnimation(...)` (`Services/ChrInsService.cs:221-223`)

2. **Enemy Act bytes (LastAct/ForceAct) from AiThink**
- AiThink chain definition: `[ChrManipulator, 0xC0]` where `ChrManipulator` is `0x580` for modern patches (`Memory/Offsets.cs:193-199,295`)
- `LastAct` offset is `0xE9C2` for modern patches (`Memory/Offsets.cs:338-344`)
- Read site: `TargetService.GetLastAct()` (`Services/TargetService.cs:74-75`)
- UI labels “Last Act” / “Current Animation” shown in target tab (`Views/Tabs/TargetTab.xaml:589-617`) and Resistances overlay (`Views/Windows/ResistancesWindow.xaml:112-127`)

3. **AI cooltime list of animation IDs (not AtkParam IDs)**
- `AiAttackComp` offset, `CoolTimeCount`, `CoolTimeList` read (`Memory/Offsets.cs:314-320,396-407`; `Services/AiService.cs:137-155`)
- Displayed as `ID: {AnimationId}` in AI window (`Views/Windows/AiWindow.xaml:83-94`)

### What the AttackInfo hook actually reads
The `AttackInfo` system is not a `currentAtkParamId` reader. It hooks a combat/damage path and logs damage breakdown for the **locked target**.

- Hook toggled by target UI checkbox (`ViewModels/TargetViewModel.cs:774-785`)
- Hook installation/wiring (`Services/AttackInfoService.cs:19-43`)
- Hook pattern location (`Memory/Pattern.cs:416-421`) and per-version address table (`Memory/Offsets.cs:2244-2263`)
- Ring-buffer polling in UI tick (`Services/AttackInfoService.cs:51-90`, `ViewModels/TargetViewModel.cs:1118-1124`)

From `Resources.resx` `SaveAttackInfo` assembly:
- Filter is locked target compare: `cmp rdx, [rsi+0x1e0]` (`Properties/Resources.resx:964-967`)
- Captures damage fields from `rsi` offsets (raw/final splits, poise, type) (`Resources.resx:978-1004`)
- “EnemyId” written from `[rdx+0x60]` (`Resources.resx:1005-1006`)

And `ChrIns + 0x60` is `NpcParamId` (`Memory/Offsets.cs:141`), so AttackInfo’s `EnemyId` column is functionally **NpcParamId of locked target**, not `AtkParamId`.

### Requested term sweep results

Search scope was whole `TarnishedTool/` plus a code-only pass excluding `TarnishedTool/Properties/*`.

- `currentAtk`, `CurrentAtk`, `attackParamId`, `atkParamId`:
  - **No hits in code paths** (services/viewmodels/memory logic).
- `BehaviorJudgeId`, `behaviorJudge`, `addBehaviorJudgeId`:
  - Hits exist in param-definition resources, not runtime readers:
    - `Properties/Resources.resx:129785` (`behaviorJudgeId`)
    - `Properties/Resources.resx:6016` (`addBehaviorJudgeId_condition`)
    - `Properties/Resources.resx:7688` (`addBehaviorJudgeId_add`)
- `isDisableParry`:
  - Hit exists in param-definition resources only (`Properties/Resources.resx:54357-54358`), no runtime logic reads this field by name.
- `Crucible`:
  - Many string-table/content hits (names, labels, ids) in `Resources.resx`, but no code-path hardcoding of “Crucible-specific parry logic” in services/viewmodels.

### Layer 5: Param table access

Runtime param row access is in `ParamService.GetParamData(...)`:

1. `soloParamRepo = Read<nint>(SoloParamRepositoryImp.Base)` (`Services/ParamService.cs:198`)
2. `tableBase = soloParamRepo + tableIndex * 0x48` (`Services/ParamService.cs:201`)
3. `paramResCap = Read<nint>(tableBase + 0x88 + slotIndex*8)` (`Services/ParamService.cs:206`)
4. `ptr1 = Read<nint>(paramResCap + 0x80)` (`Services/ParamService.cs:209`)
5. `paramData = Read<nint>(ptr1 + 0x80)` (`Services/ParamService.cs:212`)
6. `descriptorBase = paramData + 0x40`, row descriptor stride `0x18` (`Services/ParamService.cs:215-217`, `22,26`)

Then `GetParamRow(...)` binary-searches row IDs and returns row pointer (`Services/ParamService.cs:13-37`).

**Atk/Behavior indices are explicit**:
- `AtkParam_Npc` table index `7`, slot `0` (`Services/ParamRepository.cs:23`)
- `BehaviorParam` table index `12`, slot `0` (`Services/ParamRepository.cs:25`)

**Comparison to TGA chain**:
- TarnishedTool path is not the exact `0x280 -> 0x80 -> 0x80 -> 0` shape documented elsewhere; it is `+0x88 -> +0x80 -> +0x80` from table base after `tableIndex*0x48`.

### Layer 6: The AttackInfo pipeline

Trigger and flow:
1. User enables “Show Attack Info” (`TargetTab.xaml:637-639`)
2. `TargetViewModel` toggles hook on/off (`TargetViewModel.cs:774-785`)
3. `AttackInfoService.ToggleAttackInfoHook(true)` writes trampoline code and detours `Hooks.AttackInfo` (`AttackInfoService.cs:19-43`)
4. Hooked assembly (`SaveAttackInfo`) writes entries into 16-slot code-cave ring buffer (`CodeCaveOffsets.AttackInfoStart`, struct size `0x48`) (`CodeCaveOffsets.cs:40-43`; `AttackInfoService.cs:16-18`)
5. On each game tick (`64ms` timer), `TargetTick` polls ring buffer and appends entries (`GameTickService.cs:20-21`; `TargetViewModel.cs:1118-1124`)

Behavioral implications:
- Capture is **event-driven in hooked code**, not a pure frame poll of attack state.
- UI consumption is polled (`64ms`) but reads previously captured events.
- Capture is **locked-target scoped** due to compare in asm (`Resources.resx:964-967`).
- Not a general per-enemy “currently executing AtkParamId” stream.

## The translation table — C# patterns to our C++ build

| Pattern | TarnishedTool implementation (paraphrased) | C++ equivalent sketch | Gotcha |
|---|---|---|---|
| External process memory read | `ReadProcessMemory` wrappers (`MemoryService.Read<T>`) | `template<typename T> T Read(uintptr_t p){ return *reinterpret_cast<T*>(p); }` (in injected DLL) | Keep SEH/guard pages handling; direct deref can AV. |
| Pointer-chain walking | `FollowPointers(base, offsets, readFinalPtr, derefBase)` (`MemoryService.cs:176-191`) | `Follow(base,{...},read_final,deref_base)` utility | Preserve semantics: TarnishedTool has optional base deref and optional final deref. |
| WorldChrMan roster walk | read `worldChrMan`, then begin/end pointers and 8-byte entries (`ChrInsService.cs:25-33`) | `auto begin=*(uintptr_t*)(wcm+0x1F1B8); ...` | Need patch-aware offsets table. |
| Locked target capture hook | Save target ptr in code cave via detour (`TargetService.cs:20-35`, `Resources.resx:807-810`) | In-process detour/trampoline storing pointer | In injected DLL, avoid remote `CreateRemoteThread` assembly execution scaffolding. |
| Attack event hook ring buffer | Hook writes 16 structs of size `0x48`; poll+ack in C# (`AttackInfoService.cs:16-18,55-86`) | Shared ring buffer struct in our module; producer hook + consumer tick | This yields damage events, not current attack param. |
| Param row resolve | `SoloParamRepositoryImp` chain + binary search descriptors (`ParamService.cs:194-219`, `13-37`) | C++ function returning `uint8_t* row` via same chain | Endianness/packing fine on x64; watch signed/unsigned row offset reads. |
| Enemy act byte | `LastAct = *(aiThink + 0xE9C2)` modern patches (`Offsets.cs:338-344`; `TargetService.cs:74-75`) | `uint8_t last_act = Read<uint8_t>(aiThink + off.last_act);` | Not AtkParamId; useful as side telemetry only. |
| Current animation ID | `*(ChrTimeActPtr + 0xD0)` (`ChrInsService.cs:221-223`) | `int anim = Read<int>(timeAct + 0xD0);` | Also not AtkParamId; many attacks share anim-level patterns. |

## Per-version offset tables

TarnishedTool has a large hardcoded per-version table in `Memory/Offsets.cs` keyed by file version parsing (`Offsets.cs:17-45`).

### ER 1.16 / 1.16.1 mapping
- `Version2_6_0` comment = `1.16` (`Enums/GameVersion.cs:30`)
- `Version2_6_1` comment = `1.16.1` (`Enums/GameVersion.cs:31`)
- File-version detection accepts `"2.6.0."` and `"2.6.1."` (`Offsets.cs:42-43`)

### Relevant 1.16.1 constants for our work
- `WorldChrMan.Base = moduleBase + 0x3D65F88` (`Offsets.cs:948-950`)
- `SoloParamRepositoryImp.Base = moduleBase + 0x3D81EE8` (`Offsets.cs:1394-1396`)
- `Hooks.LockedTargetPtr = moduleBase + 0x717372` (`Offsets.cs:2151-2171`)
- `Hooks.AttackInfo = moduleBase + 0x47E22B` (`Offsets.cs:2244-2262`)
- `Functions.ChrInsByHandle = moduleBase + 0x507C70` (`Offsets.cs:1703-1722`)
- `Functions.GetChrInsByEntityId = moduleBase + 0x507E00` (`Offsets.cs:1820-1839`)

### Relevant modern-branch (1.16+) struct offsets used by readers
- `WorldChrMan.ChrInsByUpdatePrioBegin = 0x1F1B8` (`Offsets.cs:86-93`)
- `WorldChrMan.ChrInsByUpdatePrioEnd = 0x1F1C0` (`Offsets.cs:95-102`)
- `ChrIns.ChrManipulator = 0x580` (`Offsets.cs:193-199`)
- `ChrIns.AiThinkOffsets.LastAct = 0xE9C2` (`Offsets.cs:338-344`)
- `ChrIns.AiThinkOffsets.ForceAct = 0xE9C1` (`Offsets.cs:330-336`)

## Critical findings for our v1 build

1. **Does TarnishedTool definitively show how to read `currentAtkParamId` from a `ChrIns` at runtime?**
- **No.** No C# service/viewmodel path reads `currentAtkParamId`; the attack logger is a hooked damage-event logger for locked target, and “EnemyId” comes from `ChrIns + 0x60` (NpcParamId), not attack param (`Resources.resx:1005-1006`, `Offsets.cs:141`).

2. **What’s the exact pointer chain?**
- For **ChrIns enumeration**: `worldChrMan = *WorldChrMan.Base` -> `begin = *(worldChrMan + 0x1F1B8)` -> `end = *(worldChrMan + 0x1F1C0)` -> entry pointers every `0x8` (`ChrInsService.cs:25-33`, `Offsets.cs:86-102`).
- For **current animation**: `timeAct = *(*(chrIns + 0x190) + 0x18)` then `anim = *(timeAct + 0xD0)` (`Offsets.cs:205,238-241`; `ChrInsService.cs:221-223`).
- For **last act**: `aiThink = *(*(chrIns + 0x580) + 0xC0)` then `lastAct = *(aiThink + 0xE9C2)` (`Offsets.cs:193-199,295,338-344`; `TargetService.cs:74-75`).
- For **attack info event logger** (not currentAtk): hook callback checks locked-target compare at `[rsi+0x1e0]`, then logs damage fields + `EnemyId = *(lockedTarget + 0x60)` (`Resources.resx:964-967,978-1006`).

3. **What changes for our build (injected DLL) vs TarnishedTool (external reader)?**
- Remove `OpenProcess/ReadProcessMemory/WriteProcessMemory` dependency; use direct in-process reads/writes with crash guards.
- Replace remote-thread shellcode runner (`CreateRemoteThread`) with direct function calls or in-process trampolines.
- Keep offset tables, pointer chains, and ring-buffer design patterns.
- Keep detour semantics (jump patch + original-byte restore), but implementation can be tighter with in-process hook libs.

4. **Gotchas / fragility points**
- AttackInfo logs only when **locked target** matches internal event context (`Resources.resx:964-967`).
- `TargetTick` early-outs if target invalid, so UI polling stops when lock/target state breaks (`TargetViewModel.cs:1052-1056`, `1178-1199`).
- Poll interval is 64ms (`GameTickService.cs:20-21`), so UI updates are not per-frame.
- AttackInfo gives damage decomposition, not attack-param identity; cannot directly drive `isDisableParry` filter.
- There is no hardcoded Crucible-specific parry logic in code paths; “Crucible” appears in resource data/labels, not runtime attack-selection logic.

5. **Updated Gate 0 confidence**
- **Pre-TarnishedTool**: 62% (from prior synthesis).
- **Post-TarnishedTool**: **54%** for “find direct runtime `currentAtkParamId` quickly”.

Rationale: TarnishedTool contributes strong modern offsets/hook infrastructure and confirms useful proxies (`LastAct`, animation IDs), but it does **not** provide the missing direct `ChrIns -> currentAtkParamId` bridge.

## Sources

All permalinks use commit `887a0d4b2ba4b48fd42c235cebab04e068368353`.

- Local: `README.md:6,50`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/README.md#L6>
- Local: `LICENSE:1-21`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/LICENSE#L1>
- Local: `TarnishedTool/TarnishedTool.csproj:13,18`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/TarnishedTool.csproj#L13>
- Local: `TarnishedTool/Enums/GameVersion.cs:7-31`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Enums/GameVersion.cs#L7>
- Local: `TarnishedTool/MainWindow.xaml.cs:204-209`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/MainWindow.xaml.cs#L204>
- Local: `TarnishedTool/Utilities/PatchManager.cs:15-21`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Utilities/PatchManager.cs#L15>
- Local: `TarnishedTool/Memory/Offsets.cs:15-45,86-102,139-141,193-199,295,330-344,933-951,1379-1397,2151-2171,2244-2263`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/Offsets.cs#L15>
- Local: `TarnishedTool/Memory/AobScanner.cs:35,130-131`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/AobScanner.cs#L35>
- Local: `TarnishedTool/Memory/Pattern.cs:18-28,416-421`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/Pattern.cs#L18>
- Local: `TarnishedTool/Memory/Kernel32.cs:24-52`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/Kernel32.cs#L24>
- Local: `TarnishedTool/Services/MemoryService.cs:64-69,130-133,154-174,176-191,193-222,253-260`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/MemoryService.cs#L64>
- Local: `TarnishedTool/Memory/HookManager.cs:26-62`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/HookManager.cs#L26>
- Local: `TarnishedTool/Services/ChrInsService.cs:23-45,221-223,230-238,295-312`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/ChrInsService.cs#L23>
- Local: `TarnishedTool/Services/TargetService.cs:20-35,74-75`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/TargetService.cs#L20>
- Local: `TarnishedTool/ViewModels/TargetViewModel.cs:774-785,1050-1056,1118-1124,1178-1199`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/ViewModels/TargetViewModel.cs#L774>
- Local: `TarnishedTool/Services/AttackInfoService.cs:16-18,19-43,51-90`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/AttackInfoService.cs#L16>
- Local: `TarnishedTool/Properties/Resources.resx:960-1017`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Properties/Resources.resx#L960>
- Local: `TarnishedTool/Properties/Resources.resx:6016,7688,54357-54358,129785`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Properties/Resources.resx#L54357>
- Local: `TarnishedTool/Memory/CodeCaveOffsets.cs:39-43`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/CodeCaveOffsets.cs#L39>
- Local: `TarnishedTool/Services/ParamService.cs:13-37,194-219`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/ParamService.cs#L13>
- Local: `TarnishedTool/Services/ParamRepository.cs:23,25`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/ParamRepository.cs#L23>
- Local: `TarnishedTool/Services/GameTickService.cs:20-21`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/GameTickService.cs#L20>
- Local: `TarnishedTool/Views/Tabs/TargetTab.xaml:589-617,637-639`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Views/Tabs/TargetTab.xaml#L589>
- Local: `TarnishedTool/Views/Windows/AiWindow.xaml:83-94`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Views/Windows/AiWindow.xaml#L83>
- Local: `TarnishedTool/Views/Windows/ResistancesWindow.xaml:112-127`  
  GitHub: <https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Views/Windows/ResistancesWindow.xaml#L112>
