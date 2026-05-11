#!/bin/bash
# rebuild-and-stage.sh — full rebuild cycle for parry-tell-probe.
#
# Run from /home/joshua.blattner/claude/elden-ring/. Uses the SSH service
# on station (claude@station) to compile and the SMB share to stage.
#
# Steps:
#   1. SCP probe.cpp + probe.vcxproj + MinHook vendor tree to station
#   2. Build via MSBuild on station
#   3. Copy resulting DLL to staging area
#   4. (Does NOT auto-drop into mods folder — Elden Ring may be running)
#
# Authentication: uses tools/station-ssh.sh which authenticates with the
# claude@station password from /etc/ssh-credentials-station (root:600,
# accessed via sudo NOPASSWD). The key-based auth path was retired
# 2026-05-11 — see tools/station-ssh.sh header for rationale.
#
# After this succeeds, either run swap-mode.bat <mode> on station to
# refresh the INI, or copy the DLL into mods folder via SMB:
#   cp /mnt/station-projects/elden-ring/probe/stage/parry-tell-probe.dll \
#      /mnt/station-mods/parry-tell-probe.dll

set -euo pipefail

REPO=/home/joshua.blattner/claude/elden-ring

# shellcheck source=station-ssh.sh
. "$REPO/tools/station-ssh.sh"

echo "1. Verify SSH service is up..."
if ! station_ssh 'echo SSH_OK' >/dev/null 2>&1; then
    echo "[ERROR] SSH to station failed. Is the SSH service started, and"  >&2
    echo "         is /etc/ssh-credentials-station present + sudo NOPASSWD"  >&2
    echo "         configured for the current user?"                          >&2
    exit 1
fi

echo "2. SCP probe.cpp + probe.vcxproj + MinHook vendor tree to station..."
# Sync ALL build inputs, not just probe.cpp. A .vcxproj or vendor change
# that doesn't sync produces a binary built from mixed local/station inputs.
station_scp "$REPO/probe/probe.cpp" \
    'claude@station:C:/Projects/elden-ring/probe/probe.cpp'
station_scp "$REPO/probe/probe.vcxproj" \
    'claude@station:C:/Projects/elden-ring/probe/probe.vcxproj'
# MinHook vendor tree: scp -r OVERLAYS files but does not delete files
# that were removed locally. To prevent a stale vendor file from compiling
# into the next build, mirror the tree: blow away the remote vendor dir,
# then recursive-copy fresh. The rmdir is best-effort (it's fine if the
# dir doesn't yet exist on first build).
station_ssh 'if exist "C:\Projects\elden-ring\probe\vendor" rmdir /S /Q "C:\Projects\elden-ring\probe\vendor"' >/dev/null
station_scp_recursive "$REPO/probe/vendor" \
    'claude@station:C:/Projects/elden-ring/probe/'

echo "3. MSBuild on station..."
if ! station_ssh '"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\Projects\elden-ring\probe\probe.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal' 2>&1 | tee /tmp/probe-build.log | tail -10; then
    echo "[ERROR] MSBuild failed. See /tmp/probe-build.log for full output." >&2
    exit 2
fi

# Verify DLL produced.
DLL=/mnt/station-projects/elden-ring/probe/bin/Release/parry-tell-probe.dll
if [ ! -f "$DLL" ]; then
    echo "[ERROR] DLL not at $DLL — build claimed success but produced no output." >&2
    exit 3
fi

echo "4. Stage DLL..."
station_ssh 'copy /Y C:\Projects\elden-ring\probe\bin\Release\parry-tell-probe.dll C:\Projects\elden-ring\probe\stage\parry-tell-probe.dll' >/dev/null

# Verify staged DLL is byte-identical to the build artifact (Codex flag).
STAGED=/mnt/station-projects/elden-ring/probe/stage/parry-tell-probe.dll
if [ ! -f "$STAGED" ]; then
    echo "[ERROR] staged DLL missing at $STAGED" >&2
    exit 4
fi
if ! cmp -s "$DLL" "$STAGED"; then
    echo "[ERROR] staged DLL differs from build artifact:" >&2
    echo "  build:  $(stat -c%s "$DLL") bytes mtime=$(stat -c%y "$DLL")" >&2
    echo "  staged: $(stat -c%s "$STAGED") bytes mtime=$(stat -c%y "$STAGED")" >&2
    exit 5
fi

echo ""
echo "OK. Build artifact:"
ls -la "$DLL"
echo ""
echo "Staged at: $STAGED"
ls -la "$STAGED"
echo ""
echo "Next steps:"
echo "  - If Elden Ring is closed, drop into mods now:"
echo "      cp /mnt/station-projects/elden-ring/probe/stage/parry-tell-probe.dll \\"
echo "         /mnt/station-mods/parry-tell-probe.dll"
echo "  - If Elden Ring is running, close it first, then drop."
