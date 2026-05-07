# TGA Cheat Engine Table — Technique Reference

## ABSOLUTE RULE
TGA is a Cheat Engine table, not a shippable runtime module for our mod. The author explicitly warns against online use and bans. This pass is memory-layout archaeology only; no scripts/AOBs from TGA should be copied into our build artifacts.

## Repo summary
- Repo URL: `https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA`
- Local commit (this checkout): `2504848a543798f2e53964a1d1998af2f58ebb7e` (`git log --oneline -20` shows only `2504848 Release v1.17.0 (#142)`, likely shallow clone)
- License: no `LICENSE`/`COPYING` file found in this checkout root.
- README online-use warning: "This table is not meant to be used online and you will most likely be banned if you attempt to do so." (`README.md:29`)
- ER compatibility signals:
  - README says `Game: App ver. 1.16` (`README.md:23`)
  - Changelog for `v1.17.0` says "Supported version to v1.16.0" (`CHANGELOG.md:4`, `CHANGELOG.md:17`)

## .CT file inventory
- `.CT` files present in this local clone: **none** (`find ... -name '*.CT'` returned no matches).
- This checkout is CE2FS source form (`CheatTable/.../*.xml` + `*.cea`) rather than packed `.CT` release artifact.
- Effective "main table" in source form:
  - Root entry metadata: `CheatTable/CheatEntries/Open - The Grand Archives - Elden Ring/.xml` (532 bytes)
  - Root `Open` script: `CheatTable/CheatEntries/Open - The Grand Archives - Elden Ring/.cea` (43,293 bytes)

## Structure of a Cheat Engine Table (.CT) — quick primer for the orchestrator
In packed `.CT`, CE stores XML entries that correspond here to split files:
- `*.xml` files: CheatEntry metadata (`Description`, `Address`, `Offsets`, type, bitfields, dropdown links)
- `*.cea` files: Auto Assembler / Lua / C-script logic (hooks, symbol registration, helper automation)
- Parent group `.xml` files with `x-ce2fs-child-order` define tree structure.

In this repo, CE2FS exploded the single `.CT` into filesystem fragments, so pointer chains are mostly in per-entry `.xml`, and hook logic in sibling `.cea`.

## Reverse-engineering inventory — what TGA has already mapped

### Player ChrIns / WorldChrMan
- `Open` script enables singleton finder and then helper stack (`.cea:1186`-`.cea:1190`), including `FD4Singleton Finder & Symbol Registerer` (ID 1002).
- FD4 singleton finder discovers singleton addresses at runtime and registers them as symbols (`MiscWIP/Dependencies/FD4Singleton Finder & Symbol Registerer.cea:22`-`:35`). This is the current mechanism that supplies `WorldChrMan`/`SoloParamRepository` symbols.
- `getPlayerIns()` resolves local or slot player from `WorldChrMan`:
  - local player path: `WorldChrMan + 0x1E508`
  - network slot path: `[WorldChrMan + 0x10EF8] + 0x10 * slot`
  - (`MiscWIP/Dependencies/Global Functions/Get functions.cea:97`-`:109`)
- Player animation chain confirms ChrIns path from `WorldChrMan` through net-player list:
  - current animation offsets include `+10EF8`, `+0*10`, `+190`, `+18`, `+20`
  - (`Hero/Animation/Current Animation/.xml:6`-`:13`)

AOB note:
- The old direct `WorldChrMan` AOB in root `Open` script is present but commented out (`Open/.cea:570`), indicating migration to singleton-finder path.

### Enemy ChrIns / locked-on target
- Targeted enemy helper installs an instruction hook (AOB-based signature scan) and stores `rax` into `TargetedNpcInfo_Data` (`Scripts/Helpers/Targeted Npc Info/.cea:2`, `:6`, `:16`-`:18`).
- This `TargetedNpcInfo_Data` is then used as base for all targeted enemy reads (`Targeted Npc Info/Character Type.xml:7`, `Targeted Npc Info/NpcId.xml:6`).
- Practical chain example for targeted NPC position-relative operations uses `[[[TargetedNpcInfo_Data]+190]+68]+70` (`Targeted Npc Info/Position/Teleport Self to Npc.cea:3`).

### Animation state (current animation, animation time, animation frame)
- Player current animation:
  - base `WorldChrMan`, offsets `10EF8 -> 0*10 -> 190 -> 18 -> 20`
  - (`Hero/Animation/Current Animation/.xml:6`-`:13`)
- Player current animation time:
  - same chain ending `+24` for length played
  - (`Hero/Animation/Current Animation/Length played [seconds].xml:6`-`:13`)
- Targeted NPC current animation:
  - base `TargetedNpcInfo_Data`, offsets `190 -> 18 -> 20`
  - (`Targeted Npc Info/Animation/Current Animation/.xml:6`-`:11`)
- Last-hit NPC current animation:
  - base `LastHitNpcAddr`, offsets `190 -> 18 -> 20`
  - (`Last Hit Npc Info/Animation/Current Animation/.xml:6`-`:11`)

No direct frame-counter field with an explicit "frame" label was found in these animation helper groups; table exposes animation ID and elapsed time.

### Attack state — THE LOAD-BEARING SECTION
Search terms reviewed: `atk`, `attack`, `AtkParam`, `parry`, `isDisableParry`, `behavior`, `currentatk`, `atkparamid`, `parry window`.

What exists:
- Param-field helpers for attack params (not live current-attack state), e.g.:
  - `atkBehaviorId` at `+0x82` (`ID Helpers/AtkParam_Pc/Start/atkBehaviorId.xml:3`-`:6`)
  - `isDisableParry` bit at `+0x18A` (`ID Helpers/AtkParam_Pc/Start/isDisableParry.xml:3`-`:8`)
  - `parryForwardOffset` at `+0x1AE` (`ID Helpers/AtkParam_Pc/Start/parryForwardOffset.xml:3`-`:6`)
- NpcParam/NpcThinkParam views for targeted/last-hit entities (param metadata perspective), e.g.:
  - `parryAttack` field (`Targeted Npc Info/NpcParam/parryAttack.xml:3`-`:6`)
  - `rangedAttackId` in NpcThinkParam (`Targeted Npc Info/NpcThinkParam/rangedAttackId.xml:3`-`:6`)

What is not found:
- No explicit `currentAtkParamId` field.
- No "show parry windows" feature string or equivalent naming.
- No direct `ChrIns -> active attack id` pointer chain exposed in helper entries.

CRITICAL QUESTION result:
- A direct "show parry windows" / "current attack id" runtime feature is **not present** in this source checkout.

### Param table access (regulation.bin runtime data)
- TGA uses `SoloParamRepository` as the base symbol for param roots (e.g., `AtkParam_Npc`, `AtkParam_Pc`) (`Param Mods/Params/AtkParam_Npc.xml:8`, `Param Mods/Params/AtkParam_Pc.xml:8`).
- Atk param base roots from SoloParamRepository:
  - `AtkParam_Npc`: offsets `280 -> 80 -> 80 -> 0` (`AtkParam_Npc.xml:9`-`:14`)
  - `AtkParam_Pc`: offsets `2C8 -> 80 -> 80 -> 0` (`AtkParam_Pc.xml:9`-`:14`)
- Runtime helper bridge in `Open` script updates `paramHelper+8` addresses via `paramUtils:getIdAddressInParam(...)` every second (`Open/.cea:1197`-`:1204`).
- C-side param API is via `CSRegulationManager` row walking (`CParamUtils.cea:19`, `:23`-`:33`, `:111`-`:118`), i.e., not bespoke per-param AOB chains in this area.

### Hit detection / damage state
Examples around per-character combat state:
- `NoDamage` flag from character module path:
  - base `TargetedNpcInfo_Data`, offsets `190 -> 0 -> 19B`, bit 1 (`Targeted Npc Info/Character Flags/NoDamage.xml:10`-`:15`)
- `No Hit` / `No Attack` bit flags cluster at offset `+530` from character base (`Targeted Npc Info/Character Flags/No Hit.xml:10`-`:13`, `.../No Attack.xml:10`-`:13`).
- Toughness and SuperArmor nearby under `+190` subtree:
  - toughness durability `+48 -> +10` (`.../Toughness/ToughnessDurability.xml:8`-`:10`)
  - super armor durability `+40 -> +10` (`.../SuperArmor/SADurability.xml:8`-`:10`)

These clusters are useful adjacency hints for live combat-state neighborhoods, but still do not expose a named current attack-param id.

## Memory map — what TGA has reverse-engineered

| What's read | Pointer chain (global + offsets) | File:line in .CT source tree | Comment from table |
|---|---|---|---|
| Local Player ChrIns | `WorldChrMan + 0x1E508` | `MiscWIP/Dependencies/Global Functions/Get functions.cea:108` | `getPlayerIns()` fallback path |
| Net player slot ChrIns | `[WorldChrMan + 0x10EF8] + 0x10*slot` | `.../Get functions.cea:102`-`:103` | Multiplayer slot access |
| Player current animation id | `WorldChrMan -> 10EF8 -> 0*10 -> 190 -> 18 -> 20` | `Hero/Animation/Current Animation/.xml:6`-`:13` | `Current Animation` |
| Player animation elapsed seconds | same path, terminal `+24` | `Hero/Animation/Current Animation/Length played [seconds].xml:6`-`:13` | `Length played [seconds]` |
| Targeted enemy base | Hook-captured into `TargetedNpcInfo_Data` | `Scripts/Helpers/Targeted Npc Info/.cea:6`, `:16`-`:18` | AOB hook writes `rax` into helper symbol |
| Targeted enemy current animation id | `TargetedNpcInfo_Data -> 190 -> 18 -> 20` | `.../Targeted Npc Info/Animation/Current Animation/.xml:6`-`:11` | `Current Animation` |
| Last-hit enemy base | Hook-captured into `LastHitNpcAddr` | `Scripts/Helpers/Last Hit Npc Info/.cea:9`, `:15`-`:18` | AOB hook writes `rbx` into helper symbol |
| Last-hit current animation id | `LastHitNpcAddr -> 190 -> 18 -> 20` | `.../Last Hit Npc Info/Animation/Current Animation/.xml:6`-`:11` | `Current Animation` |
| Targeted NpcParam root | `TargetedNpcInfo_Data -> 58 -> 18 -> C0 -> 18 -> 0` | `.../Targeted Npc Info/NpcParam/.xml:7`-`:14` | NpcParam table view for target |
| Targeted NpcThinkParam root | `TargetedNpcInfo_Data -> 58 -> 18 -> C0 -> 30 -> 0` | `.../Targeted Npc Info/NpcThinkParam/.xml:7`-`:14` | Think param table view |
| `AtkParam_Pc` root in param repository | `SoloParamRepository -> 2C8 -> 80 -> 80 -> 0` | `Param Mods/Params/AtkParam_Pc.xml:8`-`:14` | Param table root |
| `AtkParam_Npc` root in param repository | `SoloParamRepository -> 280 -> 80 -> 80 -> 0` | `Param Mods/Params/AtkParam_Npc.xml:8`-`:14` | Param table root |
| `isDisableParry` field (AtkParam_Pc row) | `AtkParam_Pc row + 0x18A` | `Scripts/Helpers/ID Helpers/AtkParam_Pc/Start/isDisableParry.xml:3`-`:8` | Param field offset |
| `atkBehaviorId` field (AtkParam_Pc row) | `AtkParam_Pc row + 0x82` | `.../AtkParam_Pc/Start/atkBehaviorId.xml:3`-`:6` | Param field offset |
| Character `NoDamage` flag (targeted) | `TargetedNpcInfo_Data -> 190 -> 0 -> 19B (bit1)` | `.../Character Flags/NoDamage.xml:10`-`:15` | Damage immunity flag |

## Concrete findings for our v1 build

1. **Does TGA reveal a path from ChrIns to currentAtkParamId?**
   - **No (directly).** This checkout exposes ChrIns roots, animation state, NpcParam/NpcThinkParam views, and param-row field maps, but no explicit `currentAtkParamId` chain or "show parry windows" feature.

2. **If yes: what's the offset chain?**
   - Not available in this source snapshot.
   - Closest useful pieces are:
     - ChrIns/combat neighborhood around `+190` (animation/toughness/superarmor/flags)
     - AtkParam row field offsets (`atkBehaviorId`, `isDisableParry`, etc.) once an attack param row address is known.

3. **What ER version does this TGA table currently target?**
   - Supported app version is 1.16.x per README/changelog (`README.md:23`, `CHANGELOG.md:17`).

4. **Single biggest insight for Gate 0**
   - TGA’s **most reusable technique** is the entity-capture helper pattern: hook one stable runtime instruction to materialize live per-entity bases (`TargetedNpcInfo_Data`, `LastHitNpcAddr`), then traverse known `+190`-subtree offsets for combat state and map into param space separately.

## Sources
- Local repo: `https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA`
- Local commit: `2504848a543798f2e53964a1d1998af2f58ebb7e`
- `README.md:23`, `README.md:29`
- `CHANGELOG.md:4`, `CHANGELOG.md:17`
- `CheatTable/CheatEntries/Open - The Grand Archives - Elden Ring/.cea:565`-`:575`, `:1186`-`:1190`, `:1197`-`:1204`
- `.../MiscWIP/Dependencies/FD4Singleton Finder & Symbol Registerer.cea:22`-`:35`
- `.../MiscWIP/Dependencies/Global Functions/Get functions.cea:97`-`:109`
- `.../Hero/Animation/Current Animation/.xml:6`-`:13`
- `.../Scripts/Helpers/Targeted Npc Info/.cea:2`, `:6`, `:16`-`:18`
- `.../Scripts/Helpers/Last Hit Npc Info/.cea:2`, `:9`, `:15`-`:18`
- `.../Scripts/Helpers/Targeted Npc Info/Animation/Current Animation/.xml:6`-`:11`
- `.../Scripts/Helpers/Last Hit Npc Info/Animation/Current Animation/.xml:6`-`:11`
- `.../Scripts/Helpers/Targeted Npc Info/NpcParam/.xml:7`-`:14`
- `.../Scripts/Helpers/Targeted Npc Info/NpcThinkParam/.xml:7`-`:14`
- `.../Param Mods/Params/AtkParam_Npc.xml:8`-`:14`
- `.../Param Mods/Params/AtkParam_Pc.xml:8`-`:14`
- `.../Scripts/Helpers/ID Helpers/AtkParam_Pc/Start/atkBehaviorId.xml:3`-`:6`
- `.../Scripts/Helpers/ID Helpers/AtkParam_Pc/Start/isDisableParry.xml:3`-`:8`
