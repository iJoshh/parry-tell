#!/usr/bin/env python3
"""segment_by_f11.py — split a probe capture into per-F11-cycle segments.

A v6.4+ session typically contains multiple F11 arm/disarm cycles, one per
boss arena. This tool reads the session's .log.txt to find ARMED/DISARMED
boundaries and reports the corresponding sample windows in the .bin.

Usage:
    python tools/segment_by_f11.py <base-path>
    python tools/segment_by_f11.py <base-path> --label 0=margit 1=godrick

NOTE: --emit-bins is reserved but NOT implemented. The manifest produced
contains all the timing info needed to filter the .bin downstream
(qualify_oracle.py can take a ts_ms_rel range filter).

<base-path> is the session path without extension, e.g.
    captures/sessions/20260511/qualification-20260511-195759

Output: a JSON segment manifest at <base-path>.segments.json with the shape:
{
  "session": "qualification-20260511-195759",
  "armed_intervals_ms_rel": [[t_arm, t_disarm], ...],
  "segments": [
    {"index": 0, "label": "boss1_margit", "ts_start_ms_rel": ..., "ts_end_ms_rel": ...,
     "sample_first_idx": ..., "sample_last_idx": ..., "focused_rows": ...,
     "distinct_focused_cids": [4380, 4382], ...},
    ...
  ]
}

The per-segment .bin files (with --emit-bins) are named
<base-path>.segment-NN.bin and contain only the PTS0 records whose
ts_ms_rel falls inside the ARMED interval. Useful for handing one fight
at a time to the analyzer.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys
from dataclasses import asdict, dataclass, field
from typing import Any, Optional

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

import probe_bin


F11_PATTERN = re.compile(r"^(\d+)\s+F11:\s+(armed|disarmed)\s*$")


@dataclass
class ArmInterval:
    arm_ts_ms_rel: int
    disarm_ts_ms_rel: Optional[int]   # None if session ended while armed

    @property
    def duration_ms(self) -> Optional[int]:
        if self.disarm_ts_ms_rel is None:
            return None
        return self.disarm_ts_ms_rel - self.arm_ts_ms_rel


def parse_f11_log(log_path: str) -> list[ArmInterval]:
    """Read the .log.txt and extract ARMED/DISARMED pairs.

    Tolerates: multiple arm-without-disarm cycles (the probe never re-arms
    while armed, but if the log is truncated we just keep the last-known
    arm timestamp). Returns intervals in chronological order.
    """
    intervals: list[ArmInterval] = []
    current_arm: Optional[int] = None
    with open(log_path, "r") as fh:
        for line in fh:
            m = F11_PATTERN.match(line.strip())
            if not m:
                continue
            ts_rel = int(m.group(1))
            evt = m.group(2)
            if evt == "armed":
                if current_arm is not None:
                    # arm-without-disarm: implicitly close the previous
                    # interval AT THE NEW ARM TIMESTAMP. This matches the
                    # comment's stated semantics and prevents the stale
                    # interval from greedily consuming all later samples
                    # in segment_samples() (which would otherwise leave
                    # the real second segment empty).
                    intervals.append(ArmInterval(current_arm, ts_rel))
                current_arm = ts_rel
            elif evt == "disarmed":
                if current_arm is not None:
                    intervals.append(ArmInterval(current_arm, ts_rel))
                    current_arm = None
                # disarm without arm: ignore (boot log spam, race)
    # Trailing armed-but-no-disarm: keep as open interval
    if current_arm is not None:
        intervals.append(ArmInterval(current_arm, None))
    return intervals


def segment_samples(
    base_path: str,
    intervals: list[ArmInterval],
    labels: dict[int, str],
) -> tuple[list[dict[str, Any]], list[Any]]:
    """Walk samples once across all bin shards, bucket by armed-interval.

    `base_path` is the session path WITHOUT a .bin extension. We use
    probe_bin.read_session so rotated shards (`.bin.001`, `.bin.002`, ...)
    are walked in order. Probe rotates at 2 GB boundaries (see
    RotateBinIfNeeded in probe.cpp).

    Returns (segments_metadata, all_samples_in_order).

    The .log.txt F11 timestamps and the .bin sample ts_ms_rel use DIFFERENT
    epochs. The .log.txt uses probe-init-relative ms (i.e. "since the probe
    DLL loaded"); the .bin sample ts_ms_rel is relative to session_start_ms
    which is captured in the manifest header. We translate log-side
    timestamps into bin-side timestamps via `log_ts - session_start_ms`
    before comparison.
    """
    samples: list[Any] = []
    session_start_ms: Optional[int] = None
    for bf in probe_bin.read_session(base_path):
        samples.extend(bf.samples)
        if session_start_ms is None:
            for m in bf.manifests:
                v = m.fields.get("session_start_ms")
                if v is not None:
                    try:
                        session_start_ms = int(v)
                        break
                    except ValueError:
                        pass
    if session_start_ms is None:
        # If we can't find it, the segmentation results will be wrong by a
        # large offset. Bail loudly rather than silently producing empty
        # segments.
        raise RuntimeError(
            f"no session_start_ms in any manifest at {base_path}.bin*; cannot "
            f"translate .log.txt timestamps."
        )

    # Pre-build interval [start, end] in BIN ts_ms_rel coordinates.
    # Open intervals get end = the last sample's ts (good-enough for cases
    # where the probe was disarmed implicitly by game exit).
    last_ts = samples[-1].ts_ms_rel if samples else 0
    interval_bounds: list[tuple[int, int]] = []
    for iv in intervals:
        start = iv.arm_ts_ms_rel - session_start_ms
        end = (iv.disarm_ts_ms_rel - session_start_ms) if iv.disarm_ts_ms_rel is not None else last_ts
        interval_bounds.append((start, end))

    # Assign each sample to at most one interval (the first that contains it).
    # Samples outside all intervals are "between captures" — discarded for
    # per-segment work but the global .bin still has them.
    seg_samples: list[list[tuple[int, Any]]] = [[] for _ in interval_bounds]
    for i, s in enumerate(samples):
        ts = s.ts_ms_rel
        for j, (a, b) in enumerate(interval_bounds):
            if a <= ts <= b:
                seg_samples[j].append((i, s))
                break

    segments_meta: list[dict[str, Any]] = []
    for j, (a, b) in enumerate(interval_bounds):
        sm = seg_samples[j]
        label = labels.get(j, f"seg{j:02d}")
        focused_cids: collections.Counter[int] = collections.Counter()
        focused_rows = 0
        for _, s in sm:
            for e in s.enemies:
                if e.is_focused:
                    focused_rows += 1
                    focused_cids[e.field_at_0x064] += 1
        segments_meta.append(
            {
                "index": j,
                "label": label,
                "ts_start_ms_rel": a,
                "ts_end_ms_rel": b,
                "duration_ms": b - a,
                "sample_count": len(sm),
                "sample_first_idx": sm[0][0] if sm else None,
                "sample_last_idx": sm[-1][0] if sm else None,
                "focused_rows": focused_rows,
                "distinct_focused_cids": dict(focused_cids.most_common(10)),
                "open_interval": intervals[j].disarm_ts_ms_rel is None,
            }
        )
    return segments_meta, samples


def emit_segment_bins(
    base_path: str,
    segments_meta: list[dict[str, Any]],
    all_samples: list[Any],
    raw_bin_path: str,
) -> None:
    """Write per-segment .bin files by carving the original record stream.

    NOTE: probe_bin parses records to Sample objects but doesn't expose
    the byte-level record offsets. For per-segment .bin emit, we re-parse
    the source file at the record level (each record header has a u32
    record_size). This is good-enough for a Quick Tool — for production
    analyzers, just feed the original .bin to qualify_oracle with a
    ts_ms_rel filter.
    """
    raise NotImplementedError(
        "emit_bins is not implemented; use the segment metadata + a ts_ms_rel "
        "filter on probe_bin instead. (Implementing requires exposing record "
        "byte offsets in probe_bin.read_bin which we deliberately don't do.)"
    )


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("base_path", help="session base path (no extension)")
    ap.add_argument(
        "--label",
        nargs="*",
        default=[],
        help="segN=name mappings, e.g. --label 0=margit 1=godrick",
    )
    ap.add_argument(
        "--emit-bins",
        action="store_true",
        help="(NOT IMPLEMENTED) emit per-segment .bin files",
    )
    ap.add_argument(
        "--out",
        default=None,
        help="output JSON path (default: <base>.segments.json)",
    )
    args = ap.parse_args(argv[1:])

    bin_path = args.base_path + ".bin"
    log_path = args.base_path + ".log.txt"
    out_path = args.out or (args.base_path + ".segments.json")

    if not os.path.exists(bin_path):
        print(f"ERROR: {bin_path} not found", file=sys.stderr)
        return 1
    if not os.path.exists(log_path):
        print(f"ERROR: {log_path} not found", file=sys.stderr)
        return 1

    labels: dict[int, str] = {}
    for raw in args.label:
        if "=" not in raw:
            print(f"WARN: ignoring malformed --label '{raw}' (need N=name)", file=sys.stderr)
            continue
        k, v = raw.split("=", 1)
        try:
            labels[int(k)] = v
        except ValueError:
            print(f"WARN: ignoring non-integer label index '{k}'", file=sys.stderr)

    intervals = parse_f11_log(log_path)
    if not intervals:
        print(f"WARN: no F11 armed/disarmed pairs in {log_path}", file=sys.stderr)
        # still write an empty manifest so callers know we ran
        manifest = {
            "session": os.path.basename(args.base_path),
            "armed_intervals_ms_rel": [],
            "segments": [],
        }
        with open(out_path, "w") as fh:
            json.dump(manifest, fh, indent=2)
        print(f"wrote {out_path} (empty)")
        return 0

    segments, _samples = segment_samples(args.base_path, intervals, labels)

    manifest = {
        "session": os.path.basename(args.base_path),
        "armed_intervals_ms_rel": [
            [iv.arm_ts_ms_rel, iv.disarm_ts_ms_rel] for iv in intervals
        ],
        "segments": segments,
    }

    with open(out_path, "w") as fh:
        json.dump(manifest, fh, indent=2)
    print(f"wrote {out_path}")

    # Tiny human-readable summary
    print()
    print(f"  session: {os.path.basename(args.base_path)}")
    print(f"  intervals: {len(intervals)}")
    for seg in segments:
        dur_s = seg["duration_ms"] / 1000.0
        open_mark = " (open)" if seg["open_interval"] else ""
        cids = ", ".join(
            f"c{cid}:{n}" for cid, n in seg["distinct_focused_cids"].items()
        )
        print(
            f"    seg{seg['index']:02d} ({seg['label']}): "
            f"{dur_s:5.1f}s, {seg['focused_rows']:5d} focused rows{open_mark}"
        )
        if cids:
            print(f"      focused cids: {cids}")

    if args.emit_bins:
        print(
            "ERROR: --emit-bins is not implemented in this version. "
            "The manifest was written but no .bin files were produced. "
            "Use the segment metadata + a ts_ms_rel filter on probe_bin instead.",
            file=sys.stderr,
        )
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
