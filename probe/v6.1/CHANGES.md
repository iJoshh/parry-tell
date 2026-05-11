# Probe v6.1 — WCM init resilience

Status: drafted 2026-05-10. NOT applied yet. Apply only if smoke or qualification
confirms we need the roster path enabled for trash-mob discovery (Stormveil).

## What's wrong in v6.0

On 2026-05-09, three live smoke runs all produced `roster: disabled (fall back
to player + boss bars only)` because the probe's `ValidateRosterInit()` retry
window (30 iterations × 500ms = 15s) was exhausted before the user got past
the title screen and into the world. WCM is mapped in the process but its
contents (the enemy roster span) only populate once a save is loaded.

The fallback path is correct for **smoke** (player anim-time only) — confirmed
by the 9,867 clean samples captured on smoke-20260509-170547. But for
**discovery** in Stormveil with trash mobs (Banished Knights, etc. that won't
trigger a boss bar), we need the WCM roster path to be live.

## Three changes

### C1 — Extend initial WCM grace from 15s to 60s

`probe.cpp:2745`. Change the retry-loop iteration count from 30 to 120. The
500ms sleep stays. Effect: probe waits up to 60s for WCM contents to become
readable. This covers the slow case where the user is mashing through the
title screen and save-select menus.

```diff
     // (g) roster validation (NOT fail-closed)
     bool rosterOk = false;
-    for (int i = 0; i < 30 && g_running.load(); ++i) {
+    for (int i = 0; i < 120 && g_running.load(); ++i) {
         if (ValidateRosterInit()) { rosterOk = true; break; }
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
     }
```

### C2 — Reattempt WCM validation on F11-arm if currently disabled

`probe.cpp:3017` (F11Thread). When the user presses F11 to arm, and we're
currently in roster-disabled fallback, give WCM one more chance: run up to
10 quick validation attempts (500ms apart = 5s) before arming. If successful,
log a manifest event saying roster came online mid-session.

This handles the common case where the user loads into the world, walks
around for a few seconds, and only then presses F11. The user's expectation
("when I press F11 it should work") aligns with this behavior.

```diff
 DWORD WINAPI F11Thread(LPVOID) {
     bool prev = false;
     while (g_running.load()) {
         SHORT s = GetAsyncKeyState(VK_F11);
         bool pressed = (s & 0x8000) != 0;
         if (pressed && !prev) {
             bool was = g_armed.load();
+            // If we're about to ARM and the roster has been disabled,
+            // give WCM one more chance — the user may have just loaded
+            // into the world.
+            if (!was && !g_roster.enabled) {
+                BootLog("F11: roster recheck attempt (was disabled, retrying)");
+                LogF("F11: roster recheck attempt");
+                for (int i = 0; i < 10 && g_running.load(); ++i) {
+                    if (ValidateRosterInit()) {
+                        BootLog("F11: roster ENABLED on retry");
+                        LogF("F11: roster ENABLED on retry");
+                        WriteSessionManifest(0, 0, 0, 0, g_roster, g_sessionStartMs);
+                        break;
+                    }
+                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
+                }
+                if (!g_roster.enabled) {
+                    BootLog("F11: roster recheck FAILED, staying in fallback");
+                    LogF("F11: roster recheck FAILED");
+                }
+            }
             g_armed.store(!was);
```

NOTE on the `WriteSessionManifest(0, 0, 0, 0, g_roster, g_sessionStartMs)`
call: this re-emits the manifest with `roster_enabled=1` so post-session
tooling knows the capture has a mix of fallback samples (before arm) and
roster-enabled samples (after arm). The 0s for version components are
ignored by the manifest writer (it pulls them from the live `g_refs`
struct). If the writer requires real values, capture them once at init
time and reuse — defer to implementation time.

### C3 — De-spam the per-iteration WCM-not-ready boot log

`probe.cpp:871`. Today the loop emits "roster_fail: WCM not yet readable
(game not loaded?)" once per iteration → 30 identical lines per startup
that drowned out everything else in the boot log. Emit only every 5th
iteration after the first one.

```diff
 static bool ValidateRosterInit() {
     g_roster = RosterValidation{};
     if (!g_refs.ready || !g_refs.wcmPtrAddr) return false;

+    static int s_wcmRetryCount = 0;
     uintptr_t wcm = 0;
     if (!SafeRead<uintptr_t>(g_refs.wcmPtrAddr, &wcm) || !LooksLikeUserPtr(wcm)) {
-        BootLog("roster_fail: WCM not yet readable (game not loaded?)");
+        if (s_wcmRetryCount == 0 || (s_wcmRetryCount % 5) == 0) {
+            BootLog("roster_fail: WCM not yet readable (attempt #%d)", s_wcmRetryCount);
+        }
+        s_wcmRetryCount++;
         return false;
     }
+    s_wcmRetryCount = 0;
```

The static counter resets to 0 when WCM finally becomes readable. Effect:
boot log now shows attempts 0, 5, 10, 15, 20, 25, ... instead of 30
identical lines.

## Apply procedure (run only if needed)

```bash
cd /home/joshua.blattner/claude/elden-ring

# 1. Apply the patch
git apply probe/v6.1/probe-v6.1.patch

# 2. Verify the three change-points compiled correctly via grep
grep -n "i < 120" probe/probe.cpp           # should match 1 line near 2745
grep -n "roster recheck attempt" probe/probe.cpp  # should match 1 line in F11Thread
grep -n "s_wcmRetryCount" probe/probe.cpp   # should match 3 lines in ValidateRosterInit

# 3. SCP + build via station
scp -i ~/.ssh/station_key probe/probe.cpp claude@station:C:/Projects/elden-ring/probe/probe.cpp
ssh -i ~/.ssh/station_key claude@station '"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\Projects\elden-ring\probe\probe.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal'

# 4. Wait for Josh to close the game, then drop the new DLL
cp /mnt/station-projects/elden-ring/probe/bin/Release/parry-tell-probe.dll /mnt/station-mods/parry-tell-probe.dll
```

## When NOT to apply

- If smoke + qualification both PASS with the boss-bar fallback (Banished
  Knight at Stormveil entrance has a boss bar — high confidence). Don't
  risk a regression on a working path.
- If station SSH isn't up (Josh starts it manually).
- If we're mid-discovery session — don't swap a live DLL.
