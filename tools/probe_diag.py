"""probe_diag.py — aggregate diagnostic logs from the latest probe run.

When a smoke / qualification / discovery test fails, the failure info is
split between two files:

  1. parry-tell-probe.boot.log next to the DLL in Game\\mods\\
     (written before config is loaded — sig-scan failures, version
     mismatches, roster validation, hook install)
  2. <session>-<ts>.log.txt in the configured log_dir
     (written after config is loaded — runtime warnings, F11 events,
     adaptive sampling stepdowns)

This tool finds the most recent of each (via the SMB-mounted paths from
the VM) and prints them together in chronological order.

Usage:
    python tools/probe_diag.py                  # latest of everything
    python tools/probe_diag.py --tail 50        # last 50 lines of each
    python tools/probe_diag.py --logs-dir <p>   # override logs dir
"""

from __future__ import annotations

import os
import sys
from typing import Optional

DEFAULT_BOOT_LOG = "/mnt/station-mods/parry-tell-probe.boot.log"
DEFAULT_LOGS_DIR = "/mnt/station-projects/elden-ring/logs"


def _find_latest_log(logs_dir: str) -> Optional[str]:
    """Return path to the most recent .log.txt under logs_dir, or None.

    Handles stale-SMB / permission errors gracefully — the whole point of
    this helper is to collect failure context, so a stale mount on the logs
    dir must not crash the diagnostic itself.
    """
    if not os.path.isdir(logs_dir):
        return None
    try:
        names = os.listdir(logs_dir)
    except OSError as exc:
        print(f"[warn] cannot list logs dir {logs_dir}: {exc}", file=sys.stderr)
        return None
    candidates = []
    for name in names:
        if not name.endswith(".log.txt"):
            continue
        full = os.path.join(logs_dir, name)
        try:
            candidates.append((os.path.getmtime(full), full))
        except OSError:
            continue
    if not candidates:
        return None
    candidates.sort(reverse=True)
    return candidates[0][1]


def _tail(path: str, n: int) -> list[str]:
    try:
        with open(path) as fh:
            lines = fh.readlines()
    except OSError as exc:
        return [f"[error reading {path}: {exc}]"]
    return lines[-n:] if n > 0 else lines


def main(argv: list[str]) -> int:
    tail = 0  # 0 = full file
    logs_dir = DEFAULT_LOGS_DIR
    boot_log = DEFAULT_BOOT_LOG

    i = 1
    while i < len(argv):
        if argv[i] == "--tail" and i + 1 < len(argv):
            try:
                tail = int(argv[i + 1])
            except ValueError:
                print(f"--tail expects an integer, got {argv[i + 1]!r}", file=sys.stderr)
                return 2
            if tail < 0:
                print(f"--tail must be >= 0 (use 0 for full file)", file=sys.stderr)
                return 2
            i += 2
        elif argv[i] == "--logs-dir" and i + 1 < len(argv):
            logs_dir = argv[i + 1]; i += 2
        elif argv[i] == "--boot-log" and i + 1 < len(argv):
            boot_log = argv[i + 1]; i += 2
        elif argv[i] in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            print(f"unknown arg: {argv[i]}", file=sys.stderr)
            return 2

    print("=" * 70)
    print(f"BOOT LOG: {boot_log}")
    print("=" * 70)
    if not os.path.exists(boot_log):
        print(f"[not found — DLL may have failed to write its boot log]")
        print(f"[expected: <mods folder>/parry-tell-probe.boot.log]")
    else:
        for line in _tail(boot_log, tail):
            print(line.rstrip())

    print()
    latest = _find_latest_log(logs_dir)
    print("=" * 70)
    print(f"LATEST SESSION LOG: {latest or '(none found)'}")
    print(f"  (search dir: {logs_dir})")
    print("=" * 70)
    if latest is None:
        print("[no .log.txt files in logs dir — capture didn't reach the "
              "session-files-open phase. Check boot log for init failure.]")
    else:
        for line in _tail(latest, tail):
            print(line.rstrip())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
