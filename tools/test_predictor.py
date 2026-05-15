#!/usr/bin/env python3
"""Unit tests for the Python predictor reference at tools/parry_predictor.py.

The Python module is a 1:1 algorithmic mirror of the C++ engine in
probe/probe.cpp. This harness validates the PYTHON implementation; it
cannot directly run the C++ code. Divergence between the two is caught
in two ways:

  1. By construction: every Python branch carries a comment citing the
     C++ line range it mirrors. Reviews check that pairing.
  2. In-game smoke test: Phase 4.4's regression harness replays a real
     boss-fight capture against the C++ engine's decision log and
     against the Python engine, then diffs. Any divergence surfaces
     there.

So: this harness is the FIRST line of defense ("does the algorithm work
at all?"), and the in-game smoke is the SECOND ("does the C++ implement
the algorithm correctly?"). A C++ change that breaks the spec without
breaking the spec text will pass this harness silently — that's a known
gap closed by the integration phase.

Coverage targets (from PHASE4-PLAN.md commit-5 spec):
- DB loader / table builder
- Family fallback (exact wins, c4311 -> c4310, c9990 absent, c0000 ignored)
- Lead-time math at lead=0, lead>0, lead<0
- Latch consumption (no double-cue per (boss, instance, window))
- Reset on anim_id change (with debounce)
- Reset on anim_time rewind
- Reset on c-id change
- Reset on disappearance > grace
- Target filter: enabled, target_known, target_match permutations
- NaN/Inf guard
- Window overflow (>32 windows per anim)
- Negative-lead window-exceeds suppression

Run as:
    python3 tools/test_predictor.py
or with pytest:
    pytest tools/test_predictor.py -v
"""

from __future__ import annotations

import math
import sys
import unittest
from pathlib import Path

# Make tools/ importable when run directly.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from parry_predictor import (
    ACTION_BEFORE_LEAD,
    ACTION_FIRE,
    ACTION_LATCHED,
    ACTION_LATE_INSIDE_WINDOW,
    ACTION_NO_DB,
    ACTION_NO_KEY,
    ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW,
    ACTION_SUPPRESSED_POST_WINDOW,
    ACTION_SUPPRESSED_TARGET,
    ANIM_ID_DEBOUNCE_POLLS,
    ANIM_REWIND_TOLERANCE_S,
    BOSS_DISAPPEAR_GRACE_MS,
    MAX_WINDOWS_PER_ANIM_LATCH,
    BossTickInput,
    ParryWindow,
    PredictionConfig,
    PredictorEngine,
    action_name,
    load_parry_db,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def make_engine(db_entries: dict[tuple[int, int], list[tuple[float, float]]] | None = None,
                loaded: bool = True) -> PredictorEngine:
    """Build an engine with a tiny synthetic DB.

    db_entries maps (cid, anim_id) -> [(open_s, close_s), ...]
    If loaded=False, the engine has no DB at all (NO_DB path).
    """
    if not loaded:
        return PredictorEngine()
    db = {}
    if db_entries:
        for (cid, anim), windows in db_entries.items():
            key = (cid << 32) | (anim & 0xFFFFFFFF)
            db[key] = [ParryWindow(o, c) for (o, c) in windows]
    return PredictorEngine(db=db)


def tick(handle=1, raw_cid=4310, anim_id=4001100, anim_time_s=0.0,
         target_known=False, target_match=False) -> BossTickInput:
    return BossTickInput(
        boss_handle=handle,
        raw_cid=raw_cid,
        anim_id=anim_id,
        anim_time_s=anim_time_s,
        target_known=target_known,
        target_match=target_match,
    )


def cfg(lead_ms=0, filter_on=False) -> PredictionConfig:
    return PredictionConfig(audio_cue_lead_ms=lead_ms,
                            target_filter_enabled=filter_on)


# ---------------------------------------------------------------------------
# DB loader + family fallback
# ---------------------------------------------------------------------------


class TestDbLoader(unittest.TestCase):
    def test_real_db_loads(self):
        """The committed parry_data.bin should parse without error."""
        root = Path(__file__).resolve().parent.parent
        db = load_parry_db(root / "data" / "parry_data.bin")
        # Sanity: the build summary said 1,919 anims; our parser should agree.
        self.assertEqual(len(db), 1919,
                         f"Expected 1919 anim rows in committed DB; got {len(db)}")

    def test_engine_initial_state(self):
        """Fresh engine has no boss state and no decisions queued."""
        e = make_engine()
        self.assertEqual(e.boss_state, {})
        self.assertTrue(e.db_loaded)

    def test_no_db_path(self):
        """Constructor with no args reports ACTION_NO_DB."""
        e = make_engine(loaded=False)
        self.assertFalse(e.db_loaded)
        out = e.evaluate_tick(tick(), cfg(), now_ms_rel=0)
        self.assertEqual(out[0].action, ACTION_NO_DB)

    def test_empty_db_emits_no_key_not_no_db(self):
        """Successfully loaded but empty DB → NO_KEY (matches C++ runtime)."""
        e = make_engine({}, loaded=True)
        self.assertTrue(e.db_loaded)
        out = e.evaluate_tick(tick(raw_cid=4310, anim_id=999), cfg(), now_ms_rel=0)
        self.assertEqual(out[0].action, ACTION_NO_KEY)


class TestCidResolution(unittest.TestCase):
    def test_exact_match_wins(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        # Use the EXACT cid that exists.
        out = e.evaluate_tick(tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.4),
                              cfg(), now_ms_rel=0)
        self.assertEqual(out[0].resolved_cid, 4310)
        self.assertEqual(out[0].action, ACTION_BEFORE_LEAD)

    def test_family_fallback_c4311_to_c4310(self):
        """c4311 has no row but c4310 does — family fallback hits c4310."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(tick(raw_cid=4311, anim_id=4001100, anim_time_s=0.4),
                              cfg(), now_ms_rel=0)
        self.assertEqual(out[0].resolved_cid, 4310)
        self.assertEqual(out[0].action, ACTION_BEFORE_LEAD)

    def test_exact_wins_over_family(self):
        """If both c4311 AND c4310 have rows, exact c4311 wins."""
        e = make_engine({
            (4310, 4001100): [(0.5, 0.6)],
            (4311, 4001100): [(0.7, 0.8)],
        })
        out = e.evaluate_tick(tick(raw_cid=4311, anim_id=4001100, anim_time_s=0.4),
                              cfg(), now_ms_rel=0)
        self.assertEqual(out[0].resolved_cid, 4311)
        self.assertEqual(out[0].window_open_s, 0.7)

    def test_c9990_absent_no_recursive_fallback(self):
        """c9990 with no row should NOT fall back to c9990 again (loop)."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(tick(raw_cid=9990, anim_id=4001100, anim_time_s=0.4),
                              cfg(), now_ms_rel=0)
        # Family of 9990 is 9990 (already aligned) — no fallback.
        self.assertEqual(out[0].resolved_cid, 0)
        self.assertEqual(out[0].action, ACTION_NO_KEY)

    def test_c0000_junk_filter(self):
        """cid < 1000 always returns NO_KEY without DB lookup."""
        e = make_engine({(0, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(tick(raw_cid=0, anim_id=4001100, anim_time_s=0.4),
                              cfg(), now_ms_rel=0)
        self.assertEqual(out[0].resolved_cid, 0)
        self.assertEqual(out[0].action, ACTION_NO_KEY)

    def test_c999_junk_filter(self):
        """Boundary: cid 999 is junk."""
        e = make_engine({(999, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(tick(raw_cid=999, anim_id=4001100, anim_time_s=0.4),
                              cfg(), now_ms_rel=0)
        self.assertEqual(out[0].action, ACTION_NO_KEY)


# ---------------------------------------------------------------------------
# Lead-time math
# ---------------------------------------------------------------------------


class TestLeadTimeMath(unittest.TestCase):
    def test_lead_zero_fires_at_window_open(self):
        """anim_time exactly equal to window.open_s with lead=0 → FIRE."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(lead_ms=0), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_FIRE,
                         "Equality at window-open should be ACTION_FIRE, not LATE_INSIDE_WINDOW")
        self.assertAlmostEqual(out[0].lead_time_ms, 0.0)

    def test_lead_zero_before_open_is_before_lead(self):
        """anim_time < window.open_s with lead=0 → BEFORE_LEAD."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.3),
            cfg(lead_ms=0), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_BEFORE_LEAD)
        self.assertAlmostEqual(out[0].lead_time_ms, 200.0, places=3)

    def test_lead_zero_inside_window_is_late_inside(self):
        """anim_time > window.open_s but <= close with lead=0 → LATE_INSIDE_WINDOW."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.55),
            cfg(lead_ms=0), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_LATE_INSIDE_WINDOW)

    def test_lead_zero_past_close_suppressed(self):
        """anim_time > window.close_s → SUPPRESSED_POST_WINDOW."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.7),
            cfg(lead_ms=0), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_SUPPRESSED_POST_WINDOW)

    def test_positive_lead_fires_before_window_open(self):
        """lead=50, anim_time=0.46 (40ms before 0.5 open) → FIRE."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.46),
            cfg(lead_ms=50), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_FIRE)
        self.assertAlmostEqual(out[0].lead_time_ms, 40.0, places=3)

    def test_positive_lead_still_before_threshold(self):
        """lead=50, anim_time=0.43 (70ms before 0.5 open) → BEFORE_LEAD."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.43),
            cfg(lead_ms=50), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_BEFORE_LEAD)

    def test_negative_lead_fires_after_window_open(self):
        """lead=-50 (fire 50ms AFTER window opens). Window [0.5, 0.6].
        Fire time = 0.55. anim_time=0.56 → FIRE."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.56),
            cfg(lead_ms=-50), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_FIRE)

    def test_negative_lead_too_early_is_before_lead(self):
        """lead=-50, anim_time=0.52 (before fire time 0.55) → BEFORE_LEAD."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.52),
            cfg(lead_ms=-50), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_BEFORE_LEAD)

    def test_negative_lead_exceeds_window(self):
        """lead=-150 on a 100ms window — fire time exceeds close → suppressed."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.51),
            cfg(lead_ms=-150), now_ms_rel=0,
        )
        self.assertEqual(out[0].action,
                         ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW)


# ---------------------------------------------------------------------------
# Latch consumption (no double-cue)
# ---------------------------------------------------------------------------


class TestLatchConsumption(unittest.TestCase):
    def test_no_double_cue_same_window_same_instance(self):
        """Two ticks at the same window open both lead to FIRE then LATCHED."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out1 = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(lead_ms=0), now_ms_rel=0,
        )
        out2 = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(lead_ms=0), now_ms_rel=4,
        )
        self.assertEqual(out1[0].action, ACTION_FIRE)
        self.assertEqual(out2[0].action, ACTION_LATCHED)

    def test_multiple_windows_independent_latches(self):
        """One anim with 2 windows — each consumed independently."""
        e = make_engine({(4310, 4001100): [(0.3, 0.4), (0.6, 0.7)]})
        # Tick at 0.3 — window 0 fires, window 1 is BEFORE_LEAD.
        out1 = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.3),
            cfg(lead_ms=0), now_ms_rel=0,
        )
        self.assertEqual(out1[0].action, ACTION_FIRE)
        self.assertEqual(out1[1].action, ACTION_BEFORE_LEAD)
        # Tick at 0.6 — window 0 is past-close, window 1 fires.
        out2 = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.6),
            cfg(lead_ms=0), now_ms_rel=4,
        )
        self.assertEqual(out2[0].action, ACTION_SUPPRESSED_POST_WINDOW)
        self.assertEqual(out2[1].action, ACTION_FIRE)


# ---------------------------------------------------------------------------
# Instance reset triggers
# ---------------------------------------------------------------------------


class TestInstanceResets(unittest.TestCase):
    def test_first_ever_sighting_increments_to_1(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=0,
        )
        st = e.boss_state[1]
        self.assertEqual(st.anim_instance_seq, 1)
        self.assertTrue(st.ever_seen)

    def test_anim_id_change_requires_debounce(self):
        """Single-poll anim flicker does NOT reset instance."""
        e = make_engine({
            (4310, 4001100): [(0.5, 0.6)],
            (4310, 4001101): [(0.4, 0.5)],
        })
        # Tick 1: established anim 4001100.
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=0,
        )
        seq0 = e.boss_state[1].anim_instance_seq
        # Tick 2: flicker to 4001101 (single poll). Should NOT reset yet.
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001101, anim_time_s=0.0),
            cfg(), now_ms_rel=4,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0,
                         "Single-poll anim flicker should not reset instance")
        # Tick 3: stays at 4001101 — now we have 2 consecutive polls → reset.
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001101, anim_time_s=0.0),
            cfg(), now_ms_rel=8,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0 + 1)

    def test_flicker_back_clears_pending(self):
        """anim_id X → Y → X (flicker back) should not reset."""
        e = make_engine({
            (4310, 4001100): [(0.5, 0.6)],
            (4310, 4001101): [(0.4, 0.5)],
        })
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=0,
        )
        seq0 = e.boss_state[1].anim_instance_seq
        # Tick: anim_id=Y for one poll
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001101, anim_time_s=0.0),
            cfg(), now_ms_rel=4,
        )
        # Tick: anim_id=X (back) — pending should clear.
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=8,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0,
                         "Flicker X→Y→X should not trigger reset")
        self.assertEqual(e.boss_state[1].pending_count, 0)

    def test_anim_time_rewind_triggers_reset(self):
        """Same anim, anim_time rewinds > tolerance → instance reset.
        This is the Margit-bonks-twice-in-a-row case."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        # Tick 1: anim_id 4001100 at time 0.55 — fires window.
        out1 = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.55),
            cfg(), now_ms_rel=0,
        )
        self.assertEqual(out1[0].action, ACTION_LATE_INSIDE_WINDOW)
        seq0 = e.boss_state[1].anim_instance_seq
        # Tick 2: same anim, but anim_time rewound to 0.0 (boss starts the
        # anim again from frame 0). Should reset instance.
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=4,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0 + 1)
        # And critically: the next FIRE should be a fresh fire, not LATCHED.
        out3 = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(), now_ms_rel=8,
        )
        self.assertEqual(out3[0].action, ACTION_FIRE,
                         "Margit-bonk-twice: second instance must FIRE fresh")

    def test_small_rewind_under_tolerance_does_not_reset(self):
        """anim_time rewind < 50ms tolerance — same instance retained."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(), now_ms_rel=0,
        )
        seq0 = e.boss_state[1].anim_instance_seq
        # Rewind by 30ms (under 50ms tolerance).
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.47),
            cfg(), now_ms_rel=4,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0)

    def test_disappearance_grace(self):
        """Boss vanishes for > BOSS_DISAPPEAR_GRACE_MS → instance reset on return."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=0,
        )
        seq0 = e.boss_state[1].anim_instance_seq
        # Reappear 300ms later (> 250ms grace).
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=BOSS_DISAPPEAR_GRACE_MS + 50,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0 + 1)

    def test_disappearance_within_grace_no_reset(self):
        """Boss vanishes briefly but reappears within grace — no reset."""
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=0,
        )
        seq0 = e.boss_state[1].anim_instance_seq
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=BOSS_DISAPPEAR_GRACE_MS - 50,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0)

    def test_cid_change_triggers_reset(self):
        """resolved_cid changes → instance reset."""
        e = make_engine({
            (4310, 4001100): [(0.5, 0.6)],
            (5000, 4001100): [(0.3, 0.4)],
        })
        e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=0,
        )
        seq0 = e.boss_state[1].anim_instance_seq
        e.evaluate_tick(
            tick(raw_cid=5000, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=4,
        )
        self.assertEqual(e.boss_state[1].anim_instance_seq, seq0 + 1)
        self.assertEqual(e.boss_state[1].resolved_cid, 5000)


# ---------------------------------------------------------------------------
# Target filter
# ---------------------------------------------------------------------------


class TestTargetFilter(unittest.TestCase):
    def test_filter_off_target_unknown_still_fires(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5,
                 target_known=False, target_match=False),
            cfg(lead_ms=0, filter_on=False), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_FIRE)

    def test_filter_on_no_target_known_suppresses(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5,
                 target_known=False, target_match=False),
            cfg(lead_ms=0, filter_on=True), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_SUPPRESSED_TARGET)

    def test_filter_on_target_known_but_no_match_suppresses(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5,
                 target_known=True, target_match=False),
            cfg(lead_ms=0, filter_on=True), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_SUPPRESSED_TARGET)

    def test_filter_on_target_match_fires(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.5,
                 target_known=True, target_match=True),
            cfg(lead_ms=0, filter_on=True), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_FIRE)


# ---------------------------------------------------------------------------
# Defensive edge cases
# ---------------------------------------------------------------------------


class TestEdgeCases(unittest.TestCase):
    def test_nan_anim_time_returns_no_key(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=float("nan")),
            cfg(), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_NO_KEY)

    def test_inf_anim_time_returns_no_key(self):
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=float("inf")),
            cfg(), now_ms_rel=0,
        )
        self.assertEqual(out[0].action, ACTION_NO_KEY)

    def test_window_overflow_emits_one_no_key(self):
        """Anim with 33 windows triggers the overflow guard at window 32."""
        windows_33 = [(float(i) * 0.1, float(i) * 0.1 + 0.05) for i in range(33)]
        e = make_engine({(4310, 4001100): windows_33})
        out = e.evaluate_tick(
            tick(raw_cid=4310, anim_id=4001100, anim_time_s=0.0),
            cfg(), now_ms_rel=0,
        )
        # We expect MAX_WINDOWS_PER_ANIM_LATCH normal decisions, then ONE
        # overflow NO_KEY.
        self.assertEqual(len(out), MAX_WINDOWS_PER_ANIM_LATCH + 1)
        self.assertEqual(out[-1].action, ACTION_NO_KEY)
        self.assertEqual(out[-1].window_ordinal, MAX_WINDOWS_PER_ANIM_LATCH)

    def test_per_boss_state_isolation(self):
        """Two different boss handles have independent state.

        Boss 1 fires + latches; boss 2 must still fire fresh on its own
        identical input. Their state objects must be distinct.
        """
        e = make_engine({(4310, 4001100): [(0.5, 0.6)]})
        out1 = e.evaluate_tick(
            tick(handle=1, raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(), now_ms_rel=0,
        )
        out2 = e.evaluate_tick(
            tick(handle=2, raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(), now_ms_rel=4,
        )
        self.assertEqual(out1[0].action, ACTION_FIRE)
        self.assertEqual(out2[0].action, ACTION_FIRE,
                         "Boss 2 must fire fresh; boss 1's latch must not leak")
        self.assertEqual(len(e.boss_state), 2)
        self.assertIsNot(e.boss_state[1], e.boss_state[2],
                         "Boss states must be distinct objects")
        # Boss 1 has consumed window 0 (bit 0); boss 2 has too — but
        # those are bits on separate state objects.
        self.assertEqual(e.boss_state[1].consumed_windows, 1)
        self.assertEqual(e.boss_state[2].consumed_windows, 1)
        # Now tick boss 1 again at same time — must be LATCHED, not FIRE.
        out3 = e.evaluate_tick(
            tick(handle=1, raw_cid=4310, anim_id=4001100, anim_time_s=0.5),
            cfg(), now_ms_rel=8,
        )
        self.assertEqual(out3[0].action, ACTION_LATCHED,
                         "Boss 1's latch must persist independently")


# ---------------------------------------------------------------------------
# Integration: replay against the real DB
# ---------------------------------------------------------------------------


class TestRealDbIntegration(unittest.TestCase):
    """Sanity check against the committed parry_data.bin."""

    def setUp(self):
        root = Path(__file__).resolve().parent.parent
        self.engine = PredictorEngine(db_path=root / "data" / "parry_data.bin")

    def test_known_godrick_cid_has_windows(self):
        """c4310 (Godrick Soldier from the v6.3 PASS) is in the DB."""
        # Find any anim_id under c4310 that has a window.
        c4310_anims = [
            k & 0xFFFFFFFF for k in self.engine.db
            if (k >> 32) == 4310
        ]
        self.assertGreater(len(c4310_anims), 0,
                           "c4310 should have at least one anim with windows")

    def test_full_attack_sequence_against_real_db(self):
        """Simulate an attack animation playing through its windows.

        Picks a c4310 anim with EXACTLY ONE window in the reasonable
        time range, so the per-tick assertion isn't confused by other
        windows firing at the same poll.
        """
        # Find a c4310 anim with EXACTLY ONE window in (0.1, 1.0).
        target_key = None
        for k, wins in self.engine.db.items():
            if ((k >> 32) == 4310 and len(wins) == 1
                    and 0.1 < wins[0].open_s < 1.0):
                target_key = k
                break
        self.assertIsNotNone(target_key,
                             "Need a c4310 anim with exactly one window in "
                             "(0.1, 1.0)")

        cid = target_key >> 32
        anim_id = target_key & 0xFFFFFFFF
        win = self.engine.db[target_key][0]

        # Replay 50 ticks at 20ms intervals from t=0 to t=1.0s.
        window0_fires = []
        for tick_idx in range(50):
            t_s = tick_idx * 0.020
            out = self.engine.evaluate_tick(
                tick(handle=42, raw_cid=cid, anim_id=anim_id, anim_time_s=t_s),
                cfg(lead_ms=0), now_ms_rel=tick_idx * 20,
            )
            # Only count FIRE-class decisions on WINDOW 0 specifically.
            for d in out:
                if d.window_ordinal != 0:
                    continue
                if d.action in (ACTION_FIRE, ACTION_LATE_INSIDE_WINDOW):
                    window0_fires.append((t_s, d.action))

        self.assertEqual(
            len(window0_fires), 1,
            f"Expected exactly 1 FIRE for c{cid} anim {anim_id} window 0; "
            f"got {len(window0_fires)}. Window=[{win.open_s:.3f},{win.close_s:.3f}]; "
            f"fires={window0_fires}",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
