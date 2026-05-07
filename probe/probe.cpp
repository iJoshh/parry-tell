// MIT License — Copyright (c) 2026 Josh Blattner
//
// parry-tell probe DLL
//
// Purpose:
//   Gate 0 instrumentation for ER 1.16.1. This DLL logs live combat memory reads
//   into parry-tell-probe.csv for offline analysis. It intentionally does no UI,
//   no audio, and no gameplay writes.
//
// Output:
//   parry-tell-probe.csv next to this DLL.
//
// Safety model:
//   - DllMain is loader-lock safe and minimal (pin module + spawn worker).
//   - All host-process memory dereferences use SEH-wrapped reads.
//   - Read failures are logged from worker-thread context and the frame is skipped.

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// -----------------------------------------------------------------------------
// Offsets (ER 1.16.1)
// Credit: TarnishedTool by borgCode (MIT-licensed), summarized in
// archaeology/06-tarnishedtool-borrow-map.md and PROBE-SPEC.md.
// -----------------------------------------------------------------------------

// Module-relative roots/functions (add to eldenring.exe base from GetModuleHandleA(NULL)).
constexpr uintptr_t WORLD_CHR_MAN_BASE       = 0x3D65F88;  // TarnishedTool Offsets.cs:948-950
constexpr uintptr_t SOLO_PARAM_REPO_BASE     = 0x3D81EE8;  // TarnishedTool Offsets.cs:1394-1396
constexpr uintptr_t LOCKED_TARGET_HOOK_PTR   = 0x717372;   // TarnishedTool Offsets.cs:2151-2171
constexpr uintptr_t FN_CHR_INS_BY_HANDLE     = 0x507C70;   // TarnishedTool Offsets.cs:1703-1722
constexpr uintptr_t FN_GET_CHR_INS_BY_ENTITY = 0x507E00;   // TarnishedTool Offsets.cs:1820-1839

// Open question unresolved in this workspace (no archaeology/10):
// probe runs with WorldChrMan fallback instead of CSFeManImp boss bars.
constexpr uintptr_t CSFEMAN_IMP_BASE         = 0x0;
constexpr uintptr_t FE_BOSS_HP_BARS          = 0x0;
constexpr int       FE_BOSS_HP_BAR_COUNT     = 3;
constexpr uintptr_t FE_BOSS_HP_BAR_STRIDE    = 0x0;
constexpr uintptr_t FE_BOSS_HANDLE_OFFSET    = 0x8;
constexpr uint64_t  FE_INVALID_HANDLE        = 0xFFFFFFFFFFFFFFFFull;

// WorldChrMan fields (from *WORLD_CHR_MAN_BASE).
constexpr uintptr_t WCM_CHR_INS_BY_PRIO_BEGIN = 0x1F1B8;
constexpr uintptr_t WCM_CHR_INS_BY_PRIO_END   = 0x1F1C0;
constexpr uintptr_t WCM_CHR_SET_POOL          = 0x10EF8;
constexpr uintptr_t WCM_PLAYER_INS            = 0x1E508;   // TarnishedTool Offsets.cs:PlayerIns modern

// ChrIns fields.
constexpr uintptr_t CHR_INS_ENTITY_ID         = 0x80;      // PROBE-SPEC.md
constexpr uintptr_t CHR_INS_BLOCK_ID          = 0x6C;      // PROBE-SPEC.md
constexpr uintptr_t CHR_INS_NPC_PARAM_ID      = 0x60;      // PROBE-SPEC.md
constexpr uintptr_t CHR_INS_MODULE_BAG        = 0x190;     // PROBE-SPEC.md
constexpr uintptr_t CHR_INS_CHR_MANIPULATOR   = 0x580;     // PROBE-SPEC.md
constexpr uintptr_t CHR_INS_CHR_TYPE          = 0x68;      // PROBE-SPEC.md

// Modules/animation.
constexpr uintptr_t MOD_TIME_ACT              = 0x18;
constexpr uintptr_t MOD_AI_THINK_VIA_MANIP    = 0xC0;
constexpr uintptr_t TA_ANIMATION_ID           = 0xD0;
constexpr uintptr_t TA_ANIM_TIME_RAW_START    = 0xD4;      // log D4..DF raw bytes per spec

// AI Think fields.
constexpr uintptr_t AI_LAST_ACT               = 0xE9C2;
constexpr uintptr_t AI_FORCE_ACT              = 0xE9C1;
constexpr uintptr_t AI_NPC_THINK_PARAM_ID     = 0x28;      // TarnishedTool Offsets.cs:299
constexpr uintptr_t AI_TARGETING_SYSTEM       = 0xC480;    // TarnishedTool Offsets.cs:304
constexpr uintptr_t AI_SP_EFFECT_OBSERVE_COMP = 0xDBF0;    // TarnishedTool Offsets.cs:_=>0xDBF0 for 1.16.1

// SpEffectObserve linked-list entry.
constexpr uintptr_t SP_OBS_HEAD               = 0x10;
constexpr uintptr_t SP_OBS_NEXT               = 0x0;
constexpr uintptr_t SP_OBS_TARGET             = 0x18;

// Candidate sweep. Per PROBE-SPEC.md, Gate 0.B requires the full 0xE000..0xEFFC
// range (1024 columns). Earlier draft used a half-range; this is the spec-compliant
// version. CSV rows get wide but storage is cheap, and we don't want a false
// negative on Gate 0.B because the target field happened to live in the second
// half of the AI struct neighborhood.
constexpr uint32_t TARGET_CANDIDATE_RANGE_START = 0xE000;
constexpr uint32_t TARGET_CANDIDATE_RANGE_END   = 0xF000;  // exclusive
constexpr size_t   TARGET_CANDIDATE_COUNT =
    (TARGET_CANDIDATE_RANGE_END - TARGET_CANDIDATE_RANGE_START) / 4;

constexpr uint32_t INVALID_BLOCK_ID         = 0xFFFFFFFFu;
constexpr uint32_t GAME_READY_TIMEOUT_MS    = 60 * 1000;
constexpr uint32_t GAME_READY_POLL_MS       = 500;
constexpr uint32_t FRAME_SLEEP_MS           = 33;
constexpr uint32_t HEARTBEAT_MS             = 1000;

HMODULE g_module = nullptr;
char g_log_path[MAX_PATH] = {};

struct ReadDiagnostics {
    uint32_t failures = 0;
    uintptr_t first_fault_addr = 0;
};

thread_local ReadDiagnostics* g_active_diag = nullptr;

void note_read_failure(uintptr_t addr) {
    if (!g_active_diag) {
        return;
    }
    g_active_diag->failures++;
    if (g_active_diag->first_fault_addr == 0) {
        g_active_diag->first_fault_addr = addr;
    }
}

template <typename T>
std::optional<T> safe_read(uintptr_t addr) {
    T value{};
    __try {
        value = *reinterpret_cast<const T*>(addr);
        return value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        note_read_failure(addr);
        return std::nullopt;
    }
}

std::optional<std::array<uint8_t, 16>> safe_read_16(uintptr_t addr) {
    std::array<uint8_t, 16> out{};
    __try {
        memcpy(out.data(), reinterpret_cast<const void*>(addr), out.size());
        return out;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        note_read_failure(addr);
        return std::nullopt;
    }
}

void resolve_log_path() {
    char dll_path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(g_module, dll_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        DWORD t = GetTempPathA(MAX_PATH, g_log_path);
        if (t == 0 || t >= MAX_PATH) {
            g_log_path[0] = '.';
            g_log_path[1] = '\\';
            g_log_path[2] = 0;
        }
        size_t cur = 0;
        while (g_log_path[cur]) ++cur;
        const char* fname = "parry-tell-probe.csv";
        for (size_t i = 0; fname[i] && cur < MAX_PATH - 1; ++i, ++cur) {
            g_log_path[cur] = fname[i];
        }
        g_log_path[cur] = 0;
        return;
    }

    int last_sep = -1;
    for (int i = 0; dll_path[i]; ++i) {
        if (dll_path[i] == '\\' || dll_path[i] == '/') {
            last_sep = i;
        }
    }

    if (last_sep < 0) {
        snprintf(g_log_path, MAX_PATH, "parry-tell-probe.csv");
        return;
    }

    for (int i = 0; i <= last_sep; ++i) {
        g_log_path[i] = dll_path[i];
    }
    g_log_path[last_sep + 1] = 0;
    snprintf(g_log_path + last_sep + 1, MAX_PATH - last_sep - 1, "parry-tell-probe.csv");
}

void write_bytes(HANDLE file, const char* s, size_t len) {
    if (!s || len == 0 || file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, s, static_cast<DWORD>(len), &written, nullptr);
}

void append_text(HANDLE file, const char* s) {
    write_bytes(file, s, strlen(s));
}

struct BossSnapshot {
    int boss_slot = -1;
    uint64_t boss_handle = FE_INVALID_HANDLE;
    uintptr_t chr_ins = 0;
    uint32_t npc_param_id = 0;
    int32_t animation_id = 0;
    std::array<uint8_t, 16> anim_time_blob{};
    uint8_t last_act = 0;
    uint32_t npc_think_param_id = 0;
    uint32_t entity_id = 0;
    uint32_t sp_effect_observe_target = 0;
    std::array<uint32_t, TARGET_CANDIDATE_COUNT> candidates{};
    bool is_boss = true;
};

struct WorldState {
    uint64_t timestamp_ms = 0;
    uintptr_t world_chr_man = 0;
    uintptr_t player_chr_ins = 0;
    uint32_t player_entity_id = 0;
    uint64_t player_lock_on_target = 0;
    std::vector<BossSnapshot> bosses;
};

struct PlayerChainSample {
    uintptr_t world_chr_man = 0;
    uintptr_t player_slot_addr = 0;
    bool player_chr_read_ok = false;
    uintptr_t player_chr_ins = 0;
    bool player_entity_read_ok = false;
    uint32_t player_entity_id = 0;
};

struct CsvWriter {
    HANDLE file = INVALID_HANDLE_VALUE;
    bool ready = false;

    bool open_and_init(uintptr_t module_base, uintptr_t world_chr_man, uintptr_t csfeman, uintptr_t player_chr,
                       uint32_t player_entity_id) {
        file = CreateFileA(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size)) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            return false;
        }

        if (size.QuadPart == 0) {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            char line[1024];

            snprintf(line, sizeof(line), "# parry-tell probe v1\r\n");
            append_text(file, line);
            snprintf(line, sizeof(line), "# eldenring.exe module base: 0x%p\r\n", reinterpret_cast<void*>(module_base));
            append_text(file, line);
            snprintf(line, sizeof(line), "# WorldChrMan resolved: 0x%p\r\n", reinterpret_cast<void*>(world_chr_man));
            append_text(file, line);
            if (csfeman != 0) {
                snprintf(line, sizeof(line), "# CSFeManImp resolved: 0x%p\r\n", reinterpret_cast<void*>(csfeman));
            } else {
                snprintf(line, sizeof(line), "# CSFeManImp resolved: FAILED (offset unknown, fallback=WorldChrMan prio queue)\r\n");
            }
            append_text(file, line);
            snprintf(line, sizeof(line), "# player ChrIns: 0x%p\r\n", reinterpret_cast<void*>(player_chr));
            append_text(file, line);
            snprintf(line, sizeof(line), "# player entity ID: 0x%08X\r\n", player_entity_id);
            append_text(file, line);
            snprintf(line, sizeof(line), "# probe start time: %04u-%02u-%02u %02u:%02u:%02u\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            append_text(file, line);

            append_text(
                file,
                "timestamp_ms,event_type,boss_slot,boss_handle,boss_chr_ins,boss_npc_param_id,boss_animation_id,"
                "boss_animation_time,boss_last_act,player_entity_id,player_lock_on_target,");

            char col[96];
            for (uint32_t off = TARGET_CANDIDATE_RANGE_START; off < TARGET_CANDIDATE_RANGE_END; off += 4) {
                snprintf(col, sizeof(col), "target_candidate_offset_0x%04X,", off);
                append_text(file, col);
            }
            append_text(file, "sp_effect_observe_target,is_boss,notes\r\n");
        }

        ready = true;
        return true;
    }

    void close() {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        ready = false;
    }

    void log_comment(const char* msg) {
        if (!ready) return;
        append_text(file, "# ");
        append_text(file, msg);
        append_text(file, "\r\n");
    }

    void write_event_row(const char* event_type, const BossSnapshot* boss, const WorldState& state, const char* notes) {
        if (!ready) {
            return;
        }

        char line[4096];
        const int slot = boss ? boss->boss_slot : -1;
        const uint64_t handle = boss ? boss->boss_handle : FE_INVALID_HANDLE;
        const uintptr_t chr = boss ? boss->chr_ins : 0;
        const uint32_t npc_param = boss ? boss->npc_param_id : 0;
        const int32_t anim = boss ? boss->animation_id : 0;
        const uint8_t last_act = boss ? boss->last_act : 0;
        const uint32_t sp_target = boss ? boss->sp_effect_observe_target : 0;
        const int is_boss = (boss && boss->is_boss) ? 1 : 0;

        char anim_blob[33] = {};
        if (boss) {
            for (size_t i = 0; i < boss->anim_time_blob.size(); ++i) {
                snprintf(anim_blob + i * 2, sizeof(anim_blob) - i * 2, "%02X", boss->anim_time_blob[i]);
            }
        } else {
            snprintf(anim_blob, sizeof(anim_blob), "00000000000000000000000000000000");
        }

        int n = snprintf(line, sizeof(line), "%llu,%s,%d,0x%016llX,0x%p,%u,%d,%s,%u,%u,0x%016llX,",
            static_cast<unsigned long long>(state.timestamp_ms),
            event_type,
            slot,
            static_cast<unsigned long long>(handle),
            reinterpret_cast<void*>(chr),
            npc_param,
            anim,
            anim_blob,
            static_cast<unsigned int>(last_act),
            state.player_entity_id,
            static_cast<unsigned long long>(state.player_lock_on_target));
        if (n <= 0 || n >= static_cast<int>(sizeof(line))) {
            return;
        }
        write_bytes(file, line, static_cast<size_t>(n));

        if (boss) {
            for (size_t i = 0; i < TARGET_CANDIDATE_COUNT; ++i) {
                n = snprintf(line, sizeof(line), "%u,", boss->candidates[i]);
                if (n > 0 && n < static_cast<int>(sizeof(line))) {
                    write_bytes(file, line, static_cast<size_t>(n));
                }
            }
        } else {
            for (size_t i = 0; i < TARGET_CANDIDATE_COUNT; ++i) {
                append_text(file, "0,");
            }
        }

        n = snprintf(line, sizeof(line), "%u,%d,%s\r\n", sp_target, is_boss, notes ? notes : "");
        if (n > 0 && n < static_cast<int>(sizeof(line))) {
            write_bytes(file, line, static_cast<size_t>(n));
        }
    }
};

std::optional<uintptr_t> resolve_world_chr_man(uintptr_t module_base) {
    auto world_root = safe_read<uintptr_t>(module_base + WORLD_CHR_MAN_BASE);
    if (!world_root || *world_root == 0) {
        return std::nullopt;
    }
    return *world_root;
}

PlayerChainSample sample_player_chain(uintptr_t world_chr_man) {
    PlayerChainSample sample{};
    sample.world_chr_man = world_chr_man;
    sample.player_slot_addr = world_chr_man + WCM_PLAYER_INS;

    auto player_chr = safe_read<uintptr_t>(sample.player_slot_addr);
    sample.player_chr_read_ok = player_chr.has_value();
    if (player_chr) {
        sample.player_chr_ins = *player_chr;
    }

    if (sample.player_chr_ins != 0) {
        auto player_entity = safe_read<uint32_t>(sample.player_chr_ins + CHR_INS_ENTITY_ID);
        sample.player_entity_read_ok = player_entity.has_value();
        if (player_entity) {
            sample.player_entity_id = *player_entity;
        }
    }

    return sample;
}

std::optional<WorldState> snapshot_world(uintptr_t module_base, uint64_t elapsed_ms, uintptr_t world_chr_man) {
    WorldState state{};
    state.timestamp_ms = elapsed_ms;
    state.world_chr_man = world_chr_man;

    const PlayerChainSample player_chain = sample_player_chain(world_chr_man);
    state.player_chr_ins = player_chain.player_chr_ins;
    state.player_entity_id = player_chain.player_entity_id;

    auto lock_target = safe_read<uint64_t>(module_base + LOCKED_TARGET_HOOK_PTR);
    state.player_lock_on_target = lock_target.value_or(0);

    auto begin = safe_read<uintptr_t>(world_chr_man + WCM_CHR_INS_BY_PRIO_BEGIN);
    auto end = safe_read<uintptr_t>(world_chr_man + WCM_CHR_INS_BY_PRIO_END);
    if (!begin || !end || *begin == 0 || *end == 0 || *end < *begin) {
        return std::nullopt;
    }

    uintptr_t cur = *begin;
    const uintptr_t tail = *end;
    size_t iter = 0;
    while (cur + sizeof(uintptr_t) <= tail && iter < 8192) {
        iter++;
        auto chr_ptr = safe_read<uintptr_t>(cur);
        cur += sizeof(uintptr_t);
        if (!chr_ptr || *chr_ptr == 0) {
            continue;
        }

        if (*chr_ptr == state.player_chr_ins) {
            continue;
        }

        auto block = safe_read<uint32_t>(*chr_ptr + CHR_INS_BLOCK_ID);
        if (!block || *block == INVALID_BLOCK_ID) {
            continue;
        }

        BossSnapshot snap{};
        snap.boss_slot = -1;
        snap.boss_handle = FE_INVALID_HANDLE;
        snap.chr_ins = *chr_ptr;

        auto npc_param = safe_read<uint32_t>(*chr_ptr + CHR_INS_NPC_PARAM_ID);
        if (!npc_param) return std::nullopt;
        snap.npc_param_id = *npc_param;

        auto ent = safe_read<uint32_t>(*chr_ptr + CHR_INS_ENTITY_ID);
        if (!ent) return std::nullopt;
        snap.entity_id = *ent;

        auto mod_bag = safe_read<uintptr_t>(*chr_ptr + CHR_INS_MODULE_BAG);
        if (!mod_bag || *mod_bag == 0) {
            continue;
        }

        auto time_act = safe_read<uintptr_t>(*mod_bag + MOD_TIME_ACT);
        if (!time_act || *time_act == 0) {
            continue;
        }

        auto anim_id = safe_read<int32_t>(*time_act + TA_ANIMATION_ID);
        auto blob = safe_read_16(*time_act + TA_ANIM_TIME_RAW_START);
        if (!anim_id || !blob) {
            return std::nullopt;
        }
        snap.animation_id = *anim_id;
        snap.anim_time_blob = *blob;

        auto manip = safe_read<uintptr_t>(*chr_ptr + CHR_INS_CHR_MANIPULATOR);
        if (!manip || *manip == 0) {
            continue;
        }

        auto ai_think = safe_read<uintptr_t>(*manip + MOD_AI_THINK_VIA_MANIP);
        if (!ai_think || *ai_think == 0) {
            continue;
        }

        auto last_act = safe_read<uint8_t>(*ai_think + AI_LAST_ACT);
        auto think_id = safe_read<uint32_t>(*ai_think + AI_NPC_THINK_PARAM_ID);
        if (!last_act || !think_id) {
            return std::nullopt;
        }
        snap.last_act = *last_act;
        snap.npc_think_param_id = *think_id;

        // SpEffectObserveEntry.Target candidate.
        snap.sp_effect_observe_target = 0;
        auto obs_head_ptr = safe_read<uintptr_t>(*ai_think + AI_SP_EFFECT_OBSERVE_COMP + SP_OBS_HEAD);
        if (obs_head_ptr && *obs_head_ptr != 0) {
            auto next = safe_read<uintptr_t>(*obs_head_ptr + SP_OBS_NEXT);
            if (next && *next != 0 && *next != *obs_head_ptr) {
                auto tgt = safe_read<uint32_t>(*next + SP_OBS_TARGET);
                if (tgt) {
                    snap.sp_effect_observe_target = *tgt;
                }
            }
        }

        // Candidate sweep.
        for (size_t i = 0; i < TARGET_CANDIDATE_COUNT; ++i) {
            const uintptr_t off = TARGET_CANDIDATE_RANGE_START + static_cast<uintptr_t>(i) * 4;
            auto v = safe_read<uint32_t>(*ai_think + off);
            if (!v) {
                return std::nullopt;
            }
            snap.candidates[i] = *v;
        }

        // Fallback heuristic: with CSFeManImp unresolved we track all non-player ChrIns.
        // This keeps Margit visible in solo Gate 0 runs without requiring boss-bar pointers.
        snap.is_boss = true;
        state.bosses.push_back(snap);
    }

    return state;
}

const BossSnapshot* find_boss_by_chr(const WorldState& s, uintptr_t chr) {
    for (const auto& b : s.bosses) {
        if (b.chr_ins == chr) {
            return &b;
        }
    }
    return nullptr;
}

void emit_diff_rows(CsvWriter& csv, const std::optional<WorldState>& prev, const WorldState& curr) {
    bool wrote = false;

    if (!prev) {
        for (const auto& b : curr.bosses) {
            csv.write_event_row("boss_appeared", &b, curr, "initial_snapshot");
            wrote = true;
        }
        if (!wrote) {
            csv.write_event_row("tick", nullptr, curr, "initial_snapshot_empty");
        }
        return;
    }

    if (prev->player_lock_on_target != curr.player_lock_on_target) {
        csv.write_event_row("player_lock_changed", nullptr, curr, "");
        wrote = true;
    }

    for (const auto& b : curr.bosses) {
        const BossSnapshot* old = find_boss_by_chr(*prev, b.chr_ins);
        if (!old) {
            csv.write_event_row("boss_appeared", &b, curr, "");
            wrote = true;
            continue;
        }

        if (old->animation_id != b.animation_id || old->anim_time_blob != b.anim_time_blob) {
            csv.write_event_row("boss_animation_changed", &b, curr, "");
            wrote = true;
        }

        if (old->sp_effect_observe_target != b.sp_effect_observe_target || old->candidates != b.candidates) {
            csv.write_event_row("boss_target_field_changed", &b, curr, "");
            wrote = true;
        }
    }

    for (const auto& old : prev->bosses) {
        if (!find_boss_by_chr(curr, old.chr_ins)) {
            csv.write_event_row("boss_disappeared", &old, curr, "");
            wrote = true;
        }
    }

    static uint64_t last_heartbeat_ms = 0;
    if (!wrote && curr.timestamp_ms >= last_heartbeat_ms + HEARTBEAT_MS) {
        csv.write_event_row("tick", nullptr, curr, "heartbeat");
        last_heartbeat_ms = curr.timestamp_ms;
    }
}

DWORD WINAPI worker_thread(LPVOID) {
    OutputDebugStringA("[parry-tell-probe] worker thread started\n");

    resolve_log_path();
    {
        char banner[MAX_PATH + 64];
        snprintf(banner, sizeof(banner),
                 "[parry-tell-probe] log path resolved: %s\n", g_log_path);
        OutputDebugStringA(banner);
    }

    CsvWriter csv{};

    const uintptr_t module_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (module_base == 0) {
        OutputDebugStringA(
            "[parry-tell-probe] FATAL: GetModuleHandleA(NULL) returned 0; "
            "cannot resolve eldenring.exe base. Exiting.\n");
        return 0;
    }
    {
        char banner[128];
        snprintf(banner, sizeof(banner),
                 "[parry-tell-probe] eldenring.exe base = %p; "
                 "polling for WorldChrMan (up to 60s)\n",
                 reinterpret_cast<void*>(module_base));
        OutputDebugStringA(banner);
    }

    const uint64_t start_ms = GetTickCount64();
    uint64_t last_poll_log_ms = 0;

    uintptr_t world_chr_man = 0;
    bool game_ready = false;
    while (GetTickCount64() - start_ms < GAME_READY_TIMEOUT_MS) {
        ReadDiagnostics diag{};
        g_active_diag = &diag;
        auto wcm = resolve_world_chr_man(module_base);
        g_active_diag = nullptr;

        if (wcm && *wcm != 0) {
            world_chr_man = *wcm;
            game_ready = true;
            break;
        }

        const uint64_t now = GetTickCount64();
        if (now - last_poll_log_ms >= 5000) {
            char banner[160];
            snprintf(banner, sizeof(banner),
                     "[parry-tell-probe] still polling: WorldChrMan unresolved "
                     "after %llus (load a save to populate it)\n",
                     static_cast<unsigned long long>((now - start_ms) / 1000));
            OutputDebugStringA(banner);
            last_poll_log_ms = now;
        }

        Sleep(GAME_READY_POLL_MS);
    }

    if (game_ready) {
        OutputDebugStringA(
            "[parry-tell-probe] game-ready: WorldChrMan resolved; "
            "opening CSV and beginning capture\n");
    } else {
        OutputDebugStringA(
            "[parry-tell-probe] FATAL: game-ready timeout after 60s; "
            "WorldChrMan never resolved. CSV will be opened with the failure "
            "comment then closed. Quit ER and tell Josh.\n");
    }

    // Resolve player once for header block, then re-sampled each frame.
    uintptr_t player_chr_for_header = 0;
    uint32_t player_entity_for_header = 0;
    if (game_ready) {
        ReadDiagnostics diag{};
        g_active_diag = &diag;
        auto pchr = safe_read<uintptr_t>(world_chr_man + WCM_PLAYER_INS);
        if (pchr) {
            player_chr_for_header = *pchr;
            if (player_chr_for_header != 0) {
                auto pent = safe_read<uint32_t>(player_chr_for_header + CHR_INS_ENTITY_ID);
                if (pent) player_entity_for_header = *pent;
            }
        }
        g_active_diag = nullptr;
    }

    if (!csv.open_and_init(module_base, world_chr_man, 0, player_chr_for_header, player_entity_for_header)) {
        char banner[MAX_PATH + 128];
        snprintf(banner, sizeof(banner),
                 "[parry-tell-probe] FATAL: CsvWriter::open_and_init failed for %s; "
                 "check folder permissions. Exiting.\n", g_log_path);
        OutputDebugStringA(banner);
        return 0;
    }
    OutputDebugStringA("[parry-tell-probe] CSV opened; entering capture loop\n");

    if (!game_ready) {
        csv.log_comment("game-ready detection FAILED: WorldChrMan singleton did not resolve within 60s");
        csv.close();
        return 0;
    }
    csv.log_comment("game-ready detection OK: WorldChrMan singleton resolved");
    csv.log_comment("CSFeManImp unresolved in this build; using WorldChrMan prio-queue fallback");

    std::optional<WorldState> prev_state;
    bool player_first_resolved_logged = false;
    uint64_t frame_index = 0;
    uint32_t null_chain_frames_since_log = 0;
    uintptr_t null_chain_first_fault_addr = 0;
    uint32_t read_fail_frames_since_log = 0;
    uintptr_t read_fail_first_fault_addr = 0;

    for (;;) {
        ++frame_index;
        const uint64_t now_ms = GetTickCount64();
        const uint64_t elapsed = now_ms - start_ms;

        ReadDiagnostics diag{};
        g_active_diag = &diag;
        const PlayerChainSample player_chain = sample_player_chain(world_chr_man);
        auto current = snapshot_world(module_base, elapsed, world_chr_man);
        g_active_diag = nullptr;

        if (current) {
            current->player_chr_ins = player_chain.player_chr_ins;
            current->player_entity_id = player_chain.player_entity_id;
        }

        const bool player_now_nonnull = (player_chain.player_chr_ins != 0);
        const bool player_just_resolved = (!player_first_resolved_logged && player_now_nonnull);
        const bool emit_player_chain_log = ((frame_index % 60) == 0) || player_just_resolved;

        if (player_just_resolved) {
            player_first_resolved_logged = true;

            char banner[320];
            snprintf(
                banner, sizeof(banner),
                "[parry-tell-probe] player ChrIns first resolved at +%llums: chr=0x%016llX entity=0x%08X\n",
                static_cast<unsigned long long>(elapsed),
                static_cast<unsigned long long>(player_chain.player_chr_ins),
                player_chain.player_entity_id);
            OutputDebugStringA(banner);

            char msg[320];
            snprintf(
                msg, sizeof(msg),
                "[parry-tell-probe] player ChrIns first resolved at +%llums: chr=0x%016llX entity=0x%08X",
                static_cast<unsigned long long>(elapsed),
                static_cast<unsigned long long>(player_chain.player_chr_ins),
                player_chain.player_entity_id);
            csv.log_comment(msg);
        }

        if (emit_player_chain_log) {
            char msg[384];
            snprintf(
                msg, sizeof(msg),
                "player_chain hop_results: wcm=0x%016llX wcm+0x1E508=0x%016llX read_ok=%c chr=0x%016llX entity_read_ok=%c entity=0x%08X",
                static_cast<unsigned long long>(player_chain.world_chr_man),
                static_cast<unsigned long long>(player_chain.player_slot_addr),
                player_chain.player_chr_read_ok ? 'Y' : 'N',
                static_cast<unsigned long long>(player_chain.player_chr_ins),
                player_chain.player_entity_read_ok ? 'Y' : 'N',
                player_chain.player_entity_id);
            csv.log_comment(msg);
        }

        if (diag.failures > 0) {
            ++read_fail_frames_since_log;
            if (read_fail_first_fault_addr == 0) {
                read_fail_first_fault_addr = diag.first_fault_addr;
            }
            if (read_fail_frames_since_log >= 60) {
                char msg[320];
                snprintf(msg, sizeof(msg),
                         "%u frames read_failures since last_log; first_fault_addr=0x%016llX",
                         read_fail_frames_since_log,
                         static_cast<unsigned long long>(read_fail_first_fault_addr));
                csv.log_comment(msg);
                read_fail_frames_since_log = 0;
                read_fail_first_fault_addr = 0;
            }
            Sleep(FRAME_SLEEP_MS);
            continue;
        }

        if (!current) {
            ++null_chain_frames_since_log;
            // Note: null_chain implies snapshot_world returned nullopt despite
            // diag.failures==0 -- i.e. a logical guard hit (null pointer in
            // chain or invalid begin/end pair) rather than a SEH read fault.
            // first_fault_addr is meaningless here so we don't log it.
            if (null_chain_frames_since_log >= 60) {
                char msg[320];
                snprintf(msg, sizeof(msg),
                         "%u frames null_chain since last_log "
                         "(snapshot_world returned nullopt with no read fault; "
                         "likely null pointer in resolved chain or invalid prio range)",
                         null_chain_frames_since_log);
                csv.log_comment(msg);
                null_chain_frames_since_log = 0;
                null_chain_first_fault_addr = 0;
            }
            Sleep(FRAME_SLEEP_MS);
            continue;
        }

        emit_diff_rows(csv, prev_state, *current);
        prev_state = std::move(current);

        Sleep(FRAME_SLEEP_MS);
    }
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);

        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCSTR>(&DllMain), &g_module)) {
            return FALSE;
        }

        HANDLE h = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
        if (!h) {
            // Worker thread is the entire reason this DLL exists. If we can't
            // start it, fail the load loudly so Josh sees the problem in the
            // Seamless launcher log instead of silently running with no probe
            // collection. The OS unloads us; the host process continues.
            OutputDebugStringA(
                "[parry-tell-probe] FATAL: CreateThread failed; "
                "aborting DLL load so failure is visible.\n");
            return FALSE;
        }
        CloseHandle(h);
    }
    return TRUE;
}
