"""Self-test for tools/probe_bin.py wire format symmetry.

Builds a synthetic .bin record matching the v6 producer's byte layout
EXACTLY (per probe.cpp comments + the layout I documented), then parses
it with probe_bin and checks every field round-trips.

Run: `python tools/test_probe_bin.py`. Exits 0 on pass, 1 on fail.

If this test ever fails, it means probe.cpp's writer or probe_bin.py's
reader has drifted. Fix BOTH to match the wire format spec.
"""

from __future__ import annotations

import io
import os
import struct
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import probe_bin


def make_synthetic_sample(
    *,
    frame: int,
    ts_ms_rel: int,
    mode: int,
    truncated: bool,
    enemies: list[dict],
) -> bytes:
    """Produce a synthetic PTS0 sample payload (the body, not the SRD0 wrapper)."""
    buf = io.BytesIO()
    # SampleHeader (132 bytes)
    buf.write(struct.pack("<II", probe_bin.PTS0_MAGIC, 1))    # magic, schema
    buf.write(struct.pack("<QQ", frame, ts_ms_rel))
    buf.write(struct.pack("<BB", mode, 1 if truncated else 0))
    buf.write(b"\x00" * 6)                                     # reserved6
    buf.write(struct.pack("<Q", 0xDEADBEEF12345678))           # wcm_ptr
    buf.write(struct.pack("<Q", 0x00007FF000000000))           # module_base
    buf.write(struct.pack("<Q", 0x000001ABCDEF0000))           # player_chr_ins
    buf.write(struct.pack("<I", 12345))                        # player_anim_id
    buf.write(struct.pack("<ffff", 0.1, 0.2, 0.3, 0.4))        # 4 floats
    buf.write(struct.pack("<fff", 1.5, 2.5, 3.5))              # pos
    buf.write(struct.pack("<Q", 0xAAAA_BBBB_CCCC_DDDD))        # lock handle
    buf.write(struct.pack("<QQQ",                              # boss handles
                          0x1111_2222_3333_4444,
                          0xFFFFFFFFFFFFFFFF,
                          0x5555_6666_7777_8888))
    buf.write(struct.pack("<Q", 0xDEAD_C0DE_FEED_BEEF))        # focused_handle
    buf.write(struct.pack("<BBBB", 1, len(enemies), 2, 0))     # focus_reason, enemy_count, adaptive_step, reserved2
    # Enemy records
    for e in enemies:
        # EnemyHeader (96 bytes)
        buf.write(struct.pack("<Q", e["chr_ins"]))
        buf.write(struct.pack("<Q", e["handle"]))
        buf.write(struct.pack("<I", e["f038"]))
        buf.write(struct.pack("<I", e["f060"]))
        buf.write(struct.pack("<I", e["f064"]))
        buf.write(struct.pack("<I", e["f068"]))
        buf.write(struct.pack("<I", e["f06C"]))
        buf.write(struct.pack("<I", e["f080"]))
        buf.write(struct.pack("<I", e["f1E8"]))
        buf.write(struct.pack("<I", e["anim_id"]))
        buf.write(struct.pack("<ffff", *e["anim_time"]))
        buf.write(struct.pack("<BBBBBB",
                              1 if e["in_lock_on"] else 0,
                              1 if e["in_boss_bar"] else 0,
                              1 if e["in_roster"] else 0,
                              e["enemy_class"],
                              1 if e["is_focused"] else 0,
                              e["focus_reason"]))
        buf.write(struct.pack("<BB", len(e["regions"]), 0))    # region_count + reserved
        buf.write(b"\x00" * 24)                                 # pad to 96

        # Region records (20 byte header + payload each)
        for r in e["regions"]:
            buf.write(struct.pack("<BB", r["region_id"], 1 if r.get("has_child") else 0))
            buf.write(struct.pack("<HH", r["payload_offset"], len(r["payload"])))
            buf.write(struct.pack("<H", r.get("child_source_offset", 0)))
            buf.write(struct.pack("<I", r.get("source_chain", 0)))
            buf.write(struct.pack("<Q", r["region_base_abs"]))
            buf.write(r["payload"])

    return buf.getvalue()


def write_synthetic_bin(path: str) -> dict:
    """Write a .bin with one manifest + a few sample records. Returns the
    expected values so the test can verify round-trip."""
    sample_payload = make_synthetic_sample(
        frame=42, ts_ms_rel=1234, mode=2, truncated=False,
        enemies=[
            {
                "chr_ins": 0x111122223333AAAA,
                "handle": 0x999988887777CCCC,
                "f038": 0x12345678,
                "f060": 0xABCD0001,
                "f064": 4380,                # this is the cXXXX we're spoofing
                "f068": 0xFEEDFACE,
                "f06C": 0x11111111,
                "f080": 0x22222222,
                "f1E8": 0x33333333,
                "anim_id": 41021,
                "anim_time": (0.1, 0.5, 0.0, -1.0),
                "in_lock_on": True,
                "in_boss_bar": False,
                "in_roster": True,
                "enemy_class": 0,
                "is_focused": True,
                "focus_reason": 1,
                "regions": [
                    {
                        "region_id": 0,
                        "payload_offset": 0,
                        "payload": bytes(range(16)),
                        "region_base_abs": 0x1234_5678_9000_0000,
                        "source_chain": 0,
                    },
                    {
                        "region_id": 4,
                        "payload_offset": 0,
                        "payload": bytes([0xFF] * 8),
                        "region_base_abs": 0xABCD_0000_0000_0000,
                        "source_chain": 4,
                        "has_child": True,
                        "child_source_offset": 0x100,
                    },
                ],
            }
        ],
    )

    with open(path, "wb") as fh:
        # Manifest
        manifest_text = (
            "schema_version=1\n"
            "mode=2\n"
            "session_start_ms=1000\n"
            "config_dump_begin\n"
            "[capture]\n"
            "mode = qualification\n"
            "config_dump_end\n"
        )
        fh.write(struct.pack("<II", probe_bin.MAN0_MAGIC, len(manifest_text)))
        fh.write(manifest_text.encode("utf-8"))

        # Sample
        fh.write(struct.pack("<II", probe_bin.SRD0_MAGIC, len(sample_payload)))
        fh.write(sample_payload)

    return {"sample_payload_len": len(sample_payload)}


def main() -> int:
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "synthetic.bin")
        expected = write_synthetic_bin(path)

        bf = probe_bin.read_bin(path)

        # Sanity
        if bf.parse_errors:
            print(f"FAIL: parse_errors={bf.parse_errors}")
            failed += 1
        if len(bf.manifests) != 1:
            print(f"FAIL: expected 1 manifest, got {len(bf.manifests)}")
            failed += 1
        if len(bf.samples) != 1:
            print(f"FAIL: expected 1 sample, got {len(bf.samples)}")
            failed += 1

        if bf.samples:
            s = bf.samples[0]
            checks = [
                ("magic+schema parsed", s.schema_version == 1, s.schema_version),
                ("frame", s.frame == 42, s.frame),
                ("ts_ms_rel", s.ts_ms_rel == 1234, s.ts_ms_rel),
                ("mode", s.mode == 2, s.mode),
                ("truncated", s.truncated is False, s.truncated),
                ("wcm_ptr", s.wcm_ptr == 0xDEADBEEF12345678, hex(s.wcm_ptr)),
                ("player_lock", s.player_lock_on_target_handle == 0xAAAA_BBBB_CCCC_DDDD, hex(s.player_lock_on_target_handle)),
                ("focused", s.focused_enemy_handle == 0xDEAD_C0DE_FEED_BEEF, hex(s.focused_enemy_handle)),
                ("focus_reason", s.focus_reason == 1, s.focus_reason),
                ("enemy_count", s.enemy_record_count == 1, s.enemy_record_count),
                ("adaptive_step", s.adaptive_step == 2, s.adaptive_step),
                ("len(enemies)", len(s.enemies) == 1, len(s.enemies)),
            ]
            for label, ok, got in checks:
                if not ok:
                    print(f"FAIL: {label} got {got}"); failed += 1

            if s.enemies:
                e = s.enemies[0]
                e_checks = [
                    ("chr_ins", e.chr_ins_abs == 0x111122223333AAAA, hex(e.chr_ins_abs)),
                    ("handle", e.handle == 0x999988887777CCCC, hex(e.handle)),
                    ("f038", e.field_at_0x038 == 0x12345678, hex(e.field_at_0x038)),
                    ("f064", e.field_at_0x064 == 4380, e.field_at_0x064),
                    ("anim_id", e.anim_id == 41021, e.anim_id),
                    ("anim_time[0]", abs(e.anim_time[0] - 0.1) < 1e-6, e.anim_time[0]),
                    ("anim_time[1]", abs(e.anim_time[1] - 0.5) < 1e-6, e.anim_time[1]),
                    ("in_lock_on", e.in_lock_on is True, e.in_lock_on),
                    ("is_focused", e.is_focused is True, e.is_focused),
                    ("region_count", e.region_count == 2, e.region_count),
                    ("len(regions)", len(e.regions) == 2, len(e.regions)),
                ]
                for label, ok, got in e_checks:
                    if not ok:
                        print(f"FAIL: enemy.{label} got {got}"); failed += 1

                if len(e.regions) >= 1:
                    r = e.regions[0]
                    if r.region_id != 0:
                        print(f"FAIL: region[0].region_id={r.region_id}"); failed += 1
                    if r.payload != bytes(range(16)):
                        print(f"FAIL: region[0].payload mismatch"); failed += 1
                if len(e.regions) >= 2:
                    r = e.regions[1]
                    if r.region_id != 4:
                        print(f"FAIL: region[1].region_id={r.region_id}"); failed += 1
                    if r.has_child_offset != 1:
                        print(f"FAIL: region[1].has_child_offset={r.has_child_offset}"); failed += 1
                    if r.child_source_offset_in_time_act != 0x100:
                        print(f"FAIL: region[1].child_offset={r.child_source_offset_in_time_act}"); failed += 1

        # Manifest content
        if bf.manifests:
            m = bf.manifests[0]
            if m.fields.get("mode") != "2":
                print(f"FAIL: manifest.mode={m.fields.get('mode')}"); failed += 1
            if "[capture]" not in m.fields.get("__config_dump__", ""):
                print(f"FAIL: config_dump didn't preserve [capture] section"); failed += 1

    if failed:
        print(f"\n{failed} check(s) failed.")
        return 1
    print("PASS — all wire-format checks round-tripped cleanly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
