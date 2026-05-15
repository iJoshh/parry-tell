// parry-tell-probe v6 — discovery probe (replaces v5f)
//
// SCOPE: One-hour memory-state capture across solo + co-op gameplay to identify
// the runtime parry-active flag (and ideally the hyperarmor flag) by post-session
// correlation against the TAE database (`data/parry_data.json`, 6,738 windows).
//
// SPEC: probe/v6/PROBE-V6-SPEC.md (revision 4, Codex green-lit). Read in full.
//
// THREE MODES (config: [capture] mode = ...):
//   - smoke:         60s sanity check, Tier 1+2 only, animation-time calibration
//   - qualification: 2-3 min vs ONE locked-on enemy, all tiers on focused only
//   - discovery:     full ~1 hr session, focused @ ~90 Hz, top @ 10 Hz, lesser @ 2 Hz
//
// HARD RULES (v6 spec — the post-write self-review checks these):
//  - The detour ONLY fills a preallocated buffer + atomic queue push. No compute.
//  - No VirtualQuery on the hot path. LooksLikeUserPtrFast (no syscalls) only.
//  - All reads SEH-wrapped via SafeRead<T> / SafeReadBytes.
//  - Module pinned (GET_MODULE_HANDLE_EX_FLAG_PIN). Hook stays installed until exit.
//  - All offsets in the ChrIns +0x38..+0x1F0 range captured as RAW field_at_0xNN.
//  - DLL does NOT load parry_data.json. Analysis is post-session Python.
//  - Init order is fixed (DllMain → worker → boot log → config → ER version
//    → sig scan → roster validation → buffer pool → manifest → hook install
//    → F11 watcher → steady state). Hook installs LAST.
//  - Smoke mode writes Tier 1+2 only (no Tier 3 binary records).
//  - Capture all of {0x038, 0x060, 0x064, 0x068, 0x06C, 0x080, 0x1E8} per enemy.
//
// CARRIED FROM v5f WITHOUT CHANGE:
//  - SEH-wrapped SafeRead<T>, MatchesAt, ParseSig, ScanModuleExecForSig
//  - LooksLikeUserPtrFast / LooksLikeUserPtr / LooksReadable<T>
//  - RipRelativeDeref, VerifyFunctionAtAddr
//  - ER FileVersion check, sig scans for WCM / GetChrInsFromHandle / UpdateUIBarStructs
//  - Module pin in DllMain, MinHook install with rollback, F11 watcher pattern
//
// NEW IN v6 (vs v5f):
//  - INI config (hand-rolled, no third-party dep)
//  - Buffer pool: 256 × 256 KB = 64 MB; SPSC ring buffer between detour + worker
//  - Worker thread: delta encoding off the game thread, binary record writer
//  - CSFeManImp sig scan + boss-bar enumeration (slots[3], handle at +0x8, sentinel ~0)
//  - WCM ChrInsByUpdatePrioBegin/End enemy roster (+0x1F1B8 / +0x1F1C0) under
//    the 7-check init-time quarantine (fall back if checks fail; not fail-closed)
//  - TimeAct chain walk per enemy (chrIns+0x190 → +0x18) plus child-pointer follow
//  - ai_struct walk (chrIns+0x580 → +0xC0 → +0xE000..+0xF000)
//  - Three-tier sampling rates: focused hook-tick, top 10 Hz, lesser 2 Hz
//  - Decimation phase staggering via hash(handle) % N
//  - Producer-side emergency drop when free_pool < 4 for 200ms
//  - Worker-side adaptive stepdown on rolling drop ratio > 5%
//  - Session manifest (schema, build, ER FileVersion, sig results, config dump)
//  - Smoke calibration report (anim-time candidate analysis)
//  - Region-relative records: (region_id, region_base_abs, source_chain,
//    payload_offset, payload_len, payload[, child_source_offset])
//
// BUILD: VS 2022 v143, x64 Release, /MT, vendored MinHook (BSD-2). probe.vcxproj
// already wired for this. Output: bin/Release/parry-tell-probe.dll.
//
// TARGET: Elden Ring 2.6.1.0 only. FileVersion check fails closed.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <unordered_map>
#include <cmath>

#include "MinHook.h"

// ===========================================================================
// Constants
// ===========================================================================

#define PROBE_VERSION_STR "v7.3-target-scan"
// v7.3-target-scan (Phase 4.0 Gate 0.B deep AI-struct scan, 2026-05-15):
// After v7.2 found ZERO ChrIns* matches and ZERO meaningful FieldInsHandle
// matches across 22 KB of boss territory, Codex's adversarial review
// pointed at TarnishedTool prior art:
//   - AiThink.TargetingSystem @0xC480 (target-related sub-struct)
//   - AiThink.SpEffectObserveComp @0xDBF0 (linked-list, entries have Target @+0x18)
//   - AiThink.AiAttackComp @0xDF10
//   - ChrIns.targetHandle @+0x6A0 (Erd-Tools-CPP layout)
// Of those: ChrIns+0x6A0/+0x6B0/+0x6B8/+0x6C0 verified offline against the
// existing v7.2 region-0 (chr_ins_root) capture as 100% zero on enemies;
// refuted (enemies don't have a target field in the player +0x6A0..+0x6C0
// neighborhood — that range is player-specific lock-on storage).
// v7.3 additions:
//   REGION_AI_STRUCT_FAR  (16): ai_struct +0x4000..+0x8000 (16 KB)
//   REGION_AI_STRUCT_DEEP (17): ai_struct +0x8000..+0xC000 (16 KB)
//   REGION_AI_STRUCT_TGT  (18): ai_struct +0xC000..+0xE000 (8 KB)  — TargetingSystem + SpEffectObserveComp head
//   REGION_MODULE_BAG_MEMBER (9, RE-ENABLED): bodies of every valid pointer
//      in module_bag head [0..0x100]. v6.3 instrumentation; capped at 16
//      modules × 512 B = 8 KB per focused enemy. The AI module pointer at
//      module_bag +0x38 is the prime suspect.
// Friendly-exclusion bug fix: qualification-nearest now refuses to pick the
// local player's ChrIns as a focused enemy (was happening in 21% of v7.2
// samples, polluting analysis).
// Per-sample budget: existing focused-enemy ~32 KB + new ~48 KB = ~80 KB;
// MAX_SAMPLE_BYTES=256 KB. Still comfortable. New regions ordered LAST in
// WriteTier3ForEnemy so any per-sample overflow drops new data first.
//
// v7.2-target-scan (Phase 4.0 Gate 0.B expanded coverage, 2026-05-12):
// Three new regions on focused enemies to widen the target-of-attention
// search territory before another roundtrip:
//   REGION_AI_STRUCT_MID  (13): ai_struct +0x1000..+0x4000 (12 KB mid-range)
//   REGION_ACTION_REQ_HEAD (14): ActionRequest module +0x000..+0x200 (512 B)
//   REGION_PLAYER_CHR_INS (15): player ChrIns +0x000..+0x800 (2 KB)
// Region 15 is sample-scoped (one capture of the LOCAL player's ChrIns
// body per sample) but emitted under the focused enemy's region list for
// wire-format compatibility. The analyzer dispatches by region_id, so
// the parsing path works unchanged.
// Per-sample byte budget: ~17 KB (existing) + 14.5 KB (new) = ~32 KB;
// MAX_SAMPLE_BYTES=256 KB. Comfortable. v7.1's player_handle in the sample
// header is preserved.
//
// v7.1-target-scan (Phase 4.0 Gate 0.B follow-up, 2026-05-12):
// v7.0 capture showed zero matches between captured AI-struct/AI-bag/module-bag
// u64 slots and player_chr_ins, confirming the target field stores a
// FieldInsHandle, not a pointer. v7.1 captures the player's own handle (at
// PlayerIns+0x08) into the v6.2 player instrumentation block's reserved
// 8-byte slot. probe_bin.py already parsed that slot as "reserved (probe
// writes 0)"; the field is renamed to `player_handle` and old captures
// continue to parse as `player_handle == 0`. No schema bump needed.
// All v7.0 regions and capture behavior are preserved unchanged.
//
// v7.0-target-scan (Phase 4.0 Gate 0.B research build, 2026-05-11):
// Adds three new Tier 3 region captures around the AI struct, AI bag, and
// module bag head, so the offline analyzer (`tools/analyze_target_field.py`)
// can scan every 8-byte slot for the player's ChrIns* and rank candidate
// target-of-attention offsets. Emitted only on focused enemies (the focus
// emphasis gate already used for TIME_ACT_FOCUS). All other behavior is v6.4.
//   New regions (additive, no schema break — IDs 10/11/12):
//     REGION_AI_BAG_HEAD     (10): ai_bag    +0x000..+0x400 (1024 B)
//     REGION_AI_STRUCT_HEAD  (11): ai_struct +0x000..+0x1000 (4096 B) PRIMARY
//     REGION_MODULE_BAG_HEAD (12): module_bag +0x000..+0x100 (256 B)
//   The existing REGION_AI_STRUCT (5) at +0xE000..+0xF000 stays as a fallback
//   territory in case the target field doesn't live in the head.
//   Co-op safety unchanged: read-only memory only, SEH-wrapped derefs,
//   loader-lock-safe DllMain, friendly-PC exclusion intact.
//
// v6.4 (production after research-007 v6.3 capture analysis): the v6.3 capture
// resolved ALL THREE research-006 offset questions with high confidence:
//   Q1 world pos: phys-chain (bag→+0x68→+0x70) wins; legacy +0x6C0 also works
//                 for player (chunk wraps on phys).
//   Q2 enemy anim_id: TimeAct + 0xD0 (path A, the ORIGINAL v6.1.1 offset) is
//                 correct. v6.2 was a false-alarm sample — focused enemy was
//                 not actively animating. v6.3 (12,467 focused rows of c4311
//                 Soldier + c4382 Knight) showed 8,265 nonzero anim_id reads
//                 with 89 transitions and clean anim_time monotonicity at
//                 +0x24 and +0x28.
//   Q3 player lock-on: +0x6B0 (FieldInsHandle), +0x6B4 (TargetArea u32).
//                 v6.3 fixed in_lock_on derivation via playerLockHandleEffective.
//
// v6.4 changes:
//   - drops Tier 3 instrumentation regions 6/7/8/9 (phys_module_body,
//     action_request_body, time_act_child_body, module_bag_member). Their
//     purpose is done. The values we need (phys pos, anim candidates,
//     pointer absolute addresses) are in the v6.2 enemy/player header block.
//   - excludes ALL WCM_PLAYER_ARRAY slots (not just slot 0) from the roster
//     sweep so co-op friendlies aren't picked as focused enemies by
//     focus_reason=qualification_nearest.
//   - adds audible F11 feedback: MessageBeep with distinct tones for
//     ARMED vs DISARMED so Josh can hear the transition without an overlay.
//   - schema stays at v2 (region drops are additive — parser tolerates
//     missing region IDs cleanly; v6.2/v6.3 captures still parse).
//
// Future re-analysis: every wire-format field that was added in v6.2/v6.3
// (anim_id_path_b, anim_id_path_c, read_idx, action_request, phys_module,
// world_pos_phys, player_chr_ins_vtable, player_pos_phys, player_phys_module,
// player_lock_new, player_lock_area_new, legacy lock @ +0x6A0) STAYS in the
// wire format — we keep all good data flowing.
#define PROBE_SCHEMA_VERSION 2u

// Buffer pool sized per spec §buffer pool sizing (rev4):
// 256 × 256 KB = 64 MB; ~2.8s of stall tolerance at 90 Hz focused capture.
// Each buffer holds ONE complete sample (Tier 1 + Tier 2 + all enabled
// Tier 3 regions for all tracked enemies). Truncation flag if exceeded.
static constexpr size_t MAX_SAMPLE_BYTES = 256u * 1024u;       // 256 KB / buffer
static constexpr size_t BUFFER_POOL_SIZE = 256u;               // 64 MB total
static_assert(MAX_SAMPLE_BYTES * BUFFER_POOL_SIZE == 64ull * 1024 * 1024,
              "buffer pool sizing mismatch");

// Tier 3 region byte caps (per spec §Tier 3 table). These are the MAX
// payload_len values the producer will copy per region. Smaller actual
// reads are OK; the worker honors payload_len.
static constexpr size_t REGION_CAP_CHR_INS_ROOT     = 0x800;   // 2048
static constexpr size_t REGION_CAP_MODULE_BAG       = 0x200;   //  512
static constexpr size_t REGION_CAP_TIME_ACT         = 0x2000;  // 8192
static constexpr size_t REGION_CAP_TIME_ACT_FOCUS   = 0x20;    //   32
static constexpr size_t REGION_CAP_TIME_ACT_CHILD   = 0x100;   //  256 per child
static constexpr size_t REGION_CAP_AI_STRUCT        = 0x1000;  // 4096
static constexpr int    TIME_ACT_CHILD_SCAN_BYTES   = 0x800;   // first 2KB scanned for ptrs
static constexpr int    TIME_ACT_CHILD_MAX          = 8;       // cap children per entity

// v6.2 instrumentation regions (schema v2): wider capture surface for the
// three offset bugs identified in research/006-SYNTHESIS.md.
static constexpr size_t REGION_CAP_PHYS_MODULE      = 0x200;   //  512 — ChrPhysicsModule body (world position leaf at +0x70)
static constexpr size_t REGION_CAP_ACTION_REQUEST   = 0x200;   //  512 — ActionRequest module body (Erd-Tools enemy anim_id at +0x90)
static constexpr size_t REGION_CAP_TIME_ACT_CHILD_BODY = 0x200; // 512 — first 3 TimeAct child bodies (wider than 0x100 child scan)
static constexpr int    TIME_ACT_CHILD_BODY_MAX     = 3;       // cap deep-body children per entity (instrumentation)

// v6.3 instrumentation regions (research 007 follow-up): module-bag-wide scan
// to find where the enemy anim_id actually lives. Module bag is ~256 bytes of
// pointer table at ChrIns+0x190; we capture the body of every valid pointer
// in [0..0x100] at 8-byte stride.
static constexpr size_t REGION_CAP_MODULE_BAG_MEMBER = 0x200;   //  512 — module body
static constexpr int    MODULE_BAG_MEMBER_SCAN_BYTES = 0x100;   //  256 bytes of bag head scanned
static constexpr int    MODULE_BAG_MEMBER_MAX        = 16;      //  cap modules captured per focused enemy

// v7.0 target-of-attention research regions (Phase 4.0 Gate 0.B):
// Capture wide windows around the AI struct, AI bag, and module bag head so the
// offline analyzer (`tools/analyze_target_field.py`) can scan every 8-byte slot
// for matches against the player's ChrIns* (already in the sample header at
// player_chr_ins_abs). This is read-only instrumentation; no new memory writes,
// no new pointer chains beyond what v6.4 already walks.
static constexpr size_t REGION_CAP_AI_BAG_HEAD       = 0x400;   // 1024 — first 0x400 of ai bag (covers +0xC0 ai_struct ptr + surrounding slots)
static constexpr size_t REGION_CAP_AI_STRUCT_HEAD    = 0x1000;  // 4096 — first 0x1000 of ai struct (PRIMARY target territory per research plan)
static constexpr size_t REGION_CAP_MODULE_BAG_HEAD   = 0x100;   //  256 — first 0x100 of module bag (covers +0x38 ai, +0x50 talk, +0x58 event, +0x80 action_req)
// v7.2 target-of-attention expanded coverage (Phase 4.0 Gate 0.B continued):
// After v7.0 confirmed pointer-equality scored 0% across all captured u64
// slots, v7.1 added handle-shape equality. v7.2 widens the search territory
// in case the target field lives outside the head regions:
//   REGION_CAP_AI_STRUCT_MID   (13): ai_struct +0x1000..+0x4000 (12288 B)
//   REGION_CAP_ACTION_REQ_HEAD (14): action_request +0x000..+0x200 (512 B)
//   REGION_CAP_PLAYER_CHR_INS  (15): player ChrIns +0x000..+0x800 (2048 B)
// Per-sample byte budget: existing focused-enemy footprint ~17 KB; +14.5 KB
// new = ~32 KB; writer cap MAX_SAMPLE_BYTES=256 KB. Comfortable.
static constexpr size_t REGION_CAP_AI_STRUCT_MID     = 0x3000;  // 12288 — ai_struct +0x1000..+0x4000 (mid-range)
static constexpr size_t REGION_CAP_ACTION_REQ_HEAD   = 0x200;   //   512 — first 0x200 of ActionRequest module body
static constexpr size_t REGION_CAP_PLAYER_CHR_INS    = 0x800;   //  2048 — player ChrIns first 0x800 (per-sample, ONCE per sample, not per enemy)
// v7.3 target-of-attention research, AI-struct-deep + module-bag-bodies pass:
// After v7.2's coverage gap analysis (and Codex adversarial review pointing at
// TarnishedTool's named offsets in the AiThink struct), capture the unscanned
// AI struct range +0x4000..+0xE000 in three chunks, with extra focus on the
// known interesting territory near +0xC480 (TargetingSystem) and +0xDBF0
// (SpEffectObserveComp). Also re-enable v6.3's MODULE_BAG_MEMBER scan to
// inspect the BODIES of module-bag-resident modules (the AI module pointer
// at module_bag +0x38 is the prime suspect).
static constexpr size_t REGION_CAP_AI_STRUCT_FAR     = 0x4000;  // 16384 — ai_struct +0x4000..+0x8000
static constexpr size_t REGION_CAP_AI_STRUCT_DEEP    = 0x4000;  // 16384 — ai_struct +0x8000..+0xC000
static constexpr size_t REGION_CAP_AI_STRUCT_TGT     = 0x2000;  // 8192  — ai_struct +0xC000..+0xE000 (TargetingSystem @0xC480, SpEffectObserveComp @0xDBF0)

// Region IDs (spec §Region-relative offsets rev3):
enum RegionId : uint8_t {
    REGION_CHR_INS_ROOT          = 0,
    REGION_MODULE_BAG            = 1,
    REGION_TIME_ACT_MODULE       = 2,
    REGION_TIME_ACT_FOCUS        = 3,
    REGION_TIME_ACT_CHILD        = 4,
    REGION_AI_STRUCT             = 5,   // legacy tail (+0xE000..+0xF000)
    // v6.2 instrumentation (schema v2):
    REGION_PHYS_MODULE           = 6,   // ChrPhysicsModule body (world pos leaf)
    REGION_ACTION_REQUEST        = 7,   // ActionRequest module body (enemy anim_id alt path)
    REGION_TIME_ACT_CHILD_BODY   = 8,   // first 3 TimeActChild bodies (deeper capture)
    // v6.3 instrumentation (research 007 follow-up):
    REGION_MODULE_BAG_MEMBER     = 9,   // body of any valid pointer in module bag head [0..0x100]
    // v7.0 target-of-attention research (Phase 4.0 Gate 0.B):
    REGION_AI_BAG_HEAD           = 10,  // ai bag +0x000..+0x400 (1024 bytes)
    REGION_AI_STRUCT_HEAD        = 11,  // ai struct +0x000..+0x1000 (4096 bytes) PRIMARY
    REGION_MODULE_BAG_HEAD       = 12,  // module bag +0x000..+0x100 (256 bytes)
    // v7.2 target-of-attention expanded coverage (Phase 4.0 Gate 0.B continued):
    REGION_AI_STRUCT_MID         = 13,  // ai struct +0x1000..+0x4000 (12288 bytes)
    REGION_ACTION_REQ_HEAD       = 14,  // ActionRequest module +0x000..+0x200 (512 bytes)
    REGION_PLAYER_CHR_INS        = 15,  // player ChrIns +0x000..+0x800 (2048 bytes) — ONE per sample
    // v7.3 target-of-attention deep AI-struct scan + module-bag-member-body re-enable:
    REGION_AI_STRUCT_FAR         = 16,  // ai_struct +0x4000..+0x8000 (16384 bytes)
    REGION_AI_STRUCT_DEEP        = 17,  // ai_struct +0x8000..+0xC000 (16384 bytes)
    REGION_AI_STRUCT_TGT         = 18,  // ai_struct +0xC000..+0xE000 (8192 bytes) — TargetingSystem + SpEffectObserveComp
    // (region 9 / REGION_MODULE_BAG_MEMBER is re-enabled; existing enum slot)
};

// Capture modes (spec §Modes):
enum CaptureMode : uint8_t {
    MODE_SMOKE         = 1,
    MODE_QUALIFICATION = 2,
    MODE_DISCOVERY     = 3,
};

// Focused-enemy reason (spec Tier 2 fields):
enum FocusedReason : uint8_t {
    FOCUS_NONE                  = 0,
    FOCUS_LOCK_ON               = 1,
    FOCUS_BOSS_BAR_0            = 2,
    FOCUS_QUALIFICATION_NEAREST = 3,
};

// Enemy class (which sampling tier the enemy is in this sample):
enum EnemyClass : uint8_t {
    ENEMY_CLASS_FOCUSED = 0,
    ENEMY_CLASS_TOP     = 1,
    ENEMY_CLASS_LESSER  = 2,
};

// Per-enemy slot caps (spec §Sample sizing):
static constexpr int MAX_TRACKED_TOP    = 8;   // 1 focused + 7 other top-tier
static constexpr int MAX_TRACKED_LESSER = 8;   // additional lesser-tier
static constexpr int MAX_TRACKED_ENEMIES = MAX_TRACKED_TOP + MAX_TRACKED_LESSER;

// Decimation factors at 90 Hz hook rate (spec §Decimation phase staggering):
//   N=9  -> ~10 Hz (top-tier Tier 3 in discovery, top-tier Tier 1+2 in lesser)
//   N=45 -> ~2  Hz (lesser-tier Tier 3)
// We enforce by `(tick_count + phase) % N == 0`.
static constexpr uint32_t DECIM_TOP_TIER3   = 9;
static constexpr uint32_t DECIM_LESSER_T12  = 9;
static constexpr uint32_t DECIM_LESSER_T3   = 45;

// Time budget (spec §Time budget rev4):
static constexpr double DEFAULT_BUDGET_MS_PER_SAMPLE = 3.0;
static constexpr double FOCUSED_TARGET_BUDGET_MS     = 2.0;

// ChrIns + WCM offsets (spec + research/phase3-offsets-codex.md):
namespace Off {
    // Layout::OFF_PLAYER_ARRAY in v5f confirms slot 0 is PlayerIns at WCM+0x10EF8.
    // Used here only for Tier 1 player_chr_ins_abs.
    constexpr int WCM_PLAYER_ARRAY = 0x10EF8;
    constexpr int PLAYER_ARRAY_LEN = 4;

    // Enemy roster (spec §roster quarantine — PROVISIONAL until 7 checks):
    constexpr int WCM_ROSTER_BEGIN = 0x1F1B8;   // ChrInsByUpdatePrioBegin
    constexpr int WCM_ROSTER_END   = 0x1F1C0;   // ChrInsByUpdatePrioEnd

    // ChrIns generic offsets (capture as RAW field_at_0xNN; do NOT interpret):
    constexpr int CHR_INS_HANDLE             = 0x008;
    constexpr int CHR_INS_VTABLE_PTR         = 0x000;  // v6.2: vtable for PlayerIns* vs ChrIns* discriminator
    constexpr int CHR_INS_FIELD_38           = 0x038;
    constexpr int CHR_INS_FIELD_60           = 0x060;
    constexpr int CHR_INS_FIELD_64           = 0x064;
    constexpr int CHR_INS_FIELD_68           = 0x068;
    constexpr int CHR_INS_FIELD_6C           = 0x06C;
    constexpr int CHR_INS_FIELD_80           = 0x080;
    constexpr int CHR_INS_FIELD_1E8          = 0x1E8;
    constexpr int CHR_INS_MODULE_BAG_PTR     = 0x190;  // → ChrModuleBag*
    constexpr int CHR_INS_AI_STRUCT_BASE     = 0x580;  // → ai bag → +0xC0 → struct
    constexpr int CHR_INS_TARGET_HANDLE      = 0x6A0;  // legacy lock-on (probe v6.1.1)
    // v6.2 lock-on candidates (research 006):
    constexpr int PLAYER_INS_LOCK_HANDLE_NEW = 0x6B0;  // Erd-Tools / vswarte: locked_on_enemy FieldInsHandle
    constexpr int PLAYER_INS_LOCK_AREA_NEW   = 0x6B4;  // Erd-Tools: TargetArea (4 bytes after handle)

    // PlayerIns position (Tier 1 video correlation; v5f confirmed):
    constexpr int PLAYER_INS_POS_X           = 0x6C0;  // legacy world-pos (probe v6.1.1)
    constexpr int PLAYER_INS_POS_Y           = 0x6C4;
    constexpr int PLAYER_INS_POS_Z           = 0x6C8;

    // ChrModuleBag → TimeAct module:
    constexpr int MODULE_BAG_TIME_ACT_PTR    = 0x18;
    // v6.2 module-bag candidates (research 006):
    constexpr int MODULE_BAG_PHYS_PTR        = 0x68;  // → CSChrPhysicsModule (TGA + TarnishedTool + vswarte)
    constexpr int MODULE_BAG_ACTION_REQ_PTR  = 0x80;  // → ActionRequest module (Erd-Tools enemy anim source)

    // TimeAct fields (decoded for Tier 2 + emphasized capture range):
    constexpr int TIME_ACT_TIME_CAND_0       = 0x20;
    constexpr int TIME_ACT_TIME_CAND_1       = 0x24;
    constexpr int TIME_ACT_TIME_CAND_2       = 0x28;
    constexpr int TIME_ACT_TIME_CAND_3       = 0x2C;
    constexpr int TIME_ACT_FOCUS_BEGIN       = 0xC0;   // emphasized region begin
    constexpr int TIME_ACT_FOCUS_END         = 0xE0;   // emphasized region end
    constexpr int TIME_ACT_ANIM_ID           = 0xD0;   // legacy enemy anim_id (probe v6.1.1) — reads 0 for enemies
    // v6.2 enemy anim_id candidates (research 006):
    constexpr int TIME_ACT_READ_IDX          = 0xC4;   // vswarte: u32 index into anim_queue
    constexpr int TIME_ACT_ANIM_QUEUE_BEGIN  = 0x20;   // vswarte: 10×16B circular buffer
    constexpr int TIME_ACT_ANIM_QUEUE_STRIDE = 0x10;   // CSChrTimeActModuleAnim entry size
    constexpr int TIME_ACT_ANIM_QUEUE_LEN    = 10;     // entries in queue
    constexpr int ACTION_REQ_ANIM_ID         = 0x90;   // Erd-Tools: enemy anim_id offset within ActionRequest module

    // v6.2 physics module (world pos leaf):
    constexpr int PHYS_MODULE_POS_X          = 0x70;
    constexpr int PHYS_MODULE_POS_Y          = 0x74;
    constexpr int PHYS_MODULE_POS_Z          = 0x78;

    // ai bag walk (chrIns+0x580 → +0xC0):
    constexpr int AI_BAG_AI_STRUCT_PTR       = 0xC0;
    constexpr int AI_STRUCT_REGION_BEGIN     = 0xE000;
    constexpr int AI_STRUCT_REGION_END       = 0xF000;

    // Phase 4.0 Gate 0.B finding: boss target-of-attention field.
    //   ChrIns +0x580 (AIBag*) → +0xC0 (AIStruct*) → +0xC988 (FieldInsHandle u64)
    // Sentinel value when no current target: 0xFFFFFFFFFFFFFFFF.
    // Validated: 5,521 samples in v7.3 capture, 63.6% match vs player_handle,
    // zero false positives across all alternative AI-struct slots scanned.
    // TarnishedTool prior art names TargetingSystem @ +0xC480; 0xC988 sits
    // at +0x508 into that sub-struct, plausibly `currentTarget`.
    constexpr int AI_STRUCT_TARGET_HANDLE    = 0xC988;
    constexpr uint64_t AI_STRUCT_TARGET_SENTINEL = 0xFFFFFFFFFFFFFFFFull;

    // CSFeManImp::bossHpBars[3] starts at +0x5BF0; per-slot 0x20; handle at +0x8.
    constexpr int CS_FE_MAN_BOSS_BARS_BASE   = 0x5BF0;
    constexpr int CS_FE_MAN_BOSS_BAR_STRIDE  = 0x20;
    constexpr int CS_FE_MAN_BOSS_BAR_HANDLE  = 0x08;
    constexpr int CS_FE_MAN_BOSS_BAR_COUNT   = 3;
}

// ===========================================================================
// Globals (atomics, lifetime, paths)
// ===========================================================================

static std::atomic<bool> g_running{false};         // process-lifetime running flag
static std::atomic<bool> g_armed{false};           // F11 toggle: detour samples
static std::atomic<bool> g_initOk{false};          // worker reached steady state
static std::atomic<uint64_t> g_tickCount{0};       // detour invocations (frames)
static std::atomic<uint64_t> g_sampleSeq{0};       // samples actually emitted

// Drop counters (atomically incremented; logged at session end):
static std::atomic<uint64_t> g_dropNoBuffer{0};       // producer couldn't get a buffer
static std::atomic<uint64_t> g_dropBudgetSkip{0};     // budget exceeded mid-sample
static std::atomic<uint64_t> g_dropProducerEmerg{0};  // producer-side emergency drop
static std::atomic<uint64_t> g_truncatedSamples{0};   // sample truncated to 256 KB

// Adaptive sampling mode flags (worker writes; producer reads):
//   0 = full rates, 1 = top T3 5 Hz, 2 = top cap 4, 3 = top T3 2 Hz
static std::atomic<int> g_adaptiveStep{0};

// Session start (steady_clock ms; written by worker once before hook installs).
static int64_t g_sessionStartMs = 0;

// Producer-side emergency: free_pool < 4 for >= 200ms ⇒ drop broad sweep next sample.
static std::atomic<bool> g_emergencyDropActive{false};
static std::atomic<int64_t> g_lowFreePoolSinceMs{0};  // 0 = not in low state

// Paths derived at init from the DLL location:
static char g_dllDir[MAX_PATH] = {0};
static char g_bootLogPath[MAX_PATH] = {0};
static char g_iniPath[MAX_PATH] = {0};

// ===========================================================================
// Boot log + diagnostics
// ===========================================================================
//
// All pre-config-load messages go to <DLL_DIR>/parry-tell-probe.boot.log.
// This is the only file we can rely on existing before the config tells us
// where to put session logs. Keep it small and append-only.

static FILE* g_bootLog = nullptr;
static CRITICAL_SECTION g_bootLogLock;
static std::atomic<bool> g_bootLogReady{false};

static void DebugBanner(const char* msg) {
    char buf[1024];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[parry-tell-probe " PROBE_VERSION_STR "] %s\n", msg);
    OutputDebugStringA(buf);
}

static void DebugFmt(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    char out[1100];
    _snprintf_s(out, sizeof(out), _TRUNCATE,
                "[parry-tell-probe " PROBE_VERSION_STR "] %s\n", buf);
    OutputDebugStringA(out);
}

static void ResolveDllPaths() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&ResolveDllPaths), &self);
    char dllPath[MAX_PATH] = {0};
    if (self && GetModuleFileNameA(self, dllPath, MAX_PATH) > 0) {
        strncpy_s(g_dllDir, dllPath, _TRUNCATE);
        char* sep = strrchr(g_dllDir, '\\');
        if (sep) *(sep + 1) = '\0';
    } else {
        // Fallback: relative (DLL might run with CWD set inconveniently;
        // boot log will land somewhere we can debug later).
        strncpy_s(g_dllDir, ".\\", _TRUNCATE);
    }
    _snprintf_s(g_bootLogPath, MAX_PATH, _TRUNCATE,
                "%sparry-tell-probe.boot.log", g_dllDir);
    _snprintf_s(g_iniPath, MAX_PATH, _TRUNCATE,
                "%sparry-tell-probe.ini", g_dllDir);
}

static void BootLogOpen() {
    InitializeCriticalSection(&g_bootLogLock);
    fopen_s(&g_bootLog, g_bootLogPath, "a");
    if (g_bootLog) {
        fprintf(g_bootLog, "# === parry-tell-probe " PROBE_VERSION_STR
                " boot %s %s ===\n", __DATE__, __TIME__);
        fflush(g_bootLog);
        g_bootLogReady.store(true);
    }
}

static void BootLog(const char* fmt, ...) {
    if (!g_bootLogReady.load()) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (!TryEnterCriticalSection(&g_bootLogLock)) return;
    if (g_bootLog) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        fprintf(g_bootLog, "%lld %s\n", ms, buf);
        fflush(g_bootLog);
    }
    LeaveCriticalSection(&g_bootLogLock);
}

static void BootLogClose() {
    if (!g_bootLogReady.load()) return;
    EnterCriticalSection(&g_bootLogLock);
    if (g_bootLog) { fflush(g_bootLog); fclose(g_bootLog); g_bootLog = nullptr; }
    LeaveCriticalSection(&g_bootLogLock);
    g_bootLogReady.store(false);
}

// ===========================================================================
// Config (INI parser, hand-rolled — no third-party JSON/INI dep)
// ===========================================================================

struct Config {
    // [output]
    char log_dir[MAX_PATH]      = {0};
    char session_name[128]      = {0};

    // [capture]
    CaptureMode mode            = MODE_SMOKE;     // REQUIRED; fail closed if invalid
    int sample_rate_hz          = 10;             // top-tier broad-sweep target Hz
    int max_enemies_tracked     = MAX_TRACKED_ENEMIES;
    int top_tier_enemies        = MAX_TRACKED_TOP;
    int lesser_tier_rate_hz     = 2;
    double budget_ms_per_sample = DEFAULT_BUDGET_MS_PER_SAMPLE;

    // [diagnostics]
    bool verbose                = true;

    // [prediction] — Phase 4.1 commit 4. Decoupled from capture/data path.
    //
    // data_collection_mode: when TRUE, install the existing detour hook and
    //   write captures as v7.x did. When FALSE, skip hook install entirely
    //   and run prediction only (production v0.1.0 default).
    // prediction_enabled: master switch for the predictor poll. Default ON.
    // prediction_poll_interval_ms: how often the worker reads game state and
    //   evaluates predictions. Default 4ms (~250 Hz). Range [1, 33].
    // audio_cue_lead_ms: cue fires when lead_time <= this value. 0 = at
    //   window-open, positive = before, negative = after. Range [-200, 500].
    // target_filter_enabled: when TRUE, suppress cues unless boss is
    //   targeting local player. Default FALSE for first-commit smoke
    //   validation (per Josh's call on the open-question loop); flip to
    //   TRUE via INI once we've verified lead-time math is correct without
    //   the target filter as a confounder.
    // prediction_decision_log_enabled: write .predictions.jsonl. Default
    //   TRUE when data_collection_mode=true OR mode!=smoke (i.e. for any
    //   diagnostic-grade session).
    bool data_collection_mode              = false;
    bool prediction_enabled                = true;
    int  prediction_poll_interval_ms       = 4;
    int  audio_cue_lead_ms                 = 0;
    bool target_filter_enabled             = false;
    bool prediction_decision_log_enabled   = true;

    // [hotkeys] arm_toggle is hard-coded F11 in this build (matches v5f).
    // Spec config field is parsed but only F11 is honored.
};

static Config g_cfg;
static std::atomic<bool> g_cfgReady{false};

// Trim ASCII whitespace + CR/LF in place.
static void TrimInPlace(char* s) {
    if (!s) return;
    char* p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { s[n - 1] = 0; --n; }
        else break;
    }
}

// Strip inline comments starting at unquoted ';'.
static void StripComment(char* s) {
    if (!s) return;
    char* p = s;
    while (*p) {
        if (*p == ';') { *p = 0; break; }
        ++p;
    }
}

static bool ParseBool(const char* v, bool* out) {
    if (!v || !*v) return false;
    if (_stricmp(v, "true") == 0 || _stricmp(v, "yes") == 0 ||
        _stricmp(v, "on") == 0   || _stricmp(v, "1") == 0)   { *out = true;  return true; }
    if (_stricmp(v, "false") == 0 || _stricmp(v, "no") == 0 ||
        _stricmp(v, "off") == 0   || _stricmp(v, "0") == 0)  { *out = false; return true; }
    return false;
}

static bool ParseMode(const char* v, CaptureMode* out) {
    if (!v) return false;
    if (_stricmp(v, "smoke") == 0)         { *out = MODE_SMOKE;         return true; }
    if (_stricmp(v, "qualification") == 0) { *out = MODE_QUALIFICATION; return true; }
    if (_stricmp(v, "discovery") == 0)     { *out = MODE_DISCOVERY;     return true; }
    return false;
}

// Returns true on success. Logs to boot log on failure with reason.
static bool LoadConfig(const char* path, Config* cfg) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) {
        BootLog("config_fail: cannot open %s (errno=%d)", path, errno);
        return false;
    }

    bool sawMode = false;       // mode is the only REQUIRED key (per spec)
    char section[64] = {0};
    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        StripComment(line);
        TrimInPlace(line);
        if (!*line) continue;

        // Section header: [name]
        if (line[0] == '[') {
            size_t n = strlen(line);
            if (n >= 2 && line[n - 1] == ']') {
                line[n - 1] = 0;
                strncpy_s(section, line + 1, _TRUNCATE);
                TrimInPlace(section);
            }
            continue;
        }

        // key = value
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        TrimInPlace(key);
        TrimInPlace(val);

        if (_stricmp(section, "output") == 0) {
            if (_stricmp(key, "log_dir") == 0) {
                strncpy_s(cfg->log_dir, val, _TRUNCATE);
            } else if (_stricmp(key, "session_name") == 0) {
                strncpy_s(cfg->session_name, val, _TRUNCATE);
            }
        } else if (_stricmp(section, "capture") == 0) {
            if (_stricmp(key, "mode") == 0) {
                if (!ParseMode(val, &cfg->mode)) {
                    BootLog("config_fail: invalid mode '%s' (smoke|qualification|discovery)", val);
                    fclose(f);
                    return false;
                }
                sawMode = true;
            } else if (_stricmp(key, "sample_rate_hz") == 0) {
                cfg->sample_rate_hz = atoi(val);
            } else if (_stricmp(key, "max_enemies_tracked") == 0) {
                cfg->max_enemies_tracked = atoi(val);
            } else if (_stricmp(key, "top_tier_enemies") == 0) {
                cfg->top_tier_enemies = atoi(val);
            } else if (_stricmp(key, "lesser_tier_rate_hz") == 0) {
                cfg->lesser_tier_rate_hz = atoi(val);
            } else if (_stricmp(key, "budget_ms_per_sample") == 0) {
                cfg->budget_ms_per_sample = atof(val);
            }
        } else if (_stricmp(section, "diagnostics") == 0) {
            if (_stricmp(key, "verbose") == 0) {
                bool b; if (ParseBool(val, &b)) cfg->verbose = b;
            }
        } else if (_stricmp(section, "prediction") == 0) {
            if (_stricmp(key, "data_collection_mode") == 0) {
                bool b; if (ParseBool(val, &b)) cfg->data_collection_mode = b;
            } else if (_stricmp(key, "prediction_enabled") == 0) {
                bool b; if (ParseBool(val, &b)) cfg->prediction_enabled = b;
            } else if (_stricmp(key, "prediction_poll_interval_ms") == 0) {
                cfg->prediction_poll_interval_ms = atoi(val);
            } else if (_stricmp(key, "audio_cue_lead_ms") == 0) {
                cfg->audio_cue_lead_ms = atoi(val);
            } else if (_stricmp(key, "target_filter_enabled") == 0) {
                bool b; if (ParseBool(val, &b)) cfg->target_filter_enabled = b;
            } else if (_stricmp(key, "prediction_decision_log_enabled") == 0) {
                bool b; if (ParseBool(val, &b)) cfg->prediction_decision_log_enabled = b;
            }
        }
        // Unknown section / key: ignored with no warning (spec: "Unknown keys
        // ignored with warning"). We log unknown sections to boot log if verbose.
    }

    fclose(f);

    if (!sawMode) {
        BootLog("config_fail: [capture] mode is required and was not present");
        return false;
    }

    // Validate ranges.
    if (cfg->sample_rate_hz < 1 || cfg->sample_rate_hz > 90) {
        BootLog("config_fail: sample_rate_hz=%d out of range [1, 90]", cfg->sample_rate_hz);
        return false;
    }
    if (cfg->lesser_tier_rate_hz < 1 || cfg->lesser_tier_rate_hz > 30) {
        BootLog("config_fail: lesser_tier_rate_hz=%d out of range [1, 30]", cfg->lesser_tier_rate_hz);
        return false;
    }
    if (cfg->top_tier_enemies < 1 || cfg->top_tier_enemies > MAX_TRACKED_TOP) {
        BootLog("config_fail: top_tier_enemies=%d out of range [1, %d]",
                cfg->top_tier_enemies, MAX_TRACKED_TOP);
        return false;
    }
    if (cfg->max_enemies_tracked < cfg->top_tier_enemies ||
        cfg->max_enemies_tracked > MAX_TRACKED_ENEMIES) {
        BootLog("config_fail: max_enemies_tracked=%d out of range [%d, %d]",
                cfg->max_enemies_tracked, cfg->top_tier_enemies, MAX_TRACKED_ENEMIES);
        return false;
    }
    if (cfg->budget_ms_per_sample < 0.5 || cfg->budget_ms_per_sample > 16.0) {
        BootLog("config_fail: budget_ms_per_sample=%.3f out of range [0.5, 16]",
                cfg->budget_ms_per_sample);
        return false;
    }
    if (cfg->log_dir[0] == 0) {
        BootLog("config_fail: [output] log_dir is required");
        return false;
    }
    if (cfg->session_name[0] == 0) {
        strncpy_s(cfg->session_name, "session", _TRUNCATE);
    }

    // [prediction] range checks. Soft-clamp instead of fail-closed so a
    // typo doesn't kill the whole process — we'd rather log and continue
    // with a clamped value than refuse to start.
    if (cfg->prediction_poll_interval_ms < 1 || cfg->prediction_poll_interval_ms > 33) {
        BootLog("config_warn: prediction_poll_interval_ms=%d out of range [1,33]; clamping",
                cfg->prediction_poll_interval_ms);
        if (cfg->prediction_poll_interval_ms < 1) cfg->prediction_poll_interval_ms = 1;
        if (cfg->prediction_poll_interval_ms > 33) cfg->prediction_poll_interval_ms = 33;
    }
    if (cfg->audio_cue_lead_ms < -200 || cfg->audio_cue_lead_ms > 500) {
        BootLog("config_warn: audio_cue_lead_ms=%d out of range [-200,500]; clamping",
                cfg->audio_cue_lead_ms);
        if (cfg->audio_cue_lead_ms < -200) cfg->audio_cue_lead_ms = -200;
        if (cfg->audio_cue_lead_ms > 500) cfg->audio_cue_lead_ms = 500;
    }

    return true;
}

// ===========================================================================
// Parry-window database (loaded once at worker init from parry_data.bin)
// ===========================================================================
//
// Binary format (little-endian; built by tools/build_parry_db.py):
//
//   HEADER:
//     [magic 4 bytes: 'P''T''P''D']
//     [version u16: 1]
//     [meta_len u16: length of meta_json in bytes]
//     [meta_json: utf8 bytes, length=meta_len]
//
//   BODY:
//     [char_count u32]
//     per char:
//       [cid u32]              # e.g. 4310 for c4310
//       [anim_count u32]
//       per anim:
//         [anim_id u32]        # full XXXYYYYYY decimal-concat OR short YYYYYY
//         [window_count u16]
//         per window:
//           [open_s float32]
//           [close_s float32]
//
// The build tool drops ambiguous short-form keys at build time (i.e. when
// two distinct source anims would map to the same short YYYYYY u32). The
// runtime predictor therefore reads raw anim_id from TimeAct +0xD0 and
// looks it up directly — no per-character encoding detection needed at
// runtime, by construction.
//
// Lookup key in g_parryDb: packed u64 = (resolved_cid << 32) | anim_id.
// Resolved cid is the result of exact-first + single-step family fallback
// applied at lookup time (not at build time).
//
// Lifetime: loaded once during worker init. Free on process exit only.
// No mutation after load. Predictor reads concurrently without locking.
// Failure to load is fail-closed for cues (g_parryDbLoaded stays false);
// process keeps running so data-collection mode is unaffected.

struct ParryWindow {
    float open_s;
    float close_s;
};

struct ParryDbMeta {
    uint16_t version = 0;
    char meta_json[8192] = {0};      // null-terminated; truncated if larger
    uint32_t char_count = 0;
    uint32_t total_anims = 0;
    uint32_t total_windows = 0;
};

static std::unordered_map<uint64_t, std::vector<ParryWindow>> g_parryDb;
static ParryDbMeta g_parryDbMeta;
static std::atomic<bool> g_parryDbLoaded{false};

static constexpr uint32_t PARRY_DB_MAGIC =
    static_cast<uint32_t>('P') |
    (static_cast<uint32_t>('T') << 8) |
    (static_cast<uint32_t>('P') << 16) |
    (static_cast<uint32_t>('D') << 24);
static constexpr uint16_t PARRY_DB_VERSION = 1;

// Pack (cid, anim_id) into the unordered_map key.
static inline uint64_t PackParryKey(uint32_t cid, uint32_t anim_id) {
    return (static_cast<uint64_t>(cid) << 32) | static_cast<uint64_t>(anim_id);
}

// Bounded byte reader used by the loader. Returns false on overflow.
// On success, advances *pos by len.
static bool ReadBytes(const uint8_t* buf, size_t buf_len, size_t* pos,
                      void* dst, size_t len) {
    if (!buf || !pos) return false;
    if (*pos > buf_len) return false;
    if (len > buf_len - *pos) return false;
    memcpy(dst, buf + *pos, len);
    *pos += len;
    return true;
}

// Load parry_data.bin (sibling to the DLL). On success, atomically
// publishes the parsed table via g_parryDb / g_parryDbMeta and sets
// g_parryDbLoaded=true. On failure, logs the reason once and leaves the
// previously-loaded state intact if any (fail-closed for cues — caller
// observes the prior g_parryDbLoaded value).
//
// The function is single-shot in v1 (called once from worker init), but
// the parse-into-locals-then-publish pattern keeps it correct if a
// future reload feature reuses it.
//
// Returns true on success, false on any structural failure.
static bool LoadParryDb() {
    char path[MAX_PATH] = {0};
    _snprintf_s(path, MAX_PATH, _TRUNCATE, "%sparry_data.bin", g_dllDir);

    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        BootLog("parry_db_fail: cannot open %s (errno=%d)", path, errno);
        return false;
    }

    // Slurp entire file (small — typical ~40 KB; cap defensively at 16 MB).
    fseek(f, 0, SEEK_END);
    long fsize_signed = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize_signed <= 0 || fsize_signed > (16 * 1024 * 1024)) {
        BootLog("parry_db_fail: bad size %ld for %s", fsize_signed, path);
        fclose(f);
        return false;
    }
    size_t fsize = static_cast<size_t>(fsize_signed);

    std::vector<uint8_t> buf(fsize);
    size_t got = fread(buf.data(), 1, fsize, f);
    fclose(f);
    if (got != fsize) {
        BootLog("parry_db_fail: short read %zu/%zu from %s", got, fsize, path);
        return false;
    }

    size_t pos = 0;

    // Parse into LOCAL temporaries; only publish to globals on full success.
    // This keeps the prior g_parryDb intact if a future reload fails midway.
    std::unordered_map<uint64_t, std::vector<ParryWindow>> tmp_db;
    ParryDbMeta tmp_meta;

    // Header: magic[4] version[u16] meta_len[u16]
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t meta_len = 0;
    if (!ReadBytes(buf.data(), fsize, &pos, &magic, 4) ||
        !ReadBytes(buf.data(), fsize, &pos, &version, 2) ||
        !ReadBytes(buf.data(), fsize, &pos, &meta_len, 2)) {
        BootLog("parry_db_fail: header truncated (fsize=%zu)", fsize);
        return false;
    }
    if (magic != PARRY_DB_MAGIC) {
        BootLog("parry_db_fail: bad magic 0x%08X (expected 'PTPD')", magic);
        return false;
    }
    if (version != PARRY_DB_VERSION) {
        BootLog("parry_db_fail: version %u != %u", (unsigned)version,
                (unsigned)PARRY_DB_VERSION);
        return false;
    }

    // Meta JSON (informational; copied for boot log + future provenance).
    if (meta_len > 0) {
        if (meta_len > sizeof(tmp_meta.meta_json) - 1) {
            BootLog("parry_db_fail: meta_len %u exceeds buffer %zu",
                    (unsigned)meta_len, sizeof(tmp_meta.meta_json) - 1);
            return false;
        }
        if (!ReadBytes(buf.data(), fsize, &pos,
                       tmp_meta.meta_json, meta_len)) {
            BootLog("parry_db_fail: meta truncated");
            return false;
        }
        tmp_meta.meta_json[meta_len] = '\0';
    }
    tmp_meta.version = version;

    // Body: char_count + per-char + per-anim + per-window.
    uint32_t char_count = 0;
    if (!ReadBytes(buf.data(), fsize, &pos, &char_count, 4)) {
        BootLog("parry_db_fail: char_count missing");
        return false;
    }
    if (char_count > 10000) {
        BootLog("parry_db_fail: char_count %u absurdly large", char_count);
        return false;
    }

    // Reserve generously so we don't rehash mid-load. ~2000 anims expected.
    tmp_db.reserve(4096);

    uint32_t total_anims = 0;
    uint32_t total_windows = 0;

    for (uint32_t ci = 0; ci < char_count; ++ci) {
        uint32_t cid = 0;
        uint32_t anim_count = 0;
        if (!ReadBytes(buf.data(), fsize, &pos, &cid, 4) ||
            !ReadBytes(buf.data(), fsize, &pos, &anim_count, 4)) {
            BootLog("parry_db_fail: char[%u] header truncated", ci);
            return false;
        }
        if (anim_count > 100000) {
            BootLog("parry_db_fail: char c%u anim_count %u absurdly large",
                    cid, anim_count);
            return false;
        }

        for (uint32_t ai = 0; ai < anim_count; ++ai) {
            uint32_t anim_id = 0;
            uint16_t window_count = 0;
            if (!ReadBytes(buf.data(), fsize, &pos, &anim_id, 4) ||
                !ReadBytes(buf.data(), fsize, &pos, &window_count, 2)) {
                BootLog("parry_db_fail: c%u anim[%u] header truncated",
                        cid, ai);
                return false;
            }
            if (window_count == 0) {
                // Defensive: build tool should not emit empty windows, but
                // if it does, just skip the entry. Don't fail the whole load.
                continue;
            }

            std::vector<ParryWindow> windows;
            windows.reserve(window_count);
            for (uint16_t wi = 0; wi < window_count; ++wi) {
                ParryWindow w;
                if (!ReadBytes(buf.data(), fsize, &pos, &w.open_s, 4) ||
                    !ReadBytes(buf.data(), fsize, &pos, &w.close_s, 4)) {
                    BootLog("parry_db_fail: c%u anim %u window[%u] truncated",
                            cid, anim_id, (unsigned)wi);
                    return false;
                }
                windows.push_back(w);
            }

            uint64_t key = PackParryKey(cid, anim_id);
            // Build tool guarantees no duplicate (cid, anim_id) keys; if
            // we somehow see one, log loudly so it surfaces at boot.
            auto inserted = tmp_db.emplace(key, std::move(windows));
            if (!inserted.second) {
                BootLog("parry_db_warn: duplicate key c%u anim_id=%u "
                        "(build tool bug?); keeping first",
                        cid, anim_id);
            } else {
                total_anims += 1;
                total_windows += window_count;
            }
        }
    }

    if (pos != fsize) {
        BootLog("parry_db_warn: %zu trailing bytes after body (fsize=%zu)",
                fsize - pos, fsize);
        // Non-fatal: the lookup table is fully populated, the trailer is
        // just an unrecognized appendix (probably a future format extension).
    }

    tmp_meta.char_count = char_count;
    tmp_meta.total_anims = total_anims;
    tmp_meta.total_windows = total_windows;

    // PUBLISH: swap the parsed locals into the globals, then release-store
    // the loaded flag. Readers that load the flag with acquire ordering
    // will see fully-populated globals or nothing. The g_parryDb swap is
    // O(1) (just swaps internal pointers) so the window between the two
    // operations is minimal even though we don't claim an explicit lock.
    g_parryDb.swap(tmp_db);
    g_parryDbMeta = tmp_meta;
    g_parryDbLoaded.store(true, std::memory_order_release);

    BootLog("parry_db_ok: %s version=%u chars=%u anims=%u windows=%u meta_len=%u",
            path, (unsigned)version, char_count, total_anims, total_windows,
            (unsigned)meta_len);
    return true;
}

// Lookup helper. resolved_cid is the exact-or-family-fallback cid that the
// predictor computed for this boss. Returns nullptr if no row exists.
// Caller MUST NOT mutate the returned vector — it's part of the read-only
// table.
static const std::vector<ParryWindow>* LookupParryWindows(uint32_t resolved_cid,
                                                          uint32_t anim_id) {
    if (!g_parryDbLoaded.load(std::memory_order_acquire)) return nullptr;
    uint64_t key = PackParryKey(resolved_cid, anim_id);
    auto it = g_parryDb.find(key);
    if (it == g_parryDb.end()) return nullptr;
    return &it->second;
}

// Resolve a raw c-id (read from ChrIns +0x064) to a lookup-ready resolved
// c-id using exact-first + single-step family fallback. Returns 0 if the
// raw cid is junk (< 1000) or if neither exact nor family-rounded cid is
// present in the DB.
//
// Family fallback: round down to nearest 10 (e.g. c4311 -> c4310). Only
// applied once; c9990 absent stays absent (no recursive fallback).
static uint32_t ResolveCidForLookup(uint32_t raw_cid, uint32_t anim_id) {
    if (raw_cid < 1000) return 0;
    if (!g_parryDbLoaded.load(std::memory_order_acquire)) return 0;

    // Exact match wins.
    if (g_parryDb.find(PackParryKey(raw_cid, anim_id)) != g_parryDb.end()) {
        return raw_cid;
    }

    // Family fallback: round down to nearest 10.
    uint32_t family_cid = (raw_cid / 10) * 10;
    if (family_cid == raw_cid) return 0;  // already aligned; no fallback path
    if (g_parryDb.find(PackParryKey(family_cid, anim_id)) != g_parryDb.end()) {
        return family_cid;
    }
    return 0;
}

// ===========================================================================
// Predictor decision engine (latch state machine + lead-time math)
// ===========================================================================
//
// Phase 4.1 commit 3. Pure C++ — no game-memory reads, no file I/O. The
// caller (predictor worker tick, landing in commit 4) gathers boss state
// from SEH-wrapped reads, then hands the numbers to EvaluatePredictionTick.
// The engine produces a PredictionDecision per (boss, window) eval and
// updates per-boss latch state. The decision-log writer (further down) is
// the only side-effect path.
//
// Latch identity is (boss_handle, anim_instance_seq, window_ordinal). The
// anim_instance_seq counter increments per boss whenever ANY of these
// trigger a "this is a new instance of the animation" condition:
//
//   - resolved c-id changed (boss got recategorized; rare but possible)
//   - anim_id changed (confirmed after 2-poll debounce against flicker)
//   - anim_time rewound by more than 50 ms (same-anim re-entry case, e.g.
//     Margit's bonk-with-staff played twice in a row)
//   - boss handle disappeared from boss bars for > 250 ms grace and came back
//
// On any instance reset, consumed_windows bitmap is cleared so the new
// instance can cue fresh.
//
// Threading: this engine runs entirely on the predictor worker thread.
// Per-boss state lives in g_bossLatch (single-thread access from worker).
// No locks needed because no other thread reads it. The parry DB IS
// shared (predictor reads, init writes), and that synchronization is
// already handled by g_parryDbLoaded acquire/release.

static constexpr int BOSS_DISAPPEAR_GRACE_MS = 250;
static constexpr float ANIM_REWIND_TOLERANCE_S = 0.050f;
static constexpr int ANIM_ID_DEBOUNCE_POLLS = 2;
static constexpr int MAX_WINDOWS_PER_ANIM_LATCH = 32;   // uint32_t bitmap

// Per-boss latch state. Keyed by boss FieldInsHandle in g_bossLatch.
struct BossPredictState {
    uint32_t resolved_cid       = 0;
    uint32_t prev_anim_id       = 0;      // last anim_id we believed was active
    float    prev_anim_time_s   = 0.0f;

    // Debounce: when a new anim_id first appears, we tentatively note it
    // here. It becomes prev_anim_id once observed for N consecutive polls.
    uint32_t pending_anim_id    = 0;
    uint8_t  pending_count      = 0;

    // Monotonic instance counter for THIS boss handle. Bumps on each
    // confirmed instance reset; never decreases. Combined with the boss
    // handle + window ordinal, uniquely identifies a parry window
    // opportunity for latch purposes.
    uint64_t anim_instance_seq  = 0;

    // Consumed bitmap for the CURRENT (anim_instance_seq) instance.
    // Bit i = "we have already emitted a cue for windows[i] this instance."
    // Cleared on instance reset.
    uint32_t consumed_windows   = 0;

    int64_t  last_seen_ms       = 0;      // session-relative ms; 0 = never
    bool     ever_seen          = false;
};

// Keyed by boss FieldInsHandle (u64). Single-thread access from predictor.
static std::unordered_map<uint64_t, BossPredictState> g_bossLatch;

// Result of one (boss, window) evaluation. The engine produces a sequence
// of these per tick and the decision-log writer serializes them.
//
// `action` enum semantics:
//   ACTION_NO_DB      — DB not loaded (predictor inert)
//   ACTION_NO_KEY     — (cid, anim_id) lookup returned no row
//   ACTION_BEFORE_LEAD — lead_time still bigger than configured threshold
//   ACTION_FIRE       — cue should fire (predictor would call PlaySoundW)
//   ACTION_LATCHED    — already fired for this (instance, window)
//   ACTION_LATE_TARGET_SWITCH — target switched to player after threshold
//                              crossed; fire NOW (per plan target-switch rule)
//   ACTION_LATE_INSIDE_WINDOW — anim_time is inside [open, close]; cue
//                              window is open; fire if not latched
//   ACTION_SUPPRESSED_TARGET — target_match=false and target_filter on
//   ACTION_SUPPRESSED_POST_WINDOW — anim_time past window close
//   ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW —
//                              audio_cue_lead_ms < 0 and target fire-time
//                              > window_close_s (window too short for the
//                              configured delay)
enum PredictionAction : uint8_t {
    ACTION_NO_DB                = 0,
    ACTION_NO_KEY               = 1,
    ACTION_BEFORE_LEAD          = 2,
    ACTION_FIRE                 = 3,
    ACTION_LATCHED              = 4,
    ACTION_LATE_TARGET_SWITCH   = 5,
    ACTION_LATE_INSIDE_WINDOW   = 6,
    ACTION_SUPPRESSED_TARGET    = 7,
    ACTION_SUPPRESSED_POST_WINDOW       = 8,
    ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW = 9,
};

static const char* PredictionActionName(PredictionAction a) {
    switch (a) {
        case ACTION_NO_DB:                              return "no_db";
        case ACTION_NO_KEY:                             return "no_key";
        case ACTION_BEFORE_LEAD:                        return "before_lead";
        case ACTION_FIRE:                               return "fire";
        case ACTION_LATCHED:                            return "latched";
        case ACTION_LATE_TARGET_SWITCH:                 return "fire_late_target_switch";
        case ACTION_LATE_INSIDE_WINDOW:                 return "fire_late_inside_window";
        case ACTION_SUPPRESSED_TARGET:                  return "suppressed_target";
        case ACTION_SUPPRESSED_POST_WINDOW:             return "suppressed_post_window";
        case ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW:
                                                        return "suppressed_negative_lead_exceeds_window";
    }
    return "unknown";
}

struct PredictionDecision {
    int64_t  ts_ms_rel              = 0;     // session-relative poll time
    uint64_t boss_handle            = 0;
    uintptr_t boss_chr_ins          = 0;
    uint32_t raw_cid                = 0;
    uint32_t resolved_cid           = 0;     // 0 if no DB row
    uint32_t anim_id                = 0;
    float    anim_time_s            = 0.0f;
    uint16_t window_ordinal         = 0xFFFF; // 0xFFFF if no specific window
    float    window_open_s          = 0.0f;
    float    window_close_s         = 0.0f;
    float    lead_time_ms           = 0.0f;
    int      configured_lead_ms     = 0;
    bool     target_filter_enabled  = false;
    bool     target_match           = false;
    uint64_t anim_instance_seq      = 0;
    PredictionAction action         = ACTION_NO_KEY;
};

// Configuration that the engine reads each tick. Populated from g_cfg by
// the worker before calling EvaluatePredictionTick. Kept as a small POD
// so the engine has no implicit dependency on Config struct layout
// (decouples engine from INI surface for unit-testing).
struct PredictionConfig {
    int  audio_cue_lead_ms      = 0;       // [-200, 500]
    bool target_filter_enabled  = false;
};

// Per-boss tick input. Caller assembles this from one boss-bar entry.
struct BossTickInput {
    uint64_t  boss_handle    = 0;
    uintptr_t boss_chr_ins   = 0;          // for logging only
    uint32_t  raw_cid        = 0;          // ChrIns +0x064
    uint32_t  anim_id        = 0;          // TimeAct +0xD0
    float     anim_time_s    = 0.0f;       // TimeAct +0x24
    bool      target_match   = false;      // boss targeting player?
    bool      target_known   = false;      // false if target read failed
};

// Forward decl — decision sink defined further down.
static void WritePredictionDecision(const PredictionDecision& d);

// Drop state entries for bosses we haven't seen in a LONG time
// (10 seconds). Keeps the map small. Called from the worker periodically.
static void GcBossLatch(int64_t now_ms_rel) {
    constexpr int64_t GC_AGE_MS = 10000;
    for (auto it = g_bossLatch.begin(); it != g_bossLatch.end(); ) {
        if (it->second.ever_seen &&
            (now_ms_rel - it->second.last_seen_ms) > GC_AGE_MS) {
            it = g_bossLatch.erase(it);
        } else {
            ++it;
        }
    }
}

// Core latch transition: given the prior state and current tick observation,
// decide whether this is a NEW animation instance (and if so, increment the
// seq and clear the consumed-windows bitmap). Returns true if an instance
// reset happened.
static bool BumpInstanceSeqIfNeeded(BossPredictState* st,
                                    uint32_t resolved_cid,
                                    uint32_t anim_id,
                                    float anim_time_s,
                                    int64_t now_ms_rel) {
    if (!st->ever_seen) {
        st->resolved_cid = resolved_cid;
        st->prev_anim_id = anim_id;
        st->prev_anim_time_s = anim_time_s;
        st->anim_instance_seq = 1;
        st->consumed_windows = 0;
        st->last_seen_ms = now_ms_rel;
        st->ever_seen = true;
        st->pending_anim_id = 0;
        st->pending_count = 0;
        return true;
    }

    bool reset = false;

    // (1) Disappearance > grace: previous last_seen_ms is more than
    // BOSS_DISAPPEAR_GRACE_MS old. (We never get here if the boss vanished
    // permanently — GcBossLatch reaps and the next sighting is fresh.)
    if ((now_ms_rel - st->last_seen_ms) > BOSS_DISAPPEAR_GRACE_MS) {
        reset = true;
    }

    // (2) Resolved c-id changed.
    if (resolved_cid != 0 && resolved_cid != st->resolved_cid) {
        reset = true;
    }

    // (3) anim_id changed (with debounce). If the new anim_id is stable
    // for ANIM_ID_DEBOUNCE_POLLS consecutive polls, that's a real change.
    // Otherwise it's flicker — ignore.
    if (anim_id != st->prev_anim_id) {
        if (anim_id == st->pending_anim_id) {
            st->pending_count += 1;
            if (st->pending_count >= ANIM_ID_DEBOUNCE_POLLS) {
                reset = true;
                st->pending_anim_id = 0;
                st->pending_count = 0;
            }
        } else {
            st->pending_anim_id = anim_id;
            st->pending_count = 1;
        }
    } else {
        // Same anim as before — clear any pending debounce candidate.
        st->pending_anim_id = 0;
        st->pending_count = 0;
    }

    // (4) anim_time rewind > tolerance on same anim_id. Handles
    // same-anim re-entry (e.g. boss does anim_X, finishes, does anim_X
    // again without a different anim in between).
    if (anim_id == st->prev_anim_id) {
        float dt = st->prev_anim_time_s - anim_time_s;
        if (dt > ANIM_REWIND_TOLERANCE_S) {
            reset = true;
        }
    }

    if (reset) {
        st->resolved_cid = resolved_cid;
        st->prev_anim_id = anim_id;
        st->prev_anim_time_s = anim_time_s;
        st->anim_instance_seq += 1;
        st->consumed_windows = 0;
    } else {
        // Same instance — just update progress trackers.
        st->prev_anim_time_s = anim_time_s;
        // prev_anim_id stays as the believed-active anim until debounce
        // promotes pending_anim_id; we DON'T overwrite it with this
        // tick's anim_id if it differs (that would defeat the debounce).
    }

    st->last_seen_ms = now_ms_rel;
    return reset;
}

// Evaluate one window for one boss. Returns the decision; does not write
// it. Caller writes (allowing test harnesses to capture decisions in-mem).
static PredictionDecision EvaluatePredictionWindow(
        BossPredictState* st,
        const BossTickInput& in,
        const PredictionConfig& cfg,
        const ParryWindow& window,
        uint16_t window_ordinal,
        int64_t now_ms_rel) {
    PredictionDecision d;
    d.ts_ms_rel             = now_ms_rel;
    d.boss_handle           = in.boss_handle;
    d.boss_chr_ins          = in.boss_chr_ins;
    d.raw_cid               = in.raw_cid;
    d.resolved_cid          = st->resolved_cid;
    d.anim_id               = in.anim_id;
    d.anim_time_s           = in.anim_time_s;
    d.window_ordinal        = window_ordinal;
    d.window_open_s         = window.open_s;
    d.window_close_s        = window.close_s;
    d.lead_time_ms          = (window.open_s - in.anim_time_s) * 1000.0f;
    d.configured_lead_ms    = cfg.audio_cue_lead_ms;
    d.target_filter_enabled = cfg.target_filter_enabled;
    d.target_match          = in.target_match;
    d.anim_instance_seq     = st->anim_instance_seq;

    // Bitmap bounds: skip windows we can't track (failsafe). Log once
    // via the action so the decision log surfaces it.
    if (window_ordinal >= MAX_WINDOWS_PER_ANIM_LATCH) {
        d.action = ACTION_NO_KEY;  // sentinel — no latch available
        return d;
    }

    // NaN/Inf hygiene. A bad DB row OR a garbage SEH read could feed us
    // a non-finite float. Comparisons against NaN return false, which
    // would cause the lead-time check (`d.lead_time_ms > cfg.audio_cue_lead_ms`)
    // to falsely succeed and fire a cue at random. Reject the row.
    if (!std::isfinite(in.anim_time_s) ||
        !std::isfinite(window.open_s) ||
        !std::isfinite(window.close_s)) {
        d.action = ACTION_NO_KEY;
        return d;
    }

    const uint32_t bit = 1u << window_ordinal;
    const bool already_consumed = (st->consumed_windows & bit) != 0;

    // Past window-close: no cue, regardless of latch or filter.
    if (in.anim_time_s > window.close_s) {
        d.action = ACTION_SUPPRESSED_POST_WINDOW;
        return d;
    }

    if (already_consumed) {
        d.action = ACTION_LATCHED;
        return d;
    }

    // Target filter: if enabled and target unknown OR not us, suppress.
    //
    // TODO(phase 4.1 commit 5+): emit ACTION_LATE_TARGET_SWITCH when target
    // crosses from not-us → us AFTER the lead threshold was crossed but
    // BEFORE window_close. Requires tracking per-window "was suppressed
    // last poll" bitmap. Not needed for first-commit smoke since
    // target_filter_enabled defaults to false; add when the filter
    // validation cycle starts.
    if (cfg.target_filter_enabled) {
        if (!in.target_known || !in.target_match) {
            d.action = ACTION_SUPPRESSED_TARGET;
            return d;
        }
    }

    // Negative-lead handling: if lead is negative, the "should fire" time
    // is window_open_s + (-lead_ms / 1000). If that exceeds window_close_s,
    // the cue can never fire for this window. Plan §lead-time-math.
    if (cfg.audio_cue_lead_ms < 0) {
        float fire_time_s = window.open_s + (-cfg.audio_cue_lead_ms / 1000.0f);
        if (fire_time_s > window.close_s) {
            d.action = ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW;
            return d;
        }
        if (in.anim_time_s < fire_time_s) {
            d.action = ACTION_BEFORE_LEAD;
            return d;
        }
        // Past fire_time_s and not yet consumed: fire.
        st->consumed_windows |= bit;
        d.action = ACTION_FIRE;
        return d;
    }

    // Lead >= 0 path. Fire when (window_open_s - anim_time_s)*1000 <= lead.
    // For lead == 0, that's "anim_time has reached window_open". For
    // lead > 0, that's "anim_time is within lead_ms of window_open".
    if (d.lead_time_ms > cfg.audio_cue_lead_ms) {
        d.action = ACTION_BEFORE_LEAD;
        return d;
    }

    // Lead threshold crossed. Tag with LATE_INSIDE_WINDOW only when
    // anim_time is STRICTLY past window_open — at-or-before window_open
    // is a normal predictive fire. Equality counts as on-time (lead=0
    // semantics: "fire AT the window-open frame").
    if (in.anim_time_s > window.open_s) {
        st->consumed_windows |= bit;
        d.action = ACTION_LATE_INSIDE_WINDOW;
        return d;
    }

    st->consumed_windows |= bit;
    d.action = ACTION_FIRE;
    return d;
}

// Public entry point: evaluate all windows for one boss this tick.
// Writes decisions to the sink. Returns the number of decisions emitted.
static int EvaluatePredictionTick(const BossTickInput& in,
                                  const PredictionConfig& cfg,
                                  int64_t now_ms_rel) {
    // Resolve cid first (uses DB).
    uint32_t resolved_cid = ResolveCidForLookup(in.raw_cid, in.anim_id);

    // Get-or-create state.
    auto& st = g_bossLatch[in.boss_handle];

    // Update instance/latch state. Even when there's no DB row, we still
    // tick the latch so the next tick's transitions are correct.
    BumpInstanceSeqIfNeeded(&st, resolved_cid, in.anim_id, in.anim_time_s,
                            now_ms_rel);

    // No DB loaded → emit one "no_db" decision and bail.
    if (!g_parryDbLoaded.load(std::memory_order_acquire)) {
        PredictionDecision d;
        d.ts_ms_rel             = now_ms_rel;
        d.boss_handle           = in.boss_handle;
        d.boss_chr_ins          = in.boss_chr_ins;
        d.raw_cid               = in.raw_cid;
        d.resolved_cid          = 0;
        d.anim_id               = in.anim_id;
        d.anim_time_s           = in.anim_time_s;
        d.configured_lead_ms    = cfg.audio_cue_lead_ms;
        d.target_filter_enabled = cfg.target_filter_enabled;
        d.target_match          = in.target_match;
        d.anim_instance_seq     = st.anim_instance_seq;
        d.action                = ACTION_NO_DB;
        WritePredictionDecision(d);
        return 1;
    }

    // No row for (cid, anim) → emit one "no_key" decision and bail. This
    // is the COMMON case for animations the DB doesn't cover (idle, walk,
    // most non-attack anims). The decision log filter at write time
    // can rate-limit these.
    if (resolved_cid == 0) {
        PredictionDecision d;
        d.ts_ms_rel             = now_ms_rel;
        d.boss_handle           = in.boss_handle;
        d.boss_chr_ins          = in.boss_chr_ins;
        d.raw_cid               = in.raw_cid;
        d.resolved_cid          = 0;
        d.anim_id               = in.anim_id;
        d.anim_time_s           = in.anim_time_s;
        d.configured_lead_ms    = cfg.audio_cue_lead_ms;
        d.target_filter_enabled = cfg.target_filter_enabled;
        d.target_match          = in.target_match;
        d.anim_instance_seq     = st.anim_instance_seq;
        d.action                = ACTION_NO_KEY;
        WritePredictionDecision(d);
        return 1;
    }

    const std::vector<ParryWindow>* wins =
        LookupParryWindows(resolved_cid, in.anim_id);
    if (!wins || wins->empty()) {
        // Should never happen if resolved_cid != 0 (we just looked it up),
        // but be defensive. Emit a no_key and bail.
        PredictionDecision d;
        d.ts_ms_rel             = now_ms_rel;
        d.boss_handle           = in.boss_handle;
        d.boss_chr_ins          = in.boss_chr_ins;
        d.raw_cid               = in.raw_cid;
        d.resolved_cid          = resolved_cid;
        d.anim_id               = in.anim_id;
        d.anim_time_s           = in.anim_time_s;
        d.configured_lead_ms    = cfg.audio_cue_lead_ms;
        d.target_filter_enabled = cfg.target_filter_enabled;
        d.target_match          = in.target_match;
        d.anim_instance_seq     = st.anim_instance_seq;
        d.action                = ACTION_NO_KEY;
        WritePredictionDecision(d);
        return 1;
    }

    // Per-window evaluation.
    int emitted = 0;
    for (size_t i = 0; i < wins->size(); ++i) {
        if (i >= MAX_WINDOWS_PER_ANIM_LATCH) {
            // Overflow guard: log a single warning by emitting an action
            // that the writer will surface, and stop.
            PredictionDecision d;
            d.ts_ms_rel             = now_ms_rel;
            d.boss_handle           = in.boss_handle;
            d.raw_cid               = in.raw_cid;
            d.resolved_cid          = resolved_cid;
            d.anim_id               = in.anim_id;
            d.anim_time_s           = in.anim_time_s;
            d.window_ordinal        = (uint16_t)i;
            d.configured_lead_ms    = cfg.audio_cue_lead_ms;
            d.target_filter_enabled = cfg.target_filter_enabled;
            d.target_match          = in.target_match;
            d.anim_instance_seq     = st.anim_instance_seq;
            d.action                = ACTION_NO_KEY;
            WritePredictionDecision(d);
            emitted += 1;
            break;
        }
        PredictionDecision d = EvaluatePredictionWindow(
            &st, in, cfg, (*wins)[i], (uint16_t)i, now_ms_rel);
        WritePredictionDecision(d);
        emitted += 1;
    }
    return emitted;
}

// ===========================================================================
// Decision-log JSONL writer
// ===========================================================================
//
// One file per session: <log_dir>/<session_tag>.predictions.jsonl. Opened
// by the worker after session paths are resolved (commit 4); writes are
// append-buffered with fflush on a 1s cadence matching existing logs.
//
// Rate limiting: ACTION_NO_KEY and ACTION_BEFORE_LEAD are the high-volume
// states (boss-not-doing-anything-interesting or pre-threshold). We
// rate-limit those to one row per (boss, anim_id, action) per second so
// the log stays usable. Interesting transitions (FIRE, SUPPRESSED_*,
// LATCHED, NO_DB) are always emitted.

static FILE* g_predictionLog = nullptr;
static CRITICAL_SECTION g_predictionLogLock;
static bool g_predictionLogReady = false;

// Structured rate-limit key. XOR composition can collide unrelated tuples;
// a struct + custom hash avoids it. PredictionAction is uint8_t so the
// packed key fits in 96 bits — use a pair<uint64,uint64> for the map key
// (boss_handle | anim_id+action) since std::unordered_map needs a hash.
struct PredRateKey {
    uint64_t boss_handle;
    uint64_t anim_id_action;   // (anim_id << 8) | action
    bool operator==(const PredRateKey& o) const {
        return boss_handle == o.boss_handle && anim_id_action == o.anim_id_action;
    }
};
struct PredRateKeyHash {
    size_t operator()(const PredRateKey& k) const noexcept {
        // splitmix64-flavored mix; cheap and collision-friendly.
        uint64_t x = k.boss_handle * 0x9E3779B97F4A7C15ull;
        x ^= k.anim_id_action + 0x94D049BB133111EBull + (x << 6) + (x >> 2);
        return static_cast<size_t>(x);
    }
};

// Last-emit timestamp for the rate-limiter. MUST be accessed only under
// g_predictionLogLock (so multi-threaded callers in future commits don't
// race the map). Today the engine is single-threaded so this is just
// hygiene; commit 4 may add a flusher thread that doesn't write but
// also doesn't need this map.
static std::unordered_map<PredRateKey, int64_t, PredRateKeyHash> g_predictionLogRateMap;
static constexpr int64_t PREDICTION_LOG_RATE_LIMIT_MS = 1000;

static bool IsRateLimitedAction(PredictionAction a) {
    return a == ACTION_NO_KEY || a == ACTION_BEFORE_LEAD;
}

// Must be called WITH g_predictionLogLock held.
static bool PredictionLogShouldEmitLocked(const PredictionDecision& d) {
    if (!IsRateLimitedAction(d.action)) return true;
    PredRateKey k{
        d.boss_handle,
        (static_cast<uint64_t>(d.anim_id) << 8) | static_cast<uint64_t>(d.action),
    };
    auto it = g_predictionLogRateMap.find(k);
    int64_t prev = (it == g_predictionLogRateMap.end())
        ? d.ts_ms_rel - PREDICTION_LOG_RATE_LIMIT_MS - 1   // force emit
        : it->second;
    if ((d.ts_ms_rel - prev) < PREDICTION_LOG_RATE_LIMIT_MS) return false;
    g_predictionLogRateMap[k] = d.ts_ms_rel;
    return true;
}

// Sanitize a float for JSON output. printf "%f" emits "nan"/"inf"/"-inf"
// which are NOT valid JSON tokens — they would crash the regression
// harness. Convert any non-finite value to 0.0f so the line stays valid.
// Callers that care about non-finiteness should already have rejected
// the row to ACTION_NO_KEY; this is belt-and-suspenders for the NO_DB /
// NO_KEY paths that bypass the per-window NaN check.
static inline float SanitizeJsonFloat(float v) {
    return std::isfinite(v) ? v : 0.0f;
}

// Write one decision as a JSON line. Thread-safe via the critical section.
// No-op if the log isn't ready (commit-4 will wire init).
static void WritePredictionDecision(const PredictionDecision& d) {
    if (!g_predictionLogReady || !g_predictionLog) return;

    // Build the line in a stack buffer. Max realistic size ~500 chars.
    char buf[640];
    int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "{\"ts_ms\":%lld,"
        "\"boss_handle\":%llu,"
        "\"boss_chr_ins\":\"0x%016llx\","
        "\"raw_cid\":%u,"
        "\"resolved_cid\":%u,"
        "\"anim_id\":%u,"
        "\"anim_time_s\":%.4f,"
        "\"window_ord\":%u,"
        "\"window_open_s\":%.4f,"
        "\"window_close_s\":%.4f,"
        "\"lead_ms\":%.2f,"
        "\"cfg_lead_ms\":%d,"
        "\"target_filter\":%s,"
        "\"target_match\":%s,"
        "\"inst_seq\":%llu,"
        "\"action\":\"%s\"}\n",
        (long long)d.ts_ms_rel,
        (unsigned long long)d.boss_handle,
        (unsigned long long)d.boss_chr_ins,
        d.raw_cid,
        d.resolved_cid,
        d.anim_id,
        SanitizeJsonFloat(d.anim_time_s),
        (unsigned)d.window_ordinal,
        SanitizeJsonFloat(d.window_open_s),
        SanitizeJsonFloat(d.window_close_s),
        SanitizeJsonFloat(d.lead_time_ms),
        d.configured_lead_ms,
        d.target_filter_enabled ? "true" : "false",
        d.target_match ? "true" : "false",
        (unsigned long long)d.anim_instance_seq,
        PredictionActionName(d.action));
    if (n <= 0) return;

    // Both rate-limit check and fwrite happen under the lock. The lock
    // is a process-local critical section so contention is bounded;
    // single-threaded callers in v1 see ~0 overhead.
    EnterCriticalSection(&g_predictionLogLock);
    bool ok = PredictionLogShouldEmitLocked(d);
    if (ok && g_predictionLog) {
        fwrite(buf, 1, (size_t)n, g_predictionLog);
    }
    LeaveCriticalSection(&g_predictionLogLock);
}

// Open/close hooks called from worker init / shutdown (commit 4 wires).
static bool PredictionLogOpen(const char* path) {
    InitializeCriticalSection(&g_predictionLogLock);
    if (fopen_s(&g_predictionLog, path, "w") != 0 || !g_predictionLog) {
        BootLog("prediction_log_fail: cannot open %s (errno=%d)", path, errno);
        DeleteCriticalSection(&g_predictionLogLock);
        return false;
    }
    // Clear stale rate-limit state in case Open is called after a prior
    // session (defensive — current architecture is one Open per process,
    // but matches the parry-DB parse-into-locals hygiene).
    g_predictionLogRateMap.clear();
    g_predictionLogReady = true;
    BootLog("prediction_log_ok: %s", path);
    return true;
}

static void PredictionLogFlush() {
    if (!g_predictionLogReady) return;
    EnterCriticalSection(&g_predictionLogLock);
    if (g_predictionLog) fflush(g_predictionLog);
    LeaveCriticalSection(&g_predictionLogLock);
}

static void PredictionLogClose() {
    if (!g_predictionLogReady) return;
    EnterCriticalSection(&g_predictionLogLock);
    if (g_predictionLog) {
        fflush(g_predictionLog);
        fclose(g_predictionLog);
        g_predictionLog = nullptr;
    }
    g_predictionLogRateMap.clear();
    LeaveCriticalSection(&g_predictionLogLock);
    DeleteCriticalSection(&g_predictionLogLock);
    g_predictionLogReady = false;
}

// ===========================================================================
// SEH-wrapped read primitives (unchanged from v5f)
// ===========================================================================

template<typename T>
static bool SafeRead(uintptr_t addr, T* out) {
    if (!addr || !out) return false;
    __try {
        *out = *reinterpret_cast<T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SafeReadBytes: copy `len` bytes from `addr` into `dst`. SEH-wrapped so a
// fault mid-copy degrades to "false" (caller should treat the partial-copy
// result as garbage and skip the region). Used for Tier 3 bulk capture.
static bool SafeReadBytes(uintptr_t addr, size_t len, void* dst) {
    if (!addr || !dst || len == 0) return false;
    __try {
        memcpy(dst, reinterpret_cast<const void*>(addr), len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Slow path; init-time only. Uses VirtualQuery (kernel syscall).
template<typename T>
static bool LooksReadable(uintptr_t addr) {
    if (!addr) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                     PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & readable)) return false;
    uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return (addr + sizeof(T)) <= regionEnd;
}

// Slow path; init-time only.
static bool LooksLikeUserPtr(uintptr_t v) {
    if (v == 0) return false;
    if (v & 0x7) return false;
    if ((v >> 47) != 0) return false;
    if (v < 0x10000ULL) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(v), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return true;
}

// FAST path; safe to call from detour. Pure compute, no syscalls. SEH in
// SafeRead handles real faults; this just rules out obviously bogus values.
static inline bool LooksLikeUserPtrFast(uintptr_t v) {
    if (v == 0) return false;
    if (v & 0x7) return false;
    if ((v >> 47) != 0) return false;
    if (v < 0x10000ULL) return false;
    return true;
}

// ===========================================================================
// Pattern (signature) scanning (unchanged from v5f)
// ===========================================================================

struct SigByte { uint8_t b; bool wild; };

static bool ParseSig(const char* sig, SigByte* out, int outCap, int* outLen) {
    int n = 0;
    const char* p = sig;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (n >= outCap) return false;
        if (*p == '?') {
            out[n].b = 0; out[n].wild = true;
            ++n;
            if (*(p+1) == '?') ++p;
            ++p;
            continue;
        }
        char hex[3] = { p[0], p[1], 0 };
        if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1])) return false;
        out[n].b = (uint8_t)strtoul(hex, nullptr, 16);
        out[n].wild = false;
        ++n;
        p += 2;
    }
    *outLen = n;
    return n > 0;
}

static bool MatchesAt(const uint8_t* haystack, const SigByte* pattern, int patLen) {
    __try {
        for (int j = 0; j < patLen; ++j) {
            if (pattern[j].wild) continue;
            if (haystack[j] != pattern[j].b) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct ScanResult {
    uintptr_t addrs[8] = {0};
    int count = 0;
    bool overflow = false;
};

static ScanResult ScanModuleExecForSig(HMODULE mod, const char* sig) {
    ScanResult r;
    if (!mod) return r;

    SigByte pattern[256];
    int patLen = 0;
    if (!ParseSig(sig, pattern, 256, &patLen) || patLen <= 0) return r;

    auto base = reinterpret_cast<uint8_t*>(mod);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return r;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return r;

    auto sec = IMAGE_FIRST_SECTION(nt);
    int nSec = nt->FileHeader.NumberOfSections;
    for (int s = 0; s < nSec; ++s, ++sec) {
        DWORD chars = sec->Characteristics;
        bool isExec = (chars & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool isCode = (chars & IMAGE_SCN_CNT_CODE) != 0;
        if (!(isExec || isCode)) continue;

        uint8_t* secBase = base + sec->VirtualAddress;
        size_t secSize = sec->Misc.VirtualSize;
        if (secSize == 0 || secSize < (size_t)patLen) continue;

        size_t end = secSize - patLen;
        for (size_t i = 0; i <= end; ++i) {
            if (MatchesAt(secBase + i, pattern, patLen)) {
                if (r.count < 8) {
                    r.addrs[r.count++] = reinterpret_cast<uintptr_t>(secBase + i);
                } else {
                    r.overflow = true;
                    return r;
                }
            }
        }
    }
    return r;
}

static uintptr_t RipRelativeDeref(uintptr_t addr, int addOffset) {
    if (!addr) return 0;
    uintptr_t dispAddr = addr + addOffset;
    int32_t disp = 0;
    if (!LooksReadable<int32_t>(dispAddr)) return 0;
    if (!SafeRead<int32_t>(dispAddr, &disp)) return 0;
    return dispAddr + 4 + (int64_t)disp;
}

static bool VerifyFunctionAtAddr(uintptr_t fnAddr, const char* sig) {
    if (!fnAddr) return false;
    SigByte pattern[256];
    int patLen = 0;
    if (!ParseSig(sig, pattern, 256, &patLen) || patLen <= 0) return false;
    return MatchesAt(reinterpret_cast<const uint8_t*>(fnAddr), pattern, patLen);
}

// ===========================================================================
// Game-version check (Elden Ring 2.6.1.0 only)
// ===========================================================================

// Returns 1 = matches, 0 = wrong version, -1 = read failed.
static int CheckExpectedGameVersion(uint32_t* outMajor, uint32_t* outMinor,
                                    uint32_t* outBuild, uint32_t* outPatch) {
    if (outMajor) *outMajor = 0;
    if (outMinor) *outMinor = 0;
    if (outBuild) *outBuild = 0;
    if (outPatch) *outPatch = 0;
    HMODULE er = GetModuleHandleA("eldenring.exe");
    if (!er) return -1;
    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(er, path, MAX_PATH) == 0) return -1;

    DWORD dummy = 0;
    DWORD verSize = GetFileVersionInfoSizeA(path, &dummy);
    if (verSize == 0) return -1;

    BYTE* verData = static_cast<BYTE*>(malloc(verSize));
    if (!verData) return -1;
    int result = -1;
    if (GetFileVersionInfoA(path, 0, verSize, verData)) {
        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT ffiLen = 0;
        if (VerQueryValueA(verData, "\\", reinterpret_cast<LPVOID*>(&ffi), &ffiLen) &&
            ffi && ffiLen >= sizeof(VS_FIXEDFILEINFO)) {
            uint32_t major = HIWORD(ffi->dwFileVersionMS);
            uint32_t minor = LOWORD(ffi->dwFileVersionMS);
            uint32_t build = HIWORD(ffi->dwFileVersionLS);
            uint32_t patch = LOWORD(ffi->dwFileVersionLS);
            if (outMajor) *outMajor = major;
            if (outMinor) *outMinor = minor;
            if (outBuild) *outBuild = build;
            if (outPatch) *outPatch = patch;
            result = (major == 2 && minor == 6 && build == 1 && patch == 0) ? 1 : 0;
        }
    }
    free(verData);
    return result;
}

// ===========================================================================
// Game function signatures (resolved at init)
// ===========================================================================

// WCM global (PostureBarMod, verified):
static const char* SIG_WORLD_CHR_MAN =
    "48 8B 05 ? ? ? ? 48 85 C0 74 0F 48 39 88";

// GetChrInsFromHandle (PostureBarMod, verified):
static const char* SIG_GET_CHR_INS_FROM_HANDLE =
    "48 83 EC 28 E8 17 FF FF FF 48 85 C0 74 08 48 8B 00 48 83 C4 28 C3";

// UpdateUIBarStructs (PostureBarMod, verified for 2.6.1):
static const char* SIG_UPDATE_UI_BAR_STRUCTS =
    "40 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 C7 44 24 30 FE FF FF FF "
    "48 89 9C 24 B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 58 48";

// CSFeManImp singleton pointer (PostureBarMod's Hooking.cpp:34):
//   `CSFeManSignature = Signature("48 8B 0D ? ? ? ? 8B DA 48 85 C9 75 ?
//                                  48 8D 0D ? ? ? ? E8 ? ? ? ? 4C 8B C8
//                                  4C 8D 05 ? ? ? ? BA B4 00 00 00 48 8D 0D
//                                  ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ?
//                                  8B D3 E8 ? ? ? ? 48 8B D8")
//                       .Scan().Add(3).Rip().As<uint64_t>();`
// The pattern matches a `mov rcx, [rip+disp]` whose disp resolves to the
// singleton pointer slot (a global qword that, when dereferenced, gives
// the CSFeManImp* instance).
static const char* SIG_CS_FE_MAN_IMP =
    "48 8B 0D ? ? ? ? 8B DA 48 85 C9 75 ? 48 8D 0D ? ? ? ? E8 ? ? ? ? "
    "4C 8B C8 4C 8D 05 ? ? ? ? BA B4 00 00 00 48 8D 0D ? ? ? ? E8 ? ? ? ? "
    "48 8B 0D ? ? ? ? 8B D3 E8 ? ? ? ? 48 8B D8";

// ===========================================================================
// Resolved game refs
// ===========================================================================

struct GameRefs {
    uintptr_t er_module_base   = 0;
    uintptr_t wcmPtrAddr       = 0;       // address of the WCM* (deref to get WCM*)
    uintptr_t getChrInsFn      = 0;
    uintptr_t updateUIBarFn    = 0;
    uintptr_t csFeManPtrAddr   = 0;       // address of the CSFeManImp* slot (deref→inst)
    bool ready                 = false;
};
static GameRefs g_refs;

typedef void* (*GetChrInsFromHandle_t)(void* worldChrMan, uint64_t* handlePtr);
typedef void  (*UpdateUIBarStructs_t)(uintptr_t moveMapStep, uintptr_t time);
static UpdateUIBarStructs_t g_originalUpdateUIBar = nullptr;

static bool ResolveGameRefs() {
    HMODULE er = GetModuleHandleA("eldenring.exe");
    if (!er) return false;
    g_refs.er_module_base = reinterpret_cast<uintptr_t>(er);

    // WCM ptr-slot
    {
        ScanResult hits = ScanModuleExecForSig(er, SIG_WORLD_CHR_MAN);
        if (hits.overflow || hits.count != 1) {
            BootLog("sig_fail: WCM hits=%d overflow=%d", hits.count, hits.overflow);
            return false;
        }
        uintptr_t wcm = RipRelativeDeref(hits.addrs[0], 3);
        if (!wcm) { BootLog("sig_fail: WCM rip-deref returned 0"); return false; }
        g_refs.wcmPtrAddr = wcm;
    }

    // GetChrInsFromHandle
    {
        ScanResult hits = ScanModuleExecForSig(er, SIG_GET_CHR_INS_FROM_HANDLE);
        if (hits.overflow || hits.count != 1) {
            BootLog("sig_fail: getFn hits=%d overflow=%d", hits.count, hits.overflow);
            return false;
        }
        uintptr_t fn = hits.addrs[0];
        if (!VerifyFunctionAtAddr(fn, SIG_GET_CHR_INS_FROM_HANDLE)) {
            BootLog("sig_fail: getFn verify failed at 0x%016llX", (unsigned long long)fn);
            return false;
        }
        g_refs.getChrInsFn = fn;
    }

    // UpdateUIBarStructs (hook target)
    {
        ScanResult hits = ScanModuleExecForSig(er, SIG_UPDATE_UI_BAR_STRUCTS);
        if (hits.overflow || hits.count != 1) {
            BootLog("sig_fail: UpdateUIBar hits=%d overflow=%d "
                    "(may indicate another mod hooked this fn — e.g. PostureBarMod). "
                    "Try unloading conflicting mods.",
                    hits.count, hits.overflow);
            return false;
        }
        uintptr_t fn = hits.addrs[0];
        if (!VerifyFunctionAtAddr(fn, SIG_UPDATE_UI_BAR_STRUCTS)) {
            BootLog("sig_fail: UpdateUIBar verify failed at 0x%016llX",
                    (unsigned long long)fn);
            return false;
        }
        g_refs.updateUIBarFn = fn;
    }

    // CSFeManImp ptr-slot — required for boss-bar enumeration (spec §Tier 1).
    {
        ScanResult hits = ScanModuleExecForSig(er, SIG_CS_FE_MAN_IMP);
        if (hits.overflow || hits.count != 1) {
            BootLog("sig_fail: CSFeManImp hits=%d overflow=%d (boss-bars unavailable)",
                    hits.count, hits.overflow);
            return false;
        }
        uintptr_t slot = RipRelativeDeref(hits.addrs[0], 3);
        if (!slot) {
            BootLog("sig_fail: CSFeManImp rip-deref returned 0");
            return false;
        }
        g_refs.csFeManPtrAddr = slot;
    }

    g_refs.ready = true;
    BootLog("sig_ok: er_base=0x%016llX wcm=0x%016llX getFn=0x%016llX uiFn=0x%016llX feMan=0x%016llX",
            (unsigned long long)g_refs.er_module_base,
            (unsigned long long)g_refs.wcmPtrAddr,
            (unsigned long long)g_refs.getChrInsFn,
            (unsigned long long)g_refs.updateUIBarFn,
            (unsigned long long)g_refs.csFeManPtrAddr);
    return true;
}

// ===========================================================================
// Roster quarantine — 7-check init-time validation
// (spec §Enemy roster quarantine; checks 1-6 init-time, 7 deferred to runtime)
// ===========================================================================

struct RosterValidation {
    bool check1 = false;  // begin/end readable
    bool check2 = false;  // begin <= end
    bool check3 = false;  // (end-begin) % 8 == 0
    bool check4 = false;  // count in [0, 2048)
    bool check5 = false;  // sample candidates pass shape + handle nonzero/sane
    bool check6 = false;  // at least one candidate has a TimeAct chain
    bool check7 = false;  // runtime: a boss-bar enemy appears in roster (deferred)
    bool enabled = false; // true iff checks 1-6 all pass
    int  candidate_count = 0;
};
static RosterValidation g_roster;
static std::atomic<bool> g_check7Done{false};   // runtime confirmation set later
static std::atomic<int64_t> g_qualStartMs{-1};

static bool ValidateRosterInit() {
    g_roster = RosterValidation{};
    if (!g_refs.ready || !g_refs.wcmPtrAddr) return false;

    // v6.1: De-spam — emit roster_fail messages every 5th attempt instead of every one.
    static int s_wcmRetryCount = 0;

    uintptr_t wcm = 0;
    if (!SafeRead<uintptr_t>(g_refs.wcmPtrAddr, &wcm) || !LooksLikeUserPtr(wcm)) {
        if (s_wcmRetryCount == 0 || (s_wcmRetryCount % 5) == 0) {
            BootLog("roster_fail: WCM not yet readable (attempt #%d)", s_wcmRetryCount);
        }
        s_wcmRetryCount++;
        return false;
    }
    s_wcmRetryCount = 0;

    uintptr_t beginAddr = wcm + Off::WCM_ROSTER_BEGIN;
    uintptr_t endAddr   = wcm + Off::WCM_ROSTER_END;
    uintptr_t begin = 0, end = 0;
    if (!SafeRead<uintptr_t>(beginAddr, &begin) ||
        !SafeRead<uintptr_t>(endAddr, &end)) {
        BootLog("roster_fail: check1 (read of begin/end) failed");
        return false;
    }
    g_roster.check1 = true;

    if (begin == 0 || end == 0 || begin > end) {
        BootLog("roster_fail: check2 (begin<=end & nonzero) begin=0x%llX end=0x%llX",
                (unsigned long long)begin, (unsigned long long)end);
        return false;
    }
    g_roster.check2 = true;

    uintptr_t span = end - begin;
    if ((span % sizeof(uintptr_t)) != 0) {
        BootLog("roster_fail: check3 (span %zu not 8-aligned)", (size_t)span);
        return false;
    }
    g_roster.check3 = true;

    size_t count = span / sizeof(uintptr_t);
    if (count > 2048) {
        BootLog("roster_fail: check4 (count=%zu out of [0, 2048))", count);
        return false;
    }
    g_roster.check4 = true;
    g_roster.candidate_count = (int)count;

    // Check 5: sample first up to 32 candidates; require AT LEAST ONE to have
    // a sane handle resolvable through GetChrInsFromHandle. (Per spec we want
    // candidates to pass shape + handle nonzero + GCFH returns canonical ptr.)
    int sane = 0;
    GetChrInsFromHandle_t getFn = reinterpret_cast<GetChrInsFromHandle_t>(g_refs.getChrInsFn);
    int probeN = (count < 32) ? (int)count : 32;
    for (int i = 0; i < probeN; ++i) {
        uintptr_t slot = begin + (size_t)i * sizeof(uintptr_t);
        uintptr_t chrIns = 0;
        if (!SafeRead<uintptr_t>(slot, &chrIns)) continue;
        if (!LooksLikeUserPtrFast(chrIns)) continue;

        uintptr_t handleAddr = chrIns + Off::CHR_INS_HANDLE;
        uint64_t handle = 0;
        if (!SafeRead<uint64_t>(handleAddr, &handle)) continue;
        if (handle == 0 || handle == UINT64_MAX) continue;

        void* resolved = nullptr;
        __try {
            resolved = getFn(reinterpret_cast<void*>(wcm),
                             reinterpret_cast<uint64_t*>(handleAddr));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            resolved = nullptr;
        }
        uintptr_t resolvedAddr = reinterpret_cast<uintptr_t>(resolved);
        if (LooksLikeUserPtrFast(resolvedAddr)) {
            ++sane;
        }
    }
    if (sane == 0 && count > 0) {
        BootLog("roster_fail: check5 (no sane candidates over %d sampled)", probeN);
        return false;
    }
    g_roster.check5 = true;

    // Check 6: at least one candidate's chrIns + 0x190 → +0x18 yields a sane TimeAct ptr.
    bool sawTimeAct = false;
    for (int i = 0; i < probeN; ++i) {
        uintptr_t slot = begin + (size_t)i * sizeof(uintptr_t);
        uintptr_t chrIns = 0;
        if (!SafeRead<uintptr_t>(slot, &chrIns)) continue;
        if (!LooksLikeUserPtrFast(chrIns)) continue;

        uintptr_t bagAddr = chrIns + Off::CHR_INS_MODULE_BAG_PTR;
        uintptr_t bag = 0;
        if (!SafeRead<uintptr_t>(bagAddr, &bag) || !LooksLikeUserPtrFast(bag)) continue;

        uintptr_t timeActAddr = bag + Off::MODULE_BAG_TIME_ACT_PTR;
        uintptr_t ta = 0;
        if (!SafeRead<uintptr_t>(timeActAddr, &ta) || !LooksLikeUserPtrFast(ta)) continue;

        sawTimeAct = true;
        break;
    }
    if (!sawTimeAct && count > 0) {
        BootLog("roster_fail: check6 (no candidate with TimeAct chain)");
        return false;
    }
    g_roster.check6 = true;

    g_roster.enabled = true;
    BootLog("roster_ok: enabled count=%d (checks 1-6 pass; check7 deferred)",
            g_roster.candidate_count);
    return true;
}

// ===========================================================================
// Buffer pool + SPSC queue (producer = detour, consumer = worker)
// ===========================================================================
//
// Each buffer is `MAX_SAMPLE_BYTES` (256 KB), aligned to 64 bytes. A buffer
// holds ONE complete sample's serialized payload + a fixed-size header.
//
// Two atomic indexes operate over a power-of-two-sized array of buffer
// indices. We use BUFFER_POOL_SIZE = 256 (a power of two), so wrap is
// (idx & (BUFFER_POOL_SIZE-1)).
//
// State of each pool slot is implicit:
//   free_head: next free buffer the producer will grab.
//   filled_head: next filled buffer the worker will consume.
// Both monotonically increase; modular index gives the actual slot.
//
// Producer flow (detour, game thread):
//   1. CAS-bump free_head to claim a buffer index.
//      If free_head - filled_tail == 0: pool empty, drop sample.
//   2. Write into pool[idx].data; set pool[idx].len.
//   3. Bump filled_head to mark filled (only after data is fully written).
//
// Worker flow (off-thread):
//   1. Read filled_head; if filled_head == filled_tail, sleep.
//   2. Process pool[filled_tail].
//   3. Bump filled_tail.
//
// Free slots = BUFFER_POOL_SIZE - (filled_head - filled_tail).
// (More precisely: producer claims buffers contiguously; the consumer
// consumes in order; the gap between filled_head and filled_tail is
// "in-flight". A separate 'free_head' isn't needed because the producer
// always uses filled_head as its write index — it just bumps it after
// writing and reports failure if the gap is too large.)
//
// We collapse this to ONE atomic write_idx + one atomic read_idx; the
// producer fails (drops) if (write_idx - read_idx) >= BUFFER_POOL_SIZE.

struct alignas(64) PoolBuffer {
    uint8_t* data = nullptr;          // _aligned_malloc(MAX_SAMPLE_BYTES, 64)
    size_t   len  = 0;                // valid bytes written by producer
    uint64_t frame = 0;               // tick counter at capture time
    uint64_t ts_ms_rel = 0;           // ms since session start
    bool     truncated = false;
    uint8_t  mode_at_capture = 0;     // CaptureMode value
    uint8_t  pad[7] = {0};
};

static PoolBuffer g_pool[BUFFER_POOL_SIZE];
static std::atomic<uint64_t> g_writeIdx{0};   // producer pushes here
static std::atomic<uint64_t> g_readIdx{0};    // worker consumes here
static std::atomic<bool> g_poolReady{false};

static bool BufferPoolAlloc() {
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        g_pool[i].data = static_cast<uint8_t*>(_aligned_malloc(MAX_SAMPLE_BYTES, 64));
        if (!g_pool[i].data) {
            // Free what we got
            for (size_t j = 0; j < i; ++j) {
                _aligned_free(g_pool[j].data);
                g_pool[j].data = nullptr;
            }
            BootLog("pool_fail: _aligned_malloc(%zu) returned null at slot %zu",
                    MAX_SAMPLE_BYTES, i);
            return false;
        }
        g_pool[i].len = 0;
    }
    g_writeIdx.store(0);
    g_readIdx.store(0);
    g_poolReady.store(true);
    BootLog("pool_ok: %zu × %zu = %zu MB buffer pool allocated",
            BUFFER_POOL_SIZE, MAX_SAMPLE_BYTES,
            (BUFFER_POOL_SIZE * MAX_SAMPLE_BYTES) / (1024 * 1024));
    return true;
}

static void BufferPoolFree() {
    g_poolReady.store(false);
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        if (g_pool[i].data) {
            _aligned_free(g_pool[i].data);
            g_pool[i].data = nullptr;
        }
    }
}

// Returns slot index 0..BUFFER_POOL_SIZE-1, or -1 on pool empty.
static inline int ProducerClaim() {
    uint64_t w = g_writeIdx.load(std::memory_order_relaxed);
    uint64_t r = g_readIdx.load(std::memory_order_acquire);
    if ((w - r) >= BUFFER_POOL_SIZE) {
        g_dropNoBuffer.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    return (int)(w & (BUFFER_POOL_SIZE - 1));
}

static inline void ProducerCommit() {
    g_writeIdx.fetch_add(1, std::memory_order_release);
}

// Returns free-pool size right now (approximate; producer may have just
// claimed). Used by emergency-drop logic.
static inline int FreePoolSize() {
    uint64_t w = g_writeIdx.load(std::memory_order_relaxed);
    uint64_t r = g_readIdx.load(std::memory_order_acquire);
    int64_t in_flight = (int64_t)(w - r);
    return (int)(BUFFER_POOL_SIZE - in_flight);
}

// Worker side: returns slot index 0..BUFFER_POOL_SIZE-1, or -1 if queue empty.
static inline int ConsumerPeek() {
    uint64_t w = g_writeIdx.load(std::memory_order_acquire);
    uint64_t r = g_readIdx.load(std::memory_order_relaxed);
    if (w == r) return -1;
    return (int)(r & (BUFFER_POOL_SIZE - 1));
}

static inline void ConsumerRelease() {
    g_readIdx.fetch_add(1, std::memory_order_release);
}

// ===========================================================================
// Per-enemy state (handle → state map; small fixed array, linear-scan)
// ===========================================================================

struct EnemyState {
    bool     in_use         = false;
    uint64_t handle         = 0;       // canonical key
    uintptr_t chr_ins_abs   = 0;       // last-known address (may stale on respawn)
    uint32_t phase          = 0;       // hash(handle) % 90 — used for decimation
    uint64_t last_observed_tick = 0;   // for LRU eviction
    EnemyClass cls          = ENEMY_CLASS_LESSER;
    bool     is_focused     = false;
    FocusedReason focus_reason = FOCUS_NONE;
};

// Tracking pool: keep at most MAX_TRACKED_ENEMIES at any time.
static EnemyState g_enemies[MAX_TRACKED_ENEMIES];

static inline uint32_t Hash64(uint64_t v) {
    // splitmix64-style finalizer (non-cryptographic, fast).
    v ^= v >> 33; v *= 0xFF51AFD7ED558CCDull;
    v ^= v >> 33; v *= 0xC4CEB9FE1A85EC53ull;
    v ^= v >> 33;
    return (uint32_t)(v & 0xFFFFFFFFu);
}

// Find or insert an enemy by handle. On insert: evicts LRU if full.
// Returns nullptr only on pathological case (slots all in_use this very frame).
static EnemyState* EnemyTrack(uint64_t handle, uintptr_t chr_ins_abs, uint64_t tick) {
    // Find existing.
    int firstFree = -1;
    int lru = -1;
    uint64_t lruTick = UINT64_MAX;
    for (int i = 0; i < MAX_TRACKED_ENEMIES; ++i) {
        EnemyState& e = g_enemies[i];
        if (e.in_use && e.handle == handle) {
            e.chr_ins_abs = chr_ins_abs;
            e.last_observed_tick = tick;
            return &e;
        }
        if (!e.in_use && firstFree == -1) firstFree = i;
        if (e.in_use && e.last_observed_tick < lruTick) {
            lru = i;
            lruTick = e.last_observed_tick;
        }
    }

    int slot = (firstFree != -1) ? firstFree : lru;
    if (slot == -1) return nullptr;

    EnemyState& e = g_enemies[slot];
    e.in_use = true;
    e.handle = handle;
    e.chr_ins_abs = chr_ins_abs;
    e.phase = Hash64(handle) % 90;        // 90 = LCM(9, 45) range — staggers both decimations
    e.last_observed_tick = tick;
    e.cls = ENEMY_CLASS_LESSER;
    e.is_focused = false;
    e.focus_reason = FOCUS_NONE;
    return &e;
}

// Mark enemies not seen in `staleness` ticks as free (for re-use).
static void EnemyEvictStale(uint64_t tick, uint64_t staleness = 600) {
    for (int i = 0; i < MAX_TRACKED_ENEMIES; ++i) {
        EnemyState& e = g_enemies[i];
        if (!e.in_use) continue;
        if (tick - e.last_observed_tick > staleness) {
            e.in_use = false;
            e.handle = 0;
            e.chr_ins_abs = 0;
        }
    }
}

// ===========================================================================
// Sample serialization (writer helper for the pool buffer)
// ===========================================================================
//
// Wire format (little-endian, packed; sizes match the v6 spec):
//
//   SampleHeader (fixed 132 bytes — 4+4+8+8+1+1+6+8+8+8+4+16+12+8+24+8+1+1+1+1):
//     u32   magic = 'PTS0'
//     u32   schema_version
//     u64   frame
//     u64   ts_ms_rel
//     u8    mode_at_capture
//     u8    truncated
//     u8    reserved[6]
//     u64   wcm_ptr
//     u64   module_base_eldenring
//     u64   player_chr_ins_abs
//     u32   player_anim_id
//     f32   player_anim_time_candidates[4]
//     f32   player_pos_xyz[3]
//     u64   player_lock_on_target_handle
//     u64   boss_bar_handles[3]
//     u64   focused_enemy_handle
//     u8    focused_reason
//     u8    enemy_record_count       (filled in at end of write)
//     u8    adaptive_step_at_capture
//     u8    reserved2
//
//   For each enemy (variable):
//     EnemyHeader (0x60 bytes):
//       u64   enemy_chr_ins_abs
//       u64   enemy_handle
//       u32   field_at_0x038
//       u32   field_at_0x060
//       u32   field_at_0x064
//       u32   field_at_0x068
//       u32   field_at_0x06C
//       u32   field_at_0x080
//       u32   field_at_0x1E8
//       u32   enemy_anim_id
//       f32   enemy_anim_time_candidates[4]
//       u8    in_lock_on
//       u8    in_boss_bar
//       u8    in_roster
//       u8    enemy_class
//       u8    is_focused
//       u8    focus_reason
//       u8    region_count_for_this_enemy
//       u8    reserved
//
//     For each region record (variable):
//       u8    region_id
//       u8    has_child_offset
//       u16   payload_offset
//       u16   payload_len
//       u16   child_source_offset_in_time_act (valid iff has_child_offset)
//       u32   source_chain
//       u64   region_base_abs
//       u8    payload[payload_len]
//
// All multi-byte fields are little-endian (native x64). No alignment padding
// inside the wire format beyond what's shown.

#define SAMPLE_MAGIC 0x30535450u  // 'PTS0' little-endian

struct Writer {
    uint8_t* buf = nullptr;
    size_t   cap = 0;
    size_t   pos = 0;
    bool     overflow = false;

    inline bool put(const void* src, size_t n) {
        if (overflow) return false;
        if (pos + n > cap) { overflow = true; return false; }
        memcpy(buf + pos, src, n);
        pos += n;
        return true;
    }
    inline bool put_u8 (uint8_t v)   { return put(&v, 1); }
    inline bool put_u16(uint16_t v)  { return put(&v, 2); }
    inline bool put_u32(uint32_t v)  { return put(&v, 4); }
    inline bool put_u64(uint64_t v)  { return put(&v, 8); }
    inline bool put_f32(float v)     { return put(&v, 4); }
    // Return current position so we can patch in a count later.
    inline size_t mark()             { return pos; }
    inline void patch_u8 (size_t off, uint8_t v)  { if (off + 1 <= pos) buf[off] = v; }
};

// ===========================================================================
// Tier 3 region capture helpers (fill writer; SEH-safe; bounded by remaining cap)
// ===========================================================================

// Write a single Tier 3 region record. Returns true if at least the header
// + payload was written; false if writer overflowed mid-record (caller should
// stop adding regions for this enemy and set truncated flag).
static bool WriteRegionRecord(Writer& w,
                              RegionId region_id,
                              uintptr_t region_base_abs,
                              uint32_t source_chain,
                              uint16_t payload_offset,
                              size_t payload_cap,
                              bool has_child_offset,
                              uint16_t child_source_offset)
{
    if (w.overflow) return false;
    // We write: 1+1+2+2+2 + 4 + 8 + payload_len bytes.
    constexpr size_t REGION_HEADER_BYTES = 1 + 1 + 2 + 2 + 2 + 4 + 8;
    if (w.pos + REGION_HEADER_BYTES > w.cap) { w.overflow = true; return false; }
    size_t avail = w.cap - (w.pos + REGION_HEADER_BYTES);
    size_t to_read = (payload_cap < avail) ? payload_cap : avail;
    if (to_read == 0) { w.overflow = true; return false; }
    if (to_read > 0xFFFFu) to_read = 0xFFFFu;

    // Header
    w.put_u8 ((uint8_t)region_id);
    w.put_u8 ((uint8_t)(has_child_offset ? 1 : 0));
    w.put_u16(payload_offset);
    w.put_u16((uint16_t)to_read);
    w.put_u16(child_source_offset);
    w.put_u32(source_chain);
    w.put_u64((uint64_t)region_base_abs);

    // Payload — SEH-wrapped bulk copy directly into the writer slot.
    if (w.pos + to_read > w.cap) { w.overflow = true; return false; }
    bool ok = SafeReadBytes(region_base_abs + payload_offset, to_read, w.buf + w.pos);
    if (!ok) {
        // Region unreadable; zero out and continue. payload_len header already
        // says `to_read` bytes are present — keep that contract.
        memset(w.buf + w.pos, 0, to_read);
    }
    w.pos += to_read;
    return true;
}

// ===========================================================================
// QPC time helper (high-resolution wall clock for budget checks)
// ===========================================================================

static double g_qpcMsPerCount = 0.0;

static void InitQpc() {
    LARGE_INTEGER f;
    if (QueryPerformanceFrequency(&f) && f.QuadPart > 0) {
        g_qpcMsPerCount = 1000.0 / (double)f.QuadPart;
    } else {
        g_qpcMsPerCount = 0.0;  // disable budget gating if QPC broken
    }
}

static inline int64_t QpcNow() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (int64_t)c.QuadPart;
}

static inline double QpcElapsedMs(int64_t start) {
    if (g_qpcMsPerCount <= 0.0) return 0.0;
    int64_t now = QpcNow();
    return (double)(now - start) * g_qpcMsPerCount;
}

// ===========================================================================
// Tier 1 + Tier 2 + focused-enemy resolution + Tier 3 capture (in detour)
// ===========================================================================
//
// Called once per detour invocation when armed. All work happens inside an
// outer SEH wrapper in the caller (DetourUpdateUIBarStructs) so unexpected
// faults degrade to "no sample this frame" rather than crashing.

// Working scratch (per-sample, lives across helper calls within one tick):
struct EnemySnapshot {
    uintptr_t chr_ins_abs    = 0;
    uint64_t  handle         = 0;
    uint32_t  field_at_0x038 = 0;
    uint32_t  field_at_0x060 = 0;
    uint32_t  field_at_0x064 = 0;
    uint32_t  field_at_0x068 = 0;
    uint32_t  field_at_0x06C = 0;
    uint32_t  field_at_0x080 = 0;
    uint32_t  field_at_0x1E8 = 0;
    uintptr_t module_bag     = 0;
    uintptr_t time_act       = 0;
    uintptr_t ai_bag         = 0;   // v7.0: stashed for AI_BAG_HEAD region capture
    uintptr_t ai_struct      = 0;
    uint32_t  anim_id        = 0;  // path A: TimeActModule + 0xD0 (legacy)
    float     anim_time[4]   = {0,0,0,0};
    bool      in_lock_on     = false;
    bool      in_boss_bar    = false;
    bool      in_roster      = false;
    EnemyClass cls           = ENEMY_CLASS_LESSER;
    bool      is_focused     = false;
    FocusedReason focus_reason = FOCUS_NONE;

    // v6.2 instrumentation (schema v2):
    // Path-B: TimeActModule + 0x20 + read_idx*16 → anim_id (vswarte anim_queue model)
    uint32_t  anim_id_path_b = 0;
    uint32_t  read_idx       = 0xFFFFFFFFu;  // sentinel: not read
    // Path-C: module_bag → +0x80 (ActionRequest) → +0x90 (Erd-Tools enemy anim)
    uint32_t  anim_id_path_c = 0;
    uintptr_t action_request = 0;
    // World position dual-read:
    // legacy: chrIns + 0x6C0..0x6C8 (already captured in field_at_0x06C and elsewhere)
    uintptr_t phys_module    = 0;
    float     world_pos_phys[3] = {0,0,0};  // module_bag → +0x68 → +0x70/74/78
};

// Resolve an enemy's ChrIns* + read all Tier 2 fields (cheap reads only).
// Returns false if the chrIns shape fails or handle is invalid.
static bool EnemyReadTier2(uintptr_t chr_ins_abs, EnemySnapshot* s) {
    if (!LooksLikeUserPtrFast(chr_ins_abs)) return false;

    uint64_t handle = 0;
    if (!SafeRead<uint64_t>(chr_ins_abs + Off::CHR_INS_HANDLE, &handle)) return false;
    if (handle == 0 || handle == UINT64_MAX) return false;

    s->chr_ins_abs = chr_ins_abs;
    s->handle      = handle;

    // Raw field captures (NEUTRAL — no semantic interpretation):
    SafeRead<uint32_t>(chr_ins_abs + Off::CHR_INS_FIELD_38,  &s->field_at_0x038);
    SafeRead<uint32_t>(chr_ins_abs + Off::CHR_INS_FIELD_60,  &s->field_at_0x060);
    SafeRead<uint32_t>(chr_ins_abs + Off::CHR_INS_FIELD_64,  &s->field_at_0x064);
    SafeRead<uint32_t>(chr_ins_abs + Off::CHR_INS_FIELD_68,  &s->field_at_0x068);
    SafeRead<uint32_t>(chr_ins_abs + Off::CHR_INS_FIELD_6C,  &s->field_at_0x06C);
    SafeRead<uint32_t>(chr_ins_abs + Off::CHR_INS_FIELD_80,  &s->field_at_0x080);
    SafeRead<uint32_t>(chr_ins_abs + Off::CHR_INS_FIELD_1E8, &s->field_at_0x1E8);

    // Walk module bag → TimeAct.
    uintptr_t bag = 0;
    if (SafeRead<uintptr_t>(chr_ins_abs + Off::CHR_INS_MODULE_BAG_PTR, &bag) &&
        LooksLikeUserPtrFast(bag)) {
        s->module_bag = bag;
        uintptr_t ta = 0;
        if (SafeRead<uintptr_t>(bag + Off::MODULE_BAG_TIME_ACT_PTR, &ta) &&
            LooksLikeUserPtrFast(ta)) {
            s->time_act = ta;
            SafeRead<uint32_t>(ta + Off::TIME_ACT_ANIM_ID, &s->anim_id);
            SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_0, &s->anim_time[0]);
            SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_1, &s->anim_time[1]);
            SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_2, &s->anim_time[2]);
            SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_3, &s->anim_time[3]);

            // v6.2 path-B: anim_queue[read_idx].anim_id (vswarte CSChrTimeActModuleAnim model).
            // anim_queue at +0x20 is 10 × 16 bytes; entry layout: i32 anim_id @+0,
            // f32 play_time @+4, f32 play_time2 @+8, f32 anim_length @+C.
            // read_idx is u32 at +0xC4. Bound-check before deref.
            uint32_t ridx = 0xFFFFFFFFu;
            if (SafeRead<uint32_t>(ta + Off::TIME_ACT_READ_IDX, &ridx)) {
                s->read_idx = ridx;
                if (ridx < (uint32_t)Off::TIME_ACT_ANIM_QUEUE_LEN) {
                    int entryOff = Off::TIME_ACT_ANIM_QUEUE_BEGIN +
                                   (int)ridx * Off::TIME_ACT_ANIM_QUEUE_STRIDE;
                    SafeRead<uint32_t>(ta + entryOff, &s->anim_id_path_b);
                }
            }
        }

        // v6.2 path-C: module_bag → +0x80 (ActionRequest) → +0x90 (enemy anim).
        uintptr_t ar = 0;
        if (SafeRead<uintptr_t>(bag + Off::MODULE_BAG_ACTION_REQ_PTR, &ar) &&
            LooksLikeUserPtrFast(ar)) {
            s->action_request = ar;
            SafeRead<uint32_t>(ar + Off::ACTION_REQ_ANIM_ID, &s->anim_id_path_c);
        }

        // v6.2 world-pos new chain: module_bag → +0x68 (CSChrPhysicsModule) → +0x70 (Vector3).
        uintptr_t phys = 0;
        if (SafeRead<uintptr_t>(bag + Off::MODULE_BAG_PHYS_PTR, &phys) &&
            LooksLikeUserPtrFast(phys)) {
            s->phys_module = phys;
            SafeRead<float>(phys + Off::PHYS_MODULE_POS_X, &s->world_pos_phys[0]);
            SafeRead<float>(phys + Off::PHYS_MODULE_POS_Y, &s->world_pos_phys[1]);
            SafeRead<float>(phys + Off::PHYS_MODULE_POS_Z, &s->world_pos_phys[2]);
        }
    }

    // Walk ai bag → ai struct.
    uintptr_t aibag = 0;
    if (SafeRead<uintptr_t>(chr_ins_abs + Off::CHR_INS_AI_STRUCT_BASE, &aibag) &&
        LooksLikeUserPtrFast(aibag)) {
        s->ai_bag = aibag;   // v7.0: stash for AI_BAG_HEAD region
        uintptr_t ai = 0;
        if (SafeRead<uintptr_t>(aibag + Off::AI_BAG_AI_STRUCT_PTR, &ai) &&
            LooksLikeUserPtrFast(ai)) {
            s->ai_struct = ai;
        }
    }

    return true;
}

// Write Tier 3 regions for one enemy. Honors writer cap; on overflow returns
// the count of regions actually written. Caller patches the count in the
// EnemyHeader.
static uint8_t WriteTier3ForEnemy(Writer& w,
                                  const EnemySnapshot& s,
                                  bool include_focus_emphasis,
                                  bool drop_broad_sweep,
                                  uintptr_t player_chr_ins_for_region15)
{
    if (drop_broad_sweep) return 0;
    if (s.chr_ins_abs == 0) return 0;
    uint8_t count = 0;

    // Region 0: chr_ins_root (0..0x800)
    if (WriteRegionRecord(w, REGION_CHR_INS_ROOT,
                          s.chr_ins_abs, 0u,
                          0, REGION_CAP_CHR_INS_ROOT,
                          false, 0)) ++count;
    else return count;

    // Region 1: module_bag first 0x200
    if (s.module_bag) {
        if (WriteRegionRecord(w, REGION_MODULE_BAG,
                              s.module_bag, 1u,
                              0, REGION_CAP_MODULE_BAG,
                              false, 0)) ++count;
        else return count;
    }

    // Region 2: time_act_module first 0x2000
    if (s.time_act) {
        if (WriteRegionRecord(w, REGION_TIME_ACT_MODULE,
                              s.time_act, 2u,
                              0, REGION_CAP_TIME_ACT,
                              false, 0)) ++count;
        else return count;

        // Region 3: time_act_focus (+0xC0..+0xE0) — emphasized 32 bytes
        if (include_focus_emphasis) {
            if (WriteRegionRecord(w, REGION_TIME_ACT_FOCUS,
                                  s.time_act, 3u,
                                  Off::TIME_ACT_FOCUS_BEGIN,
                                  REGION_CAP_TIME_ACT_FOCUS,
                                  false, 0)) ++count;
            else return count;
        }

        // Region 4: time_act_child — scan first TIME_ACT_CHILD_SCAN_BYTES of
        // TimeAct for pointer-shaped qwords; capture each child's first 0x100.
        // STRICT: source-offset 8-aligned, target 8-aligned + LooksLikeUserPtrFast.
        // Cap TIME_ACT_CHILD_MAX = 8.
        int childN = 0;
        for (int off = 0; off < TIME_ACT_CHILD_SCAN_BYTES &&
                          childN < TIME_ACT_CHILD_MAX; off += 8) {
            uintptr_t target = 0;
            if (!SafeRead<uintptr_t>(s.time_act + off, &target)) continue;
            if ((target & 0x7) != 0) continue;           // strict align
            if (!LooksLikeUserPtrFast(target)) continue;
            if (WriteRegionRecord(w, REGION_TIME_ACT_CHILD,
                                  target, 4u,
                                  0, REGION_CAP_TIME_ACT_CHILD,
                                  true, (uint16_t)off)) {
                ++count;
                ++childN;
            } else {
                return count;  // writer full
            }
        }
    }

    // Region 5: ai_struct (+0xE000..+0xF000) — legacy tail territory
    if (s.ai_struct) {
        if (WriteRegionRecord(w, REGION_AI_STRUCT,
                              s.ai_struct, 5u,
                              Off::AI_STRUCT_REGION_BEGIN,
                              REGION_CAP_AI_STRUCT,
                              false, 0)) ++count;
        else return count;
    }

    // v6.4: regions 6/7/8/9 are DROPPED. They were instrumentation for
    // research-006/007 and their purpose is done. Region IDs stay reserved
    // in the enum so old captures still parse correctly. The values they
    // captured (phys pos, anim candidates B/C, module pointer absolutes)
    // are still in the v6.2 header block on every focused row, so future
    // re-analysis can still work with that header data.

    // v7.0 target-of-attention research regions (Phase 4.0 Gate 0.B):
    // Capture wide windows around AI struct head, AI bag head, and module bag
    // head. Offline analyzer scans for the player's ChrIns* pattern (which is
    // already in the sample header at player_chr_ins_abs) inside these regions
    // to rank candidate target-of-attention offsets. Read-only; no new chains.
    // Emitted only for focused enemies (boss-bar / lock-on / nearest in
    // qualification mode) — the broader roster doesn't justify the per-sample
    // byte budget. include_focus_emphasis is the focused-enemy gate elsewhere
    // in this function (see TIME_ACT_FOCUS); reuse it.
    if (include_focus_emphasis) {
        // Region 10: ai_bag head (+0x000..+0x400)
        if (s.ai_bag) {
            if (WriteRegionRecord(w, REGION_AI_BAG_HEAD,
                                  s.ai_bag, 10u,
                                  0, REGION_CAP_AI_BAG_HEAD,
                                  false, 0)) ++count;
            else return count;
        }
        // Region 11: ai_struct head (+0x000..+0x1000) — PRIMARY target territory
        if (s.ai_struct) {
            if (WriteRegionRecord(w, REGION_AI_STRUCT_HEAD,
                                  s.ai_struct, 11u,
                                  0, REGION_CAP_AI_STRUCT_HEAD,
                                  false, 0)) ++count;
            else return count;
        }
        // Region 12: module_bag head (+0x000..+0x100)
        if (s.module_bag) {
            if (WriteRegionRecord(w, REGION_MODULE_BAG_HEAD,
                                  s.module_bag, 12u,
                                  0, REGION_CAP_MODULE_BAG_HEAD,
                                  false, 0)) ++count;
            else return count;
        }
        // v7.2 expanded territory:
        // Region 13: ai_struct mid-range (+0x1000..+0x4000) — broader target
        // hunting. struct_offset in the analyzer = payload_offset + scan_off,
        // so a hit at scan_off=0x10 here resolves to ai_struct+0x1010.
        if (s.ai_struct) {
            if (WriteRegionRecord(w, REGION_AI_STRUCT_MID,
                                  s.ai_struct, 13u,
                                  0x1000, REGION_CAP_AI_STRUCT_MID,
                                  false, 0)) ++count;
            else return count;
        }
        // Region 14: ActionRequest module body head — known module that may
        // own a target-of-attention field. ActionRequest pointer is already
        // walked at probe.cpp:1556-1563 (path-C anim_id) and stashed in
        // s.action_request.
        if (s.action_request) {
            if (WriteRegionRecord(w, REGION_ACTION_REQ_HEAD,
                                  s.action_request, 14u,
                                  0, REGION_CAP_ACTION_REQ_HEAD,
                                  false, 0)) ++count;
            else return count;
        }
        // Region 15: PLAYER ChrIns root (sample-scoped, emitted under the
        // focused enemy's record for wire-format compatibility). Provides
        // context for identifying handle-shaped neighbors of Josh's
        // friendly summons / NPC allies. Threaded through the call signature
        // because the player ChrIns is only resolved in the detour's outer
        // scope (not on the per-enemy snapshot).
        if (player_chr_ins_for_region15) {
            if (WriteRegionRecord(w, REGION_PLAYER_CHR_INS,
                                  player_chr_ins_for_region15, 15u,
                                  0, REGION_CAP_PLAYER_CHR_INS,
                                  false, 0)) ++count;
            else return count;
        }
        // v7.3: deep AI struct scan. Three regions cover the previously-
        // unscanned gap between +0x4000 (where region 13 ai_struct_mid ends)
        // and +0xE000 (where region 5 legacy tail begins). The +0xC000..0xE000
        // region is the highest-value: TarnishedTool names TargetingSystem at
        // +0xC480 and SpEffectObserveComp at +0xDBF0. Ordered LAST so any
        // per-sample writer overflow drops new exploratory data first.
        if (s.ai_struct) {
            if (WriteRegionRecord(w, REGION_AI_STRUCT_FAR,
                                  s.ai_struct, 16u,
                                  0x4000, REGION_CAP_AI_STRUCT_FAR,
                                  false, 0)) ++count;
            else return count;
            if (WriteRegionRecord(w, REGION_AI_STRUCT_DEEP,
                                  s.ai_struct, 17u,
                                  0x8000, REGION_CAP_AI_STRUCT_DEEP,
                                  false, 0)) ++count;
            else return count;
            if (WriteRegionRecord(w, REGION_AI_STRUCT_TGT,
                                  s.ai_struct, 18u,
                                  0xC000, REGION_CAP_AI_STRUCT_TGT,
                                  false, 0)) ++count;
            else return count;
        }
        // v7.3: re-enable v6.3's REGION_MODULE_BAG_MEMBER. Scans the first
        // MODULE_BAG_MEMBER_SCAN_BYTES of the module bag head at 8-byte stride
        // for valid pointers, captures each module's body (up to 0x200 bytes
        // each, MODULE_BAG_MEMBER_MAX modules total). The AI module at
        // module_bag +0x38 is the prime suspect for the target field; this
        // captures its body so the analyzer can inspect its contents.
        if (s.module_bag) {
            int memberN = 0;
            for (int scan_off = 0;
                 scan_off < MODULE_BAG_MEMBER_SCAN_BYTES &&
                 memberN < MODULE_BAG_MEMBER_MAX;
                 scan_off += 8)
            {
                uintptr_t mod_ptr = 0;
                if (!SafeRead<uintptr_t>(s.module_bag + scan_off, &mod_ptr)) continue;
                if ((mod_ptr & 0x7) != 0) continue;
                if (!LooksLikeUserPtrFast(mod_ptr)) continue;
                if (WriteRegionRecord(w, REGION_MODULE_BAG_MEMBER,
                                      mod_ptr, 9u,
                                      0, REGION_CAP_MODULE_BAG_MEMBER,
                                      /*has_child_offset=*/true,
                                      (uint16_t)scan_off)) {
                    ++count;
                    ++memberN;
                } else {
                    return count;  // writer full
                }
            }
        }
    }

    return count;
}

// Helper: write a single enemy's full record (header + Tier 3) into the
// writer. Returns true if the EnemyHeader was written; false if the writer
// overflowed before the header could fit (in which case nothing is appended).
// On Tier 3 truncation mid-enemy, the EnemyHeader's region count reflects
// what was actually written.
static bool WriteEnemyRecord(Writer& w,
                             const EnemySnapshot& s,
                             bool include_tier3,
                             bool include_focus_emphasis,
                             bool drop_broad_sweep,
                             uintptr_t player_chr_ins_for_region15)
{
    // Reserve EnemyHeader: 136 bytes in schema v2 (96 legacy + 40 v6.2 block).
    constexpr size_t ENEMY_HEADER_BYTES = 136;
    if (w.pos + ENEMY_HEADER_BYTES > w.cap) { w.overflow = true; return false; }

    size_t startMark = w.pos;
    w.put_u64((uint64_t)s.chr_ins_abs);
    w.put_u64(s.handle);
    w.put_u32(s.field_at_0x038);
    w.put_u32(s.field_at_0x060);
    w.put_u32(s.field_at_0x064);
    w.put_u32(s.field_at_0x068);
    w.put_u32(s.field_at_0x06C);
    w.put_u32(s.field_at_0x080);
    w.put_u32(s.field_at_0x1E8);
    w.put_u32(s.anim_id);
    w.put_f32(s.anim_time[0]);
    w.put_f32(s.anim_time[1]);
    w.put_f32(s.anim_time[2]);
    w.put_f32(s.anim_time[3]);
    w.put_u8(s.in_lock_on  ? 1 : 0);
    w.put_u8(s.in_boss_bar ? 1 : 0);
    w.put_u8(s.in_roster   ? 1 : 0);
    w.put_u8((uint8_t)s.cls);
    w.put_u8(s.is_focused ? 1 : 0);
    w.put_u8((uint8_t)s.focus_reason);
    size_t regionCountMark = w.pos;
    w.put_u8(0);   // region_count_for_this_enemy — patched below
    w.put_u8(0);   // reserved
    // v6.2 instrumentation block (44 bytes; pad below brings total to 64 = 136 - 72 legacy):
    w.put_u32(s.anim_id_path_b);                 // 4 — vswarte anim_queue[read_idx].anim_id
    w.put_u32(s.read_idx);                       // 4 — u32 index used; 0xFFFFFFFF = not read
    w.put_u32(s.anim_id_path_c);                 // 4 — Erd-Tools ActionRequest+0x90
    w.put_u32(0);                                // 4 — reserved (align to 8)
    w.put_u64((uint64_t)s.action_request);       // 8 — abs addr of ActionRequest module
    w.put_u64((uint64_t)s.phys_module);          // 8 — abs addr of CSChrPhysicsModule
    w.put_f32(s.world_pos_phys[0]);              // 4 — phys-chain pos
    w.put_f32(s.world_pos_phys[1]);              // 4
    w.put_f32(s.world_pos_phys[2]);              // 4 — total 8+4+4+4+4+8+8+4+4+4 = 56? Let me recount carefully.
    // Recount of v6.2 block writes: u32(4)+u32(4)+u32(4)+u32(4)+u64(8)+u64(8)+f32(4)+f32(4)+f32(4) = 44.
    // I want 40 bytes exactly. Drop the second reserved u32.
    // Actually corrected: 4+4+4+8+8+4+4+4 = 40. Above writes one extra u32. Pad to 8-aligned.
    // 40 bytes total for v6.2 block is fine. Sanity below.
    // Sanity: ENEMY_HEADER_BYTES math —
    //   legacy: 8+8+(7*4)+4+(4*4)+(6*1)+(2*1) = 72 bytes
    //   v6.2 block above: u32*3 + u32_reserved + u64*2 + f32*3 = 12+4+16+12 = 44 bytes
    //   total written so far = 72 + 44 = 116
    //   target = 136 → PAD = 20
    constexpr size_t WRITTEN_LEGACY = 8 + 8 + 7*4 + 4 + 4*4 + 6 + 2;
    static_assert(WRITTEN_LEGACY == 72, "enemy header legacy field math drifted");
    constexpr size_t WRITTEN_V62 = 4 + 4 + 4 + 4 + 8 + 8 + 4 + 4 + 4;
    static_assert(WRITTEN_V62 == 44, "v6.2 enemy block field math drifted");
    constexpr size_t WRITTEN = WRITTEN_LEGACY + WRITTEN_V62;
    constexpr size_t PAD = ENEMY_HEADER_BYTES - WRITTEN;
    static_assert(PAD == 20, "v6.2 enemy header pad math drifted");
    // v6.2 reserved-byte contract (the 20 PAD bytes at end of EnemyHeader):
    //   bytes 0..7   reserved for v6.3 anim_id_path_d (u32) + reserved (u32)
    //                  — placeholder if a fourth anim source surfaces
    //   bytes 8..15  reserved for v6.3 lock_on_legacy_chr_target (u64)
    //                  — only if v6.2 capture reveals enemies need their own
    //                    lock-on/threat field captured per-row
    //   bytes 16..19 reserved for v6.3 extra flags (u32)
    // Future evolutions should consume contiguously from byte 0 of the pad
    // and document any new claim with a static_assert that the consumed
    // bytes <= 20. Schema MUST bump if any pad byte is repurposed.
    if (w.pos + PAD > w.cap) { w.overflow = true; w.pos = startMark; return false; }
    memset(w.buf + w.pos, 0, PAD);
    w.pos += PAD;

    // Tier 3 regions (if eligible).
    if (include_tier3) {
        uint8_t rc = WriteTier3ForEnemy(w, s, include_focus_emphasis,
                                        drop_broad_sweep,
                                        player_chr_ins_for_region15);
        w.patch_u8(regionCountMark, rc);
    }
    return true;
}

// ===========================================================================
// Top-level sample capture (called from detour, in game thread)
// ===========================================================================

// (No GetChrInsFromHandle wrapper here — boss-bar enemies are matched by
// handle in the roster sweep below. Init-time roster check 5 has the only
// call site that needs the function pointer directly.)

// Check whether the producer-side emergency-drop should fire next sample.
// Spec: free_pool < 4 for >=200ms ⇒ drop broad sweep; reset when free_pool > 16.
static void UpdateEmergencyDropState(int64_t now_ms) {
    int free = FreePoolSize();
    bool active = g_emergencyDropActive.load(std::memory_order_relaxed);
    if (free > 16) {
        if (active) g_emergencyDropActive.store(false, std::memory_order_relaxed);
        g_lowFreePoolSinceMs.store(0, std::memory_order_relaxed);
        return;
    }
    if (free < 4) {
        int64_t since = g_lowFreePoolSinceMs.load(std::memory_order_relaxed);
        if (since == 0) {
            g_lowFreePoolSinceMs.store(now_ms, std::memory_order_relaxed);
        } else if ((now_ms - since) >= 200) {
            if (!active) {
                g_emergencyDropActive.store(true, std::memory_order_relaxed);
                g_dropProducerEmerg.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } else {
        // 4 <= free <= 16: stay in current state but reset the low-streak timer.
        g_lowFreePoolSinceMs.store(0, std::memory_order_relaxed);
    }
}

// Decide whether this enemy gets Tier 3 this sample.
// Focused: every tick.
// Top-tier: every DECIM_TOP_TIER3 ticks (with phase stagger). In `discovery`
//   mode only — qualification only emits Tier 3 on the focused enemy; smoke
//   never emits Tier 3.
// Lesser: every DECIM_LESSER_T3 ticks (with phase stagger). discovery only.
// Adaptive stepdown:
//   step 1: top T3 cadence 5 Hz → DECIM_TOP_TIER3 effective doubles (×2 = 18).
//   step 2: top cap drops from 8 to 4.
//   step 3: top T3 cadence drops to 2 Hz (×9 = 81 effective).
static bool ShouldEmitTier3(const EnemyState& es,
                            uint64_t tick,
                            CaptureMode mode,
                            int adaptiveStep)
{
    if (mode == MODE_SMOKE) return false;
    if (es.is_focused) return true;
    if (mode == MODE_QUALIFICATION) return false;   // discovery only past here
    uint32_t N;
    if (es.cls == ENEMY_CLASS_TOP) {
        // 90 Hz hook rate: N=9→10 Hz, N=18→5 Hz, N=45→2 Hz.
        if (adaptiveStep >= 3)      N = 45;                    // 2 Hz
        else if (adaptiveStep >= 1) N = DECIM_TOP_TIER3 * 2;   // 5 Hz (18)
        else                        N = DECIM_TOP_TIER3;       // 10 Hz (9)
    } else {
        // LESSER
        N = DECIM_LESSER_T3;
    }
    return ((tick + es.phase) % N) == 0;
}

// Lesser-tier Tier 1+2 decimation: 10 Hz fixed (spec doesn't lower lesser
// under adaptive stepdown — only top-tier rates and cap change).
static bool ShouldEmitLesserT12(const EnemyState& es, uint64_t tick) {
    return ((tick + es.phase) % DECIM_LESSER_T12) == 0;
}

// Main per-tick capture. Runs in detour. Must NOT compute deltas or do any
// non-trivial work. SafeRead + memcpy + atomic queue push only.
static void SampleOnce(uint64_t tick, int64_t qpcStart) {
    if (!g_refs.ready || !g_poolReady.load()) return;

    uint64_t seq = g_sampleSeq.fetch_add(1, std::memory_order_relaxed) + 1;
    (void)seq;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t now_ms_abs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    // ts_ms_rel is RELATIVE to session start (per spec field name).
    int64_t now_ms = (g_sessionStartMs > 0) ? (now_ms_abs - g_sessionStartMs) : 0;

    UpdateEmergencyDropState(now_ms_abs);     // emergency timer uses absolute clock
    bool drop_broad_sweep = g_emergencyDropActive.load(std::memory_order_relaxed);

    int slot = ProducerClaim();
    if (slot < 0) return;            // pool full; sample dropped (counter bumped)

    PoolBuffer& pb = g_pool[slot];
    pb.frame = tick;
    pb.ts_ms_rel = (uint64_t)now_ms;
    pb.mode_at_capture = (uint8_t)g_cfg.mode;
    pb.truncated = false;
    pb.len = 0;

    Writer w;
    w.buf = pb.data;
    w.cap = MAX_SAMPLE_BYTES;
    w.pos = 0;

    // ----- SampleHeader -----
    w.put_u32(SAMPLE_MAGIC);
    w.put_u32(PROBE_SCHEMA_VERSION);
    w.put_u64(tick);
    w.put_u64((uint64_t)now_ms);
    w.put_u8((uint8_t)g_cfg.mode);
    size_t truncatedMark = w.mark();
    w.put_u8(0);                                 // truncated — patched at end
    uint8_t reserved6[6] = {0,0,0,0,0,0};
    w.put(reserved6, 6);

    // Read WCM*
    uintptr_t wcm = 0;
    if (!SafeRead<uintptr_t>(g_refs.wcmPtrAddr, &wcm) || !LooksLikeUserPtrFast(wcm)) {
        // Game state not ready yet; emit a minimal sample so the worker still
        // sees a row and we know the hook is alive.
        w.put_u64(0);
        w.put_u64((uint64_t)g_refs.er_module_base);
        w.put_u64(0);
        w.put_u32(0);
        for (int i = 0; i < 4; ++i) w.put_f32(0.0f);
        for (int i = 0; i < 3; ++i) w.put_f32(0.0f);
        w.put_u64(0xFFFFFFFFFFFFFFFFull);
        // v6.2 Tier 1 player instrumentation block (48 bytes, all-zero on short sample):
        w.put_u64(0);                            // playerChrInsVtable
        for (int i = 0; i < 3; ++i) w.put_f32(0.0f);  // playerPosPhys
        w.put_u64(0);                            // playerPhysModule
        w.put_u64(0xFFFFFFFFFFFFFFFFull);        // playerLockHandleNew
        w.put_u32(0xFFFFFFFFu);                  // playerLockAreaNew
        w.put_u64(0);                            // v7.1: playerHandle (zero when player not resolved)
        for (int i = 0; i < 3; ++i) w.put_u64(0xFFFFFFFFFFFFFFFFull);
        w.put_u64(0);                            // focused_handle
        w.put_u8((uint8_t)FOCUS_NONE);
        w.put_u8(0);                             // enemy_record_count
        w.put_u8((uint8_t)g_adaptiveStep.load(std::memory_order_relaxed));
        w.put_u8(0);                             // reserved2
        pb.len = w.pos;
        ProducerCommit();
        return;
    }
    w.put_u64((uint64_t)wcm);
    w.put_u64((uint64_t)g_refs.er_module_base);

    // ----- Resolve player slot 0 (local player) AND all other WCM_PLAYER
    // slots so the roster sweep can exclude friendlies in co-op sessions.
    // v6.4 co-op safety: when Josh plays Seamless Coop with up to 5 friends,
    // those friendlies are in the roster span — without exclusion,
    // focus_reason=qualification_nearest will latch onto whoever's closest,
    // not the boss. The boss-bar path still picks bosses via
    // boss_bar_handles[]; this is only for the nearest-fallback case.
    uintptr_t playerSlotAddr = wcm + Off::WCM_PLAYER_ARRAY;
    uintptr_t playerSlot = 0;
    SafeRead<uintptr_t>(playerSlotAddr, &playerSlot);

    uintptr_t playerChrIns = 0;
    if (LooksLikeUserPtrFast(playerSlot)) {
        SafeRead<uintptr_t>(playerSlot, &playerChrIns);
        if (!LooksLikeUserPtrFast(playerChrIns)) playerChrIns = 0;
    }
    w.put_u64((uint64_t)playerChrIns);

    // Collect ALL valid player ChrIns* from the WCM_PLAYER_ARRAY (and the
    // Seamless Coop extension that may follow it) for friendly-exclusion
    // during the roster sweep. We scan FRIENDLY_SCAN_SLOTS = 8 slots: 4
    // for the base game's WCM_PLAYER_ARRAY + up to 4 more for the Seamless
    // extension that's known to exist past it. Every slot is validated via
    // LooksLikeUserPtrFast, so non-player slots (unused or out-of-bounds)
    // are safely ignored. De-dup against same chr_ins. If Seamless ever
    // extends past 8 slots, this needs to grow — but for tonight's 6-player
    // lobby this comfortably covers Josh + 5 friends.
    constexpr int FRIENDLY_SCAN_SLOTS = 8;
    uintptr_t friendlyPCs[FRIENDLY_SCAN_SLOTS] = {0};
    int friendlyCount = 0;
    for (int slot = 0; slot < FRIENDLY_SCAN_SLOTS; ++slot) {
        uintptr_t slotAddr = wcm + Off::WCM_PLAYER_ARRAY +
                             (uintptr_t)slot * sizeof(uintptr_t);
        uintptr_t slotPtr = 0;
        if (!SafeRead<uintptr_t>(slotAddr, &slotPtr)) continue;
        if (!LooksLikeUserPtrFast(slotPtr)) continue;
        uintptr_t friendlyChr = 0;
        if (!SafeRead<uintptr_t>(slotPtr, &friendlyChr)) continue;
        if (!LooksLikeUserPtrFast(friendlyChr)) continue;
        // v7.3: de-dup against OTHER friendly slots we already collected,
        // but DO include the local player. Previously this skipped the
        // player ("already if matches playerChrIns"), which broke the
        // downstream qualification-nearest exclusion check at line ~2313:
        // the player wasn't in friendlyPCs[], so `chrIns == friendlyPCs[j]`
        // never matched the player, and qualification-nearest picked Josh
        // as the "nearest enemy" in 21% of v7.2 samples (player vs self
        // has distance 0). Including the local player in friendlyPCs[]
        // makes the existing exclusion check correctly skip Josh too.
        bool already = false;
        for (int j = 0; j < friendlyCount; ++j) {
            if (friendlyPCs[j] == friendlyChr) { already = true; break; }
        }
        if (already) continue;
        friendlyPCs[friendlyCount++] = friendlyChr;
    }

    uint32_t playerAnimId = 0;
    float playerAnimTime[4] = {0,0,0,0};
    float playerPos[3] = {0,0,0};
    uint64_t playerLockHandle = 0xFFFFFFFFFFFFFFFFull;
    // v6.2 instrumentation locals:
    uint64_t playerChrInsVtable = 0;
    float playerPosPhys[3] = {0,0,0};
    uint64_t playerPhysModule = 0;
    uint64_t playerLockHandleNew = 0xFFFFFFFFFFFFFFFFull;
    uint32_t playerLockAreaNew = 0xFFFFFFFFu;

    // v7.1: read player's own FieldInsHandle at PlayerIns +0x08, used by
    // analyze_target_field.py to scan boss AI struct for the player handle
    // (the v7.0 analyzer found zero matches against player_chr_ins pointer,
    // confirming the target field stores a handle rather than a pointer).
    uint64_t playerHandle = 0;
    if (playerChrIns) {
        // Walk module bag → TimeAct
        uintptr_t bag = 0;
        if (SafeRead<uintptr_t>(playerChrIns + Off::CHR_INS_MODULE_BAG_PTR, &bag) &&
            LooksLikeUserPtrFast(bag)) {
            uintptr_t ta = 0;
            if (SafeRead<uintptr_t>(bag + Off::MODULE_BAG_TIME_ACT_PTR, &ta) &&
                LooksLikeUserPtrFast(ta)) {
                SafeRead<uint32_t>(ta + Off::TIME_ACT_ANIM_ID, &playerAnimId);
                SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_0, &playerAnimTime[0]);
                SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_1, &playerAnimTime[1]);
                SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_2, &playerAnimTime[2]);
                SafeRead<float>(ta + Off::TIME_ACT_TIME_CAND_3, &playerAnimTime[3]);
            }
            // v6.2: physics module → world pos
            uintptr_t phys = 0;
            if (SafeRead<uintptr_t>(bag + Off::MODULE_BAG_PHYS_PTR, &phys) &&
                LooksLikeUserPtrFast(phys)) {
                playerPhysModule = (uint64_t)phys;
                SafeRead<float>(phys + Off::PHYS_MODULE_POS_X, &playerPosPhys[0]);
                SafeRead<float>(phys + Off::PHYS_MODULE_POS_Y, &playerPosPhys[1]);
                SafeRead<float>(phys + Off::PHYS_MODULE_POS_Z, &playerPosPhys[2]);
            }
        }
        SafeRead<float>(playerChrIns + Off::PLAYER_INS_POS_X, &playerPos[0]);
        SafeRead<float>(playerChrIns + Off::PLAYER_INS_POS_Y, &playerPos[1]);
        SafeRead<float>(playerChrIns + Off::PLAYER_INS_POS_Z, &playerPos[2]);
        SafeRead<uint64_t>(playerChrIns + Off::CHR_INS_TARGET_HANDLE, &playerLockHandle);
        // v6.2: vtable + new lock-on candidates
        SafeRead<uint64_t>(playerChrIns + Off::CHR_INS_VTABLE_PTR, &playerChrInsVtable);
        SafeRead<uint64_t>(playerChrIns + Off::PLAYER_INS_LOCK_HANDLE_NEW, &playerLockHandleNew);
        SafeRead<uint32_t>(playerChrIns + Off::PLAYER_INS_LOCK_AREA_NEW, &playerLockAreaNew);
        // v7.1: player's own FieldInsHandle (at ChrIns+0x08), for target-field
        // analyzer to scan boss AI struct for handle-shaped matches.
        SafeRead<uint64_t>(playerChrIns + Off::CHR_INS_HANDLE, &playerHandle);
    }

    w.put_u32(playerAnimId);
    for (int i = 0; i < 4; ++i) w.put_f32(playerAnimTime[i]);
    for (int i = 0; i < 3; ++i) w.put_f32(playerPos[i]);
    w.put_u64(playerLockHandle);
    // v6.2 Tier 1 player instrumentation block (schema v2 only — 48 bytes):
    w.put_u64(playerChrInsVtable);     // 8 bytes — vtable for PlayerIns* vs ChrIns* discriminator
    for (int i = 0; i < 3; ++i) w.put_f32(playerPosPhys[i]);  // 12 bytes — world pos via phys chain
    w.put_u64(playerPhysModule);        // 8 bytes — absolute address of phys module (for region cross-ref)
    w.put_u64(playerLockHandleNew);     // 8 bytes — lock-on at +0x6B0
    w.put_u32(playerLockAreaNew);       // 4 bytes — TargetArea at +0x6B4
    // v7.1: repurpose the v6.2-reserved 8-byte slot as the player's own
    // FieldInsHandle (at PlayerIns+0x08). Old v6.4/v7.0 captures wrote 0
    // here, which probe_bin.py already accepts as the reserved field, so
    // the wire format change is forward-compatible.
    w.put_u64(playerHandle);            // 8 bytes — player's own FieldInsHandle

    // v6.3: derive an EFFECTIVE lock-on handle for analytics + focus-selection.
    // Research 007 proved +0x6A0 is dead (1 distinct value, 0 transitions in
    // 8,773 samples) while +0x6B0 tracks reality (17 transitions correlating
    // with intentional lock-on toggles). Prefer +0x6B0 when valid; fall back
    // to +0x6A0 (with a sentinel check) only as belt-and-braces in case the
    // +0x6B0 read returns wild data on some future game state we haven't
    // observed.
    uint64_t playerLockHandleEffective = 0xFFFFFFFFFFFFFFFFull;
    if (playerLockHandleNew != 0xFFFFFFFFFFFFFFFFull && playerLockHandleNew != 0) {
        playerLockHandleEffective = playerLockHandleNew;
    }

    // ----- Boss-bar handles -----
    uint64_t bossHandles[3] = {
        0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull
    };
    if (g_refs.csFeManPtrAddr) {
        uintptr_t feMan = 0;
        if (SafeRead<uintptr_t>(g_refs.csFeManPtrAddr, &feMan) &&
            LooksLikeUserPtrFast(feMan)) {
            for (int b = 0; b < Off::CS_FE_MAN_BOSS_BAR_COUNT; ++b) {
                uintptr_t slotAddr = feMan + Off::CS_FE_MAN_BOSS_BARS_BASE +
                                     b * Off::CS_FE_MAN_BOSS_BAR_STRIDE +
                                     Off::CS_FE_MAN_BOSS_BAR_HANDLE;
                uint64_t h = 0xFFFFFFFFFFFFFFFFull;
                SafeRead<uint64_t>(slotAddr, &h);
                bossHandles[b] = h;
            }
        }
    }
    for (int b = 0; b < 3; ++b) w.put_u64(bossHandles[b]);

    // ----- Resolve focused enemy + boss-bar enemies + roster enemies -----
    //
    // Strategy: walk the roster span (when enabled) to collect candidate
    // ChrIns* entries with their handles + positions. Boss-bar enemies and
    // the lock-on target are recognized by their handles in this list. If
    // the roster is disabled OR a target handle isn't in the roster, that
    // enemy is referenced only in Tier 1 (boss_bar_handles[]) — no Tier
    // 2/3 record for it this sample. The analysis tool can still see the
    // handle was present.
    EnemySnapshot snaps[MAX_TRACKED_ENEMIES];
    int nEnemies = 0;
    uint64_t focusedHandle = 0;
    FocusedReason focusReason = FOCUS_NONE;

    // Pass 1: Roster sweep (when enabled). Walk the begin/end span; for each
    // chrIns, read its handle and see if it's "interesting" (matches a boss
    // bar handle or the player's lock-on, OR is anywhere in the roster — in
    // which case we pick by distance later).
    // We collect ALL roster chrIns into a working list, then rank.
    //
    // We do TWO passes:
    //  (a) priority pass: scan for boss-bar + lock-on handles first so they
    //      are guaranteed to land in work[] even on a large roster.
    //  (b) fill pass: fill remaining work[] slots with whatever else is in
    //      the roster (for distance ranking → top-tier nearest enemies).
    struct WorkEntry { uintptr_t chrIns; uint64_t handle; uint64_t handleFieldAddr; float dx, dy, dz; bool inBossBar; bool isLockedOn; };
    constexpr int WORK_CAP = 64;
    WorkEntry work[WORK_CAP];
    int nWork = 0;

    if (g_roster.enabled) {
        uintptr_t beginAddr = wcm + Off::WCM_ROSTER_BEGIN;
        uintptr_t endAddr   = wcm + Off::WCM_ROSTER_END;
        uintptr_t begin = 0, end = 0;
        SafeRead<uintptr_t>(beginAddr, &begin);
        SafeRead<uintptr_t>(endAddr, &end);
        if (begin && end && end >= begin) {
            uintptr_t span = end - begin;
            int count = (int)(span / sizeof(uintptr_t));
            // No cap on `count` here — boss/lock priority pass scans the
            // entire span, then the fill pass adds nearest enemies up to
            // the work[] cap.

            // Priority pass: walk full span, only retain matches on boss
            // bars or lock-on. Stops early once all priority slots filled.
            int prioWanted = 0;
            for (int b = 0; b < 3; ++b) {
                if (bossHandles[b] != 0xFFFFFFFFFFFFFFFFull && bossHandles[b] != 0) ++prioWanted;
            }
            // v6.3: use +0x6B0 (effective) for analytics; +0x6A0 was confirmed dead.
            if (playerLockHandleEffective != 0xFFFFFFFFFFFFFFFFull && playerLockHandleEffective != 0) ++prioWanted;

            int prioFound = 0;
            for (int i = 0; i < count && prioFound < prioWanted && nWork < WORK_CAP; ++i) {
                uintptr_t slotPtr = begin + (size_t)i * sizeof(uintptr_t);
                uintptr_t chrIns = 0;
                if (!SafeRead<uintptr_t>(slotPtr, &chrIns)) continue;
                if (!LooksLikeUserPtrFast(chrIns)) continue;
                // v6.1.1: skip the player chr_ins. The WCM roster includes the
                // player as an entity (the player IS a CharaIns); without this
                // exclusion qualification_nearest picks the player as the
                // "closest enemy" because distance to self is zero.
                // v6.4 co-op extension: also skip all OTHER friendly PCs
                // collected from WCM_PLAYER_ARRAY (up to PLAYER_ARRAY_LEN = 4
                // slots; covers Josh + 3 friends; Seamless Coop sessions
                // with more friends may need PLAYER_ARRAY_LEN bumped).
                {
                    bool isFriendly = false;
                    for (int j = 0; j < friendlyCount; ++j) {
                        if (chrIns == friendlyPCs[j]) { isFriendly = true; break; }
                    }
                    if (isFriendly) continue;
                }
                uintptr_t handleField = chrIns + Off::CHR_INS_HANDLE;
                uint64_t h = 0;
                if (!SafeRead<uint64_t>(handleField, &h)) continue;
                if (h == 0 || h == UINT64_MAX) continue;

                bool isPrio = false;
                bool inBoss = false;
                bool isLock = false;
                for (int b = 0; b < 3; ++b) {
                    if (bossHandles[b] == h) { isPrio = true; inBoss = true; break; }
                }
                if (h == playerLockHandleEffective && playerLockHandleEffective != 0xFFFFFFFFFFFFFFFFull) {
                    isPrio = true; isLock = true;
                }
                if (!isPrio) continue;

                WorkEntry& we = work[nWork++];
                we.chrIns = chrIns;
                we.handle = h;
                we.handleFieldAddr = handleField;
                we.inBossBar = inBoss;
                we.isLockedOn = isLock;
                float ex = 0, ey = 0, ez = 0;
                SafeRead<float>(chrIns + Off::PLAYER_INS_POS_X, &ex);
                SafeRead<float>(chrIns + Off::PLAYER_INS_POS_Y, &ey);
                SafeRead<float>(chrIns + Off::PLAYER_INS_POS_Z, &ez);
                we.dx = ex - playerPos[0];
                we.dy = ey - playerPos[1];
                we.dz = ez - playerPos[2];
                ++prioFound;

                // Runtime check 7: a boss-bar enemy appears in the roster.
                if (inBoss && !g_check7Done.load(std::memory_order_relaxed)) {
                    g_check7Done.store(true, std::memory_order_relaxed);
                }
            }

            // Fill pass: add other roster enemies up to the work[] cap.
            // Skip entries already added in priority pass (matched by handle).
            int fillCap = WORK_CAP;
            if (count < fillCap) fillCap = count;
            for (int i = 0; i < count && nWork < fillCap; ++i) {
                uintptr_t slotPtr = begin + (size_t)i * sizeof(uintptr_t);
                uintptr_t chrIns = 0;
                if (!SafeRead<uintptr_t>(slotPtr, &chrIns)) continue;
                if (!LooksLikeUserPtrFast(chrIns)) continue;
                // v6.4 co-op extension: skip all friendly PCs (was v6.1.1
                // skipping only local player). See priority-pass note above.
                {
                    bool isFriendly = false;
                    for (int j = 0; j < friendlyCount; ++j) {
                        if (chrIns == friendlyPCs[j]) { isFriendly = true; break; }
                    }
                    if (isFriendly) continue;
                }
                uintptr_t handleField = chrIns + Off::CHR_INS_HANDLE;
                uint64_t h = 0;
                if (!SafeRead<uint64_t>(handleField, &h)) continue;
                if (h == 0 || h == UINT64_MAX) continue;

                // De-dup against priority entries.
                bool already = false;
                for (int j = 0; j < nWork; ++j) {
                    if (work[j].handle == h) { already = true; break; }
                }
                if (already) continue;

                WorkEntry& we = work[nWork++];
                we.chrIns = chrIns;
                we.handle = h;
                we.handleFieldAddr = handleField;
                we.inBossBar = false;
                we.isLockedOn = false;

                // Position for distance ranking.
                float ex = 0, ey = 0, ez = 0;
                SafeRead<float>(chrIns + Off::PLAYER_INS_POS_X, &ex);
                SafeRead<float>(chrIns + Off::PLAYER_INS_POS_Y, &ey);
                SafeRead<float>(chrIns + Off::PLAYER_INS_POS_Z, &ez);
                we.dx = ex - playerPos[0];
                we.dy = ey - playerPos[1];
                we.dz = ez - playerPos[2];
            }
        }
    }
    // NOTE on roster-disabled fallback: when g_roster.enabled is false, work[]
    // is empty. Boss-bar enemies still appear in Tier 1's boss_bar_handles[]
    // but cannot be safely resolved into a chrIns* without a live struct
    // field for GetChrInsFromHandle (v5e debugging proved a stack copy of the
    // handle returns the input back). The spec's roster-fallback path
    // accepts this degraded mode; analysis tools can still see boss-bar
    // handles were present.

    // Choose focused-enemy by spec priority:
    //   1. lock-on  2. boss-bar slot 0  3. qualification: nearest  4. none
    int focusedWorkIdx = -1;
    if (playerLockHandleEffective != 0xFFFFFFFFFFFFFFFFull && playerLockHandleEffective != 0) {
        for (int i = 0; i < nWork; ++i) {
            if (work[i].handle == playerLockHandleEffective) {
                focusedWorkIdx = i;
                focusReason = FOCUS_LOCK_ON;
                break;
            }
        }
    }
    if (focusedWorkIdx == -1 && bossHandles[0] != 0xFFFFFFFFFFFFFFFFull) {
        for (int i = 0; i < nWork; ++i) {
            if (work[i].handle == bossHandles[0]) {
                focusedWorkIdx = i;
                focusReason = FOCUS_BOSS_BAR_0;
                break;
            }
        }
    }
    if (focusedWorkIdx == -1 && g_cfg.mode == MODE_QUALIFICATION && nWork > 0) {
        // Pick nearest hostile (we don't have hostility flag; use nearest).
        float bestD = 1e30f; int best = -1;
        for (int i = 0; i < nWork; ++i) {
            float d = work[i].dx*work[i].dx + work[i].dy*work[i].dy + work[i].dz*work[i].dz;
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best != -1) {
            focusedWorkIdx = best;
            focusReason = FOCUS_QUALIFICATION_NEAREST;
        }
    }

    if (focusedWorkIdx != -1) focusedHandle = work[focusedWorkIdx].handle;
    w.put_u64(focusedHandle);
    w.put_u8((uint8_t)focusReason);
    size_t enemyCountMark = w.mark();
    w.put_u8(0);  // enemy_record_count — patched after we write enemies
    w.put_u8((uint8_t)g_adaptiveStep.load(std::memory_order_relaxed));
    w.put_u8(0);  // reserved2

    // Build snaps[]: focused first (so it always fits even on truncation),
    // then other top-tier (boss bars + nearest), then lesser.
    auto snapFromWork = [&](const WorkEntry& we, EnemyClass cls,
                            bool is_focused, FocusedReason fr) -> bool {
        if (nEnemies >= g_cfg.max_enemies_tracked) return false;
        EnemySnapshot& s = snaps[nEnemies];
        s = EnemySnapshot{};
        if (!EnemyReadTier2(we.chrIns, &s)) return false;
        s.in_lock_on  = we.isLockedOn;
        s.in_boss_bar = we.inBossBar;
        s.in_roster   = true;
        s.cls         = cls;
        s.is_focused  = is_focused;
        s.focus_reason = fr;
        ++nEnemies;
        return true;
    };

    if (focusedWorkIdx != -1) {
        snapFromWork(work[focusedWorkIdx], ENEMY_CLASS_FOCUSED, true, focusReason);
    }

    // Top-tier cap (apply adaptive stepdown step 2: cap drops from 8 to 4).
    int topCap = g_cfg.top_tier_enemies;
    int adaptiveStep = g_adaptiveStep.load(std::memory_order_relaxed);
    if (adaptiveStep >= 2 && topCap > 4) topCap = 4;

    int topAdded = (focusedWorkIdx != -1) ? 1 : 0;
    // Boss bars first.
    for (int b = 0; b < 3 && topAdded < topCap; ++b) {
        uint64_t h = bossHandles[b];
        if (h == 0xFFFFFFFFFFFFFFFFull || h == 0) continue;
        if (h == focusedHandle) continue;
        for (int i = 0; i < nWork; ++i) {
            if (work[i].handle == h) {
                if (snapFromWork(work[i], ENEMY_CLASS_TOP, false, FOCUS_NONE)) ++topAdded;
                break;
            }
        }
    }
    // Then nearest roster enemies.
    // Naive selection: repeatedly pick smallest unmatched distance.
    bool used[64] = {0};
    for (int i = 0; i < nWork; ++i) {
        if (work[i].handle == focusedHandle) used[i] = true;
        if (work[i].inBossBar) used[i] = true;
    }
    while (topAdded < topCap) {
        float bestD = 1e30f; int best = -1;
        for (int i = 0; i < nWork; ++i) {
            if (used[i]) continue;
            float d = work[i].dx*work[i].dx + work[i].dy*work[i].dy + work[i].dz*work[i].dz;
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best == -1) break;
        used[best] = true;
        if (snapFromWork(work[best], ENEMY_CLASS_TOP, false, FOCUS_NONE)) ++topAdded;
    }
    // Lesser-tier: fill remaining slots up to max_enemies_tracked with the
    // next nearest. These get T1+T2 at 10 Hz / T3 at 2 Hz.
    while (nEnemies < g_cfg.max_enemies_tracked) {
        float bestD = 1e30f; int best = -1;
        for (int i = 0; i < nWork; ++i) {
            if (used[i]) continue;
            float d = work[i].dx*work[i].dx + work[i].dy*work[i].dy + work[i].dz*work[i].dz;
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best == -1) break;
        used[best] = true;
        snapFromWork(work[best], ENEMY_CLASS_LESSER, false, FOCUS_NONE);
    }

    // Update tracking-state map (for decimation phase persistence) AFTER snaps[].
    EnemyEvictStale(tick);
    for (int i = 0; i < nEnemies; ++i) {
        EnemyState* es = EnemyTrack(snaps[i].handle, snaps[i].chr_ins_abs, tick);
        if (es) {
            es->cls = snaps[i].cls;
            es->is_focused = snaps[i].is_focused;
            es->focus_reason = snaps[i].focus_reason;
        }
    }

    // ----- Write enemies in priority order (focused first) with budget gate -----
    // Order: focused, then top-tier, then lesser.
    int writtenEnemies = 0;

    auto emitOne = [&](int idx) -> bool {
        const EnemySnapshot& s = snaps[idx];
        // Tier 1+2 always. Tier 3 conditional on mode + decimation + focused-vs-class.
        EnemyState fakeEs{};
        fakeEs.cls = s.cls;
        fakeEs.is_focused = s.is_focused;
        // Phase: load from tracking map.
        for (int j = 0; j < MAX_TRACKED_ENEMIES; ++j) {
            if (g_enemies[j].in_use && g_enemies[j].handle == s.handle) {
                fakeEs.phase = g_enemies[j].phase;
                break;
            }
        }
        bool incTier3   = ShouldEmitTier3(fakeEs, tick, g_cfg.mode, adaptiveStep);
        bool incFocus32 = s.is_focused;     // emphasized 32-byte region only on focused
        // For lesser-class enemies we also gate Tier 1+2 by decimation.
        // v6.1.1: previously returned true (counting as "written") to silently
        // skip decimation; but the caller increments writtenEnemies on true,
        // which inflated the on-disk enemy_count beyond what was actually
        // serialized. Return a sentinel-indicating-skip instead by returning
        // false; the analyzer-side count needs the actual emitted record
        // total to match the bytes that follow.
        if (s.cls == ENEMY_CLASS_LESSER &&
            !ShouldEmitLesserT12(fakeEs, tick)) {
            return false;      // skip silently — lesser Tier 1+2 at 10 Hz
        }
        return WriteEnemyRecord(w, s, incTier3, incFocus32, drop_broad_sweep,
                                playerChrIns);
    };

    // 1) Focused enemy.
    int focusedSnapIdx = -1;
    for (int i = 0; i < nEnemies; ++i) {
        if (snaps[i].is_focused) { focusedSnapIdx = i; break; }
    }
    if (focusedSnapIdx != -1) {
        if (emitOne(focusedSnapIdx)) ++writtenEnemies;
    }

    // Budget check after focused — if elapsed exceeds soft target, skip
    // non-focused Tier 3 (achieved by treating remaining as truncate-on-fit).
    bool softBudgetExceeded = false;
    double cfgBudget = g_cfg.budget_ms_per_sample;
    if (cfgBudget < 1.0) cfgBudget = 1.0;
    double softBudget = cfgBudget * 0.66;   // ~2 ms when budget is 3
    if (QpcElapsedMs(qpcStart) > softBudget) {
        softBudgetExceeded = true;
    }

    // 2) Other top-tier enemies (boss bars + nearest, in snap order).
    for (int i = 0; i < nEnemies; ++i) {
        if (i == focusedSnapIdx) continue;
        if (snaps[i].cls != ENEMY_CLASS_TOP) continue;
        if (QpcElapsedMs(qpcStart) > cfgBudget) break;
        if (softBudgetExceeded) {
            // Soft budget exceeded: write header only (no Tier 3) so the
            // sample still has a row for this enemy.
            if (WriteEnemyRecord(w, snaps[i], /*include_tier3=*/false,
                                 /*include_focus_emphasis=*/false,
                                 /*drop_broad_sweep=*/true,
                                 /*player_chr_ins_for_region15=*/0)) {
                ++writtenEnemies;
                g_dropBudgetSkip.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            if (emitOne(i)) ++writtenEnemies;
        }
    }

    // 3) Lesser-tier enemies (with decimation). Skip entirely if hard budget out.
    for (int i = 0; i < nEnemies; ++i) {
        if (i == focusedSnapIdx) continue;
        if (snaps[i].cls != ENEMY_CLASS_LESSER) continue;
        if (QpcElapsedMs(qpcStart) > cfgBudget) {
            g_dropBudgetSkip.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        if (softBudgetExceeded) {
            if (WriteEnemyRecord(w, snaps[i], /*include_tier3=*/false,
                                 /*include_focus_emphasis=*/false,
                                 /*drop_broad_sweep=*/true,
                                 /*player_chr_ins_for_region15=*/0)) {
                ++writtenEnemies;
                g_dropBudgetSkip.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            if (emitOne(i)) ++writtenEnemies;
        }
    }

    // Patch enemy_record_count and truncated flag.
    w.patch_u8(enemyCountMark, (uint8_t)writtenEnemies);
    if (w.overflow) {
        w.patch_u8(truncatedMark, 1);
        pb.truncated = true;
        g_truncatedSamples.fetch_add(1, std::memory_order_relaxed);
    }
    pb.len = w.pos;

    ProducerCommit();
}

// ===========================================================================
// Worker thread: drains the SPSC queue, writes binary records + CSV summary
// ===========================================================================
//
// File layout in <log_dir>/:
//   <session>-<ts>.bin            — binary per-sample records (raw payloads)
//   <session>-<ts>.csv            — Tier 1 + Tier 2 summary, pandas-friendly
//   <session>-<ts>.log.txt        — diagnostics
//   <session>-<ts>.calibration.txt — written at end of smoke mode only
//
// .bin record format: each pool buffer is written as
//   u32 magic=0x53524430 ('SRD0' = Sample Record Delimiter 0)
//   u32 length (bytes that follow, NOT including this header)
//   u8[]  raw producer-side serialized sample (starting with 'PTS0' header)
//
// TODO(v6.1): per spec §Change-delta logging, the worker should compare
// each Tier 3 region payload against a per-(handle, region_id) last-known
// state and emit either [full_snapshot] (first time, or every Nth as
// keyframe, or on child-target change) or [delta] records. v6 ships with
// FULL SNAPSHOTS ONLY — analysis works fine on them, just at 3-5x larger
// .bin file. ~1 hour of discovery is 5-10 GB raw vs ~1-3 GB delta-encoded.
// Disk space is not the binding constraint (separate NVMe). Add the delta
// encoder before the production probe ships (probe v7) or if a 1-hour
// discovery session ever exceeds the disk margin.
//
// The session manifest is the FIRST .bin record after a special manifest
// magic. See WriteSessionManifest() for layout.
//
// .csv columns (one row per emitted sample, one extra row per enemy):
//   sample_seq, ts_ms_rel, mode, focused_handle, focus_reason,
//   enemy_record_count, truncated, adaptive_step, sample_bytes
// Followed by enemy rows tagged with sample_seq:
//   sample_seq, enemy_idx, enemy_chr_ins, enemy_handle,
//   field_38, field_60, field_64, field_68, field_6C, field_80, field_1E8,
//   anim_id, t0, t1, t2, t3, in_lock_on, in_boss_bar, in_roster, class,
//   is_focused, focus_reason, region_count

#define SAMPLE_RECORD_MAGIC 0x30445253u  // 'SRD0' little-endian
#define MANIFEST_MAGIC      0x304E414Du  // 'MAN0' little-endian

static FILE* g_binFile = nullptr;
static FILE* g_csvFile = nullptr;
static FILE* g_logFile = nullptr;
static char  g_sessionTagPath[MAX_PATH] = {0};   // <log_dir>/<session>-<ts>
static std::atomic<uint64_t> g_binBytesWritten{0};
static int g_binRotateIdx = 0;
static constexpr uint64_t BIN_ROTATE_BYTES = 2ull * 1024 * 1024 * 1024;  // 2 GB

static void LogF(const char* fmt, ...) {
    if (!g_logFile) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    fprintf(g_logFile, "%lld %s\n", ms, buf);
}

static bool OpenSessionFiles() {
    // <session>-<YYYYMMDD-HHMMSS> base path.
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[32];
    _snprintf_s(ts, sizeof(ts), _TRUNCATE,
                "%04u%02u%02u-%02u%02u%02u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    // Ensure log_dir ends with a separator.
    char dir[MAX_PATH];
    strncpy_s(dir, g_cfg.log_dir, _TRUNCATE);
    size_t dn = strlen(dir);
    if (dn == 0) return false;
    if (dir[dn - 1] != '\\' && dir[dn - 1] != '/') {
        if (dn + 1 < MAX_PATH) { dir[dn] = '\\'; dir[dn + 1] = 0; }
    }

    _snprintf_s(g_sessionTagPath, MAX_PATH, _TRUNCATE,
                "%s%s-%s", dir, g_cfg.session_name, ts);

    char binPath[MAX_PATH], csvPath[MAX_PATH], logPath[MAX_PATH];
    _snprintf_s(binPath, MAX_PATH, _TRUNCATE, "%s.bin", g_sessionTagPath);
    _snprintf_s(csvPath, MAX_PATH, _TRUNCATE, "%s.csv", g_sessionTagPath);
    _snprintf_s(logPath, MAX_PATH, _TRUNCATE, "%s.log.txt", g_sessionTagPath);

    if (fopen_s(&g_binFile, binPath, "wb") != 0 || !g_binFile) {
        BootLog("session_open_fail: cannot open %s", binPath);
        return false;
    }
    if (fopen_s(&g_csvFile, csvPath, "wb") != 0 || !g_csvFile) {
        BootLog("session_open_fail: cannot open %s", csvPath);
        return false;
    }
    if (fopen_s(&g_logFile, logPath, "wb") != 0 || !g_logFile) {
        BootLog("session_open_fail: cannot open %s", logPath);
        return false;
    }
    // CSV header
    // v6.2: enemy rows append 8 instrumentation columns at end. Sample rows
    // unchanged. Positional CSV consumers must accept the new tail columns.
    fprintf(g_csvFile,
        "kind,sample_seq,ts_ms_rel,mode,focused_handle,focus_reason,"
        "enemy_record_count,truncated,adaptive_step,sample_bytes,"
        "enemy_idx,enemy_chr_ins,enemy_handle,"
        "field_38,field_60,field_64,field_68,field_6C,field_80,field_1E8,"
        "anim_id,t0,t1,t2,t3,in_lock_on,in_boss_bar,in_roster,class,"
        "is_focused,focus_reason_e,region_count,"
        // v6.2 enemy instrumentation tail (only populated on E rows; S rows blank):
        "anim_id_path_b,read_idx,anim_id_path_c,action_request_abs,phys_module_abs,"
        "phys_pos_x,phys_pos_y,phys_pos_z\n");
    BootLog("session_open: %s.{bin,csv,log.txt}", g_sessionTagPath);
    return true;
}

static void CloseSessionFiles() {
    if (g_binFile) { fflush(g_binFile); fclose(g_binFile); g_binFile = nullptr; }
    if (g_csvFile) { fflush(g_csvFile); fclose(g_csvFile); g_csvFile = nullptr; }
    if (g_logFile) { fflush(g_logFile); fclose(g_logFile); g_logFile = nullptr; }
}

static void RotateBinIfNeeded() {
    if (!g_binFile) return;
    if (g_binBytesWritten.load() < BIN_ROTATE_BYTES) return;
    fflush(g_binFile);
    fclose(g_binFile);
    ++g_binRotateIdx;
    char path[MAX_PATH];
    _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s.bin.%03d",
                g_sessionTagPath, g_binRotateIdx);
    if (fopen_s(&g_binFile, path, "wb") != 0 || !g_binFile) {
        // Cannot rotate; we'll have to drop bin records until next session.
        LogF("bin_rotate_fail: %s", path);
        return;
    }
    g_binBytesWritten.store(0);
    LogF("bin_rotate: opened %s", path);
}

static void WriteBinRecord(const uint8_t* payload, size_t len) {
    if (!g_binFile || len == 0) return;
    uint32_t magic = SAMPLE_RECORD_MAGIC;
    uint32_t len32 = (uint32_t)len;
    fwrite(&magic, 1, 4, g_binFile);
    fwrite(&len32, 1, 4, g_binFile);
    fwrite(payload, 1, len, g_binFile);
    g_binBytesWritten.fetch_add(8 + len, std::memory_order_relaxed);
    RotateBinIfNeeded();
}

// ----- Session manifest (first .bin record) -----

// Read the entire INI file into a heap buffer for inclusion in the manifest's
// `config_dump` field. Caller frees with `free()`. Returns nullptr on failure.
// Caps at 16 KB to keep the manifest record bounded.
static char* SlurpIniForManifest(size_t* outLen) {
    *outLen = 0;
    FILE* f = nullptr;
    if (fopen_s(&f, g_iniPath, "rb") != 0 || !f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return nullptr; }
    if (sz > 16 * 1024) sz = 16 * 1024;       // cap
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = 0;
    *outLen = got;
    fclose(f);
    return buf;
}

static void WriteSessionManifest(uint32_t erMajor, uint32_t erMinor,
                                 uint32_t erBuild, uint32_t erPatch,
                                 const RosterValidation& rv,
                                 int64_t session_start_ms)
{
    if (!g_binFile) return;

    // Build manifest payload as a key=value text block prefixed by MANIFEST_MAGIC.
    // Simple text format keeps the Python parser trivial. ~24 KB cap (8 KB
    // metadata + 16 KB inlined config dump).
    char buf[24 * 1024];
    int n = 0;
    n += _snprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE,
        "schema_version=%u\n"
        "build_date=%s\n"
        "build_time=%s\n"
        "probe_version=" PROBE_VERSION_STR "\n"
        "er_file_version=%u.%u.%u.%u\n"
        "er_module_base=0x%016llX\n"
        "wcm_ptr_addr=0x%016llX\n"
        "get_chr_ins_fn=0x%016llX\n"
        "update_ui_bar_fn=0x%016llX\n"
        "cs_fe_man_ptr_addr=0x%016llX\n"
        "roster_enabled=%d\n"
        "roster_check1=%d\n"
        "roster_check2=%d\n"
        "roster_check3=%d\n"
        "roster_check4=%d\n"
        "roster_check5=%d\n"
        "roster_check6=%d\n"
        "roster_candidate_count=%d\n"
        "mode=%u\n"
        "session_start_ms=%lld\n"
        "log_dir=%s\n"
        "session_name=%s\n"
        "sample_rate_hz=%d\n"
        "max_enemies_tracked=%d\n"
        "top_tier_enemies=%d\n"
        "lesser_tier_rate_hz=%d\n"
        "budget_ms_per_sample=%.3f\n"
        "verbose=%d\n",
        PROBE_SCHEMA_VERSION,
        __DATE__, __TIME__,
        erMajor, erMinor, erBuild, erPatch,
        (unsigned long long)g_refs.er_module_base,
        (unsigned long long)g_refs.wcmPtrAddr,
        (unsigned long long)g_refs.getChrInsFn,
        (unsigned long long)g_refs.updateUIBarFn,
        (unsigned long long)g_refs.csFeManPtrAddr,
        rv.enabled ? 1 : 0,
        rv.check1 ? 1 : 0, rv.check2 ? 1 : 0, rv.check3 ? 1 : 0,
        rv.check4 ? 1 : 0, rv.check5 ? 1 : 0, rv.check6 ? 1 : 0,
        rv.candidate_count,
        (unsigned)g_cfg.mode,
        (long long)session_start_ms,
        g_cfg.log_dir, g_cfg.session_name,
        g_cfg.sample_rate_hz, g_cfg.max_enemies_tracked,
        g_cfg.top_tier_enemies, g_cfg.lesser_tier_rate_hz,
        g_cfg.budget_ms_per_sample, g_cfg.verbose ? 1 : 0);

    // Inline the entire config file as `config_dump` (with begin/end markers
    // so the analysis parser knows where the literal INI text starts/ends).
    size_t iniLen = 0;
    char* iniBuf = SlurpIniForManifest(&iniLen);
    if (iniBuf && iniLen > 0 && (size_t)n + iniLen + 64 < sizeof(buf)) {
        n += _snprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE,
                         "config_dump_begin\n");
        memcpy(buf + n, iniBuf, iniLen);
        n += (int)iniLen;
        n += _snprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE,
                         "\nconfig_dump_end\n");
    }
    if (iniBuf) free(iniBuf);

    // build_hash: TODO — a real SHA-256 of the source file would require
    // generating a header at build time. Until then, embed the build
    // date/time which is unique per rebuild and good enough for diagnostics.
    n += _snprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE,
                     "build_hash=BUILD_%s_%s\n", __DATE__, __TIME__);

    uint32_t magic = MANIFEST_MAGIC;
    uint32_t len32 = (uint32_t)n;
    fwrite(&magic, 1, 4, g_binFile);
    fwrite(&len32, 1, 4, g_binFile);
    fwrite(buf, 1, n, g_binFile);
    fflush(g_binFile);
    g_binBytesWritten.fetch_add(8 + n, std::memory_order_relaxed);
}

static void WriteSessionEndManifest() {
    if (!g_binFile) return;
    char buf[1024];
    int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "session_end\n"
        "ticks=%llu\n"
        "samples_emitted=%llu\n"
        "drops_no_buffer=%llu\n"
        "drops_budget_skip=%llu\n"
        "drops_producer_emerg=%llu\n"
        "truncated_samples=%llu\n"
        "final_adaptive_step=%d\n"
        "roster_check7_runtime=%d\n",
        (unsigned long long)g_tickCount.load(),
        (unsigned long long)g_sampleSeq.load(),
        (unsigned long long)g_dropNoBuffer.load(),
        (unsigned long long)g_dropBudgetSkip.load(),
        (unsigned long long)g_dropProducerEmerg.load(),
        (unsigned long long)g_truncatedSamples.load(),
        g_adaptiveStep.load(),
        g_check7Done.load() ? 1 : 0);
    uint32_t magic = MANIFEST_MAGIC;
    uint32_t len32 = (uint32_t)n;
    fwrite(&magic, 1, 4, g_binFile);
    fwrite(&len32, 1, 4, g_binFile);
    fwrite(buf, 1, n, g_binFile);
    fflush(g_binFile);
}

// ----- CSV summarizer (parses a producer payload and writes summary rows) -----
//
// Reads enough of the wire format to emit Tier 1+2 fields. Tier 3 region
// payloads stay only in the .bin (too big for CSV).
//
// Returns true on success.

struct ParsedHeader {
    uint32_t magic;
    uint32_t schema_version;
    uint64_t frame;
    uint64_t ts_ms_rel;
    uint8_t  mode;
    uint8_t  truncated;
    uint64_t wcm_ptr;
    uint64_t module_base;
    uint64_t player_chr_ins;
    uint32_t player_anim_id;
    float    player_anim_time[4];
    float    player_pos[3];
    uint64_t player_lock_handle;
    uint64_t boss_handles[3];
    uint64_t focused_handle;
    uint8_t  focus_reason;
    uint8_t  enemy_record_count;
    uint8_t  adaptive_step;
};

struct Reader {
    const uint8_t* buf;
    size_t cap;
    size_t pos;
    bool   ok;

    Reader(const uint8_t* b, size_t c) : buf(b), cap(c), pos(0), ok(true) {}
    inline bool get(void* dst, size_t n) {
        if (!ok || pos + n > cap) { ok = false; return false; }
        memcpy(dst, buf + pos, n); pos += n; return true;
    }
    inline uint8_t  get_u8 () { uint8_t  v = 0; get(&v, 1); return v; }
    inline uint16_t get_u16() { uint16_t v = 0; get(&v, 2); return v; }
    inline uint32_t get_u32() { uint32_t v = 0; get(&v, 4); return v; }
    inline uint64_t get_u64() { uint64_t v = 0; get(&v, 8); return v; }
    inline float    get_f32() { float    v = 0; get(&v, 4); return v; }
    inline bool     skip(size_t n) { if (!ok || pos + n > cap) { ok = false; return false; } pos += n; return true; }
};

static bool ParseHeader(Reader& r, ParsedHeader* h) {
    h->magic              = r.get_u32();
    h->schema_version     = r.get_u32();
    h->frame              = r.get_u64();
    h->ts_ms_rel          = r.get_u64();
    h->mode               = r.get_u8();
    h->truncated          = r.get_u8();
    uint8_t reserved6[6]; r.get(reserved6, 6);
    h->wcm_ptr            = r.get_u64();
    h->module_base        = r.get_u64();
    h->player_chr_ins     = r.get_u64();
    h->player_anim_id     = r.get_u32();
    for (int i = 0; i < 4; ++i) h->player_anim_time[i] = r.get_f32();
    for (int i = 0; i < 3; ++i) h->player_pos[i]       = r.get_f32();
    h->player_lock_handle = r.get_u64();
    // v6.2 Tier 1 player instrumentation block (48 bytes, schema v2+):
    if (h->schema_version >= 2) {
        r.skip(48);  // 8 vtable + 12 phys_pos + 8 phys_module + 8 lock_new + 4 area_new + 8 reserved
    }
    for (int i = 0; i < 3; ++i) h->boss_handles[i]     = r.get_u64();
    h->focused_handle     = r.get_u64();
    h->focus_reason       = r.get_u8();
    h->enemy_record_count = r.get_u8();
    h->adaptive_step      = r.get_u8();
    (void)r.get_u8();  // reserved2
    if (!r.ok) return false;
    if (h->magic != SAMPLE_MAGIC) return false;
    return true;
}

static void EmitCsvSampleRow(uint64_t seq, const ParsedHeader& h, size_t sample_bytes) {
    if (!g_csvFile) return;
    // S row: 40 columns total to match header. Section 1 (kind..sample_bytes) =
    // 10 values; remaining 30 cells empty.
    //   3 blanks: enemy_idx, enemy_chr_ins, enemy_handle
    //   7 blanks: field_38, field_60, field_64, field_68, field_6C, field_80, field_1E8
    //   5 blanks: anim_id, t0, t1, t2, t3
    //   4 blanks: in_lock_on, in_boss_bar, in_roster, class
    //   3 blanks: is_focused, focus_reason_e, region_count
    //   8 blanks: anim_id_path_b, read_idx, anim_id_path_c, action_request_abs, phys_module_abs, phys_pos_x, phys_pos_y, phys_pos_z
    //  30 total blanks → 29 commas between them + 1 trailing newline.
    // After the `,` at end of `%zu,`, the literal needs exactly 29 more commas + \n.
    fprintf(g_csvFile,
        "S,%llu,%llu,%u,0x%016llX,%u,%u,%u,%u,%zu,"
        ",,"                       // 2 commas → enemy_idx + enemy_chr_ins blanks (3 cells total with leading comma)
        ","                        // 1 → enemy_handle blank (now 3 cells)
        ",,,,,,,"                  // 7 → field_* (10 cells)
        ",,,,,"                    // 5 → anim_id..t3 (15 cells)
        ",,,,"                     // 4 → in_lock_on..class (19 cells)
        ",,,"                      // 3 → is_focused..region_count (22 cells)
        ",,,,,,,"                  // 7 → 7 of 8 v6.2 tail (29 cells)
        "\n",                      // 1 newline closes the 30th cell (last v6.2 blank)
        (unsigned long long)seq,
        (unsigned long long)h.ts_ms_rel,
        (unsigned)h.mode,
        (unsigned long long)h.focused_handle,
        (unsigned)h.focus_reason,
        (unsigned)h.enemy_record_count,
        (unsigned)h.truncated,
        (unsigned)h.adaptive_step,
        sample_bytes);
}

static void EmitCsvEnemyRow(uint64_t seq, int enemy_idx, Reader& r, uint32_t schema_version) {
    // EnemyHeader: 96 bytes for schema v1, 136 bytes for schema v2 (+v6.2 block).
    uint64_t chrIns = r.get_u64();
    uint64_t handle = r.get_u64();
    uint32_t f38    = r.get_u32();
    uint32_t f60    = r.get_u32();
    uint32_t f64    = r.get_u32();
    uint32_t f68    = r.get_u32();
    uint32_t f6C    = r.get_u32();
    uint32_t f80    = r.get_u32();
    uint32_t f1E8   = r.get_u32();
    uint32_t aId    = r.get_u32();
    float t0 = r.get_f32(), t1 = r.get_f32(), t2 = r.get_f32(), t3 = r.get_f32();
    uint8_t inLock = r.get_u8();
    uint8_t inBoss = r.get_u8();
    uint8_t inRos  = r.get_u8();
    uint8_t cls    = r.get_u8();
    uint8_t isFoc  = r.get_u8();
    uint8_t fReas  = r.get_u8();
    uint8_t regCnt = r.get_u8();
    (void)r.get_u8();    // reserved
    // v6.2 instrumentation block (44 bytes, schema v2+; +20 pad = 64 → 136-byte header):
    uint32_t animIdB = 0, readIdx = 0xFFFFFFFFu, animIdC = 0;
    uint64_t actionReqAbs = 0, physModuleAbs = 0;
    float phx = 0, phy = 0, phz = 0;
    if (schema_version >= 2) {
        animIdB     = r.get_u32();
        readIdx     = r.get_u32();
        animIdC     = r.get_u32();
        (void)r.get_u32();  // reserved align
        actionReqAbs   = r.get_u64();
        physModuleAbs  = r.get_u64();
        phx = r.get_f32(); phy = r.get_f32(); phz = r.get_f32();
        uint8_t padV2[20]; r.get(padV2, 20);  // pad to 136-byte header
    } else {
        uint8_t pad[24]; r.get(pad, 24);  // v1 pad to 96
    }
    if (!r.ok) return;

    if (g_csvFile) {
        fprintf(g_csvFile,
            "E,%llu,,,,,,,,,"
            "%d,0x%016llX,0x%016llX,"
            "0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,"
            "0x%08X,%.6f,%.6f,%.6f,%.6f,"
            "%u,%u,%u,%u,%u,%u,%u,"
            "0x%08X,%u,0x%08X,0x%016llX,0x%016llX,%.6f,%.6f,%.6f\n",
            (unsigned long long)seq,
            enemy_idx, (unsigned long long)chrIns, (unsigned long long)handle,
            f38, f60, f64, f68, f6C, f80, f1E8,
            aId, t0, t1, t2, t3,
            (unsigned)inLock, (unsigned)inBoss, (unsigned)inRos, (unsigned)cls,
            (unsigned)isFoc, (unsigned)fReas, (unsigned)regCnt,
            animIdB, (unsigned)readIdx, animIdC,
            (unsigned long long)actionReqAbs, (unsigned long long)physModuleAbs,
            phx, phy, phz);
    }

    // Skip Tier 3 records for this enemy. Each region record:
    //   1+1+2+2+2+4+8 = 20 byte header + payload_len bytes.
    for (int i = 0; i < regCnt && r.ok; ++i) {
        (void)r.get_u8();                // region_id
        (void)r.get_u8();                // has_child_offset
        (void)r.get_u16();               // payload_offset
        uint16_t payLen = r.get_u16();
        (void)r.get_u16();               // child_source_offset
        (void)r.get_u32();               // source_chain
        (void)r.get_u64();               // region_base_abs
        if (!r.skip(payLen)) break;
    }
}

// Anim-time monotonicity tracker (smoke-mode calibration).
struct AnimTimeTracker {
    int      monotonic_segments = 0;
    float    max_segment_dur    = 0.0f;
    bool     f32_in_range       = true;     // turns false on bad value seen
    bool     rewinds_on_anim_id_change = false;  // confirmed at least once
    // Internal:
    bool     have_prev = false;
    float    prev_val = 0.0f;
    uint32_t prev_anim_id = 0;
    float    seg_start_val = 0.0f;
    int      seg_samples = 0;
};

static void AnimTrackUpdate(AnimTimeTracker* t, uint32_t animId, float val) {
    // Spec gate: f32 finite + in plausible range (0..600s).
    bool finite_ok = !(val != val) && val >= 0.0f && val <= 600.0f;
    if (!finite_ok) {
        t->f32_in_range = false;
    }
    if (!t->have_prev) {
        t->have_prev = true;
        t->prev_val = val;
        t->prev_anim_id = animId;
        t->seg_start_val = val;
        t->seg_samples = 1;
        return;
    }
    if (animId != t->prev_anim_id) {
        // Anim ID changed — close any current segment.
        if (t->seg_samples >= 3) {
            float dur = t->prev_val - t->seg_start_val;
            if (dur > t->max_segment_dur) t->max_segment_dur = dur;
            t->monotonic_segments += 1;
        }
        // Rewind detection: did the time field reset (val < prev_val) on anim ID change?
        if (val < t->prev_val) t->rewinds_on_anim_id_change = true;
        t->seg_start_val = val;
        t->seg_samples = 1;
        t->prev_val = val;
        t->prev_anim_id = animId;
        return;
    }
    // Same anim ID — continue or break the segment.
    if (val + 1e-6f >= t->prev_val) {
        t->seg_samples += 1;
        t->prev_val = val;
    } else {
        // Non-monotonic during stable anim id — close + reset.
        if (t->seg_samples >= 3) {
            float dur = t->prev_val - t->seg_start_val;
            if (dur > t->max_segment_dur) t->max_segment_dur = dur;
            t->monotonic_segments += 1;
        }
        t->seg_start_val = val;
        t->seg_samples = 1;
        t->prev_val = val;
    }
}

static void AnimTrackFinalize(AnimTimeTracker* t) {
    if (t->seg_samples >= 3) {
        float dur = t->prev_val - t->seg_start_val;
        if (dur > t->max_segment_dur) t->max_segment_dur = dur;
        t->monotonic_segments += 1;
    }
}

// Smoke calibration state — accumulates over the whole session.
struct CalibrationState {
    AnimTimeTracker player_t[4];
};
static CalibrationState g_calib;

static void WriteCalibrationReport() {
    if (g_cfg.mode != MODE_SMOKE) return;
    char path[MAX_PATH];
    _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s.calibration.txt", g_sessionTagPath);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        LogF("calib_open_fail: %s", path);
        return;
    }

    for (int i = 0; i < 4; ++i) AnimTrackFinalize(&g_calib.player_t[i]);

    fprintf(f, "animation-time candidate analysis (smoke run):\n");
    const int offs[4] = {0x20, 0x24, 0x28, 0x2C};
    int winner = -1;
    float winnerDur = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const AnimTimeTracker& t = g_calib.player_t[i];
        bool gate = (t.f32_in_range &&
                     t.monotonic_segments >= 3 &&
                     t.max_segment_dur >= 0.3f &&
                     t.rewinds_on_anim_id_change);
        fprintf(f,
            "  +0x%02X: monotonic_segments=%d max_segment_dur=%.2fs "
            "f32_in_range=%s rewinds_on_anim_id_change=%s gate=%s\n",
            offs[i],
            t.monotonic_segments,
            t.max_segment_dur,
            t.f32_in_range ? "true" : "false",
            t.rewinds_on_anim_id_change ? "true" : "false",
            gate ? "PASS" : "FAIL");
        if (gate && t.max_segment_dur > winnerDur) {
            winnerDur = t.max_segment_dur;
            winner = i;
        }
    }
    if (winner >= 0) {
        fprintf(f, "\nWinner: TimeAct + 0x%02X (max_segment_dur=%.2fs)\n",
                offs[winner], winnerDur);
    } else {
        fprintf(f, "\nNo candidate passed the gate. Re-research before continuing.\n");
    }
    fclose(f);
    LogF("calib_written: %s", path);
}

// ===========================================================================
// Predictor game-state readers + worker tick (Phase 4.1 commit 4)
// ===========================================================================
//
// These helpers translate SEH-wrapped game-memory reads into the inputs the
// decision engine expects. They live AFTER all read primitives, the roster,
// and the boss-bar offsets are defined, so we can call into them directly
// from the worker's predictor tick.
//
// Co-op safety: every read is SafeRead-wrapped. No writes to game memory.
// On any read failure, the input is marked unknown (target_known=false) or
// the boss is skipped (returns false). Predictor errs on suppress, not fire.

// Shutdown event for waitable polling sleep. Created by worker init (NOT
// DllMain — per Decision G of the locked Phase 4.1 design doc; loader-lock-
// adjacent allocations stay out of DllMain). g_running stays as the fast
// flag the rest of the codebase already uses.
static HANDLE g_shutdownEvent = nullptr;

// Dereference WCM and return its address. Returns 0 on failure.
static uintptr_t ReadWcm() {
    if (!g_refs.wcmPtrAddr) return 0;
    uintptr_t wcm = 0;
    if (!SafeRead<uintptr_t>(g_refs.wcmPtrAddr, &wcm)) return 0;
    if (!LooksLikeUserPtrFast(wcm)) return 0;
    return wcm;
}

// Read the local player's FieldInsHandle. Returns true on success.
//
// Two-hop deref: WCM_PLAYER_ARRAY[0] holds a SLOT POINTER (not a ChrIns).
// The slot pointer must be dereferenced one more time to get the actual
// player ChrIns. This matches the existing capture-path semantics at
// probe.cpp:3118-3127 (which Claude's first draft got wrong; Codex
// caught the one-hop bug at review).
//
// We don't cache because slot 0 can be 0 during transitions (loading
// screens, character creation) — re-read each tick and gracefully
// report "no player" when it isn't there yet.
static bool ReadLocalPlayerHandle(uint64_t* out_handle) {
    if (!out_handle) return false;
    uintptr_t wcm = ReadWcm();
    if (!wcm) return false;

    uintptr_t player_slot = 0;
    if (!SafeRead<uintptr_t>(wcm + Off::WCM_PLAYER_ARRAY, &player_slot)) return false;
    if (!LooksLikeUserPtrFast(player_slot)) return false;

    uintptr_t player_chr_ins = 0;
    if (!SafeRead<uintptr_t>(player_slot, &player_chr_ins)) return false;
    if (!LooksLikeUserPtrFast(player_chr_ins)) return false;

    uint64_t h = 0;
    if (!SafeRead<uint64_t>(player_chr_ins + Off::CHR_INS_HANDLE, &h)) return false;
    if (h == 0 || h == UINT64_MAX) return false;
    *out_handle = h;
    return true;
}

// Walk ChrIns → AIBag → AIStruct → +0xC988 (target FieldInsHandle).
// Returns true and sets *out_handle on a clean walk. Returns false on any
// dereference failure OR if the read FieldInsHandle is the sentinel
// (0xFFFFFFFFFFFFFFFF, "no current target").
static bool ReadBossTargetHandle(uintptr_t boss_chr_ins, uint64_t* out_handle) {
    if (!out_handle || !boss_chr_ins) return false;

    uintptr_t ai_bag = 0;
    if (!SafeRead<uintptr_t>(boss_chr_ins + Off::CHR_INS_AI_STRUCT_BASE, &ai_bag)) {
        return false;
    }
    if (!LooksLikeUserPtrFast(ai_bag)) return false;

    uintptr_t ai_struct = 0;
    if (!SafeRead<uintptr_t>(ai_bag + Off::AI_BAG_AI_STRUCT_PTR, &ai_struct)) {
        return false;
    }
    if (!LooksLikeUserPtrFast(ai_struct)) return false;

    uint64_t target = 0;
    if (!SafeRead<uint64_t>(ai_struct + Off::AI_STRUCT_TARGET_HANDLE, &target)) {
        return false;
    }
    if (target == Off::AI_STRUCT_TARGET_SENTINEL) return false;

    *out_handle = target;
    return true;
}

// Read boss anim_id and anim_time_s from the proven TimeAct path A:
//   ChrIns +0x190 (ModuleBag*) → +0x18 (TimeAct*) → +0xD0 (anim_id u32)
//                                                → +0x24 (anim_time_s f32)
// Returns true on success. Sets *out_anim_id and *out_anim_time_s.
static bool ReadBossAnimState(uintptr_t boss_chr_ins,
                              uint32_t* out_anim_id,
                              float* out_anim_time_s) {
    if (!out_anim_id || !out_anim_time_s || !boss_chr_ins) return false;

    uintptr_t mod_bag = 0;
    if (!SafeRead<uintptr_t>(boss_chr_ins + Off::CHR_INS_MODULE_BAG_PTR, &mod_bag)) {
        return false;
    }
    if (!LooksLikeUserPtrFast(mod_bag)) return false;

    uintptr_t time_act = 0;
    if (!SafeRead<uintptr_t>(mod_bag + Off::MODULE_BAG_TIME_ACT_PTR, &time_act)) {
        return false;
    }
    if (!LooksLikeUserPtrFast(time_act)) return false;

    uint32_t anim_id = 0;
    float anim_time = 0.0f;
    if (!SafeRead<uint32_t>(time_act + Off::TIME_ACT_ANIM_ID, &anim_id)) return false;
    if (!SafeRead<float>(time_act + Off::TIME_ACT_TIME_CAND_1, &anim_time)) return false;

    *out_anim_id = anim_id;
    *out_anim_time_s = anim_time;
    return true;
}

// Enumerate active boss-bar entries. Writes up to MAX_TRACKED_BOSSES handles
// to out_handles. Returns the count actually populated (0..3). UINT64_MAX
// and 0 are treated as "slot empty" and skipped.
static int EnumerateActiveBossHandles(uint64_t* out_handles, int max_handles) {
    if (!out_handles || max_handles <= 0) return 0;
    if (!g_refs.csFeManPtrAddr) return 0;

    uintptr_t feMan = 0;
    if (!SafeRead<uintptr_t>(g_refs.csFeManPtrAddr, &feMan)) return 0;
    if (!LooksLikeUserPtrFast(feMan)) return 0;

    int n = 0;
    int cap = (max_handles < Off::CS_FE_MAN_BOSS_BAR_COUNT)
            ? max_handles : Off::CS_FE_MAN_BOSS_BAR_COUNT;
    for (int b = 0; b < cap; ++b) {
        uintptr_t slot = feMan + Off::CS_FE_MAN_BOSS_BARS_BASE +
                         (uintptr_t)b * Off::CS_FE_MAN_BOSS_BAR_STRIDE +
                         Off::CS_FE_MAN_BOSS_BAR_HANDLE;
        uint64_t h = 0;
        if (!SafeRead<uint64_t>(slot, &h)) continue;
        if (h == 0 || h == UINT64_MAX) continue;
        out_handles[n++] = h;
    }
    return n;
}

// Build a per-tick map of {target_handles[]} → {ChrIns* or 0}. Single
// roster walk; resolves all requested handles in one pass. This is the
// shape RunPredictorTick uses now (one walk per tick, not one per boss).
//
// Inputs:
//   target_handles[] — up to N FieldInsHandles to resolve (UINT64_MAX
//                      entries are treated as "skip slot").
//   n_targets        — number of valid entries.
// Outputs:
//   out_chr_ins[]    — written with ChrIns* for each found handle, or
//                      0 if not found. Length must be >= n_targets.
//
// Friendly PCs are NEVER returned (v6.4 friendly-exclusion semantics).
static void ResolveBossHandlesBatch(const uint64_t* target_handles,
                                    int n_targets,
                                    uintptr_t* out_chr_ins) {
    for (int i = 0; i < n_targets; ++i) out_chr_ins[i] = 0;
    if (n_targets == 0) return;
    if (!g_roster.enabled) return;

    uintptr_t wcm = ReadWcm();
    if (!wcm) return;

    uintptr_t begin = 0, end = 0;
    if (!SafeRead<uintptr_t>(wcm + Off::WCM_ROSTER_BEGIN, &begin)) return;
    if (!SafeRead<uintptr_t>(wcm + Off::WCM_ROSTER_END,   &end))   return;
    if (!begin || !end || end <= begin) return;

    uintptr_t span_bytes = end - begin;
    if (span_bytes > (uintptr_t)(2048 * sizeof(uintptr_t))) return;

    // Gather friendly PCs once. Two-hop deref: WCM_PLAYER_ARRAY[i] holds
    // a slot pointer that must be dereferenced once more to yield the
    // player ChrIns*.
    uintptr_t friendlyPCs[Off::PLAYER_ARRAY_LEN] = {0};
    int friendlyCount = 0;
    for (int i = 0; i < Off::PLAYER_ARRAY_LEN; ++i) {
        uintptr_t slot_addr = wcm + Off::WCM_PLAYER_ARRAY + (uintptr_t)i * sizeof(uintptr_t);
        uintptr_t slot_ptr = 0;
        if (!SafeRead<uintptr_t>(slot_addr, &slot_ptr)) continue;
        if (!LooksLikeUserPtrFast(slot_ptr)) continue;
        uintptr_t chr_ins = 0;
        if (!SafeRead<uintptr_t>(slot_ptr, &chr_ins)) continue;
        if (!LooksLikeUserPtrFast(chr_ins)) continue;
        friendlyPCs[friendlyCount++] = chr_ins;
    }

    int count = (int)(span_bytes / sizeof(uintptr_t));
    int remaining = n_targets;

    for (int i = 0; i < count && remaining > 0; ++i) {
        uintptr_t slotPtr = begin + (uintptr_t)i * sizeof(uintptr_t);
        uintptr_t chrIns = 0;
        if (!SafeRead<uintptr_t>(slotPtr, &chrIns)) continue;
        if (!LooksLikeUserPtrFast(chrIns)) continue;

        // Friendly exclusion: never resolve a boss handle to a friendly PC.
        bool friendly = false;
        for (int j = 0; j < friendlyCount; ++j) {
            if (chrIns == friendlyPCs[j]) { friendly = true; break; }
        }
        if (friendly) continue;

        uint64_t h = 0;
        if (!SafeRead<uint64_t>(chrIns + Off::CHR_INS_HANDLE, &h)) continue;
        if (h == 0 || h == UINT64_MAX) continue;

        // Check against all requested target handles. Break out once we've
        // resolved every requested handle (early-exit when remaining==0).
        for (int t = 0; t < n_targets; ++t) {
            if (out_chr_ins[t] != 0) continue;          // already resolved
            if (target_handles[t] == UINT64_MAX) continue;
            if (target_handles[t] == h) {
                out_chr_ins[t] = chrIns;
                remaining -= 1;
                break;
            }
        }
    }
}

// Read the c-id (ChrIns +0x064). Returns the raw u32 value or 0 on read
// failure. Caller validates the value via ResolveCidForLookup.
static uint32_t ReadBossRawCid(uintptr_t boss_chr_ins) {
    if (!boss_chr_ins) return 0;
    uint32_t cid = 0;
    if (!SafeRead<uint32_t>(boss_chr_ins + Off::CHR_INS_FIELD_64, &cid)) return 0;
    return cid;
}

// Drive one predictor tick. Enumerates active bosses, reads each one's
// state, and dispatches to the decision engine. Counters in/out via the
// stats struct so the caller can log periodic summaries.
struct PredictionTickStats {
    int bosses_seen           = 0;
    int bosses_read_ok        = 0;
    int bosses_target_known   = 0;
    int decisions_emitted     = 0;
};

static void RunPredictorTick(const PredictionConfig& engine_cfg,
                             int64_t now_ms_rel,
                             PredictionTickStats* stats) {
    constexpr int MAX_BOSSES = Off::CS_FE_MAN_BOSS_BAR_COUNT;  // 3
    uint64_t handles[MAX_BOSSES] = {0};
    int nHandles = EnumerateActiveBossHandles(handles, MAX_BOSSES);
    if (stats) stats->bosses_seen += nHandles;
    if (nHandles == 0) return;

    // Read player handle once per tick. If this fails, target filter cannot
    // be applied; we still run the predictor with target_known=false.
    uint64_t player_handle = 0;
    bool have_player_handle = ReadLocalPlayerHandle(&player_handle);

    // Single roster walk that resolves ALL boss handles at once. Replaces
    // the per-boss roster walk that was 3× more expensive at 250Hz.
    uintptr_t boss_chr_ins_arr[MAX_BOSSES] = {0};
    ResolveBossHandlesBatch(handles, nHandles, boss_chr_ins_arr);

    for (int b = 0; b < nHandles; ++b) {
        uint64_t boss_handle = handles[b];
        uintptr_t boss_chr_ins = boss_chr_ins_arr[b];
        if (!boss_chr_ins) continue;  // can't read state, no decision

        BossTickInput in;
        in.boss_handle  = boss_handle;
        in.boss_chr_ins = boss_chr_ins;
        in.raw_cid      = ReadBossRawCid(boss_chr_ins);
        if (!ReadBossAnimState(boss_chr_ins, &in.anim_id, &in.anim_time_s)) {
            // No anim state — can't predict. Skip silently. The boss is
            // present but TimeAct is unreadable this frame (mid-transition,
            // unmapped, etc.). Try again next poll.
            continue;
        }
        if (stats) stats->bosses_read_ok += 1;

        // Target field. Filter-disabled callers just get target_match=false
        // and target_known=false, but we still attempt the read so the
        // decision log can include the value when target_filter is off
        // (handy for offline analysis: "would the filter have suppressed?").
        uint64_t boss_target = 0;
        if (have_player_handle && ReadBossTargetHandle(boss_chr_ins, &boss_target)) {
            in.target_known = true;
            in.target_match = (boss_target == player_handle);
            if (stats) stats->bosses_target_known += 1;
        } else {
            in.target_known = false;
            in.target_match = false;
        }

        int emitted = EvaluatePredictionTick(in, engine_cfg, now_ms_rel);
        if (stats) stats->decisions_emitted += emitted;
    }
}

// ----- The worker thread main loop -----

static DWORD WINAPI WorkerMain(LPVOID);

static HANDLE g_workerThread = nullptr;
static HANDLE g_f11Thread = nullptr;

static DWORD WINAPI WorkerMain(LPVOID) {
    DebugBanner("worker thread up");

    ResolveDllPaths();
    BootLogOpen();
    BootLog("worker: probe=" PROBE_VERSION_STR " build=%s %s", __DATE__, __TIME__);

    // (a) boot log already open
    // (b) load config
    if (!LoadConfig(g_iniPath, &g_cfg)) {
        DebugBanner("FAIL: config load — staying alive (no hook). See boot log.");
        // sleep forever (per spec: fail-closed config)
        while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        BootLogClose();
        return 0;
    }
    g_cfgReady.store(true);
    BootLog("config: mode=%u session=%s log_dir=%s",
            (unsigned)g_cfg.mode, g_cfg.session_name, g_cfg.log_dir);

    // (d) open session log files
    if (!OpenSessionFiles()) {
        DebugBanner("FAIL: cannot open session files — staying alive (no hook).");
        while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        BootLogClose();
        return 0;
    }

    auto sessionStart = std::chrono::steady_clock::now().time_since_epoch();
    g_sessionStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(sessionStart).count();

    // (d.1) Create shutdown event for waitable predictor sleep. Out of
    // DllMain so loader-lock-adjacent allocations stay in worker context.
    // Manual-reset so multiple wait sites all see the signal.
    g_shutdownEvent = CreateEventW(nullptr, /*manual reset*/ TRUE,
                                   /*initial state*/ FALSE, nullptr);
    if (!g_shutdownEvent) {
        BootLog("init_warn: CreateEvent failed (err=%lu); falling back to sleep_for",
                GetLastError());
    } else {
        BootLog("shutdown_event: created");
    }

    // (d.2) Load parry DB (sibling to DLL). Fail-closed for CUES on error,
    // not for the whole process — data-collection mode and the existing
    // detour-arms path stay viable even if the predictor goes silent.
    LoadParryDb();

    // (d.3) Open prediction decision log if enabled. Reuses g_sessionTagPath
    // for naming consistency with the other session artifacts: file is
    // `<session>-<timestamp>.predictions.jsonl` alongside the .bin/.csv/.log.txt.
    // Must run AFTER OpenSessionFiles() (which populates g_sessionTagPath).
    if (g_cfg.prediction_decision_log_enabled) {
        char pred_log_path[MAX_PATH] = {0};
        _snprintf_s(pred_log_path, MAX_PATH, _TRUNCATE,
                    "%s.predictions.jsonl", g_sessionTagPath);
        // Non-fatal if open fails — predictor will still RUN, decisions
        // just don't get persisted (writes are no-ops via g_predictionLogReady).
        PredictionLogOpen(pred_log_path);
    }

    // (e) ER FileVersion
    uint32_t major = 0, minor = 0, build = 0, patch = 0;
    int versionResult = -1;
    for (int i = 0; i < 30 && g_running.load(); ++i) {
        versionResult = CheckExpectedGameVersion(&major, &minor, &build, &patch);
        if (versionResult >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (versionResult == 0) {
        BootLog("init_fail: ER FileVersion %u.%u.%u.%u (expected 2.6.1.0)",
                major, minor, build, patch);
        LogF("init_fail: wrong ER FileVersion %u.%u.%u.%u",
             major, minor, build, patch);
        while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CloseSessionFiles();
        BootLogClose();
        return 0;
    }
    if (versionResult == -1) {
        BootLog("init_warn: ER FileVersion unreadable; proceeding with caution");
        LogF("init_warn: ER FileVersion unreadable");
    } else {
        BootLog("init_ok: ER FileVersion %u.%u.%u.%u", major, minor, build, patch);
    }

    // (f) sig scan with retry
    bool sigOk = false;
    for (int i = 0; i < 30 && g_running.load(); ++i) {
        if (ResolveGameRefs()) { sigOk = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!sigOk) {
        BootLog("init_fail: sig scan exhausted");
        LogF("init_fail: sig scan exhausted");
        while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CloseSessionFiles();
        BootLogClose();
        return 0;
    }

    // (g) roster validation (NOT fail-closed)
    // v6.1: extended grace from 15s to 60s (covers slow boot + save-select).
    bool rosterOk = false;
    for (int i = 0; i < 120 && g_running.load(); ++i) {
        if (ValidateRosterInit()) { rosterOk = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!rosterOk) {
        BootLog("roster: disabled (fall back to player + boss bars only)");
        LogF("roster: disabled (fall back)");
        // not fatal — continue
    }

    // (h) buffer pool
    if (!BufferPoolAlloc()) {
        BootLog("init_fail: buffer pool alloc");
        LogF("init_fail: buffer pool alloc");
        while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CloseSessionFiles();
        BootLogClose();
        return 0;
    }

    // (i) write session manifest
    WriteSessionManifest(major, minor, build, patch, g_roster, g_sessionStartMs);

    // (j) install hook — ONLY if data_collection_mode is enabled.
    // Phase 4.1 decoupling: the predictor doesn't need the detour. When
    // shipping v0.1.0 (data_collection_mode=false default), we skip the
    // hook entirely, reducing surface area and CPU cost.
    InitQpc();
    extern bool InstallHook();
    if (g_cfg.data_collection_mode) {
        if (!InstallHook()) {
            BootLog("init_fail: hook install");
            LogF("init_fail: hook install");
            while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
            BufferPoolFree();
            CloseSessionFiles();
            BootLogClose();
            return 0;
        }
        BootLog("hook: installed (data_collection_mode=true)");
    } else {
        BootLog("hook: skipped (data_collection_mode=false; predictor-only)");
    }

    // (k) F11 watcher — only needed when data collection is active.
    extern DWORD WINAPI F11Thread(LPVOID);
    if (g_cfg.data_collection_mode) {
        g_f11Thread = CreateThread(nullptr, 0, F11Thread, nullptr, 0, nullptr);
    } else {
        BootLog("f11_thread: skipped (data_collection_mode=false)");
    }

    g_initOk.store(true);
    if (g_cfg.data_collection_mode) {
        BootLog("READY: press F11 to arm/disarm sampling; predictor running in parallel");
        LogF("ready: data_collection_mode=true; predictor running");
    } else {
        BootLog("READY: predictor running (data_collection_mode=false)");
        LogF("ready: predictor-only mode");
    }

    // Track when the user first arms, so we can warn at +30s in qualification
    // mode if check 7 (a boss-bar enemy appears in the roster) hasn't fired.
    int64_t firstArmedMs = 0;
    bool armedPrev = false;
    bool check7WarningEmitted = false;

    // (l) steady state: drain queue + adaptive sampling monitor +
    // PREDICTOR POLL. Predictor runs every prediction_poll_interval_ms;
    // queue drain runs opportunistically alongside.
    int64_t flushDeadlineMs = g_sessionStartMs + 1000;
    int64_t adaptiveCheckMs = g_sessionStartMs + 1000;     // poll every 1s, but keep 5s rolling
    int currentStep = 0;
    int64_t recoveryStartMs = 0;

    // Predictor cadence state.
    const int   pred_poll_ms = g_cfg.prediction_poll_interval_ms;
    int64_t     nextPredictMs = g_sessionStartMs;          // fire immediately on first iter
    int64_t     nextGcMs      = g_sessionStartMs + 5000;   // every 5s prune g_bossLatch
    PredictionConfig pred_engine_cfg{};
    pred_engine_cfg.audio_cue_lead_ms      = g_cfg.audio_cue_lead_ms;
    pred_engine_cfg.target_filter_enabled  = g_cfg.target_filter_enabled;
    PredictionTickStats pred_stats{};
    int64_t     predStatsLogMs = g_sessionStartMs + 5000;  // log summary every 5s

    // Rolling-window samples: 6 buckets × 1s each. Use index = (sec_since_start % 6).
    // Each bucket holds (drops, samples) at the END of that second. We compare
    // current totals against the bucket from 5s ago.
    constexpr int ROLL_BUCKETS = 6;
    struct DropSample { uint64_t drops; uint64_t samples; bool initialized; };
    DropSample rollHist[ROLL_BUCKETS] = {};
    int rollIdx = 0;
    int64_t lastBucketMs = g_sessionStartMs;

    while (g_running.load()) {
        bool didWork = false;
        // Drain at most a batch so we get back to monitoring promptly.
        for (int i = 0; i < 64; ++i) {
            int idx = ConsumerPeek();
            if (idx < 0) break;
            PoolBuffer& pb = g_pool[idx];
            uint64_t seq = g_sampleSeq.load();   // not perfectly synchronized; OK for diagnostics

            // Write the bin record (raw producer payload).
            WriteBinRecord(pb.data, pb.len);

            // Parse + emit CSV summary rows.
            Reader r(pb.data, pb.len);
            ParsedHeader hdr{};
            if (ParseHeader(r, &hdr)) {
                EmitCsvSampleRow(seq, hdr, pb.len);

                // Smoke calibration: track player anim-time candidates.
                if (g_cfg.mode == MODE_SMOKE && hdr.player_chr_ins != 0 && hdr.player_anim_id != 0) {
                    for (int i = 0; i < 4; ++i) {
                        AnimTrackUpdate(&g_calib.player_t[i], hdr.player_anim_id,
                                        hdr.player_anim_time[i]);
                    }
                }

                for (int e = 0; e < hdr.enemy_record_count && r.ok; ++e) {
                    EmitCsvEnemyRow(seq, e, r, hdr.schema_version);
                }
            }

            ConsumerRelease();
            didWork = true;
        }

        // Periodic flush + adaptive sampling.
        auto nowSc = std::chrono::steady_clock::now().time_since_epoch();
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowSc).count();
        if (nowMs >= flushDeadlineMs) {
            if (g_csvFile) fflush(g_csvFile);
            if (g_binFile) fflush(g_binFile);
            if (g_logFile) fflush(g_logFile);
            if (g_cfg.prediction_decision_log_enabled) {
                PredictionLogFlush();
            }
            flushDeadlineMs = nowMs + 1000;
        }
        // Check 7 timeout (qualification mode only): if user has been armed
        // for >=30s and no boss-bar enemy has appeared in the roster, log
        // a warning. This is the runtime confirmation of roster validation
        // check 7 — failure here means roster confidence is reduced (boss
        // bars not enrolling in WCM's prio queue, possibly a layout drift).
        bool armedNow = g_armed.load();
        if (armedNow && !armedPrev) {
            if (firstArmedMs == 0) firstArmedMs = nowMs;
        }
        armedPrev = armedNow;
        if (g_cfg.mode == MODE_QUALIFICATION && !check7WarningEmitted &&
            firstArmedMs > 0 && (nowMs - firstArmedMs) >= 30000 &&
            !g_check7Done.load()) {
            LogF("roster_check7_warn: 30s after first arm in qualification mode, "
                 "no boss-bar enemy has appeared in roster span. Roster confidence reduced.");
            check7WarningEmitted = true;
        }

        if (nowMs >= adaptiveCheckMs) {
            uint64_t totalDrops   = g_dropNoBuffer.load() + g_dropBudgetSkip.load() +
                                    g_dropProducerEmerg.load();
            uint64_t totalSamples = g_sampleSeq.load();

            // Advance the rolling-window bucket every 1 second.
            if (nowMs - lastBucketMs >= 1000) {
                rollIdx = (rollIdx + 1) % ROLL_BUCKETS;
                rollHist[rollIdx].drops = totalDrops;
                rollHist[rollIdx].samples = totalSamples;
                rollHist[rollIdx].initialized = true;
                lastBucketMs = nowMs;
            }

            // Compare against ~5s ago: use the bucket two slots ahead in the
            // ring (which represents the snapshot from 5s prior to current).
            int oldestIdx = (rollIdx + 1) % ROLL_BUCKETS;
            double dropRatio = 0.0;
            if (rollHist[oldestIdx].initialized) {
                uint64_t winDrops   = totalDrops   - rollHist[oldestIdx].drops;
                uint64_t winSamples = totalSamples - rollHist[oldestIdx].samples;
                uint64_t winAttempts = winSamples + winDrops;
                if (winAttempts > 0) {
                    dropRatio = (double)winDrops / (double)winAttempts;
                }
            }

            if (dropRatio > 0.05 && currentStep < 3) {
                currentStep += 1;
                g_adaptiveStep.store(currentStep, std::memory_order_relaxed);
                LogF("adaptive: step UP to %d (drop_ratio=%.3f over 5s rolling)",
                     currentStep, dropRatio);
                recoveryStartMs = 0;
            } else if (dropRatio < 0.01 && currentStep > 0) {
                if (recoveryStartMs == 0) {
                    recoveryStartMs = nowMs;
                } else if ((nowMs - recoveryStartMs) >= 30000) {
                    currentStep -= 1;
                    g_adaptiveStep.store(currentStep, std::memory_order_relaxed);
                    LogF("adaptive: step DOWN to %d (recovery, drop_ratio=%.3f)",
                         currentStep, dropRatio);
                    recoveryStartMs = 0;
                }
            } else {
                recoveryStartMs = 0;
            }
            adaptiveCheckMs = nowMs + 1000;        // poll every second
        }

        // Predictor tick — gated by prediction_enabled and cadence.
        if (g_cfg.prediction_enabled && nowMs >= nextPredictMs) {
            int64_t now_rel = nowMs - g_sessionStartMs;
            RunPredictorTick(pred_engine_cfg, now_rel, &pred_stats);
            // Schedule next tick. If we drifted (poll exceeded interval),
            // catch up to nowMs+interval rather than chaining drift.
            nextPredictMs = nowMs + pred_poll_ms;
        }

        // Per-tick GC of the boss latch map (cheap; only fires every 5s).
        if (nowMs >= nextGcMs) {
            GcBossLatch(nowMs - g_sessionStartMs);
            nextGcMs = nowMs + 5000;
        }

        // Tick-stats summary (no flush yet — flush happens with the
        // other logs at the 1s cadence below).
        if (g_cfg.prediction_enabled && nowMs >= predStatsLogMs) {
            LogF("predictor: bosses_seen=%d read_ok=%d target_known=%d decisions=%d (last 5s)",
                 pred_stats.bosses_seen, pred_stats.bosses_read_ok,
                 pred_stats.bosses_target_known, pred_stats.decisions_emitted);
            pred_stats = PredictionTickStats{};
            predStatsLogMs = nowMs + 5000;
        }

        // Wait until either the shutdown event fires or the next predictor
        // tick is due. If queue work happened this iteration, skip the
        // sleep (didWork=true) so we drain promptly.
        if (!didWork) {
            int64_t sleep_ms;
            if (g_cfg.prediction_enabled) {
                int64_t until_next = nextPredictMs - nowMs;
                sleep_ms = (until_next < 1) ? 1 : (until_next > 33 ? 33 : until_next);
            } else {
                sleep_ms = 2;  // legacy idle cadence when predictor off
            }
            if (g_shutdownEvent) {
                DWORD wr = WaitForSingleObject(g_shutdownEvent, (DWORD)sleep_ms);
                if (wr == WAIT_OBJECT_0) {
                    // Signaled — break out of the steady loop promptly.
                    break;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            }
        }
    }

    // Drain remaining (best effort).
    for (int n = 0; n < 1024; ++n) {
        int idx = ConsumerPeek();
        if (idx < 0) break;
        WriteBinRecord(g_pool[idx].data, g_pool[idx].len);
        ConsumerRelease();
    }

    WriteSessionEndManifest();
    WriteCalibrationReport();
    LogF("worker_exit: ticks=%llu samples=%llu drops_nb=%llu drops_bs=%llu drops_em=%llu trunc=%llu",
         (unsigned long long)g_tickCount.load(),
         (unsigned long long)g_sampleSeq.load(),
         (unsigned long long)g_dropNoBuffer.load(),
         (unsigned long long)g_dropBudgetSkip.load(),
         (unsigned long long)g_dropProducerEmerg.load(),
         (unsigned long long)g_truncatedSamples.load());

    // Close prediction log before session files since it has its own
    // critical section + handle.
    PredictionLogClose();

    CloseSessionFiles();
    BootLog("worker exit");
    BootLogClose();

    // NOTE: g_shutdownEvent is NOT closed here. Safe-leak to process exit
    // matches the existing teardown model — DllMain may still SetEvent
    // from the detach path, and closing the handle from worker while
    // DllMain might call SetEvent is a cross-thread race on a plain
    // HANDLE. The OS reaps the handle at process exit. (Codex flagged
    // this at review; same fix pattern the buffer pool uses.)
    return 0;
}

// ===========================================================================
// The detour (runs in game thread, every frame)
// ===========================================================================

static void DetourUpdateUIBarStructs(uintptr_t moveMapStep, uintptr_t time) {
    // Bump tick counter regardless of armed state — useful for diagnostics.
    uint64_t tick = g_tickCount.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_running.load(std::memory_order_relaxed) &&
        g_armed.load(std::memory_order_relaxed) &&
        g_initOk.load(std::memory_order_relaxed)) {

        int64_t qpcStart = QpcNow();
        __try {
            SampleOnce(tick, qpcStart);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Sample faulted — counter so the worker can log it.
            g_dropProducerEmerg.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Always chain to the original, forwarding both args unchanged.
    if (g_originalUpdateUIBar) {
        g_originalUpdateUIBar(moveMapStep, time);
    }
}

// ===========================================================================
// Hook installation (with rollback) + F11 watcher
// ===========================================================================

static bool g_hookInstalled = false;

bool InstallHook() {
    if (!g_refs.ready) return false;
    if (g_hookInstalled) return true;

    if (MH_Initialize() != MH_OK) {
        BootLog("hook_fail: MH_Initialize");
        return false;
    }

    LPVOID target = reinterpret_cast<LPVOID>(g_refs.updateUIBarFn);

    if (MH_CreateHook(target,
                      reinterpret_cast<LPVOID>(&DetourUpdateUIBarStructs),
                      reinterpret_cast<LPVOID*>(&g_originalUpdateUIBar)) != MH_OK) {
        BootLog("hook_fail: MH_CreateHook");
        MH_Uninitialize();
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        BootLog("hook_fail: MH_EnableHook");
        MH_RemoveHook(target);
        MH_Uninitialize();
        g_originalUpdateUIBar = nullptr;
        return false;
    }

    g_hookInstalled = true;
    BootLog("hook_ok: installed on UpdateUIBarStructs");
    return true;
}

DWORD WINAPI F11Thread(LPVOID) {
    bool prev = false;
    while (g_running.load()) {
        SHORT s = GetAsyncKeyState(VK_F11);
        bool pressed = (s & 0x8000) != 0;
        if (pressed && !prev) {
            bool was = g_armed.load();
            // v6.1: if user is about to ARM and roster is currently disabled,
            // give WCM one more chance — the user may have just loaded a save.
            if (!was && !g_roster.enabled) {
                BootLog("F11: roster recheck attempt (was disabled, retrying)");
                LogF("F11: roster recheck attempt");
                for (int i = 0; i < 10 && g_running.load(); ++i) {
                    if (ValidateRosterInit()) {
                        BootLog("F11: roster ENABLED on retry");
                        LogF("F11: roster ENABLED on retry");
                        // Re-emit manifest so post-session tooling sees roster_enabled=1.
                        // Version components are pulled from the live g_refs struct,
                        // so the four zeros here are placeholders that get overwritten.
                        WriteSessionManifest(0, 0, 0, 0, g_roster, g_sessionStartMs);
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                if (!g_roster.enabled) {
                    BootLog("F11: roster recheck FAILED, staying in fallback");
                    LogF("F11: roster recheck FAILED");
                }
            }
            g_armed.store(!was);
            if (!was) {
                BootLog("F11: armed");
                LogF("F11: armed");
                // v6.4 audible feedback: ARMED = two short low beeps (~250ms total).
                // 660 Hz is a comfortable mid-low tone, distinct from disarm.
                // Beep() blocks the calling thread — fine here, F11Thread is
                // dedicated and the 50ms sleep loop accommodates extra latency.
                Beep(660, 100);
                Beep(660, 100);
            } else {
                BootLog("F11: disarmed");
                LogF("F11: disarmed");
                // v6.4 audible feedback: DISARMED = one long high beep (~400ms).
                // 1320 Hz (octave above arm tone) + longer duration makes it
                // obviously different from ARMED even if you missed the visual.
                Beep(1320, 400);
            }
        }
        prev = pressed;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0;
}

// ===========================================================================
// DllMain (loader-lock-safe, module pinned)
// ===========================================================================
//
// Module pinned via GET_MODULE_HANDLE_EX_FLAG_PIN so a stray FreeLibrary
// can never unmap our code while the detour or worker is still running.
// On detach we set running=false and let the OS reclaim memory + close
// handles. The hook stays installed until process exit (safe-leak teardown).

BOOL WINAPI DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hMod);
            HMODULE pinned = nullptr;
            BOOL pinOk = GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCSTR>(&DllMain),
                &pinned);
            if (!pinOk) {
                DebugBanner("DLL_PROCESS_ATTACH FAILED: cannot pin module; refusing to load");
                return FALSE;
            }
            g_running.store(true);
            g_workerThread = CreateThread(nullptr, 0, WorkerMain, nullptr, 0, nullptr);
            DebugBanner("DLL_PROCESS_ATTACH (" PROBE_VERSION_STR ", module pinned)");
            break;
        }
        case DLL_PROCESS_DETACH: {
            g_running.store(false);
            // Wake the worker if it's blocked in WaitForSingleObject so it
            // exits the steady loop promptly. Best-effort signal only;
            // don't wait on threads inside DllMain (loader-lock hazard).
            if (g_shutdownEvent) {
                SetEvent(g_shutdownEvent);
            }
            if (g_workerThread) { CloseHandle(g_workerThread); g_workerThread = nullptr; }
            if (g_f11Thread)    { CloseHandle(g_f11Thread);    g_f11Thread = nullptr; }
            DebugBanner("DLL_PROCESS_DETACH (" PROBE_VERSION_STR ", safe-leak teardown)");
            break;
        }
    }
    return TRUE;
}
