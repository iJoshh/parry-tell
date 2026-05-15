#!/usr/bin/env python3
"""Build a compact runtime parry DB binary from data/parry_data.json.

Binary format (little-endian):

HEADER:
  [magic 4 bytes: 'PTPD']
  [version u16: 1]
  [meta_len u16]
  [meta_json utf-8 bytes]

BODY:
  [char_count u32]
  per char:
    [cid u32]
    [anim_count u32]
    per anim:
      [anim_id u32]      # full or short encoding key
      [window_count u16]
      per window:
        [open_s float32]
        [close_s float32]

Schema notes:
- Source anim IDs are keys like aXXX_YYYYYY.
- Canonical parse mirrors tools/qualify_oracle.py:
  full = int(XXX) * 1_000_000 + int(YYYYYY), short = int(YYYYYY).
- Emit BOTH encodings as lookup keys. If full == short, emit only once.
- Character keys are cNNNN; cids < 1000 are filtered out.
- Only animations with non-empty parry_windows are emitted.

Ambiguous short-form policy (Phase 4.1 safety):
- Some characters contain multiple source anims with the same short key
  (YYYYYY) but different full keys (XXXYYYYYY). Merging those windows under a
  single short key corrupts lookup timing.
- Build-time therefore DROPS ambiguous short-form keys entirely while keeping
  full-form keys intact.
- Motivation: runtime encoding shape varies by character and is discovered from
  capture data in tools/qualify_oracle.py:determine_anim_id_encoding; the build
  step has no capture context, so it must prefer "clean miss" over false cues.

Usage:
  python3 tools/build_parry_db.py \
    --input data/parry_data.json \
    --output data/parry_data.bin \
    --verbose
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path
from typing import Any

MAGIC = b"PTPD"
VERSION = 1
META_MAX_BYTES = 8192
U32_MAX = (1 << 32) - 1


class BuildError(Exception):
    """Fatal build error with user-facing text."""


def _parse_aid_str(aid_str: str) -> tuple[int, int] | None:
    """Canonical parse: aXXX_YYYYYY -> (full, short)."""
    if not aid_str.startswith("a"):
        return None
    body = aid_str[1:]
    if "_" not in body:
        return None
    try:
        prefix_str, suffix_str = body.split("_", 1)
        prefix = int(prefix_str)
        suffix = int(suffix_str)
    except ValueError:
        return None
    full = prefix * 1_000_000 + suffix
    return (full, suffix)


def warn(msg: str) -> None:
    print(f"WARN: {msg}", file=sys.stderr)


def repo_root() -> Path:
    # tools/build_parry_db.py -> repo root is one parent up from tools/
    return Path(__file__).resolve().parents[1]


def resolve_repo_path(root: Path, path_str: str) -> Path:
    p = Path(path_str)
    if p.is_absolute():
        return p
    return (root / p).resolve()


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Build compact parry DB binary from data/parry_data.json"
    )
    parser.add_argument(
        "--input",
        default=str(root / "data" / "parry_data.json"),
        help="Input JSON path (default: <repo>/data/parry_data.json)",
    )
    parser.add_argument(
        "--output",
        default=str(root / "data" / "parry_data.bin"),
        help="Output BIN path (default: <repo>/data/parry_data.bin)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print per-character emitted anim/window stats",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as fh:
            obj = json.load(fh)
    except FileNotFoundError as exc:
        raise BuildError(f"Input file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise BuildError(f"Invalid JSON in {path}: {exc}") from exc

    if not isinstance(obj, dict):
        raise BuildError("Top-level JSON must be an object")
    if "_meta" not in obj:
        raise BuildError("Input JSON missing required top-level '_meta' field")
    if "characters" not in obj:
        raise BuildError("Input JSON missing required top-level 'characters' field")
    if not isinstance(obj["characters"], dict):
        raise BuildError("Input JSON field 'characters' must be an object")
    return obj


def serialize_meta(meta_obj: Any) -> bytes:
    meta_json = json.dumps(meta_obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    meta_bytes = meta_json.encode("utf-8")
    if len(meta_bytes) > META_MAX_BYTES:
        raise BuildError(
            f"Serialized _meta is {len(meta_bytes)} bytes; exceeds {META_MAX_BYTES}-byte limit"
        )
    if len(meta_bytes) > 0xFFFF:
        raise BuildError(f"Serialized _meta is {len(meta_bytes)} bytes; exceeds u16 meta_len")
    return meta_bytes


def build_payload(
    data: dict[str, Any],
    *,
    verbose: bool,
) -> tuple[bytes, int, int, int, int, int, int, int, list[tuple[int, int, int]]]:
    chars_raw = data["characters"]

    total_numeric_cids = 0
    filtered_cid_lt_1000 = 0

    # Hold (cid_num, char_obj) for deterministic sort.
    candidate_chars: list[tuple[int, dict[str, Any]]] = []

    for cid_key, cdata in chars_raw.items():
        if not isinstance(cid_key, str) or not cid_key.startswith("c"):
            continue
        try:
            cid_num = int(cid_key[1:])
        except ValueError:
            continue
        if not isinstance(cdata, dict):
            continue

        total_numeric_cids += 1
        if cid_num < 1000:
            filtered_cid_lt_1000 += 1
            continue
        candidate_chars.append((cid_num, cdata))

    candidate_chars.sort(key=lambda row: row[0])

    emitted_chars: list[tuple[int, list[tuple[int, list[tuple[float, float]]]]]] = []
    verbose_stats: list[tuple[int, int, int]] = []

    total_anims = 0
    total_windows = 0
    dropped_short_keys_total = 0
    dropped_short_keys_chars = 0

    for cid_num, cdata in candidate_chars:
        anims_obj = cdata.get("animations")
        if not isinstance(anims_obj, dict):
            continue

        # Gather source anims that have non-empty parry_windows and valid aid parse.
        source_anims: list[tuple[int, int, str, list[dict[str, Any]]]] = []
        for aid_str, anim_data in anims_obj.items():
            if not isinstance(aid_str, str) or not isinstance(anim_data, dict):
                continue
            pw = anim_data.get("parry_windows")
            if not isinstance(pw, list) or len(pw) == 0:
                continue
            parsed = _parse_aid_str(aid_str)
            if parsed is None:
                continue
            full_id, short_id = parsed
            if full_id < 0 or full_id > U32_MAX:
                raise BuildError(
                    f"anim_id_full overflow for cid c{cid_num} aid '{aid_str}': {full_id}"
                )
            if short_id < 0 or short_id > U32_MAX:
                raise BuildError(
                    f"anim_id_short overflow for cid c{cid_num} aid '{aid_str}': {short_id}"
                )
            source_anims.append((full_id, short_id, aid_str, pw))

        # Deterministic source anim ordering per requirement.
        source_anims.sort(key=lambda row: (row[0], row[1]))

        # Build full and short maps separately.
        # Full map is always preserved.
        full_map: dict[int, list[tuple[float, float]]] = {}
        # Short map is conditionally emitted (dropped when ambiguous).
        short_map: dict[int, list[tuple[float, float]]] = {}
        # Track which source full-ids contribute to each short key so we can
        # detect ambiguous short-form collisions.
        short_sources: dict[int, set[int]] = {}
        ambiguous_short_ids: set[int] = set()

        for full_id, short_id, aid_str, windows_raw in source_anims:
            parsed_windows: list[tuple[float, float]] = []
            for idx, w in enumerate(windows_raw):
                if not isinstance(w, dict):
                    warn(
                        f"c{cid_num} {aid_str} window[{idx}] is not a dict; dropping"
                    )
                    continue
                if "start_time" not in w or "end_time" not in w:
                    warn(
                        f"c{cid_num} {aid_str} window[{idx}] missing start_time/end_time; dropping"
                    )
                    continue
                try:
                    open_s = float(w["start_time"])
                    close_s = float(w["end_time"])
                except (TypeError, ValueError):
                    warn(
                        f"c{cid_num} {aid_str} window[{idx}] non-numeric start/end "
                        f"({w.get('start_time')!r}, {w.get('end_time')!r}); dropping"
                    )
                    continue

                if open_s >= close_s:
                    warn(
                        f"c{cid_num} {aid_str} window[{idx}] start_time>=end_time "
                        f"({open_s} >= {close_s}); emitting as-is"
                    )
                if open_s < 0.0 or open_s > 60.0:
                    warn(
                        f"c{cid_num} {aid_str} window[{idx}] suspicious start_time "
                        f"{open_s} (expected 0..60s)"
                    )
                if close_s < 0.0 or close_s > 60.0:
                    warn(
                        f"c{cid_num} {aid_str} window[{idx}] suspicious end_time "
                        f"{close_s} (expected 0..60s)"
                    )
                parsed_windows.append((open_s, close_s))

            if not parsed_windows:
                continue

            # Full-form bucket: always preserved.
            full_bucket = full_map.get(full_id)
            if full_bucket is None:
                full_map[full_id] = list(parsed_windows)
            else:
                full_bucket.extend(parsed_windows)

            # Short-form bucket:
            # - if full==short, dedup by not creating a separate short entry
            # - if short collides across distinct source full-ids, mark
            #   ambiguous and drop that short key before emit.
            if full_id != short_id:
                sources = short_sources.setdefault(short_id, set())
                if sources and full_id not in sources:
                    ambiguous_short_ids.add(short_id)
                    prior_fulls = ", ".join(str(v) for v in sorted(sources))
                    warn(
                        f"c{cid_num} short anim_id u32={short_id} ambiguous: "
                        f"full {full_id} collides with prior full(s) [{prior_fulls}]; "
                        f"dropping short key for this character"
                    )
                sources.add(full_id)

                # If this short key equals an existing full key from some anim,
                # it is also ambiguous at runtime (numeric key shape-collides).
                if short_id in full_map:
                    ambiguous_short_ids.add(short_id)
                    warn(
                        f"c{cid_num} short anim_id u32={short_id} collides with "
                        f"existing full-form key; dropping short key for this character"
                    )

                short_bucket = short_map.get(short_id)
                if short_bucket is None:
                    short_map[short_id] = list(parsed_windows)
                else:
                    short_bucket.extend(parsed_windows)

        # Drop ambiguous short keys, including any windows previously added.
        for sid in ambiguous_short_ids:
            short_map.pop(sid, None)

        # Final emitted map = full-form map + non-ambiguous short-form map.
        merged = dict(full_map)
        for sid, wins in short_map.items():
            if sid in merged:
                # Defensive: should already have been marked ambiguous above.
                continue
            merged[sid] = wins

        if not merged:
            continue

        anim_entries = sorted(merged.items(), key=lambda kv: kv[0])
        char_window_count = 0
        for _anim_id, wins in anim_entries:
            char_window_count += len(wins)

        emitted_chars.append((cid_num, anim_entries))
        total_anims += len(anim_entries)
        total_windows += char_window_count
        if ambiguous_short_ids:
            dropped_short_keys_total += len(ambiguous_short_ids)
            dropped_short_keys_chars += 1
        verbose_stats.append((cid_num, len(anim_entries), char_window_count))

    out = bytearray()

    # Body begins with char_count.
    out.extend(struct.pack("<I", len(emitted_chars)))

    for cid_num, anim_entries in emitted_chars:
        out.extend(struct.pack("<II", cid_num, len(anim_entries)))
        for anim_id, windows in anim_entries:
            if anim_id < 0 or anim_id > U32_MAX:
                raise BuildError(f"Internal error: anim id out of u32 range: {anim_id}")
            if len(windows) > 0xFFFF:
                raise BuildError(
                    f"c{cid_num} anim_id {anim_id} has {len(windows)} windows; exceeds u16"
                )
            out.extend(struct.pack("<IH", anim_id, len(windows)))
            for open_s, close_s in windows:
                out.extend(struct.pack("<ff", open_s, close_s))

    if verbose:
        for cid_num, anim_count, window_count in verbose_stats:
            print(f"  c{cid_num}: anims={anim_count}, windows={window_count}")

    return (
        bytes(out),
        total_numeric_cids,
        filtered_cid_lt_1000,
        len(emitted_chars),
        total_anims,
        total_windows,
        dropped_short_keys_total,
        dropped_short_keys_chars,
        verbose_stats,
    )


def write_bin(path: Path, meta_bytes: bytes, body_bytes: bytes) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)

    header = struct.pack("<4sHH", MAGIC, VERSION, len(meta_bytes))
    blob = header + meta_bytes + body_bytes

    with path.open("wb") as fh:
        fh.write(blob)

    return len(blob)


def main() -> int:
    args = parse_args()
    root = repo_root()

    input_path = resolve_repo_path(root, args.input)
    output_path = resolve_repo_path(root, args.output)

    data = load_json(input_path)
    meta_bytes = serialize_meta(data["_meta"])

    (
        body_bytes,
        scanned_chars,
        filtered_chars,
        emitted_chars,
        total_anims,
        total_windows,
        dropped_short_keys_total,
        dropped_short_keys_chars,
        _verbose_stats,
    ) = build_payload(data, verbose=args.verbose)

    out_size = write_bin(output_path, meta_bytes, body_bytes)

    in_size = input_path.stat().st_size
    ratio = float("inf") if out_size == 0 else (in_size / out_size)

    # "Loaded" = passed the c<1000 filter; "scanned" = total c-prefixed
    # entries observed in the source. This makes the summary numbers
    # add up cleanly: loaded + filtered = scanned.
    loaded_chars = scanned_chars - filtered_chars

    print(
        f"Scanned {scanned_chars} characters from {input_path}; "
        f"loaded {loaded_chars} ({filtered_chars} filtered out: <1000 cid)"
    )
    print(
        f"Emitted {emitted_chars} characters with {total_anims} total anims, "
        f"{total_windows} total windows"
    )
    print(
        "Dropped "
        f"{dropped_short_keys_total} short-form keys due to ambiguity affecting "
        f"{dropped_short_keys_chars} characters; full-form keys preserved."
    )
    print(f"Wrote {output_path} ({out_size} bytes, {len(meta_bytes)} bytes meta)")
    print(f"Compression ratio: input/output = {ratio:.2f}x")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BuildError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
    except KeyboardInterrupt:
        print("ERROR: interrupted", file=sys.stderr)
        raise SystemExit(130)
