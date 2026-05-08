# parry-tell-probe v6 — Test Runbook

Everything you need to actually run the smoke / qualification / discovery
sessions and what happens after each.

## State as of pre-test (handoff for Josh)

- Source: `probe/probe.cpp` (3076 lines, v6 per locked spec)
- DLL built and staged at `C:\Projects\elden-ring\probe\stage\parry-tell-probe.dll`
  (229 KB). Backup tarball at `probe/releases/probe-v6.tar.gz`.
- INI templates staged alongside DLL: `parry-tell-probe.ini.{smoke,qualification,discovery}`
- Logs directory ready: `C:\Projects\elden-ring\logs\` (the INIs already
  point at this; visible from my VM at `/mnt/station-projects/elden-ring/logs/`)
- Analysis tools: `tools/probe_status.py`, `tools/qualify_oracle.py`,
  `tools/analyze_discovery.py`, plus the `tools/probe_bin.py` library.
  Wire-format symmetry verified by `tools/test_probe_bin.py` (passes).
  End-to-end qualification logic verified by `tools/test_qualify_oracle.py`
  on synthetic data that mimics a real fight (passes).

## Already dropped (as of 2026-05-08)

The DLL and the smoke INI are ALREADY in `Game\mods\`:

```
Game\mods\parry-tell-probe.dll          <- v6, 229 KB, fresh
Game\mods\parry-tell-probe.ini          <- smoke config (mode = smoke)
Game\mods\parry-tell-probe.dll.disabled <- v5f kept as audit-trail backup
Game\mods\parry-tell-probe.csv.v5f-leftover <- old v5f data, archived
```

Launch the game and you're in smoke mode immediately.

## Switching modes (smoke -> qualification -> discovery)

Run on station (NOT the VM, NOT in Tailscale shell):

```
swap-mode.bat smoke
swap-mode.bat qualification
swap-mode.bat discovery
```

The script lives at `C:\Projects\elden-ring\probe\stage\swap-mode.bat`.
**Elden Ring must be CLOSED** when you run it (file lock).

The script copies the appropriate INI from the staging area over
`Game\mods\parry-tell-probe.ini`. The DLL is unchanged across modes.

To see the script's output: open `cmd.exe`, `cd C:\Projects\elden-ring\probe\stage`,
then run `swap-mode.bat smoke`. (Double-clicking from Explorer also
works but the window vanishes on success.)

## Smoke test (60 sec)

**Goal:** confirm the hook fires, log files appear, the DLL doesn't crash,
and the calibration report identifies a monotonic anim-time candidate.

**Setup:** smoke INI staged; F11 = arm/disarm.

**Gameplay (60s at a Grace, after pressing F11):**

1. Walk in circles 10s (locomotion loops)
2. One light attack
3. One heavy attack
4. One gesture (long anim)
5. One item use
6. One roll
7. One sprint (variable)
8. Walk in circles 10s

Press F11 to disarm before quitting (clean session boundary).

**Expected output (in `C:\Projects\elden-ring\logs\`):**

- `smoke-<ts>.bin` — should have manifest + a few hundred Tier 1+2 sample records
- `smoke-<ts>.csv` — pandas-friendly per-sample summary
- `smoke-<ts>.log.txt` — diagnostics
- `smoke-<ts>.calibration.txt` — animation-time candidate analysis

**Pass condition:** `.calibration.txt` shows at least one of `+0x20`, `+0x24`,
`+0x28`, `+0x2C` with `gate=PASS` (monotonic + rewinds + max_segment_dur ≥ 0.3s
+ in_range). Spec rev3 expects `+0x24` to win based on practice-tool research.

**Sanity check from VM:**

```
python tools/probe_status.py /mnt/station-projects/elden-ring/logs/smoke-<ts>
```

This will show sample count, drops, manifest. If sample_count is 0 or all
zero handles, F11 wasn't pressed or sig-scan failed.

**Fail modes:**
- No files at all → DLL didn't load. Check Elden Mod Loader output, or
  `C:\Projects\elden-ring\probe\stage\parry-tell-probe.boot.log` (the DLL
  writes here if config can't be loaded).
- Files exist but empty → init_fail. Check `.log.txt`.
- Files have data but calibration shows all `gate=FAIL` → anim-time path
  is wrong; need offset re-research before continuing to qualification.

## Qualification (2-3 min)

**Goal:** prove the join key (which `field_at_0xNN` maps to a `cXXXX`),
confirm the anim-time field works on enemy data, verify predicted parry
windows match observations within ±11 ms.

**Setup:** swap `parry-tell-probe.ini.qualification` to `parry-tell-probe.ini`.

**Gameplay:**

1. Travel to Stormveil entrance (or any reliable parry-eligible enemy
   you can reset easily).
2. Lock on to ONE enemy. Recommended: a Banished Knight (common,
   parryable, lots of attack reps).
3. Press F11 to arm.
4. Fight for 2-3 minutes — let the enemy get most of its attack rotations
   off. Don't kill it too fast; we want repetition.
5. Press F11 to disarm before quitting.

**Expected output:**

- `qualification-<ts>.bin` — Tier 1+2+3 records on the locked-on enemy
- `qualification-<ts>.csv` — summary
- `qualification-<ts>.log.txt` — diagnostics (look for `roster_check7_warn`
  if the boss-bar enemy never enrolled in the roster span — reduces but
  doesn't fail confidence)

**Run analysis:**

```
python tools/qualify_oracle.py /mnt/station-projects/elden-ring/logs/qualification-<ts>
```

**Pass output looks like:**

```
QUALIFICATION REPORT — qualification-<ts>

Samples parsed: <some thousands>

Join key: field_at_0x064 (constant value 4380 over <N> focused-enemy rows)
Identified character: c4380

Anim time field: TimeAct + 0x24 (monotonic_segments=87 max_segment_dur=4.8s
                                 rewinds=True in_range=True passed=True)

Anim ID encoding: full (matched 19/47 db anim_ids in capture)

DB parry windows for c4380: <total>
Windows whose anim_id appeared in capture: <observed>
Within ±11.0ms: <matched>
Misses: <off-time>
Anim_ids in DB never seen in capture: <unobserved>

Verdict: PASSED
```

**Fail conditions and what to do:**

1. `no field_at_0xNN was constant across focused-enemy rows AND matched
   a cXXXX in the database` → either we're capturing the wrong enemy
   (stop showing me the Tarnished's own ChrIns) or the cXXXX→field
   mapping is at an offset I'm not capturing. Add candidate fields,
   rebuild, retry.
2. `no enemy_anim_time candidate passed the spec gate` → smoke should
   have caught this; if it didn't, the player vs. enemy struct layouts
   diverge for anim_time.
3. `predicted windows didn't match observations` → join key wrong, or
   anim-time field wrong, or database wrong for that cid. Check the
   diagnostic and decide.

**Do NOT run the discovery session until qualification PASSES.**

## Discovery (~1 hr)

**Goal:** capture enough memory state during varied gameplay to identify
the runtime parry-active flag (and ideally the hyperarmor flag) by post-
session correlation against database parry windows.

**Setup:** swap `parry-tell-probe.ini.discovery` to `parry-tell-probe.ini`.

**Suggested gameplay (~1 hour):**

1. Stormveil mob route (varied enemies — soldiers, Banished Knights,
   dogs, ravens, the Grafted Scion area).
2. Roundtable visit (NPCs, no combat — baseline non-combat memory).
3. Crucible Knight if available (high-confidence parryable boss).
4. Aim for diverse `cXXXX` coverage and lots of parryable attack reps.

Press F11 to arm at session start, F11 to disarm before quitting.

**Expected output:** ~5-10 GB `.bin` (rotates at 2 GB into `.bin.001`,
`.bin.002`, ...), plus the usual `.csv` / `.log.txt`.

**Run analysis:**

```
python tools/analyze_discovery.py /mnt/station-projects/elden-ring/logs/discovery-stormveil-1-<ts>
```

This reads the qualification report (sibling `.qualification.json` if
present) to know which cid is the join key. Outputs ranked byte-change
candidates: bytes that change much more often inside a database-predicted
parry window than outside.

**The actual discovery is iterative.** v1 of the analyzer is a
scaffolding; the real flag-hunting happens by reading the top-50 ranked
bytes and seeing which one(s) consistently flip on→off→on at window
boundaries across many windows. We'll iterate the analyzer once we have
real data.

## Tooling reference

Run from `~/claude/elden-ring/` on the VM.

```
# Quick "is this capture alive?" — top-line PASS/FAIL verdict at the head
python tools/probe_status.py <path-without-.bin>

# When something fails — pulls boot.log + latest .log.txt into one view
python tools/probe_diag.py [--tail 50]

# Self-tests (run anytime to confirm pipeline still works)
python tools/test_probe_bin.py            # wire format round-trip
python tools/test_qualify_oracle.py       # synthetic qualification end-to-end

# Full analysis
python tools/qualify_oracle.py <path-without-.bin>  [--tolerance-ms 11]
python tools/analyze_discovery.py <path-without-.bin>  [--cid c4380]
```

Capture base paths (replace `<ts>` with the real timestamp):
```
/mnt/station-projects/elden-ring/logs/smoke-<ts>
/mnt/station-projects/elden-ring/logs/qualification-<ts>
/mnt/station-projects/elden-ring/logs/discovery-stormveil-1-<ts>
```

Plain-text gameplay scripts (you can pull these up on phone):
```
C:\Projects\elden-ring\probe\stage\GAMEPLAY-smoke.txt
C:\Projects\elden-ring\probe\stage\GAMEPLAY-qualification.txt
C:\Projects\elden-ring\probe\stage\GAMEPLAY-discovery.txt
```

## What changes between sessions

Between smoke / qualification / discovery, only the `.ini` file changes
(rename the matching `.ini.<mode>` to `parry-tell-probe.ini`). The DLL
is identical. No rebuild required.

If you want me to swap INIs between sessions: just say "swap to
qualification" or "swap to discovery" and I'll do it (assuming the game
isn't holding the file open).

## File-lock gotcha

The DLL and INI files are locked while the game is running. To swap
either, the game has to be closed first. The probe writes to its session
log files continuously; those don't need to be unlocked between sessions
because each session opens a new timestamped pair.
