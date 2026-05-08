# HANDOFF — parry-tell (elden-ring)

**Session closed:** 2026-05-08 (America/Chicago)
**HEAD:** 6db35ca — feat: walk-through self-service tooling for the test session
**Branch:** main — clean, in sync with origin/main

---

## Where we left off

Probe v6 is built, reviewed, and staged. The DLL is live in `Game\mods\` with
the smoke INI loaded. Josh has not yet run any v6 test session. The analysis
pipeline is written and self-tested. Everything is ready for Josh's playtest.

---

## Accomplishments this session

1. **v6 source written and built** (~3,076 lines C++). New vs v5f: INI config
   parser (fail-closed), 64 MB SPSC ring buffer (256 × 256 KB), worker thread
   writing binary records + CSV + diagnostics, CSFeManImp sig-scan + boss-bar
   enumeration, WCM enemy roster behind 7-check init quarantine, TimeAct chain
   walk, ai_struct walk, three-tier sampling (~90 / 10 / 2 Hz), decimation
   phase staggering, producer-side emergency drop, worker-side adaptive
   stepdown via 5 s rolling window, session manifest with full `config_dump`,
   smoke calibration report with anim-time gate. Built clean first try.

2. **Codex review of v6 source** — `block` verdict; all 6 fixes applied; 1
   blocker fixed (roster pass split into priority + fill); 1 blocker declined
   with documented reasoning (detour compute rule); delta encoding deferred to
   v6.1.

3. **Post-capture analysis pipeline** — 7 files in `tools/`:
   - `probe_bin.py` — wire-format reader library
   - `probe_status.py` — quick health report with top-line VERDICT
   - `qualify_oracle.py` — full qualification analyzer
   - `analyze_discovery.py` — discovery byte-ranking scaffolding
   - `probe_diag.py` — log aggregator for "DLL didn't load" triage
   - `rebuild-and-stage.sh` — one-command rebuild cycle
   - `test_probe_bin.py` (PASS), `test_qualify_oracle.py` (PASS, 8 real DB
     parry windows)

4. **Self-service tooling** — `probe/v6/swap-mode.bat` for INI swaps between
   modes; `probe/v6/GAMEPLAY-{smoke,qualification,discovery}.txt` for phone
   reference; `probe_status.py` top-line VERDICT; `probe_diag.py` log
   aggregator.

5. **Staged on station** — DLL + smoke INI dropped into `Game\mods\`; v5f
   preserved as `parry-tell-probe.dll.disabled`; old v5f CSV renamed to
   `.csv.v5f-leftover`; `C:\Projects\elden-ring\logs\` created and
   SMB-visible; `C:\Projects\elden-ring\probe\stage\` populated.

6. **Wrap-up email sent** via Resend with all playtest steps.

---

## Next steps (priority order)

**1. Josh runs smoke test** ← this is item 1
   - Launch Elden Ring with `Game\mods\parry-tell-probe.dll` + smoke INI
     already in place.
   - Follow `probe/v6/GAMEPLAY-smoke.txt` (8-step deliberate-action script,
     ~60 s at any Grace).
   - Tell Claude "smoke done."

**2. Claude parses smoke results**
   - Runs `python tools/probe_status.py` against the latest capture.
   - Reads `.calibration.txt` from `C:\Projects\elden-ring\logs\`.
   - Reports PASS / FAIL with detail.

**3. If smoke PASS → qualification**
   - Josh runs `probe/v6/swap-mode.bat qualification` on station.
   - Follows `GAMEPLAY-qualification.txt` (~2–3 min vs Banished Knight).
   - Tells Claude "qualification done."
   - Claude runs `python tools/qualify_oracle.py`.

**4. If qualification PASS → discovery**
   - Josh runs `swap-mode.bat discovery`.
   - Follows `GAMEPLAY-discovery.txt` (~1 hr Stormveil + boss).
   - Tells Claude "discovery done."
   - Claude runs `python tools/analyze_discovery.py` on the ~5–10 GB capture.

**5. Discovery result determines production mod path**
   - Parry-active flag found → production mod uses Path B (live read).
   - Not found → Path A (database lookup) per PHASE3-PLAN.md.

---

## Open questions for Josh

None. Everything is ready for his playtime. No decisions needed before the
smoke test.

---

## Tried and ruled out this session

| Approach | Why ruled out |
|---|---|
| `D:\parry-tell-logs\` | D: drive does not exist on station |
| `E:\parry-tell-logs\` | E: not writable by `claude` SMB user |
| `GetChrInsFromHandle(wcm, &stack_handle_copy)` for boss-bar handles outside roster | v5e debugging proved function returns input unchanged when given a stack pointer; documented as known limitation; roster-disabled fallback genuinely cannot resolve boss-bar handles |
| Worker-side delta encoding in v6 | Deferred to v6.1; not blocking; produces 3–5× larger `.bin` without it |

---

## Files modified or created this session

| File | Status |
|---|---|
| `probe/probe.cpp` | Rewritten v5f → v6 (~3,076 lines) |
| `probe/v6/PROBE-V6-SPEC.md` | No changes (spec was locked at session start) |
| `probe/v6/RUNBOOK.md` | New |
| `probe/v6/parry-tell-probe.ini.smoke` | New |
| `probe/v6/parry-tell-probe.ini.qualification` | New |
| `probe/v6/parry-tell-probe.ini.discovery` | New |
| `probe/v6/GAMEPLAY-smoke.txt` | New |
| `probe/v6/GAMEPLAY-qualification.txt` | New |
| `probe/v6/GAMEPLAY-discovery.txt` | New |
| `probe/v6/swap-mode.bat` | New |
| `probe/releases/probe-v6.tar.gz` | New build artifact |
| `tools/probe_bin.py` | New |
| `tools/probe_status.py` | New |
| `tools/qualify_oracle.py` | New |
| `tools/analyze_discovery.py` | New |
| `tools/probe_diag.py` | New |
| `tools/rebuild-and-stage.sh` | New |
| `tools/test_probe_bin.py` | New |
| `tools/test_qualify_oracle.py` | New |
| `CHANGELOG.md` | New (first entry) |
| `HANDOFF.md` | Rewritten (this file) |
| `PHASE3-PLAN.md` | Session log appended |
| `C:\Projects\elden-ring\logs\` | Created on station, empty, SMB-visible |
| `C:\Projects\elden-ring\probe\stage\` | Populated with DLL + INIs + scripts |
| `/mnt/station-mods/parry-tell-probe.dll` | v6 dropped (fresh) |
| `/mnt/station-mods/parry-tell-probe.ini` | Smoke config |
| `/mnt/station-mods/parry-tell-probe.dll.disabled` | v5f preserved as audit trail |
| `/mnt/station-mods/parry-tell-probe.csv.v5f-leftover` | Renamed from `.csv` |

---

## Services / processes

No services restarted this session.

- `Game\mods\parry-tell-probe.dll` — v6 DLL is in place; smoke INI loaded.
  Elden Ring was closed during the drop; copy succeeded cleanly.
- SSH service on station: manually started by Josh at session start; verify
  state at next session resume.
- SMB mounts (`/mnt/station-mods/`, `/mnt/station-projects/`) — live via
  `x-systemd.automount`; will re-mount on first access next session.

---

## Git state at close

```
Branch:       main
HEAD:         6db35ca  feat: walk-through self-service tooling for the test session
Working tree: clean (before session-close commit)
Remote:       origin/main — in sync (just pushed)
```

Commits this session:
- `9af84e8` feat(probe): v6 source — discovery probe per locked spec
- `7c4827e` feat(tools): post-capture analysis pipeline + runbook
- `6db35ca` feat: walk-through self-service tooling for the test session

A session-close commit + tag will be added by `commit-and-tag.sh sc` after
this HANDOFF is written.

---

## Pickup prompt for next session

> "Probe v6 is built and staged. Josh has just finished the smoke test (or
> is about to). Load HANDOFF.md, confirm SMB mounts are live
> (`mount | grep station`), then run `python tools/probe_status.py` against
> the latest capture in `C:\Projects\elden-ring\logs\` and report the
> top-line VERDICT. If PASS, proceed to qualification per the next-steps
> sequence in HANDOFF.md."
