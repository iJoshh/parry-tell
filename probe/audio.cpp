// audio.cpp -- Phase 4.2 audio cue implementation.
//
// Strategy: load a WAV once at worker init (file override OR embedded
// resource), keep the buffer alive for process lifetime, and fire
// PlaySoundW(buf, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) on each
// predictor fire decision. PlaySoundW returns immediately; OS handles
// playback off our thread. We never wait, never poll, never crash on
// failure -- predictor stays alive even if audio is dead.
//
// Failure modes:
//   - INI disabled        -> g_audio_disabled = true, all Fires are no-ops
//   - WAV override fails  -> fall back to embedded resource
//   - Resource missing    -> g_audio_disabled = true, BootLog once
//   - PlaySoundW returns FALSE on a Fire call -> LogF once, then silently
//                                                no-op forever (don't spam)

#include "audio.h"
#include "resource.h"

#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
#include <new>

#pragma comment(lib, "winmm.lib")

// ----- Externs from probe.cpp ----------------------------------------------
//
// probe.cpp owns the BootLog (init-time) + LogF (runtime) loggers and the
// DLL HMODULE. Phase 4.2 adds g_dllModule to probe.cpp -- see the patch
// block in DllMain that assigns it on DLL_PROCESS_ATTACH.

extern HMODULE g_dllModule;
// BootLog / LogF are C++ linkage (no extern "C") -- probe.cpp defines them
// as plain functions in this translation unit. We just need a matching
// declaration for the call sites below.
void BootLog(const char* fmt, ...);
void LogF(const char* fmt, ...);

// ----- Module state (worker-thread only -- see audio.h threading note) ----

static unsigned char* g_audio_buf            = nullptr;
static DWORD          g_audio_size           = 0;
static bool           g_audio_owns_buf       = false;   // true = heap, false = resource-locked
static bool           g_audio_disabled       = true;    // default disabled until Init succeeds
static bool           g_audio_logged_fire_fail = false;

// ----- Helpers --------------------------------------------------------------

// Minimal RIFF/WAVE header check. We don't walk inner chunks -- that's
// PlaySoundW's job and the kernel API tolerates more variations than a
// hand-rolled parser would. Truncated/malformed inner chunks make PlaySoundW
// return FALSE, which FireAudioCue handles by disabling audio for the
// session. The buffer is bounded by either our heap allocation (sized
// exactly to the file we read) or by SizeofResource (kernel-owned, sized
// to the embedded resource); PlaySoundW does not read past the buffer it
// is given even if a malformed chunk header claims more bytes. The
// override path takes only Josh-controlled WAV files at startup -- there
// is no untrusted-input attack surface here.
static bool LooksLikeRiffWave(const unsigned char* buf, size_t size) {
    if (!buf || size < 12) return false;
    if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F') return false;
    if (buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E') return false;
    return true;
}

// Try to load a user-supplied WAV from disk into a freshly-allocated heap
// buffer. Caller takes ownership on success (sets g_audio_owns_buf).
// Returns false (and logs) on any failure; caller falls back to resource.
static bool TryLoadWavFromFile(const char* path) {
    if (!path || !*path) return false;

    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        BootLog("audio_override_warn: cannot open '%s' (errno=%d); falling back to resource",
                path, errno);
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz_long = ftell(f);
    if (sz_long < 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }

    // Hard cap at 1 MB. PHASE4-PLAN says <= 1MB for override; embedded
    // resource is capped at 128 KB by spec but we don't enforce that here
    // (resource validation path has its own check).
    const long MAX_OVERRIDE_BYTES = 1024 * 1024;
    if (sz_long == 0 || sz_long > MAX_OVERRIDE_BYTES) {
        BootLog("audio_override_warn: '%s' size %ld out of range (1..%ld); falling back",
                path, sz_long, MAX_OVERRIDE_BYTES);
        fclose(f);
        return false;
    }
    DWORD sz = (DWORD)sz_long;

    unsigned char* buf = new (std::nothrow) unsigned char[sz];
    if (!buf) {
        BootLog("audio_override_warn: alloc %lu bytes failed; falling back", (unsigned long)sz);
        fclose(f);
        return false;
    }

    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    if (nread != sz) {
        BootLog("audio_override_warn: short read %zu/%lu from '%s'; falling back",
                nread, (unsigned long)sz, path);
        delete[] buf;
        return false;
    }

    if (!LooksLikeRiffWave(buf, sz)) {
        BootLog("audio_override_warn: '%s' not a RIFF/WAVE; falling back", path);
        delete[] buf;
        return false;
    }

    g_audio_buf      = buf;
    g_audio_size     = sz;
    g_audio_owns_buf = true;
    BootLog("audio_override_ok: loaded '%s' (%lu bytes)", path, (unsigned long)sz);
    return true;
}

// Locate and lock the embedded WAV resource. Sets g_audio_buf to a
// resource-owned pointer (do NOT free in shutdown).
static bool TryLoadWavFromResource() {
    if (!g_dllModule) {
        BootLog("audio_resource_fail: g_dllModule is NULL (DllMain didn't store HMODULE?)");
        return false;
    }

    HRSRC hRes = FindResourceW(g_dllModule, MAKEINTRESOURCEW(IDR_AUDIO_CUE_WAV), L"WAVE");
    if (!hRes) {
        BootLog("audio_resource_fail: FindResourceW(IDR_AUDIO_CUE_WAV, WAVE) returned NULL (err=%lu)",
                GetLastError());
        return false;
    }

    DWORD sz = SizeofResource(g_dllModule, hRes);
    if (sz == 0) {
        BootLog("audio_resource_fail: SizeofResource == 0");
        return false;
    }

    HGLOBAL hLoaded = LoadResource(g_dllModule, hRes);
    if (!hLoaded) {
        BootLog("audio_resource_fail: LoadResource returned NULL (err=%lu)", GetLastError());
        return false;
    }

    void* ptr = LockResource(hLoaded);
    if (!ptr) {
        BootLog("audio_resource_fail: LockResource returned NULL");
        return false;
    }

    if (!LooksLikeRiffWave((unsigned char*)ptr, sz)) {
        BootLog("audio_resource_fail: embedded resource is not RIFF/WAVE (size=%lu)",
                (unsigned long)sz);
        return false;
    }

    g_audio_buf      = (unsigned char*)ptr;
    g_audio_size     = sz;
    g_audio_owns_buf = false;
    BootLog("audio_resource_ok: embedded WAV %lu bytes", (unsigned long)sz);
    return true;
}

// ----- Public API ----------------------------------------------------------

bool InitAudioCue(const AudioCueConfig& cfg) {
    // Reset module state defensively (Init is called once per process today,
    // but be safe against future re-init scenarios).
    if (g_audio_buf && g_audio_owns_buf) {
        delete[] g_audio_buf;
    }
    g_audio_buf              = nullptr;
    g_audio_size             = 0;
    g_audio_owns_buf         = false;
    g_audio_disabled         = true;
    g_audio_logged_fire_fail = false;

    if (!cfg.enabled) {
        BootLog("audio_init: disabled by INI (audio_cue_enabled=false)");
        // Not a failure -- caller specifically asked for no audio.
        return true;
    }

    // Try override file first if specified.
    if (cfg.wav_path && *cfg.wav_path) {
        if (TryLoadWavFromFile(cfg.wav_path)) {
            g_audio_disabled = false;
            return true;
        }
        // fall through to resource fallback
    }

    if (TryLoadWavFromResource()) {
        g_audio_disabled = false;
        return true;
    }

    BootLog("audio_init_fail: no playable WAV available; cues will be silent");
    g_audio_disabled = true;
    return false;
}

void FireAudioCue() {
    if (g_audio_disabled || !g_audio_buf) return;

    // SND_MEMORY: buf is a raw RIFF/WAVE chunk, not a filename.
    // SND_ASYNC:  return immediately; OS plays in the background.
    // SND_NODEFAULT: if play fails, do NOT fall back to the Windows default
    //                beep -- silence is better than a misleading system sound.
    //
    // PlaySoundW's LPCWSTR is a polymorphic-pointer-with-flag-disambiguation;
    // when SND_MEMORY is set it's actually a const void* to bytes. The
    // reinterpret_cast is the standard pattern across MSDN samples.
    BOOL ok = PlaySoundW(reinterpret_cast<LPCWSTR>(g_audio_buf),
                         nullptr,
                         SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    if (!ok && !g_audio_logged_fire_fail) {
        LogF("audio_fire_fail: PlaySoundW returned FALSE (err=%lu); audio disabled for session",
             GetLastError());
        g_audio_logged_fire_fail = true;
        // First failure -> disable for the rest of the session. Avoids
        // burning hot-path cycles on PlaySoundW calls that won't succeed.
        // (PHASE4-PLAN allows this: "Prefer 'skip cue' over 'block or
        // crash'".)
        g_audio_disabled = true;
    }
}

void ShutdownAudioCue() {
    // Stop any in-flight playback. NULL filename + NULL handle is the
    // documented "stop all current PlaySound playback" idiom.
    PlaySoundW(nullptr, nullptr, 0);

    if (g_audio_buf && g_audio_owns_buf) {
        delete[] g_audio_buf;
    }
    g_audio_buf      = nullptr;
    g_audio_size     = 0;
    g_audio_owns_buf = false;
    g_audio_disabled = true;
}
