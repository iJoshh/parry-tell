# Preflight gate 0.2c — Hello-world DLL load test

This is the smallest possible test that ME2 / Seamless Co-op can load an arbitrary DLL via `external_dlls`. If this works, the real mod's load path will work. If this fails, no other code matters until ME2/Seamless config is fixed.

## What's here

- `hello.cpp` — ~120 lines of C++. On load, logs "[parry-tell] hello from preflight DLL" via `OutputDebugStringA` (visible in DebugView) AND writes a log file named `parry-tell-hello.log` in the **same directory as `hello.dll`**. Spawns a heartbeat thread that logs every 5 seconds for 30 seconds.
- `hello.vcxproj` — Visual Studio 2022 project file. Builds `hello.dll` for x64 Release with the v143 (VS 2022) toolset, statically linked CRT.

## Build instructions

1. **Open in Visual Studio 2022.** `File → Open → Project/Solution`. Pick `hello.vcxproj`. (You can also right-click the file in Explorer and pick "Open With → Visual Studio.")

2. **Set the configuration to `Release | x64`.** This is the dropdown at the top of the VS window. The project only has a Release-x64 config defined, so it should default to that, but verify.

3. **Build.** Menu → `Build → Build Solution` (or Ctrl+Shift+B).

4. **Output:** `bin\Release\hello.dll` next to the project file.

## Test procedure

1. **Copy `hello.dll`** to your ER folder under `SeamlessCoop\dllMods\` (or wherever your Seamless install puts external DLLs — Seamless's docs/install README will tell you).

2. **Add to Seamless's external_dlls config.** Edit `seamlesscoopsettings.ini` (in your ER `SeamlessCoop/` folder). Find the `external_dlls = ...` line and add `dllMods/hello.dll` to the list (alongside any existing entries — typically `SeamlessCoop/ersc.dll`).

3. **Download DebugView** if you don't already have it: https://learn.microsoft.com/sysinternals/downloads/debugview. Run it. Menu: `Capture → Capture Win32` and `Capture → Capture Global Win32` should both be checked.

4. **Launch ER via `ersc_launcher.exe`**. (NOT through Steam directly.) Confirm no EAC splash screen — that's the canonical "Seamless is active" signal.

5. **Get to the main menu.** You don't need to load a save — just confirming DLL loads, not testing any game state.

6. **Look in DebugView** for these lines:
   ```
   [parry-tell] hello from preflight DLL
   [parry-tell] DLL loaded into host process — preflight gate 0.2c PASSED
   [parry-tell] heartbeat 1/6 (DLL alive in host process)
   ```
   The heartbeats will appear over the next 30 seconds.

7. **Backup check:** open `parry-tell-hello.log` next to where you placed `hello.dll` (it'll be in your Seamless `dllMods/` folder). Same lines should be there with timestamps.

## What the results mean

**If you see the messages:** ME2 / Seamless DLL load path works. Preflight gate 0.2c PASSED. You can quit ER and we move on.

**If you don't see the messages:**
- Check Seamless's launcher log (usually somewhere in your ER folder; varies by version) for errors loading `hello.dll`.
- Most common causes:
  - Wrong path in `external_dlls` (relative paths in Seamless's config are usually relative to the ER `Game/` folder)
  - Wrong architecture (must be x64 — verify `hello.dll` is x64 with `dumpbin /headers hello.dll | findstr machine` from a VS Developer Command Prompt)
  - Defender quarantined the DLL (check Defender's quarantine log; this is what preflight 0.2b prevents but worth verifying)
  - `seamlesscoopsettings.ini` syntax wrong (Seamless's parser is finicky about commas and whitespace)
- Send me the launcher log + a screenshot of your `external_dlls` config line and I'll debug.

## Cleanup after preflight passes

Leave `hello.dll` installed during the rest of preflight if you want — it's harmless. Remove it before installing the real `parry-tell.dll` to keep things clean.

## Why this looks like overkill for a "hello world"

It's not, actually. The OutputDebugString channel is the canonical Windows kernel-debugging channel — DebugView reads it without any privileges or tricks, so we get a noise-free signal that the DLL loaded. The log file is the backup channel in case DebugView isn't running. The heartbeat thread is what proves the DLL didn't just briefly load and unload — it's actively running inside ER's process, which is what we need for the real mod.

If we cheaped out on the test (say, just a `MessageBox`), we'd risk a false-pass: the DLL might load, show the box, and crash on next operation. The heartbeat catches that.
