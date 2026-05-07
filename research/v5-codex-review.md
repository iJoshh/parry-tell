# Adversarial Review — parry-tell-probe v5 (Codex, 2026-05-07)

## Verdict
**BLOCK**.

v5 is safer than v4 on read volume, but it still has multiple host-crash paths in teardown/lifetime and one major arbitrary-call risk path if signature resolution is wrong. Do not ship this build into a real session.

## Critical bugs (would crash game or DLL on common paths)

### 1) Teardown race: worker can use freed `FILE*` / deleted critical section
- **File:line:** `probe/probe-v5.cpp:91-112`, `478-494`, `442-455`
- **What's wrong:** `DLL_PROCESS_DETACH` sets `g_running=false`, waits only 1s, then unconditionally `fclose(g_csv)` and `DeleteCriticalSection(&g_csvLock)`. If worker is still alive (blocked in sig scan / in `getFn` / scheduler delay), it can still call `CsvLog` or `CsvComment`, then `EnterCriticalSection` on deleted storage or `fprintf` on a closed `FILE*`.
- **Why this is crash-grade:** Undefined behavior here is classic AV/heap corruption/fast-fail territory, matching your v4 failure class.
- **What to do:** Never destroy shared logging state until worker exit is guaranteed. If you cannot guarantee exit in detach, leak safely (skip close/delete) instead of UAF. Better: move full shutdown out of `DllMain` and use explicit stop path or worker-owned cleanup via `FreeLibraryAndExitThread` pattern.

### 2) DLL can unload while worker is still executing code in that DLL
- **File:line:** `probe/probe-v5.cpp:480-485`, `422-459`
- **What's wrong:** After a 1s wait timeout, code closes thread handle and continues detach. If worker is still running, instruction pointer may still be inside `probe-v5.cpp` when module unmaps.
- **Why this is crash-grade:** Running unmapped code is immediate process crash.
- **What to do:** Treat `WAIT_TIMEOUT` as "do not unload resources/code yet" (safe leak) or redesign so worker self-terminates and owns module refcount until exit.

### 3) `DllMain` does blocking/synchronization and CRT/file I/O under loader lock
- **File:line:** `probe/probe-v5.cpp:470-472`, `482`, `486-489`
- **What's wrong:** `CsvOpen`/`CsvComment`/`fprintf`/`fclose` + `WaitForSingleObject` in `DllMain` are loader-lock hostile.
- **Why this is crash-grade:** Loader-lock deadlocks and shutdown-time crashes are realistic here, especially in injected-mod teardown.
- **What to do:** Keep `DllMain` minimal: set flags, maybe create primitives, return. Move I/O and waits outside `DllMain`.

### 4) Calling unvalidated signature hit as function pointer can execute arbitrary game code path
- **File:line:** `probe/probe-v5.cpp:286-294`, `329`, `380-382`
- **What's wrong:** First signature match is trusted as `GetChrInsFromHandle` and called. If it's a false hit, you are executing arbitrary bytes as a function.
- **Why `__try` is not enough:** SEH only catches faults in this thread; it does not undo memory corruption, bad writes, or engine-state mutation caused by wrong code path.
- **What to do:** Require stronger validation before call: executable section bounds, prologue fingerprint, nearby xrefs/context checks, and ideally multi-signal verification (not single raw pattern).

## High-risk bugs (would crash on uncommon but realistic paths)

### 5) Signature scanner says ".text" but scans entire image, returns first match only
- **File:line:** `probe/probe-v5.cpp:182-205`
- **What's wrong:** Scanner iterates `SizeOfImage` from module base, not PE section-filtered `.text`. It also accepts first hit without uniqueness check.
- **Failure mode:** False positives in non-code sections or wrong code clone -> bogus `wcmPtrAddr`/`getFn` -> eventual crash when used.
- **What to do:** Parse PE headers, scan executable code sections only, collect all matches, require exactly one expected hit or fail closed.

### 6) Scanner dereferences raw bytes without readability guard or SEH
- **File:line:** `probe/probe-v5.cpp:197-202`
- **What's wrong:** `base[i+j]` is read directly across full image window with no `VirtualQuery`/SEH guard.
- **Failure mode:** If a page in scan span is not readable, worker takes AV. Unhandled worker AV can kill host process.
- **What to do:** Page-walk readable ranges before scan, or wrap compare in SEH (with care for performance).

### 7) `wcm` pointer value is not shape-validated before pointer arithmetic and function call
- **File:line:** `probe/probe-v5.cpp:317-324`, `332`, `381`
- **What's wrong:** You validate readability of `g_refs.wcmPtrAddr` and nonzero `wcm`, but never gate `wcm` itself with canonical/heap/module sanity before using it as base and passing to game function.
- **Failure mode:** Bad signature -> plausible nonzero pointer -> random reads + hostile `getFn(world=garbage)` call.
- **What to do:** Validate `wcm` against expected region characteristics and known object invariants before any use.

### 8) Polling-thread posture introduces unsynchronized lifetime/race risks hook posture avoids
- **File:line:** `probe/probe-v5.cpp:422-455` vs PostureBarMod `PostureBarUI.cpp:428-452`
- **What's wrong:** v5 runs out-of-band from engine frame lifecycle. Reads and `getFn` calls can happen during transitions where game thread mutates/tears structures.
- **Failure mode:** transient invalid states, occasional bad handles, rare hangs/crashes during load/unload edges. Hooked posture executes in an engine-owned safe point and inherits its ordering.
- **What to do:** For production, prefer hook-context sampling with hotkey gating, not independent polling thread.

## Medium-risk issues (silent failure / wrong data)

### 9) Heap-pointer heuristic is overfit to a narrow VA band
- **File:line:** `probe/probe-v5.cpp:144-150`
- **What's wrong:** `LooksLikeHeapPtr` only accepts `high32` in `0x7FF3..0x7FF7`.
- **Failure mode:** Valid pointers outside that band are rejected -> false negatives, "empty/bad" slots, misleading diagnostics.
- **What to do:** Replace hardcoded high bits with canonical user-mode check + `VirtualQuery` type/protect checks.

### 10) GetChrIns signature is brittle across tiny binary changes
- **File:line:** `probe/probe-v5.cpp:254-257`
- **What's wrong:** Pattern hardcodes relative call displacement bytes (`E8 17 FF FF FF`) and entire short sequence.
- **Failure mode:** minor patch / compiler drift causes false miss (init never ready) or accidental wrong hit if cloned.
- **What to do:** Use a more semantic pattern (wildcard call disp, anchor unique neighborhood) and validate post-match.

### 11) Dead state flag: `init_failed` is written and never consumed
- **File:line:** `probe/probe-v5.cpp:263`, `434`
- **What's wrong:** `g_refs.init_failed` has no effect.
- **Failure mode:** misleading state model; future code may assume this flag gates behavior when it does nothing.
- **What to do:** Either remove it or actually use it in hotkey/log/control flow.

### 12) Slot read-fail logging drops slot index (`slot=?`)
- **File:line:** `probe/probe-v5.cpp:338`
- **What's wrong:** On `slotAddr` read failure, log loses which slot failed.
- **Failure mode:** diagnostics ambiguity; makes multiplayer slot resolution harder and can mask systematic slot-specific faults.
- **What to do:** include concrete `slot=%d` in that branch.

## Direct answers to attack checklist

1. **Crash safety:** yes, there are multiple crash-capable paths (teardown UAF, unload-while-running, loader-lock misuse, bad function-pointer execution).
2. **Sig scan correctness:** RIP math in `RipRelativeDeref` is correct (`dispAddr+4+disp`), but scanner correctness policy is weak (whole-image scan, first-hit wins, no uniqueness/prologue validation).
3. **Type punning/aliasing:** no obvious strict-aliasing UB in `SafeRead`; bigger risk is wrong-object usage from weak pointer/function validation.
4. **Threading/lifetime:** detach path is unsafe under concurrent logging and under slow/hung worker.
5. **DllMain compliance:** not compliant with strict loader-lock best practices.
6. **`GetChrInsFromHandle` call:** missing executable/provenance validation and timeout/liveness guard around call-side effects.
7. **Polling vs hook:** polling thread adds race/lifetime windows and teardown hazards hook posture largely avoids.
8. **Logic bug (outside above categories):** `init_failed` dead flag + `slot=?` logging defect.
