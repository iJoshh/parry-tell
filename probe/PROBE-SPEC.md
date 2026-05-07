# Gate 0 Probe DLL — Specification

## Purpose

This is the spec the probe DLL is built to. The probe is a one-shot data-collection tool whose job is to verify Gate 0.A (animation read works in 1.16.1, animations map to AtkParam rows via offline lookup) and Gate 0.B (find the enemy "current target" field offset). It does NOT play audio, draw UI, or behave like the final mod — it's instrumentation only.

Output: a CSV log file Josh sends back after a 10-minute test fight. Orchestrator-Claude reads the log and decides "Gate 0 passes, build the real mod" vs "scope down to Option A" vs "specific field is wrong, here's the new candidate, rebuild probe."

## Architecture

```
parry-tell-probe.dll (loaded by Seamless via external_dlls)
└── DllMain (minimal — same loader-lock-safe pattern as hello.dll)
    └── Pin module
    └── Spawn worker thread, return
        └── Worker thread:
            ├── Wait for game ready (poll until WorldChrMan singleton resolves to non-null)
            ├── Resolve all module-relative offsets from TarnishedTool's 1.16.1 table
            ├── Resolve CSFeManImp singleton
            ├── Open CSV log file
            ├── Loop forever (~30Hz, sleep 33ms):
            │   1. Read CSFeManImp.bossHpBars[3] — log every active boss handle
            │   2. For each active boss ChrIns:
            │      - Read currentAnimation, currentAnimationTime
            │      - Read every candidate target field (8 candidates, see below)
            │      - Read last_act, npc_think_param_id, entity_id
            │   3. Read player.entityId, player.lockOnTarget
            │   4. If state changed since last frame, write a row to CSV
            └── On host shutdown: log handle is auto-closed by the OS
```

## Memory layout (lifted from TarnishedTool MIT for ER 1.16.1)

Source: `archaeology/06-tarnishedtool-borrow-map.md`. All offsets verified for ER 1.16.1.

### Module-relative bases (require `eldenring.exe` module base from `GetModuleHandleA(NULL)` or equivalent)

```cpp
constexpr uintptr_t WORLD_CHR_MAN_BASE        = 0x3D65F88;  // ChrInsByUpdatePrioBegin/End live inside
constexpr uintptr_t SOLO_PARAM_REPO_BASE      = 0x3D81EE8;  // for AtkParam_Npc lookups
constexpr uintptr_t LOCKED_TARGET_HOOK_PTR    = 0x717372;   // player.lockOnTarget — Practice Tool documents this
constexpr uintptr_t FN_CHR_INS_BY_HANDLE      = 0x507C70;   // resolve handle -> ChrIns
constexpr uintptr_t FN_GET_CHR_INS_BY_ENTITY  = 0x507E00;
// CSFeManImp singleton offset — STILL UNKNOWN. Requires one more Codex dispatch
// or first-launch search-for-non-null-pattern. See "Open question" below.
```

### WorldChrMan struct offsets (from `*WORLD_CHR_MAN_BASE`)

```cpp
constexpr uintptr_t WCM_CHR_INS_BY_PRIO_BEGIN = 0x1F1B8;
constexpr uintptr_t WCM_CHR_INS_BY_PRIO_END   = 0x1F1C0;
constexpr uintptr_t WCM_CHR_SET_POOL          = 0x10EF8;  // for handle-based lookup
```

### ChrIns struct offsets (each ChrIns pointer is 8 bytes)

```cpp
constexpr uintptr_t CHR_INS_ENTITY_ID         = 0x80;     // u32
constexpr uintptr_t CHR_INS_BLOCK_ID          = 0x6C;     // u32, skip if 0xFFFFFFFF
constexpr uintptr_t CHR_INS_NPC_PARAM_ID      = 0x60;     // u32, "what kind of enemy is this"
constexpr uintptr_t CHR_INS_MODULE_BAG        = 0x190;    // -> ChrModuleBag*
constexpr uintptr_t CHR_INS_CHR_MANIPULATOR   = 0x580;    // -> sub-struct that holds AI
constexpr uintptr_t CHR_INS_CHR_TYPE          = 0x68;     // u8 — for PvE filter
```

### ChrModuleBag struct offsets (from `*(ChrIns + 0x190)`)

```cpp
constexpr uintptr_t MOD_TIME_ACT              = 0x18;     // -> TimeAct module*
constexpr uintptr_t MOD_AI_THINK_VIA_MANIP    = 0xC0;     // *(*(chrIns+0x580)+0xC0) -> AI struct
```

### TimeAct module offsets (from `*(*(ChrIns+0x190)+0x18)`)

```cpp
constexpr uintptr_t TA_ANIMATION_ID           = 0xD0;     // s32 — currentAnimation
constexpr uintptr_t TA_ANIMATION_TIME         = 0xD4;     // f32 — guess; needs verification (could be 0xD8)
constexpr uintptr_t TA_ANIMATION_LENGTH       = 0xD8;     // f32 — guess; needs verification
```

(Animation time and length offsets may need adjustment — Practice Tool reads `cur_anim_time` and `cur_anim_length` at offsets adjacent to `cur_anim`, but the exact relative offsets weren't fully captured. Probe logs all 16 bytes after `cur_anim` so we can identify the correct ones from data.)

### AI Think struct offsets (from `*(*(ChrIns+0x580)+0xC0)`)

```cpp
constexpr uintptr_t AI_LAST_ACT               = 0xE9C2;   // u8 — current action class
constexpr uintptr_t AI_FORCE_ACT              = 0xE9C1;   // u8 — debug-set action override
constexpr uintptr_t AI_NPC_THINK_PARAM_ID     = ?;        // resolve from TarnishedTool offsets table
constexpr uintptr_t AI_TARGETING_SYSTEM       = 0xC480;   // pointer to targeting sub-struct
constexpr uintptr_t AI_SP_EFFECT_OBSERVE_COMP = ?;        // resolve from TarnishedTool offsets table

// THE GATE 0.B QUESTION:
// We need the "current target entity handle" field. Best candidate per
// archaeology/09 is SpEffectObserveEntry.Target = +0x18 on observe-comp list nodes.
// Probe logs ALL candidates in the AI struct neighborhood:
constexpr uintptr_t TARGET_CANDIDATE_RANGE_START = 0xE000;
constexpr uintptr_t TARGET_CANDIDATE_RANGE_END   = 0xF000;  // sweep this range
// Plus the explicit candidates from archaeology:
//   - SpEffectObserveEntry list at +SP_EFFECT_OBSERVE_COMP, each entry +0x18
//   - TargetingSystem at +0xC480, sub-fields TBD
```

### CSFeManImp / boss bars

Per `archaeology/09-targeting-and-boss-bar.md`, lifted from Erd-Tools-CPP (GPLv3 — we re-implement from documented facts, no code copying):

```cpp
// CSFeManImp singleton — module-relative offset is THE one open question.
// Resolution path (in order of preference):
//   1. Codex dispatch to find it in TarnishedTool's offset table
//   2. AOB scan from a known signature
//   3. First-launch "look for non-null pointer in plausible region" heuristic
constexpr uintptr_t CSFEMAN_IMP_BASE          = ?;  // TBD
constexpr uintptr_t FE_BOSS_HP_BARS           = ?;  // offset on CSFeManImp
constexpr int       FE_BOSS_HP_BAR_COUNT      = 3;
constexpr uintptr_t FE_BOSS_HP_BAR_STRIDE     = ?;  // sizeof(BossHpBar slot)
constexpr uintptr_t FE_BOSS_HANDLE_OFFSET     = 0x8;  // within slot
constexpr uint64_t  FE_INVALID_HANDLE         = 0xFFFFFFFFFFFFFFFFull;  // UINT64_MAX
```

## CSV log format

One row per state change (de-bounced — don't log a row every frame if nothing changed). Header:

```
timestamp_ms,event_type,boss_slot,boss_handle,boss_chr_ins,boss_npc_param_id,boss_animation_id,boss_animation_time,boss_last_act,player_entity_id,player_lock_on_target,target_candidate_offset_0xE000,target_candidate_offset_0xE004,...,target_candidate_offset_0xEFFC,sp_effect_observe_target,notes
```

Where:
- `timestamp_ms` — ms since DLL load
- `event_type` — `boss_appeared`, `boss_disappeared`, `boss_animation_changed`, `boss_target_field_changed`, `player_lock_changed`, `tick` (max once per second when nothing else changed, as a heartbeat)
- `boss_slot` — 0, 1, 2 for the three bars; -1 for non-boss-specific events
- `boss_handle` — UINT64_MAX if invalid; useful for cross-referencing
- `boss_chr_ins` — pointer (hex) for debug
- `boss_animation_time` — log raw bytes at TimeAct+0xD4..0xDF as a hex blob; we'll figure out the format from data
- `target_candidate_offset_*` — every 4 bytes from `aiThink + 0xE000` to `aiThink + 0xEFFC` as u32. 1024 columns. Yes that's a lot. We're hunting a needle in a haystack and storage is cheap.
- `sp_effect_observe_target` — the focused candidate from archaeology/09; logged separately for easy first-pass analysis

The probe should also log a one-time header section at the top of the file:

```
# parry-tell probe v1
# eldenring.exe module base: 0x...
# WorldChrMan resolved: 0x...
# CSFeManImp resolved: 0x... (or "FAILED" with reason)
# player ChrIns: 0x...
# player entity ID: 0x...
# probe start time: 2026-05-XX HH:MM:SS
```

## Build target

- Visual Studio 2022, v143 toolset, x64 Release, statically linked CRT.
- Single-file build: `probe.cpp`, `probe.vcxproj` — no external deps.
- Output: `parry-tell-probe.dll`, ~50KB.

## Test procedure (Josh's role, ~30 minutes total)

1. Build the probe DLL in VS.
2. Drop into Seamless `external_dlls`. Launch ER via `ersc_launcher.exe`.
3. Load a save with access to a boss. **Solo session, NOT co-op** — keeps the test data clean.
4. Fight Margit (Limgrave's mandatory boss-bar tutorial fight) for 5–10 minutes. Try to:
   - Get hit by various attacks (we want the data, not your win condition)
   - Switch lock-on mid-fight intentionally
   - Break lock and re-establish lock
   - If you summon Mimic Tear, do so AFTER the first 3 minutes, so we have data with and without an ally for the boss to target.
5. Quit ER cleanly.
6. Find `parry-tell-probe.csv` next to the DLL. Send via the GitHub repo.

## What success looks like

When I read the log, I'm looking for:

- **Gate 0.A pass:** `boss_animation_id` changes when Margit visibly starts a new attack. The animation IDs match what we extracted from `c1000.anibnd.dcx` (Margit/Morgott archive).
- **Gate 0.B pass:** ONE of the `target_candidate_offset_*` columns reliably:
  - Equals `player_entity_id` when Margit is visibly attacking Josh
  - Differs from `player_entity_id` when Margit is attacking Mimic Tear or facing away
  - Changes at the exact moment lock state changes
- **Boss bar trigger:** `boss_appeared` event fires when Margit's bar appears, `boss_disappeared` when she dies or you flee.

## What partial success looks like

- **0.A passes, 0.B fails:** drop the targeting filter for v1. Mod fires hue on every parryable attack. Document as a v1 limitation.
- **0.A fails (animations don't match extracted data):** check the TimeAct offset; might need to re-extract for a different patch level.
- **Boss bar trigger fails:** revert to "always active" detection — mod runs whenever player is in combat. Lower-precision but ships.

## Open questions before code generation

1. **CSFeManImp module-relative offset.** Need one Codex pass before probe code is written. (Or: probe ships without boss-bar trigger, just enumerates ALL ChrIns; we cross-reference manually from the log.)
2. **TimeAct animation time format.** Float seconds? Float frames? Int ticks? Probe logs raw bytes; format determined from data.
3. **Whether `eldenring.exe` always loads at the same base address (ASLR).** If yes, easy. If no, probe must read its own module base via `GetModuleHandleA`.

## What this spec produces

When Codex (or Claude) implements this:
- `probe/probe.cpp` (~300 lines)
- `probe/probe.vcxproj`
- `probe/README.md` — Josh-facing build + test instructions

When Josh runs it:
- `parry-tell-probe.csv` — the data we use to make the Gate 0 decision

When orchestrator processes the CSV:
- Either green-light Step 2 (production build) with confidence in offset table, OR identify the wrong offset and rebuild probe with corrected reads.
