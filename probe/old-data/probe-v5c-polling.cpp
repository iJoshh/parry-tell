// parry-tell-probe v5c — minimal-invasion, hotkey-triggered slot probe
//
// CHANGES FROM v4 (which crashed the game):
//   - REMOVED: WCM startup memory dump
//   - REMOVED: prio queue walk
//   - REMOVED: continuous polling thread (now: hotkey-gated only)
//   - REMOVED: every read of WCM + 0x1E508 (PlayerIns, NOT a ChrIns; v4
//              type-confused this and read garbage at +0x1E8 etc.)
//   - ADDED: signature scan for WorldChrMan global pointer
//   - ADDED: signature scan for GetChrInsFromHandle game function
//   - ADDED: hotkey-triggered single-shot sampling (F11)
//   - ADDED: probes ALL 4 entries of WCM->playerArray (Seamless slot question)
//   - ADDED: handle-based ChrIns resolution (production-mod pattern)
//
// FIXES FROM CODEX v5 REVIEW (research/v5-codex-review.md):
//   - Teardown UAF (#1): DLL_PROCESS_DETACH no longer fcloses g_csv or
//     DeleteCriticalSection. Safe leak — process is dying anyway.
//   - Unload-while-running (#2): RESOLVED in v5c — module is pinned via
//     GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN) at attach.
//   - DllMain compliance (#3): no fopen, no WaitForSingleObject. Worker
//     thread opens its own CSV.
//   - Function-pointer validation (#4): VerifyFunctionAtAddr re-checks
//     full signature at the resolved fn address. Strong identity check.
//   - Sig scanner (#5): PE-section-filtered (exec sections only), unique-
//     match requirement, capped hit collection.
//   - SEH-wrapped byte compare (#6): MatchesAt is fully SEH-wrapped; v5c
//     also removed the unguarded prefilter byte read.
//   - WCM shape validation (#7): LooksLikeUserPtr applied to wcm.
//   - LooksLikeHeapPtr → LooksLikeUserPtr (#9): broader canonical user
//     mode + VirtualQuery sanity.
//   - Dead init_failed flag (#11): now consumed in hotkey path.
//   - slot=? log bug (#12): now logs slot index in all branches.
//
// FIXES FROM CODEX v5b REVIEW (research/v5b-codex-review.md):
//   - #1 Unload-while-running: module pin via GET_MODULE_HANDLE_EX_FLAG_PIN.
//   - #2 Unguarded prefilter byte: removed; MatchesAt does the full check.
//   - #3 4-byte prolog check too weak: replaced with full-signature
//     re-verify (VerifyFunctionAtAddr).
//
// EXPLICITLY NOT FIXED (deferred, documented):
//   - Hot-thread polling vs hook posture (v5 Codex #8): v5 is exploratory;
//     hook-based redesign deferred to v6 if v5 crashes. Hotkey gating is
//     a strong mitigation for now.
//   - Signature brittleness across game patches (v5 Codex #10): accepted;
//     game version is pinned and we control the install.
//
// TARGET: Elden Ring 2.6.1.0 (verified via FileVersion before install)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>

// ---------- Global atomics for safe teardown ----------
static std::atomic<bool> g_running{false};
static std::atomic<bool> g_workerExited{false};

// ---------- Logging (worker-thread only after init) ----------
static FILE* g_csv = nullptr;
static CRITICAL_SECTION g_csvLock;
static std::atomic<bool> g_csvReady{false};
static char g_csvPath[MAX_PATH] = {0};

static void DebugBanner(const char* msg) {
    char buf[1024];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[parry-tell-probe v5] %s\n", msg);
    OutputDebugStringA(buf);
}

static void DebugFmt(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    char out[1100];
    _snprintf_s(out, sizeof(out), _TRUNCATE, "[parry-tell-probe v5] %s\n", buf);
    OutputDebugStringA(out);
}

// Open CSV from worker thread (NOT DllMain). Loader-lock safe.
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
            fprintf(g_csv, "# === session start v5 ===\n");
        }
        fflush(g_csv);
        g_csvReady.store(true);
    }
}

static void CsvLog(const char* event, const char* detail) {
    if (!g_csvReady.load() || !g_running.load()) return;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    EnterCriticalSection(&g_csvLock);
    if (g_csv) {
        fprintf(g_csv, "%lld,%s,%s\n", ms, event, detail ? detail : "");
        fflush(g_csv);
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
    EnterCriticalSection(&g_csvLock);
    if (g_csv) {
        fprintf(g_csv, "# %s\n", buf);
        fflush(g_csv);
    }
    LeaveCriticalSection(&g_csvLock);
}

// Final flush from worker thread before it exits.
// We do NOT fclose or DeleteCriticalSection — process is dying, OS reaps
// resources. Avoiding teardown UAF is more important than pristine cleanup.
static void CsvFinalFlush() {
    if (!g_csvReady.load()) return;
    EnterCriticalSection(&g_csvLock);
    if (g_csv) {
        fprintf(g_csv, "# === session end v5 ===\n");
        fflush(g_csv);
    }
    LeaveCriticalSection(&g_csvLock);
    g_csvReady.store(false);  // future Csv* calls become no-ops
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

// Returns true iff [addr, addr+sizeof(T)) is in a readable, committed,
// non-guard, non-noaccess page.
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

// Canonical user-mode pointer check + VirtualQuery sanity. Replaces v5's
// narrow heap-band heuristic per Codex #9.
static bool LooksLikeUserPtr(uintptr_t v) {
    if (v == 0) return false;
    if (v & 0x7) return false;                      // 8-byte aligned
    // x86-64 user-mode is below 0x00007FFFFFFFFFFF; high bits must be zero.
    if ((v >> 47) != 0) return false;
    if (v < 0x10000ULL) return false;               // not in null/sentinel range
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(v), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
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

// SEH-wrapped buffer compare. Used so a flaky page in a code section can't
// take down the worker. Per Codex #6.
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

// Walk PE section table and scan only sections marked executable. Returns
// vector of all hits (capped). Per Codex #5: don't trust first hit; require
// uniqueness for important resolutions.
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
        if (secSize == 0) continue;
        if (secSize < (size_t)patLen) continue;

        size_t end = secSize - patLen;
        for (size_t i = 0; i <= end; ++i) {
            // No unguarded prefilter — MatchesAt is SEH-wrapped and
            // exits early on first mismatch. Per Codex v5b #2.
            if (MatchesAt(secBase + i, pattern, patLen)) {
                if (r.count < 8) {
                    r.addrs[r.count++] = reinterpret_cast<uintptr_t>(secBase + i);
                } else {
                    r.overflow = true;
                    return r;  // bail early — too many hits, sig is bad
                }
            }
        }
    }
    return r;
}

// Resolve `mov rax/rcx, [rip+disp32]`-shape RIP-relative reference at
// addr+addOffset. Returns absolute target.
static uintptr_t RipRelativeDeref(uintptr_t addr, int addOffset) {
    if (!addr) return 0;
    uintptr_t dispAddr = addr + addOffset;
    int32_t disp = 0;
    if (!LooksReadable<int32_t>(dispAddr)) return 0;
    if (!SafeRead<int32_t>(dispAddr, &disp)) return 0;
    return dispAddr + 4 + (int64_t)disp;
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
    constexpr int CHR_INS_ENTITY_ID  = 0x1E8;  // 1.8+ / 2.6.1
    constexpr int CHR_INS_TARGET_HND = 0x6A0;
}

// ---------- Game function signatures (PostureBarMod, verified) ----------
static const char* SIG_WORLD_CHR_MAN =
    "48 8B 05 ? ? ? ? 48 85 C0 74 0F 48 39 88";

static const char* SIG_GET_CHR_INS_FROM_HANDLE =
    "48 83 EC 28 E8 17 FF FF FF 48 85 C0 74 08 48 8B 00 48 83 C4 28 C3";

// Function staleness check: re-verify the FULL signature at the
// resolved address right before use. This catches concurrent code
// patching / memory remap between sig scan and call.
//
// IMPORTANT (per Codex v5c deep critic): this is NOT independent
// identity validation. If the sig scan returned a false positive
// (a unique-but-wrong match), VerifyFunctionAtAddr will still pass
// because the bytes ARE there. Identity rests on the unique-match
// guarantee from ScanModuleExecForSig + the prologue shape of the
// signature itself + version pinning to ER 2.6.1.0. v5c accepts
// this for an exploratory probe; v6 should add xref/section-context
// validation if a stronger identity guarantee is needed.
static bool VerifyFunctionAtAddr(uintptr_t fnAddr, const char* sig) {
    if (!fnAddr) return false;
    SigByte pattern[256];
    int patLen = 0;
    if (!ParseSig(sig, pattern, 256, &patLen) || patLen <= 0) return false;
    return MatchesAt(reinterpret_cast<const uint8_t*>(fnAddr), pattern, patLen);
}

// ---------- Resolved game state ----------
struct GameRefs {
    uintptr_t wcmPtrAddr = 0;
    uintptr_t getChrInsFn = 0;
    bool ready = false;
    bool init_failed = false;
};
static GameRefs g_refs;
typedef void* (*GetChrInsFromHandle_t)(void* worldChrMan, uint64_t* handlePtr);

static bool ResolveGameRefs() {
    HMODULE er = GetModuleHandleA("eldenring.exe");
    if (!er) {
        DebugBanner("eldenring.exe not loaded yet");
        return false;
    }

    // WCM signature: must be unique. If multiple matches, bail closed.
    ScanResult wcmHits = ScanModuleExecForSig(er, SIG_WORLD_CHR_MAN);
    if (wcmHits.overflow || wcmHits.count != 1) {
        DebugFmt("FAIL: WCM sig hit count = %d (overflow=%d)", wcmHits.count, wcmHits.overflow ? 1 : 0);
        return false;
    }
    uintptr_t wcmPtrAddr = RipRelativeDeref(wcmHits.addrs[0], /*Add*/3);
    if (!wcmPtrAddr) {
        DebugBanner("FAIL: WCM RIP deref failed");
        return false;
    }

    // GetChrInsFromHandle: must be unique AND have correct prologue.
    ScanResult getHits = ScanModuleExecForSig(er, SIG_GET_CHR_INS_FROM_HANDLE);
    if (getHits.overflow || getHits.count != 1) {
        DebugFmt("FAIL: getFn sig hit count = %d (overflow=%d)", getHits.count, getHits.overflow ? 1 : 0);
        return false;
    }
    uintptr_t getFn = getHits.addrs[0];
    if (!VerifyFunctionAtAddr(getFn, SIG_GET_CHR_INS_FROM_HANDLE)) {
        DebugFmt("FAIL: getFn signature re-verify at 0x%016llX", (unsigned long long)getFn);
        return false;
    }

    g_refs.wcmPtrAddr = wcmPtrAddr;
    g_refs.getChrInsFn = getFn;
    g_refs.ready = true;
    DebugFmt("WCM ptr addr = 0x%016llX", (unsigned long long)wcmPtrAddr);
    DebugFmt("GetChrInsFromHandle = 0x%016llX", (unsigned long long)getFn);
    CsvComment("v5 init: WCM=0x%016llX getFn=0x%016llX",
               (unsigned long long)wcmPtrAddr, (unsigned long long)getFn);
    return true;
}

// ---------- Sampling ----------
static std::atomic<int> g_sampleSeq{0};

static void SampleOnce() {
    if (!g_refs.ready) {
        DebugBanner("sample: refs not ready");
        return;
    }

    int seq = ++g_sampleSeq;
    CsvComment("--- sample %d begin ---", seq);

    // Step 1: deref WCM pointer global → WCM*.
    uintptr_t wcm = 0;
    if (!LooksReadable<uintptr_t>(g_refs.wcmPtrAddr) ||
        !SafeRead<uintptr_t>(g_refs.wcmPtrAddr, &wcm) ||
        wcm == 0) {
        CsvLog("sample_fail", "wcm ptr read failed or null");
        DebugFmt("sample %d: wcm read failed", seq);
        return;
    }
    // Validate WCM shape per Codex #7.
    if (!LooksLikeUserPtr(wcm)) {
        char buf[80]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "wcm shape invalid 0x%016llX", (unsigned long long)wcm);
        CsvLog("sample_fail", buf);
        DebugFmt("sample %d: %s", seq, buf);
        return;
    }
    DebugFmt("sample %d: wcm = 0x%016llX", seq, (unsigned long long)wcm);
    CsvComment("wcm=0x%016llX", (unsigned long long)wcm);

    // Step 2: walk all 4 playerArray slots.
    GetChrInsFromHandle_t getFn = reinterpret_cast<GetChrInsFromHandle_t>(g_refs.getChrInsFn);

    for (int slot = 0; slot < Layout::PLAYER_ARRAY_LEN; ++slot) {
        uintptr_t slotAddr = wcm + Layout::OFF_PLAYER_ARRAY + (slot * sizeof(uintptr_t));

        uintptr_t slotEntry = 0;
        if (!LooksReadable<uintptr_t>(slotAddr) ||
            !SafeRead<uintptr_t>(slotAddr, &slotEntry)) {
            char buf[64]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d read_fail", slot);
            CsvLog("slot", buf);
            continue;
        }
        if (slotEntry == 0) {
            char buf[64]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d empty", slot);
            CsvLog("slot", buf);
            continue;
        }
        if (!LooksLikeUserPtr(slotEntry)) {
            char buf[128]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d entry=0x%016llX shape=invalid", slot, (unsigned long long)slotEntry);
            CsvLog("slot", buf);
            continue;
        }

        // Deref to get ChrIns*.
        uintptr_t chrInsPtr = 0;
        if (!LooksReadable<uintptr_t>(slotEntry) ||
            !SafeRead<uintptr_t>(slotEntry, &chrInsPtr) ||
            chrInsPtr == 0 || !LooksLikeUserPtr(chrInsPtr)) {
            char buf[128]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d entry=0x%016llX deref_fail_or_bad", slot, (unsigned long long)slotEntry);
            CsvLog("slot", buf);
            continue;
        }

        // Step 3: read handle.
        uintptr_t handleAddr = chrInsPtr + Layout::CHR_INS_HANDLE;
        uint64_t handle = 0;
        if (!LooksReadable<uint64_t>(handleAddr) ||
            !SafeRead<uint64_t>(handleAddr, &handle) ||
            handle == 0 || handle == UINT64_MAX) {
            char buf[160]; _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "slot=%d chrIns=0x%016llX handle_invalid=0x%016llX",
                slot, (unsigned long long)chrInsPtr, (unsigned long long)handle);
            CsvLog("slot", buf);
            continue;
        }

        // Step 4: round-trip via GetChrInsFromHandle.
        void* resolved = nullptr;
        __try {
            resolved = getFn(reinterpret_cast<void*>(wcm), &handle);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            resolved = nullptr;
        }
        uintptr_t resolvedAddr = reinterpret_cast<uintptr_t>(resolved);

        // Step 5: read canary fields.
        uint32_t entityId = 0;
        int      blockId  = 0;
        int      chrType  = 0;
        bool     readsOk  = false;

        if (resolved && LooksLikeUserPtr(resolvedAddr)) {
            uintptr_t entAddr = resolvedAddr + Layout::CHR_INS_ENTITY_ID;
            uintptr_t blkAddr = resolvedAddr + Layout::CHR_INS_BLOCK_ID;
            uintptr_t typAddr = resolvedAddr + Layout::CHR_INS_CHR_TYPE;

            bool a = LooksReadable<uint32_t>(entAddr) && SafeRead<uint32_t>(entAddr, &entityId);
            bool b = LooksReadable<int>(blkAddr)      && SafeRead<int>(blkAddr, &blockId);
            bool c = LooksReadable<int>(typAddr)      && SafeRead<int>(typAddr, &chrType);
            readsOk = a && b && c;
        }

        char detail[256];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
            "slot=%d chrIns=0x%016llX handle=0x%016llX resolved=0x%016llX entity=0x%08X block=%d type=%d ok=%d",
            slot,
            (unsigned long long)chrInsPtr,
            (unsigned long long)handle,
            (unsigned long long)resolvedAddr,
            entityId, blockId, chrType, readsOk ? 1 : 0);
        CsvLog("slot", detail);
        DebugFmt("sample %d %s", seq, detail);
    }

    CsvComment("--- sample %d end ---", seq);
}

// ---------- Worker thread ----------
static DWORD WINAPI WorkerThread(LPVOID) {
    DebugBanner("worker thread up");
    CsvOpen();
    CsvComment("worker thread up; build %s %s", __DATE__, __TIME__);

    // Sig scan with retries (game may still be initializing).
    for (int i = 0; i < 30 && g_running.load(); ++i) {
        if (ResolveGameRefs()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!g_refs.ready) {
        g_refs.init_failed = true;
        DebugBanner("FAIL: could not resolve game refs after retries; idle");
        CsvLog("init_fail", "sig scan exhausted");
    } else {
        DebugBanner("READY: press F11 to sample");
    }

    // Hotkey watcher: edge-trigger F11.
    bool prev = false;
    while (g_running.load()) {
        SHORT s = GetAsyncKeyState(VK_F11);
        bool pressed = (s & 0x8000) != 0;
        if (pressed && !prev) {
            DebugBanner("F11 pressed");
            if (g_refs.init_failed) {
                CsvLog("hotkey_idle", "init_failed");
            } else if (g_refs.ready) {
                SampleOnce();
            } else {
                CsvLog("hotkey_idle", "refs_not_ready");
            }
        }
        prev = pressed;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    DebugBanner("worker thread exiting");
    CsvFinalFlush();
    g_workerExited.store(true);
    return 0;
}

// ---------- DllMain (loader-lock-safe) ----------
//
// Per Codex #1/#2/#3: DllMain does the absolute minimum. No fopen, no
// CriticalSection init, no WaitForSingleObject. Worker thread does all
// of that itself. On detach we set the running flag false and rely on
// the worker self-checking; if the worker is still inside getFn() when
// the process tears down, the OS reaps it. We do NOT wait or close
// resources — that's the path Codex flagged as UAF / unload-while-running.

static HANDLE g_workerThread = nullptr;

BOOL WINAPI DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hMod);
            // Pin the module so FreeLibrary can't unmap our code while
            // the worker is still running. Per Codex v5b #1 + v5c deep
            // critic: this is the canonical fix for the unload-while-
            // running race. Both flags are required: PIN does the pin,
            // FROM_ADDRESS interprets &DllMain as an address (otherwise
            // it would be treated as a module-name string and fail).
            // If the pin fails, we refuse to load — better to fail fast
            // at attach than to proceed with the unload race still open.
            HMODULE pinned = nullptr;
            BOOL pinOk = GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCSTR>(&DllMain),
                &pinned);
            if (!pinOk) {
                DebugBanner("DLL_PROCESS_ATTACH (v5c) FAILED: cannot pin module; refusing to load");
                return FALSE;
            }
            g_running.store(true);
            // CreateThread is loader-lock-safe (the new thread won't
            // run any DLL code until DllMain returns).
            g_workerThread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
            // OutputDebugStringA is permitted under loader lock per MSDN.
            DebugBanner("DLL_PROCESS_ATTACH (v5c, module pinned)");
            break;
        }
        case DLL_PROCESS_DETACH: {
            // Signal worker to stop; do NOT wait under loader lock and do
            // NOT close shared resources. Process is dying — let the OS
            // reclaim everything. This is the safe leak.
            g_running.store(false);
            if (g_workerThread) {
                CloseHandle(g_workerThread);
                g_workerThread = nullptr;
            }
            DebugBanner("DLL_PROCESS_DETACH (v5b) - safe-leak teardown");
            break;
        }
    }
    return TRUE;
}
