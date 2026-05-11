#!/usr/bin/env python3
"""
calibrate_smoke.py — Standalone smoke calibration analyzer.

Re-derives the animation-time candidate analysis in Python from a captured
.bin file. Equivalent to the in-DLL WriteCalibrationReport(), but reliable:
the in-DLL version only fires on clean worker-thread exit, which doesn't
always happen if the game is closed mid-loop.

Usage:
    python tools/calibrate_smoke.py <base-path>

Where <base-path> is the session prefix without extension, e.g.:
    /mnt/station-projects/elden-ring/logs/smoke-20260509-170547

Output:
    Prints a per-candidate report and an overall VERDICT to stdout.
    Writes <base-path>.calibration.txt with the same content.

Gate logic (matches probe.cpp WriteCalibrationReport rules, with anim-transition
tolerance correction):

  For each of the 4 candidate offsets (TimeAct + 0x20, +0x24, +0x28, +0x2C):
    - f32_in_range: every observed value is in [0, 600] seconds.
    - monotonic_segments: count of distinct anim_id spans where the value
      grew by >= 0.05 seconds (i.e. accumulated, not stuck at a constant).
    - max_segment_dur: longest single-segment growth observed.
    - rewinds_on_anim_id_change: when anim_id changes, the candidate value
      should drop. Compared with a 2-sample tolerance window after each
      transition (the probe races the game's anim state machine and the
      value can lag 1-2 samples behind the id change).

  PASS gate: f32_in_range AND monotonic_segments >= 3 AND
             max_segment_dur >= 0.3 AND rewinds_on_anim_id_change.

The winner is the PASS candidate with the largest max_segment_dur.

This is a smoke-mode answer; qualification disambiguates between two close
candidates by matching predicted parry windows against database ground truth.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from typing import List, Optional

# Allow running from anywhere under the project.
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import probe_bin

CANDIDATE_OFFSETS = [0x20, 0x24, 0x28, 0x2C]

# Tolerance: number of post-transition samples to ignore for rewind detection.
#
# Measured against the 2026-05-09 smoke capture (9,867 samples, 38 anim
# transitions): observed lag from anim_id change to first value update on
# +0x24/+0x28 ranges from 1 to 10 samples (~11ms to ~110ms at 91 Hz).
# Median 4 samples, P90 9 samples. A 12-sample window covers the full
# observed distribution with margin; raise it again if a future capture
# blows past this.
ANIM_TRANSITION_TOLERANCE_SAMPLES = 12

# Floating-point tolerance for "value did not decrease."
FP_TOLERANCE = 0.001

# Minimum monotonic growth to count as a real segment (filters out values
# stuck at a constant for the whole anim).
MIN_SEGMENT_GROWTH_SEC = 0.05


@dataclass
class CandidateResult:
    offset: int
    f32_in_range: bool = True
    monotonic_segments: int = 0
    max_segment_dur: float = 0.0
    rewinds_on_anim_id_change: bool = True
    # Diagnostics
    transitions_seen: int = 0
    rewind_violations: int = 0
    first_violation_anim_change: Optional[tuple] = None  # (old_id, new_id, ts_ms)
    raw_min: float = float("inf")
    raw_max: float = float("-inf")

    def gate_pass(self) -> bool:
        return (
            self.f32_in_range
            and self.monotonic_segments >= 3
            and self.max_segment_dur >= 0.3
            and self.rewinds_on_anim_id_change
        )

    def fail_reasons(self) -> List[str]:
        reasons = []
        if not self.f32_in_range:
            reasons.append(
                f"out-of-range value seen (raw_min={self.raw_min:.2f}, raw_max={self.raw_max:.2f})"
            )
        if self.monotonic_segments < 3:
            reasons.append(
                f"only {self.monotonic_segments} monotonic segment(s) (need >= 3)"
            )
        if self.max_segment_dur < 0.3:
            reasons.append(
                f"longest segment only {self.max_segment_dur:.2f}s (need >= 0.30s)"
            )
        if not self.rewinds_on_anim_id_change:
            reasons.append(
                f"{self.rewind_violations}/{self.transitions_seen} anim transitions "
                "did not produce a value rewind"
            )
        return reasons


def analyze_candidate(samples, candidate_idx: int) -> CandidateResult:
    """Process all samples for a single candidate offset and produce a result."""
    r = CandidateResult(offset=CANDIDATE_OFFSETS[candidate_idx])

    cur_anim_id: Optional[int] = None
    cur_seg_start: Optional[float] = None
    cur_seg_max: Optional[float] = None
    # cur_anim_max: peak value seen across the ENTIRE current anim, not just
    # the current monotonic segment. The rewind check compares against this
    # so that within-anim micro-rewinds (e.g. looping anims, multi-segment
    # tracks) do not corrupt the comparison baseline.
    cur_anim_max: Optional[float] = None
    samples_since_id_change: int = 0
    pending_id_change: Optional[tuple] = None  # (old_id, new_id, prev_anim_max, ts_ms)

    for s in samples:
        val = s.player_anim_time[candidate_idx]
        anim_id = s.player_anim_id
        ts = s.ts_ms_rel

        # Track raw value range.
        if val < r.raw_min:
            r.raw_min = val
        if val > r.raw_max:
            r.raw_max = val

        # Range check.
        if not (0.0 <= val <= 600.0):
            r.f32_in_range = False
            # Continue scanning so raw_min/max are accurate.

        # First sample bootstrap.
        if cur_anim_id is None:
            cur_anim_id = anim_id
            cur_seg_start = val
            cur_seg_max = val
            cur_anim_max = val
            samples_since_id_change = 999  # past tolerance from the start
            continue

        # Detect anim_id change.
        if anim_id != cur_anim_id:
            # Close the prior segment if it was real.
            seg_dur = (cur_seg_max - cur_seg_start) if (cur_seg_max is not None and cur_seg_start is not None) else 0.0
            if seg_dur >= MIN_SEGMENT_GROWTH_SEC:
                r.monotonic_segments += 1
                if seg_dur > r.max_segment_dur:
                    r.max_segment_dur = seg_dur
            # Start tracking the new anim's first sample.
            r.transitions_seen += 1
            pending_id_change = (cur_anim_id, anim_id, cur_anim_max, ts)
            cur_anim_id = anim_id
            cur_seg_start = val
            cur_seg_max = val
            cur_anim_max = val
            samples_since_id_change = 0
            continue

        # Same anim_id as previous sample.
        samples_since_id_change += 1

        # Track whole-anim peak (never resets within an anim).
        if cur_anim_max is None or val > cur_anim_max:
            cur_anim_max = val

        # Rewind check: only evaluate AFTER the tolerance window. The reason
        # is the probe samples at ~90Hz and the game's anim transition takes
        # several frames (median ~4, P90 ~9 on the 2026-05-09 capture) to
        # propagate into the time field, so the first samples after an id
        # change can still carry the old anim's value. Compare against the
        # PREVIOUS anim's whole-anim peak so within-anim micro-rewinds don't
        # corrupt the baseline.
        if (
            pending_id_change is not None
            and samples_since_id_change >= ANIM_TRANSITION_TOLERANCE_SAMPLES
        ):
            old_max = pending_id_change[2]
            if old_max is not None and val >= old_max - FP_TOLERANCE:
                # Value did not rewind across the transition.
                r.rewind_violations += 1
                r.rewinds_on_anim_id_change = False
                if r.first_violation_anim_change is None:
                    r.first_violation_anim_change = (
                        pending_id_change[0],
                        pending_id_change[1],
                        pending_id_change[3],
                    )
            pending_id_change = None  # only check once per transition

        # Monotonic growth within the segment.
        if cur_seg_max is None:
            cur_seg_max = val
            cur_seg_start = val
        elif val < cur_seg_max - FP_TOLERANCE:
            # Reset segment — value went backwards within same anim.
            # This isn't necessarily a fail (could be looping anim), just
            # close the current segment and start fresh.
            seg_dur = cur_seg_max - cur_seg_start if cur_seg_start is not None else 0.0
            if seg_dur >= MIN_SEGMENT_GROWTH_SEC:
                r.monotonic_segments += 1
                if seg_dur > r.max_segment_dur:
                    r.max_segment_dur = seg_dur
            cur_seg_start = val
            cur_seg_max = val
        else:
            cur_seg_max = val

    # Close the final open segment.
    if cur_seg_max is not None and cur_seg_start is not None:
        seg_dur = cur_seg_max - cur_seg_start
        if seg_dur >= MIN_SEGMENT_GROWTH_SEC:
            r.monotonic_segments += 1
            if seg_dur > r.max_segment_dur:
                r.max_segment_dur = seg_dur

    # If we never saw any anim transitions, conservatively mark rewind as False.
    if r.transitions_seen == 0:
        r.rewinds_on_anim_id_change = False

    return r


def format_report(samples, results: List[CandidateResult]) -> str:
    lines = []
    push = lines.append

    push("=" * 72)
    push("ANIMATION-TIME CANDIDATE ANALYSIS — smoke run")
    push("=" * 72)
    push("")
    push(f"Samples processed: {len(samples)}")
    if samples:
        dur_ms = samples[-1].ts_ms_rel - samples[0].ts_ms_rel
        push(f"Capture duration: {dur_ms / 1000.0:.1f}s ({samples[0].ts_ms_rel}ms -> {samples[-1].ts_ms_rel}ms)")
        rate = len(samples) / (dur_ms / 1000.0) if dur_ms > 0 else 0
        push(f"Effective sample rate: {rate:.1f} Hz")
        distinct_ids = sorted({s.player_anim_id for s in samples})
        push(f"Distinct player anim_ids: {len(distinct_ids)}")
        if len(distinct_ids) <= 24:
            push(f"  ids: {distinct_ids}")
        else:
            push(f"  first 12: {distinct_ids[:12]}")
            push(f"  last  12: {distinct_ids[-12:]}")
    push("")

    push("Per-candidate gate results:")
    push("")
    push("  offset   monotonic   max_seg   in_range  rewinds   transitions   gate")
    push("  ------   ---------   -------   --------  -------   -----------   ----")
    for r in results:
        gate = "PASS" if r.gate_pass() else "FAIL"
        rewinds_str = f"{r.transitions_seen - r.rewind_violations}/{r.transitions_seen}"
        push(
            f"  +0x{r.offset:02X}     {r.monotonic_segments:6d}      "
            f"{r.max_segment_dur:5.2f}s   "
            f"{'yes' if r.f32_in_range else 'NO ':>5}      "
            f"{rewinds_str:>7}   {r.transitions_seen:9d}     {gate}"
        )
    push("")

    # Failure reasons for FAILed candidates.
    failed = [r for r in results if not r.gate_pass()]
    if failed:
        push("Failure breakdown:")
        for r in failed:
            push(f"  +0x{r.offset:02X}:")
            for reason in r.fail_reasons():
                push(f"    - {reason}")
            if r.first_violation_anim_change:
                old_id, new_id, ts = r.first_violation_anim_change
                push(f"    first non-rewind transition: anim_id {old_id} -> {new_id} at ts={ts}ms")
        push("")

    # Verdict.
    push("=" * 72)
    push("VERDICT")
    push("=" * 72)
    passing = [r for r in results if r.gate_pass()]
    if not passing:
        push("FAIL — no candidate passed the gate.")
        push("")
        push("Next step: re-research the TimeAct struct layout for ER 2.6.1.")
        push("The probe's offsets 0x20/0x24/0x28/0x2C are the four working candidates")
        push("from the v6 spec. If none pass, the struct has shifted or the candidate")
        push("set needs widening.")
    elif len(passing) == 1:
        w = passing[0]
        push(f"PASS — TimeAct + 0x{w.offset:02X} is the unique winner.")
        push(f"  max_segment_dur={w.max_segment_dur:.2f}s, monotonic_segments={w.monotonic_segments}")
        push("")
        push("Next step: proceed to qualification mode against a Banished Knight.")
    else:
        # Multiple candidates passed; rank by max_segment_dur.
        passing.sort(key=lambda x: x.max_segment_dur, reverse=True)
        push(f"PASS — {len(passing)} candidates passed the gate:")
        for r in passing:
            push(f"  +0x{r.offset:02X} (max_segment_dur={r.max_segment_dur:.2f}s, segments={r.monotonic_segments})")
        push("")
        push(f"Tentative winner by largest segment: TimeAct + 0x{passing[0].offset:02X}")
        push("Note: qualification mode will disambiguate by matching predicted parry")
        push("windows against the parry_data.json ground truth within ±11ms tolerance.")
        push("")
        push("Next step: proceed to qualification — both candidates carry forward.")

    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) != 2:
        print("Usage: calibrate_smoke.py <base-path>", file=sys.stderr)
        print("  e.g. calibrate_smoke.py /mnt/station-projects/elden-ring/logs/smoke-20260509-170547",
              file=sys.stderr)
        return 2

    base = argv[1]
    bin_path = base + ".bin"
    if not os.path.exists(bin_path):
        print(f"ERROR: {bin_path} does not exist", file=sys.stderr)
        return 1

    samples = list(probe_bin.all_samples(base))
    if not samples:
        print("ERROR: no samples in capture (was F11 pressed?)", file=sys.stderr)
        return 1

    results = [analyze_candidate(samples, i) for i in range(4)]
    report = format_report(samples, results)
    sys.stdout.write(report)

    # Also write to disk next to the .bin. The capture path is often on
    # a read-only SMB share (/mnt/station-projects is RO from this VM),
    # so fall back to <repo>/data/calibration-reports/ when the .bin dir
    # is unwritable.
    out_path = base + ".calibration.txt"
    try:
        with open(out_path, "w") as f:
            f.write(report)
        print(f"\nWritten: {out_path}")
    except OSError as e:
        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        fallback_dir = os.path.join(repo_root, "data", "calibration-reports")
        try:
            os.makedirs(fallback_dir, exist_ok=True)
            fallback_out = os.path.join(fallback_dir, os.path.basename(out_path))
            with open(fallback_out, "w") as f:
                f.write(report)
            print(f"\n(write to {out_path} failed: {e}; wrote to {fallback_out} instead)",
                  file=sys.stderr)
        except OSError as e2:
            print(f"\n(could not write report anywhere: {e}; fallback also failed: {e2})",
                  file=sys.stderr)

    # Exit code mirrors the verdict for scripting.
    if any(r.gate_pass() for r in results):
        return 0
    return 3  # gate fail


if __name__ == "__main__":
    sys.exit(main(sys.argv))
