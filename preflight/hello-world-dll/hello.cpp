// hello.cpp — Preflight test DLL for parry-tell project.
//
// Purpose: verify that ME2 / Seamless Co-op can load an arbitrary DLL via the
// external_dlls mechanism. If this works, the real mod's DLL load path will work.
// If this fails, no other code matters until ME2/Seamless config is fixed.
//
// What this DLL does:
//   - On DLL_PROCESS_ATTACH: pins itself in memory (so the worker thread can't
//     outlive the loaded module), spawns a worker thread, and returns IMMEDIATELY.
//     Per Microsoft's loader-lock guidance, DllMain must do as little as possible.
//   - The worker thread does ALL the actual work: prints "[parry-tell] hello..."
//     via OutputDebugStringA (visible in DebugView), writes a log file in the
//     same directory as hello.dll, then heartbeats every 5 seconds for 30 seconds.
//
// Build: see hello.vcxproj in the same folder.
// Output: hello.dll
//
// Test procedure: see README.md in this folder.

#include <windows.h>
#include <stdio.h>

namespace {

HMODULE g_module = nullptr;  // pinned at DLL load; never freed
char    g_log_path[MAX_PATH] = {};

// Resolve a log path next to the DLL itself. Uses Win32 only — no CRT iostream,
// no STL allocations. Safe to call from DllMain (does not touch loader-lock-
// hostile APIs), but we still defer the call to the worker thread out of an
// abundance of caution.
void resolve_log_path() {
    char dll_path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(g_module, dll_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        // Fallback: write into Windows temp dir, which always exists.
        DWORD t = GetTempPathA(MAX_PATH, g_log_path);
        if (t == 0 || t >= MAX_PATH) {
            // Last resort: current directory. Always writable in a game process.
            g_log_path[0] = '.'; g_log_path[1] = '\\'; g_log_path[2] = 0;
        }
        // Append filename
        size_t cur = 0;
        while (g_log_path[cur]) ++cur;
        const char* fname = "parry-tell-hello.log";
        for (size_t i = 0; fname[i] && cur < MAX_PATH - 1; ++i, ++cur) {
            g_log_path[cur] = fname[i];
        }
        g_log_path[cur] = 0;
        return;
    }

    // dll_path looks like e.g. C:\path\to\hello.dll. Strip filename, append log name.
    // Find last backslash.
    int last_sep = -1;
    for (int i = 0; dll_path[i]; ++i) {
        if (dll_path[i] == '\\' || dll_path[i] == '/') last_sep = i;
    }
    if (last_sep < 0) {
        // No separator — just use the dll's directory equivalent.
        snprintf(g_log_path, MAX_PATH, "parry-tell-hello.log");
    } else {
        // Copy dll_path up to and including the separator, then append filename.
        for (int i = 0; i <= last_sep; ++i) g_log_path[i] = dll_path[i];
        g_log_path[last_sep + 1] = 0;
        snprintf(g_log_path + last_sep + 1, MAX_PATH - last_sep - 1,
                 "parry-tell-hello.log");
    }
}

void log_line(const char* msg) {
    // Channel 1: DebugView via OutputDebugString. Loader-lock-safe per Microsoft.
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");

    // Channel 2: log file via Win32 (no iostream, no CRT lazy init).
    HANDLE h = CreateFileA(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[512];
    int  n = snprintf(buf, sizeof(buf), "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
                      st.wYear, st.wMonth, st.wDay,
                      st.wHour, st.wMinute, st.wSecond, msg);
    if (n > 0 && n < (int)sizeof(buf)) {
        DWORD written = 0;
        WriteFile(h, buf, (DWORD)n, &written, nullptr);
    }
    CloseHandle(h);
}

DWORD WINAPI worker_thread(LPVOID /*param*/) {
    // ALL real work happens here, OUT of the loader lock. DllMain has already
    // returned by the time this thread starts executing.
    resolve_log_path();
    log_line("[parry-tell] hello from preflight DLL");
    log_line("[parry-tell] DLL loaded into host process — preflight gate 0.2c PASSED");

    char buf[256];
    snprintf(buf, sizeof(buf), "[parry-tell] log file: %s", g_log_path);
    log_line(buf);

    // Heartbeat: 6 ticks, 5 seconds apart, ~30 seconds total. Confirms the DLL
    // is genuinely running inside the host process, not just briefly loaded.
    for (int i = 1; i <= 6; ++i) {
        Sleep(5000);
        snprintf(buf, sizeof(buf),
                 "[parry-tell] heartbeat %d/6 (DLL alive in host process)", i);
        log_line(buf);
    }
    log_line("[parry-tell] heartbeat thread exiting cleanly");
    return 0;
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            // KEEP THIS MINIMAL. Microsoft loader-lock rules: do not call
            // anything that might allocate, take locks, or touch CRT in ways
            // that lazy-initialize. OutputDebugString is documented safe;
            // CreateThread is documented safe.
            DisableThreadLibraryCalls(hinst);

            // Pin the module: prevents the host from unloading us while the
            // worker thread is running. We never call FreeLibrary on g_module.
            // The DLL effectively has process lifetime.
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                                    (LPCSTR)&DllMain, &g_module)) {
                // Pin failed — bail rather than risk a use-after-unload crash
                // in the worker thread. ME2/Seamless will see the failure.
                OutputDebugStringA("[parry-tell] FAILED to pin module — aborting load\n");
                return FALSE;
            }

            // Spawn worker thread; it does everything else.
            HANDLE h = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
            if (h) {
                CloseHandle(h);  // we don't need the handle; thread runs to completion
            } else {
                OutputDebugStringA("[parry-tell] FAILED to spawn worker thread\n");
                // Module is already pinned; intentionally leak rather than
                // unwind in DllMain. Host process will tear us down on exit.
            }
            break;
        }
        // Intentionally NO logging on DLL_PROCESS_DETACH:
        //   - During graceful unload, loader lock is held.
        //   - During process termination, the world is on fire and CRT/file I/O
        //     may already be torn down. Logging here is a recipe for crashes.
        case DLL_PROCESS_DETACH:
        default:
            break;
    }
    return TRUE;
}
