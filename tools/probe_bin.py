"""parry-tell-probe v6 .bin / .csv reader.

Library used by tools/qualify_oracle.py and tools/analyze_discovery.py.

Wire format mirrors the v6 probe source (probe/probe.cpp). Byte-for-byte
match required — if you change one, change the other.

Records on disk:
    SRD0 record:    u32 magic = 0x53524430 ('SRD0')
                    u32 length
                    u8[length] payload (a 'PTS0' sample)
    MAN0 record:    u32 magic = 0x304E414D ('MAN0')
                    u32 length
                    u8[length] manifest text (key=value lines, plus
                                              optional config_dump section)

Sample payload starts with 'PTS0' header (132 bytes), followed by enemy
records (96-byte header each, plus zero or more region records).
"""

from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass, field
from typing import Iterable, Iterator, Optional

# ---------------------------------------------------------------------------
# Magic numbers (must match probe/probe.cpp)
# ---------------------------------------------------------------------------

SRD0_MAGIC = 0x30445253  # 'SRD0' little-endian (matches probe.cpp SAMPLE_RECORD_MAGIC)
MAN0_MAGIC = 0x304E414D  # 'MAN0' little-endian (matches probe.cpp MANIFEST_MAGIC)
PTS0_MAGIC = 0x30535450  # 'PTS0' little-endian (matches probe.cpp SAMPLE_MAGIC)

# Region IDs from RegionId enum in probe.cpp
REGION_NAMES = {
    0: "chr_ins_root",
    1: "module_bag",
    2: "time_act_module",
    3: "time_act_focus",
    4: "time_act_child",
    5: "ai_struct",
}

CAPTURE_MODE_NAMES = {1: "smoke", 2: "qualification", 3: "discovery"}
FOCUS_REASON_NAMES = {
    0: "none",
    1: "lock_on",
    2: "boss_bar_0",
    3: "qualification_nearest",
}
ENEMY_CLASS_NAMES = {0: "focused", 1: "top", 2: "lesser"}


# ---------------------------------------------------------------------------
# Dataclasses
# ---------------------------------------------------------------------------


@dataclass
class Manifest:
    raw_text: str
    fields: dict[str, str] = field(default_factory=dict)

    @classmethod
    def parse(cls, text: str) -> "Manifest":
        m = cls(raw_text=text)
        in_config_dump = False
        config_dump_lines: list[str] = []
        for line in text.splitlines():
            if line == "config_dump_begin":
                in_config_dump = True
                continue
            if line == "config_dump_end":
                in_config_dump = False
                continue
            if in_config_dump:
                config_dump_lines.append(line)
                continue
            if "=" in line:
                k, _, v = line.partition("=")
                m.fields[k.strip()] = v.strip()
        if config_dump_lines:
            m.fields["__config_dump__"] = "\n".join(config_dump_lines)
        return m

    def __getitem__(self, key: str) -> str:
        return self.fields[key]

    def get(self, key: str, default: Optional[str] = None) -> Optional[str]:
        return self.fields.get(key, default)


@dataclass
class RegionRecord:
    region_id: int
    has_child_offset: int
    payload_offset: int
    payload_len: int
    child_source_offset_in_time_act: int
    source_chain: int
    region_base_abs: int
    payload: bytes

    @property
    def region_name(self) -> str:
        return REGION_NAMES.get(self.region_id, f"region_{self.region_id}")


@dataclass
class EnemyRecord:
    chr_ins_abs: int
    handle: int
    field_at_0x038: int
    field_at_0x060: int
    field_at_0x064: int
    field_at_0x068: int
    field_at_0x06C: int
    field_at_0x080: int
    field_at_0x1E8: int
    anim_id: int
    anim_time: tuple[float, float, float, float]
    in_lock_on: bool
    in_boss_bar: bool
    in_roster: bool
    enemy_class: int
    is_focused: bool
    focus_reason: int
    region_count: int
    regions: list[RegionRecord] = field(default_factory=list)

    @property
    def class_name(self) -> str:
        return ENEMY_CLASS_NAMES.get(self.enemy_class, f"class_{self.enemy_class}")


@dataclass
class Sample:
    schema_version: int
    frame: int
    ts_ms_rel: int
    mode: int
    truncated: bool
    wcm_ptr: int
    module_base_eldenring: int
    player_chr_ins: int
    player_anim_id: int
    player_anim_time: tuple[float, float, float, float]
    player_pos: tuple[float, float, float]
    player_lock_on_target_handle: int
    boss_bar_handles: tuple[int, int, int]
    focused_enemy_handle: int
    focus_reason: int
    enemy_record_count: int
    adaptive_step: int
    enemies: list[EnemyRecord] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Low-level binary reader
# ---------------------------------------------------------------------------


class ParseError(Exception):
    pass


class _Cursor:
    __slots__ = ("buf", "pos", "end")

    def __init__(self, buf: bytes, pos: int = 0, end: Optional[int] = None) -> None:
        self.buf = buf
        self.pos = pos
        self.end = end if end is not None else len(buf)

    def need(self, n: int, what: str) -> None:
        if self.pos + n > self.end:
            raise ParseError(
                f"truncated {what}: need {n} bytes at pos {self.pos}, "
                f"end {self.end}"
            )

    def u8(self) -> int:
        self.need(1, "u8")
        v = self.buf[self.pos]
        self.pos += 1
        return v

    def u16(self) -> int:
        self.need(2, "u16")
        v = struct.unpack_from("<H", self.buf, self.pos)[0]
        self.pos += 2
        return v

    def u32(self) -> int:
        self.need(4, "u32")
        v = struct.unpack_from("<I", self.buf, self.pos)[0]
        self.pos += 4
        return v

    def u64(self) -> int:
        self.need(8, "u64")
        v = struct.unpack_from("<Q", self.buf, self.pos)[0]
        self.pos += 8
        return v

    def f32(self) -> float:
        self.need(4, "f32")
        v = struct.unpack_from("<f", self.buf, self.pos)[0]
        self.pos += 4
        return v

    def take(self, n: int, what: str) -> bytes:
        self.need(n, what)
        b = bytes(self.buf[self.pos : self.pos + n])
        self.pos += n
        return b


def _parse_sample(payload: bytes) -> Sample:
    cur = _Cursor(payload)
    magic = cur.u32()
    if magic != PTS0_MAGIC:
        raise ParseError(
            f"bad sample magic: got 0x{magic:08X} expected 0x{PTS0_MAGIC:08X} (PTS0)"
        )
    schema_version = cur.u32()
    frame = cur.u64()
    ts_ms_rel = cur.u64()
    mode = cur.u8()
    truncated = bool(cur.u8())
    cur.take(6, "header reserved6")
    wcm_ptr = cur.u64()
    module_base = cur.u64()
    player_chr_ins = cur.u64()
    player_anim_id = cur.u32()
    player_anim_time = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
    player_pos = (cur.f32(), cur.f32(), cur.f32())
    player_lock = cur.u64()
    boss = (cur.u64(), cur.u64(), cur.u64())
    focused = cur.u64()
    focus_reason = cur.u8()
    enemy_count = cur.u8()
    adaptive_step = cur.u8()
    cur.u8()  # reserved2

    sample = Sample(
        schema_version=schema_version,
        frame=frame,
        ts_ms_rel=ts_ms_rel,
        mode=mode,
        truncated=truncated,
        wcm_ptr=wcm_ptr,
        module_base_eldenring=module_base,
        player_chr_ins=player_chr_ins,
        player_anim_id=player_anim_id,
        player_anim_time=player_anim_time,
        player_pos=player_pos,
        player_lock_on_target_handle=player_lock,
        boss_bar_handles=boss,
        focused_enemy_handle=focused,
        focus_reason=focus_reason,
        enemy_record_count=enemy_count,
        adaptive_step=adaptive_step,
    )

    # 2026-05-11: the probe (v6.0 and v6.1) has a count-vs-payload mismatch
    # where enemy_count in the PTS0 header is the count of enemy SLOTS the
    # snapshot considered (including silently-skipped lesser-tier decimations
    # at probe.cpp:2047), while the bytes on disk only contain the records
    # that were actually emitted. So enemy_count is an UPPER BOUND, not an
    # exact count. Stop reading enemies when bytes run out instead of erroring.
    # When the probe is fixed to write the true count this loop will still
    # work correctly; the early-exit is just defensive.
    for _ in range(enemy_count):
        if cur.pos >= len(cur.buf):
            break
        try:
            sample.enemies.append(_parse_enemy(cur))
        except ParseError:
            # Partial enemy record (probe truncated mid-write). Stop here;
            # caller can inspect sample.enemy_record_count vs len(enemies) to
            # detect the discrepancy if it matters.
            break
    return sample


def _parse_enemy(cur: _Cursor) -> EnemyRecord:
    chr_ins = cur.u64()
    handle = cur.u64()
    f038 = cur.u32()
    f060 = cur.u32()
    f064 = cur.u32()
    f068 = cur.u32()
    f06C = cur.u32()
    f080 = cur.u32()
    f1E8 = cur.u32()
    anim_id = cur.u32()
    anim_t = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
    in_lock = bool(cur.u8())
    in_boss = bool(cur.u8())
    in_ros = bool(cur.u8())
    cls = cur.u8()
    is_foc = bool(cur.u8())
    foc_reason = cur.u8()
    reg_count = cur.u8()
    cur.u8()  # reserved
    cur.take(24, "enemy header pad")  # padding to ENEMY_HEADER_BYTES = 96

    enemy = EnemyRecord(
        chr_ins_abs=chr_ins,
        handle=handle,
        field_at_0x038=f038,
        field_at_0x060=f060,
        field_at_0x064=f064,
        field_at_0x068=f068,
        field_at_0x06C=f06C,
        field_at_0x080=f080,
        field_at_0x1E8=f1E8,
        anim_id=anim_id,
        anim_time=anim_t,
        in_lock_on=in_lock,
        in_boss_bar=in_boss,
        in_roster=in_ros,
        enemy_class=cls,
        is_focused=is_foc,
        focus_reason=foc_reason,
        region_count=reg_count,
    )

    for _ in range(reg_count):
        enemy.regions.append(_parse_region(cur))
    return enemy


def _parse_region(cur: _Cursor) -> RegionRecord:
    region_id = cur.u8()
    has_child = cur.u8()
    payload_off = cur.u16()
    payload_len = cur.u16()
    child_src = cur.u16()
    src_chain = cur.u32()
    region_base = cur.u64()
    payload = cur.take(payload_len, f"region {region_id} payload")
    return RegionRecord(
        region_id=region_id,
        has_child_offset=has_child,
        payload_offset=payload_off,
        payload_len=payload_len,
        child_source_offset_in_time_act=child_src,
        source_chain=src_chain,
        region_base_abs=region_base,
        payload=payload,
    )


# ---------------------------------------------------------------------------
# High-level streaming reader
# ---------------------------------------------------------------------------


@dataclass
class BinFile:
    path: str
    manifests: list[Manifest] = field(default_factory=list)
    samples: list[Sample] = field(default_factory=list)
    raw_bytes: int = 0
    record_count: int = 0
    parse_errors: list[str] = field(default_factory=list)


def read_bin(path: str, *, lazy: bool = False) -> BinFile:
    """Read one .bin file (or rotated bin like foo.bin.001) into memory.

    Set lazy=True to skip parsing sample payloads (just count records).
    Useful for quick sanity checks on a fresh capture.
    """
    bf = BinFile(path=path)
    with open(path, "rb") as fh:
        data = fh.read()
    bf.raw_bytes = len(data)
    pos = 0
    while pos + 8 <= len(data):
        magic, length = struct.unpack_from("<II", data, pos)
        pos += 8
        if pos + length > len(data):
            bf.parse_errors.append(
                f"truncated record at byte {pos - 8}: "
                f"length {length} exceeds remaining {len(data) - pos}"
            )
            break
        payload = data[pos : pos + length]
        pos += length
        bf.record_count += 1
        if magic == MAN0_MAGIC:
            try:
                bf.manifests.append(Manifest.parse(payload.decode("utf-8", errors="replace")))
            except Exception as exc:  # pragma: no cover (defensive)
                bf.parse_errors.append(f"manifest parse failed: {exc}")
        elif magic == SRD0_MAGIC:
            if lazy:
                continue
            try:
                bf.samples.append(_parse_sample(payload))
            except ParseError as exc:
                bf.parse_errors.append(
                    f"sample parse failed at record {bf.record_count}: {exc}"
                )
        else:
            bf.parse_errors.append(
                f"unknown record magic 0x{magic:08X} at byte {pos - 8 - length}"
            )
    return bf


def read_session(base_path: str) -> Iterator[BinFile]:
    """Yield BinFile objects for one session.

    `base_path` is the path WITHOUT the .bin suffix (e.g.
    "C:/Projects/elden-ring/logs/qualification-20260508-145300").
    Yields the primary .bin first, then .bin.001, .bin.002, ... if present.
    """
    primary = base_path + ".bin"
    if os.path.exists(primary):
        yield read_bin(primary)
    idx = 1
    while True:
        rotated = f"{base_path}.bin.{idx:03d}"
        if not os.path.exists(rotated):
            break
        yield read_bin(rotated)
        idx += 1


def all_samples(base_path: str) -> Iterator[Sample]:
    """Convenience: yield every Sample across rotated .bin files in order."""
    for bf in read_session(base_path):
        yield from bf.samples


# ---------------------------------------------------------------------------
# CLI: dump a .bin to stdout for inspection
# ---------------------------------------------------------------------------


def _human_handle(h: int) -> str:
    if h == 0xFFFFFFFFFFFFFFFF:
        return "<sentinel>"
    if h == 0:
        return "<zero>"
    return f"0x{h:016X}"


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: probe_bin.py <session.bin>", file=sys.stderr)
        return 2
    bf = read_bin(argv[1])
    print(f"# {bf.path}")
    print(f"# {bf.raw_bytes} bytes, {bf.record_count} records, "
          f"{len(bf.manifests)} manifests, {len(bf.samples)} samples")
    if bf.parse_errors:
        print(f"# {len(bf.parse_errors)} parse errors:")
        for err in bf.parse_errors[:5]:
            print(f"#   {err}")
        if len(bf.parse_errors) > 5:
            print(f"#   ... {len(bf.parse_errors) - 5} more")
    for i, m in enumerate(bf.manifests):
        print(f"# manifest[{i}]:")
        for k, v in m.fields.items():
            if k == "__config_dump__":
                print(f"#   config_dump: {len(v)} bytes")
                continue
            print(f"#   {k} = {v}")
    if bf.samples:
        s = bf.samples[0]
        print(f"# first sample: frame={s.frame} ts={s.ts_ms_rel}ms "
              f"mode={CAPTURE_MODE_NAMES.get(s.mode, s.mode)} "
              f"enemies={s.enemy_record_count} truncated={s.truncated}")
        print(f"#   focused_handle={_human_handle(s.focused_enemy_handle)} "
              f"reason={FOCUS_REASON_NAMES.get(s.focus_reason, s.focus_reason)}")
        print(f"#   adaptive_step={s.adaptive_step}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
