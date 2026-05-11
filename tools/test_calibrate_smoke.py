#!/usr/bin/env python3
"""
test_calibrate_smoke.py — Self-test for calibrate_smoke.py.

Generates a synthetic .bin file modeled on the real smoke-20260509-170547
capture (the first live probe run, ER 2.6.1 player anim sweep) and runs
calibrate_smoke.analyze_candidate on it. Verifies:

  - +0x20 (always-zero junk) FAILs gate (no segments).
  - +0x24, +0x28 (the real anim-time candidates) PASS gate.
  - +0x2C (animation total duration, value resets but doesn't accumulate
    monotonically across anim_ids) FAILs the rewind check.

This is the regression gate. If calibrate_smoke.py's logic drifts, this
test catches it before the next live capture.
"""

from __future__ import annotations

import os
import struct
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import probe_bin
import calibrate_smoke


# Modeled on the 2026-05-09 smoke run.
# - Player traverses 5 distinct anims, each ~1.5-2.0 seconds long.
# - Probe samples at ~91 Hz (~11 ms cadence).
# - +0x20: always 0.0 (junk slot — boolean? early-init bit?).
# - +0x24, +0x28: real anim time (~98% correlated, but +0x24 is the play head
#   and +0x28 is the play head copied with a small fp adjustment). Both
#   reset to ~0 on anim change, accumulate up to anim duration, then jump.
# - +0x2C: total anim duration. CONSTANT within an anim, jumps to new value
#   on anim change. Stays high, never rewinds toward zero.

ANIM_TRACE = [
    # (anim_id, duration_sec, +0x2C_total_dur)
    (68021,    1.8,   18.0),    # idle/walk
    (25030000, 1.0,    0.80),   # light attack 1
    (25030010, 0.95,   2.63),   # light attack 2 (combo)
    (25030020, 1.05,   2.43),   # light attack 3 (combo)
    (50110,    1.7,    1.70),   # gesture
    (80000,    0.45,   0.45),   # roll start
    (80070,    1.55,   1.55),   # roll body
    (27110,    2.0,    2.00),   # sprint
    (68021,    1.6,   18.0),    # idle/walk again
]

SAMPLE_INTERVAL_MS = 11  # ~91 Hz


def build_synthetic_samples():
    """Yield (anim_id, t0, t1, t2, t3, ts_ms) tuples for the trace."""
    ts = 100  # session ms start (small non-zero to mimic real)
    seq = 0
    for anim_id, duration, total_dur in ANIM_TRACE:
        n_samples = int(duration / (SAMPLE_INTERVAL_MS / 1000.0))
        for i in range(n_samples):
            t = i * (SAMPLE_INTERVAL_MS / 1000.0)
            # Model the 1-2 sample lag observed in the real data:
            # for the FIRST 2 samples of a new anim, the +0x24/+0x28 values
            # are still carrying the PREVIOUS anim's final value. The probe
            # detects anim_id changes faster than the time field updates.
            t0 = 0.0  # junk slot
            if i < 2 and seq > 0:
                # lagged: pretend last anim is still in time field
                t1 = 1.6  # fake "previous anim still showing"
                t2 = 1.65
                t3 = 18.0  # 0x2C also lags
            else:
                t1 = t  # play head
                t2 = t + 0.01  # play head with small offset
                t3 = total_dur
            yield (anim_id, t0, t1, t2, t3, ts)
            ts += SAMPLE_INTERVAL_MS
            seq += 1


def build_pts0_payload(seq, anim_id, t_vals, ts_ms):
    """Build a 132-byte PTS0 header matching probe_bin._parse_sample layout exactly.

    Layout (verified against probe_bin.py:222-247):
      u32 magic, u32 schema, u64 frame, u64 ts_ms_rel,
      u8 mode, u8 truncated, 6 bytes reserved,
      u64 wcm_ptr, u64 module_base, u64 player_chr_ins,
      u32 player_anim_id, 4xf32 player_anim_time, 3xf32 player_pos,
      u64 player_lock, 3xu64 boss_bars, u64 focused,
      u8 focus_reason, u8 enemy_count, u8 adaptive_step, u8 reserved2
    Total = 132 bytes.
    """
    p = bytearray()
    p += struct.pack("<I", probe_bin.PTS0_MAGIC)   # u32 magic
    p += struct.pack("<I", 1)                      # u32 schema
    p += struct.pack("<Q", seq)                    # u64 frame
    p += struct.pack("<Q", ts_ms)                  # u64 ts_ms_rel
    p += struct.pack("<B", 1)                      # u8 mode=smoke
    p += struct.pack("<B", 0)                      # u8 truncated
    p += b"\0" * 6                                 # reserved6
    p += struct.pack("<Q", 0x00007FF652655F88)     # wcm_ptr
    p += struct.pack("<Q", 0x00007FF64E8F0000)     # module_base
    p += struct.pack("<Q", 0x00007FF457599260)     # player_chr_ins
    p += struct.pack("<I", anim_id)                # player_anim_id
    p += struct.pack("<ffff", *t_vals)             # player_anim_time[4]
    p += struct.pack("<fff", 0.0, 0.0, 0.0)        # player_pos
    p += struct.pack("<Q", 0)                      # player_lock
    p += struct.pack("<QQQ", 0xFFFFFFFFFFFFFFFF,
                     0xFFFFFFFFFFFFFFFF,
                     0xFFFFFFFFFFFFFFFF)           # boss_bars[3]
    p += struct.pack("<Q", 0)                      # focused
    p += struct.pack("<B", 0)                      # focus_reason
    p += struct.pack("<B", 0)                      # enemy_count
    p += struct.pack("<B", 0)                      # adaptive_step
    p += struct.pack("<B", 0)                      # reserved2
    if len(p) != 132:
        raise AssertionError(f"PTS0 payload is {len(p)} bytes, expected 132")
    return bytes(p)


def write_synthetic_bin(path):
    """Write a synthetic .bin matching probe.cpp wire format."""
    samples = list(build_synthetic_samples())
    print(f"Building synthetic .bin with {len(samples)} samples across {len(ANIM_TRACE)} anims")

    with open(path, "wb") as f:
        # Manifest record first.
        manifest_text = (
            "manifest=start\nmode=1\ner_version=2.6.1.0\n"
            "roster_enabled=0\nbuild_hash=BUILD_test\n"
        )
        manifest_bytes = manifest_text.encode("utf-8")
        f.write(struct.pack("<II", probe_bin.MAN0_MAGIC, len(manifest_bytes)))
        f.write(manifest_bytes)

        # Sample records.
        for seq, (anim_id, t0, t1, t2, t3, ts) in enumerate(samples):
            payload = build_pts0_payload(seq, anim_id, (t0, t1, t2, t3), ts)
            # Wrap in SRD0
            f.write(struct.pack("<II", probe_bin.SRD0_MAGIC, len(payload)))
            f.write(payload)

    print(f"Wrote {os.path.getsize(path)} bytes to {path}")
    return path


def main():
    with tempfile.TemporaryDirectory() as tmp:
        base = os.path.join(tmp, "smoke-synthetic")
        bin_path = base + ".bin"
        write_synthetic_bin(bin_path)

        # Read back via probe_bin to confirm wire format integrity.
        samples = list(probe_bin.all_samples(base))
        print(f"Read back {len(samples)} samples")
        assert len(samples) >= 800, f"too few samples: {len(samples)}"

        # Run the analyzer.
        results = [calibrate_smoke.analyze_candidate(samples, i) for i in range(4)]

        print()
        report = calibrate_smoke.format_report(samples, results)
        print(report)

        # Assertions.
        failures = []

        # +0x20 (all zero) — should FAIL gate (no segments, no transitions
        # produce rewind because value never changes).
        if results[0].gate_pass():
            failures.append("+0x20 should FAIL gate (it's the always-zero junk slot)")
        if results[0].monotonic_segments != 0:
            failures.append(
                f"+0x20 should have 0 segments (always zero); got {results[0].monotonic_segments}"
            )

        # +0x24, +0x28 — should PASS gate. They accumulate within each anim,
        # reset on transition, with the 1-2 sample lag tolerated.
        for idx in (1, 2):
            if not results[idx].gate_pass():
                failures.append(
                    f"+0x{0x20 + 4*idx:02X} should PASS gate; got "
                    f"monotonic_segments={results[idx].monotonic_segments}, "
                    f"max_seg={results[idx].max_segment_dur:.2f}s, "
                    f"rewinds={results[idx].rewinds_on_anim_id_change}, "
                    f"in_range={results[idx].f32_in_range}"
                )

        # +0x2C — should FAIL gate. Within an anim, +0x2C stays constant
        # (so it has no monotonic growth segments). The synthetic fixture
        # models this as a constant per anim that jumps to a new constant
        # on anim_id change, so segments=0 -> FAIL.
        if results[3].gate_pass():
            failures.append(
                f"+0x2C should FAIL gate (constant-per-anim duration); "
                f"got monotonic_segments={results[3].monotonic_segments}"
            )

        if failures:
            print()
            print("=" * 72)
            print("TEST FAILURES:")
            print("=" * 72)
            for fail in failures:
                print(f"  - {fail}")
            return 1

        print()
        print("PASS — calibrate_smoke gate logic behaves correctly on synthetic data.")
        print("  +0x20 FAILed (always-zero junk).")
        print(f"  +0x24 PASSed (max_seg={results[1].max_segment_dur:.2f}s, "
              f"{results[1].monotonic_segments} segments).")
        print(f"  +0x28 PASSed (max_seg={results[2].max_segment_dur:.2f}s, "
              f"{results[2].monotonic_segments} segments).")
        print(f"  +0x2C FAILed (constant-per-anim duration slot, "
              f"{results[3].monotonic_segments} segments).")
        return 0


if __name__ == "__main__":
    sys.exit(main())
