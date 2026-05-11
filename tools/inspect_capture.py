#!/usr/bin/env python3
"""inspect_capture.py — diagnostic dump of a probe capture.

Whereas `qualify_oracle.py` produces a PASS/FAIL verdict, this script
produces a structured dump of what the probe actually saw, regardless of
whether the data was good enough to qualify. Use this when:

  - qualification FAILED and you want to know WHY (was it the roster?
    the join key? the anim_time? the database?)
  - you fought an enemy whose c-id isn't in the parry database yet
  - you want to confirm v6.1's roster fix actually worked

Usage:
    python tools/inspect_capture.py <base-path>

Output sections:
    1. Capture overview (samples, duration, mode, roster status)
    2. Focused-enemy summary (count, distinct chr_ins, distinct field values)
    3. Per-enemy detail: what fields are constant, what fields vary,
       what anim_ids appeared
    4. Anim-time monotonicity per candidate offset
    5. Database join attempt: does any constant field match a known c-id?
    6. Plain-English next steps based on what was found.
"""

from __future__ import annotations

import os
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

import probe_bin
import qualify_oracle


FIELD_NAMES = qualify_oracle.FIELD_NAMES
CANDIDATE_OFFSETS = (0x20, 0x24, 0x28, 0x2C)


def fmt_hex_or_dec(v: int) -> str:
    """Show small ints as decimal, large ints as hex too."""
    if v < 100000:
        return f"{v}"
    return f"{v} (0x{v:X})"


def section(title: str) -> None:
    print()
    print("=" * 72)
    print(title)
    print("=" * 72)


def inspect(base_path: str) -> int:
    samples = list(probe_bin.all_samples(base_path))
    if not samples:
        print(f"ERROR: no samples in {base_path}.bin (was F11 pressed?)",
              file=sys.stderr)
        return 1

    # ----- 1. Overview -----
    section("1. CAPTURE OVERVIEW")
    dur_ms = samples[-1].ts_ms_rel - samples[0].ts_ms_rel
    mode_label = {0: "smoke", 1: "smoke", 2: "qualification", 3: "discovery"}.get(
        samples[0].mode, f"unknown({samples[0].mode})"
    )
    print(f"  base path: {base_path}")
    print(f"  samples:   {len(samples)}")
    print(f"  duration:  {dur_ms / 1000.0:.1f}s")
    if dur_ms > 0:
        print(f"  rate:      {len(samples) / (dur_ms / 1000.0):.1f} Hz")
    print(f"  mode:      {mode_label}")

    # ----- 2. Enemy roster summary -----
    section("2. ENEMY ROSTER SUMMARY")
    enemy_rows_total = 0
    focused_rows = 0
    samples_with_any_enemy = 0
    samples_with_focused = 0
    distinct_chr_ins: set[int] = set()
    distinct_focused_chr_ins: set[int] = set()
    for s in samples:
        if s.enemies:
            samples_with_any_enemy += 1
        has_focused = False
        for e in s.enemies:
            enemy_rows_total += 1
            distinct_chr_ins.add(e.chr_ins_abs)
            if e.is_focused:
                focused_rows += 1
                distinct_focused_chr_ins.add(e.chr_ins_abs)
                has_focused = True
        if has_focused:
            samples_with_focused += 1

    print(f"  total enemy rows across all samples: {enemy_rows_total}")
    print(f"  samples with ANY enemy row:          {samples_with_any_enemy}")
    print(f"  samples with FOCUSED enemy row:      {samples_with_focused}")
    print(f"  distinct chr_ins (any role):         {len(distinct_chr_ins)}")
    print(f"  distinct chr_ins (focused only):     {len(distinct_focused_chr_ins)}")

    if enemy_rows_total == 0:
        print()
        print("  >>> ZERO enemy rows captured.")
        print("  This means the probe was in roster-disabled fallback AND no")
        print("  boss-bar enemy was visible to the player during the capture.")
        print("  v6.1 was supposed to fix this — check the .log.txt for")
        print("  'roster: ENABLED' messages; if absent, v6.1 didn't catch the")
        print("  init window either.")
        return 2

    if focused_rows == 0:
        print()
        print("  >>> Enemies captured, but NONE were focused (lock-on missed?).")
        print("  Make sure you LOCK ON to the enemy with the camera-stick press")
        print("  BEFORE pressing F11, and keep the lock-on for the whole fight.")
        return 2

    # ----- 3. Per-enemy detail -----
    section("3. PER-FOCUSED-ENEMY DETAIL")
    by_chr_ins: dict[int, list[probe_bin.EnemyRecord]] = defaultdict(list)
    for s in samples:
        for e in s.enemies:
            if e.is_focused:
                by_chr_ins[e.chr_ins_abs].append(e)

    for ci, rows in sorted(by_chr_ins.items(), key=lambda kv: -len(kv[1])):
        print(f"\n  chr_ins=0x{ci:X}  rows={len(rows)}")
        # For each tracked field, is it constant across all rows?
        for fname in FIELD_NAMES:
            vals = [getattr(r, fname) for r in rows]
            distinct = Counter(vals)
            if len(distinct) == 1:
                v = next(iter(distinct))
                print(f"    {fname:20} CONSTANT = {fmt_hex_or_dec(v)}")
            else:
                top = distinct.most_common(3)
                desc = ", ".join(f"{fmt_hex_or_dec(v)}x{c}" for v, c in top)
                print(f"    {fname:20} varies ({len(distinct)} distinct): {desc}")
        # Distinct anim_ids observed
        anim_ids = sorted({r.anim_id for r in rows})
        print(f"    anim_id              {len(anim_ids)} distinct: "
              f"{anim_ids[:8]}{'...' if len(anim_ids) > 8 else ''}")

    # ----- 4. Anim-time monotonicity per candidate -----
    section("4. ANIM-TIME CANDIDATE QUALITY (focused enemy, primary chr_ins)")
    main_ci = max(by_chr_ins.items(), key=lambda kv: len(kv[1]))[0]
    main_rows = by_chr_ins[main_ci]
    print(f"  Evaluating chr_ins=0x{main_ci:X} ({len(main_rows)} rows)")

    # Use qualify_oracle's gate-check logic but only print results, not pass/fail.
    verdict = qualify_oracle.find_anim_time_field(samples)
    print()
    print(f"  offset   monotonic   max_seg   in_range  rewinds   passed")
    print(f"  ------   ---------   -------   --------  -------   ------")
    print(f"  +0x{verdict.candidate_offset:02X}     {verdict.monotonic_segments:6d}      "
          f"{verdict.max_segment_dur:5.2f}s   "
          f"{'yes' if verdict.in_range else 'NO ':>5}      "
          f"{'yes' if verdict.rewinds_on_anim_id_change else 'NO ':>5}      "
          f"{'PASS' if verdict.passed else 'FAIL'}")
    print(f"  (only the best candidate is shown; see qualify_oracle.py for all 4)")

    # ----- 5. Database join attempt -----
    section("5. DATABASE JOIN ATTEMPT")
    try:
        char_db = qualify_oracle.load_database(qualify_oracle.DEFAULT_DATABASE_PATH)
    except FileNotFoundError as e:
        print(f"  ERROR: {e}")
        return 3
    print(f"  database loaded: {len(char_db)} characters with parry windows")

    # For each constant field in the main enemy, see if its value matches a c-id.
    print()
    print(f"  testing main enemy (chr_ins=0x{main_ci:X}):")
    join_candidates = []
    for fname in FIELD_NAMES:
        vals = {getattr(r, fname) for r in main_rows}
        if len(vals) != 1:
            continue
        v = next(iter(vals))
        in_db = v in char_db
        marker = "***MATCH" if in_db else "no match"
        print(f"    {fname:20} = {fmt_hex_or_dec(v):24} → {marker}")
        if in_db:
            cd = char_db[v]
            join_candidates.append((fname, v, cd))
            print(f"        cid={cd.cid}, {len(cd.parry_windows)} parry windows in DB")

    if not join_candidates:
        # Also check c-ids close to constant values (off-by-one, +/-10, etc.)
        print()
        print("  No exact match. Checking near-misses for each constant field:")
        for fname in FIELD_NAMES:
            vals = {getattr(r, fname) for r in main_rows}
            if len(vals) != 1:
                continue
            v = next(iter(vals))
            # Look in db for any c-id within ±20 of this value.
            near = sorted([(abs(cid - v), cid) for cid in char_db.keys() if abs(cid - v) <= 20])
            if near:
                print(f"    {fname} = {v}: nearest c-ids in DB: "
                      f"{[(d, char_db[cid].cid) for d, cid in near[:5]]}")

    # ----- 6. Next steps -----
    section("6. NEXT STEPS")
    if not join_candidates:
        print("  No constant field on the focused enemy matched a c-id in our")
        print("  parry database. Likely outcomes:")
        print("    A) the enemy you fought is a c-id we don't have parry data")
        print("       for (e.g. c4140 Godrick Soldier, c4180 Lordsworn — these")
        print("       have animations in the DB but zero parry windows extracted).")
        print("    B) the probe IS capturing data correctly but the join-key")
        print("       offset is on a different field than we tracked.")
        print()
        print("  Look at the constant-field values printed above. If one of them")
        print("  is in the 2000-7999 range, that's almost certainly the c-id —")
        print("  cross-check it against community wikis (e.g. soulsmodding wiki")
        print("  /Elden_Ring/Enemy_IDs) to identify the enemy type, then either:")
        print("    1. Re-fight a known-DB enemy (e.g. c2130 Banished Knight in")
        print("       Stormveil) so qualification can complete, OR")
        print("    2. Use this fight's data as 'mystery enemy' input to expand")
        print("       the parry-data extraction (Phase 3 work).")
    elif not verdict.passed:
        print(f"  Enemy identified as {join_candidates[0][2].cid}, but the")
        print(f"  anim_time field check FAILED:")
        print(f"    monotonic_segments={verdict.monotonic_segments} (need >=3)")
        print(f"    max_segment_dur={verdict.max_segment_dur:.2f}s (need >=0.30s)")
        print(f"    rewinds={verdict.rewinds_on_anim_id_change}")
        print(f"    in_range={verdict.in_range}")
        print(f"  This is a probe data-quality issue. Check that the enemy")
        print(f"  performed several attacks during capture (need varied anims).")
    else:
        cd = join_candidates[0][2]
        print(f"  Enemy identified as {cd.cid} ({len(cd.parry_windows)} parry windows in DB).")
        print(f"  anim_time field PASSED gate at +0x{verdict.candidate_offset:02X}.")
        print(f"  Run `python tools/qualify_oracle.py {base_path}` for the full")
        print(f"  predicted-vs-observed parry window match report.")

    return 0


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("Usage: inspect_capture.py <base-path>", file=sys.stderr)
        print("  e.g. inspect_capture.py /mnt/station-projects/elden-ring/logs/qualification-20260511-121252",
              file=sys.stderr)
        return 64
    return inspect(argv[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
