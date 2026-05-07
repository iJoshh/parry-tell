// parry-tell-probe v5f — hook-based, F11-armed slot probe
//
// ARCHITECTURE (CHANGED FROM v5c):
//   v5c was a polling worker thread that read game memory on F11 edges.
//   v5d hooks UpdateUIBarStructs (PostureBarMod's hook target — a per-frame
//   UI update function in eldenring.exe). The detour runs synchronously
//   inside the game's frame loop, where game state is consistent.
//
//   F11 no longer drives the sample timing. F11 toggles an `armed` atomic.
//   The detour calls SampleOnce() iff armed AND >=1s since last sample.
//   Then it always chains to the original function.
//
//   This eliminates the polling-thread-vs-game-thread race that motivated
//   the synthesis recommendation in research/SYNTHESIS.md.
//
// FLOW:
//   1. DllMain attach: pin module, spawn worker thread.
//   2. Worker thread: open CSV, sig-scan for WCM + GetChrInsFromHandle +
//      UpdateUIBarStructs, install MinHook detour, watch F11 to toggle armed.
//   3. Detour (per-frame, game thread): if armed && cooldown elapsed,
//      call SampleOnce; then chain to original UpdateUIBarStructs.
//   4. SampleOnce: walk all 4 playerArray slots, log entity_id/block_id/
//      chr_type/handle for each (read-only).
//
// SAFETY:
//   - All reads SEH-wrapped (SafeRead<T>) with VirtualQuery prefilter.
//   - Module pinned (cannot be FreeLibrary'd while detour is live).
//   - Signature scanner: PE-section-filtered, unique-match required,
//     SEH-wrapped byte compare.
//   - Detour is __cdecl-compatible with the 47-byte prologue we hook (we
//     do not modify any registers; we only read game memory and chain).
//   - 1Hz cap on sampling even when armed — keeps log volume sane.
//   - F11 watcher thread does ZERO game-memory reads. It only flips
//     g_armed and emits CSV log lines.
//
// COMPATIBILITY:
//   - PostureBarMod uses the SAME hook target. We are NOT compatible
//     with it via load-order chaining: whichever mod loads SECOND will
//     fail to sig-scan (the first mod has patched the prologue with a
//     JMP). Per Codex v5d #2, we fail loudly with a diagnostic message
//     pointing at PostureBarMod when sig-scan returns 0 hits.
//   - For now: do not run parry-tell-probe and PostureBarMod together.
//     v6 may add hook-aware discovery if compatibility becomes important.
//   - This probe is non-destructive: detour does not modify any game
//     state; it only reads + logs + chains.
//
// TARGET: Elden Ring 2.6.1.0 (verified via FileVersion before install)
//
// FIXES CARRIED FORWARD FROM v5/v5b/v5c CODEX REVIEWS:
//   #1-3 (teardown/lifetime/DllMain): module pinned + minimal DllMain.
//   #4 (function-ptr identity): full-signature re-verify.
//   #5 (uniqueness): unique-match required for ALL three signatures.
//   #6 (SEH compare): MatchesAt is SEH-wrapped, no unguarded prefilter.
//   #7 (WCM shape): LooksLikeUserPtr applied to wcm.
//   #8 (POLLING POSTURE): RESOLVED IN v5d — we are now hook-based.
//   #9 (heuristic): LooksLikeUserPtr (canonical user mode + VirtualQuery).
//   #10 (sig brittleness): accepted, version-pinned to ER 2.6.1.0.
//   #11/#12 (dead flag, slot logging): fixed.
//
// FIXES FROM v5d CODEX REVIEW (research/v5d-codex-review.md):
//   #1 (detour ABI mismatch): typedef now matches PostureBarMod's two-arg
//      shape; both args forwarded unchanged when chaining.
//   #2 (stacked-hook coexistence claim): retracted; we now fail loudly
//      if sig-scan finds 0 hits (likely indicating another mod is here)
//      with a diagnostic pointing at PostureBarMod.
//   #3 (MinHook partial-failure rollback): InstallHook now calls
//      MH_RemoveHook + MH_Uninitialize on enable failure.
//   #4 (1Hz cooldown race): replaced load+store with compare_exchange.
//   #5 (CSV blocks game thread): TryEnterCriticalSection everywhere;
//      drop log line on contention rather than block the detour.
//   #6 (init_failed regressed dead flag): removed.
//
// FIXES IN v5e (from v5d in-game test data, 2026-05-07):
//   - Per-second hitch: dropped per-line fflush; CRT buffers writes
//     and worker thread flushes once per second off the game thread.
//   - GetChrInsFromHandle pass: was passing &handle (stack copy);
//     now passes &chrIns->handle (live struct address) like
//     PostureBarMod does. v5d data showed resolved == chrIns for
//     every sample (indicating the function returned input unchanged).
//   - Multi-offset probe: log canary fields at BOTH ChrIns layout
//     (entity_id +0x1E8, block_id +0x38) AND PlayerIns layout
//     (block_id +0x6D0, mapX +0x6C0, mapAngle +0x6CC) so the next
//     test settles which struct playerArray[0] actually points at.
//
// FIXES IN v5f (from v5e in-game test data, 2026-05-07):
//   - Hitch persisted in v5e despite fflush removal. Root cause was
//     ~30 VirtualQuery syscalls per sample (LooksReadable<T> +
//     LooksLikeUserPtr called per pointer hop x 4 slots). VirtualQuery
//     is a kernel transition, microseconds each. Fixed by introducing
//     LooksLikeUserPtrFast (pure compute, no syscalls) for hot-path
//     use; SEH wrapping in SafeRead<T> already catches actual faults
//     cleanly. Slow VirtualQuery-backed variants retained for init-
//     time signature scan validation.
//
// CONFIRMED IN v5e DATA:
//   - Slot 0 IS the local player (mapX_6C0 changes as player walks).
//   - playerArray[0] is a PlayerIns*, NOT a generic ChrIns*.
//     Real fields are at PlayerIns offsets (block +0x6D0, mapX +0x6C0,
//     mapAngle +0x6CC) — TarnishedTool's PlayerInsOffsets layout for
//     2.6.1, NOT PostureBarMod's ChrIns layout. v6+ should drop the
//     ChrIns offset probes (entity_id +0x1E8 etc.) since they are not
//     the right layout for this slot.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>

#include "MinHook.h"

// ---------- Global atomics ----------
static std::atomic<bool> g_running{false};         // worker thread alive
static std::atomic<bool> g_armed{false};           // F11 toggle: detour samples
static std::atomic<long long> g_lastSampleMs{0};   // 1Hz cooldown

// ---------- Logging ----------
static FILE* g_csv = nullptr;
static CRITICAL_SECTION g_csvLock;
static std::atomic<bool> g_csvReady{false};
static char g_csvPath[MAX_PATH] = {0};

static void DebugBanner(const char* msg) {
    char buf[1024];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[parry-tell-probe v5f] %s\n", msg);
    OutputDebugStringA(buf);
}

static void DebugFmt(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    char out[1100];
    _snprintf_s(out, sizeof(out), _TRUNCATE, "[parry-tell-probe v5f] %s\n", buf);
    OutputDebugStringA(out);
}

static void CsvOpen() {
    char dllPath[MAX_PATH] = {0};
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&CsvOpen, &self);
    if (self) {
        GetModuleFileNameA(self, dllPath, MAX_PATH);
        char* sep = strrchr(dllPath, '\\');
        if (sep) *(sep + 1) = '\0';
        _snprintf_s(g_csvPath, sizeof(g_csvPath), _TRUNCATE, "%sparry-tell-probe.csv", dllPath);
    } else {
        _snprintf_s(g_csvPath, sizeof(g_csvPath), _TRUNCATE, "parry-tell-probe.csv");
    }

    InitializeCriticalSection(&g_csvLock);
    fopen_s(&g_csv, g_csvPath, "a");
    if (g_csv) {
        fseek(g_csv, 0, SEEK_END);
        long sz = ftell(g_csv);
        if (sz == 0) {
            fprintf(g_csv, "ts_ms,event,detail\n");
        } else {
            fprintf(g_csv, "# === session start v5f ===\n");
        }
        fflush(g_csv);
        g_csvReady.store(true);
    }
}

// CSV writers use TryEnterCriticalSection — never blocks the caller.
// Per Codex v5d #5: detour runs in game thread; we can't risk it
// blocking on the F11 thread holding the CSV lock.
//
// v5e perf fix: dropped per-line fflush. v5d's per-second hitch was
// caused by ~8 fflushes per sample (each one a synchronous disk
// syscall, milliseconds on Windows). The CRT does its own buffering
// (~4KB), so fprintf alone is fast. The CSV is flushed periodically
// by a dedicated worker-thread flush loop and on session-end via
// CsvFinalFlush. Worst case on hard crash is losing the last few
// log lines, which is acceptable for diagnostics — we already lose
// far more in a crash dump than in a stale CSV buffer.
static void CsvLog(const char* event, const char* detail) {
    if (!g_csvReady.load() || !g_running.load()) return;
    if (!TryEnterCriticalSection(&g_csvLock)) return;
    if (g_csv) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        fprintf(g_csv, "%lld,%s,%s\n", ms, event, detail ? detail : "");
    }
    LeaveCriticalSection(&g_csvLock);
}

static void CsvComment(const char* fmt, ...) {
    if (!g_csvReady.load() || !g_running.load()) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (!TryEnterCriticalSection(&g_csvLock)) return;
    if (g_csv) {
        fprintf(g_csv, "# %s\n", buf);
    }
    LeaveCriticalSection(&g_csvLock);
}

// Periodic flush from the worker thread so we don't lose data over
// long sessions without forcing the game thread to wait on disk.
static void CsvPeriodicFlush() {
    if (!g_csvReady.load() || !g_running.load()) return;
    if (!TryEnterCriticalSection(&g_csvLock)) return;
    if (g_csv) {
        fflush(g_csv);
    }
    LeaveCriticalSection(&g_csvLock);
}

static void CsvFinalFlush() {
    if (!g_csvReady.load()) return;
    EnterCriticalSection(&g_csvLock);
    if (g_csv) {
        fprintf(g_csv, "# === session end v5f ===\n");
        fflush(g_csv);
    }
    LeaveCriticalSection(&g_csvLock);
    g_csvReady.store(false);
}

// ---------- SEH-wrapped read primitives ----------
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

// LooksReadable — slow path, uses VirtualQuery (kernel syscall ~µs).
// Use for one-time checks: signature scan address, init-time
// validation. NEVER call from the detour hot path.
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

// LooksLikeUserPtr — slow path, uses VirtualQuery. Init-time only.
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

// LooksLikeUserPtrFast — pure compute, no syscalls. Use in the detour
// hot path. Per v5e in-game data: VirtualQuery on the hot path causes
// a per-second hitch (~30 syscalls per sample). SEH wrapping in
// SafeRead<T> already catches real faults; the fast check here just
// rules out obviously bogus values (null, sub-64KB, alignment-wrong,
// non-canonical user-space). False positives (a "looks-good" value
// that turns out unreadable) cleanly degrade to SafeRead returning
// false.
static inline bool LooksLikeUserPtrFast(uintptr_t v) {
    if (v == 0) return false;
    if (v & 0x7) return false;
    if ((v >> 47) != 0) return false;
    if (v < 0x10000ULL) return false;
    return true;
}

// ---------- Pattern (signature) scanning ----------
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

// ---------- Layout (verified via SYNTHESIS.md) ----------
namespace Layout {
    constexpr int OFF_PLAYER_ARRAY = 0x10EF8;
    constexpr int PLAYER_ARRAY_LEN = 4;

    constexpr int CHR_INS_HANDLE     = 0x008;
    constexpr int CHR_INS_BLOCK_ID   = 0x038;
    constexpr int CHR_INS_CHR_TYPE   = 0x064;
    constexpr int CHR_INS_TEAM_TYPE  = 0x06C;
    constexpr int CHR_INS_MODULES    = 0x190;
    constexpr int CHR_INS_ENTITY_ID  = 0x1E8;
    constexpr int CHR_INS_TARGET_HND = 0x6A0;
}

// ---------- Game version check ----------
//
// Per Codex v5d post-review #3: we claim to be version-pinned to ER
// 2.6.1.0 but didn't enforce it. If the user runs the probe on a
// different ER build, our offsets may be wrong even if signatures
// happen to match. This function reads eldenring.exe's FileVersion
// resource and returns true iff it matches 2.6.1.0.
//
// Returns:
//   1 = matches expected version
//   0 = read succeeded but version is wrong
//  -1 = read failed (no version info / module missing) — caller decides
static int CheckExpectedGameVersion() {
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
        if (VerQueryValueA(verData, "\\", reinterpret_cast<LPVOID*>(&ffi), &ffiLen) && ffi && ffiLen >= sizeof(VS_FIXEDFILEINFO)) {
            WORD major = HIWORD(ffi->dwFileVersionMS);
            WORD minor = LOWORD(ffi->dwFileVersionMS);
            WORD build = HIWORD(ffi->dwFileVersionLS);
            WORD patch = LOWORD(ffi->dwFileVersionLS);
            DebugFmt("eldenring.exe FileVersion = %u.%u.%u.%u", major, minor, build, patch);
            CsvComment("eldenring.exe FileVersion = %u.%u.%u.%u", major, minor, build, patch);
            // Expected: 2.6.1.0
            result = (major == 2 && minor == 6 && build == 1 && patch == 0) ? 1 : 0;
        }
    }
    free(verData);
    return result;
}

// ---------- Game function signatures ----------
// WCM global (from PostureBarMod, verified):
static const char* SIG_WORLD_CHR_MAN =
    "48 8B 05 ? ? ? ? 48 85 C0 74 0F 48 39 88";

// GetChrInsFromHandle game function (from PostureBarMod, verified):
static const char* SIG_GET_CHR_INS_FROM_HANDLE =
    "48 83 EC 28 E8 17 FF FF FF 48 85 C0 74 08 48 8B 00 48 83 C4 28 C3";

// UpdateUIBarStructs hook target (from PostureBarMod, verified for 2.6.1):
// 48-byte signature, very specific prologue.
static const char* SIG_UPDATE_UI_BAR_STRUCTS =
    "40 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 C7 44 24 30 FE FF FF FF "
    "48 89 9C 24 B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 58 48";

// ---------- Resolved game refs ----------
struct GameRefs {
    uintptr_t wcmPtrAddr = 0;
    uintptr_t getChrInsFn = 0;
    uintptr_t updateUIBarFn = 0;
    bool ready = false;
};
static GameRefs g_refs;
typedef void* (*GetChrInsFromHandle_t)(void* worldChrMan, uint64_t* handlePtr);

// Hook function pointers — exact signature from PostureBarMod's
// reference (Hooking.hpp:186): two-arg, void return, default __fastcall
// in MSVC x64 ABI (RCX, RDX). Critical to match: dropping the second
// arg corrupts RDX before we chain to original.
typedef void (*UpdateUIBarStructs_t)(uintptr_t moveMapStep, uintptr_t time);
static UpdateUIBarStructs_t g_originalUpdateUIBar = nullptr;

static bool ResolveGameRefs() {
    HMODULE er = GetModuleHandleA("eldenring.exe");
    if (!er) return false;

    ScanResult wcmHits = ScanModuleExecForSig(er, SIG_WORLD_CHR_MAN);
    if (wcmHits.overflow || wcmHits.count != 1) {
        DebugFmt("FAIL: WCM sig hits=%d overflow=%d", wcmHits.count, wcmHits.overflow);
        return false;
    }
    uintptr_t wcmPtrAddr = RipRelativeDeref(wcmHits.addrs[0], 3);
    if (!wcmPtrAddr) return false;

    ScanResult getHits = ScanModuleExecForSig(er, SIG_GET_CHR_INS_FROM_HANDLE);
    if (getHits.overflow || getHits.count != 1) {
        DebugFmt("FAIL: getFn sig hits=%d overflow=%d", getHits.count, getHits.overflow);
        return false;
    }
    uintptr_t getFn = getHits.addrs[0];
    if (!VerifyFunctionAtAddr(getFn, SIG_GET_CHR_INS_FROM_HANDLE)) return false;

    ScanResult uiHits = ScanModuleExecForSig(er, SIG_UPDATE_UI_BAR_STRUCTS);
    if (uiHits.overflow || uiHits.count != 1) {
        // Common cause of hits=0: another mod already hooked this function
        // and patched its prologue (e.g. PostureBarMod). MinHook's chained
        // trampolines only help when the SECOND-loaded mod can still find
        // the original prologue, which our sig-based discovery cannot.
        // Fail loudly with diagnostic guidance per Codex v5d #2.
        DebugFmt("FAIL: UpdateUIBar sig hits=%d overflow=%d "
                 "(may indicate another mod is hooking this function — "
                 "PostureBarMod uses the same hook target; try unloading it)",
                 uiHits.count, uiHits.overflow);
        return false;
    }
    uintptr_t uiFn = uiHits.addrs[0];
    if (!VerifyFunctionAtAddr(uiFn, SIG_UPDATE_UI_BAR_STRUCTS)) return false;

    g_refs.wcmPtrAddr = wcmPtrAddr;
    g_refs.getChrInsFn = getFn;
    g_refs.updateUIBarFn = uiFn;
    g_refs.ready = true;
    DebugFmt("WCM ptr addr = 0x%016llX", (unsigned long long)wcmPtrAddr);
    DebugFmt("GetChrInsFromHandle = 0x%016llX", (unsigned long long)getFn);
    DebugFmt("UpdateUIBarStructs = 0x%016llX", (unsigned long long)uiFn);
    CsvComment("v5f init: WCM=0x%016llX getFn=0x%016llX uiFn=0x%016llX",
               (unsigned long long)wcmPtrAddr,
               (unsigned long long)getFn,
               (unsigned long long)uiFn);
    return true;
}

// ---------- Sample logic (called from detour, in game thread) ----------
static std::atomic<int> g_sampleSeq{0};

static void SampleOnce() {
    if (!g_refs.ready) return;

    int seq = ++g_sampleSeq;
    CsvComment("--- sample %d begin (in-detour) ---", seq);

    // v5f: hot path now uses LooksLikeUserPtrFast (pure compute) and
    // raw SafeRead (SEH-wrapped). VirtualQuery is too expensive at 30+
    // syscalls per sample on the game thread. SEH catches real faults
    // cleanly; the fast pointer-shape check rules out obviously bogus
    // values without entering the kernel.
    uintptr_t wcm = 0;
    if (!SafeRead<uintptr_t>(g_refs.wcmPtrAddr, &wcm) ||
        !LooksLikeUserPtrFast(wcm)) {
        CsvLog("sample_fail", "wcm read or shape failed");
        return;
    }
    CsvComment("wcm=0x%016llX", (unsigned long long)wcm);

    GetChrInsFromHandle_t getFn = reinterpret_cast<GetChrInsFromHandle_t>(g_refs.getChrInsFn);

    for (int slot = 0; slot < Layout::PLAYER_ARRAY_LEN; ++slot) {
        uintptr_t slotAddr = wcm + Layout::OFF_PLAYER_ARRAY + (slot * sizeof(uintptr_t));

        uintptr_t slotEntry = 0;
        if (!SafeRead<uintptr_t>(slotAddr, &slotEntry)) {
            char buf[64]; _snprintf_s(buf, sizeof(buf), _TRUNCATE, "slot=%d read_fail", slot);
            CsvLog("slot", buf);
            continue;
        }
        if (slotEntry == 0) {
            char buf[64]; _snprintf_s(buf, sizeof(buf), _TRUNCATE, "slot=%d empty", slot);
            CsvLog("slot", buf);
            continue;
        }
        if (!LooksLikeUserPtrFast(slotEntry)) {
            char buf[128]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d entry=0x%016llX shape=invalid", slot, (unsigned long long)slotEntry);
            CsvLog("slot", buf);
            continue;
        }

        uintptr_t chrInsPtr = 0;
        if (!SafeRead<uintptr_t>(slotEntry, &chrInsPtr) ||
            !LooksLikeUserPtrFast(chrInsPtr)) {
            char buf[128]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d entry=0x%016llX deref_fail", slot, (unsigned long long)slotEntry);
            CsvLog("slot", buf);
            continue;
        }

        uintptr_t handleAddr = chrInsPtr + Layout::CHR_INS_HANDLE;
        uint64_t handle = 0;
        if (!SafeRead<uint64_t>(handleAddr, &handle) ||
            handle == 0 || handle == UINT64_MAX) {
            char buf[160]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d chrIns=0x%016llX handle_invalid=0x%016llX",
                slot, (unsigned long long)chrInsPtr, (unsigned long long)handle);
            CsvLog("slot", buf);
            continue;
        }

        // v5e fix: pass POINTER TO LIVE HANDLE FIELD, not pointer to our
        // stack copy. PostureBarMod uses &(*playerArray[0])->handle —
        // i.e. the address of the handle inside the live struct. Passing
        // a stack-copy pointer made GetChrInsFromHandle return the input
        // back unchanged in v5d (resolved == chrIns for every sample).
        void* resolved = nullptr;
        __try {
            resolved = getFn(reinterpret_cast<void*>(wcm),
                             reinterpret_cast<uint64_t*>(handleAddr));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            resolved = nullptr;
        }
        uintptr_t resolvedAddr = reinterpret_cast<uintptr_t>(resolved);

        // v5e: probe MULTIPLE candidate offsets so we can identify the
        // actual struct layout empirically. v5d data showed entity_id at
        // +0x1E8 reads as 0 even when chain succeeds. TarnishedTool's
        // PlayerInsOffsets has block at +0x6D0, mapX at +0x6C0, mapAngle
        // at +0x6CC for 2.6.1 — values that change as Josh moves through
        // the world. We log both ChrIns-shape and PlayerIns-shape canary
        // fields so the next test session settles which struct
        // playerArray[0] actually points at.
        uint32_t entityId_1E8 = 0;
        int      blockId_38   = 0;
        int      chrType_64   = 0;
        int      blockId_6D0  = 0;
        float    mapX_6C0     = 0.0f;
        float    mapAng_6CC   = 0.0f;
        bool     readsOk      = false;

        if (resolved && LooksLikeUserPtrFast(resolvedAddr)) {
            uintptr_t e1E8 = resolvedAddr + 0x1E8;
            uintptr_t b38  = resolvedAddr + 0x038;
            uintptr_t t64  = resolvedAddr + 0x064;
            uintptr_t b6D0 = resolvedAddr + 0x6D0;
            uintptr_t mX   = resolvedAddr + 0x6C0;
            uintptr_t mA   = resolvedAddr + 0x6CC;

            bool a = SafeRead<uint32_t>(e1E8, &entityId_1E8);
            bool b = SafeRead<int>(b38,  &blockId_38);
            bool c = SafeRead<int>(t64,  &chrType_64);
            bool d = SafeRead<int>(b6D0, &blockId_6D0);
            bool e = SafeRead<float>(mX, &mapX_6C0);
            bool f = SafeRead<float>(mA, &mapAng_6CC);
            readsOk = a && b && c && d && e && f;
        }

        char detail[384];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
            "slot=%d chrIns=0x%016llX handle=0x%016llX resolved=0x%016llX "
            "ChrIns[ent_1E8=0x%08X blk_38=%d typ_64=%d] "
            "PlayerIns[blk_6D0=%d mapX_6C0=%.4f mapAng_6CC=%.4f] "
            "ok=%d",
            slot,
            (unsigned long long)chrInsPtr,
            (unsigned long long)handle,
            (unsigned long long)resolvedAddr,
            entityId_1E8, blockId_38, chrType_64,
            blockId_6D0, mapX_6C0, mapAng_6CC,
            readsOk ? 1 : 0);
        CsvLog("slot", detail);
    }

    CsvComment("--- sample %d end ---", seq);
}

// ---------- The detour (runs in game thread, every frame) ----------
//
// Signature matches the original exactly (two args, see PostureBarMod
// Hooking.hpp:186). We must forward both args unchanged when chaining.
//
// We never modify game state, never modify register values seen by
// the original function, and always chain to the original. The detour
// body is wrapped in SEH so any unexpected fault degrades to "no
// sample this frame" rather than crashing the host.
//
// Cooldown uses CAS so two near-simultaneous frames can't both pass
// the gate.
static void DetourUpdateUIBarStructs(uintptr_t moveMapStep, uintptr_t time) {
    if (g_running.load() && g_armed.load() && g_refs.ready) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        long long last = g_lastSampleMs.load();
        if (ms - last >= 1000) {
            // Atomic gate: only one frame wins the race.
            if (g_lastSampleMs.compare_exchange_strong(last, ms)) {
                __try {
                    SampleOnce();
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    CsvLog("sample_seh", "detour caught exception");
                }
            }
        }
    }

    // Always chain to the original, forwarding BOTH args unchanged.
    if (g_originalUpdateUIBar) {
        g_originalUpdateUIBar(moveMapStep, time);
    }
}

// ---------- Hook installation ----------
//
// Partial-failure rollback per Codex v5d #3: if any step succeeds but a
// later step fails, undo the prior step before returning. This avoids
// leaving MinHook in a partial-init state that could break later mods.
static bool InstallHook() {
    if (!g_refs.ready) return false;

    if (MH_Initialize() != MH_OK) {
        DebugBanner("FAIL: MH_Initialize");
        return false;
    }

    LPVOID target = reinterpret_cast<LPVOID>(g_refs.updateUIBarFn);

    if (MH_CreateHook(target,
                      reinterpret_cast<LPVOID>(&DetourUpdateUIBarStructs),
                      reinterpret_cast<LPVOID*>(&g_originalUpdateUIBar)) != MH_OK) {
        DebugBanner("FAIL: MH_CreateHook");
        // Roll back: tear down MinHook entirely.
        MH_Uninitialize();
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        DebugBanner("FAIL: MH_EnableHook");
        // Roll back: remove the hook we created, then uninitialize.
        MH_RemoveHook(target);
        MH_Uninitialize();
        g_originalUpdateUIBar = nullptr;
        return false;
    }

    DebugBanner("HOOK INSTALLED on UpdateUIBarStructs");
    CsvComment("hook installed");
    return true;
}

// ---------- F11 watcher (does NO game memory reads) ----------
static DWORD WINAPI F11Thread(LPVOID) {
    bool prev = false;
    while (g_running.load()) {
        SHORT s = GetAsyncKeyState(VK_F11);
        bool pressed = (s & 0x8000) != 0;
        if (pressed && !prev) {
            bool was = g_armed.load();
            g_armed.store(!was);
            if (!was) {
                DebugBanner("F11: armed");
                CsvLog("armed", "true");
            } else {
                DebugBanner("F11: disarmed");
                CsvLog("armed", "false");
            }
        }
        prev = pressed;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0;
}

// ---------- Worker thread (init + lifetime) ----------
static HANDLE g_workerThread = nullptr;
static HANDLE g_f11Thread = nullptr;

static DWORD WINAPI WorkerThread(LPVOID) {
    DebugBanner("worker thread up");
    CsvOpen();
    CsvComment("worker thread up; build %s %s", __DATE__, __TIME__);

    // Wait for eldenring.exe to be loaded, then check its FileVersion.
    // We refuse to install hooks unless the version is exactly 2.6.1.0
    // (the version our offsets are validated for). This prevents
    // accidentally running on a wrong patch where signatures coincide.
    int versionCheckResult = -1;
    for (int i = 0; i < 30 && g_running.load(); ++i) {
        versionCheckResult = CheckExpectedGameVersion();
        if (versionCheckResult >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (versionCheckResult == 0) {
        DebugBanner("FAIL: eldenring.exe is not version 2.6.1.0 — refusing to install hooks");
        CsvLog("init_fail", "wrong game version");
        // Stay alive but never install hooks.
        while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CsvFinalFlush();
        return 0;
    }
    if (versionCheckResult == -1) {
        DebugBanner("WARN: could not read eldenring.exe FileVersion; proceeding with caution");
        CsvLog("init_warn", "version check unavailable");
        // Don't fail — just warn. The user might have a slightly different
        // exe (e.g. with anti-cheat patches stripped) that lacks the version
        // resource but is still 2.6.1.0 binary-compatible.
    }

    // Sig scan with retries (game may still be initializing).
    for (int i = 0; i < 30 && g_running.load(); ++i) {
        if (ResolveGameRefs()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!g_refs.ready) {
        DebugBanner("FAIL: could not resolve game refs; idle");
        CsvLog("init_fail", "sig scan exhausted");
        // Stay alive to keep CSV open; never install hook without refs.
    } else if (!InstallHook()) {
        DebugBanner("FAIL: hook install failed; idle");
        CsvLog("init_fail", "hook install");
    } else {
        DebugBanner("READY: press F11 to arm/disarm sampling");
        CsvLog("ready", "press F11 to arm");
        // Spawn the F11 watcher.
        g_f11Thread = CreateThread(nullptr, 0, F11Thread, nullptr, 0, nullptr);
    }

    // Worker thread idles + periodic CSV flush. Flushes once per second
    // so we don't lose log data over long sessions, but never blocks the
    // game thread on disk I/O (the detour writes go straight into the
    // CRT buffer — flush happens here, off-thread).
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        CsvPeriodicFlush();
    }

    DebugBanner("worker thread exiting");
    CsvFinalFlush();
    return 0;
}

// ---------- DllMain (loader-lock-safe) ----------
//
// Module is pinned via GET_MODULE_HANDLE_EX_FLAG_PIN so a stray FreeLibrary
// can never unmap our code while the detour or worker is still running.
// On detach we set running=false and let the OS reclaim — no waits, no
// fclose, no DeleteCriticalSection. Safe leak. The hook stays installed
// until process exit, which is fine because the module is pinned.

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
                DebugBanner("DLL_PROCESS_ATTACH (v5f) FAILED: cannot pin module; refusing to load");
                return FALSE;
            }
            g_running.store(true);
            g_workerThread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
            DebugBanner("DLL_PROCESS_ATTACH (v5d, module pinned)");
            break;
        }
        case DLL_PROCESS_DETACH: {
            g_running.store(false);
            if (g_workerThread) { CloseHandle(g_workerThread); g_workerThread = nullptr; }
            if (g_f11Thread)    { CloseHandle(g_f11Thread);    g_f11Thread = nullptr; }
            DebugBanner("DLL_PROCESS_DETACH (v5f) - safe-leak teardown");
            break;
        }
    }
    return TRUE;
}
