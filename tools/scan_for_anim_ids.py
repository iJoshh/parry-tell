"""scan_for_anim_ids.py

Reusable helper for verifying ChrIns / TimeActModule struct offsets against
the captured research fixture in data/research-fixture/.

Two main jobs:

  1. Dump the anim_queue at TimeActModule + 0x20 (10 entries x 16 bytes, per
     vswarte/eldenring-rs CSChrTimeActModuleAnim struct) and the read_idx
     at +0xC4 / write_idx at +0xC0. Cross-check each entry's anim_id (i32@0)
     against anim_id_search_targets.json (both full and short encodings).

  2. Dump the pointer at chr_ins_root + 0x190 (ChrModuleBag pointer) and
     check it looks like a stable user-mode pointer across samples.

Pure stdlib (struct, json, pathlib, os, sys). Reusable for future captures.

Usage:
    python3 tools/scan_for_anim_ids.py [fixture_dir]

Default fixture_dir = data/research-fixture/.
"""
from __future__ import annotations

import json
import os
import struct
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Constants — offsets being verified
# ---------------------------------------------------------------------------

# Offset 1: ChrIns -> [+0x190 ChrModuleBag ptr] -> [+0x68 ChrPhysicsModule ptr] -> +0x70 Vector3
CHR_INS_MODULE_BAG_PTR = 0x190

# Offset 2: TimeActModule layout
TIME_ACT_ANIM_QUEUE_BEGIN = 0x20   # 10 entries x 16 bytes = 160 bytes
TIME_ACT_ANIM_QUEUE_ENTRIES = 10
TIME_ACT_ANIM_QUEUE_STRIDE = 16
TIME_ACT_WRITE_IDX = 0xC0          # u32
TIME_ACT_READ_IDX  = 0xC4          # u32

# A "user-mode" pointer on x64 Windows looks like 0x00007FFxxxxxxxxx (top 16
# bits zero, low 48 bits in the 0x7FF0_0000_0000 range for the typical heap
# arenas Elden Ring uses). We use a coarse sanity check here.
def _looks_like_usermode_ptr(p: int) -> bool:
    if p == 0:
        return False
    # Top 16 bits must be zero (canonical x64 user-mode address).
    if (p >> 48) != 0:
        return False
    # Must be at least page-aligned-ish (not a 4-byte int that happens to land
    # in the low gigabytes of the address space).
    if p < 0x10000:
        return False
    return True


# ---------------------------------------------------------------------------
# Fixture loading
# ---------------------------------------------------------------------------

def _find_region(sample_dir: Path, region_idx: int) -> Path:
    """Find region file by index prefix (e.g., region_00_...)."""
    prefix = f"region_{region_idx:02d}_"
    for p in sample_dir.iterdir():
        if p.name.startswith(prefix) and p.suffix == ".bin":
            return p
    raise FileNotFoundError(f"No region {region_idx:02d} in {sample_dir}")


def load_region(sample_dir: Path, region_idx: int) -> bytes:
    return _find_region(sample_dir, region_idx).read_bytes()


def load_anim_id_targets(fixture_dir: Path) -> tuple[set[int], set[int]]:
    """Return (full_form_set, short_form_set) of c4380 anim IDs."""
    p = fixture_dir / "anim_id_search_targets.json"
    d = json.loads(p.read_text())
    full = set(d.get("c4380_anim_ids_full_form", []))
    short = set(d.get("c4380_anim_ids_short_form", []))
    return full, short


# ---------------------------------------------------------------------------
# Per-sample analysis
# ---------------------------------------------------------------------------

def dump_anim_queue(time_act_module_bytes: bytes,
                    full_ids: set[int], short_ids: set[int]) -> dict:
    """Parse the anim_queue + read/write idx from a time_act_module region."""
    write_idx = struct.unpack_from("<I", time_act_module_bytes, TIME_ACT_WRITE_IDX)[0]
    read_idx  = struct.unpack_from("<I", time_act_module_bytes, TIME_ACT_READ_IDX )[0]

    entries = []
    for i in range(TIME_ACT_ANIM_QUEUE_ENTRIES):
        off = TIME_ACT_ANIM_QUEUE_BEGIN + i * TIME_ACT_ANIM_QUEUE_STRIDE
        anim_id, play_time, play_time2, anim_length = struct.unpack_from(
            "<i f f f", time_act_module_bytes, off,
        )
        match_full = anim_id in full_ids
        match_short = anim_id in short_ids
        match = match_full or match_short
        entries.append({
            "index": i,
            "offset": off,
            "anim_id": anim_id,
            "play_time": play_time,
            "play_time2": play_time2,
            "anim_length": anim_length,
            "match_full": match_full,
            "match_short": match_short,
            "match": match,
        })
    return {
        "write_idx": write_idx,
        "read_idx": read_idx,
        "entries": entries,
    }


def dump_module_bag_ptr(chr_ins_root_bytes: bytes) -> dict:
    """Read the u64 at ChrIns + 0x190 and sanity-check it."""
    ptr = struct.unpack_from("<Q", chr_ins_root_bytes, CHR_INS_MODULE_BAG_PTR)[0]
    return {
        "raw": ptr,
        "hex": f"0x{ptr:016X}",
        "is_usermode_ptr": _looks_like_usermode_ptr(ptr),
    }


# ---------------------------------------------------------------------------
# Report formatting
# ---------------------------------------------------------------------------

def _fmt_entry(e: dict) -> str:
    tag_parts = []
    if e["match_full"]:
        tag_parts.append(f"MATCH c4380_full:{e['anim_id']}")
    if e["match_short"]:
        tag_parts.append(f"MATCH c4380_short:{e['anim_id']}")
    if not tag_parts:
        tag_parts.append("no-match")
    tag = " ".join(tag_parts)
    return (
        f"  queue[{e['index']}] @+0x{e['offset']:02X}: "
        f"anim_id={e['anim_id']:>11d} ({tag}) "
        f"play_time={e['play_time']: .6f} "
        f"play_time2={e['play_time2']: .6f} "
        f"anim_length={e['anim_length']: .6f}"
    )


def format_sample_report(name: str, ts_ms: int, dump: dict) -> str:
    lines = [f"{name} (ts_ms_rel={ts_ms}):"]
    lines.append(f"  write_idx = {dump['write_idx']}")
    lines.append(f"  read_idx  = {dump['read_idx']}")
    for e in dump["entries"]:
        lines.append(_fmt_entry(e))
    # Read-pointed entry shortcut:
    ri = dump["read_idx"]
    if 0 <= ri < len(dump["entries"]):
        e = dump["entries"][ri]
        ok = "MATCH" if e["match"] else "NO-MATCH"
        lines.append(
            f"  >> queue[read_idx={ri}].anim_id={e['anim_id']} ({ok})"
        )
    else:
        lines.append(f"  >> read_idx={ri} is OUT OF BOUNDS (queue len = 10)")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(fixture_dir_arg: str | None = None) -> int:
    fixture_dir = Path(fixture_dir_arg or "data/research-fixture")
    if not fixture_dir.exists():
        # Try relative to the script's project root (one level up from tools/).
        here = Path(__file__).resolve().parent
        alt = here.parent / "data/research-fixture"
        if alt.exists():
            fixture_dir = alt
    if not fixture_dir.exists():
        print(f"ERROR: fixture dir not found: {fixture_dir}", file=sys.stderr)
        return 2

    full_ids, short_ids = load_anim_id_targets(fixture_dir)
    print(f"Loaded {len(full_ids)} full-form + {len(short_ids)} short-form c4380 anim IDs.")
    print()

    samples = [
        ("sample_early", 444433, fixture_dir / "sample_early_ts444433"),
        ("sample_mid",   456590, fixture_dir / "sample_mid_ts456590"),
        ("sample_late",  464004, fixture_dir / "sample_late_ts464004"),
    ]

    # ---- Offset 1: module bag ptr at chr_ins_root + 0x190 ----
    print("=" * 78)
    print("Offset 1: ChrIns + 0x190  (ChrModuleBag* expected)")
    print("=" * 78)
    bag_ptr_samples = []
    for name, ts, sdir in samples:
        if not sdir.exists():
            print(f"  {name}: MISSING sample dir {sdir}")
            continue
        try:
            root = load_region(sdir, 0)
        except FileNotFoundError as e:
            print(f"  {name}: {e}")
            continue
        info = dump_module_bag_ptr(root)
        bag_ptr_samples.append((name, info))
        print(f"  {name} (ts={ts}): ChrIns+0x190 = {info['hex']}  "
              f"usermode_ptr={info['is_usermode_ptr']}")
    if bag_ptr_samples:
        ptrs = [s[1]["raw"] for s in bag_ptr_samples]
        unique = set(ptrs)
        all_usermode = all(s[1]["is_usermode_ptr"] for s in bag_ptr_samples)
        print()
        print(f"  Unique pointer values across samples: {len(unique)}")
        print(f"  All look like user-mode ptrs:         {all_usermode}")
        if len(unique) == 1 and all_usermode:
            print("  VERDICT: stable user-mode ptr — Offset 1 entrypoint CONFIRMED.")
        elif all_usermode:
            print("  VERDICT: all user-mode but values differ — ptr exists but isn't fixed across captures (still confirms entrypoint).")
        else:
            print("  VERDICT: at least one sample doesn't look like a user-mode ptr — REVIEW.")
    print()

    # ---- Offset 2: anim queue at TimeActModule + 0x20 ----
    print("=" * 78)
    print("Offset 2: TimeActModule + 0x20  (anim_queue, 10 x 16 bytes)")
    print("           write_idx@0xC0, read_idx@0xC4")
    print("=" * 78)
    read_idx_hits = []
    for name, ts, sdir in samples:
        if not sdir.exists():
            print(f"{name}: MISSING sample dir {sdir}")
            print()
            continue
        try:
            ta = load_region(sdir, 2)
        except FileNotFoundError as e:
            print(f"{name}: {e}")
            print()
            continue
        dump = dump_anim_queue(ta, full_ids, short_ids)
        print(format_sample_report(name, ts, dump))
        print()
        ri = dump["read_idx"]
        if 0 <= ri < len(dump["entries"]):
            e = dump["entries"][ri]
            read_idx_hits.append({
                "sample": name,
                "ts": ts,
                "read_idx": ri,
                "anim_id": e["anim_id"],
                "match": e["match"],
            })
        else:
            read_idx_hits.append({
                "sample": name,
                "ts": ts,
                "read_idx": ri,
                "anim_id": None,
                "match": False,
                "oob": True,
            })

    # ---- Cross-sample summary ----
    print("=" * 78)
    print("Cross-sample queue[read_idx] summary:")
    print("=" * 78)
    matches = 0
    distinct_anim_ids = set()
    for h in read_idx_hits:
        oob = h.get("oob", False)
        tag = "OOB" if oob else ("MATCH" if h["match"] else "NO-MATCH")
        print(f"  {h['sample']} ts={h['ts']}: read_idx={h['read_idx']}, "
              f"anim_id={h['anim_id']} ({tag})")
        if not oob:
            distinct_anim_ids.add(h["anim_id"])
            if h["match"]:
                matches += 1
    print()
    print(f"  Total queue[read_idx] hits matching c4380 anim set: {matches} / {len(read_idx_hits)}")
    print(f"  Distinct anim_ids across samples: {len(distinct_anim_ids)} ({sorted(distinct_anim_ids)})")
    if matches >= 2:
        print("  VERDICT: Offset 2 CONFIRMED — read_idx points to a matching c4380 anim ID in ≥2 of 3 samples.")
    elif matches == 1:
        print("  VERDICT: Offset 2 PARTIAL — 1/3 samples matched; investigate.")
    else:
        print("  VERDICT: Offset 2 NOT CONFIRMED via read_idx — see full queue dumps for any matches at other indices.")

    # Also: are there ANY entries in the full queue across samples that match?
    print()
    print("Any-entry match scan (sanity — even if read_idx is wrong, queue should hold c4380 anims):")
    for name, ts, sdir in samples:
        if not sdir.exists():
            continue
        ta = load_region(sdir, 2)
        d = dump_anim_queue(ta, full_ids, short_ids)
        hits = [e for e in d["entries"] if e["match"]]
        print(f"  {name}: {len(hits)} of 10 entries match c4380 anim set "
              f"-> indices {[e['index'] for e in hits]} ids {[e['anim_id'] for e in hits]}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else None))
