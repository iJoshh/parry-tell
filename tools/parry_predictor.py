#!/usr/bin/env python3
"""Reference predictor decision engine — Python port of probe/probe.cpp.

This is the algorithmic mirror of the C++ engine. It exists to test the
predictor's behavior on the VM without needing to build and run the DLL
in-game. Every constant, every reset trigger, every decision branch
matches the C++ implementation at probe/probe.cpp:1023-1480.

The C++ engine is the source of truth at runtime. This Python port is
the source of truth for "what SHOULD the engine do for input X." If they
diverge, fix the divergence — don't paper over it.

Usage:
    >>> from tools.parry_predictor import (
    ...     PredictorEngine, PredictionConfig, BossTickInput,
    ...     ACTION_FIRE, ACTION_BEFORE_LEAD,
    ... )
    >>> engine = PredictorEngine(db_path='data/parry_data.bin')
    >>> cfg = PredictionConfig(audio_cue_lead_ms=0, target_filter_enabled=False)
    >>> tick = BossTickInput(
    ...     boss_handle=0x1234, raw_cid=4310, anim_id=4001100,
    ...     anim_time_s=0.45, target_known=False, target_match=False,
    ... )
    >>> decisions = engine.evaluate_tick(tick, cfg, now_ms_rel=100)

Constants kept in sync with probe.cpp:
    BOSS_DISAPPEAR_GRACE_MS         = 250
    ANIM_REWIND_TOLERANCE_S         = 0.050
    ANIM_ID_DEBOUNCE_POLLS          = 2
    MAX_WINDOWS_PER_ANIM_LATCH      = 32
    AI_STRUCT_TARGET_SENTINEL       = 0xFFFFFFFFFFFFFFFF
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Constants — MUST match probe/probe.cpp
# ---------------------------------------------------------------------------

BOSS_DISAPPEAR_GRACE_MS = 250
ANIM_REWIND_TOLERANCE_S = 0.050
ANIM_ID_DEBOUNCE_POLLS = 2
MAX_WINDOWS_PER_ANIM_LATCH = 32

# Match the DLL's binary loader limits.
PARRY_DB_MAGIC = b"PTPD"
PARRY_DB_VERSION = 1
MAX_FILE_SIZE = 16 * 1024 * 1024
MAX_META_JSON_BYTES = 8191
MAX_ANIM_COUNT_PER_CHAR = 100000
MAX_CHAR_COUNT = 10000


# ---------------------------------------------------------------------------
# PredictionAction enum — MUST match enum in probe.cpp
# ---------------------------------------------------------------------------

ACTION_NO_DB = 0
ACTION_NO_KEY = 1
ACTION_BEFORE_LEAD = 2
ACTION_FIRE = 3
ACTION_LATCHED = 4
ACTION_LATE_TARGET_SWITCH = 5
ACTION_LATE_INSIDE_WINDOW = 6
ACTION_SUPPRESSED_TARGET = 7
ACTION_SUPPRESSED_POST_WINDOW = 8
ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW = 9


_ACTION_NAMES = {
    ACTION_NO_DB: "no_db",
    ACTION_NO_KEY: "no_key",
    ACTION_BEFORE_LEAD: "before_lead",
    ACTION_FIRE: "fire",
    ACTION_LATCHED: "latched",
    ACTION_LATE_TARGET_SWITCH: "fire_late_target_switch",
    ACTION_LATE_INSIDE_WINDOW: "fire_late_inside_window",
    ACTION_SUPPRESSED_TARGET: "suppressed_target",
    ACTION_SUPPRESSED_POST_WINDOW: "suppressed_post_window",
    ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW: "suppressed_negative_lead_exceeds_window",
}


def action_name(a: int) -> str:
    return _ACTION_NAMES.get(a, "unknown")


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------


@dataclass
class ParryWindow:
    open_s: float
    close_s: float


@dataclass
class PredictionConfig:
    audio_cue_lead_ms: int = 0
    target_filter_enabled: bool = False


@dataclass
class BossTickInput:
    boss_handle: int = 0
    boss_chr_ins: int = 0   # for logging parity with C++; not used in math
    raw_cid: int = 0
    anim_id: int = 0
    anim_time_s: float = 0.0
    target_match: bool = False
    target_known: bool = False


@dataclass
class BossPredictState:
    """Mirrors the C++ struct at probe.cpp:1023.

    Fields are written by BumpInstanceSeqIfNeeded and read by
    evaluate_window. Single-thread access — no locking.
    """
    resolved_cid: int = 0
    prev_anim_id: int = 0
    prev_anim_time_s: float = 0.0

    pending_anim_id: int = 0
    pending_count: int = 0

    anim_instance_seq: int = 0
    consumed_windows: int = 0    # uint32 bitmap

    last_seen_ms: int = 0
    ever_seen: bool = False


@dataclass
class PredictionDecision:
    ts_ms_rel: int = 0
    boss_handle: int = 0
    boss_chr_ins: int = 0
    raw_cid: int = 0
    resolved_cid: int = 0
    anim_id: int = 0
    anim_time_s: float = 0.0
    window_ordinal: int = 0xFFFF
    window_open_s: float = 0.0
    window_close_s: float = 0.0
    lead_time_ms: float = 0.0
    configured_lead_ms: int = 0
    target_filter_enabled: bool = False
    target_match: bool = False
    anim_instance_seq: int = 0
    action: int = ACTION_NO_KEY


# ---------------------------------------------------------------------------
# Binary DB loader (Python equivalent of LoadParryDb in C++)
# ---------------------------------------------------------------------------


def _read_exact(buf: bytes, pos: int, n: int) -> tuple[bytes, int]:
    if pos + n > len(buf):
        raise ValueError(
            f"truncated read at pos={pos} want={n} have={len(buf) - pos}"
        )
    return buf[pos : pos + n], pos + n


def load_parry_db(path: str | Path) -> dict[int, list[ParryWindow]]:
    """Read parry_data.bin into a dict[(cid << 32) | anim_id] -> windows.

    Same packing as the C++ PackParryKey: `(uint64(cid) << 32) | uint32(anim_id)`.
    """
    p = Path(path)
    fsize = p.stat().st_size
    if fsize <= 0:
        raise ValueError(f"empty file: {p}")
    if fsize > MAX_FILE_SIZE:
        raise ValueError(f"file size {fsize} exceeds limit {MAX_FILE_SIZE}")

    with p.open("rb") as fh:
        buf = fh.read()

    pos = 0
    magic, pos = _read_exact(buf, pos, 4)
    if magic != PARRY_DB_MAGIC:
        raise ValueError(f"bad magic {magic!r}")
    ver_bytes, pos = _read_exact(buf, pos, 2)
    (version,) = struct.unpack("<H", ver_bytes)
    if version != PARRY_DB_VERSION:
        raise ValueError(f"version {version} != {PARRY_DB_VERSION}")
    meta_len_bytes, pos = _read_exact(buf, pos, 2)
    (meta_len,) = struct.unpack("<H", meta_len_bytes)
    if meta_len > MAX_META_JSON_BYTES:
        raise ValueError(f"meta_len {meta_len} too large")
    _meta_bytes, pos = _read_exact(buf, pos, meta_len)

    char_count_bytes, pos = _read_exact(buf, pos, 4)
    (char_count,) = struct.unpack("<I", char_count_bytes)
    if char_count > MAX_CHAR_COUNT:
        raise ValueError(f"char_count {char_count} too large")

    db: dict[int, list[ParryWindow]] = {}
    for _ in range(char_count):
        hdr, pos = _read_exact(buf, pos, 8)
        cid, anim_count = struct.unpack("<II", hdr)
        if anim_count > MAX_ANIM_COUNT_PER_CHAR:
            raise ValueError(f"c{cid} anim_count {anim_count} too large")
        for _ in range(anim_count):
            ah, pos = _read_exact(buf, pos, 6)
            anim_id, window_count = struct.unpack("<IH", ah)
            windows: list[ParryWindow] = []
            for _ in range(window_count):
                wb, pos = _read_exact(buf, pos, 8)
                op, cl = struct.unpack("<ff", wb)
                windows.append(ParryWindow(op, cl))
            if window_count > 0:
                key = (cid << 32) | (anim_id & 0xFFFFFFFF)
                # Mirror the C++ loader's first-wins policy at
                # probe.cpp:914-921. The build tool guarantees no
                # duplicates, but if one slips in, keep the first row
                # (matching what the DLL would do).
                if key not in db:
                    db[key] = windows
    return db


# ---------------------------------------------------------------------------
# Engine — mirror of probe.cpp Resolve/Bump/EvaluatePredictionWindow logic
# ---------------------------------------------------------------------------


class PredictorEngine:
    """Pure-Python port of the C++ engine. Same algorithm, same constants.

    Stateful: tracks per-boss latch state across ticks. Reset by calling
    `reset()`.
    """

    def __init__(self, db_path: str | Path | None = None,
                 db: Optional[dict[int, list[ParryWindow]]] = None) -> None:
        # db_loaded mirrors the C++ g_parryDbLoaded atomic. The DB is
        # considered "loaded" only when an explicit db dict or db_path was
        # provided (even if the resulting dict is empty — an empty but
        # successfully-loaded DB should emit NO_KEY, not NO_DB, matching
        # the runtime behavior at probe.cpp:1373/1396).
        if db is not None:
            self.db = db
            self.db_loaded = True
        elif db_path is not None:
            self.db = load_parry_db(db_path)
            self.db_loaded = True
        else:
            self.db = {}
            self.db_loaded = False
        # Per-boss state, keyed by boss FieldInsHandle (u64).
        self.boss_state: dict[int, BossPredictState] = {}

    def reset(self) -> None:
        """Clear all per-boss state. Equivalent to fresh worker init."""
        self.boss_state.clear()

    # ---- Lookup helpers (mirror ResolveCidForLookup + LookupParryWindows) ----

    @staticmethod
    def _pack_key(cid: int, anim_id: int) -> int:
        return (cid << 32) | (anim_id & 0xFFFFFFFF)

    def _resolve_cid(self, raw_cid: int, anim_id: int) -> int:
        """Exact-first + single-step family fallback. Returns 0 on no-match."""
        if raw_cid < 1000:
            return 0
        if self._pack_key(raw_cid, anim_id) in self.db:
            return raw_cid
        family = (raw_cid // 10) * 10
        if family == raw_cid:
            return 0
        if self._pack_key(family, anim_id) in self.db:
            return family
        return 0

    def _lookup_windows(self, resolved_cid: int,
                        anim_id: int) -> Optional[list[ParryWindow]]:
        return self.db.get(self._pack_key(resolved_cid, anim_id))

    # ---- Latch state machine (mirror BumpInstanceSeqIfNeeded) ----

    def _bump_instance_seq_if_needed(
        self,
        st: BossPredictState,
        resolved_cid: int,
        anim_id: int,
        anim_time_s: float,
        now_ms_rel: int,
    ) -> bool:
        """Returns True iff anim_instance_seq was incremented this tick."""
        if not st.ever_seen:
            st.resolved_cid = resolved_cid
            st.prev_anim_id = anim_id
            st.prev_anim_time_s = anim_time_s
            st.anim_instance_seq = 1
            st.consumed_windows = 0
            st.last_seen_ms = now_ms_rel
            st.ever_seen = True
            st.pending_anim_id = 0
            st.pending_count = 0
            return True

        reset = False

        # (1) Disappearance > grace.
        if (now_ms_rel - st.last_seen_ms) > BOSS_DISAPPEAR_GRACE_MS:
            reset = True

        # (2) Resolved c-id changed.
        if resolved_cid != 0 and resolved_cid != st.resolved_cid:
            reset = True

        # (3) anim_id changed with debounce.
        if anim_id != st.prev_anim_id:
            if anim_id == st.pending_anim_id:
                st.pending_count += 1
                if st.pending_count >= ANIM_ID_DEBOUNCE_POLLS:
                    reset = True
                    st.pending_anim_id = 0
                    st.pending_count = 0
            else:
                st.pending_anim_id = anim_id
                st.pending_count = 1
        else:
            st.pending_anim_id = 0
            st.pending_count = 0

        # (4) anim_time rewind > tolerance on same anim.
        if anim_id == st.prev_anim_id:
            dt = st.prev_anim_time_s - anim_time_s
            if dt > ANIM_REWIND_TOLERANCE_S:
                reset = True

        if reset:
            st.resolved_cid = resolved_cid
            st.prev_anim_id = anim_id
            st.prev_anim_time_s = anim_time_s
            st.anim_instance_seq += 1
            st.consumed_windows = 0
        else:
            st.prev_anim_time_s = anim_time_s

        st.last_seen_ms = now_ms_rel
        return reset

    # ---- Per-window evaluator (mirror EvaluatePredictionWindow) ----

    def _evaluate_window(
        self,
        st: BossPredictState,
        in_: BossTickInput,
        cfg: PredictionConfig,
        window: ParryWindow,
        window_ordinal: int,
        now_ms_rel: int,
    ) -> PredictionDecision:
        d = PredictionDecision(
            ts_ms_rel=now_ms_rel,
            boss_handle=in_.boss_handle,
            boss_chr_ins=in_.boss_chr_ins,
            raw_cid=in_.raw_cid,
            resolved_cid=st.resolved_cid,
            anim_id=in_.anim_id,
            anim_time_s=in_.anim_time_s,
            window_ordinal=window_ordinal,
            window_open_s=window.open_s,
            window_close_s=window.close_s,
            lead_time_ms=(window.open_s - in_.anim_time_s) * 1000.0,
            configured_lead_ms=cfg.audio_cue_lead_ms,
            target_filter_enabled=cfg.target_filter_enabled,
            target_match=in_.target_match,
            anim_instance_seq=st.anim_instance_seq,
        )

        if window_ordinal >= MAX_WINDOWS_PER_ANIM_LATCH:
            d.action = ACTION_NO_KEY
            return d

        # NaN/Inf guard — must match C++ std::isfinite check.
        if (not math.isfinite(in_.anim_time_s)
                or not math.isfinite(window.open_s)
                or not math.isfinite(window.close_s)):
            d.action = ACTION_NO_KEY
            return d

        bit = 1 << window_ordinal
        already_consumed = bool(st.consumed_windows & bit)

        # Past window-close: no cue.
        if in_.anim_time_s > window.close_s:
            d.action = ACTION_SUPPRESSED_POST_WINDOW
            return d

        if already_consumed:
            d.action = ACTION_LATCHED
            return d

        # Target filter.
        if cfg.target_filter_enabled:
            if not in_.target_known or not in_.target_match:
                d.action = ACTION_SUPPRESSED_TARGET
                return d

        # Negative-lead path.
        if cfg.audio_cue_lead_ms < 0:
            fire_time_s = window.open_s + (-cfg.audio_cue_lead_ms / 1000.0)
            if fire_time_s > window.close_s:
                d.action = ACTION_SUPPRESSED_NEGATIVE_LEAD_EXCEEDS_WINDOW
                return d
            if in_.anim_time_s < fire_time_s:
                d.action = ACTION_BEFORE_LEAD
                return d
            st.consumed_windows |= bit
            d.action = ACTION_FIRE
            return d

        # Lead >= 0 path.
        if d.lead_time_ms > cfg.audio_cue_lead_ms:
            d.action = ACTION_BEFORE_LEAD
            return d

        # Strict > for late-inside (equality at open is on-time FIRE).
        if in_.anim_time_s > window.open_s:
            st.consumed_windows |= bit
            d.action = ACTION_LATE_INSIDE_WINDOW
            return d

        st.consumed_windows |= bit
        d.action = ACTION_FIRE
        return d

    # ---- Public entry point (mirror EvaluatePredictionTick) ----

    def evaluate_tick(
        self,
        in_: BossTickInput,
        cfg: PredictionConfig,
        now_ms_rel: int,
    ) -> list[PredictionDecision]:
        """Run one predictor tick for one boss. Returns decisions emitted.

        The C++ EvaluatePredictionTick writes decisions to a JSONL sink.
        This Python equivalent returns them as a list so tests can inspect.
        """
        resolved_cid = self._resolve_cid(in_.raw_cid, in_.anim_id)

        st = self.boss_state.setdefault(in_.boss_handle, BossPredictState())
        self._bump_instance_seq_if_needed(
            st, resolved_cid, in_.anim_id, in_.anim_time_s, now_ms_rel,
        )

        # NO_DB fires only when no DB was ever provided to the engine
        # (default constructor). A successfully loaded but empty DB falls
        # through to the NO_KEY path below, matching the C++ runtime.
        if not self.db_loaded:
            d = PredictionDecision(
                ts_ms_rel=now_ms_rel,
                boss_handle=in_.boss_handle,
                boss_chr_ins=in_.boss_chr_ins,
                raw_cid=in_.raw_cid,
                resolved_cid=0,
                anim_id=in_.anim_id,
                anim_time_s=in_.anim_time_s,
                configured_lead_ms=cfg.audio_cue_lead_ms,
                target_filter_enabled=cfg.target_filter_enabled,
                target_match=in_.target_match,
                anim_instance_seq=st.anim_instance_seq,
                action=ACTION_NO_DB,
            )
            return [d]

        if resolved_cid == 0:
            d = PredictionDecision(
                ts_ms_rel=now_ms_rel,
                boss_handle=in_.boss_handle,
                boss_chr_ins=in_.boss_chr_ins,
                raw_cid=in_.raw_cid,
                resolved_cid=0,
                anim_id=in_.anim_id,
                anim_time_s=in_.anim_time_s,
                configured_lead_ms=cfg.audio_cue_lead_ms,
                target_filter_enabled=cfg.target_filter_enabled,
                target_match=in_.target_match,
                anim_instance_seq=st.anim_instance_seq,
                action=ACTION_NO_KEY,
            )
            return [d]

        windows = self._lookup_windows(resolved_cid, in_.anim_id)
        if not windows:
            d = PredictionDecision(
                ts_ms_rel=now_ms_rel,
                boss_handle=in_.boss_handle,
                boss_chr_ins=in_.boss_chr_ins,
                raw_cid=in_.raw_cid,
                resolved_cid=resolved_cid,
                anim_id=in_.anim_id,
                anim_time_s=in_.anim_time_s,
                configured_lead_ms=cfg.audio_cue_lead_ms,
                target_filter_enabled=cfg.target_filter_enabled,
                target_match=in_.target_match,
                anim_instance_seq=st.anim_instance_seq,
                action=ACTION_NO_KEY,
            )
            return [d]

        out: list[PredictionDecision] = []
        for i, w in enumerate(windows):
            if i >= MAX_WINDOWS_PER_ANIM_LATCH:
                # Overflow guard — C++ emits one NO_KEY decision and stops.
                d = PredictionDecision(
                    ts_ms_rel=now_ms_rel,
                    boss_handle=in_.boss_handle,
                    raw_cid=in_.raw_cid,
                    resolved_cid=resolved_cid,
                    anim_id=in_.anim_id,
                    anim_time_s=in_.anim_time_s,
                    window_ordinal=i,
                    configured_lead_ms=cfg.audio_cue_lead_ms,
                    target_filter_enabled=cfg.target_filter_enabled,
                    target_match=in_.target_match,
                    anim_instance_seq=st.anim_instance_seq,
                    action=ACTION_NO_KEY,
                )
                out.append(d)
                break
            out.append(self._evaluate_window(st, in_, cfg, w, i, now_ms_rel))
        return out
