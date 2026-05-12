"""End-to-end test for qualify_oracle.py against a synthetic qualification capture.

Builds a fake .bin where:
- focused enemy has constant field_at_0x064 = 2130 (matches c2130 in real DB)
- enemy_anim_time[1] (TimeAct +0x24) is monotonic during anim_id holds and rewinds on change
- a few anim_ids match c2130's database parry windows

Then runs qualify_oracle.run_qualification and verifies it returns PASSED.

This is a guardrail against regressions in the analysis logic. Real
captures will have noise this test doesn't simulate (handles changing,
empty samples, truncation, etc.); fix those as they come up.
"""

from __future__ import annotations

import json
import os
import struct
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import probe_bin
import qualify_oracle
from test_probe_bin import make_synthetic_sample


def build_qualification_bin(path: str, char_db: dict) -> dict:
    """Pick a real cid from the database, generate samples that simulate a
    fight against it (multiple anim_ids, anim_time progressing through DB
    windows), write to `path`. Returns metadata so the test can verify."""

    # Pick c2130 — 79 windows, manageable size.
    cid = "c2130"
    cid_num = int(cid[1:])
    char = char_db[cid_num]

    # Use a handful of windows to simulate animations the enemy actually
    # performs during the fake qualification fight.
    pw = char.parry_windows[:8]   # first 8 windows from db

    samples: list[bytes] = []
    frame = 0
    ts_ms_rel = 0
    chr_ins_abs = 0x000001ABCD000000

    # For each parry window in the spec, generate ~30 samples advancing
    # anim_time from 0 → window_end + 0.2s, then reset on next window.
    #
    # Models the anim-transition LAG observed in real probe captures: when
    # the game emits a new anim_id, the anim_time field can take 1-10 samples
    # (median ~4, P90 ~9 on smoke-20260509-170547 at 91 Hz) to catch up to
    # the new value. The test fixture injects LAG_SAMPLES "lag samples" at
    # the start of each new animation where the anim_id has already changed
    # but the anim_time still carries the previous anim's final value.
    # find_anim_time_field must tolerate this. Set to the P90 observed in
    # real data so the regression gate is meaningfully strict.
    LAG_SAMPLES = 9
    prev_anim_final_t = 0.0  # the last anim_time value of the previous anim
    is_first_anim = True
    for w in pw:
        anim_time = 0.0
        # Use full encoding (matches what the runtime is expected to produce
        # if the encoding analysis picks "full"; the database also indexes
        # both, so the analyzer will pick whichever encoding has hits).
        anim_id = w.anim_id_full
        sample_step_s = 0.011    # 11 ms ≈ 90 Hz focused capture
        n_samples = int((w.window_end_s + 0.2) / sample_step_s) + 1
        # Emit LAG_SAMPLES at the START of every new anim except the first one,
        # where anim_id is the new value but anim_time is still the prev final.
        lag_remaining = 0 if is_first_anim else LAG_SAMPLES
        is_first_anim = False
        for _ in range(n_samples):
            if lag_remaining > 0:
                emitted_time = prev_anim_final_t
                lag_remaining -= 1
            else:
                emitted_time = anim_time
            payload = make_synthetic_sample(
                frame=frame, ts_ms_rel=ts_ms_rel, mode=2, truncated=False,
                enemies=[
                    {
                        "chr_ins": chr_ins_abs,
                        "handle": 0xAAAABBBBCCCCDDDD,
                        "f038": 0x10000 + (anim_id & 0xFFFF),  # noisy
                        "f060": 0x99999999,
                        "f064": cid_num,                        # CONSTANT (the join key)
                        "f068": 0x88888888,
                        "f06C": 0x77777777,
                        "f080": 0x66666666,
                        "f1E8": 0x55555555,
                        "anim_id": anim_id,
                        "anim_time": (0.0, emitted_time, 0.0, -1.0),  # TimeAct+0x24 is the live one
                        "in_lock_on": True,
                        "in_boss_bar": False,
                        "in_roster": True,
                        "enemy_class": 0,
                        "is_focused": True,
                        "focus_reason": 1,
                        "regions": [],         # qualification only emits T1+T2 for non-focused;
                                                # focused gets full T3, but the test doesn't need it
                    }
                ],
            )
            samples.append(payload)
            frame += 1
            ts_ms_rel += 11
            if lag_remaining == 0:
                # only advance the "real" anim_time once we're past the lag
                anim_time += sample_step_s
        # Remember the final emitted time of this anim for the next anim's lag.
        prev_anim_final_t = emitted_time

    # Write .bin
    with open(path, "wb") as fh:
        # Manifest
        manifest = (
            "schema_version=1\n"
            "mode=2\n"
            "session_start_ms=0\n"
            "config_dump_begin\n"
            "[capture]\n"
            "mode = qualification\n"
            "config_dump_end\n"
        )
        fh.write(struct.pack("<II", probe_bin.MAN0_MAGIC, len(manifest)))
        fh.write(manifest.encode("utf-8"))

        for payload in samples:
            fh.write(struct.pack("<II", probe_bin.SRD0_MAGIC, len(payload)))
            fh.write(payload)

    return {
        "expected_cid": cid,
        "sample_count": len(samples),
        "anim_count": len(pw),
    }


def test_family_fallback_lookup() -> int:
    """Unit test for _lookup_with_family_fallback. The DB is keyed by
    family-parent for most c-ids (multiples of 10); variants like c4311
    don't have their own row and must fall back to c4310. The 19 exception
    c-ids (e.g. c3251) have their own rows and must NOT round down.
    """
    char_db = qualify_oracle.load_database(qualify_oracle.DEFAULT_DATABASE_PATH)
    failed = 0

    # Case 1: exact match (c2130 has its own DB row with parry windows).
    cd, fallback = qualify_oracle._lookup_with_family_fallback(2130, char_db)
    if cd is None or cd.cid != "c2130" or fallback:
        print(f"FAIL: exact lookup c2130 returned ({cd.cid if cd else None}, fallback={fallback})")
        failed += 1

    # Case 2: family fallback (c4311 → c4310). The v6.3 live capture saw
    # c4311 Godrick Soldiers; this is the canonical fallback case.
    cd, fallback = qualify_oracle._lookup_with_family_fallback(4311, char_db)
    if cd is None or cd.cid != "c4310" or not fallback:
        print(f"FAIL: c4311 fallback returned ({cd.cid if cd else None}, fallback={fallback})")
        failed += 1

    # Case 3: exact takes precedence over fallback (c3251 has its OWN row
    # with parry windows AND c3250 also exists). Must return c3251, not
    # round down silently.
    cd, fallback = qualify_oracle._lookup_with_family_fallback(3251, char_db)
    if cd is None or cd.cid != "c3251" or fallback:
        print(f"FAIL: c3251 should self-match, got ({cd.cid if cd else None}, fallback={fallback})")
        failed += 1

    # Case 4: completely-absent c-id (c9999 not in db, c9990 not in db).
    # Must return (None, False) — no silent match against an unrelated row.
    cd, fallback = qualify_oracle._lookup_with_family_fallback(9999, char_db)
    if cd is not None:
        print(f"FAIL: c9999 returned {cd.cid} instead of None")
        failed += 1

    # Case 5: value is already a multiple of 10 but absent from DB.
    # Must not loop — return (None, False) without retrying.
    cd, fallback = qualify_oracle._lookup_with_family_fallback(9990, char_db)
    if cd is not None:
        print(f"FAIL: c9990 returned {cd.cid} instead of None")
        failed += 1

    return failed


def main() -> int:
    char_db = qualify_oracle.load_database(qualify_oracle.DEFAULT_DATABASE_PATH)
    if not char_db:
        print("FAIL: empty char_db")
        return 1

    failed = 0

    # Unit tests on the family-fallback helper.
    failed += test_family_fallback_lookup()

    with tempfile.TemporaryDirectory() as tmp:
        bin_path = os.path.join(tmp, "qualification-test.bin")
        meta = build_qualification_bin(bin_path, char_db)

        result = qualify_oracle.run_qualification(
            base_path=os.path.splitext(bin_path)[0],
            database_path=qualify_oracle.DEFAULT_DATABASE_PATH,
        )

        print(qualify_oracle.format_report(result, bin_path))
        print()

        if result["verdict"] != "PASSED":
            print(f"FAIL: verdict was {result['verdict']}")
            failed += 1

        if result.get("join_key", {}).get("matched_cid") != meta["expected_cid"]:
            print(f"FAIL: matched_cid was {result.get('join_key', {}).get('matched_cid')} "
                  f"expected {meta['expected_cid']}")
            failed += 1

        # The c2130 exact-match capture must NOT be flagged as family-fallback.
        if result.get("join_key", {}).get("matched_via_family_fallback"):
            print("FAIL: c2130 exact-match capture flagged as family fallback")
            failed += 1

        wc = result.get("window_check", {})
        if wc.get("matched_within_tolerance", 0) < 1:
            print(f"FAIL: no windows matched within tolerance")
            failed += 1

    if failed:
        print(f"\n{failed} check(s) failed.")
        return 1
    print("PASS — qualify_oracle round-trips a synthetic qualification capture cleanly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
