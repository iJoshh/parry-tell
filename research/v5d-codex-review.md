# Adversarial Review — parry-tell-probe v5d (Codex, 2026-05-07)

## Verdict
**BLOCK**.

v5d fixes the v5c polling posture risk, but introduces/retains two ship-blocking issues:
1. Likely ABI mismatch in the detour trampoline call path.
2. Practical incompatibility with PostureBarMod load-order despite claims of clean stacked hooking.

## Findings (ordered by severity)

### 1) Detour prototype is likely wrong for this target (crash/UB risk)
- Probe uses one-arg `void` signature: `probe-v5d.cpp:347, 500, 518`
- PostureBarMod reference for same target uses two args `(uintptr_t, uintptr_t)`: `Hooking.hpp:186`, `PostureBarUI.hpp:294`, `PostureBarUI.cpp:428, 430`
- Detour body calls lots of code before chaining, so arg2 register is not reliably preserved.
- Fix: change to two-arg signature end-to-end and forward both args.

### 2) Claimed "stacked hook compatibility" is not generally true
- Resolver requires exact live prologue bytes. MinHook patches entry bytes to jump when enabled (`hook.c:370`).
- Result: if one mod hooks first, second mod may fail signature resolution and never hook.
- Fix: make target resolution hook-aware or explicitly declare/guard unsupported coexistence.

### 3) Partial MinHook failure cleanup is missing
- No rollback if `MH_CreateHook` succeeds but `MH_EnableHook` fails.
- Fix: remove hook on enable failure; optionally uninitialize on terminal init failure.

### 4) 1Hz gate is non-atomic check/store
- Race window in cooldown gate.
- Fix: CAS-based timestamp update.

### 5) Detour path can block on logging I/O (frametime risk)
- Detour executes sampling in game thread.
- `CsvLog` takes lock and `fflush`es every line.
- Fix: TryEnterCriticalSection or move I/O off detour thread.

### 6) `init_failed` regressed to dead state
- Written but never read.

## Direct answers to the 10 attack points

1. **Detour correctness:** not safe; one-arg typedef conflicts with two-arg reference. Blocker.
2. **MinHook lifecycle:** worker-thread init is good; detach unhook unnecessary with pinned safe-leak; rollback cleanup on partial failure missing.
3. **Compatibility with PostureBarMod:** chaining possible in principle, but signature-based discovery often breaks load-order coexistence.
4. **Detour SEH:** acceptable; not swallowing exceptions from original.
5. **1Hz cooldown race:** yes, possible double fire; CAS recommended.
6. **F11 thread races:** no crash-class race found.
7. **Game-thread reentrancy/blocking:** yes, game thread can block on CSV lock.
8. **MinHook failure modes:** missing cleanup for partial init.
9. **Hook target stability over time:** low risk for ER code pages.
10. **Other crash vectors:** dominant one is detour ABI mismatch.

## Prior finding closure status in v5d

- v5 #1 teardown UAF: closed.
- v5 #2 unload-while-running: closed (module pin).
- v5 #3 DllMain compliance: mostly closed.
- v5 #4 function-ptr identity: closed to prior bar.
- v5 #5 sig scanner uniqueness: closed.
- v5 #6 SEH compare: closed.
- v5 #7 WCM shape validation: closed.
- v5 #9 user-pointer heuristic: closed.
- v5 #11/#12 dead flag + slot logging: #12 closed, #11 regressed/open.
- v5b #1 dynamic unload: closed.
- v5b #2 unguarded prefilter: closed.
- v5b #3 weak prologue check: closed.

## How v5d (post-review) addresses each finding

### #1 Detour ABI — FIXED
Typedef now `void(*)(uintptr_t moveMapStep, uintptr_t time)`. Detour signature matches. Both args forwarded unchanged when chaining to original.

### #2 Coexistence claim — RETRACTED
Source comments updated to explicitly state v5d is NOT compatible with PostureBarMod via load-order chaining. The sig-scan failure path now logs a diagnostic naming PostureBarMod when 0 hits, so the user knows what to do.

### #3 Partial-failure rollback — FIXED
InstallHook calls `MH_RemoveHook(target) + MH_Uninitialize()` on enable failure, and `MH_Uninitialize()` on create failure. Original-fn pointer cleared.

### #4 CAS cooldown — FIXED
`g_lastSampleMs.compare_exchange_strong(last, ms)` gates SampleOnce. Two near-simultaneous frames cannot both pass.

### #5 Logging I/O blocks game thread — FIXED
Both CsvLog and CsvComment use TryEnterCriticalSection. On contention they drop the log line rather than block. Acceptable for diagnostics.

### #6 Dead flag — FIXED
`init_failed` removed from struct + worker thread.
