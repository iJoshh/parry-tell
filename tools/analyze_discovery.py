"""analyze_discovery.py — post-discovery analysis for parry-tell-probe v6.

Reads the .bin from a `mode = discovery` session and tries to identify the
runtime parry-active flag (or a hyperarmor flag) by correlating per-region
memory state against database-predicted parry windows.

This is a SCAFFOLDING tool — the discovery itself is iterative work that
follows real data. v1 of this script provides:

  1. Sanity report: sample counts, mode, manifest, drop counters, time span
  2. Per-region byte-change frequency: which offsets in each region change
     during a parry window vs outside it
  3. Top candidates ranked by (in-window mutation rate / out-of-window rate)
  4. JSON dump of per-(region_id, payload_offset) statistics for follow-up

The output is meant to be read by Claude (Mae) and iterated on. The actual
parry-active flag will reveal itself as a byte at some (region_id, offset)
that flips on at a window-start time and flips off at window-end.

Usage:
    python tools/analyze_discovery.py <bin_base_path>
    python tools/analyze_discovery.py <bin_base_path> --cid c4380

If --cid is omitted, the script uses the qualification report's identified
cid (looking for `<base>.qualification.json` next to the .bin).
"""

from __future__ import annotations

import json
import os
import sys
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from typing import Optional

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import probe_bin  # noqa: E402
import qualify_oracle  # noqa: E402

REPO_ROOT = os.path.dirname(_HERE)


# ---------------------------------------------------------------------------
# Sanity / overview
# ---------------------------------------------------------------------------


@dataclass
class SessionOverview:
    sample_count: int
    duration_seconds: float
    mode: int
    truncated_count: int
    manifest_fields: dict[str, str] = field(default_factory=dict)
    drop_counters: dict[str, int] = field(default_factory=dict)
    distinct_focused_handles: int = 0
    distinct_anim_ids: int = 0
    distinct_field_values: dict[str, int] = field(default_factory=dict)


def session_overview(samples: list[probe_bin.Sample],
                     manifests: list[probe_bin.Manifest]) -> SessionOverview:
    if not samples:
        return SessionOverview(sample_count=0, duration_seconds=0.0, mode=-1,
                               truncated_count=0)
    first = samples[0]
    last = samples[-1]
    duration = (last.ts_ms_rel - first.ts_ms_rel) / 1000.0
    truncated = sum(1 for s in samples if s.truncated)
    handles: set[int] = set()
    anim_ids: set[int] = set()
    field_values: dict[str, set[int]] = {fname: set() for fname in qualify_oracle.FIELD_NAMES}
    for s in samples:
        if s.focused_enemy_handle and s.focused_enemy_handle != 0xFFFFFFFFFFFFFFFF:
            handles.add(s.focused_enemy_handle)
        for e in s.enemies:
            anim_ids.add(e.anim_id)
            for fname in qualify_oracle.FIELD_NAMES:
                field_values[fname].add(getattr(e, fname))

    overview = SessionOverview(
        sample_count=len(samples),
        duration_seconds=duration,
        mode=first.mode,
        truncated_count=truncated,
        distinct_focused_handles=len(handles),
        distinct_anim_ids=len(anim_ids),
        distinct_field_values={k: len(v) for k, v in field_values.items()},
    )

    # Pull manifest fields. If multiple manifests (session-start + session-end),
    # merge with end winning.
    for m in manifests:
        for k, v in m.fields.items():
            if k == "__config_dump__":
                continue
            overview.manifest_fields[k] = v

    # Drop counters from session-end manifest if present.
    for k in ("ticks", "samples_emitted", "drops_no_buffer", "drops_budget_skip",
              "drops_producer_emerg", "truncated_samples", "final_adaptive_step",
              "roster_check7_runtime"):
        if k in overview.manifest_fields:
            try:
                overview.drop_counters[k] = int(overview.manifest_fields[k])
            except ValueError:
                pass

    return overview


# ---------------------------------------------------------------------------
# Build per-(region_id, offset) byte timeseries for the focused enemy
# ---------------------------------------------------------------------------


@dataclass
class ByteChangeStats:
    region_id: int
    payload_offset: int
    distinct_values: int = 0
    transition_count: int = 0          # how many times this byte changed value
    in_window_changes: int = 0         # transitions inside a parry window
    out_window_changes: int = 0        # transitions outside any parry window
    in_window_samples: int = 0
    out_window_samples: int = 0


def build_byte_stats(
    samples: list[probe_bin.Sample],
    parry_windows_by_anim: dict[int, list[tuple[float, float]]],
    anim_time_idx: int,
) -> dict[tuple[int, int], ByteChangeStats]:
    """For every byte in every captured Tier 3 region (focused enemy only),
    compute how often the byte changes inside a parry window vs outside.

    Memory cost: 1 entry per unique (region_id, payload_offset) byte. With
    ~17 KB/sample of regions, this is up to ~17,000 entries. Linear in
    sample count.
    """
    stats: dict[tuple[int, int], ByteChangeStats] = {}
    last_value: dict[tuple[int, int], int] = {}

    for s in samples:
        focused = next((e for e in s.enemies if e.is_focused), None)
        if focused is None:
            continue
        anim_id = focused.anim_id
        anim_time = focused.anim_time[anim_time_idx]
        in_window = False
        for w_start, w_end in parry_windows_by_anim.get(anim_id, []):
            if w_start <= anim_time <= w_end:
                in_window = True
                break

        for region in focused.regions:
            base_off = region.payload_offset
            for i, b in enumerate(region.payload):
                key = (region.region_id, base_off + i)
                st = stats.get(key)
                if st is None:
                    st = ByteChangeStats(region_id=region.region_id,
                                         payload_offset=base_off + i)
                    stats[key] = st
                if in_window:
                    st.in_window_samples += 1
                else:
                    st.out_window_samples += 1

                prev = last_value.get(key)
                if prev is not None and prev != b:
                    st.transition_count += 1
                    if in_window:
                        st.in_window_changes += 1
                    else:
                        st.out_window_changes += 1
                last_value[key] = b

    # Compute distinct_values lazily — do a second pass per byte to count
    # values seen. Cheap because we already have the sample list.
    seen: dict[tuple[int, int], set[int]] = defaultdict(set)
    for s in samples:
        focused = next((e for e in s.enemies if e.is_focused), None)
        if focused is None:
            continue
        for region in focused.regions:
            base_off = region.payload_offset
            for i, b in enumerate(region.payload):
                seen[(region.region_id, base_off + i)].add(b)

    for key, st in stats.items():
        st.distinct_values = len(seen.get(key, set()))

    return stats


# ---------------------------------------------------------------------------
# Rank candidates
# ---------------------------------------------------------------------------


def rank_candidates(stats: dict[tuple[int, int], ByteChangeStats],
                    min_in_window_changes: int = 5,
                    top_n: int = 50) -> list[ByteChangeStats]:
    """Score each byte by in-window mutation rate / out-window rate.

    A byte that flips ON at every window start and OFF at every window end
    will have a very high in-window change rate vs out-of-window rate. We
    require min_in_window_changes to filter out single-fluke transitions.
    """
    scored: list[tuple[float, ByteChangeStats]] = []
    for st in stats.values():
        if st.in_window_changes < min_in_window_changes:
            continue
        if st.in_window_samples == 0:
            continue
        in_rate = st.in_window_changes / st.in_window_samples
        out_rate = (st.out_window_changes / st.out_window_samples
                    if st.out_window_samples > 0 else 0.0)
        # Score: in_rate is what we want to be high; (in_rate - out_rate) is
        # the discrimination. A flag that changes ONLY at window boundaries
        # has very high in_rate, very low out_rate. Avoid div-by-zero by
        # adding a small epsilon.
        ratio = in_rate / (out_rate + 1e-6)
        score = (in_rate - out_rate) * (1.0 + min(ratio / 10.0, 5.0))
        scored.append((score, st))
    scored.sort(key=lambda kv: -kv[0])
    return [st for _, st in scored[:top_n]]


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def discover(
    base_path: str,
    cid: Optional[str] = None,
    anim_time_idx: Optional[int] = None,
    encoding: Optional[str] = None,
) -> dict:
    # Load all samples + manifests across rotations.
    all_samples: list[probe_bin.Sample] = []
    all_manifests: list[probe_bin.Manifest] = []
    parse_errors: list[str] = []
    for bf in probe_bin.read_session(base_path):
        all_samples.extend(bf.samples)
        all_manifests.extend(bf.manifests)
        parse_errors.extend(bf.parse_errors)

    if not all_samples:
        return {"verdict": "FAILED", "reason": "no samples in capture"}

    overview = session_overview(all_samples, all_manifests)

    # Resolve cid + anim_time_idx + encoding from sibling qualification report
    # if not provided.
    qual_path = base_path + ".qualification.json"
    if cid is None or anim_time_idx is None or encoding is None:
        if not os.path.exists(qual_path):
            # Try the closest qualification.json in the same dir.
            d = os.path.dirname(base_path)
            cands = [f for f in os.listdir(d) if f.endswith(".qualification.json")]
            if cands:
                cands.sort()
                qual_path = os.path.join(d, cands[-1])
        if os.path.exists(qual_path):
            with open(qual_path) as fh:
                qd = json.load(fh)
            if cid is None and qd.get("join_key", {}).get("matched_cid"):
                cid = qd["join_key"]["matched_cid"]
            if anim_time_idx is None and qd.get("anim_time"):
                anim_time_idx = qd["anim_time"].get("candidate_index", 1)
            if encoding is None and qd.get("anim_id_encoding"):
                encoding = qd["anim_id_encoding"].get("encoding", "full")

    # Reasonable fallbacks if no qualification.json.
    if anim_time_idx is None:
        anim_time_idx = 1     # +0x24, the most common anim-time slot
    if encoding is None:
        encoding = "full"

    # Build parry-window map for the identified character (if any).
    parry_windows_by_anim: dict[int, list[tuple[float, float]]] = {}
    if cid is not None:
        try:
            cid_num = int(cid[1:])
        except (TypeError, ValueError):
            cid_num = -1
        char_db = qualify_oracle.load_database(qualify_oracle.DEFAULT_DATABASE_PATH)
        char = char_db.get(cid_num)
        if char is not None:
            for w in char.parry_windows:
                aid = w.anim_id_full if encoding == "full" else w.anim_id_short
                parry_windows_by_anim.setdefault(aid, []).append(
                    (w.window_start_s, w.window_end_s)
                )

    # If we don't have a target cid, run the byte-change pass anyway with
    # NO parry-window labels — the report will still show byte mutation
    # frequencies, just won't be able to discriminate in-vs-out-of-window.
    stats = build_byte_stats(all_samples, parry_windows_by_anim, anim_time_idx)
    ranked = rank_candidates(stats)

    return {
        "verdict": "REPORT" if cid else "REPORT (no cid; raw byte stats only)",
        "reason": "see top candidates" if ranked else "no candidates passed filter",
        "overview": asdict(overview),
        "qualification_used": {
            "qualification_report": qual_path if os.path.exists(qual_path) else None,
            "cid": cid,
            "anim_time_idx": anim_time_idx,
            "encoding": encoding,
            "parry_windows_count": sum(len(v) for v in parry_windows_by_anim.values()),
        },
        "parse_errors": parse_errors[:20],
        "top_candidates": [
            {
                "region_id": st.region_id,
                "region_name": probe_bin.REGION_NAMES.get(st.region_id, "?"),
                "payload_offset_hex": f"0x{st.payload_offset:04X}",
                "distinct_values": st.distinct_values,
                "transition_count": st.transition_count,
                "in_window_changes": st.in_window_changes,
                "out_window_changes": st.out_window_changes,
                "in_window_samples": st.in_window_samples,
                "out_window_samples": st.out_window_samples,
                "in_rate": (st.in_window_changes / st.in_window_samples
                            if st.in_window_samples else 0.0),
                "out_rate": (st.out_window_changes / st.out_window_samples
                             if st.out_window_samples else 0.0),
            }
            for st in ranked
        ],
    }


def format_report(result: dict, base_path: str) -> str:
    lines = []
    lines.append(f"DISCOVERY ANALYSIS — {os.path.basename(base_path)}")
    lines.append("")
    if "overview" in result:
        ov = result["overview"]
        lines.append(f"Samples: {ov['sample_count']}  Duration: {ov['duration_seconds']:.1f}s "
                     f"Mode: {ov['mode']}  Truncated: {ov['truncated_count']}")
        if ov.get("drop_counters"):
            dc = ov["drop_counters"]
            lines.append(f"Drops: nb={dc.get('drops_no_buffer', 0)} "
                         f"bs={dc.get('drops_budget_skip', 0)} "
                         f"em={dc.get('drops_producer_emerg', 0)}")
        lines.append(f"Distinct focused handles: {ov.get('distinct_focused_handles', 0)}")
        lines.append(f"Distinct enemy anim_ids: {ov.get('distinct_anim_ids', 0)}")
    if "qualification_used" in result:
        qu = result["qualification_used"]
        lines.append("")
        lines.append(f"Using cid={qu.get('cid')} encoding={qu.get('encoding')} "
                     f"anim_time_idx={qu.get('anim_time_idx')} "
                     f"parry_windows={qu.get('parry_windows_count')}")
    if result.get("top_candidates"):
        lines.append("")
        lines.append("Top byte candidates (ranked by in-window mutation rate vs out):")
        lines.append("rank | region          | offset  | dv  | trans | in_chg/in_smp | out_chg/out_smp")
        lines.append("-" * 90)
        for i, c in enumerate(result["top_candidates"][:25]):
            lines.append(
                f"{i+1:>4} | {c['region_name']:<15} | {c['payload_offset_hex']:<7} "
                f"| {c['distinct_values']:>3} | {c['transition_count']:>5} "
                f"| {c['in_window_changes']:>4}/{c['in_window_samples']:<5} "
                f"| {c['out_window_changes']:>4}/{c['out_window_samples']:<5}"
            )
    if "parse_errors" in result and result["parse_errors"]:
        lines.append("")
        lines.append(f"Parse errors: {len(result['parse_errors'])} (first 5):")
        for err in result["parse_errors"][:5]:
            lines.append(f"  {err}")
    lines.append("")
    lines.append(f"Verdict: {result['verdict']}")
    if "reason" in result:
        lines.append(f"  {result['reason']}")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: analyze_discovery.py <bin_base_path> [--cid cXXXX] "
              "[--anim-time-idx 0..3] [--encoding full|short]", file=sys.stderr)
        return 2
    base = argv[1]
    if base.endswith(".bin"):
        base = base[:-4]
    cid = None
    anim_time_idx: Optional[int] = None
    encoding: Optional[str] = None
    i = 2
    while i < len(argv):
        if argv[i] == "--cid" and i + 1 < len(argv):
            cid = argv[i + 1]; i += 2
        elif argv[i] == "--anim-time-idx" and i + 1 < len(argv):
            anim_time_idx = int(argv[i + 1]); i += 2
        elif argv[i] == "--encoding" and i + 1 < len(argv):
            encoding = argv[i + 1]; i += 2
        else:
            print(f"unknown arg: {argv[i]}", file=sys.stderr); return 2

    result = discover(base, cid=cid, anim_time_idx=anim_time_idx, encoding=encoding)
    print(format_report(result, base))

    out = base + ".discovery.json"
    with open(out, "w") as fh:
        json.dump(result, fh, indent=2, default=str)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
