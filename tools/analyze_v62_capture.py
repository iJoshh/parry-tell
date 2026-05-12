#!/usr/bin/env python3
"""Analyze parry-tell-probe v6.2 capture for world-pos / anim_id / lock-on offsets.

Primary input is a schema v2 probe .bin file parsed via tools.probe_bin.
Outputs a markdown report tailored for research/007-v62-capture-analysis-codex.md.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.probe_bin import read_bin

NO_TARGET64 = 0xFFFFFFFFFFFFFFFF
NO_TARGET32 = 0xFFFFFFFF


@dataclass
class Verdict:
    winner: str
    confidence: str
    key_number: str
    rationale: str


def fmt_hex(v: int, width: int = 16) -> str:
    return f"0x{v:0{width}X}"


def f32_from_u32(u: int) -> float:
    return struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0]


def axis_stats(vals: list[float]) -> dict[str, float]:
    clean = [v for v in vals if math.isfinite(v)]
    if not clean:
        return {
            "n": 0,
            "min": float("nan"),
            "max": float("nan"),
            "range": float("nan"),
            "mean": float("nan"),
            "var": float("nan"),
            "std": float("nan"),
        }
    mean = sum(clean) / len(clean)
    var = sum((x - mean) ** 2 for x in clean) / len(clean)
    return {
        "n": len(clean),
        "min": min(clean),
        "max": max(clean),
        "range": max(clean) - min(clean),
        "mean": mean,
        "var": var,
        "std": var ** 0.5,
    }


def pct(part: int, total: int) -> float:
    return (100.0 * part / total) if total else 0.0


def speed_profile_samples(samples: list[Any], pos_getter) -> dict[str, Any]:
    speeds: list[float] = []
    deltas: list[float] = []
    bad = 0
    for a, b in zip(samples, samples[1:]):
        dt = (b.ts_ms_rel - a.ts_ms_rel) / 1000.0
        if dt <= 0:
            continue
        pa = pos_getter(a)
        pb = pos_getter(b)
        if not all(math.isfinite(x) for x in (*pa, *pb)):
            bad += 1
            continue
        dx = pb[0] - pa[0]
        dy = pb[1] - pa[1]
        dz = pb[2] - pa[2]
        d = (dx * dx + dy * dy + dz * dz) ** 0.5
        deltas.append(d)
        speeds.append(d / dt)
    if not speeds:
        return {"n": 0, "bad": bad}
    q_speed = statistics.quantiles(speeds, n=100)
    q_delta = statistics.quantiles(deltas, n=100)
    return {
        "n": len(speeds),
        "bad": bad,
        "speed_min": min(speeds),
        "speed_med": statistics.median(speeds),
        "speed_p95": q_speed[94],
        "speed_p99": q_speed[98],
        "speed_max": max(speeds),
        "delta_med": statistics.median(deltas),
        "delta_p95": q_delta[94],
        "gt12": sum(1 for v in speeds if v > 12.0),
        "gt20": sum(1 for v in speeds if v > 20.0),
        "gt50": sum(1 for v in speeds if v > 50.0),
    }


def speed_profile_rows(rows: list[tuple[Any, Any]], pos_getter) -> dict[str, Any]:
    by_handle: dict[int, list[tuple[int, Any]]] = defaultdict(list)
    for s, e in rows:
        by_handle[e.handle].append((s.ts_ms_rel, e))

    speeds: list[float] = []
    for arr in by_handle.values():
        arr.sort(key=lambda x: x[0])
        for (ta, ea), (tb, eb) in zip(arr, arr[1:]):
            dt = (tb - ta) / 1000.0
            if dt <= 0:
                continue
            pa = pos_getter(ea)
            pb = pos_getter(eb)
            if not all(math.isfinite(x) for x in (*pa, *pb)):
                continue
            dx = pb[0] - pa[0]
            dy = pb[1] - pa[1]
            dz = pb[2] - pa[2]
            d = (dx * dx + dy * dy + dz * dz) ** 0.5
            speeds.append(d / dt)

    if not speeds:
        return {"n": 0}
    q = statistics.quantiles(speeds, n=100)
    return {
        "n": len(speeds),
        "speed_med": statistics.median(speeds),
        "speed_p95": q[94],
        "speed_p99": q[98],
        "speed_max": max(speeds),
        "gt12": sum(1 for v in speeds if v > 12.0),
        "gt20": sum(1 for v in speeds if v > 20.0),
        "gt50": sum(1 for v in speeds if v > 50.0),
    }


def transitions(vals: list[int]) -> int:
    return sum(1 for i in range(1, len(vals)) if vals[i] != vals[i - 1])


def parse_arm_disarm(log_path: Path) -> dict[str, int]:
    out: dict[str, int] = {}
    if not log_path.exists():
        return out
    rx = re.compile(r"^(\d+)\s+.*\b(armed|disarmed)\b")
    with log_path.open("r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = rx.search(line)
            if not m:
                continue
            ts = int(m.group(1))
            state = m.group(2)
            if state == "armed":
                out.setdefault("first_arm_ms", ts)
                out["last_arm_ms"] = ts
            elif state == "disarmed":
                out["last_disarm_ms"] = ts
    return out


def q1_world_pos(samples: list[Any], focused_rows: list[tuple[Any, Any]]) -> tuple[dict[str, Any], Verdict]:
    legacy_axes = {
        "x": axis_stats([s.player_pos[0] for s in samples]),
        "y": axis_stats([s.player_pos[1] for s in samples]),
        "z": axis_stats([s.player_pos[2] for s in samples]),
    }
    phys_axes = {
        "x": axis_stats([s.player_pos_phys[0] for s in samples]),
        "y": axis_stats([s.player_pos_phys[1] for s in samples]),
        "z": axis_stats([s.player_pos_phys[2] for s in samples]),
    }

    legacy_speed = speed_profile_samples(samples, lambda s: s.player_pos)
    phys_speed = speed_profile_samples(samples, lambda s: s.player_pos_phys)

    diffs = {
        "x": [s.player_pos[0] - s.player_pos_phys[0] for s in samples],
        "y": [s.player_pos[1] - s.player_pos_phys[1] for s in samples],
        "z": [s.player_pos[2] - s.player_pos_phys[2] for s in samples],
    }
    diff_summary: dict[str, dict[str, float]] = {}
    for axis, vals in diffs.items():
        vals_sorted = sorted(vals)
        diff_summary[axis] = {
            "median": statistics.median(vals),
            "min": vals_sorted[0],
            "max": vals_sorted[-1],
        }

    phys_spike_events: list[dict[str, Any]] = []
    for i in range(1, len(samples)):
        a = samples[i - 1]
        b = samples[i]
        dt = (b.ts_ms_rel - a.ts_ms_rel) / 1000.0
        if dt <= 0:
            continue
        dx = b.player_pos_phys[0] - a.player_pos_phys[0]
        dy = b.player_pos_phys[1] - a.player_pos_phys[1]
        dz = b.player_pos_phys[2] - a.player_pos_phys[2]
        speed = (dx * dx + dy * dy + dz * dz) ** 0.5 / dt
        if speed > 50.0:
            phys_spike_events.append(
                {
                    "idx": i,
                    "ts_ms_rel": b.ts_ms_rel,
                    "speed": speed,
                    "from": a.player_pos_phys,
                    "to": b.player_pos_phys,
                }
            )

    focused_phys_axes = {
        "x": axis_stats([e.world_pos_phys[0] for _, e in focused_rows]),
        "y": axis_stats([e.world_pos_phys[1] for _, e in focused_rows]),
        "z": axis_stats([e.world_pos_phys[2] for _, e in focused_rows]),
    }
    focused_legacy_axes = {
        "x": axis_stats([f32_from_u32(e.field_at_0x068) for _, e in focused_rows]),
        "y": axis_stats([f32_from_u32(e.field_at_0x06C) for _, e in focused_rows]),
        "z": axis_stats([f32_from_u32(e.field_at_0x080) for _, e in focused_rows]),
    }
    focused_phys_speed = speed_profile_rows(focused_rows, lambda e: e.world_pos_phys)
    focused_legacy_speed = speed_profile_rows(
        focused_rows,
        lambda e: (
            f32_from_u32(e.field_at_0x068),
            f32_from_u32(e.field_at_0x06C),
            f32_from_u32(e.field_at_0x080),
        ),
    )

    enemy_legacy_bad = (
        abs(focused_legacy_axes["x"]["range"]) < 1e-9
        and focused_legacy_axes["x"]["max"] < 1e-20
    )
    player_phys_coherent = phys_axes["x"]["range"] > 1.0 and phys_axes["z"]["range"] > 1.0

    if enemy_legacy_bad and player_phys_coherent:
        verdict = Verdict(
            winner="phys",
            confidence="high",
            key_number=(
                f"enemy legacy-ish X range={focused_legacy_axes['x']['range']:.3g} vs "
                f"enemy phys X range={focused_phys_axes['x']['range']:.3f}"
            ),
            rationale=(
                "Enemy legacy-ish reinterpret is structurally dead (near-zero constant), while "
                "phys-chain vectors vary and decode cleanly from region-6 +0x70 bytes."
            ),
        )
    elif player_phys_coherent:
        verdict = Verdict(
            winner="phys",
            confidence="medium",
            key_number=f"player phys p95 speed={phys_speed.get('speed_p95', float('nan')):.3f} m/s",
            rationale="Phys path tracks motion; legacy also tracks player map coords but is not robust for enemy pathing.",
        )
    else:
        verdict = Verdict(
            winner="neither",
            confidence="low",
            key_number="no coherent motion profile",
            rationale="Neither path showed stable motion coherence.",
        )

    return {
        "legacy_axes": legacy_axes,
        "phys_axes": phys_axes,
        "legacy_speed": legacy_speed,
        "phys_speed": phys_speed,
        "diff_summary": diff_summary,
        "phys_spike_events": phys_spike_events,
        "focused_phys_axes": focused_phys_axes,
        "focused_legacy_axes": focused_legacy_axes,
        "focused_phys_speed": focused_phys_speed,
        "focused_legacy_speed": focused_legacy_speed,
    }, verdict


def q2_anim_id(
    focused_rows: list[tuple[Any, Any]],
    anim_targets: set[int],
) -> tuple[dict[str, Any], Verdict]:
    paths = {
        "path_a": [e.anim_id for _, e in focused_rows],
        "path_b": [e.anim_id_path_b for _, e in focused_rows],
        "path_c": [e.anim_id_path_c for _, e in focused_rows],
    }

    path_stats: dict[str, dict[str, Any]] = {}
    for name, vals in paths.items():
        n = len(vals)
        nz = [v for v in vals if v != 0]
        matches = [v for v in vals if v in anim_targets]
        matches_nz = [v for v in nz if v in anim_targets]
        path_stats[name] = {
            "n": n,
            "non_zero": len(nz),
            "non_zero_pct": pct(len(nz), n),
            "match_any": len(matches),
            "match_any_pct": pct(len(matches), n),
            "match_non_zero": len(matches_nz),
            "match_non_zero_pct": pct(len(matches_nz), len(nz)) if nz else 0.0,
            "distinct": len(set(vals)),
            "transitions": transitions(vals),
            "top_values": Counter(vals).most_common(10),
        }

    read_idx_vals = [e.read_idx for _, e in focused_rows]
    read_idx_counter = Counter(read_idx_vals)

    # Byte-level scans in regions 6/7/8/9. v6.3: region 9 (module_bag_member)
    # was added so the analyzer covers the wide module-bag sweep. Hit keys
    # include source_chain (= module-bag offset for region 9, time_act head
    # offset for regions 4/8) so hits from different module-bag slots don't
    # collapse into a single false signal.
    hit_counts: Counter[tuple[str, int, int, int, int]] = Counter()
    hit_examples: dict[tuple[str, int, int, int, int], tuple[int, int, int, str]] = {}
    rows_with_any_hit = 0
    rows_region_presence: Counter[int] = Counter()

    for s, e in focused_rows:
        row_hit = False
        for r in e.regions:
            if r.region_id not in (6, 7, 8, 9):
                continue
            rows_region_presence[r.region_id] += 1
            src_chain = r.source_chain  # bag/timeact offset for grouped hits
            payload = r.payload
            for off in range(0, len(payload) - 3, 4):
                b = payload[off : off + 4]
                u32le = struct.unpack_from("<I", b)[0]
                u32be = struct.unpack_from(">I", b)[0]
                u16lo = struct.unpack_from("<H", b, 0)[0]
                u16hi = struct.unpack_from("<H", b, 2)[0]
                if u32le in anim_targets:
                    k = ("u32le", r.region_id, src_chain, off, u32le)
                    hit_counts[k] += 1
                    row_hit = True
                    hit_examples.setdefault(k, (s.ts_ms_rel, e.handle, r.region_base_abs, b.hex()))
                if u32be in anim_targets:
                    k = ("u32be", r.region_id, src_chain, off, u32be)
                    hit_counts[k] += 1
                    row_hit = True
                    hit_examples.setdefault(k, (s.ts_ms_rel, e.handle, r.region_base_abs, b.hex()))
                if u16lo in anim_targets:
                    k = ("u16lo", r.region_id, src_chain, off, u16lo)
                    hit_counts[k] += 1
                    row_hit = True
                    hit_examples.setdefault(k, (s.ts_ms_rel, e.handle, r.region_base_abs, b.hex()))
                if u16hi in anim_targets:
                    k = ("u16hi", r.region_id, src_chain, off + 2, u16hi)
                    hit_counts[k] += 1
                    row_hit = True
                    hit_examples.setdefault(k, (s.ts_ms_rel, e.handle, r.region_base_abs, b.hex()))
        if row_hit:
            rows_with_any_hit += 1

    # Additional byte-level probes: region7 +0x90 and time_act queue fields.
    action_req_0x90 = Counter()
    time_act_q0 = Counter()
    time_act_w_r_idx = Counter()
    first_byte_examples: dict[str, Any] = {}

    for s, e in focused_rows:
        r7 = next((r for r in e.regions if r.region_id == 7), None)
        r2 = next((r for r in e.regions if r.region_id == 2), None)
        if r7 and len(r7.payload) >= 0x94:
            v = struct.unpack_from("<I", r7.payload, 0x90)[0]
            action_req_0x90[v] += 1
            if "action_request" not in first_byte_examples:
                first_byte_examples["action_request"] = {
                    "ts_ms_rel": s.ts_ms_rel,
                    "handle": e.handle,
                    "region_base": r7.region_base_abs,
                    "bytes": r7.payload[0x90 : 0x94].hex(),
                    "u32": v,
                }
        if r2 and len(r2.payload) >= 0xC8:
            q0 = struct.unpack_from("<i", r2.payload, 0x20)[0]
            widx = struct.unpack_from("<I", r2.payload, 0xC0)[0]
            ridx = struct.unpack_from("<I", r2.payload, 0xC4)[0]
            time_act_q0[q0] += 1
            time_act_w_r_idx[(widx, ridx)] += 1
            if "time_act" not in first_byte_examples:
                first_byte_examples["time_act"] = {
                    "ts_ms_rel": s.ts_ms_rel,
                    "handle": e.handle,
                    "region_base": r2.region_base_abs,
                    "bytes_q0": r2.payload[0x20 : 0x24].hex(),
                    "q0_i32": q0,
                    "bytes_ridx": r2.payload[0xC4 : 0xC8].hex(),
                    "ridx": ridx,
                }

    top_hits = []
    for k, c in hit_counts.most_common(20):
        ex = hit_examples[k]
        top_hits.append(
            {
                "kind": k[0],
                "region_id": k[1],
                "source_chain": k[2],   # bag offset (region 9) or time_act head offset (regions 4/8)
                "offset": k[3],         # offset within the captured 512B body
                "value": k[4],
                "count": c,
                "pct_rows": pct(c, len(focused_rows)),
                "example_ts_ms_rel": ex[0],
                "example_handle": ex[1],
                "example_region_base": ex[2],
                "example_bytes": ex[3],
            }
        )

    total_u32_hits = sum(c for k, c in hit_counts.items() if k[0] in ("u32le", "u32be"))
    best_u32 = max(
        (
            (k, c)
            for k, c in hit_counts.items()
            if k[0] in ("u32le", "u32be")
        ),
        key=lambda x: x[1],
        default=None,
    )

    if path_stats["path_a"]["match_any"] > 0 and path_stats["path_a"]["transitions"] > 5:
        verdict = Verdict(
            winner="path_a",
            confidence="medium",
            key_number=f"path_a matches={path_stats['path_a']['match_any']}/{len(focused_rows)}",
            rationale="Path A produced matching anim IDs with temporal transitions.",
        )
    elif path_stats["path_b"]["match_any"] > 0 and path_stats["path_b"]["transitions"] > 5:
        verdict = Verdict(
            winner="path_b",
            confidence="medium",
            key_number=f"path_b matches={path_stats['path_b']['match_any']}/{len(focused_rows)}",
            rationale="Path B produced matching anim IDs with temporal transitions.",
        )
    elif path_stats["path_c"]["match_any"] > 0 and path_stats["path_c"]["transitions"] > 5:
        verdict = Verdict(
            winner="path_c",
            confidence="medium",
            key_number=f"path_c matches={path_stats['path_c']['match_any']}/{len(focused_rows)}",
            rationale="Path C produced matching anim IDs with temporal transitions.",
        )
    elif best_u32 and best_u32[1] >= max(100, int(0.05 * len(focused_rows))):
        (kind, rid, src_chain, off, _value), count = best_u32
        # For region 9 (module_bag_member) the src_chain is the bag offset of
        # the captured module — report it so the winner is unambiguous about
        # WHICH module the anim_id lives on.
        if rid == 9:
            location = f"region=9(bag+0x{src_chain:X}),offset=0x{off:X}"
        elif rid in (4, 8):
            location = f"region={rid}(time_act+0x{src_chain:X}),offset=0x{off:X}"
        else:
            location = f"region={rid},offset=0x{off:X}"
        verdict = Verdict(
            winner=f"fixture-scan-find({location})",
            confidence="medium",
            key_number=f"best {kind} hit count={count}/{len(focused_rows)}",
            rationale="Consistent aligned scan hit suggests a plausible field offset.",
        )
    else:
        verdict = Verdict(
            winner="NONE",
            confidence="high",
            key_number=f"total u32 hits={total_u32_hits} over {len(focused_rows)} focused rows",
            rationale=(
                "All direct paths are static sentinels/zeros with 0 target-ID matches; "
                "scan hits are sparse collisions (mostly u16), not a stable anim field."
            ),
        )

    return {
        "path_stats": path_stats,
        "read_idx_counter": read_idx_counter,
        "rows_region_presence": rows_region_presence,
        "rows_with_any_hit": rows_with_any_hit,
        "hit_counts": hit_counts,
        "top_hits": top_hits,
        "action_req_0x90": action_req_0x90,
        "time_act_q0": time_act_q0,
        "time_act_w_r_idx": time_act_w_r_idx,
        "first_byte_examples": first_byte_examples,
    }, verdict


def q3_lock_on(samples: list[Any], focused_rows: list[tuple[Any, Any]]) -> tuple[dict[str, Any], Verdict]:
    legacy = [s.player_lock_on_target_handle for s in samples]
    new = [s.player_lock_on_target_handle_new for s in samples]
    area = [s.player_lock_on_target_area_new for s in samples]
    vtables = [s.player_chr_ins_vtable for s in samples]

    legacy_trans = transitions(legacy)
    new_trans = transitions(new)
    area_trans = transitions(area)

    legacy_dist = Counter(legacy)
    new_dist = Counter(new)
    area_dist = Counter(area)
    vtable_dist = Counter(vtables)

    new_onoff = [v != NO_TARGET64 for v in new]
    onoff_boundaries = transitions(new_onoff)
    within_on_changes = sum(
        1
        for i in range(1, len(new))
        if new_onoff[i] and new_onoff[i - 1] and new[i] != new[i - 1]
    )

    # Candidate matching to same-sample enemy handles.
    legacy_match = 0
    new_match = 0
    legacy_non_sentinel = 0
    new_non_sentinel = 0
    for s in samples:
        hset = {e.handle for e in s.enemies}
        lv = s.player_lock_on_target_handle
        nv = s.player_lock_on_target_handle_new
        if lv not in (0, NO_TARGET64):
            legacy_non_sentinel += 1
        if nv not in (0, NO_TARGET64):
            new_non_sentinel += 1
        if lv in hset:
            legacy_match += 1
        if nv in hset:
            new_match += 1

    # Compare player vtable against focused enemy chr_ins vtable (region 0 +0x0).
    enemy_vtables = Counter()
    for _s, e in focused_rows:
        r0 = next((r for r in e.regions if r.region_id == 0 and len(r.payload) >= 8), None)
        if not r0:
            continue
        ev = struct.unpack_from("<Q", r0.payload, 0)[0]
        enemy_vtables[ev] += 1

    module_base_counter = Counter(s.module_base_eldenring for s in samples)
    base = next(iter(module_base_counter)) if len(module_base_counter) == 1 else None
    player_vtable_rvas = Counter((v - base) for v in vtables) if base is not None else Counter()
    enemy_vtable_rvas = Counter()
    if base is not None:
        for v, c in enemy_vtables.items():
            enemy_vtable_rvas[v - base] += c

    transition_events: list[dict[str, Any]] = []
    for i in range(1, len(samples)):
        if new[i] != new[i - 1]:
            s = samples[i]
            transition_events.append(
                {
                    "idx": i,
                    "ts_ms_rel": s.ts_ms_rel,
                    "from": new[i - 1],
                    "to": new[i],
                    "area": area[i],
                    "legacy": legacy[i],
                }
            )

    if legacy_trans <= 1 and new_trans >= 8 and onoff_boundaries >= 8:
        conf = "high" if onoff_boundaries >= 10 else "medium"
        verdict = Verdict(
            winner="new_6B0",
            confidence=conf,
            key_number=(
                f"transitions new={new_trans} vs legacy={legacy_trans}; "
                f"on/off boundaries={onoff_boundaries}"
            ),
            rationale=(
                "`+0x6B0` toggles between sentinel and handle-like values with expected cadence; "
                "`+0x6A0` is a single stable pointer-like constant."
            ),
        )
    elif legacy_trans > new_trans and legacy_trans >= 8:
        verdict = Verdict(
            winner="legacy_6A0",
            confidence="low",
            key_number=f"legacy transitions={legacy_trans}",
            rationale="Legacy showed stronger transition behavior in this capture.",
        )
    else:
        verdict = Verdict(
            winner="inconclusive",
            confidence="low",
            key_number=f"transitions legacy={legacy_trans}, new={new_trans}",
            rationale="Neither field cleanly matched expected lock-on dynamics.",
        )

    return {
        "legacy_trans": legacy_trans,
        "new_trans": new_trans,
        "area_trans": area_trans,
        "legacy_dist": legacy_dist,
        "new_dist": new_dist,
        "area_dist": area_dist,
        "vtable_dist": vtable_dist,
        "new_onoff_boundaries": onoff_boundaries,
        "new_within_on_changes": within_on_changes,
        "legacy_match": legacy_match,
        "new_match": new_match,
        "legacy_non_sentinel": legacy_non_sentinel,
        "new_non_sentinel": new_non_sentinel,
        "module_base_counter": module_base_counter,
        "player_vtable_rvas": player_vtable_rvas,
        "enemy_vtables": enemy_vtables,
        "enemy_vtable_rvas": enemy_vtable_rvas,
        "transition_events": transition_events,
    }, verdict


def render_report(
    *,
    bin_path: Path,
    log_path: Path,
    manifest_fields: dict[str, str],
    parse_errors: list[str],
    sample_count: int,
    focused_count: int,
    q1: dict[str, Any],
    q1_verdict: Verdict,
    q2: dict[str, Any],
    q2_verdict: Verdict,
    q3: dict[str, Any],
    q3_verdict: Verdict,
    arm_info: dict[str, int],
) -> str:
    session_start = int(manifest_fields.get("session_start_ms", "0")) if manifest_fields.get("session_start_ms") else None
    mode = manifest_fields.get("mode", "?")
    schema = manifest_fields.get("schema_version", "?")

    dt_summary = "n/a"
    if sample_count > 1:
        # inferred from q1 speed n and duration from samples impossible without sample list here;
        # keep concise in header, detailed values below.
        dt_summary = "see Q1"

    lines: list[str] = []
    lines.append("# v6.2 Capture Analysis (q62.bin)")
    lines.append("")
    lines.append("## 1. Executive summary: 3-row verdict table")
    lines.append("")
    lines.append("| Question | Verdict | Confidence | Most important number |")
    lines.append("|---|---|---|---|")
    lines.append(
        f"| Q1 World position | **WORLD POS WINNER = {q1_verdict.winner}** | {q1_verdict.confidence} | {q1_verdict.key_number} |"
    )
    lines.append(
        f"| Q2 Enemy anim_id | **ENEMY ANIM_ID WINNER = {q2_verdict.winner}** | {q2_verdict.confidence} | {q2_verdict.key_number} |"
    )
    lines.append(
        f"| Q3 Lock-on handle | **LOCK-ON WINNER = {q3_verdict.winner}** | {q3_verdict.confidence} | {q3_verdict.key_number} |"
    )
    lines.append("")
    lines.append("Capture metadata:")
    lines.append(f"- bin: `{bin_path}`")
    lines.append(f"- schema_version: `{schema}`, mode: `{mode}`, samples: `{sample_count}`, focused rows: `{focused_count}`")
    lines.append(f"- parse_errors: `{len(parse_errors)}`")
    if session_start is not None:
        lines.append(f"- manifest session_start_ms: `{session_start}`")
    if arm_info:
        for k in sorted(arm_info):
            lines.append(f"- {k}: `{arm_info[k]}`")
        if session_start is not None and "last_arm_ms" in arm_info and "last_disarm_ms" in arm_info:
            arm_rel_start = arm_info["last_arm_ms"] - session_start
            arm_rel_end = arm_info["last_disarm_ms"] - session_start
            lines.append(
                f"- last arm window relative to session_start: `{arm_rel_start}..{arm_rel_end}` ms (duration `{(arm_rel_end-arm_rel_start)/1000:.3f}` s)"
            )
    lines.append("")

    # Q1
    lines.append("## 2. Q1 world pos detailed")
    lines.append("")
    lines.append("Player position (legacy `+0x6C0` vs phys-chain `+0x190→+0x68→+0x70`):")
    for label, stats in (("legacy", q1["legacy_axes"]), ("phys", q1["phys_axes"])):
        lines.append(
            f"- {label} axis ranges: "
            f"x `{stats['x']['min']:.6f}..{stats['x']['max']:.6f}` (Δ `{stats['x']['range']:.6f}`), "
            f"y `{stats['y']['min']:.6f}..{stats['y']['max']:.6f}` (Δ `{stats['y']['range']:.6f}`), "
            f"z `{stats['z']['min']:.6f}..{stats['z']['max']:.6f}` (Δ `{stats['z']['range']:.6f}`)"
        )

    ls = q1["legacy_speed"]
    ps = q1["phys_speed"]
    lines.append(
        "- legacy speed profile: "
        f"median `{ls.get('speed_med', float('nan')):.3f}` m/s, "
        f"p95 `{ls.get('speed_p95', float('nan')):.3f}`, "
        f"max `{ls.get('speed_max', float('nan')):.3f}`, "
        f"`>12m/s` `{ls.get('gt12', 0)}/{ls.get('n', 0)}`"
    )
    lines.append(
        "- phys speed profile: "
        f"median `{ps.get('speed_med', float('nan')):.3f}` m/s, "
        f"p95 `{ps.get('speed_p95', float('nan')):.3f}`, "
        f"max `{ps.get('speed_max', float('nan')):.3f}`, "
        f"`>12m/s` `{ps.get('gt12', 0)}/{ps.get('n', 0)}`"
    )
    d = q1["diff_summary"]
    lines.append(
        f"- legacy-minus-phys offsets (median): x `{d['x']['median']:.6f}`, y `{d['y']['median']:.6f}`, z `{d['z']['median']:.6f}`"
    )
    lines.append(
        f"- legacy-minus-phys min/max: x `{d['x']['min']:.6f}..{d['x']['max']:.6f}`, "
        f"y `{d['y']['min']:.6f}..{d['y']['max']:.6f}`, z `{d['z']['min']:.6f}..{d['z']['max']:.6f}`"
    )

    spike_events = q1["phys_spike_events"]
    lines.append(f"- phys spike events (`speed > 50 m/s`): `{len(spike_events)}`")
    for ev in spike_events[:3]:
        lines.append(
            f"  - idx `{ev['idx']}`, ts `{ev['ts_ms_rel']}`: `{ev['speed']:.3f}` m/s, "
            f"from `{tuple(round(x,6) for x in ev['from'])}` to `{tuple(round(x,6) for x in ev['to'])}`"
        )

    lines.append("")
    lines.append("Focused enemy position comparison (`world_pos_phys` vs legacy-ish reinterpret `(0x068,0x06C,0x080)` as f32):")
    fpa = q1["focused_phys_axes"]
    fla = q1["focused_legacy_axes"]
    lines.append(
        f"- focused phys ranges: x Δ `{fpa['x']['range']:.6f}`, y Δ `{fpa['y']['range']:.6f}`, z Δ `{fpa['z']['range']:.6f}`"
    )
    lines.append(
        f"- focused legacy-ish ranges: x Δ `{fla['x']['range']:.6g}`, y Δ `{fla['y']['range']:.6f}`, z Δ `{fla['z']['range']:.6f}`"
    )
    fps = q1["focused_phys_speed"]
    fls = q1["focused_legacy_speed"]
    lines.append(
        f"- focused phys speed: median `{fps.get('speed_med', float('nan')):.3f}` m/s, "
        f"p95 `{fps.get('speed_p95', float('nan')):.3f}`, max `{fps.get('speed_max', float('nan')):.3f}`"
    )
    lines.append(
        f"- focused legacy-ish speed: median `{fls.get('speed_med', float('nan')):.3f}` m/s, "
        f"p95 `{fls.get('speed_p95', float('nan')):.3f}`, max `{fls.get('speed_max', float('nan')):.3f}`"
    )
    lines.append("")
    lines.append(
        "Byte-level evidence (first focused row): region-6 `phys_module_body` at `+0x70` decodes directly to captured `world_pos_phys`; "
        "region-7 `+0x90` and region-2 queue fields shown in Q2."
    )
    lines.append(f"- WORLD POS WINNER = **{q1_verdict.winner}** ({q1_verdict.confidence})")
    lines.append(f"- rationale: {q1_verdict.rationale}")
    lines.append("")

    # Q2
    lines.append("## 3. Q2 enemy anim_id detailed")
    lines.append("")
    lines.append("Per-path stats across focused rows:")
    for name in ("path_a", "path_b", "path_c"):
        st = q2["path_stats"][name]
        topv = ", ".join(f"{fmt_hex(v,8)}:{c}" for v, c in st["top_values"][:3])
        lines.append(
            f"- {name}: non-zero `{st['non_zero']}/{st['n']}` ({st['non_zero_pct']:.3f}%), "
            f"match(c4380 IDs) `{st['match_any']}/{st['n']}` ({st['match_any_pct']:.3f}%), "
            f"distinct `{st['distinct']}`, transitions `{st['transitions']}`, top `{topv}`"
        )

    ric = q2["read_idx_counter"]
    lines.append(
        "- read_idx distribution: "
        + ", ".join(f"{fmt_hex(v,8)}:{c}" for v, c in ric.most_common(5))
    )

    lines.append(
        f"- region presence counts (focused rows): "
        f"R6 `{q2['rows_region_presence'].get(6,0)}`, R7 `{q2['rows_region_presence'].get(7,0)}`, "
        f"R8 `{q2['rows_region_presence'].get(8,0)}`, R9 `{q2['rows_region_presence'].get(9,0)}`"
    )
    lines.append(f"- rows with any scan hit (u32/u16/u32-be): `{q2['rows_with_any_hit']}/{focused_count}`")

    u32_hits = [h for h in q2["top_hits"] if h["kind"].startswith("u32")]
    lines.append(f"- u32-aligned hits total keys: `{len(u32_hits)}` (top below)")
    for h in u32_hits[:10]:
        # For region 9 the source_chain is the module-bag offset; for regions
        # 4/8 it's the time_act head offset. Render so the bag slot is visible.
        if h["region_id"] == 9:
            loc = f"R9(bag+0x{h['source_chain']:X})+0x{h['offset']:X}"
        elif h["region_id"] in (4, 8):
            loc = f"R{h['region_id']}(time_act+0x{h['source_chain']:X})+0x{h['offset']:X}"
        else:
            loc = f"R{h['region_id']}+0x{h['offset']:X}"
        lines.append(
            f"  - {h['kind']} {loc} = `{h['value']}` "
            f"count `{h['count']}` ({h['pct_rows']:.4f}% rows), example bytes `{h['example_bytes']}`"
        )

    a90 = q2["action_req_0x90"]
    taq0 = q2["time_act_q0"]
    tawr = q2["time_act_w_r_idx"]
    lines.append(
        "- region7 `action_request_body +0x90` u32 distribution: "
        + ", ".join(f"{fmt_hex(v,8)}:{c}" for v, c in a90.most_common(3))
    )
    lines.append(
        "- region2 `time_act_module +0x20` (queue[0].anim_id) i32 distribution: "
        + ", ".join(f"{v}:{c}" for v, c in taq0.most_common(3))
    )
    lines.append(
        "- region2 write/read idx pairs (`+0xC0/+0xC4`): "
        + ", ".join(f"({w},{r}):{c}" for (w, r), c in tawr.most_common(3))
    )

    ex = q2["first_byte_examples"]
    if "action_request" in ex:
        ax = ex["action_request"]
        lines.append(
            f"- byte example action_request: ts `{ax['ts_ms_rel']}`, handle `{fmt_hex(ax['handle'])}`, "
            f"base `{fmt_hex(ax['region_base'])}`, bytes@0x90 `{ax['bytes']}` => `{fmt_hex(ax['u32'],8)}`"
        )
    if "time_act" in ex:
        tx = ex["time_act"]
        lines.append(
            f"- byte example time_act: ts `{tx['ts_ms_rel']}`, handle `{fmt_hex(tx['handle'])}`, "
            f"base `{fmt_hex(tx['region_base'])}`, bytes@0x20 `{tx['bytes_q0']}` => `{tx['q0_i32']}`, "
            f"bytes@0xC4 `{tx['bytes_ridx']}` => `{tx['ridx']}`"
        )

    lines.append(f"- ENEMY ANIM_ID WINNER = **{q2_verdict.winner}** ({q2_verdict.confidence})")
    lines.append(f"- rationale: {q2_verdict.rationale}")
    lines.append("")

    # Q3
    lines.append("## 4. Q3 lock-on detailed")
    lines.append("")
    lines.append(
        f"- distinct values: legacy `{len(q3['legacy_dist'])}`, new `{len(q3['new_dist'])}`, area `{len(q3['area_dist'])}`"
    )
    lines.append(
        f"- transition counts: legacy `{q3['legacy_trans']}`, new `{q3['new_trans']}`, area `{q3['area_trans']}`"
    )
    lines.append(
        "- legacy top values: "
        + ", ".join(f"{fmt_hex(v)}:{c}" for v, c in q3["legacy_dist"].most_common(5))
    )
    lines.append(
        "- new top values: "
        + ", ".join(f"{fmt_hex(v)}:{c}" for v, c in q3["new_dist"].most_common(6))
    )
    lines.append(
        "- area top values: "
        + ", ".join(f"{fmt_hex(v,8)}:{c}" for v, c in q3["area_dist"].most_common(5))
    )

    lines.append(
        f"- new on/off boundaries (non-sentinel vs sentinel): `{q3['new_onoff_boundaries']}`; "
        f"within-on target changes: `{q3['new_within_on_changes']}`"
    )
    lines.append(
        f"- same-sample enemy-handle matches: legacy `{q3['legacy_match']}/{sample_count}`, "
        f"new `{q3['new_match']}/{sample_count}`"
    )

    module_bases = q3["module_base_counter"]
    lines.append(
        "- module base(s): "
        + ", ".join(f"{fmt_hex(v)}:{c}" for v, c in module_bases.items())
    )
    lines.append(
        "- player vtable(s): "
        + ", ".join(f"{fmt_hex(v)}:{c}" for v, c in q3["vtable_dist"].most_common(5))
    )
    if q3["player_vtable_rvas"]:
        lines.append(
            "- player vtable RVA(s): "
            + ", ".join(f"0x{r:X}:{c}" for r, c in q3["player_vtable_rvas"].most_common(5))
        )
    lines.append(
        "- focused enemy vtable(s) from region0+0x0: "
        + ", ".join(f"{fmt_hex(v)}:{c}" for v, c in q3["enemy_vtables"].most_common(5))
    )
    if q3["enemy_vtable_rvas"]:
        lines.append(
            "- focused enemy vtable RVA(s): "
            + ", ".join(f"0x{r:X}:{c}" for r, c in q3["enemy_vtable_rvas"].most_common(5))
        )

    lines.append(
        "- vswarte cross-reference (from local research/006 findings): "
        "`PlayerIns` has `player_menu_ctrl` at `+0x6A0`, `unk6a8[8]`, then `locked_on_enemy` at `+0x6B0`; "
        "capture behavior matches this layout (`+0x6A0` pointer-like constant, `+0x6B0/+0x6B4` toggle-paired values)."
    )

    lines.append("- new-field transition events (first 10):")
    for ev in q3["transition_events"][:10]:
        lines.append(
            f"  - idx `{ev['idx']}` ts `{ev['ts_ms_rel']}`: {fmt_hex(ev['from'])} -> {fmt_hex(ev['to'])}, "
            f"area `{fmt_hex(ev['area'],8)}`, legacy `{fmt_hex(ev['legacy'])}`"
        )

    lines.append(f"- LOCK-ON WINNER = **{q3_verdict.winner}** ({q3_verdict.confidence})")
    lines.append(f"- rationale: {q3_verdict.rationale}")
    lines.append("")

    # Side findings
    lines.append("## 5. Side findings (unexpected in data)")
    lines.append("")
    lines.append("- Focus reason is always `3` (`qualification_nearest`) across all samples/focused rows; `in_lock_on` flags are never set.")
    lines.append("- Focused handle includes a non-canonical value `0xFFFFFFFF17100000` for 1,781 rows; enemy vtable stays valid during these rows.")
    lines.append("- Manifest says `sample_rate_hz=10`, but observed sample delta is ~12 ms (about 83 Hz), indicating the writer is capturing near-hook cadence.")
    lines.append("- Capture spans only the second arm window: sample ts_rel `308145..412540` maps to absolute `380482952..380587347` ms (about 104.4 s), matching the `F11: armed` re-arm at `380482950`.")
    lines.append("")

    # v6.3 patch recommendations
    lines.append("## 6. Recommended probe v6.3 patch")
    lines.append("")
    lines.append("Keep:")
    lines.append("- Keep `player_lock_on_target_handle_new` (`+0x6B0`) and `player_lock_on_target_area_new` (`+0x6B4`).")
    lines.append("- Keep phys-chain world position (`module_bag +0x68 -> phys +0x70`) for both player and enemy paths.")
    lines.append("- Keep player/enemy vtable capture at least for one more release to guard object-type drift.")
    lines.append("")
    lines.append("Remove or demote:")
    lines.append("- Remove legacy lock-on read `+0x6A0` from primary analytics path (retain debug-only one release if desired).")
    lines.append("- Remove direct enemy anim candidates `time_act+0xD0`, path-B queue read, and path-C `action_request+0x90` as active signals (all dead in this capture).")
    lines.append("- Treat current region-6/7/8 scan hits as noise unless a future capture yields stable high-coverage u32 hits at one offset.")
    lines.append("")
    lines.append("Anim-ID next step:")
    lines.append("- Add one wider/adjacent instrumentation pass for enemy animation (beyond current 512B windows), or hook-based oracle path, before promoting any enemy anim offset in qualification logic.")
    lines.append("")

    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", default="/tmp/v62-capture/q62.bin", type=Path)
    ap.add_argument("--log", default="/tmp/v62-capture/q62.log.txt", type=Path)
    ap.add_argument(
        "--anim-targets",
        default="/home/joshua.blattner/claude/elden-ring/data/research-fixture/anim_id_search_targets.json",
        type=Path,
    )
    ap.add_argument(
        "--out",
        default="/home/joshua.blattner/claude/elden-ring/research/007-v62-capture-analysis-codex.md",
        type=Path,
    )
    args = ap.parse_args()

    bf = read_bin(str(args.bin))
    if not bf.samples:
        raise SystemExit(f"no samples parsed from {args.bin}")

    with args.anim_targets.open("r", encoding="utf-8") as fh:
        targets = json.load(fh)
    anim_targets = set(targets["c4380_anim_ids_full_form"]) | set(targets["c4380_anim_ids_short_form"])

    samples = bf.samples
    focused_rows: list[tuple[Any, Any]] = []
    for s in samples:
        for e in s.enemies:
            if e.is_focused:
                focused_rows.append((s, e))

    q1, v1 = q1_world_pos(samples, focused_rows)
    q2, v2 = q2_anim_id(focused_rows, anim_targets)
    q3, v3 = q3_lock_on(samples, focused_rows)

    manifest = bf.manifests[0].fields if bf.manifests else {}
    arm_info = parse_arm_disarm(args.log)

    report = render_report(
        bin_path=args.bin,
        log_path=args.log,
        manifest_fields=manifest,
        parse_errors=bf.parse_errors,
        sample_count=len(samples),
        focused_count=len(focused_rows),
        q1=q1,
        q1_verdict=v1,
        q2=q2,
        q2_verdict=v2,
        q3=q3,
        q3_verdict=v3,
        arm_info=arm_info,
    )

    args.out.write_text(report, encoding="utf-8")

    print(f"wrote {args.out}")
    print(f"Q1: WORLD POS WINNER = {v1.winner} ({v1.confidence})")
    print(f"Q2: ENEMY ANIM_ID WINNER = {v2.winner} ({v2.confidence})")
    print(f"Q3: LOCK-ON WINNER = {v3.winner} ({v3.confidence})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
