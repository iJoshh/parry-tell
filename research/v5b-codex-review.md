# Adversarial Review — parry-tell-probe v5b (Codex, 2026-05-07)

## Verdict
**FIX-AND-RESHIP**.

Most of the previously reported issues are genuinely fixed.
One major crash-class lifetime bug remains (dynamic unload while worker may still execute DLL code), plus a couple hardening gaps.

## Findings (ordered by severity)

### 1) Dynamic unload can still unmap the DLL while `WorkerThread` is running (crash-class)
- File: `probe-v5.cpp:567`, `571`, `573`, `500`
- `DLL_PROCESS_DETACH` only sets `g_running=false` and closes the thread handle; it does not guarantee worker exit before unload.
- If detach is from `FreeLibrary` (`lpReserved == NULL`), module code can be unmapped while worker is still sleeping/scanning/calling `getFn`.
- This is the key remaining host-crash path.

### 2) Scanner still has one unguarded byte read before SEH compare
- File: `probe-v5.cpp:268-269`
- `MatchesAt` is SEH-wrapped, but first-byte prefilter `secBase[i]` is not.
- Usually fine on normal image mappings, but technically this reintroduces an AV surface if a scanned page is unreadable/execute-only.

### 3) 4-byte prologue check is too weak as an identity check
- File: `probe-v5.cpp:316`, `360`
- `48 83 EC 28` is common and not unique.
- It is a useful sanity check, but not enough to prove target identity if signature hit is wrong.
- In your pinned-version posture this is acceptable risk, but from pure adversarial safety it's still a weak guard.

## Prior review fix-status check

- Codex #1 teardown UAF: **fixed** (no `fclose`/`DeleteCriticalSection` in detach).
- Codex #2 unload-while-running: **not fully fixed** (resource UAF fixed, module-lifetime race remains).
- Codex #3 DllMain compliance: **mostly fixed** (I/O and waits moved out; remaining DllMain ops are minimal-ish).
- Codex #4 prologue check: **implemented**.
- Codex #5 section-filter + uniqueness: **implemented**.
- Codex #6 SEH compare: **implemented but incomplete** due unguarded prefilter byte read.
- Codex #7 WCM pointer shape validation: **implemented**.
- Codex #9 broad user-pointer check: **implemented**.
- Codex #11/#12 init_failed + slot logging: **implemented**.

## Direct answers to checklist

1. **Are claimed fixes correct?** Mostly yes. The one not fully closed is the unload/lifetime issue.

2. **Worker-thread CSV race (`g_csvReady` + `g_running`)** In current architecture, CSV calls are worker-thread-only, so this is effectively safe. If any second thread ever calls `CsvLog`, current ordering is not enough.

3. **PE section walk correctness** Structurally correct. Main remaining issue is the unguarded `secBase[i]` read.

4. **4-byte prologue false positives** Yes, possible. Treat as weak gate, not unique validator.

5. **DllMain ops legality** `DisableThreadLibraryCalls`, atomic stores, handle close are fine. `CreateThread` in DllMain commonly used but discouraged; safe only if you avoid synchronization/waits in DllMain (you now do). `OutputDebugStringA` usually tolerated.

6. **`CsvFinalFlush` then `g_csvReady=false` ordering** With single caller thread, order is fine. Multi-threaded would need rework.

## Bottom line

v5b is much better and removes the worst v5 teardown hazards.
Would not ship this exact build because the dynamic-unload lifetime hole is still crash-capable.

Minimal reship target:
1. Handle `lpReserved` in detach and define policy explicitly.
2. Guarantee module lifetime until worker exits (or forbid dynamic unload operationally).
3. Wrap/remove the scanner prefilter raw byte read (`secBase[i]`).

## How v5c addresses this (post-review fixes)

1. **Module pinning** via `GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, &DllMain, &pinned)` at DLL_PROCESS_ATTACH. The OS now refuses to unload us until process exit. `lpReserved` policy becomes irrelevant — we can't be unloaded.
2. **Prefilter byte read removed.** `MatchesAt` does the full SEH-wrapped check from byte 0.
3. **Prolog check upgraded** to `VerifyFunctionAtAddr` which re-runs the full signature pattern at the resolved function address. Combined with the unique-hit guarantee from sig-scan, this is strong identity.
