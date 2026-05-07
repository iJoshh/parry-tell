# Targeting and Boss-Bar Identification

## Part 1: Enemy current-target field

### Findings
- **TarnishedTool** — `/TarnishedTool/Memory/Offsets.cs:392` defines `SpEffectObserveEntry.Target = 0x18` under `AiThinkOffsets`, and `/TarnishedTool/Services/AiService.cs:98-110` reads it from `aiThink + SpEffectObserveComp` linked-list entries.
  - Field name: `SpEffectObserveEntry.Target`
  - Offset on AI struct: `aiThink + SpEffectObserveComp` list node `+0x18`
  - Confidence: **medium** (it is definitely an AI-side target enum/ID usage, but not explicitly documented as "current attack target entity handle")
  - Known-good patch: TarnishedTool table supports through **ER 1.16.1** (`Version2_6_1` path in repo)
- **TarnishedTool** — `/TarnishedTool/Memory/Offsets.cs:304` has `AiThinkOffsets.TargetingSystem = 0xC480`; `/TarnishedTool/Memory/Offsets.cs:410-418` has `TargetingSystemOffsets.DebugDrawFlags = 0xC8` and comments `Draw current target`.
  - Field name: `TargetingSystem` + debug flags
  - Offset on AI struct: `aiThink + 0xC480` (debug draw flags at `+0xC8` within targeting-system sub-struct)
  - Confidence: **low** for gameplay target identity (clearly debug-view related, not proven to hold current target entity)
  - Known-good patch: through **ER 1.16.1** in TarnishedTool version map
- **Practice Tool** — `/practice-tool/xtask/src/codegen/aob_scans.rs:246` defines `CurrentTarget` AOB; `/lib/libeldenring/src/pointers.rs:518` exposes `current_target` pointer chain; `/practice-tool/src/widgets/target.rs:175-177` hook stores `rax` (targeted entity ptr source) and is wired via `/practice-tool/src/config.rs:307-310` `CfgCommand::Target`.
  - Field name: `CurrentTarget`
  - Offset on AI struct: **not an AI-struct offset**; global/game target pointer hook site
  - Confidence: **low** for your need (this is player-target direction, not enemy->who-they-target)
  - Known-good patch: project includes addresses through `V2_06_1` (ER 1.16.1)
- **Not found in scanned sources** (`tarnishedtool`, `erd-tools`, `erd-tools-cpp`, `posturebarmod`, `practice-tool`, `tga-cheat-table`, Hexinton CT): a clearly named/used field explicitly equivalent to **"enemy AI current target entity handle"** on `AiThink` or a nearby sub-struct.

### Best candidate offset for ER 1.16.1
- Try first: **`SpEffectObserveEntry.Target` at list entry `+0x18`**, reached from `aiThink + SpEffectObserveComp`.
- Reasoning: it is the only enemy-AI-side target-labeled field actively read from `AiThink` in TarnishedTool; unlike `CurrentTarget` in Practice Tool, it is not player lock-on plumbing.
- Confidence: **medium-low**.

### If not found
Fallback is still valid: probe `aiThink + 0xE000..0xF000` while forcing visible target swaps, logging 4-byte and 8-byte candidates and diffing against ground-truth moments. One strong probe session should isolate a stable candidate.

## Part 2: Boss health bar / current-boss-entity identification

### Findings
- **Erd-Tools-CPP** — `/Erd-Tools-CPP/Include/ErdTools_globals.h:223-255` defines `CSFeManImp` with `bossHpBars[3]`, each `BossHpBar` containing `bossHandle` at offset `0x8` in the slot struct.
  - Field/flag/event: `CSFeManImp::bossHpBars[i].bossHandle`
  - How used: represents active boss-bar entity handles currently on FE/UI side
  - Confidence: **high**
- **Erd-Tools-CPP** — `/Erd-Tools-CPP/Hook/FeHook.cpp:90-96` iterates `bossHpBars`, checks `bossHandle != UINT64_MAX`, resolves to `ChrIns` via `GetChrInsFromHandle`.
  - Field/flag/event: `bossHpBars[i].bossHandle`
  - How used: runtime lookup of live boss entities tied to displayed boss bars
  - Confidence: **high**
- **Erd-Tools-CPP** — `/Erd-Tools-CPP/Hook/ErdHook.cpp:87-105` resolves boss-bar functions/pointers (`_getBossBarPtr`, `_enableBossBarAddr`, `_disableBossBarAddr`, `_applyBossBarDmg`, `CSFeMan`).
  - Field/flag/event: FE manager boss-bar plumbing
  - How used: confirms this is the game subsystem controlling displayed boss bars
  - Confidence: **high**
- **TarnishedTool Boss Revives are static-data + EventFlag workflows, not active boss-bar detection**:
  - `/TarnishedTool/ViewModels/EnemyViewModel.cs:577-653` picks "closest boss" by block + map distance from prebuilt list
  - `/TarnishedTool/ViewModels/EnemyViewModel.cs:696-733` sets/reads configured boss event flags for revive/status
  - `/TarnishedTool/Utilities/DataLoader.cs:337-367,424-441` loads `BossRevives` CSV with `BossFlags`/`FirstEncounterFlags`
  - `/TarnishedTool/Models/BossRevive.cs:7-20` is a static model, not live boss-bar entity resolver
  - Confidence: **high** (for what it is: revive/status tooling)

### Recommended approach for our mod
1. **Read FE global current boss bar entities (`CSFeManImp->bossHpBars[i].bossHandle`)**.
2. Read EventFlag "in boss fight" then enumerate ChrIns for boss-like entities.
3. Infer via fog wall + aggressive nearby enemy.

Best strategy: **#1**. It is directly tied to what the UI is actually drawing and already validated in `erd-tools-cpp` by resolving `bossHandle -> ChrIns`.

## Sources
- TarnishedTool commit: `887a0d4b2ba4b48fd42c235cebab04e068368353` (`https://github.com/borgCode/TarnishedTool`)
- Erd-Tools-CPP commit: `6d42204a88a25f8d73ed922d3280c6c075d639e5` (`https://github.com/Nordgaren/Erd-Tools-CPP`)
- Practice Tool commit: `db9fbb1dd67ce92c8463b7e282c2687c16508ae1` (`https://github.com/veeenu/eldenring-practice-tool`)

Local citations used:
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:295-305`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:388-394`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Memory/Offsets.cs:410-418`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Services/AiService.cs:98-110`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/ViewModels/EnemyViewModel.cs:577-653`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/ViewModels/EnemyViewModel.cs:696-733`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Utilities/DataLoader.cs:337-367`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Utilities/DataLoader.cs:424-441`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/tarnishedtool/TarnishedTool/Models/BossRevive.cs:7-20`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/xtask/src/codegen/aob_scans.rs:246`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/lib/libeldenring/src/pointers.rs:518`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/practice-tool/src/config.rs:307-310`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/practice-tool/practice-tool/src/widgets/target.rs:175-177`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Include/ErdTools_globals.h:223-255`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/FeHook.cpp:90-96`
- `/home/joshua.blattner/claude/elden-ring/.archaeology-sources/erd-tools-cpp/Erd-Tools-CPP/Hook/ErdHook.cpp:87-105`

GitHub permalinks:
- https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/Offsets.cs#L295-L305
- https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Memory/Offsets.cs#L388-L394
- https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Services/AiService.cs#L98-L110
- https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/ViewModels/EnemyViewModel.cs#L577-L653
- https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/ViewModels/EnemyViewModel.cs#L696-L733
- https://github.com/borgCode/TarnishedTool/blob/887a0d4b2ba4b48fd42c235cebab04e068368353/TarnishedTool/Utilities/DataLoader.cs#L337-L367
- https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/xtask/src/codegen/aob_scans.rs#L246
- https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/pointers.rs#L518
- https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/target.rs#L175-L177
- https://github.com/Nordgaren/Erd-Tools-CPP/blob/6d42204a88a25f8d73ed922d3280c6c075d639e5/Erd-Tools-CPP/Include/ErdTools_globals.h#L223-L255
- https://github.com/Nordgaren/Erd-Tools-CPP/blob/6d42204a88a25f8d73ed922d3280c6c075d639e5/Erd-Tools-CPP/Hook/FeHook.cpp#L90-L96
- https://github.com/Nordgaren/Erd-Tools-CPP/blob/6d42204a88a25f8d73ed922d3280c6c075d639e5/Erd-Tools-CPP/Hook/ErdHook.cpp#L87-L105
