# Hexinton CT Archaeology (v2.93, CE 7.5)

## Scope / caveat
This pass is on a single packed Cheat Engine table XML (`eldenring_all-in-one_Hexinton_v2.93_ce7.5.CT`), not a CE2FS tree. Findings are offset-technique archaeology only (reference use), and this table is old relative to ER 1.16+.

## 1) File metadata
- Source file: `.archaeology-sources/hexinton/eldenring_all-in-one_Hexinton_v2.93_ce7.5.CT`
- Size: `7,114,531` bytes (~6.8 MiB on disk), mtime `2023-04-03 13:06:52 -0500`.
- CE XML root: `CheatEngineTableVersion="42"` (`.CT:2`).
- Internal banner: `Author : Team Hexinton` and dated script header (`.CT:24-29`).
- Built-in version check uses `tablever=0x1000900000000` (`.CT:64`) and compares against EXE version via `getFileVersion(...)` (`.CT:63-68`), i.e. table expects game version string equivalent to `1.9.0.0` by its formatter logic (`.CT:57-61`).
- Additional embedded notes reference `Executable Version: 1.8.1.0` in coordinate scripts (`.CT:18776`, `.CT:18894`, `.CT:19006`).
- Repo README is feature-oriented and does not pin an exact ER patch (`README.md:1-71`).

## 2) Attack-related entry inventory (targeted)
Search terms used: `atk|Attack|parry|behavior|current attack|active attack|AtkParam`.

High-signal entries (Description + address/offset pattern + line):
- `NoAttack` -> `WorldChrMan` + offsets `1E508, 0*10, 520` (`.CT:4042` with offsets at `.CT:4047-4053`).
- `All No Attack` -> `CHR_DBG_FLAGS+D` (`.CT:5481`).
- `Force Parry Mode` -> `CHR_DBG_FLAGS+13` (`.CT:5523`).
- `Reflect Attack (May Crach)` -> script symbol `ReflectAttack` (`.CT:14228`, script at `.CT:14177-14223`).
- `Easy Parry (Makes it easier to Parry)` (`.CT:15147`).
- `Global Parry` (`.CT:15283`).
- `AutoParry` (`.CT:15301`, script starts `.CT:15313`).
- `Attack` param editor root (`.CT:32497`) + `Attack Logger` (`.CT:32527`).
- `AtkParam_Npc` editor root (`.CT:43414`) and second class-definition block (`.CT:91416+`).
- `AtkParam_Pc` class-definition block (`.CT:91718+`).
- `Behavior` param editor root (`.CT:41420`) with many behavior-related fields (e.g., `behaviorJudgeId`, `behaviorType`).

Notes:
- Broad grep finds many `atk*`/`behavior*` field names inside param editor schemas (hundreds of field-level entries), mostly static param row field definitions rather than runtime actor-state pointers.
- No explicit `Description` string for `current attack` / `active attack` / `currentAtkParamId` was found.

## 3) Effect Logger, and Attack/Bullet logger presence
- `Attack Logger` exists and is script-backed (`.CT:32527`). Mechanism:
  - AOB hook at `AttackAccessor` (`.CT:32533`), writes seen runtime attack-struct pointers into `AttackLog` ring-ish buffer (`.CT:32536-32555`), then Lua maps pointer->ID via `AttackTableInverse` (`.CT:32602`) for UI list.
- `Effect Logger` exists (`.CT:34190`). Mechanism:
  - AOB hook at `EffectIconAccessor` (`.CT:34196`), stores effect addresses in `EffectLog` (`.CT:34201-34219`), Lua maps via `EffectTableInverse` (`.CT:34270`) and also reads fields like VFX id at `+0x170` (`.CT:34283-34290`).
- `Bullet Logger` exists (`.CT:37443`). Mechanism:
  - Hook near bullet creation (`CreateBulletFunc`) captures bullet id `[rbx+4B4]` and shooter id `[rbx+4C0]` into `CreateBulletLog` (`.CT:37449-37488`), then Lua displays counts (`.CT:37507+`).

## 4) ChrIns / WorldChrMan offsets vs TGA
WorldChrMan base resolution:
- Hexinton resolves `WorldChrMan` by AOB scan + RIP-relative decode + `registerSymbol("WorldChrMan", ...)` (`.CT:214-220`).

Observed local/net slot usage:
- Defines `LocalPlayerOffset=10EF8` (`.CT:508`), widely used as `[WorldChrMan]+10EF8` chains (e.g., `.CT:1057`, `.CT:18231-18239`, `.CT:119766`).
- Also uses `[WorldChrMan]+1E508` chains for local-player oriented actor access (many entries, e.g. `.CT:3940`, `.CT:15057`, `.CT:15760`, `.CT:120945`).

Comparison to TGA references in your prompt:
- `+1E508` local-player family: **matches**.
- `+10EF8` net-player slot family: **matches**.
- Hexinton uses both families in parallel depending on script/feature context.

## 5) Param access pattern (SoloParamRepository?)
- No literal `SoloParamRepository` symbol/name appears.
- Hexinton resolves param roots through symbols `CSRegulationManagerImp` (`.CT:242-248`) and `PARAM` (`.CT:257`), then dereferences param tables from there (e.g., `[PARAM]+...` in scripts at `.CT:11726`, `.CT:11857`, `.CT:16518`).
- `Alt Param Patcher` builds a param index from `[CSRegulationManagerImp]+18` (`define(ParamPatch,[CSRegulationManagerImp]+18`, `.CT:90506`) and enumerates param descriptors to resolve ID tables (`.CT:90560-90640`).
- `AtkParam_Npc` / `AtkParam_Pc` are exposed both as CE entries and Lua classes (`.CT:43414`, `.CT:91416+`, `.CT:91718+`).

## 6) Unique vs TGA (likely value-add)
Potentially unique/high-value techniques from this table:
- Runtime loggers for three domains:
  - attack invocation stream (`Attack Logger`)
  - effect application stream (`Effect Logger`)
  - bullet spawn stream (`Bullet Logger`)
- The logger pattern is practical RE instrumentation: hook callsite -> capture runtime pointer/ids -> map back through param inverse tables for human-readable IDs.
- This is a stronger dynamic-observation workflow than static offset-only tables, and is likely the main transferable technique if TGA stack lacks equivalent runtime logging UX.

## 7) Concrete findings requested
1. `currentAtkParamId` exposed at runtime?
- **Not as an explicit named field** (`currentAtkParamId`/`current attack` label not found).
- **Functionally yes via Attack Logger**: it captures active attack struct pointers in live execution and maps them back to attack IDs (`AttackTableInverse`) (`.CT:32533-32555`, `.CT:32602`).

2. Mechanism?
- Predominantly **hook-based + computed mapping** for runtime attack/effect/bullet IDs.
- Not a simple fixed-chain direct memory read for a single `currentAtkParamId` field.

3. Offset-chain compatibility with TGA?
- **Yes at family level**: uses both `WorldChrMan + 0x1E508` and `WorldChrMan + 0x10EF8` chains.
- Exact downstream offsets are feature-specific and may have drifted since this 2023-era table.

4. Unique technique worth borrowing?
- **Yes**: the logger triad (attack/effect/bullet) plus inverse-table ID mapping is the most reusable concept for modern tooling archaeology.

## Bottom line
Hexinton mostly corroborates the same WorldChrMan/ChrIns offset families as TGA, while adding a notable dynamic instrumentation angle (attack/effect/bullet loggers) rather than a clean direct `currentAtkParamId` offset.
