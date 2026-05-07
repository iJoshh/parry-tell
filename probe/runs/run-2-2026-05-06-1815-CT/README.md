# Run 2 — probe v2 — 2026-05-06 ~18:15 CT

**Probe version:** v2 patched (md5 of tarball: `755742fb341bdfd68448ae69eb635e31`)
**Game:** Elden Ring 1.16.1, eldenring.exe base = `0x00007FF6C9FB0000`
**Test type:** Josh ran in-game, ~28 seconds of capture, no parryable enemies fought
**Operator:** Josh (Windows, station)

## Files

- `STATION.log` — DebugView export, ~109KB, 13 probe banner lines
- `parry-tell-probe.csv` — probe output, ~954KB, 13,232 lines (5 heartbeats + 13,225 comment lines, mostly spam)

## Key findings

- Plumbing GREEN end-to-end: WorldChrMan resolved at `0x00007FF425572D70`, CSV opened, capture loop entered.
- **Player ChrIns RESOLUTION FAILED:** startup banner shows `player ChrIns: 0x0000000000000000`. Player slot at `WCM + 0x1E508` was null at first capture moment.
- 7,969 `# snapshot failed due to null/invalid chain; skipping frame` comments
- 5,247 `# read failure(s) this frame: count=2 first_addr=0x000463000078E9C2` comments — fault address `0xE9C2` = `AI_LAST_ACT` offset, suggests prio queue walk hitting invalid ChrIns entries (Codex's diagnosis)

## What this informed

probe v3 — adds per-frame player chain re-sampling, hop logging, spam dedup. See `../releases/probe-v3.tar.gz`.
