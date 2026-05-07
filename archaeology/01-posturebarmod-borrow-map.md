# PostureBarMod Borrow Map

## Repo summary
- Repo URL: `https://github.com/Mordrog/EldenRing-PostureBarMod.git` (from local `git -C ... remote -v`).
- Last commit visible in local history: `0a26e52dc97c5ebc590007868e422529605d65ff` / `Version 0.7.0` (from local `git -C ... log --oneline -20`; commit permalink: [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/commit/0a26e52dc97c5ebc590007868e422529605d65ff)).
- License: MIT (`LICENSE:1-21`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/LICENSE#L1)).
- Build target: Windows DLL (`ConfigurationType=DynamicLibrary`) for Win32 and x64 (`PostureBarMod.vcxproj:87-125`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/PostureBarMod.vcxproj#L87)).

## Build / project structure
- Toolchain/SDK in project file:
- `PlatformToolset` is `v143` (`PostureBarMod.vcxproj:89,95,102,109,115,122`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/PostureBarMod.vcxproj#L89)).
- `WindowsTargetPlatformVersion` is `10.0` (unsuffixed) (`PostureBarMod.vcxproj:83`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/PostureBarMod.vcxproj#L83)).
- Dependencies are vendored source, not package-managed:
- ImGui sources are compiled directly (`Source\ImGui\imgui*.cpp`) (`PostureBarMod.vcxproj:32-38`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/PostureBarMod.vcxproj#L32)).
- MinHook C sources are compiled directly (`Source\Minhook\*.c`) (`PostureBarMod.vcxproj:44-48`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/PostureBarMod.vcxproj#L44)).
- MinHook version: not pinned in `.vcxproj`; vendored header shows copyright span `2009-2017` only (`Source/Minhook/MinHook.h:2-4`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Minhook/MinHook.h#L2)).
- ImGui version: `IMGUI_VERSION "1.83"` / `IMGUI_VERSION_NUM 18300` (`Source/ImGui/imgui.h:65-66`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/ImGui/imgui.h#L65)).

## File-by-file map

### `README.md`
- Purpose: user-facing install/compat/changelog doc.
- Key items:
- “display-only, no game data changes” stance (`README.md:9-11`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/README.md#L9)).
- Last explicit AOB refresh note is patch `1.10.1` in Beta `0.5.0` (`README.md:61-63`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/README.md#L61)).
- Classification: `REFERENCE ONLY`.

### `Source/Main/Hooking.cpp`
- Purpose: locate runtime signatures and install MinHook + renderer hooks.
- Key functions/classes:
- `Hooking::Hooking()` initializes MinHook (`Source/Main/Hooking.cpp:9-12`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L9)).
- `Hooking::Hook()` performs AOB scans + installs hooks (`Source/Main/Hooking.cpp:19-65`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L19)).
- `Hooking::Unhook()` tears down D3D hooks and removes MinHook hooks (`Source/Main/Hooking.cpp:83-86`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L83)).
- Classification:
- `Hooking::Hooking`, `Hooking::Unhook`: `LIFT VERBATIM`.
- `Hooking::Hook` and all signatures: `LIFT AND ADAPT`.

### `Source/Main/Hooking.hpp`
- Purpose: game memory structs + function-pointer typedefs for hook layer.
- Key functions/classes:
- `GameData::Module0x18` with `currentAnimation` at `+0x20` (`Source/Main/Hooking.hpp:63-67`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L63)).
- `GameData::StaggerModule` with `stagger`, `staggerMax`, `resetTimer` (`Source/Main/Hooking.hpp:106-116`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L106)).
- `GameData::ChrModuleBag` module pointers (`Source/Main/Hooking.hpp:128-138`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L128)).
- `GameData::ChrIns` including `chrType`, `teamType`, `chrModulelBag` (`Source/Main/Hooking.hpp:140-153`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L140)).
- `GameData::WorldChrMan` + `playerArray` offset layout (`Source/Main/Hooking.hpp:155-158`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L155)).
- Classification: `LIFT AND ADAPT`.

### `Source/Main/PostureBarUI.cpp`
- Purpose: per-update entity enumeration and readout of posture/stamina/status values + bar draw logic.
- Key functions/classes:
- `PostureBarUI::Draw()` transforms game/UI coordinates to viewport and renders via ImGui background draw list (`Source/Main/PostureBarUI.cpp:32-271`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L32)).
- `PostureBarUI::isMenuOpen()` checks `isLoading` + menu-state signatures (`Source/Main/PostureBarUI.cpp:417-426`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L417)).
- `PostureBarUI::updateUIBarStructs()` is the load-bearing read loop (`Source/Main/PostureBarUI.cpp:428-659`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L428)).
- Classification:
- `updateUIBarStructs` pointer-walk/read loop: `LIFT AND ADAPT`.
- rendering/bar style methods: `REFERENCE ONLY`.

### `Source/Main/PostureBarUI.hpp`
- Purpose: posture-bar data models/config + `PostureBarUI` interface.
- Key functions/classes:
- `BarData` helpers for value/max/inverse fill (`Source/Main/PostureBarUI.hpp:54-65`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.hpp#L54)).
- `PostureBarUI` interface incl. hooked callback declaration (`Source/Main/PostureBarUI.hpp:271-305`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.hpp#L271)).
- Classification: `REFERENCE ONLY` (except callback signature style, which is `LIFT AND ADAPT`).

### `Source/Main/D3DRenderer.cpp`
- Purpose: D3D12 vtable hook bootstrap, ImGui initialization/lifecycle, per-frame overlay render.
- Key functions/classes:
- `D3DRenderer::Hook()` installs vtable hooks for `ExecuteCommandLists`, `Present`, `ResizeTarget` (`Source/Main/D3DRenderer.cpp:125-136`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L125)).
- `D3DRenderer::InitHook()` builds temporary DX12 objects and method table (`Source/Main/D3DRenderer.cpp:165-340`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L165)).
- `D3DRenderer::Overlay()` initializes ImGui context/backends and renders draw data each frame (`Source/Main/D3DRenderer.cpp:509-751`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L509)).
- `D3DRenderer::ResetRenderState()` handles teardown (`Source/Main/D3DRenderer.cpp:796-830`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L796)).
- Classification: `LIFT AND ADAPT`.

### `Source/Main/D3DRenderer.hpp`
- Purpose: D3D renderer type declarations and hook API.
- Key functions/classes:
- `D3DRenderer` declares hook callbacks and overlay lifecycle (`Source/Main/D3DRenderer.hpp:28-127`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.hpp#L28)).
- Classification: `LIFT AND ADAPT`.

### `Source/Main/Memory.hpp`
- Purpose: signature scanner primitives (`MemoryHandle`, `Module`, `Signature`).
- Key functions/classes:
- `MemoryHandle::Rip()` implements RIP-relative resolution (`Source/Main/Memory.hpp:89-94`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Memory.hpp#L89)).
- `Signature::Scan()` performs byte-pattern scan across module region (`Source/Main/Memory.hpp:294-317`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Memory.hpp#L294)).
- Classification: `LIFT VERBATIM`.

### `Source/Main/Logger.cpp` and `Source/Main/Logger.hpp`
- Purpose: thread-safe file logger with source-location context.
- Key functions/classes:
- `Logger::log()` lazily opens `dllPath + "PostureModLog.txt"`, writes + flushes (`Source/Main/Logger.cpp:32-50`, `Source/Main/Logger.hpp:18`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Logger.cpp#L32)).
- Classification: `LIFT VERBATIM`.

### `PostureBarMod.vcxproj`
- Purpose: build graph/configs for DLL and vendored dependencies.
- Key functions/classes: N/A (project metadata only).
- Classification: `REFERENCE ONLY`.

## Patterns we're inheriting

### Pattern: ME2 / Seamless DLL injection scaffold
- `DllMain` stores module handle, disables thread callbacks, and starts worker thread via `CreateThread` on process attach; on detach it flips kill flags (`Source/dllmain.cpp:3-13`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/dllmain.cpp#L3)).
- Worker thread (`MainThread`) does: find DLL path -> load INI -> sleep 3s -> create renderer/UI/hooking objects -> `g_Hooking->Hook()` -> loop until `g_Running` false (`Source/PostureBarMod.cpp:427-507`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/PostureBarMod.cpp#L427)).
- Initialization order/race handling: hard-coded `sleep_for(3s)` before hook init is the only startup race mitigation (`Source/PostureBarMod.cpp:435-441`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/PostureBarMod.cpp#L435)).
- Stability assessment: scaffold shape is reusable; raw `CreateThread` + fixed delay are brittle but workable for v1.
- Confidence: **high**.

### Pattern: AOB scanning + hook installation
- AOBs in `Hooking.cpp` (verbatim) and targets:
- `worldChrSignature`: `48 8B 05 ? ? ? ? 48 85 C0 74 0F 48 39 88`; used with `.Add(3).Rip()` to resolve the global `WorldChrMan` pointer used later in `updateUIBarStructs` (`Source/Main/Hooking.cpp:30`, `Source/Main/PostureBarUI.cpp:437`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L30)).
- `CSFeManSignature`: `48 8B 0D ? ? ? ? 8B DA 48 85 C9 75 ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 4C 8B C8 4C 8D 05 ? ? ? ? BA B4 00 00 00 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? 8B D3 E8 ? ? ? ? 48 8B D8`; resolves `CSFeManImp` pointer read in UI updates (`Source/Main/Hooking.cpp:34`, `Source/Main/PostureBarUI.cpp:438`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L34)).
- `isLoading`: `10 75 ? ? ? ? 00 00 20 75 ? ? ? ? 00 00 00 00 00 00 00 00 00 00 E4 01 00 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ? ? ? 00 00 00 00 00 00 00 00 00 00 00 00 00 19 0E 19 B4 00 00 00 00 00`; drives menu/loading suppression (`Source/Main/Hooking.cpp:38`, `Source/Main/PostureBarUI.cpp:422`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L38)).
- `menuState` (same pattern used 3x with offsets +1244/+1248/+1252): `20 9E ? ? ? ? 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 40 CB ? ? ? ? 00 00 18 CB ? ? ? ? 00 00 00 00 00 00 00 00 00 00 97 01 00 80`; also used by `isMenuOpen()` gating (`Source/Main/Hooking.cpp:42-47`, `Source/Main/PostureBarUI.cpp:423`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L42)).
- `GetChrInsFromHandleFunc`: `48 83 EC 28 E8 17 FF FF FF 48 85 C0 74 08 48 8B 00 48 83 C4 28 C3`; resolves handle->`ChrIns*` conversion function used for player/boss/entity loops (`Source/Main/Hooking.cpp:50`, `Source/Main/PostureBarUI.cpp:452,496,567`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L50)).
- `UpdateUIBarStructsFunc`: `40 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 C7 44 24 30 FE FF FF FF 48 89 9C 24 B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 58 48`; this is the actual hook target redirected to `g_postureUI->updateUIBarStructs` (`Source/Main/Hooking.cpp:54,58`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L54)).
- MinHook usage: `MH_Initialize` in constructor, `MH_CreateHook` for UI update hook, then `MH_EnableHook(MH_ALL_HOOKS)` (`Source/Main/Hooking.cpp:11,58,65`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L11)).
- Stale-for-1.16+ assessment: last explicit AOB update documented for patch `1.10.1`; no later changelog entry says AOBs were refreshed (`README.md:61-63`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/README.md#L61)).
- Confidence: **high** on patterns/AOB list, **medium** on exact 1.16 breakage timing.

### Pattern: WorldChrMan -> ChrIns enumeration
- Pointer roots: `worldChar` from `worldChrSignature`, `feMan` from `CSFeManSignature` (`Source/Main/PostureBarUI.cpp:437-438`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L437)).
- Player: takes `(*worldChar->playerArray[0])->handle` then resolves `ChrIns` through `GetChrInsFromHandleFunc` (`Source/Main/PostureBarUI.cpp:452`, `Source/Main/Hooking.hpp:155-158`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L452)).
- NPCs: iterates `feMan->bossHpBars` and `feMan->entityHpBars` handles, then resolves each handle to `ChrIns` via `GetChrInsFromHandleFunc` (`Source/Main/PostureBarUI.cpp:493-499,564-570`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L493)).
- Render-distance/visibility filter behavior: this path is effectively “has HP-bar slot + valid handle + `staggerMax > 0`”; entity visibility also gates draw through `entityHpBars[i].isVisible` (`Source/Main/PostureBarUI.cpp:499,565,570,585`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L565)).
- Player-vs-NPC filter: `chrType`/`teamType` fields exist in `ChrIns` layout but are not used in this loop (`Source/Main/Hooking.hpp:147-149`, `Source/Main/PostureBarUI.cpp:493-605`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L147)).
- Confidence: **high**.

### Pattern: ChrIns -> ChrModuleBag -> StaggerModule (analog for AtkParamId)
- Exact declared chain:
- `ChrIns::chrModulelBag` pointer (`Source/Main/Hooking.hpp:150`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L150)).
- `ChrModuleBag::animModule` slot (`+0x18` from struct order) (`Source/Main/Hooking.hpp:130-133`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L130)).
- `Module0x18::currentAnimation` (`Source/Main/Hooking.hpp:63-67`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L63)).
- `ChrModuleBag::staggerModule` slot (`+0x40` from struct order) (`Source/Main/Hooking.hpp:134-136`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L134)).
- `StaggerModule::stagger` and `staggerMax` fields (`Source/Main/Hooking.hpp:111-114`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L111)).
- Read site uses that exact chain repeatedly (`Source/Main/PostureBarUI.cpp:460-461,514-515,587-588`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L460)).
- Additional module slots visible beyond stagger: `statModule`, `animModule`, `resistanceModule`, `physicsModule` (+ undefined gaps indicating more unknown slots) (`Source/Main/Hooking.hpp:128-138`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L128)).
- AtkParamId-specific note: no `attackParamId` or behavior-id field is declared anywhere in these structs (`Source/Main/Hooking.hpp:63-153`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L63)).
- Confidence: **high**.

### Pattern: D3D12 hooking + ImGui overlay (deferred to v2)
- Present hook setup:
- Installs hooks by vtable index: `54` (`ExecuteCommandLists`), `140` (`Present`), `146` (`ResizeTarget`) (`Source/Main/D3DRenderer.cpp:130-135`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L130)).
- ImGui context lifecycle:
- create/init: `ImGui::CreateContext`, `ImGui_ImplWin32_Init`, `ImGui_ImplDX12_Init`, `ImGui_ImplDX12_CreateDeviceObjects` (`Source/Main/D3DRenderer.cpp:655-677`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L655)).
- per-frame: `ImGui_ImplDX12_NewFrame`, `ImGui_ImplWin32_NewFrame`, `ImGui::NewFrame`, draw, `ImGui::Render`, `ImGui_ImplDX12_RenderDrawData` (`Source/Main/D3DRenderer.cpp:694-723`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L694)).
- teardown: `ImGui_ImplDX12_Shutdown`, `ImGui_ImplWin32_Shutdown`, `ImGui::DestroyContext` (`Source/Main/D3DRenderer.cpp:823-826`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L823)).
- Background draw list usage lives in UI code (`ImGui::GetBackgroundDrawList()->...`) (`Source/Main/PostureBarUI.cpp:83-92,367-377,382-414`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L83)).
- World-to-screen projection function: no 3D matrix projection routine is present in these files; instead it consumes game-provided `screenPosX/screenPosY/mod` from `feMan->entityHpBars` and scales/offsets to viewport (`Source/Main/PostureBarUI.cpp:582-585,217-225`, `Source/Main/Hooking.hpp:21-46`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L582)).
- Confidence: **high** for lifecycle, **high** for “no explicit world->screen math present”.

### Pattern: Logging
- File logging only: `Logger::log()` writes to `dllPath + PostureModLog.txt` and flushes (`Source/Main/Logger.hpp:18`, `Source/Main/Logger.cpp:44-50`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Logger.cpp#L44)).
- Uses mutex for thread safety (`Source/Main/Logger.cpp:42`, `Source/Main/Logger.hpp:32`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Logger.hpp#L32)).
- Includes source location metadata in each line (`Source/Main/Logger.cpp:26-29,48`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Logger.cpp#L26)).
- No `OutputDebugString` usage in project runtime code paths searched.
- Confidence: **high**.

### Pattern: Crash safety
- Uses C++ `try/catch`, but only under `#ifdef DEBUGLOG` in hook/render/read hotpaths (`Source/Main/Hooking.cpp:21-80`, `Source/Main/PostureBarUI.cpp:431-658`, `Source/Main/D3DRenderer.cpp:167-339,511-750`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L431)).
- No SEH (`__try/__except`) found.
- Many raw pointer dereferences in gameplay reads remain unguarded by local null checks (example: `chrIns->chrModulelBag->staggerModule` reads) (`Source/Main/PostureBarUI.cpp:452,499,570`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L452)).
- Confidence: **high**.

## What we are NOT borrowing
- The full bar UI rendering/state system (textures, fills, circles, style palette) (`Source/Main/PostureBarUI.cpp:273-414`, `Source/Main/PostureBarUI.hpp:198-266`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L273)).
- Menu-state heuristic signatures (`isLoading/menuState*`) unless needed for UX gating (`Source/Main/Hooking.cpp:38,42-47`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L38)).
- D3D12 overlay stack for v1 (audio-first build) (`Source/Main/D3DRenderer.cpp:509-751`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/D3DRenderer.cpp#L509)).

## Refresh-required-for-1.16+ inventory

| Item | What it currently is (PostureBarMod for 1.10.1) | What needs to change for ER 1.16+ | Difficulty (1-5) | Source file:line |
|---|---|---|---:|---|
| `worldChrSignature` AOB | Hardcoded signature scan + RIP resolution | Re-scan bytes or replace with symbol-based access | 4 | `Source/Main/Hooking.cpp:30` |
| `CSFeManSignature` AOB | Hardcoded long signature scan | Re-scan bytes for current binary | 4 | `Source/Main/Hooking.cpp:34` |
| `isLoading` AOB | Hardcoded blob + `Add(81)` | Re-scan blob/offset | 3 | `Source/Main/Hooking.cpp:38` |
| `menuState` AOB | Same blob read at +1244/+1248/+1252 | Re-scan blob/offset triplet or drop feature | 3 | `Source/Main/Hooking.cpp:42-47` |
| `GetChrInsFromHandleFunc` AOB | Hardcoded function signature | Re-locate function entry in 1.16+ | 5 | `Source/Main/Hooking.cpp:50` |
| `UpdateUIBarStructsFunc` AOB | Hardcoded target callback signature | Re-locate callback in 1.16+ | 5 | `Source/Main/Hooking.cpp:54` |
| `ChrIns` / `ChrModuleBag` offsets | Struct layout frozen in header | Re-validate offsets against 1.16+ runtime | 5 | `Source/Main/Hooking.hpp:128-153` |
| `Module0x18` internals | Only `currentAnimation` known | Extend to behavior/attack id fields or use other module path | 5 | `Source/Main/Hooking.hpp:63-67` |
| Known version context | Last explicit AOB refresh documented at patch 1.10.1 | Treat all AOB-dependent code as refresh-required | 2 | `README.md:61-63` |

## Specific findings for our v1 build

1. **Can we lift the DllMain + worker-thread scaffold verbatim?**
- **Yes, with minor hygiene tweaks.** The scaffold is straightforward and self-contained (`Source/dllmain.cpp:3-13`, `Source/PostureBarMod.cpp:427-443`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/dllmain.cpp#L3)).
- I would keep behavior but replace fixed `sleep_for(3s)` with an explicit readiness check in our code (`Source/PostureBarMod.cpp:435`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/PostureBarMod.cpp#L435)).

2. **Can we lift the MinHook setup verbatim?**
- **Partially.** MinHook lifecycle calls are liftable (`MH_Initialize`, `MH_CreateHook`, `MH_EnableHook`) (`Source/Main/Hooking.cpp:11,58,65`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L11)).
- The scan signatures and hook targets are patch-sensitive and require adaptation/refresh (`Source/Main/Hooking.cpp:30,34,50,54`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L30)).

3. **Which 1-3 specific AOBs do we MOST need refreshed for 1.16+? (ranked)**
- 1) `UpdateUIBarStructsFunc` hook target (`Source/Main/Hooking.cpp:54`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L54)).
- 2) `GetChrInsFromHandleFunc` resolver (`Source/Main/Hooking.cpp:50`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L50)).
- 3) `worldChrSignature` global pointer resolver (`Source/Main/Hooking.cpp:30`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.cpp#L30)).

4. **What’s the closest PostureBarMod has to “read currentAtkParamId from a ChrIns”?**
- Closest existing analog is: `ChrIns* -> chrModulelBag -> animModule -> currentAnimation` (`Source/Main/Hooking.hpp:63-67,128-133,150`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L63)).
- Existing live reads are from `staggerModule`/`statModule`/`resistanceModule`, not attack-id fields (`Source/Main/PostureBarUI.cpp:514-531,587-604`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/PostureBarUI.cpp#L514)).
- So your spoiler is correct: PostureBarMod gives the **pointer-chain pattern**, but it does **not** include a declared `currentAtkParamId` field in its module structs (`Source/Main/Hooking.hpp:63-153`, [GH](https://github.com/Mordrog/EldenRing-PostureBarMod/blob/0a26e52dc97c5ebc590007868e422529605d65ff/Source/Main/Hooking.hpp#L63)).

## Sources
- Commit used for permalinks: `0a26e52dc97c5ebc590007868e422529605d65ff` (from local `git log`).
- Required local files read:
- `README.md`
- `Source/Main/Hooking.cpp`
- `Source/Main/Hooking.hpp`
- `Source/Main/PostureBarUI.cpp`
- `Source/Main/PostureBarUI.hpp`
- `Source/Main/D3DRenderer.cpp`
- `Source/Main/D3DRenderer.hpp`
- `Source/Main/Memory.hpp`
- `Source/Main/Logger.cpp`
- `Source/Main/Logger.hpp`
- `PostureBarMod.vcxproj`
- Additional local files read for required scaffold section:
- `Source/dllmain.cpp`
- `Source/PostureBarMod.cpp`
- `Source/PostureBarMod.hpp`
- Confidence by pattern:
- ME2/DLL scaffold: **high**
- AOB/hook map: **high**
- WorldChrMan/ChrIns enumeration: **high**
- ChrIns->module walk for stagger/anim: **high**
- D3D12/ImGui overlay lifecycle: **high**
- World-to-screen projection specifics: **high** (that no explicit matrix projector is present in these files)
- Crash-safety characterization: **high**
