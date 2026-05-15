#!/usr/bin/env python3
"""Verify data/parry_data.bin is structurally valid and round-trips correctly.

This is a VM-side sanity check that lets us catch binary-format bugs
before the DLL ever tries to load the file in-game. The C++ loader in
probe/probe.cpp parses the same byte layout this script reads; if both
agree on counts, magic, and a sampled row, the format is consistent.

Usage:
    python3 tools/verify_parry_db.py [--bin data/parry_data.bin]
                                     [--json data/parry_data.json]

Exit code: 0 on success, non-zero on any structural failure.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path


PARRY_DB_MAGIC = b"PTPD"
PARRY_DB_VERSION = 1

# These limits MUST match the C++ loader in probe/probe.cpp. If you change
# them here, change them there too (and in build_parry_db.py).
MAX_FILE_SIZE = 16 * 1024 * 1024   # 16 MiB (loader rejects larger)
MAX_META_JSON_BYTES = 8191         # loader buffer is 8192 with null terminator
MAX_ANIM_COUNT_PER_CHAR = 100000   # loader sanity check
MAX_CHAR_COUNT = 10000             # loader sanity check


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_exact(buf: bytes, pos: int, n: int) -> tuple[bytes, int]:
    if pos + n > len(buf):
        raise ValueError(
            f"truncated read at pos={pos} want={n} have={len(buf) - pos}"
        )
    return buf[pos : pos + n], pos + n


def parse_bin(path: Path) -> dict:
    """Parse the binary into a dict mirroring the on-disk structure.

    Same byte order, same field widths, and same sanity guards as the
    C++ loader. A binary the loader will reject must not pass here.
    """
    fsize = path.stat().st_size
    if fsize <= 0:
        raise ValueError(f"empty or unreadable file: {path}")
    if fsize > MAX_FILE_SIZE:
        raise ValueError(
            f"file size {fsize} exceeds loader limit {MAX_FILE_SIZE}"
        )
    with path.open("rb") as fh:
        buf = fh.read()

    pos = 0
    magic, pos = read_exact(buf, pos, 4)
    if magic != PARRY_DB_MAGIC:
        raise ValueError(f"bad magic {magic!r} (expected {PARRY_DB_MAGIC!r})")

    version_bytes, pos = read_exact(buf, pos, 2)
    (version,) = struct.unpack("<H", version_bytes)
    if version != PARRY_DB_VERSION:
        raise ValueError(f"version {version} != {PARRY_DB_VERSION}")

    meta_len_bytes, pos = read_exact(buf, pos, 2)
    (meta_len,) = struct.unpack("<H", meta_len_bytes)
    if meta_len > MAX_META_JSON_BYTES:
        raise ValueError(
            f"meta_len {meta_len} exceeds loader limit {MAX_META_JSON_BYTES}"
        )
    meta_bytes, pos = read_exact(buf, pos, meta_len)
    meta_json = meta_bytes.decode("utf-8") if meta_len else ""

    char_count_bytes, pos = read_exact(buf, pos, 4)
    (char_count,) = struct.unpack("<I", char_count_bytes)
    if char_count > MAX_CHAR_COUNT:
        raise ValueError(
            f"char_count {char_count} exceeds loader limit {MAX_CHAR_COUNT}"
        )

    chars: dict[int, dict[int, list[tuple[float, float]]]] = {}

    for _ in range(char_count):
        hdr, pos = read_exact(buf, pos, 8)
        cid, anim_count = struct.unpack("<II", hdr)
        if cid in chars:
            raise ValueError(f"duplicate cid {cid} in body")
        if anim_count > MAX_ANIM_COUNT_PER_CHAR:
            raise ValueError(
                f"c{cid} anim_count {anim_count} exceeds loader limit "
                f"{MAX_ANIM_COUNT_PER_CHAR}"
            )
        anims: dict[int, list[tuple[float, float]]] = {}
        for _ in range(anim_count):
            ahdr, pos = read_exact(buf, pos, 6)
            anim_id, window_count = struct.unpack("<IH", ahdr)
            if anim_id in anims:
                raise ValueError(f"c{cid} duplicate anim_id {anim_id}")
            windows: list[tuple[float, float]] = []
            for _ in range(window_count):
                wbytes, pos = read_exact(buf, pos, 8)
                open_s, close_s = struct.unpack("<ff", wbytes)
                windows.append((open_s, close_s))
            anims[anim_id] = windows
        chars[cid] = anims

    return {
        "version": version,
        "meta_len": meta_len,
        "meta_json": meta_json,
        "char_count": char_count,
        "trailing_bytes": len(buf) - pos,
        "chars": chars,
        "file_size": len(buf),
    }


def parse_aid_str(aid: str) -> tuple[int, int] | None:
    """Same canonical parse as build_parry_db.py / qualify_oracle.py."""
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
    return (prefix * 1_000_000 + suffix, suffix)


def main() -> int:
    root = repo_root()
    p = argparse.ArgumentParser()
    p.add_argument("--bin", default=str(root / "data" / "parry_data.bin"))
    p.add_argument("--json", default=str(root / "data" / "parry_data.json"))
    args = p.parse_args()

    bin_path = Path(args.bin).resolve()
    json_path = Path(args.json).resolve()

    if not bin_path.exists():
        print(f"FAIL: bin file not found: {bin_path}", file=sys.stderr)
        return 1

    print(f"Parsing {bin_path} ({bin_path.stat().st_size} bytes)...")
    parsed = parse_bin(bin_path)

    print(f"  magic + version: OK (version={parsed['version']})")
    print(f"  meta_len: {parsed['meta_len']} bytes")
    print(f"  char_count: {parsed['char_count']}")
    print(f"  total_anims: {sum(len(a) for a in parsed['chars'].values())}")
    print(f"  total_windows: "
          f"{sum(len(w) for a in parsed['chars'].values() for w in a.values())}")
    print(f"  trailing_bytes: {parsed['trailing_bytes']}")

    if parsed["trailing_bytes"] != 0:
        print(f"WARN: {parsed['trailing_bytes']} trailing bytes (loader will "
              f"warn but not fail)")

    # Spot-check: meta_json should parse as JSON.
    try:
        meta_obj = json.loads(parsed["meta_json"]) if parsed["meta_json"] else {}
        print(f"  meta json parse: OK ({len(meta_obj)} top-level keys)")
    except json.JSONDecodeError as exc:
        print(f"FAIL: meta_json not valid JSON: {exc}", file=sys.stderr)
        return 1

    # Round-trip check: pick a known parry-window entry from the source
    # JSON and verify it appears in the binary with matching timing.
    if not json_path.exists():
        print(f"NOTE: skipping round-trip (json not found: {json_path})")
        return 0

    print(f"\nRound-trip vs {json_path}:")
    src = json.loads(json_path.read_text())
    src_chars = src.get("characters", {})

    # Walk every cid+aid in source JSON. For each that has parry_windows,
    # verify the binary has SOMETHING under either the full or short key
    # with matching window timings (within float tolerance). Short keys
    # may be dropped at build time due to ambiguity — that's OK; full
    # key must always be present.
    src_cid_count = 0
    src_anim_with_windows = 0
    missing_full = []
    mismatched_windows = []

    for cid_str, cdata in src_chars.items():
        if not cid_str.startswith("c"):
            continue
        try:
            cid_num = int(cid_str[1:])
        except ValueError:
            continue
        if cid_num < 1000:
            continue
        src_cid_count += 1
        anims = cdata.get("animations", {})
        if not isinstance(anims, dict):
            continue
        for aid_str, adata in anims.items():
            if not isinstance(adata, dict):
                continue
            pw = adata.get("parry_windows", [])
            if not pw:
                continue
            parsed_aid = parse_aid_str(aid_str)
            if parsed_aid is None:
                continue
            full_id, _short_id = parsed_aid
            src_anim_with_windows += 1

            bin_anims = parsed["chars"].get(cid_num, {})
            bin_windows = bin_anims.get(full_id)
            if bin_windows is None:
                missing_full.append((cid_num, aid_str, full_id))
                continue

            # Compare window timings (float32 precision, ~1us tolerance)
            src_windows = [
                (float(w.get("start_time", 0)), float(w.get("end_time", 0)))
                for w in pw
                if "start_time" in w and "end_time" in w
            ]
            # Build tool emits in source order; binary preserves it. Same
            # length, same values (within float32 rounding).
            if len(src_windows) != len(bin_windows):
                mismatched_windows.append(
                    (cid_num, aid_str, full_id, len(src_windows),
                     len(bin_windows))
                )
                continue
            for (s_o, s_c), (b_o, b_c) in zip(src_windows, bin_windows):
                if abs(s_o - b_o) > 1e-5 or abs(s_c - b_c) > 1e-5:
                    mismatched_windows.append(
                        (cid_num, aid_str, full_id, s_o, b_o, s_c, b_c)
                    )
                    break

    print(f"  source: {src_cid_count} cids loaded, "
          f"{src_anim_with_windows} anims with parry windows")
    print(f"  missing full-form entries in bin: {len(missing_full)}")
    print(f"  window timing mismatches: {len(mismatched_windows)}")

    if missing_full:
        print(f"\nFAIL: {len(missing_full)} source anims missing from bin",
              file=sys.stderr)
        for cid, aid, full in missing_full[:5]:
            print(f"  c{cid} {aid} (full={full})", file=sys.stderr)
        return 1
    if mismatched_windows:
        print(f"\nFAIL: {len(mismatched_windows)} window mismatches",
              file=sys.stderr)
        for row in mismatched_windows[:5]:
            print(f"  {row}", file=sys.stderr)
        return 1

    print("\n  round-trip: OK (every source anim with parry windows is in "
          "the binary under its full-form key, with matching timings)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
