# Gate 0 Attack Plan — `ChrIns -> currentAtkParamId`

## Scope
This document synthesizes six inputs:
- `archaeology/01-posturebarmod-borrow-map.md`
- `archaeology/02-liber-api-surface.md`
- `archaeology/03-practice-tool-techniques.md`
- `archaeology/04-tga-cheat-table-techniques.md`
- `PHASE1-PLAN.md`
- `research/005-claude-parry-indicator-build-research.md`

Citation keys used below:
- `[01]` `archaeology/01-posturebarmod-borrow-map.md`
- `[02]` `archaeology/02-liber-api-surface.md`
- `[03]` `archaeology/03-practice-tool-techniques.md`
- `[04]` `archaeology/04-tga-cheat-table-techniques.md`
- `[P1]` `PHASE1-PLAN.md`
- `[R5]` `research/005-claude-parry-indicator-build-research.md`

## 1) Most likely shape of the solution

### What is already proven
- We can reach `currentAnimation` from `ChrIns -> ChrModuleBag(+0x190) -> animModule slot(+0x18) -> +0x20` using the PostureBarMod-shaped layout. `[01]`
- Practice Tool independently confirms the same animation-state neighborhood shape (`... -> +0x190 -> +0x18 -> +0x20/+0x24/+0x2C`) while keeping per-version root offsets tabled. `[03]`
- TGA independently confirms animation chains rooted from `WorldChrMan + 0x10EF8` and `... -> +0x190 -> +0x18 -> +0x20`. `[04]`
- libER gives typed param reads (`AtkParam_Npc[id]`, `isDisableParry`) but no typed runtime bridge from live `ChrIns` to current attack/behavior param id. `[02]`

### Hypothesis space
- **(a) Same-module field:** `currentAtkParamId` is another 32-bit field in/near the anim module slot currently used for `currentAnimation`. `[01][03][04]`
- **(b) Behavior-module field (most likely):** `currentAtkParamId` is maintained by behavior state and reachable either through a different `ChrModuleBag` slot or a manager object referenced from one of those slots. `[01][02][03][04]`
- **(c) Derived mapping:** no stable runtime field exists; compute `AtkParamId` by mapping animation/behavior activity to param rows. `[03][04][P1]`

### Likelihood ranking (current best read)
1. **(b) Behavior-module field — highest probability (~55%)**
   - libER symbol inventory includes `GLOBAL_CSBehavior`/`GLOBAL_AnimThreadMan`, but no typed wrapper, which strongly suggests the attack/behavior truth lives outside currently typed anim fields. `[02]`
   - PostureBarMod exposes many unknown `ChrModuleBag` slots and undefined padding, i.e., room for behavior-related module pointers not yet labeled. `[01]`
2. **(a) Same anim-module neighborhood — medium probability (~30%)**
   - Three independent artifacts converge on anim state from the same module slot, so colocated attack id is plausible if we have only partially widened that struct. `[01][03][04]`
3. **(c) Derived mapping only — non-trivial fallback probability (~15%)**
   - Practice Tool and TGA both expose stable animation IDs; if no direct runtime attack id is discoverable, animation->attack lookup is still feasible. `[03][04]`

**Working Gate 0 hypothesis (explicit):**
> For an enemy `ChrIns`, a stable 32-bit `currentAtkParamId` is readable at runtime either directly from a `ChrModuleBag` module pointer graph (preferred) or an attached behavior object reachable from that graph; this id should resolve in `AtkParam_Npc` and toggle `isDisableParry` coherently with observed parryable vs non-parryable attacks. `[01][02][03][04]`

**Falsification condition (explicit):**
> If exhaustive module-pointer and first-0x100-byte snapshots across attack transitions do not yield a stable 32-bit value that consistently maps to valid `AtkParam_Npc` rows and tracks known parryability changes, the direct-runtime-id hypothesis is false for this pass. `[02][03][04][P1]`

## 2) Gate 0 concrete probe sequence (Saturday build script)

### Runtime resolution strategy
1. Resolve `WorldChrMan` via **FD4Singleton Finder pattern** (TGA technique), not a fresh AOB scan. `[04]`
2. Keep root offsets in a **version table** (Practice Tool style), even if we start with one version entry. `[03]`
3. Use PostureBarMod-like DLL scaffold and thread lifecycle for injection stability. `[01]`
4. Use libER typed param read path only after candidate id extraction (`AtkParam_Npc[id].isDisableParry`). `[02]`

### Probe steps
1. On attach, wait for game-ready, then resolve `WorldChrMan`. `[01][04]`
2. Read NPC list via `WorldChrMan + 0x10EF8` chain baseline and/or equivalent live roster path already validated in our artifacts. `[01][04]`
3. For each NPC `ChrIns`:
   - Read `ChrIns + 0x190` (`ChrModuleBag*`). `[01]`
   - Read and log all module pointers at offsets:
     `0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50`. `[01]`
4. For each non-null module pointer:
   - Read first `0x100` bytes.
   - Log as hex + decoded DWORD table (offset, u32, i32, float). 
5. Sampling cadence:
   - 30 Hz during combat window (or 20 Hz minimum).
   - 1 Hz out of combat.
6. Test scenario:
   - Fight a **Crucible Knight** for a full capture run.
   - Mark timestamps for at least one known parryable tell and one known un-parryable tell.
7. Analysis pass:
   - Find fields that change at attack start.
   - Filter candidates to 32-bit integers that:
     - are stable across active frames of one attack,
     - change at attack transition boundaries,
     - map to valid `AtkParam_Npc` rows,
     - correlate with `isDisableParry` flips as expected.
8. Promote best candidate to `currentAtkParamId` and run a second validation fight.

### Acceptance criteria
- **Pass Gate 0:** one stable field behaves as `currentAtkParamId` and supports coherent `isDisableParry` filtering in live combat. `[02][P1]`
- **Fail Gate 0:** no stable candidate survives validation; execute fallback policy below. `[P1]`

## 3) Negative outcome policy (if Gate 0 fails)

### Option A (ship path): any-windup indicator
- Trigger cue on enemy attack-windup state transitions using `currentAnimation` change logic only. `[01][03][04][P1]`
- Pros: immediately shippable, low RE risk, still useful training signal. `[P1]`
- Cons: no true parryability filter.

### Option B (clever fallback): offline animation->AtkParam mapping
- Build static lookup table from regulation data + captured runtime traces:
  `animation_id (+context) -> likely AtkParam_Npc row id`. `[03][04][R5]`
- Use runtime animation IDs (already cleanly readable) to infer attack row when direct field is missing. `[03][04]`
- Pros: can recover near-Option-B behavior without discovering hidden runtime field.
- Cons: mapping drift/ambiguity for shared animations and edge cases.

## 4) Pre-build C++ skeleton (starting point for Step 1)

### Proposed file structure
```text
src/
  dllmain.cpp
  main.cpp
  probe.hpp
  probe.cpp
  offsets.hpp
  log.hpp
```

### `src/offsets.hpp`
```cpp
#pragma once
#include <cstdint>

enum class ErVersion : uint32_t {
    V1_16_X,
};

struct OffsetTable {
    uint32_t world_chr_man_singleton = 0; // filled by singleton finder path
    uint32_t world_chr_player_array = 0x10EF8;
    uint32_t chrins_module_bag = 0x190;
};

inline const OffsetTable& OffsetsFor(ErVersion) {
    static const OffsetTable kV116{};
    return kV116;
}
```

### `src/dllmain.cpp`
```cpp
#include <windows.h>
#include <atomic>

extern DWORD WINAPI MainThread(void* module);

static std::atomic<bool> g_running{true};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE th = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        if (th) CloseHandle(th);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running.store(false, std::memory_order_relaxed);
    }
    return TRUE;
}

bool IsRunning() {
    return g_running.load(std::memory_order_relaxed);
}
```

### `src/probe.hpp`
```cpp
#pragma once
#include <array>
#include <cstdint>
#include <vector>

struct ModuleDump {
    uint64_t module_ptr = 0;
    std::array<uint8_t, 0x100> bytes{};
};

struct NpcProbeRow {
    uint64_t chrins = 0;
    uint64_t module_bag = 0;
    std::array<uint64_t, 11> module_slots{}; // 0x00..0x50 step 0x08
    std::vector<ModuleDump> dumps;
};

bool ResolveWorldChrMan(uint64_t& out_world_chr_man);
bool EnumerateNpcChrIns(uint64_t world_chr_man, std::vector<uint64_t>& out_chrins);
bool ProbeNpc(uint64_t chrins, NpcProbeRow& out_row);
void RunProbeLoop();
```

### `src/probe.cpp`
```cpp
#include "probe.hpp"
#include "offsets.hpp"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstring>

extern bool IsRunning();

static bool ReadMem(uint64_t addr, void* out, size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<void*>(addr), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void LogRow(const NpcProbeRow& row) {
    std::FILE* f = nullptr;
    fopen_s(&f, "parry-tell-probe.log", "a");
    if (!f) return;

    std::fprintf(f, "chrins=%p module_bag=%p\n", (void*)row.chrins, (void*)row.module_bag);
    for (size_t i = 0; i < row.module_slots.size(); ++i) {
        std::fprintf(f, "  slot_%02zu(+0x%02zX)=%p\n", i, i * 0x08, (void*)row.module_slots[i]);
    }

    for (const auto& d : row.dumps) {
        std::fprintf(f, "  dump module=%p\n", (void*)d.module_ptr);
        for (size_t i = 0; i < d.bytes.size(); i += 16) {
            std::fprintf(f, "    %03zX :", i);
            for (size_t j = 0; j < 16; ++j) std::fprintf(f, " %02X", d.bytes[i + j]);
            std::fprintf(f, "\n");
        }
    }

    std::fclose(f);
}

bool ResolveWorldChrMan(uint64_t& out_world_chr_man) {
    // Step 1 implementation target:
    // call FD4Singleton finder workflow and retrieve WorldChrMan symbol/value.
    out_world_chr_man = 0;
    return false;
}

bool EnumerateNpcChrIns(uint64_t world_chr_man, std::vector<uint64_t>& out_chrins) {
    out_chrins.clear();
    (void)world_chr_man;
    // Step 1 implementation target:
    // read WorldChrMan + 0x10EF8 chain baseline and collect NPC ChrIns pointers.
    return false;
}

bool ProbeNpc(uint64_t chrins, NpcProbeRow& out_row) {
    out_row = {};
    out_row.chrins = chrins;

    const auto& off = OffsetsFor(ErVersion::V1_16_X);
    if (!ReadMem(chrins + off.chrins_module_bag, &out_row.module_bag, sizeof(out_row.module_bag))) return false;
    if (!out_row.module_bag) return false;

    for (size_t i = 0; i < out_row.module_slots.size(); ++i) {
        uint64_t p = 0;
        ReadMem(out_row.module_bag + (i * 0x08), &p, sizeof(p));
        out_row.module_slots[i] = p;
        if (!p) continue;

        ModuleDump dump{};
        dump.module_ptr = p;
        ReadMem(p, dump.bytes.data(), dump.bytes.size());
        out_row.dumps.push_back(dump);
    }
    return true;
}

void RunProbeLoop() {
    while (IsRunning()) {
        uint64_t world_chr_man = 0;
        if (!ResolveWorldChrMan(world_chr_man)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        std::vector<uint64_t> npcs;
        if (EnumerateNpcChrIns(world_chr_man, npcs)) {
            for (uint64_t chrins : npcs) {
                NpcProbeRow row;
                if (ProbeNpc(chrins, row)) LogRow(row);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}
```

### `src/main.cpp`
```cpp
#include <windows.h>
#include "probe.hpp"

DWORD WINAPI MainThread(void* module) {
    (void)module;

    // Startup settle time like PostureBarMod worker-thread pattern.
    Sleep(3000);

    // Gate 0 mode: probe-only, no rendering/audio.
    RunProbeLoop();
    return 0;
}
```

## Saturday execution checklist
1. Build DLL with this skeleton.
2. Implement `ResolveWorldChrMan()` using FD4Singleton route. `[04]`
3. Implement NPC enumeration through `WorldChrMan + 0x10EF8` baseline chain. `[01][04]`
4. Run Crucible Knight capture for 10+ minutes.
5. Produce candidate-field shortlist + validation table.
6. Decide: Gate 0 pass (Option B path) or fail (Option A/lookup fallback).

## Confidence (<3 days to resolve `currentAtkParamId`)
- **Probability we directly resolve a stable runtime `currentAtkParamId` in <3 days:** **0.62**.
- Rationale: strong cross-source convergence on traversal anchors and tooling pattern, but no source gives the final field directly; the missing bridge is still manual RE. `[01][02][03][04][P1]`
