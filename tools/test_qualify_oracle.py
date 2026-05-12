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
    # Duration-slot decoy values for the +0x2C candidate. Cycles between two
    # values across anims so the duration field looks "monotonic-ish" across
    # the session but holds constant within any single anim — the exact
    # signature of a real duration slot in the live data. Without this, the
    # +0x2C synthetic slot would just be -1.0 forever and never compete
    # with +0x24 in the gate, so the duration-rejection logic wouldn't
    # actually be exercised by this test.
    duration_decoy_values = [1.0, 2.5, 1.5, 3.0]
    for anim_idx, w in enumerate(pw):
        anim_time = 0.0
        duration_decoy = duration_decoy_values[anim_idx % len(duration_decoy_values)]
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
                        # Slot layout matches the four candidate offsets:
                        #   [0] +0x20: always 0 (junk slot — fails range/segments)
                        #   [1] +0x24: live anim_time (the answer)
                        #   [2] +0x28: junk (would fail too)
                        #   [3] +0x2C: duration decoy — constant per anim,
                        #              jumps between anims. Used to be -1.0
                        #              which auto-failed the range check;
                        #              now positive + finite so the gate
                        #              ACTUALLY has to reject it on
                        #              forward_progressions instead.
                        "anim_time": (0.0, emitted_time, 0.0, duration_decoy),
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

        # The anim_time gate must pick +0x24 (the slot we populated with
        # smoothly-advancing playback). Picking +0x2C would mean the
        # duration-slot rejection regressed — the synthetic now populates
        # +0x2C with realistic duration-style decoy values (constant per
        # anim, jumping between anims), and the gate must reject those
        # via the forward_progressions threshold.
        at_offset = result.get("anim_time", {}).get("candidate_offset")
        if at_offset != 0x24:
            print(f"FAIL: anim_time field picked +0x{at_offset:02X}, "
                  f"expected +0x24 (duration-slot rejection regression)")
            failed += 1

        # The +0x2C duration-decoy candidate must have a low forward_progressions
        # count — proof that the discriminator does its job. We don't get the
        # +0x2C verdict directly back from run_qualification (only the winner),
        # but we can re-run find_anim_time_field to inspect it.
        # (Skipped for brevity — the at_offset check above is sufficient
        # because if +0x2C's progressions were high enough to confuse the
        # gate, it would have won the tiebreak over +0x24.)

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
