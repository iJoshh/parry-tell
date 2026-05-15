#!/usr/bin/env python3
"""
analyze_target_field.py — Phase 4.0 Gate 0.B target-of-attention research.

Scans the v7.0-target-scan probe capture for candidate "boss is targeting which
entity" fields inside the AI struct head, AI bag head, and module bag head.

The hypothesis: somewhere inside one of those regions, there is an 8-byte slot
that equals one of the local player's known identifiers (ChrIns* OR
FieldInsHandle) when the boss is actively attacking Josh, and equals
something else when the boss is attacking a summon / co-op partner / NPC.

We don't know the offset. We DO know the player's ChrIns* every sample (sample
header `player_chr_ins`). For FieldInsHandle equality we can use the player's
lock-on TARGET handle as a proxy (it's a handle in the same encoding space),
and we surface u64 slots that match either pattern.

Solo captures will narrow the field substantially: any offset that NEVER
equals the player pointer/handle during active boss attacks can be ruled out.
Final disambiguation between "target-of-attention" vs "last-attacker" vs
"lock-on of the local player viewed from the boss" requires multi-target
captures (Mimic, co-op partner), but that comes later. Tonight, solo data
alone is worth running.

USAGE:
  # Copy capture from SMB to local disk FIRST (CLAUDE.md SMB perf rule):
  cp /mnt/station-projects/elden-ring/logs/<sess>.bin /tmp/work.bin
  python3 tools/analyze_target_field.py /tmp/work

  # Output: /tmp/work.target_field_report.md  (top candidates ranked)

The analyzer is read-only and idempotent. It rewrites the report each run.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

# Reuse probe_bin parser
sys.path.insert(0, str(Path(__file__).parent))
from probe_bin import (  # noqa: E402
    REGION_NAMES,
    EnemyRecord,
    RegionRecord,
    Sample,
    all_samples,
    read_bin,
)

# Regions we scan for target candidates. Order matters only for the report.
TARGET_REGION_IDS: set[int] = {
    11,  # ai_struct_head    -- PRIMARY territory per research plan
    10,  # ai_bag_head       -- surrounds the +0xC0 ai_struct slot
    12,  # module_bag_head   -- +0x38 ai, +0x50 talk, +0x58 event, +0x80 action_req
    5,   # ai_struct (legacy +0xE000..+0xF000 fallback)
    # v7.2 expanded coverage:
    13,  # ai_struct_mid     -- +0x1000..+0x4000 (broader target hunting)
    14,  # action_req_head   -- ActionRequest module body
    # v7.3 deep AI struct scan + module-bag-member-bodies:
    9,   # module_bag_member -- body of every module in module_bag head (incl. AI module @+0x38)
    16,  # ai_struct_far     -- +0x4000..+0x8000
    17,  # ai_struct_deep    -- +0x8000..+0xC000
    18,  # ai_struct_tgt     -- +0xC000..+0xE000 (TargetingSystem @0xC480, SpEffectObserveComp @0xDBF0)
    # NOTE: region 15 (player_chr_ins) is NOT scanned for target candidates
    # — it's the PLAYER's struct, not the boss's. We exclude it from the
    # boss target-of-attention scan so its contents don't pollute the
    # ranking. A separate analyzer pass could use it to identify Josh's
    # friendly summons' handles for noise-rejection in multi-target captures.
}

# Scan stride for u64 (handle/pointer) candidates and u32 (entity-id) candidates.
SCAN_STRIDE_U64 = 8
SCAN_STRIDE_U32 = 4

# Junk pointer filter: addresses below USER_PTR_MIN are sentinel/null/garbage.
USER_PTR_MIN = 0x10000
USER_PTR_MAX = 0x7FFFFFFFFFFF  # plausible Windows user-space upper bound

# FieldInsHandle sentinel ("no target"): all bits set.
HANDLE_SENTINEL = 0xFFFFFFFFFFFFFFFF

# Minimum observation count before a slot can score positively. Short-region
# slots and rare-focus samples shouldn't outrank well-supported candidates.
MIN_OBSERVATIONS_FOR_RANKING = 10


def is_plausible_user_ptr(v: int) -> bool:
    return USER_PTR_MIN <= v <= USER_PTR_MAX


@dataclass
class SlotStats:
    """Aggregated stats for one (region_id, region_relative_offset) slot
    across samples. The offset is RELATIVE TO THE START OF THE STRUCT, i.e.
    `region.payload_offset + scan_off_within_payload`. This matters because
    region 5 captures from `ai_struct + 0xE000`, so a scan offset of 0x40
    inside that payload is structure offset 0xE040."""

    region_id: int
    struct_offset: int  # offset from the START of the captured struct
    samples_observed: int = 0
    samples_nonzero: int = 0
    samples_plausible_ptr: int = 0
    samples_equals_player_ptr: int = 0
    samples_equals_player_handle: int = 0
    samples_equals_player_lock_handle: int = 0
    # v7.3: track self-reference rate. A slot that always equals the
    # focused enemy's own chr_ins is a "this is me" pointer (owner field
    # in a module, parent backref, etc.) — useful diagnostic but not a
    # target candidate. Surfaced so the report can disqualify them.
    samples_equals_focused_chr_ins: int = 0
    distinct_values: set[int] = field(default_factory=set)
    transitions: int = 0
    last_value: Optional[int] = None
    # u32 stats (separate 4-byte pass for entity-id-shaped fields):
    samples_u32_observed: int = 0
    distinct_u32_values: set[int] = field(default_factory=set)
    transitions_u32: int = 0
    last_u32_value: Optional[int] = None

    def observe_u64(
        self,
        value: int,
        player_chr_ins: int,
        player_handle: int,
        player_lock_handle: int,
        focused_chr_ins: int,
    ) -> None:
        self.samples_observed += 1
        if value != 0:
            self.samples_nonzero += 1
        if is_plausible_user_ptr(value):
            self.samples_plausible_ptr += 1
        if player_chr_ins and value == player_chr_ins:
            self.samples_equals_player_ptr += 1
        if player_handle and player_handle != HANDLE_SENTINEL \
                and value == player_handle:
            self.samples_equals_player_handle += 1
        if player_lock_handle and player_lock_handle != HANDLE_SENTINEL \
                and value == player_lock_handle:
            self.samples_equals_player_lock_handle += 1
        if focused_chr_ins and value == focused_chr_ins:
            self.samples_equals_focused_chr_ins += 1
        if len(self.distinct_values) < 64:
            self.distinct_values.add(value)
        if self.last_value is not None and value != self.last_value:
            self.transitions += 1
        self.last_value = value

    def observe_u32(self, value: int) -> None:
        self.samples_u32_observed += 1
        if len(self.distinct_u32_values) < 64:
            self.distinct_u32_values.add(value)
        if self.last_u32_value is not None and value != self.last_u32_value:
            self.transitions_u32 += 1
        self.last_u32_value = value

    @property
    def equals_player_ptr_rate(self) -> float:
        if self.samples_observed == 0:
            return 0.0
        return self.samples_equals_player_ptr / self.samples_observed

    @property
    def equals_player_handle_rate(self) -> float:
        if self.samples_observed == 0:
            return 0.0
        return self.samples_equals_player_handle / self.samples_observed

    @property
    def equals_player_lock_handle_rate(self) -> float:
        if self.samples_observed == 0:
            return 0.0
        return self.samples_equals_player_lock_handle / self.samples_observed

    @property
    def plausible_ptr_rate(self) -> float:
        if self.samples_observed == 0:
            return 0.0
        return self.samples_plausible_ptr / self.samples_observed

    @property
    def transition_rate(self) -> float:
        if self.samples_observed <= 1:
            return 0.0
        return self.transitions / (self.samples_observed - 1)


def slot_key(region_id: int, struct_offset: int) -> tuple[int, int]:
    return (region_id, struct_offset)


def scan_focused_enemy(
    enemy: EnemyRecord,
    sample: Sample,
    stats_u64: dict[tuple[int, int], SlotStats],
    stats_u32: dict[tuple[int, int], SlotStats],
) -> int:
    """Scan all target regions of a focused enemy. Returns number of slots
    observed in this sample (for diagnostics)."""
    observed = 0
    for region in enemy.regions:
        if region.region_id not in TARGET_REGION_IDS:
            continue
        payload = region.payload
        plen = len(payload)
        base_off = region.payload_offset  # offset of payload start within struct
        # u64 pass: 8-byte aligned slots within payload
        for off in range(0, plen - 7, SCAN_STRIDE_U64):
            value = struct.unpack_from("<Q", payload, off)[0]
            struct_off = base_off + off
            key = slot_key(region.region_id, struct_off)
            slot = stats_u64.get(key)
            if slot is None:
                slot = SlotStats(region_id=region.region_id, struct_offset=struct_off)
                stats_u64[key] = slot
            slot.observe_u64(
                value,
                sample.player_chr_ins,
                sample.player_handle,
                sample.player_lock_on_target_handle,
                enemy.chr_ins_abs,
            )
            observed += 1
        # u32 pass: 4-byte aligned slots within payload
        for off in range(0, plen - 3, SCAN_STRIDE_U32):
            value = struct.unpack_from("<I", payload, off)[0]
            struct_off = base_off + off
            key = slot_key(region.region_id, struct_off)
            slot = stats_u32.get(key)
            if slot is None:
                slot = SlotStats(region_id=region.region_id, struct_offset=struct_off)
                stats_u32[key] = slot
            slot.observe_u32(value)
    return observed


def iter_samples(base_path: Path) -> Iterable[Sample]:
    """Yield every Sample across rotated .bin files for the given session base."""
    yield from all_samples(str(base_path))


def run_analysis(base_path: Path) -> dict:
    print(f"[analyze_target_field] loading session base {base_path} ...", file=sys.stderr)
    stats_u64: dict[tuple[int, int], SlotStats] = {}
    stats_u32: dict[tuple[int, int], SlotStats] = {}

    samples_total = 0
    focused_count = 0
    skipped_player_as_enemy = 0
    has_v70_region_count = 0
    for sample in iter_samples(base_path):
        samples_total += 1
        if not sample.player_chr_ins:
            continue
        for enemy in sample.enemies:
            if not enemy.is_focused:
                continue
            # v7.3: skip "player picked as focused enemy" samples — they
            # pollute the ranking because the slot at (e.g.) action_req+0x08
            # which holds the owner pointer will equal player_chr_ins when
            # the focused entity IS the player. v7.3 probe fixes the root
            # cause (player_chr_ins now in friendlyPCs[] for exclusion);
            # this analyzer filter handles v7.2 and earlier captures too.
            if enemy.chr_ins_abs == sample.player_chr_ins:
                skipped_player_as_enemy += 1
                continue
            focused_count += 1
            has_v70 = any(
                r.region_id in (10, 11, 12) for r in enemy.regions
            )
            if has_v70:
                has_v70_region_count += 1
            scan_focused_enemy(enemy, sample, stats_u64, stats_u32)

    print(
        f"  total samples: {samples_total}, focused records: {focused_count} "
        f"({has_v70_region_count} with v7.0 regions, "
        f"{skipped_player_as_enemy} skipped player-as-focused)",
        file=sys.stderr,
    )
    if has_v70_region_count == 0:
        print(
            "  WARNING: no v7.0 regions found. This capture predates "
            "v7.0-target-scan probe; only region 5 (legacy ai_struct tail) "
            "will be analyzed. Rebuild probe and re-capture for full "
            "coverage of head territory.",
            file=sys.stderr,
        )

    return {
        "session_base": str(base_path),
        "samples_total": samples_total,
        "focused_total": focused_count,
        "focused_with_v70_regions": has_v70_region_count,
        "skipped_player_as_enemy": skipped_player_as_enemy,
        "stats_u64": stats_u64,
        "stats_u32": stats_u32,
    }


def score_u64(s: SlotStats, max_observed: int) -> float:
    """Score a u64 slot's likelihood of being target-of-attention.

    Two equality signals: equals_player_ptr (target stores ChrIns*) and
    equals_player_handle (target stores FieldInsHandle). Either is a
    positive answer; we take the MAX, scaled by coverage. The v7.0/v7.2
    captures showed pointer-equality is 0% across all slots in real-boss
    samples, so handle-equality is the expected positive in v7.1+.
    v7.3 adds a self-reference penalty: a slot that mostly equals the
    focused enemy's own chr_ins is an owner/parent backref, not a target.
    """
    if s.samples_observed < MIN_OBSERVATIONS_FOR_RANKING:
        return 0.0
    coverage = s.samples_observed / max(1, max_observed)
    # Equality signal: max of ptr-match-rate and handle-match-rate.
    eq_rate = max(s.equals_player_ptr_rate, s.equals_player_handle_rate)
    base = eq_rate * 100.0 * coverage
    # Pointer-shape bonus: a slot that looks like a user-space pointer the
    # vast majority of the time is more interesting than one that is mostly
    # zero/junk.
    shape_bonus = s.plausible_ptr_rate * 5.0 * coverage
    # v7.3 self-reference penalty: a slot that equals the focused enemy's
    # own chr_ins is structurally an owner pointer (e.g., action_req+0x08
    # caused our v7.2 false positive). Strongly disqualify.
    self_ref_rate = (s.samples_equals_focused_chr_ins / s.samples_observed) \
        if s.samples_observed else 0.0
    self_ref_penalty = -50.0 * self_ref_rate * coverage
    # Variety bonus / penalty
    dv = len(s.distinct_values)
    if dv < 2:
        variety = -3.0  # constant: definitely not a target
    elif dv > 32:
        variety = -1.0  # too noisy: likely a counter
    else:
        variety = 1.0
    return base + shape_bonus + self_ref_penalty + variety


def score_u32(s: SlotStats, max_observed: int) -> float:
    """Score a u32 slot's likelihood of being a live entity-id field.
    No positive equality signal in solo data; rely on transition + variety,
    coverage-weighted."""
    if s.samples_u32_observed < MIN_OBSERVATIONS_FOR_RANKING:
        return 0.0
    coverage = s.samples_u32_observed / max(1, max_observed)
    dv = len(s.distinct_u32_values)
    tr = s.transitions_u32 / max(1, s.samples_u32_observed - 1)
    if dv < 2:
        return -1.0
    if dv > 32:
        return tr * 0.5 * coverage
    return (tr * 2.0 + (1.0 / dv)) * coverage


def rank_candidates(
    stats: dict[tuple[int, int], SlotStats], kind: str
) -> list[SlotStats]:
    candidates = list(stats.values())
    # Coverage normalization base: the max observations any slot got in
    # this dataset, separately for u64 and u32 passes.
    if kind == "u64":
        max_obs = max((s.samples_observed for s in candidates), default=1)
        candidates.sort(key=lambda s: score_u64(s, max_obs), reverse=True)
    else:
        max_obs = max(
            (s.samples_u32_observed for s in candidates), default=1
        )
        candidates.sort(key=lambda s: score_u32(s, max_obs), reverse=True)
    return candidates


def format_report(result: dict, top_n: int = 30) -> str:
    stats_u64 = result["stats_u64"]
    stats_u32 = result["stats_u32"]
    u64_ranked = rank_candidates(stats_u64, "u64")
    u32_ranked = rank_candidates(stats_u32, "u32")

    lines: list[str] = []
    lines.append("# Phase 4.0 Gate 0.B -- Target-of-Attention Field Analysis")
    lines.append("")
    lines.append(f"- Session base: `{result['session_base']}`")
    lines.append(f"- Total samples: {result['samples_total']}")
    lines.append(
        f"- Focused-enemy records scanned: {result['focused_total']}"
    )
    lines.append(
        f"- Focused records with v7.0 regions: "
        f"{result['focused_with_v70_regions']}"
    )
    lines.append(
        f"- Player-as-focused-enemy samples SKIPPED (v7.2 friendly-exclusion "
        f"bug): {result.get('skipped_player_as_enemy', 0)}"
    )
    lines.append("")
    lines.append("## Region map")
    lines.append("")
    lines.append("| Region ID | Name | Captured range | Probe version |")
    lines.append("|---:|---|---|---|")
    lines.append("| 5  | ai_struct (legacy)  | ai_struct + 0xE000..0xF000 | v6.x+ |")
    lines.append("| 10 | ai_bag_head         | ai_bag    + 0x0000..0x0400 | v7.0+ |")
    lines.append("| 11 | ai_struct_head      | ai_struct + 0x0000..0x1000 | v7.0+ |")
    lines.append("| 12 | module_bag_head     | module_bag + 0x0000..0x0100 | v7.0+ |")
    lines.append("| 13 | ai_struct_mid       | ai_struct + 0x1000..0x4000 | v7.2+ |")
    lines.append("| 14 | action_req_head     | ActionRequest + 0x0000..0x0200 | v7.2+ |")
    lines.append("| 15 | player_chr_ins      | player ChrIns + 0x0000..0x0800 (excluded from target scan) | v7.2+ |")
    lines.append("")
    lines.append(
        "Offsets shown below are STRUCT-RELATIVE (from the start of the "
        "named struct), not payload-relative. A 'region=ai_struct, "
        "offset=0xE040' candidate means the slot at `ai_struct + 0xE040`, "
        "captured inside region 5's payload at byte 0x40."
    )
    lines.append("")
    lines.append("## What this looks for")
    lines.append("")
    lines.append(
        "For every 8-byte slot inside the AI struct head, AI bag head, "
        "module bag head, and legacy AI struct tail, this tool counts how "
        "often the slot equaled the local player's `ChrIns*` across all "
        "focused-enemy samples. A slot that frequently equals the player "
        "is a candidate target-of-attention field. Solo captures narrow "
        "the field; final confirmation needs multi-target captures (Mimic "
        "or co-op partner)."
    )
    lines.append("")
    lines.append(
        "A separate u32-slot pass tracks 4-byte slots that transition "
        "frequently (likely live entity IDs rather than constants or "
        "counters). These are weaker candidates without a positive match "
        "signal, but worth surfacing as a secondary list."
    )
    lines.append("")
    lines.append("## Top u64 (handle/pointer) candidates")
    lines.append("")
    lines.append(
        f"Ranked by `(equals_player_ptr_rate * 100 + plausible_ptr_rate * 5) "
        f"* coverage + variety_bonus`, where coverage is "
        f"`samples_observed / max_observed_in_dataset`. Slots with fewer than "
        f"{MIN_OBSERVATIONS_FOR_RANKING} observations are excluded from "
        f"ranking. A score > 10 is worth investigating."
    )
    lines.append("")
    u64_max_obs = max(
        (s.samples_observed for s in u64_ranked), default=1
    )
    lines.append(
        "| Rank | Region | StructOff | Samples | =Ptr % | =Hdl % | "
        "=LockHdl % | =Self % | PtrLike % | Distinct | Trans | Score |"
    )
    lines.append("|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for i, slot in enumerate(u64_ranked[:top_n], start=1):
        region_name = REGION_NAMES.get(
            slot.region_id, f"region_{slot.region_id}"
        )
        s = score_u64(slot, u64_max_obs)
        self_ref_rate = (slot.samples_equals_focused_chr_ins
                         / slot.samples_observed * 100.0) \
            if slot.samples_observed else 0.0
        lines.append(
            f"| {i} | {region_name} | 0x{slot.struct_offset:04X} | "
            f"{slot.samples_observed} | "
            f"{slot.equals_player_ptr_rate * 100:.1f} | "
            f"{slot.equals_player_handle_rate * 100:.1f} | "
            f"{slot.equals_player_lock_handle_rate * 100:.1f} | "
            f"{self_ref_rate:.1f} | "
            f"{slot.plausible_ptr_rate * 100:.1f} | "
            f"{len(slot.distinct_values)} | "
            f"{slot.transitions} | "
            f"{s:.2f} |"
        )
    lines.append("")
    lines.append("## Top u32 (entity-id / 4-byte) candidates")
    lines.append("")
    lines.append(
        "Ranked by transition activity (no positive-match signal in solo "
        "capture). Low-distinct + high-transitions = strong candidate for "
        "an entity-id-shaped field."
    )
    lines.append("")
    lines.append(
        "| Rank | Region | StructOff | Samples | Distinct | Trans | "
        "TransRate |"
    )
    lines.append("|---:|---|---:|---:|---:|---:|---:|")
    for i, slot in enumerate(u32_ranked[:top_n], start=1):
        if slot.samples_u32_observed <= 1:
            continue
        region_name = REGION_NAMES.get(
            slot.region_id, f"region_{slot.region_id}"
        )
        tr_rate = slot.transitions_u32 / max(1, slot.samples_u32_observed - 1)
        lines.append(
            f"| {i} | {region_name} | 0x{slot.struct_offset:04X} | "
            f"{slot.samples_u32_observed} | "
            f"{len(slot.distinct_u32_values)} | "
            f"{slot.transitions_u32} | "
            f"{tr_rate * 100:.1f}% |"
        )
    lines.append("")
    lines.append("## Interpretation guide")
    lines.append("")
    lines.append("**Strong signal for a u64 slot:**")
    lines.append("")
    lines.append("- `=Ptr %` > 20% — target field stores the player's ChrIns*")
    lines.append("- `=Hdl %` > 20% — target field stores the player's FieldInsHandle (v7.1+ captures only)")
    lines.append("- 2-16 distinct values (live, not a counter)")
    lines.append("- Located in `ai_struct_head` or `module_bag_head`")
    lines.append("")
    lines.append(
        "Pointer-equality and handle-equality are alternative shapes for the "
        "same underlying field. Either signal (whichever fires) is the "
        "positive answer; the analyzer takes the max of the two when scoring."
    )
    lines.append("")
    lines.append("**Disqualifying signs:**")
    lines.append("")
    lines.append("- `Distinct` = 1: slot is a constant; cannot be a target.")
    lines.append(
        "- `Distinct` > 32: slot is too noisy; probably a counter, RNG, "
        "or pointer churn."
    )
    lines.append(
        "- `PtrLike %` near 0% AND `=Hdl %` = 0: slot is junk u64 noise, "
        "not a structured identifier."
    )
    lines.append("")
    lines.append(
        "The `=LockHdl %` column matches against the PLAYER'S lock-on TARGET "
        "handle (the boss's handle when Josh is locked onto it). This is "
        "NOT Josh's own handle — useful only as a sanity check on shape, "
        "not as a positive match for target-of-attention."
    )
    lines.append("")
    lines.append(
        "**v7.0 capture compatibility:** v7.0 probe captures wrote 0 in the "
        "player_handle slot, so `=Hdl %` will be 0 across the board on those "
        "captures. v7.1+ captures populate it. The analyzer is forward-"
        "compatible: it scores ptr-equality on v7.0 data and adds handle-"
        "equality on v7.1+."
    )
    lines.append("")
    lines.append("**Required next step before locking a candidate:**")
    lines.append("")
    lines.append(
        "Run multi-target captures (Mimic Tear, NPC summon, co-op partner) "
        "and re-run this tool. A real target-of-attention field will keep "
        "matching the player when the boss IS attacking Josh and will switch "
        "to non-player values when the boss attacks the summon/partner. A "
        "false candidate (e.g., 'who's locked onto me' from the boss's POV) "
        "will only match when Josh has the boss locked, regardless of who "
        "the boss is actually attacking."
    )
    return "\n".join(lines) + "\n"


def resolve_session_base(arg: Path) -> Path:
    """Accept either `/tmp/work` (bare base) or `/tmp/work.bin` (full path).
    Returns the bare base path (no .bin suffix) so `all_samples` / `read_session`
    can find rotated shards."""
    s = str(arg)
    if s.endswith(".bin"):
        return Path(s[:-4])
    return arg


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 4.0 Gate 0.B target-of-attention field analyzer"
    )
    parser.add_argument(
        "capture_base",
        type=Path,
        help="Path to capture WITHOUT .bin suffix (e.g., /tmp/work for "
        "/tmp/work.bin + rotated shards). A path ending in .bin is also "
        "accepted and the suffix is stripped.",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=30,
        help="Top N candidates per table (default 30)",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=None,
        help="Output report path (default: <capture_base>.target_field_report.md)",
    )
    args = parser.parse_args()

    base = resolve_session_base(args.capture_base)
    primary_bin = Path(str(base) + ".bin")
    if not primary_bin.is_file():
        print(
            f"error: {primary_bin} not found. Pass capture base path "
            f"without .bin suffix (or with .bin -- both work).",
            file=sys.stderr,
        )
        return 2

    result = run_analysis(base)
    report = format_report(result, top_n=args.top)

    if args.report is None:
        out = Path(str(base) + ".target_field_report.md")
    else:
        out = args.report
    out.write_text(report)
    print(f"[analyze_target_field] wrote {out}", file=sys.stderr)
    # Echo top of the report for quick eyeballing
    print("\n".join(report.splitlines()[:40]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
