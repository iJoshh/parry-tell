# Practice Tool Technique Notes (READ-ONLY, AGPL — no code copying)

## ABSOLUTE RULE
This document is **technique reference only** for `veeenu/eldenring-practice-tool` (AGPL-3.0). We may study architecture, memory-access patterns, and maintenance workflows, but we may **not** copy source text/logic into our MIT-licensed mod. Any implementation we ship must be written from scratch. (LICENSE: `LICENSE:1-15`)

## Repo summary
- Upstream URL: `https://github.com/veeenu/eldenring-practice-tool` (git remote in local clone points there).
- Local HEAD commit: `db9fbb1dd67ce92c8463b7e282c2687c16508ae1` (`Update dependencies (#172)`, 2026-04-23).
- Local tag inventory: **none present in this clone** (`git tag --list` returned empty).
- License: AGPL-3.0 (`LICENSE:1-15`).
- Workspace version is `1.9.4` (`Cargo.toml:13`), and packaged release notes in repo say this release adds ER `1.16.1` support (`lib/data/RELEASE-GH.txt:4`).
- In code, supported game versions are tabled through `Version::V2_06_1` (`lib/libeldenring/src/codegen/base_addresses.rs:94-120`, `149-151`).

## File structure overview
High-level workspace layout includes multiple crates/tools, but the two load-bearing crates for this pass are:
- `practice-tool` (`practice-tool/Cargo.toml:1-17`): DLL/exe entrypoint, dinput8 proxy path, hudhook integration, UI loop, widget orchestration.
- `lib/libeldenring` (`lib/libeldenring/Cargo.toml:1-15`): version detection, versioned base-address tables, pointer-chain definitions, param access layer, memory safety wrappers.

The workspace membership confirms both crates and surrounding tooling (`Cargo.toml:1-10`).

## Technique inventory

### Technique: DLL injection / load path
- Practice Tool exposes and forwards `DirectInput8Create`, loading system `dinput8.dll` and acting as a proxy (`practice-tool/src/lib.rs:48-83`).
- README installed mode explicitly instructs renaming the tool DLL to `dinput8.dll` in the game directory (`README.md:50-52`).
- Lifecycle:
1. `DllMain` on `DLL_PROCESS_ATTACH` checks game version (`practice-tool/src/lib.rs:232-236`).
2. It forces proxy/hook initialization and spawns a worker thread (`practice-tool/src/lib.rs:238-243`).
3. If running as `dinput8.dll`, startup is gated on right-shift hold (`practice-tool/src/lib.rs:246-257`, `198-228`).
4. `PracticeTool::new()` waits for game/menu readiness (`menu_timer > 0`) before normal operation (`practice-tool/src/practice_tool.rs:170-179`).
- Version detection is done via PE file version read and `Version::try_from`; unsupported versions show an error dialog and fail load (`lib/libeldenring/src/version.rs:27-71`, `77-97`).

### Technique: Versioned pointer tables (THE LOAD-BEARING PATTERN)
- Version enum and conversion table: `lib/libeldenring/src/codegen/base_addresses.rs:94-120`, `122-157`, `192-220`.
- Base address struct includes core roots (`world_chr_man`, `current_target`, `base_anim`, `cs_regulation_manager`) and per-version constants (`lib/libeldenring/src/codegen/base_addresses.rs:5-45`, `1190-1272`).
- Runtime pointer chains are composed from those roots in `Pointers::new()` (`lib/libeldenring/src/pointers.rs:211-243`, `321-342`, `394-396`, `518`).

| What's read | Pointer chain (base + offsets) | File:line | Stability claim |
|---|---|---|---|
| `world_chr_man` root | `module_base + BaseAddresses.world_chr_man` | `codegen/base_addresses.rs:47-73`, `1190-1272` | Version-tabled; base offset updated per `Version`. |
| Player `ChrIns` root | `world_chr_man -> +player_ins` (offset branch by version: `0x18468` older, `0x1E508` newer) | `pointers.rs:328-333` | Version-tabled via explicit match arms. |
| Locked-on hook address | `module_base + BaseAddresses.current_target`, then `pointer_chain!(current_target)` | `codegen/base_addresses.rs:79`, `1190-1272`; `pointers.rs:518` | Version-tabled root; hook site moves with patches. |
| Current animation id | `base_anim -> 0x0 -> 0x190 -> 0x18 -> 0x20` | `pointers.rs:394` | Static subchain + versioned `base_anim` root. |
| Current animation time | `base_anim -> 0x0 -> 0x190 -> 0x18 -> 0x24` | `pointers.rs:395` | Same pattern. |
| Current animation length | `base_anim -> 0x0 -> 0x190 -> 0x18 -> 0x2C` | `pointers.rs:396` | Same pattern. |
| Character points example (HP/FP/SP block) | `world_chr_man -> net_players_ins -> 0 -> 0x190 -> 0 -> 0x138` | `pointers.rs:372-380` | Version-tabled via `net_players_ins` branch (`pointers.rs:321-326`). |

Attack/behavior/current-AtkParam runtime chain:
- I found **no runtime pointer chain** in `practice-tool/src` or non-generated `lib/libeldenring/src` that resolves current attack ID/behavior ID/AtkParam ID.
- The only `AtkParam` / `behavior` symbols are generated param struct fields (schema/types), not a live `ChrIns -> current attack` reader (`lib/libeldenring/src/codegen/param_data.rs:837`, `921`, `1026`, `1971`, `2051`).

### Technique: Reading current animation / behavior / attack state
- Practice Tool renders an animation indicator by reading exactly three values: `cur_anim`, `cur_anim_time`, `cur_anim_length` (`practice-tool/src/practice_tool.rs:606-619`).
- Those values come from the fixed animation pointer chains above (`lib/libeldenring/src/pointers.rs:394-396`).
- Search result for runtime code paths:
  - `atk_param`, `AtkParam`, `attack_id`, `behavior` do not appear in non-generated runtime files (`practice-tool/src/*`, `lib/libeldenring/src/*` excluding `codegen`).
  - They do appear in generated param type definitions (`codegen/param_data.rs`) as data fields, not as “current enemy attack” reads (`lib/libeldenring/src/codegen/param_data.rs:837`, `921`, `1026`, `1971`, `2051`).
- Conclusion: prior claim stands; this project does **not** show a direct `currentAtkParamId` reader in the reviewed runtime paths.

### Technique: Reading params (regulation.bin runtime data)
- Param refresh starts from `cs_regulation_manager + module_base`, then follows a short chain (`+0x18`) to a `ParamMaster` root (`lib/libeldenring/src/params.rs:105-113`).
- It waits until memory protection is read/write before accepting pointer validity (`params.rs:114-127`).
- It builds a param-name map from `ParamMaster` entries and then iterates row metadata via per-entry `param_id`/`param_offset` vectors (`params.rs:137-172`, `200-207`, `217-226`).
- This is a **separate runtime param path** from libER’s `SoloParamRepository` API naming; it uses this project’s own `Params` map and generated `ParamVisitor` layer (`lib/libeldenring/src/lib.rs:50-66`, `params.rs:92-99`, `182-193`).

### Technique: Memory safety / crash prevention
- `PointerChain` evaluates via `ReadProcessMemory` at each hop and returns `Option` on failure (`lib/libeldenring/src/memedit.rs:48-71`).
- Data reads/writes also use `ReadProcessMemory` / `WriteProcessMemory` with `Option` result semantics (`memedit.rs:75-106`).
- This avoids raw dereference crashes during transient invalid states and gives caller-level “read failed” handling.
- Transfer lesson for C++ v1: mirror this with guarded reads (RPM-style) and fail-closed polling loops; do not hard-deref unknown chains.

### Technique: HUD overlay (deferred to v2)
- Overlay stack is `hudhook` + DX12 ImGui (`practice-tool/src/lib.rs:29-33`, `187-191`; `practice-tool/src/practice_tool.rs:796-877`).
- Useful for future UI layers, but not needed for Gate 0 / v1 audio-only probing.

### Technique: Locked-on target capture
- `Target` widget receives the per-version `current_target` chain from config (`practice-tool/src/config.rs:307-310`).
- It allocates executable memory and patches a detour at that site (`practice-tool/src/widgets/target.rs:101-117`, `159-187`).
- The detour stores an entity pointer (`entity_addr`) and then reads HP/MP/SP/resistances/poise/position from offsets under `entity + 0x190` (`target.rs:140-157`).
- This is the closest analog for “focus one enemy reliably”: if lock-on exists, you get a stable enemy pointer without full roster iteration logic.

### Technique: Per-version offset maintenance workflow
- Workflow embodied in code:
1. Add/maintain `Version` enum entries and conversion mapping (`codegen/base_addresses.rs:94-120`, `122-157`).
2. Add one `BASE_ADDRESSES_<version>` constant with updated roots (`codegen/base_addresses.rs:1190-1272` for 2.06.x).
3. Update `pointers.rs` match arms where secondary offsets moved (`player_ins`, `net_players_ins`, `torrent_enemy_ins`, etc.; `pointers.rs:321-342`).
4. Keep runtime chains stable while version tables absorb shifts (`pointers.rs:394-396`, `518`).
- Requested `git log --oneline -30` and tags in this local clone are limited (single shallow commit, no tags), so commit-by-commit 1.16.x archaeology is not available from local history.

## Widget inventory (requested)
`widgets/mod.rs` declares these modules: `character_stats`, `cycle_color`, `cycle_speed`, `deathcam`, `flag`, `group`, `item_spawn`, `label`, `multiflag`, `nudge_pos`, `position`, `quitout`, `runes`, `savefile_manager`, `target`, `warp` (`practice-tool/src/widgets/mod.rs:1-16`).

Files most related to character state / animation / combat and read in detail:
- `target.rs` (lock-on entity capture + combat-relevant state reads) (`target.rs:78-157`, `159-193`).
- `character_stats.rs` (HP/FP/SP/stat edits for player state) (`character_stats.rs:7-14`, `31-43`, `56-76`).
- `cycle_speed.rs` (animation speed pointer writes) (`cycle_speed.rs:11-13`, `45-47`; chain roots wired in `config.rs:285-289`).
- `deathcam.rs` (player/torrent flag patching) (`deathcam.rs:7-24`).
- `position.rs` and `nudge_pos.rs` (position/chunk state reads/writes) (`position.rs:44-77`, `117-135`; `nudge_pos.rs:8-20`).

## Architecture diagram (text)
1. `dinput8` proxy (or standalone exe) enters process and runs `DllMain` bootstrap (`practice-tool/src/lib.rs:75-83`, `232-262`).
2. Startup validates game version (`version::check_version`) before continuing (`lib.rs:234-236`; `version.rs:22-71`).
3. Bootstrap starts a thread, optionally waits for user startup gesture (RSHIFT), then initializes hooks/overlay (`lib.rs:242-257`, `198-228`, `179-196`).
4. `Pointers::new()` resolves versioned roots + chain offsets into typed `PointerChain` handles (`pointers.rs:211-243`, `321-342`, `394-396`, `518`).
5. Tool waits for in-game readiness via `menu_timer` and then uses periodic reads in render/update paths (`practice_tool.rs:170-179`, `606-619`).
6. Optional target subsystem detours lock-on and anchors enemy-centric reads to captured entity pointer (`target.rs:159-187`, `140-157`).
7. Param subsystem separately maps regulation tables for typed row access (`params.rs:105-135`, `200-226`).

## What we will write from scratch (not learn from)
- Any Rust-specific implementation structure (traits, crate organization, macro plumbing).
- Any hudhook/ImGui integration details.
- Any literal offset constants or detour byte layouts from AGPL source.
- Any copied control flow from AGPL files.

## Concrete findings for our v1 build

1. **Pointer-chain pattern adoptable?**
- **Yes, the pattern is adoptable at architecture level**: version enum + per-version base table + stable secondary chains is exactly the maintainable strategy we need for ER patches (`codegen/base_addresses.rs:94-120`, `192-220`; `pointers.rs:321-342`, `394-396`).
- **No direct literal reuse** of their tables/bytes due AGPL contamination risk.

2. **Closest Practice Tool gets to “what attack is this enemy doing”**
- It exposes **current animation ID/time/length** (`practice_tool.rs:606-619`; `pointers.rs:394-396`).
- It does **not** expose current attack param ID/behavior ID from live `ChrIns` runtime paths in reviewed non-generated code.

3. **Lessons for Gate 0 (`currentAtkParamId` resolution)**
- Reuse the maintenance model: split version-sensitive roots from stable subchains (`codegen/base_addresses.rs:47-89`, `1190-1272`; `pointers.rs:394-396`).
- Use lock-on capture to get a deterministic enemy pointer first, then RE attack-state chain from that anchor (`config.rs:307-310`; `target.rs:159-187`).
- Keep reads crash-tolerant (RPM-style optional reads) while probing unknown structures (`memedit.rs:48-71`, `75-106`).
- Do not expect this repo to hand us `currentAtkParamId`; that bridge is still custom RE work.

4. **Single most useful technique for v1**
- **Version-tabled pointer architecture** (enum + per-version base-address table + chain composition), because it is the mechanism that keeps live memory reads viable across patches (`codegen/base_addresses.rs:94-120`, `192-220`, `1190-1272`; `pointers.rs:321-342`, `394-396`).

5. **Adopt same version-handling pattern?**
- **Yes** at design level: explicit version gating + per-version tables + fail-fast on unknown versions (`version.rs:22-71`, `77-97`; `codegen/base_addresses.rs:122-157`).
- This is preferable to silent undefined behavior when offsets drift.

## Sources
All source citations below are local file:line references paired with GitHub permalinks pinned to local HEAD commit `db9fbb1dd67ce92c8463b7e282c2687c16508ae1`.

- `README.md:50-52`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/README.md#L50-L52
- `LICENSE:1-15`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/LICENSE#L1-L15
- `Cargo.toml:1-13`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/Cargo.toml#L1-L13
- `lib/data/RELEASE-GH.txt:4`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/data/RELEASE-GH.txt#L4
- `practice-tool/Cargo.toml:1-17`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/Cargo.toml#L1-L17
- `lib/libeldenring/Cargo.toml:1-15`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/Cargo.toml#L1-L15
- `practice-tool/src/lib.rs:48-83,179-262`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/lib.rs#L48-L83  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/lib.rs#L179-L262
- `practice-tool/src/practice_tool.rs:170-179,606-619,796-877`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/practice_tool.rs#L170-L179  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/practice_tool.rs#L606-L619  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/practice_tool.rs#L796-L877
- `practice-tool/src/config.rs:285-310`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/config.rs#L285-L310
- `practice-tool/src/widgets/mod.rs:1-16`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/mod.rs#L1-L16
- `practice-tool/src/widgets/target.rs:78-193`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/target.rs#L78-L193
- `practice-tool/src/widgets/character_stats.rs:7-76`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/character_stats.rs#L7-L76
- `practice-tool/src/widgets/cycle_speed.rs:11-47`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/cycle_speed.rs#L11-L47
- `practice-tool/src/widgets/deathcam.rs:7-24`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/deathcam.rs#L7-L24
- `practice-tool/src/widgets/position.rs:44-77,117-135`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/position.rs#L44-L77  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/position.rs#L117-L135
- `practice-tool/src/widgets/nudge_pos.rs:8-20`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/practice-tool/src/widgets/nudge_pos.rs#L8-L20
- `lib/libeldenring/src/version.rs:22-71,77-97`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/version.rs#L22-L71  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/version.rs#L77-L97
- `lib/libeldenring/src/memedit.rs:48-71,75-106`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/memedit.rs#L48-L71  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/memedit.rs#L75-L106
- `lib/libeldenring/src/params.rs:105-135,137-172,200-226`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/params.rs#L105-L135  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/params.rs#L137-L172  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/params.rs#L200-L226
- `lib/libeldenring/src/pointers.rs:211-243,321-342,372-380,394-396,518`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/pointers.rs#L211-L243  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/pointers.rs#L321-L342  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/pointers.rs#L372-L380  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/pointers.rs#L394-L396  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/pointers.rs#L518-L518
- `lib/libeldenring/src/codegen/base_addresses.rs:5-45,47-89,94-120,122-157,192-220,1190-1272`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/base_addresses.rs#L5-L45  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/base_addresses.rs#L47-L89  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/base_addresses.rs#L94-L120  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/base_addresses.rs#L122-L157  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/base_addresses.rs#L192-L220  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/base_addresses.rs#L1190-L1272
- `lib/libeldenring/src/codegen/param_data.rs:837,921,1026,1971,2051`  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/param_data.rs#L837  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/param_data.rs#L921  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/param_data.rs#L1026  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/param_data.rs#L1971  
  https://github.com/veeenu/eldenring-practice-tool/blob/db9fbb1dd67ce92c8463b7e282c2687c16508ae1/lib/libeldenring/src/codegen/param_data.rs#L2051

