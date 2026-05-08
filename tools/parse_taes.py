#!/usr/bin/env python3
"""Extract Elden Ring TAE parry data from WitchyBND anim XML files."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import xml.etree.ElementTree as ET
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_SOURCE = Path("/mnt/station-projects/elden-ring/chr-extracted")
DEFAULT_OUTPUT = Path("data/parry_data.json")
DEFAULT_SUMMARY = Path("data/parry_data_summary.md")
SAMPLE_FIXTURES = Path("data/sample-fixtures")

SCHEMA_VERSION = "1.0.0"
PARSER_VERSION = "1.0.0"
TEMPLATE_SOURCE = "WitchyBND v3.0.0.1 / TAE.Template.ER.xml"
GAME_VERSION_MARKER = (
    "Elden Ring 1.16 + SOTE (Steam install Game/eldenring.exe mtime 2025-08-21)"
)
EXTRACTION_METHOD = "UXM Selective Unpack 2.4.2 + WitchyBND v3.0.0.1"

CHR_ACTION_FLAG_VALUES = [5, 24, 49, 55, 63, 71, 73, 78, 79, 86, 94, 102, 119, 132, 143]
FUTURE_CHR_ACTION_FLAG_SET = set(CHR_ACTION_FLAG_VALUES) - {5}

INT_FIELDS_BY_TYPE = {
    1: {
        "AttackType": "attack_type",
        "AttackIndex": "attack_index",
        "BehaviorJudgeID": "behavior_judge_id",
        "DirectionType": "direction_type",
        "Source": "source",
        "StateInfo": "state_info",
    },
    2: {
        "DummyPolyID": "dummy_poly_id",
        "AttackIndex": "attack_index",
        "BehaviorJudgeID": "behavior_judge_id",
        "AttachmentType": "attachment_type",
        "Source": "source",
        "StateInfo": "state_info",
    },
}

BINDER_SUFFIX = "-anibnd-dcx-wanibnd"
CHAR_ID_RE = re.compile(r"^(c\d{4}(?:_[A-Za-z0-9]+)*)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    parser.add_argument("--sentinel-only", action="store_true")
    parser.add_argument("--single-char")
    parser.add_argument("--workers", type=int, default=min(8, os.cpu_count() or 1))
    return parser.parse_args()


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def resolve_repo_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return Path.cwd() / path


def find_anim_files(source: Path) -> list[Path]:
    return sorted(source.rglob("*anim-*.xml"), key=lambda p: str(p))


def discover_anim_files(source: Path, progress: bool) -> list[Path]:
    character_dirs = list_character_dirs(source)
    if not character_dirs:
        files = find_anim_files(source)
        if progress:
            print(f"Discovered {len(files)} anim XML files under {source}.", file=sys.stderr, flush=True)
        return files

    files: list[Path] = []
    total_dirs = len(character_dirs)
    for index, character_dir in enumerate(character_dirs, 1):
        files.extend(find_anim_files(character_dir))
        if progress and (index == 1 or index % 25 == 0 or index == total_dirs):
            print(
                f"Discovered anim XMLs in {index}/{total_dirs} character dirs "
                f"({len(files)} files so far).",
                file=sys.stderr,
                flush=True,
            )
    return sorted(files, key=lambda p: str(p))


def list_character_dirs(source: Path) -> list[Path]:
    if not source.exists():
        return []
    if source.name.endswith(BINDER_SUFFIX):
        return [source]
    return sorted(
        [path for path in source.iterdir() if path.is_dir() and path.name.endswith(BINDER_SUFFIX)],
        key=lambda p: p.name,
    )


def choose_single_char_source(source: Path, single_char: str) -> Path:
    if source.name.startswith(single_char) and source.name.endswith(BINDER_SUFFIX):
        return source
    matches = [
        path
        for path in list_character_dirs(source)
        if path.name == f"{single_char}{BINDER_SUFFIX}"
        or path.name.startswith(f"{single_char}_")
        and path.name.endswith(BINDER_SUFFIX)
    ]
    if not matches:
        raise FileNotFoundError(f"No character directory found for {single_char} under {source}")
    if len(matches) > 1:
        exact = [path for path in matches if path.name == f"{single_char}{BINDER_SUFFIX}"]
        if exact:
            return exact[0]
    return matches[0]


def character_id_from_path(anim_path: Path) -> str:
    for parent in anim_path.parents:
        name = parent.name
        if name.endswith(BINDER_SUFFIX):
            return name[: -len(BINDER_SUFFIX)]
    match = CHAR_ID_RE.match(anim_path.name)
    if match:
        return match.group(1)
    for parent in anim_path.parents:
        match = CHAR_ID_RE.match(parent.name)
        if match:
            return match.group(1)
    return "unknown"


def strip_hkt(name: str) -> str:
    name = name.strip()
    if name.lower().endswith(".hkt"):
        return name[:-4]
    return name


def event_params(event: ET.Element) -> dict[str, str]:
    params: dict[str, str] = {}
    params_node = event.find("params")
    if params_node is None:
        return params
    for param in params_node.findall("param"):
        name = param.attrib.get("name")
        value = param.attrib.get("value")
        if name is not None and value is not None:
            params[name] = value
    return params


def int_param(params: dict[str, str], source_name: str) -> int | None:
    value = params.get(source_name)
    if value is None:
        return None
    return int(value)


def bool_param(params: dict[str, str], source_name: str) -> bool | None:
    value = params.get(source_name)
    if value is None:
        return None
    if value == "True":
        return True
    if value == "False":
        return False
    raise ValueError(f"Invalid bool value for {source_name}: {value}")


def parse_time(event: ET.Element, tag: str) -> float:
    value = event.findtext(tag)
    if value is None:
        raise ValueError(f"Missing {tag}")
    return float(value)


def sort_events(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return sorted(events, key=lambda item: item["start_time"])


def parse_anim_file(anim_path: Path) -> tuple[str, str, dict[str, list[dict[str, Any]]]]:
    root = ET.parse(anim_path).getroot()
    name = root.findtext("name")

    character_id = character_id_from_path(anim_path)
    animation_id = strip_hkt(name) if name else anim_path.stem
    result: dict[str, list[dict[str, Any]]] = {
        "parry_windows": [],
        "attack_behaviors": [],
        "bullet_behaviors": [],
        "chr_action_flags": [],
    }

    events_node = root.find("events")
    if events_node is None:
        return character_id, animation_id, result

    for event in events_node.findall("event"):
        type_text = event.findtext("type")
        if type_text is None:
            continue
        event_type = int(type_text)
        if event_type not in (0, 1, 2):
            continue

        start_time = parse_time(event, "startTime")
        end_time = parse_time(event, "endTime")
        params = event_params(event)

        if event_type == 0:
            flag_type = int_param(params, "FlagType")
            if flag_type is None:
                continue
            if flag_type == 5:
                result["parry_windows"].append(
                    {
                        "start_time": start_time,
                        "end_time": end_time,
                        "frame_30": [round(start_time * 30), round(end_time * 30)],
                        "frame_60": [round(start_time * 60), round(end_time * 60)],
                    }
                )
            if flag_type in FUTURE_CHR_ACTION_FLAG_SET:
                result["chr_action_flags"].append(
                    {"flag_type": flag_type, "start_time": start_time, "end_time": end_time}
                )
            continue

        if event_type == 1:
            item: dict[str, Any] = {"start_time": start_time, "end_time": end_time}
            for source_name, json_name in INT_FIELDS_BY_TYPE[1].items():
                value = int_param(params, source_name)
                if value is not None:
                    item[json_name] = value
            result["attack_behaviors"].append(item)
            continue

        if event_type == 2:
            item = {"start_time": start_time, "end_time": end_time}
            for source_name, json_name in INT_FIELDS_BY_TYPE[2].items():
                value = int_param(params, source_name)
                if value is not None:
                    item[json_name] = value
            enable = bool_param(params, "Enable")
            if enable is not None:
                item["enable"] = enable
            result["bullet_behaviors"].append(item)

    for key in result:
        result[key] = sort_events(result[key])
    return character_id, animation_id, result


def has_tracked_events(anim_data: dict[str, list[dict[str, Any]]]) -> bool:
    return any(anim_data[key] for key in anim_data)


def merge_animation_data(
    target: dict[str, list[dict[str, Any]]], source: dict[str, list[dict[str, Any]]]
) -> None:
    for key, events in source.items():
        if not events:
            continue
        target[key].extend(events)
        target[key] = sort_events(target[key])


def empty_totals() -> dict[str, int]:
    return {
        "characters": 0,
        "characters_with_parry_data": 0,
        "animations_scanned": 0,
        "animations_with_parry_windows": 0,
        "parry_windows": 0,
        "attack_behaviors": 0,
        "bullet_behaviors": 0,
        "chr_action_flag_events": 0,
        "parse_failures": 0,
    }


def parse_anim_file_safe(anim_path: Path) -> dict[str, Any]:
    try:
        character_id, animation_id, anim_data = parse_anim_file(anim_path)
    except Exception as exc:
        return {"ok": False, "path": str(anim_path), "error": str(exc)}
    return {
        "ok": True,
        "path": str(anim_path),
        "character_id": character_id,
        "animation_id": animation_id,
        "anim_data": anim_data,
    }


def parse_anim_files(anim_files: list[Path], workers: int, progress: bool) -> list[dict[str, Any]]:
    if workers <= 1 or len(anim_files) <= 1:
        parsed = []
        for index, path in enumerate(anim_files, 1):
            parsed.append(parse_anim_file_safe(path))
            if progress and (index % 5000 == 0 or index == len(anim_files)):
                print(f"Parsed {index}/{len(anim_files)} anim XML files.", file=sys.stderr, flush=True)
        return parsed
    with ProcessPoolExecutor(max_workers=workers) as executor:
        parsed = []
        for index, item in enumerate(executor.map(parse_anim_file_safe, anim_files, chunksize=32), 1):
            parsed.append(item)
            if progress and (index % 5000 == 0 or index == len(anim_files)):
                print(f"Parsed {index}/{len(anim_files)} anim XML files.", file=sys.stderr, flush=True)
        return parsed


def character_dirs_without_anim_files(source_root: Path, anim_files: list[Path]) -> list[str]:
    character_dirs = list_character_dirs(source_root)
    if not character_dirs:
        return []
    ids_with_anims = {character_id_from_path(path) for path in anim_files}
    zeroes = []
    for character_dir in character_dirs:
        character_id = character_dir.name[: -len(BINDER_SUFFIX)]
        if character_id not in ids_with_anims:
            zeroes.append(character_dir.name)
    return zeroes


def build_database(
    anim_files: list[Path], extracted_at: str, source_root: Path, workers: int, progress: bool
) -> tuple[dict[str, Any], dict[str, Any]]:
    characters: dict[str, dict[str, Any]] = {}
    totals = empty_totals()
    parse_failures: list[dict[str, str]] = []
    parry_counts: Counter[str] = Counter()
    durations: list[dict[str, Any]] = []

    totals["characters"] = len(list_character_dirs(source_root))
    if totals["characters"] == 0:
        totals["characters"] = len({character_id_from_path(path) for path in anim_files})

    for parsed in parse_anim_files(anim_files, workers, progress):
        totals["animations_scanned"] += 1
        if not parsed["ok"]:
            totals["parse_failures"] += 1
            parse_failures.append({"path": parsed["path"], "error": parsed["error"]})
            continue
        character_id = parsed["character_id"]
        animation_id = parsed["animation_id"]
        anim_data = parsed["anim_data"]

        totals["parry_windows"] += len(anim_data["parry_windows"])
        totals["attack_behaviors"] += len(anim_data["attack_behaviors"])
        totals["bullet_behaviors"] += len(anim_data["bullet_behaviors"])
        totals["chr_action_flag_events"] += len(anim_data["chr_action_flags"])
        if anim_data["parry_windows"]:
            totals["animations_with_parry_windows"] += 1
            parry_counts[character_id] += len(anim_data["parry_windows"])
            for window in anim_data["parry_windows"]:
                duration = window["end_time"] - window["start_time"]
                if abs(duration - (2.0 / 30.0)) > 0.002:
                    durations.append(
                        {
                            "character_id": character_id,
                            "animation_id": animation_id,
                            "start_time": window["start_time"],
                            "end_time": window["end_time"],
                            "duration": duration,
                        }
                    )

        if not has_tracked_events(anim_data):
            continue

        character = characters.setdefault(character_id, {"animations": {}})
        animations = character["animations"]
        if animation_id in animations:
            merge_animation_data(animations[animation_id], anim_data)
        else:
            animations[animation_id] = anim_data

    totals["characters_with_parry_data"] = len(parry_counts)

    sorted_characters: dict[str, Any] = {}
    for character_id in sorted(characters):
        animations = characters[character_id]["animations"]
        sorted_characters[character_id] = {
            "animations": {animation_id: animations[animation_id] for animation_id in sorted(animations)}
        }

    database = {
        "_meta": {
            "schema_version": SCHEMA_VERSION,
            "parser_version": PARSER_VERSION,
            "extracted_at": extracted_at,
            "tae_template_source": TEMPLATE_SOURCE,
            "game_version_marker": GAME_VERSION_MARKER,
            "extraction_method": EXTRACTION_METHOD,
            "fps_assumption": 30,
            "extraction_rules": {
                "parry_window_event_type": 0,
                "parry_window_flag_type": 5,
                "attack_behavior_event_type": 1,
                "bullet_behavior_event_type": 2,
                "chr_action_flag_extracted_values": CHR_ACTION_FLAG_VALUES,
            },
            "totals": totals,
        },
        "characters": sorted_characters,
    }

    diagnostics = {
        "parse_failures": parse_failures,
        "parry_counts": parry_counts,
        "duration_anomalies": sorted(durations, key=lambda item: abs(item["duration"] - (2.0 / 30.0)), reverse=True),
        "zero_anim_dirs": character_dirs_without_anim_files(source_root, anim_files),
    }
    return database, diagnostics


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    path.write_text(text, encoding="utf-8")


def write_summary(
    path: Path,
    database: dict[str, Any],
    diagnostics: dict[str, Any],
    source: Path,
    wall_clock_seconds: float,
) -> None:
    totals = database["_meta"]["totals"]
    parry_counts: Counter[str] = diagnostics["parry_counts"]
    parse_failures = diagnostics["parse_failures"]
    duration_anomalies = diagnostics["duration_anomalies"]
    zeroes = diagnostics["zero_anim_dirs"]

    lines = [
        "# Parry Data Summary",
        "",
        f"- Extracted at: {database['_meta']['extracted_at']}",
        f"- Source: {source}",
        f"- Wall-clock time: {wall_clock_seconds:.2f}s",
        f"- Total characters: {totals['characters']}",
        f"- Total parry windows: {totals['parry_windows']}",
        f"- Total attack behaviors: {totals['attack_behaviors']}",
        f"- Total bullet behaviors: {totals['bullet_behaviors']}",
        "",
        "## Top 20 Characters by Parry-Window Count",
        "",
    ]

    if parry_counts:
        for character_id, count in parry_counts.most_common(20):
            lines.append(f"- {character_id}: {count}")
    else:
        lines.append("- None")

    lines.extend(["", "## Top 5 Anomalies", ""])
    anomalies: list[str] = []
    if parse_failures:
        for failure in parse_failures[:5]:
            anomalies.append(f"Parse failure: {failure['path']} ({failure['error']})")
    if duration_anomalies:
        for anomaly in duration_anomalies[:5]:
            anomalies.append(
                "Unusual parry-window duration: "
                f"{anomaly['character_id']} {anomaly['animation_id']} "
                f"{anomaly['duration']:.7f}s "
                f"({anomaly['start_time']} -> {anomaly['end_time']})"
            )
    if zeroes:
        for directory in zeroes[:5]:
            anomalies.append(f"Character directory with zero anim XMLs: {directory}")
    if anomalies:
        for anomaly in anomalies[:5]:
            lines.append(f"- {anomaly}")
    else:
        lines.append("- None")

    lines.extend(
        [
            "",
            "## Parse Totals",
            "",
            f"- Animations scanned: {totals['animations_scanned']}",
            f"- Animations with parry windows: {totals['animations_with_parry_windows']}",
            f"- ChrActionFlag events: {totals['chr_action_flag_events']}",
            f"- Parse failures: {totals['parse_failures']}",
            f"- Character dirs with zero anim XMLs: {len(zeroes)}",
            "",
        ]
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def validate_fixture_sentinel(database: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    characters = database["characters"]

    c4100 = characters.get("c4100", {}).get("animations", {})
    positive = c4100.get("a000_003000")
    if not positive:
        errors.append("Missing c4100 a000_003000 positive fixture")
    else:
        parry_windows = positive["parry_windows"]
        attack_behaviors = positive["attack_behaviors"]
        if len(parry_windows) != 1:
            errors.append(f"Positive fixture expected 1 parry window, got {len(parry_windows)}")
        elif parry_windows[0]["start_time"] != 0.6333333 or parry_windows[0]["end_time"] != 0.7:
            errors.append(f"Positive fixture parry window mismatch: {parry_windows[0]}")
        if len(attack_behaviors) != 1:
            errors.append(f"Positive fixture expected 1 attack behavior, got {len(attack_behaviors)}")
        else:
            attack = attack_behaviors[0]
            if (
                attack["start_time"] != 0.6333333
                or attack["end_time"] != 0.73333335
                or attack.get("behavior_judge_id") != 110
            ):
                errors.append(f"Positive fixture attack behavior mismatch: {attack}")

    negative = c4100.get("a000_000020")
    if negative and negative["parry_windows"]:
        errors.append("c4100 a000_000020 expected 0 parry windows")
    c0000 = characters.get("c0000", {}).get("animations", {})
    player_negative = c0000.get("a000_000000") or c0000.get("a000_000000.hkx")
    if player_negative and player_negative["parry_windows"]:
        errors.append("c0000 player fixture expected 0 parry windows")

    return errors


def validate_single_char(database: dict[str, Any], single_char: str) -> list[str]:
    totals = database["_meta"]["totals"]
    errors = []
    if single_char == "c4100":
        if totals["animations_with_parry_windows"] != 28:
            errors.append(
                "c4100 expected 28 animations with FlagType=5 events, "
                f"got {totals['animations_with_parry_windows']}"
            )
        if totals["parry_windows"] != 31:
            errors.append(f"c4100 expected 31 parry-window events, got {totals['parry_windows']}")
        for character in database["characters"].values():
            for animation_id, anim in character["animations"].items():
                for window in anim["parry_windows"]:
                    duration = window["end_time"] - window["start_time"]
                    if abs(duration - (2.0 / 30.0)) > 0.002:
                        errors.append(
                            f"c4100 unusual parry-window duration in {animation_id}: {duration:.7f}s"
                        )
                        return errors
    return errors


def run_parse(source: Path, output: Path, summary: Path, single_char: str | None, workers: int) -> int:
    started = time.perf_counter()
    extracted_at = utc_now_iso()
    parse_source = choose_single_char_source(source, single_char) if single_char else source
    anim_files = discover_anim_files(parse_source, True)
    database, diagnostics = build_database(anim_files, extracted_at, parse_source, workers, True)
    wall_clock_seconds = time.perf_counter() - started

    if single_char:
        errors = validate_single_char(database, single_char)
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
            return 1

    write_json(output, database)
    write_summary(summary, database, diagnostics, parse_source, wall_clock_seconds)
    totals = database["_meta"]["totals"]
    print(
        "Parsed "
        f"{totals['animations_scanned']} animations from {parse_source} in {wall_clock_seconds:.2f}s; "
        f"{totals['parry_windows']} parry windows, "
        f"{totals['attack_behaviors']} attack behaviors, "
        f"{totals['bullet_behaviors']} bullet behaviors, "
        f"{totals['parse_failures']} parse failures; workers={workers}."
    )
    return 0


def run_sentinel(output: Path, summary: Path, workers: int) -> int:
    started = time.perf_counter()
    extracted_at = utc_now_iso()
    source = resolve_repo_path(SAMPLE_FIXTURES)
    anim_files = find_anim_files(source)
    database, diagnostics = build_database(anim_files, extracted_at, source, workers, False)
    errors = validate_fixture_sentinel(database)
    wall_clock_seconds = time.perf_counter() - started
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    write_json(output, database)
    write_summary(summary, database, diagnostics, source, wall_clock_seconds)
    print("Sentinel fixtures passed.")
    return 0


def main() -> int:
    args = parse_args()
    source = resolve_repo_path(args.source)
    output = resolve_repo_path(args.output)
    summary = resolve_repo_path(args.summary)
    if args.sentinel_only:
        return run_sentinel(output, summary, args.workers)
    return run_parse(source, output, summary, args.single_char, args.workers)


if __name__ == "__main__":
    raise SystemExit(main())
