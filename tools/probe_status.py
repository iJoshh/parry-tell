"""probe_status.py — quick sanity check on a fresh probe capture.

Reads just the manifest + a few sample headers (no heavy parsing) and
reports whether the capture is alive, what mode, drop counters, etc.

Useful BEFORE running qualify_oracle.py / analyze_discovery.py to confirm
the .bin isn't empty/corrupt.

Usage: python tools/probe_status.py <bin_or_base_path>
"""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import probe_bin


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: probe_status.py <bin_or_base_path>", file=sys.stderr)
        return 2
    path = argv[1]
    base = path[:-4] if path.endswith(".bin") else path

    total_samples = 0
    total_records = 0
    total_bytes = 0
    manifests = []
    parse_errors = []
    files_seen = []
    first_sample = None
    last_sample = None

    for bf in probe_bin.read_session(base):
        files_seen.append((bf.path, bf.raw_bytes, bf.record_count))
        total_records += bf.record_count
        total_bytes += bf.raw_bytes
        total_samples += len(bf.samples)
        manifests.extend(bf.manifests)
        parse_errors.extend(bf.parse_errors)
        if bf.samples:
            if first_sample is None:
                first_sample = bf.samples[0]
            last_sample = bf.samples[-1]

    # Top-line verdict — quick visual signal before the detail dump.
    if not files_seen:
        print(f"VERDICT: NO CAPTURE — no files found for base path {base}")
        print(f"  Looking for: {base}.bin (and rotations .bin.001, .bin.002, ...)")
        print(f"  If the game ran but no files appeared, the DLL didn't load or sig-scan failed.")
        print(f"  Check parry-tell-probe.boot.log next to the DLL in Game\\mods\\.")
        return 1

    if total_samples == 0:
        print(f"VERDICT: CAPTURE EMPTY — files exist but no samples were emitted")
        print(f"  Likely cause: F11 was never pressed (probe stays in disarmed state by default).")
        print(f"  Check the .log.txt for sig-scan failures or roster-validation warnings.")
    elif total_samples < 50:
        print(f"VERDICT: VERY SHORT CAPTURE — only {total_samples} samples")
        print(f"  Was F11 pressed for less than ~1 second?")
    else:
        print(f"VERDICT: CAPTURE LIVE — {total_samples} samples emitted")

    print()
    print(f"=== probe capture: {os.path.basename(base)} ===")
    print()
    for fpath, sz, recs in files_seen:
        print(f"  {os.path.basename(fpath)}: {sz/1024/1024:.1f} MB, {recs} records")
    print()
    print(f"Total: {total_records} records, {total_samples} samples, "
          f"{total_bytes/1024/1024:.1f} MB on disk")

    if parse_errors:
        print()
        print(f"PARSE ERRORS: {len(parse_errors)}")
        for e in parse_errors[:5]:
            print(f"  {e}")

    if manifests:
        print()
        print(f"Manifests: {len(manifests)}")
        # Show first (start) + last (end) manifest fields.
        if manifests:
            m_start = manifests[0]
            print(f"  start: mode={m_start.fields.get('mode')} "
                  f"er_version={m_start.fields.get('er_file_version')} "
                  f"roster_enabled={m_start.fields.get('roster_enabled')} "
                  f"build_hash={m_start.fields.get('build_hash')}")
        if len(manifests) > 1:
            m_end = manifests[-1]
            for k in ("session_end", "ticks", "samples_emitted",
                      "drops_no_buffer", "drops_budget_skip", "drops_producer_emerg",
                      "truncated_samples", "final_adaptive_step", "roster_check7_runtime"):
                v = m_end.fields.get(k)
                if v is not None:
                    print(f"  end.{k} = {v}")

    if first_sample and last_sample:
        duration = (last_sample.ts_ms_rel - first_sample.ts_ms_rel) / 1000.0
        print()
        print(f"Sample timeline:")
        print(f"  first: frame={first_sample.frame} ts={first_sample.ts_ms_rel}ms")
        print(f"  last:  frame={last_sample.frame} ts={last_sample.ts_ms_rel}ms")
        print(f"  duration: {duration:.1f}s")
        print(f"  effective sample rate: {total_samples/duration:.1f} Hz" if duration > 0 else "")

        # First sample state
        s = first_sample
        mode_name = probe_bin.CAPTURE_MODE_NAMES.get(s.mode, str(s.mode))
        print()
        print(f"First sample state:")
        print(f"  mode={mode_name}  truncated={s.truncated}")
        print(f"  player_chr_ins=0x{s.player_chr_ins:016X}")
        print(f"  player_anim_id={s.player_anim_id}  player_pos={s.player_pos}")
        print(f"  player_lock=0x{s.player_lock_on_target_handle:016X}")
        print(f"  boss_bars=[0x{s.boss_bar_handles[0]:016X}, "
              f"0x{s.boss_bar_handles[1]:016X}, 0x{s.boss_bar_handles[2]:016X}]")
        print(f"  focused=0x{s.focused_enemy_handle:016X} "
              f"reason={probe_bin.FOCUS_REASON_NAMES.get(s.focus_reason, s.focus_reason)}")
        print(f"  enemy_count={s.enemy_record_count} adaptive_step={s.adaptive_step}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
