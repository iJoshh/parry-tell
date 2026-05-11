"""qualify_oracle.py — post-qualification analysis for parry-tell-probe v6.

Reads the .bin/.csv produced by a `mode = qualification` capture, joins
enemy fields against `data/parry_data.json`, and proves three things:

  1. Some `field_at_0xNN` value reliably maps to a single `cXXXX` character
     ID across the whole session for the focused enemy.
  2. One of the four `enemy_anim_time_candidates` is monotonic during
     animations and rewinds when anim_id changes (already validated in
     smoke mode; cross-checked here on enemy data).
  3. Predicted parry-window timestamps from the database line up with
     observed `enemy_anim_time` for the focused enemy within tolerance
     (±1 focused-sample period — at 90 Hz, ±11 ms).

If all three pass: prints `QUALIFICATION REPORT ... Verdict: PASSED` and
emits a JSON report next to the .bin file.

If any fail: prints `Verdict: FAILED` with diagnostic, exit code 1.

Usage:
    python tools/qualify_oracle.py <bin_base_path>
    # bin_base_path is the path WITHOUT the .bin suffix, e.g.
    # /mnt/station-projects/elden-ring/logs/qualification-20260508-145300

Requires: data/parry_data.json (the TAE database).
"""

from __future__ import annotations

import json
import os
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Optional

# Make sibling tools importable when run from anywhere.
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import probe_bin  # noqa: E402

REPO_ROOT = os.path.dirname(_HERE)
DEFAULT_DATABASE_PATH = os.path.join(REPO_ROOT, "data", "parry_data.json")


# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------


@dataclass
class ParryWindow:
    anim_id_full: int        # e.g. 41021 from a000_041021, OR 2004050 from a002_004050
    anim_id_short: int       # YYYYYY portion only
    aid_str: str             # original "a000_041021" key from JSON
    window_start_s: float
    window_end_s: float


@dataclass
class CharData:
    cid: str                 # e.g. "c4380"
    parry_windows: list[ParryWindow] = field(default_factory=list)
    anim_ids_full: set[int] = field(default_factory=set)
    anim_ids_short: set[int] = field(default_factory=set)


def _parse_aid_str(aid: str) -> Optional[tuple[int, int]]:
    """Parse 'aXXX_YYYYYY' → (full_int, short_int).

    full_int is XXXYYYYYY (concatenated decimal); short_int is YYYYYY only.
    Returns None on parse failure.
    """
    if not aid.startswith("a"):
        return None
    body = aid[1:]
    if "_" not in body:
        return None
    try:
        prefix_str, suffix_str = body.split("_", 1)
        prefix = int(prefix_str)
        suffix = int(suffix_str)
    except ValueError:
        return None
    # Build full as decimal concatenation: prefix * 1_000_000 + suffix.
    # (Suffix is always 6 digits in TAE filenames.)
    full = prefix * 1_000_000 + suffix
    return (full, suffix)


def load_database(path: str) -> dict[int, CharData]:
    """Returns {numeric_cXXXX: CharData}.

    parry_data.json schema (verified empirically):
        characters[cid].animations[aid_str].parry_windows = [
            {start_time, end_time, frame_30, frame_60}, ...
        ]
    """
    with open(path) as fh:
        db = json.load(fh)
    chars: dict[int, CharData] = {}
    for cid, cdata in db.get("characters", {}).items():
        if not cid.startswith("c"):
            continue
        try:
            num = int(cid[1:])
        except ValueError:
            continue
        cd = CharData(cid=cid)
        for aid_str, anim_data in cdata.get("animations", {}).items():
            pw_rows = anim_data.get("parry_windows") or []
            if not pw_rows:
                continue
            parsed = _parse_aid_str(aid_str)
            if parsed is None:
                continue
            full_id, short_id = parsed
            cd.anim_ids_full.add(full_id)
            cd.anim_ids_short.add(short_id)
            for w in pw_rows:
                try:
                    cd.parry_windows.append(
                        ParryWindow(
                            anim_id_full=full_id,
                            anim_id_short=short_id,
                            aid_str=aid_str,
                            window_start_s=float(w["start_time"]),
                            window_end_s=float(w["end_time"]),
                        )
                    )
                except (KeyError, TypeError, ValueError):
                    continue
        if cd.parry_windows:
            chars[num] = cd
    return chars


# ---------------------------------------------------------------------------
# Step 1: identify which field_at_0xNN is the cXXXX join key
# ---------------------------------------------------------------------------


FIELD_NAMES = (
    "field_at_0x038",
    "field_at_0x060",
    "field_at_0x064",
    "field_at_0x068",
    "field_at_0x06C",
    "field_at_0x080",
    "field_at_0x1E8",
)


@dataclass
class JoinKeyVerdict:
    field_name: str
    constant_value: int
    matched_cid: str
    rows_observed: int
    distinct_values: int


def find_join_key(
    samples: list[probe_bin.Sample],
    char_db: dict[int, CharData],
) -> Optional[JoinKeyVerdict]:
    """Find a field that's CONSTANT across all rows for the focused enemy
    AND whose value matches a cXXXX in the database.

    Returns None if no field qualifies.
    """
    # Collect focused-enemy rows by (chr_ins, handle) so we can spot when the
    # game spawns a new enemy mid-session (different chr_ins).
    by_enemy: dict[int, list[probe_bin.EnemyRecord]] = defaultdict(list)
    for s in samples:
        for e in s.enemies:
            if e.is_focused:
                # Use chr_ins as the enemy identity (handle changes after
                # respawn, but we track per-instance).
                by_enemy[e.chr_ins_abs].append(e)

    if not by_enemy:
        return None

    # The qualification protocol fights ONE enemy. If multiple chr_ins keys
    # appear, take the most-observed (e.g. respawns; we want the dominant
    # encounter).
    main_chr, main_rows = max(by_enemy.items(), key=lambda kv: len(kv[1]))
    if len(main_rows) < 30:  # need at least ~0.3s of data
        return None

    candidates: list[JoinKeyVerdict] = []
    for fname in FIELD_NAMES:
        values = [getattr(r, fname) for r in main_rows]
        distinct = set(values)
        if len(distinct) != 1:
            continue
        v = next(iter(distinct))
        if v in char_db:
            candidates.append(
                JoinKeyVerdict(
                    field_name=fname,
                    constant_value=v,
                    matched_cid=char_db[v].cid,
                    rows_observed=len(main_rows),
                    distinct_values=len(distinct),
                )
            )

    if not candidates:
        return None
    # Prefer the EARLIEST field offset (most likely to be the canonical
    # character ID slot per the offset research). Stable tie-break.
    candidates.sort(key=lambda c: FIELD_NAMES.index(c.field_name))
    return candidates[0]


# ---------------------------------------------------------------------------
# Step 2: animation-time monotonicity on the focused enemy
# ---------------------------------------------------------------------------


@dataclass
class AnimTimeVerdict:
    candidate_index: int  # 0..3 → +0x20, +0x24, +0x28, +0x2C
    candidate_offset: int
    monotonic_segments: int
    max_segment_dur: float
    rewinds_on_anim_id_change: bool
    in_range: bool
    passed: bool


CANDIDATE_OFFSETS = (0x20, 0x24, 0x28, 0x2C)


def find_anim_time_field(samples: list[probe_bin.Sample]) -> AnimTimeVerdict:
    """Pick the best of the 4 enemy_anim_time_candidates per the spec gate:
        f32 finite + 0..600 + monotonic_segments >= 3 + max_segment_dur >= 0.3s
        + rewinds_on_anim_id_change.

    Anim-transition lag tolerance: the probe samples at ~91 Hz and races the
    game's animation state machine. When the game emits a new anim_id, the
    anim_time field takes several samples to catch up — measured against the
    2026-05-09 smoke capture (player-side, 38 transitions): lag ranges from
    1 to 10 samples (median 4, P90 9). The rewind check therefore looks at
    the value ANIM_TRANSITION_LAG_SAMPLES samples AFTER the transition,
    not on the transition sample itself. A 12-sample window covers the full
    observed distribution with margin.

    Whole-anim peak baseline: the rewind comparison uses prev_anim_max
    (peak value across the entire prior anim) rather than prev_val (last
    sample's value). Looping anims and multi-segment tracks produce
    within-anim micro-rewinds; if the rewind check compared against the
    last value seen in the prior anim, those micro-rewinds would corrupt
    the baseline and produce false-positive non-rewind failures at
    transition. Discovered analyzing smoke-20260509-170547 on +0x24:
    2/38 transitions were false-positives until this fix.
    """
    ANIM_TRANSITION_LAG_SAMPLES = 12  # check rewind 12 samples after transition

    # Per-candidate state
    @dataclass
    class _S:
        in_range: bool = True
        rewinds: bool = False
        seg_count: int = 0
        max_dur: float = 0.0
        have_prev: bool = False
        prev_val: float = 0.0
        prev_anim: int = 0
        seg_start: float = 0.0
        seg_n: int = 0
        # Peak value across the entire current anim — does NOT reset on
        # within-anim micro-rewinds. Used as the baseline for the
        # transition rewind check.
        cur_anim_max: float = 0.0
        # Lag tolerance: when a transition is observed, record the prior
        # anim's whole-anim peak, then check for rewind N samples later.
        pending_rewind_check: bool = False
        pending_rewind_old_max: float = 0.0
        pending_rewind_countdown: int = 0

    state = [_S() for _ in range(4)]

    # We track on the FOCUSED enemy only.
    for s in samples:
        for e in s.enemies:
            if not e.is_focused:
                continue
            for i, val in enumerate(e.anim_time):
                st = state[i]
                # Range check.
                if not (val == val and 0.0 <= val <= 600.0):
                    st.in_range = False
                if not st.have_prev:
                    st.have_prev = True
                    st.prev_val = val
                    st.prev_anim = e.anim_id
                    st.seg_start = val
                    st.seg_n = 1
                    st.cur_anim_max = val
                    continue
                # If a pending rewind check is armed, decrement and evaluate
                # when the countdown reaches zero. We compare the current
                # value against the OLD anim's whole-anim peak (captured at
                # the transition). This is the lag-tolerant rewind detection.
                if st.pending_rewind_check:
                    st.pending_rewind_countdown -= 1
                    if st.pending_rewind_countdown <= 0:
                        if val < st.pending_rewind_old_max:
                            st.rewinds = True
                        st.pending_rewind_check = False
                if e.anim_id != st.prev_anim:
                    if st.seg_n >= 3:
                        dur = st.prev_val - st.seg_start
                        if dur > st.max_dur:
                            st.max_dur = dur
                        st.seg_count += 1
                    # Arm the lag-tolerant rewind check. Capture the old
                    # anim's whole-anim peak; we'll compare the candidate
                    # field N samples later.
                    st.pending_rewind_check = True
                    st.pending_rewind_old_max = st.cur_anim_max
                    st.pending_rewind_countdown = ANIM_TRANSITION_LAG_SAMPLES
                    st.seg_start = val
                    st.seg_n = 1
                    st.prev_val = val
                    st.prev_anim = e.anim_id
                    st.cur_anim_max = val
                    continue
                # Same anim_id.
                if val > st.cur_anim_max:
                    st.cur_anim_max = val
                if val + 1e-6 >= st.prev_val:
                    st.seg_n += 1
                    st.prev_val = val
                else:
                    if st.seg_n >= 3:
                        dur = st.prev_val - st.seg_start
                        if dur > st.max_dur:
                            st.max_dur = dur
                        st.seg_count += 1
                    st.seg_start = val
                    st.seg_n = 1
                    st.prev_val = val

    # Finalize.
    verdicts = []
    for i, st in enumerate(state):
        if st.seg_n >= 3:
            dur = st.prev_val - st.seg_start
            if dur > st.max_dur:
                st.max_dur = dur
            st.seg_count += 1
        passed = (
            st.in_range
            and st.seg_count >= 3
            and st.max_dur >= 0.3
            and st.rewinds
        )
        verdicts.append(
            AnimTimeVerdict(
                candidate_index=i,
                candidate_offset=CANDIDATE_OFFSETS[i],
                monotonic_segments=st.seg_count,
                max_segment_dur=st.max_dur,
                rewinds_on_anim_id_change=st.rewinds,
                in_range=st.in_range,
                passed=passed,
            )
        )
    # Winner: passing candidate with highest max_segment_dur.
    passing = [v for v in verdicts if v.passed]
    if passing:
        return max(passing, key=lambda v: v.max_segment_dur)
    # Else: best in-range candidate by max_dur (so the report can still tell
    # us how close we got).
    return max(verdicts, key=lambda v: (v.in_range, v.max_segment_dur))


# ---------------------------------------------------------------------------
# Step 3: predicted-vs-observed parry windows
# ---------------------------------------------------------------------------


@dataclass
class WindowMatch:
    anim_id: int
    sample_count_in_anim: int
    db_window_start_s: float
    db_window_end_s: float
    observed_anim_time_at_window_open: Optional[float]


@dataclass
class WindowMatchVerdict:
    cid: str
    db_windows_for_observed_anims: int
    db_windows_total: int
    matched_within_tolerance: int
    misses: int
    not_found_in_db: int
    tolerance_ms: float


@dataclass
class AnimIdEncodingVerdict:
    encoding: str           # "full" or "short"
    matches: int            # how many DB anim_ids had any sample with that ID
    total_db: int


def determine_anim_id_encoding(
    samples: list[probe_bin.Sample],
    char: CharData,
) -> AnimIdEncodingVerdict:
    """Decide whether the runtime anim_id is the full XXXYYYYYY form or just YYYYYY."""
    # Collect distinct anim_ids observed on the focused enemy.
    observed: set[int] = set()
    for s in samples:
        for e in s.enemies:
            if e.is_focused:
                observed.add(e.anim_id)
    full_hits = len(observed & char.anim_ids_full)
    short_hits = len(observed & char.anim_ids_short)
    if full_hits >= short_hits:
        return AnimIdEncodingVerdict("full", full_hits, len(char.anim_ids_full))
    return AnimIdEncodingVerdict("short", short_hits, len(char.anim_ids_short))


def check_predicted_windows(
    samples: list[probe_bin.Sample],
    char: CharData,
    anim_time_idx: int,
    encoding: str,
    tolerance_ms: float = 11.0,
) -> WindowMatchVerdict:
    """For each (anim_id, window_start_s) in the database, find the
    observed sample for the focused enemy where the animation was active
    and `anim_time` was closest to window_start_s. If the observed
    anim_time is within ±tolerance of the db value, count as match.
    """
    # Build per-(anim_id) timeseries of (ts_ms_rel, anim_time) for focused.
    # Use whichever encoding `determine_anim_id_encoding` chose.
    series: dict[int, list[tuple[int, float]]] = defaultdict(list)
    for s in samples:
        for e in s.enemies:
            if not e.is_focused:
                continue
            series[e.anim_id].append((s.ts_ms_rel, e.anim_time[anim_time_idx]))

    matched = 0
    misses = 0
    not_in_db = 0
    db_windows_for_observed = 0
    for w in char.parry_windows:
        anim_id = w.anim_id_full if encoding == "full" else w.anim_id_short
        start = w.window_start_s
        if anim_id not in series or not series[anim_id]:
            not_in_db += 1
            continue
        db_windows_for_observed += 1
        # Find the sample whose anim_time is closest to `start`.
        ts_at_open = min(series[anim_id], key=lambda row: abs(row[1] - start))
        diff_ms = abs(ts_at_open[1] - start) * 1000.0
        if diff_ms <= tolerance_ms:
            matched += 1
        else:
            misses += 1

    return WindowMatchVerdict(
        cid=char.cid,
        db_windows_for_observed_anims=db_windows_for_observed,
        db_windows_total=len(char.parry_windows),
        matched_within_tolerance=matched,
        misses=misses,
        not_found_in_db=not_in_db,
        tolerance_ms=tolerance_ms,
    )


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def run_qualification(
    base_path: str,
    database_path: str = DEFAULT_DATABASE_PATH,
    tolerance_ms: float = 11.0,
) -> dict:
    samples = list(probe_bin.all_samples(base_path))
    if not samples:
        return {"verdict": "FAILED", "reason": "no samples found in capture"}

    # Mode check.
    first_mode = samples[0].mode
    if first_mode != 2:  # qualification
        return {
            "verdict": "FAILED",
            "reason": f"capture is mode={first_mode}, not qualification (=2)",
        }

    char_db = load_database(database_path)
    if not char_db:
        return {"verdict": "FAILED", "reason": "database empty / unparseable"}

    # Step 1: join key.
    join_key = find_join_key(samples, char_db)
    if join_key is None:
        return {
            "verdict": "FAILED",
            "reason": "no field_at_0xNN was constant across focused-enemy rows "
                      "AND matched a cXXXX in the database",
            "samples": len(samples),
        }

    # Step 2: anim time.
    anim_time = find_anim_time_field(samples)
    if not anim_time.passed:
        return {
            "verdict": "FAILED",
            "reason": "no enemy_anim_time candidate passed the spec gate "
                      "(monotonic + rewind + max_dur >= 0.3s + in_range)",
            "join_key": join_key.__dict__,
            "anim_time_best": anim_time.__dict__,
        }

    # Step 3: predicted-vs-observed.
    char = char_db[join_key.constant_value]
    encoding = determine_anim_id_encoding(samples, char)
    window_check = check_predicted_windows(
        samples, char, anim_time.candidate_index, encoding.encoding, tolerance_ms
    )

    # Pass threshold: at least 60% of observed-anim windows match within tolerance,
    # AND at least 3 windows actually got observed (so we have a real signal).
    if window_check.db_windows_for_observed_anims < 3:
        verdict = "FAILED"
        reason = (
            f"only {window_check.db_windows_for_observed_anims} db parry "
            f"windows had matching anim_ids in capture (need >=3). Did the "
            f"enemy actually attack? Re-run with longer combat."
        )
    elif window_check.matched_within_tolerance / max(1, window_check.db_windows_for_observed_anims) >= 0.60:
        verdict = "PASSED"
        reason = "window predictions match observations within tolerance"
    else:
        verdict = "FAILED"
        reason = (
            f"only {window_check.matched_within_tolerance}/"
            f"{window_check.db_windows_for_observed_anims} predicted windows "
            f"matched observations within ±{tolerance_ms:.1f} ms. Either the "
            f"join key is wrong, the anim_time field is wrong, or the database "
            f"is wrong for cid={char.cid}."
        )

    return {
        "verdict": verdict,
        "reason": reason,
        "samples": len(samples),
        "join_key": join_key.__dict__,
        "anim_time": anim_time.__dict__,
        "anim_id_encoding": encoding.__dict__,
        "window_check": window_check.__dict__,
    }


def format_report(result: dict, base_path: str) -> str:
    lines = []
    lines.append(f"QUALIFICATION REPORT — {os.path.basename(base_path)}")
    lines.append("")
    lines.append(f"Samples parsed: {result.get('samples', 0)}")
    if "join_key" in result and result["join_key"]:
        jk = result["join_key"]
        lines.append("")
        lines.append(f"Join key: {jk['field_name']} (constant value {jk['constant_value']} "
                     f"over {jk['rows_observed']} focused-enemy rows)")
        lines.append(f"Identified character: {jk['matched_cid']}")
    if "anim_time" in result and result["anim_time"]:
        at = result["anim_time"]
        lines.append("")
        lines.append(f"Anim time field: TimeAct + 0x{at['candidate_offset']:02X} "
                     f"(monotonic_segments={at['monotonic_segments']} "
                     f"max_segment_dur={at['max_segment_dur']:.2f}s "
                     f"rewinds={at['rewinds_on_anim_id_change']} "
                     f"in_range={at['in_range']} passed={at['passed']})")
    if "anim_id_encoding" in result and result["anim_id_encoding"]:
        enc = result["anim_id_encoding"]
        lines.append("")
        lines.append(f"Anim ID encoding: {enc['encoding']} "
                     f"(matched {enc['matches']}/{enc['total_db']} "
                     f"db anim_ids in capture)")
    if "window_check" in result and result["window_check"]:
        wc = result["window_check"]
        lines.append("")
        lines.append(f"DB parry windows for {wc['cid']}: {wc['db_windows_total']}")
        lines.append(f"Windows whose anim_id appeared in capture: {wc['db_windows_for_observed_anims']}")
        lines.append(f"Within ±{wc['tolerance_ms']:.1f}ms: {wc['matched_within_tolerance']}")
        lines.append(f"Misses (anim seen but window time off): {wc['misses']}")
        lines.append(f"Anim_ids in DB never seen in capture: {wc['not_found_in_db']}")
    lines.append("")
    lines.append(f"Verdict: {result['verdict']}")
    if "reason" in result:
        lines.append(f"  {result['reason']}")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: qualify_oracle.py <bin_base_path> [--tolerance-ms 11]",
              file=sys.stderr)
        print("       (bin_base_path is the path WITHOUT the .bin suffix)",
              file=sys.stderr)
        return 2
    base = argv[1]
    if base.endswith(".bin"):
        base = base[:-4]
    tolerance_ms = 11.0
    db = DEFAULT_DATABASE_PATH
    i = 2
    while i < len(argv):
        if argv[i] == "--tolerance-ms" and i + 1 < len(argv):
            tolerance_ms = float(argv[i + 1])
            i += 2
        elif argv[i] == "--database" and i + 1 < len(argv):
            db = argv[i + 1]
            i += 2
        else:
            print(f"unknown arg: {argv[i]}", file=sys.stderr)
            return 2

    result = run_qualification(base, database_path=db, tolerance_ms=tolerance_ms)
    print(format_report(result, base))

    # Also write JSON report for downstream consumers. The capture path
    # is often on a read-only SMB share (/mnt/station-projects is RO from
    # this VM), so fall back to <repo>/data/qualification-reports/ when
    # the .bin dir is unwritable.
    out = base + ".qualification.json"
    try:
        with open(out, "w") as fh:
            json.dump(result, fh, indent=2, default=str)
    except OSError as e:
        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        fallback_dir = os.path.join(repo_root, "data", "qualification-reports")
        os.makedirs(fallback_dir, exist_ok=True)
        fallback_out = os.path.join(fallback_dir, os.path.basename(out))
        with open(fallback_out, "w") as fh:
            json.dump(result, fh, indent=2, default=str)
        print(f"(write to {out} failed: {e}; wrote to {fallback_out} instead)",
              file=sys.stderr)
        out = fallback_out
    print(f"\nReport JSON: {out}")

    return 0 if result["verdict"] == "PASSED" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
