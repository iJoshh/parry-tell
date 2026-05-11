#!/bin/bash
# Apply v6.1 patch, copy to station via SCP, build, drop DLL into mods folder.
# Run from project root: probe/v6.1/apply-and-build.sh
#
# Preconditions:
#   - station SSH service is running (Josh starts it manually)
#   - SMB mounts are LIVE (we verify mountpoints, not just dir presence)
#   - Game is CLOSED (DLL is locked while ER is running)
#
# Rollback model: any failure restores BOTH local probe.cpp AND station
# probe.cpp AND (if it was overwritten) the v6.0 DLL. The script tracks
# each mutation independently so partial failures restore correctly.

set -uo pipefail

REPO=/home/joshua.blattner/claude/elden-ring
SSH_KEY="$HOME/.ssh/station_key"
DLL_SRC=/mnt/station-projects/elden-ring/probe/bin/Release/parry-tell-probe.dll
DLL_DST=/mnt/station-mods/parry-tell-probe.dll
DLL_BACKUP=/mnt/station-mods/parry-tell-probe.dll.v6.0-backup
STATION_CPP_REMOTE='C:/Projects/elden-ring/probe/probe.cpp'
STATION_CPP_BACKUP='C:/Projects/elden-ring/probe/probe.cpp.v6.0-backup'

# --- Preflight ---
# Verify SSH key + mounts BEFORE any state mutation.
if [ ! -r "$SSH_KEY" ]; then
    echo "ERROR: SSH key not readable at $SSH_KEY"
    exit 1
fi
if ! cd "$REPO"; then
    echo "ERROR: cannot cd to $REPO"
    exit 1
fi
if ! mountpoint -q /mnt/station-mods; then
    echo "ERROR: /mnt/station-mods is not a mountpoint (SMB share may be down)"
    exit 1
fi
if ! mountpoint -q /mnt/station-projects; then
    echo "ERROR: /mnt/station-projects is not a mountpoint (SMB share may be down)"
    exit 1
fi

# --- Single-instance lock ---
# Prevents two concurrent invocations from racing on the station backup.
LOCKFILE=/tmp/parry-tell-v6.1-deploy.lock
exec 9>"$LOCKFILE"
if ! flock -n 9; then
    echo "ERROR: another deploy is in progress (lock at $LOCKFILE)"
    exit 1
fi

# --- Rollback state tracking ---
LOCAL_PATCH_APPLIED=0
STATION_CPP_BACKUP_CREATED=0
DLL_BACKUP_CREATED=0

cleanup_on_fail() {
    local exit_code=$?
    echo ""
    echo "=== FAILURE (exit $exit_code) — rolling back ==="

    if [ "$DLL_BACKUP_CREATED" = "1" ]; then
        echo "  restoring DLL from backup"
        if cp "$DLL_BACKUP" "$DLL_DST" 2>/dev/null; then
            echo "  DLL restored"
            rm -f "$DLL_BACKUP"
        else
            echo "  WARNING: DLL restore failed; backup at $DLL_BACKUP — manual restore needed"
        fi
    fi

    if [ "$STATION_CPP_BACKUP_CREATED" = "1" ]; then
        echo "  restoring station probe.cpp from backup"
        if ssh -i "$SSH_KEY" -o ConnectTimeout=5 claude@station \
            "if exist $STATION_CPP_BACKUP move /Y $STATION_CPP_BACKUP $STATION_CPP_REMOTE" 2>/dev/null; then
            echo "  station revert ok"
        else
            echo "  WARNING: station revert failed (backup at $STATION_CPP_BACKUP — manual restore needed)"
        fi
    fi

    if [ "$LOCAL_PATCH_APPLIED" = "1" ]; then
        echo "  reverting local patch"
        git apply -R probe/v6.1/probe-v6.1.patch || echo "  WARNING: local revert failed (check git status)"
    fi

    exit "$exit_code"
}
# Cleanup fires on ERR (set -e), AND on Ctrl-C / SIGTERM / SIGHUP so a user
# interruption doesn't leave a half-deployed state. The guard prevents double
# cleanup if both an error AND a signal fire.
CLEANUP_DONE=0
cleanup_guard() {
    if [ "$CLEANUP_DONE" = "0" ]; then
        CLEANUP_DONE=1
        cleanup_on_fail
    fi
}
trap cleanup_guard ERR INT TERM HUP

echo "=== Step 1: dry-run patch application ==="
if ! git apply --check probe/v6.1/probe-v6.1.patch; then
    echo "ERROR: patch no longer applies cleanly. probe.cpp may have changed."
    echo "Hand-merge from probe/v6.1/CHANGES.md or regenerate the patch."
    trap - ERR
    exit 1
fi

echo "=== Step 2: apply patch to local probe.cpp ==="
git apply probe/v6.1/probe-v6.1.patch
LOCAL_PATCH_APPLIED=1
echo "  probe.cpp patched. Sanity checks:"
echo "  - v6.1 markers in probe.cpp: $(grep -c "v6.1:" probe/probe.cpp)"
echo "  - 60s retry loop:            $(grep -c "i < 120" probe/probe.cpp)"
echo "  - WCM retry counter:         $(grep -c "s_wcmRetryCount" probe/probe.cpp)"
echo "  - F11 roster recheck:        $(grep -c "roster recheck attempt" probe/probe.cpp)"

if ! ssh -i "$SSH_KEY" -o ConnectTimeout=5 claude@station 'echo SSH_OK' >/dev/null 2>&1; then
    echo "ERROR: SSH to station unreachable. Has Josh started the SSH service?"
    false
fi
echo "  SSH to station: ok"

echo "=== Step 3: backup + copy probe.cpp to station via SCP ==="
# Refuse to clobber an existing station backup (would lose v6.0 rollback).
# Capture SSH output + exit status separately so a transient SSH failure
# is not silently treated as "backup does not exist."
SSH_CHECK_OUT=$(ssh -i "$SSH_KEY" claude@station "if exist $STATION_CPP_BACKUP (echo BACKUP_EXISTS) else (echo BACKUP_ABSENT)" 2>&1)
SSH_CHECK_RC=$?
if [ "$SSH_CHECK_RC" -ne 0 ]; then
    echo "ERROR: SSH backup-check failed (rc=$SSH_CHECK_RC):"
    echo "$SSH_CHECK_OUT"
    false
fi
if echo "$SSH_CHECK_OUT" | grep -q BACKUP_EXISTS; then
    echo "ERROR: station already has $STATION_CPP_BACKUP — refusing to overwrite."
    echo "Remove that backup manually if you really mean to re-deploy v6.1 fresh."
    false
fi
if ! echo "$SSH_CHECK_OUT" | grep -q BACKUP_ABSENT; then
    echo "ERROR: SSH backup-check returned unexpected output:"
    echo "$SSH_CHECK_OUT"
    false
fi
# Create the station backup BEFORE scp so a partial-scp failure can still
# restore via the trap.
ssh -i "$SSH_KEY" claude@station "copy /Y $STATION_CPP_REMOTE $STATION_CPP_BACKUP" >/dev/null
STATION_CPP_BACKUP_CREATED=1
echo "  station backup created at $STATION_CPP_BACKUP"
scp -i "$SSH_KEY" probe/probe.cpp "claude@station:$STATION_CPP_REMOTE"
echo "  SCP ok"

echo "=== Step 4: build via MSBuild ==="
if ! ssh -i "$SSH_KEY" claude@station \
    '"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\Projects\elden-ring\probe\probe.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal'; then
    echo "ERROR: MSBuild failed (see build output above)"
    false
fi

echo "=== Step 5: stage DLL into Game/mods ==="
if [ ! -f "$DLL_SRC" ]; then
    echo "ERROR: build artifact not found at $DLL_SRC"
    false
fi
ls -l "$DLL_SRC"

if [ -f "$DLL_BACKUP" ]; then
    echo "ERROR: $DLL_BACKUP already exists — refusing to overwrite v6.0 rollback artifact."
    echo "Either we're already on v6.1, or a previous run was aborted."
    echo "Inspect manually before re-running."
    false
fi
if [ -f "$DLL_DST" ]; then
    echo "  backing up current DLL to $DLL_BACKUP"
    cp "$DLL_DST" "$DLL_BACKUP"
    DLL_BACKUP_CREATED=1
fi
# Atomic-ish swap: copy to a temp file in the same dir, then rename over $DLL_DST.
TMP_DST="$DLL_DST.tmp.$$"
cp "$DLL_SRC" "$TMP_DST"
mv "$TMP_DST" "$DLL_DST"
ls -l "$DLL_DST"

# Success — clean up the station-side .cpp backup (local-side rollback via git
# is sufficient from here on).
echo "  removing station-side probe.cpp.v6.0-backup (success)"
ssh -i "$SSH_KEY" claude@station "del $STATION_CPP_BACKUP" >/dev/null 2>&1 || true

trap - ERR INT TERM HUP
echo ""
echo "=== DONE — v6.1 is live ==="
echo "  - probe/probe.cpp is patched (commit when ready)"
echo "  - parry-tell-probe.dll is updated in Game/mods/"
echo "  - v6.0 DLL backed up as $DLL_BACKUP"
echo ""
echo "Next: launch Elden Ring + load save + walk to a Grace + press F11."
