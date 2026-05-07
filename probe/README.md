# parry-tell probe (Gate 0)

This DLL is a one-shot data probe for Gate 0 verification on Elden Ring 1.16.1.
It logs runtime state to `parry-tell-probe.csv` next to the DLL.

## Build (Visual Studio 2022)

1. Open `probe/probe.vcxproj` in Visual Studio 2022.
2. Select configuration `Release` and platform `x64`.
3. Build the project.
4. Output DLL is `parry-tell-probe.dll`.

Build profile:
- Toolset: `v143`
- Runtime: static CRT (`/MT`)
- Type: Dynamic Library (`.dll`)

## Test Procedure (from probe spec)

1. Build the probe DLL in VS.
2. Drop it into Seamless Co-op `external_dlls`.
3. Launch Elden Ring via `ersc_launcher.exe`.
4. Load a save with boss access. Run **solo** (not co-op) for clean data.
5. Fight Margit for 5-10 minutes and intentionally:
   - get hit by varied attacks,
   - switch lock-on mid-fight,
   - break and re-establish lock,
   - optionally summon Mimic Tear after the first 3 minutes.
6. Quit Elden Ring cleanly.
7. Collect `parry-tell-probe.csv` next to the DLL.

## What to send back to orchestrator

Send:
1. `parry-tell-probe.csv`
2. Elden Ring version used (expecting 1.16.1)
3. Short notes for the run (solo/co-op, Mimic summon yes/no, boss used)

## Notes

- This build follows the spec fallback path because `CSFeManImp` offset is unresolved in this workspace (no archaeology/10 result present).
- Fallback mode enumerates `WorldChrMan` prio-queue ChrIns entries and logs candidate target fields for offline diffing.
