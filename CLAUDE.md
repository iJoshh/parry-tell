# parry-tell — Project CLAUDE.md

## Read first

This is a client-side Elden Ring + Shadow of the Erdtree mod project. Josh is
**commissioning** Claude (not co-building). Claude drives all coding via Codex
MCP; Josh runs builds + tests on Windows. License: MIT.

Active plan: `PHASE1-PLAN.md`. Handoff: `HANDOFF.md`. Long-form research:
`archaeology/` and `research/`.

## SMB perf rule: always copy locally before parsing (added 2026-05-11)

The Tailscale SMB mount is slow (~700 KB/s observed during the v6.1
qualification session). Any Python tool that streams through a multi-MB
.bin file directly off `/mnt/station-projects/...` will appear to hang —
the 89 MB qualification-20260511-125252.bin took 2+ minutes to even
read once over SMB, while reading the same file from /tmp completed in
1.6 seconds.

**Standing rule for Claude:** before running any analyzer (`inspect_capture.py`,
`qualify_oracle.py`, `calibrate_smoke.py`, `analyze_discovery.py`, anything
that opens a .bin or large .csv from station), copy the file(s) to local
disk first, then point the analyzer at the local copy.

```bash
# Pattern: copy with progress check, then analyze
cp /mnt/station-projects/elden-ring/logs/<capture>.bin /tmp/work.bin
cp /mnt/station-projects/elden-ring/logs/<capture>.csv /tmp/work.csv  # if needed
python3 tools/<analyzer>.py /tmp/work   # analyzers expect base path w/o .bin
```

Only the small ancillaries (`.log.txt`, manifest tail-reads, single-record
peeks via dd/struct) are fine to read directly off SMB. The big files
(.bin, full .csv) ALWAYS get copied first. No exceptions — even when it
looks like it might be fast enough to skip, the SMB stall is correlated
with file size, not with anything I can predict in advance, and the
session disruption from a stalled bash tool call is much worse than the
~2 min copy overhead.

## Windows file access (added 2026-05-06)

Tailscale mesh is set up. The Windows dev box (`station` in the tailnet)
exposes two SMB shares to this VM (`codeserver-vm` in the tailnet). IPs and
credentials live outside the repo (see local notes / shell environment).
Resolve hostnames via `tailscale status` at runtime — never hardcode IPs
in the repo.

- **`Projects` (read-only)** — mounted at `/mnt/station-projects/`. Maps to
  `C:\Projects\` on Windows. Used for reading probe source, builds, logs,
  CSVs that Josh saves into the project folder.
- **`mods` (read-write)** — mounted at `/mnt/station-mods/`. Maps to the
  Elden Ring `Game\mods\` folder. Used for swapping in fresh probe DLLs,
  cleaning up stale CSVs between test runs, reading the live CSV.

Credentials live in a root-only file outside the repo (mode 600). Account is
a Windows local user (standard-user, no admin) mapped to read-only or
read-write per share. Tailscale auth is identity-bound — losing the tailnet
credential means losing network access entirely.

### What Claude is allowed to do without asking

- Read anything under `/mnt/station-projects/` and `/mnt/station-mods/`
- Write/replace files under `/mnt/station-mods/parry-tell-probe.*`
  (the probe DLL, the probe CSV, future probe artifacts named `parry-tell-*`)
- Delete `/mnt/station-mods/parry-tell-probe.csv` between test runs
  to start a fresh capture
- Drop new versions of `parry-tell-probe.dll` into `/mnt/station-mods/`

### What Claude must NOT do

- **Never** modify any non-`parry-tell-*` file in `/mnt/station-mods/`. Other
  mods Josh has installed (UnlockTheFps, IncreaseAnimationDistance,
  RemoveVignette, SkipTheIntro, UltrawideFix) are off-limits — read them if
  needed for compatibility checks, do not edit them.
- **Never** attempt to write to `/mnt/station-projects/` (it's mounted RO,
  attempts will fail anyway, but don't try).
- **Never** attempt to mount additional Windows shares without explicit
  Josh approval. The two existing mounts are the entire allowed scope.
- **Never** ask Josh to share `Game\` (the parent of `mods\`) wholesale.
  Co-op safety depends on `regulation.bin` and other base game files being
  unreachable from the VM. If wider access seems needed, propose a narrowly
  scoped second share, never the parent dir.
- **Build channel via SSH is now active** (added 2026-05-06 evening). Claude
  can SCP source to Windows and trigger MSBuild remotely via SSH as the
  `claude` user (key auth only, Tailscale-scoped firewall, MANUAL service
  start — not auto-on-boot). Josh starts the SSH service when working
  together, stops it when done. See HANDOFF.md for the kill-switch chain.
  Build path: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`
- **Never** touch `regulation.bin`, `.dcx` archives, or any base game file.
  These are read-only on the Windows side and the VM can't reach them
  anyway, but the rule stays explicit: if you ever DO see them, do not
  read or write them.

### Standard handshake at session start

Future Claude session that resumes this project:

1. `mount | grep station` — confirm both mounts are live. If not, the
   `x-systemd.automount` will mount on first access; just `ls` the path.
2. `ls /mnt/station-mods/` — confirm `parry-tell-probe.dll` and
   `parry-tell-probe.csv` are visible.
3. `ls /mnt/station-projects/elden-ring/` — confirm project tree visible.
4. Resume work per `HANDOFF.md`.

If Tailscale is down (`tailscale status` shows offline), tell Josh
explicitly. Don't try to work around it — fall back to the email/drop
workflow that worked before.

### Audit expectations

Every write to `/mnt/station-mods/` shows up in Josh's Windows event log
under `claude` as the SMB user. If Josh ever wants to verify what Claude
did, he can pull the audit trail from there.

## Probe development workflow (current as of 2026-05-06 v4)

Full pipeline runs on Claude's side:

1. Claude edits probe source locally at `~/claude/elden-ring/probe/probe.cpp`
2. Claude reviews via Codex (writer-pairing rule from user-global memory)
3. Claude SCPs source to `claude@station:C:\Projects\elden-ring\probe\probe.cpp`
4. Claude triggers MSBuild via SSH:
   `ssh claude@station '"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\Projects\elden-ring\probe\probe.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal'`
5. Claude reads build output via SMB at `/mnt/station-projects/elden-ring/probe/bin/Release/parry-tell-probe.dll`
6. Claude commits tarball to `probe/releases/probe-vN.tar.gz` for audit trail
7. **Wait for Josh's "ready to reload" signal** (file locks prevent install while game is running)
8. Claude copies DLL to `/mnt/station-mods/parry-tell-probe.dll`
9. Claude deletes stale `/mnt/station-mods/parry-tell-probe.csv` to start fresh
10. Josh launches game, runs test, saves DebugView log to `C:\Projects\elden-ring\STATION-vN.log`
11. Claude reads everything from `/mnt/station-projects/` and `/mnt/station-mods/` directly
12. Score, iterate

Josh's only manual steps: launching the game, playing it, saving the DebugView
log. Everything else is on Claude's side. SSH service is manually started by
Josh at session beginning, manually stopped at end (see kill-switch email).

## What's NOT changed

- Co-op safety model: read-only memory, no `regulation.bin` writes
- License hygiene: MIT/Apache only for production, others reference-only
- Crash safety: SEH-wrapped derefs, loader-lock-safe DllMain
- Phase numbering convention from user-global memory
- Time estimates: halve first instinct
