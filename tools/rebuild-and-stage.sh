#!/bin/bash
# rebuild-and-stage.sh — full rebuild cycle for parry-tell-probe.
#
# Run from /home/joshua.blattner/claude/elden-ring/. Uses the SSH service
# on station (claude@station) to compile and the SMB share to stage.
#
# Steps:
#   1. SCP probe.cpp to station
#   2. Build via MSBuild on station
#   3. Copy resulting DLL to staging area
#   4. (Does NOT auto-drop into mods folder — Elden Ring may be running)
#
# After this succeeds, either run swap-mode.bat <mode> on station to
# refresh the INI, or copy the DLL into mods folder via SMB:
#   cp /mnt/station-projects/elden-ring/probe/stage/parry-tell-probe.dll \
#      /mnt/station-mods/parry-tell-probe.dll

set -euo pipefail

REPO=/home/joshua.blattner/claude/elden-ring
KEY=$HOME/.ssh/station_key

echo "1. Verify SSH service is up..."
if ! timeout 5 ssh -i "$KEY" -o ConnectTimeout=3 -o BatchMode=yes \
        claude@station "echo SSH_OK" >/dev/null 2>&1; then
    echo "[ERROR] SSH to station failed. Is the SSH service started?" >&2
    exit 1
fi

echo "2. SCP probe.cpp + probe.vcxproj + MinHook vendor tree to station..."
# Sync ALL build inputs, not just probe.cpp. A .vcxproj or vendor change
# that doesn't sync produces a binary built from mixed local/station inputs.
# (Codex flagged this as a medium bug — addressed.)
scp -q -i "$KEY" "$REPO/probe/probe.cpp" \
    claude@station:C:/Projects/elden-ring/probe/probe.cpp
scp -q -i "$KEY" "$REPO/probe/probe.vcxproj" \
    claude@station:C:/Projects/elden-ring/probe/probe.vcxproj
# MinHook vendor: rsync would be ideal but we only have scp; recursive scp
# of the whole vendor dir is fine (small — ~10 files).
scp -q -i "$KEY" -r "$REPO/probe/vendor" \
    claude@station:C:/Projects/elden-ring/probe/

echo "3. MSBuild on station..."
if ! ssh -i "$KEY" claude@station '"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\Projects\elden-ring\probe\probe.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal' 2>&1 | tee /tmp/probe-build.log | tail -10; then
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
ssh -i "$KEY" claude@station 'copy /Y C:\Projects\elden-ring\probe\bin\Release\parry-tell-probe.dll C:\Projects\elden-ring\probe\stage\parry-tell-probe.dll' >/dev/null

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
