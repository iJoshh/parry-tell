#!/bin/bash
# archive_session.sh — copy a capture session from station SMB to project tree.
#
# Usage:
#   tools/archive_session.sh                        # archive ALL captures from today
#   tools/archive_session.sh <session-name>         # archive one specific session
#                                                   # (e.g. qualification-20260511-195759)
#   tools/archive_session.sh --date YYYYMMDD        # archive all captures from a date
#
# Destination: captures/sessions/YYYYMMDD/<session-name>.{bin,csv,log.txt}
#
# SMB-perf rule: this is a deliberate SMB copy. Reading the bin via the
# analyzer should still be done from the local archive path, not from the
# SMB mount.

set -euo pipefail

cd "$(dirname "$0")/.."

SMB_LOGS="/mnt/station-projects/elden-ring/logs"
SMB_STATION_LOG="/mnt/station-projects/STATION.log"
DEST_ROOT="captures/sessions"

if [ ! -d "$SMB_LOGS" ]; then
    echo "ERROR: SMB not mounted at $SMB_LOGS" >&2
    exit 1
fi

mode="today"
filter=""
case "${1:-}" in
    --date)
        if [ -z "${2:-}" ]; then
            echo "ERROR: --date requires YYYYMMDD" >&2
            exit 1
        fi
        if ! [[ "$2" =~ ^[0-9]{8}$ ]]; then
            echo "ERROR: --date arg must be exactly 8 digits (YYYYMMDD), got '$2'" >&2
            exit 1
        fi
        mode="date"
        filter="$2"
        ;;
    --help|-h)
        head -16 "$0" | tail -15
        exit 0
        ;;
    "")
        mode="today"
        filter=$(date +%Y%m%d)
        ;;
    *)
        mode="single"
        filter="$1"
        ;;
esac

archive_one() {
    local session_name="$1"
    # Hard validation — session names MUST match the probe's naming pattern.
    # Format: <mode-letters>-YYYYMMDD-HHMMSS. Reject anything else (including
    # slashes, .., dots, etc.) before constructing paths. Path-traversal safe.
    if ! [[ "$session_name" =~ ^[a-z][a-z0-9_]{0,31}-[0-9]{8}-[0-9]{6}$ ]]; then
        echo "ERROR: refusing session name '$session_name' — must match" >&2
        echo "       ^[a-z][a-z0-9_]{0,31}-[0-9]{8}-[0-9]{6}$" >&2
        return 1
    fi

    local date_dir
    if [[ "$session_name" =~ ^[a-z][a-z0-9_]*-([0-9]{8})- ]]; then
        date_dir="${BASH_REMATCH[1]}"
    else
        # unreachable given the validation above, but defense in depth
        date_dir=$(date +%Y%m%d)
    fi

    local dest="$DEST_ROOT/$date_dir"
    mkdir -p "$dest"

    # Build the list of files to archive. The bin can rotate to
    # ${session}.bin.001, .002, ... at 2 GB boundaries per
    # probe.cpp RotateBinIfNeeded; copy ALL shards (codex deep-critic).
    local archive_paths=()
    archive_paths+=( "${session_name}.bin" )
    local shard_idx=1
    while true; do
        local shard
        shard=$(printf "%s.bin.%03d" "$session_name" "$shard_idx")
        if [ -f "$SMB_LOGS/$shard" ]; then
            archive_paths+=( "$shard" )
            shard_idx=$((shard_idx + 1))
        else
            break
        fi
    done
    archive_paths+=( "${session_name}.csv" )
    archive_paths+=( "${session_name}.log.txt" )

    for rel in "${archive_paths[@]}"; do
        local src="$SMB_LOGS/$rel"
        local dst="$dest/$rel"
        if [ ! -f "$src" ]; then
            echo "  skip $rel: not on station"
            continue
        fi
        # Skip if local archive matches source by BOTH size AND mtime.
        # Size-only is unreliable for in-progress captures of identical
        # final byte count. mtime catches the rewrite-with-same-size case.
        local src_size src_mtime
        src_size=$(stat -c %s "$src")
        src_mtime=$(stat -c %Y "$src")
        if [ -f "$dst" ]; then
            local dst_size dst_mtime
            dst_size=$(stat -c %s "$dst")
            dst_mtime=$(stat -c %Y "$dst")
            if [ "$src_size" = "$dst_size" ] && [ "$src_mtime" = "$dst_mtime" ]; then
                echo "  already archived: $dst"
                continue
            fi
        fi

        # Atomic-ish copy: write to .partial in the dest, verify the source
        # didn't grow during the copy, then rename. Race with an
        # in-progress probe writing more data is detected (we'll re-archive
        # next run) but not prevented — that's fine for our use case since
        # Josh always disarms before asking for archive.
        local partial="${dst}.partial.$$"
        echo "  copying $rel ($(stat -c %s "$src" | numfmt --to=iec))..."
        cp "$src" "$partial"

        local after_size after_mtime
        after_size=$(stat -c %s "$src")
        after_mtime=$(stat -c %Y "$src")
        if [ "$src_size" != "$after_size" ] || [ "$src_mtime" != "$after_mtime" ]; then
            echo "  WARN: source changed during copy (size $src_size -> $after_size); discarding partial"
            rm -f "$partial"
            continue
        fi
        mv "$partial" "$dst"
        # Preserve the source mtime so future skip-checks are accurate.
        touch -d "@$src_mtime" "$dst"
    done

    # Also snapshot the current STATION.log alongside the bin (one per
    # session-archive call — gets overwritten by later calls in the same
    # session, that's fine since we re-pull anyway).
    if [ -f "$SMB_STATION_LOG" ]; then
        cp "$SMB_STATION_LOG" "$dest/STATION-${session_name}.log"
    fi

    echo "  archived to $dest/"
}

case "$mode" in
    single)
        echo "Archiving single session: $filter"
        archive_one "$filter"
        ;;
    today|date)
        echo "Archiving sessions matching: $filter*"
        found=0
        # Find .bin files matching the filter, derive session names from them.
        for binf in "$SMB_LOGS"/*"${filter}"*.bin; do
            if [ ! -f "$binf" ]; then continue; fi
            session=$(basename "$binf" .bin)
            echo ""
            echo "Session: $session"
            archive_one "$session"
            found=$((found + 1))
        done
        if [ "$found" = "0" ]; then
            echo "(no sessions found matching $filter)"
        else
            echo ""
            echo "Archived $found session(s)."
        fi
        ;;
esac
