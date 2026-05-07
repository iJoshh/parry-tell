# parry-tell — Project CLAUDE.md

## Read first

This is a client-side Elden Ring + Shadow of the Erdtree mod project. Josh is
**commissioning** Claude (not co-building). Claude drives all coding via Codex
MCP; Josh runs builds + tests on Windows. License: MIT.

Active plan: `PHASE1-PLAN.md`. Handoff: `HANDOFF.md`. Long-form research:
`archaeology/` and `research/`.

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
- **Never** attempt to invoke MSBuild or any Windows binary remotely.
  Builds happen on Josh's Windows box, by Josh. SMB is for file transfer
  only — no execution channel.
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

## Probe development workflow (post-2026-05-06)

Old workflow: Resend email tarballs → Josh extracts → Josh builds → Josh
copies DLL → Josh emails or drops logs back.

New workflow:

1. Claude edits probe source locally at `~/claude/elden-ring/probe/probe.cpp`
2. Claude reviews via Codex (writer-pairing rule from user-global memory)
3. Claude builds tarball, ships via Resend OR drops directly into a
   designated drop folder Josh creates under `C:\Projects\elden-ring\`
   (TBD — for now, email)
4. Josh extracts + rebuilds in Visual Studio (this remains a manual step
   until/unless we set up an SSH-driven build)
5. **Claude copies the new DLL from `/mnt/station-projects/elden-ring/probe/bin/Release/`
   to `/mnt/station-mods/parry-tell-probe.dll`** — this is new, replaces
   Josh manually copying
6. **Claude deletes `/mnt/station-mods/parry-tell-probe.csv`** to start
   fresh
7. Josh runs the game test
8. Claude reads `/mnt/station-mods/parry-tell-probe.csv` for the live CSV,
   reads `STATION-vN.log` from `/mnt/station-projects/elden-ring/` for
   the DebugView capture
9. Score, iterate

The pivotal change: steps 5-6 used to require Josh's keyboard time, now
they don't. Build is still Josh's job because we don't have a Windows
build channel set up.

## What's NOT changed

- Co-op safety model: read-only memory, no `regulation.bin` writes
- License hygiene: MIT/Apache only for production, others reference-only
- Crash safety: SEH-wrapped derefs, loader-lock-safe DllMain
- Phase numbering convention from user-global memory
- Time estimates: halve first instinct
