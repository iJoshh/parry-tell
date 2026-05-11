# Research 006 — Synthesis (Claude DR + Codex DR + Fixture Verify)

**Date:** 2026-05-11 (America/Chicago)
**Inputs:**
- `006-PROMPT-er-2.6.1-chrins-offsets.md` — the dispatch prompt
- `006-claude-deep-research.findings.jsonl` — 5-axis sub-agent findings (34 findings, 4 primary sources)
- `006-codex-research.md` — independent Codex pass (16 web searches, 45 commands, fixture-aware)
- `006-fixture-verification.md` — byte-level verification against `data/research-fixture/`

## TL;DR

Three offset bugs, three different outcomes:

| Bug | Claude DR proposal | Codex DR position | Fixture verify | **Final call** |
|---|---|---|---|---|
| **World position** | `ChrIns→+0x190→+0x68→+0x70` (Vector3) | Same | Entrypoint + first hop BYTE-VERIFIED stable across 3 samples | **SHIP** behind feature flag w/ dual-read |
| **Enemy anim_id** | `TimeActModule + 0x20 + read_idx*16` (anim_queue) | Add `ActionRequest` fallback at `bag→+0x80→+0x90`, KEEP old +0xD0 read | Both proposals REFUTED for c4382 — queue is all sentinels, child blocks contain zero c4380 anim IDs | **DO NOT SHIP either**. Need wider capture surface first. |
| **Player lock-on** | `PlayerIns + 0x6B0` (FieldInsHandle) | `+0x6B0` IF this is PlayerIns*; flags object-type ambiguity | Can't byte-verify (no PlayerIns in fixture) | **SHIP with dual-read** of both `+0x6A0` and `+0x6B0`. |

The bundled "three offsets in one patch" approach is dead. Replaced by a **probe v6.2 = instrumentation build**: dual-read everything ambiguous, expand capture surface, then ship v6.3 with the confirmed answers.

## Where the four sources agreed

Four independent primary sources converged on the same struct layout:

1. **vswarte/eldenring-rs** (Rust crate, RTTI-derived from live 2.6.1 binary, last commit 2026-04-01)
2. **The Grand Archives TGA Cheat Engine Table v1.17.0** (2025-08-04, supports ER 2.6.x)
3. **Erd-Tools** (Nordgaren, last commit 2026-03-14)
4. **TarnishedTool** (borgCode, last commit 2026-05-11 — the SAME DAY as this research)

Probe paths the 4-source consensus **CONFIRMS as already correct**:
- `ChrIns + 0x190` → ChrModuleBag pointer
- `ChrModuleBag + 0x18` → CSChrTimeActModule pointer
- `ChrIns + 0x60`, `+0x64` → c-id family / category fields (current probe already uses these)

Probe paths the 4-source consensus **CONFIRMS as wrong**:
- `ChrIns + 0x6C0` for world position — this is PlayerIns minimap UI coord, not a ChrIns world position
- `PlayerIns + 0x6A0` for lock-on — this lands inside `player_menu_ctrl` pointer (a different sub-system)

## Where the byte-level fixture diverged from the source consensus

**The anim_queue story.** vswarte's `CSChrTimeActModuleAnim` struct says TimeActModule has a 10-entry × 16-byte anim_queue at +0x20, with read_idx at +0xC4. Our fixture says:

- Across all 3 samples of an actively-fighting c4382 Knight:
  - `write_idx = read_idx = 0` (never advanced)
  - All 10 queue entries are sentinel pattern: `(anim_id=-1, play_time=0.0, play_time2=0.0, anim_length=1.0)`
  - First 0xC0 bytes of TimeActModule are NOT a queue but a **pointer table to TimeActChild blocks** (pointers like `0x00007FF77D634640`)
- Even brute-force u32 scan of every captured region for any c4380 anim ID returned **zero hits**

This is a one-source-vs-bytes disagreement. vswarte's struct is RTTI-derived for ER 2.6.1, so it's not wrong in the abstract — but it doesn't model how data is actually laid out FOR ENEMIES IN c4382 in this specific build's runtime. Possible explanations:

1. **Player-only:** the anim_queue at +0x20 is real but populated only for player-controlled chars; AI uses a different module path (Erd-Tools' `ActionRequest` model, per Codex).
2. **Hash-table layout:** TimeActModule's +0x00..+0xC8 region holds pointers to per-anim TimeActChild blocks; the current anim's child is reachable via the pointer table indexed by some hash.
3. **Wrong char_ins:** the probe is hashing the right struct address but reading a stale/parent ChrIns whose modules aren't active.
4. **Different module for combat anims:** Erd-Tools points at `ChrModuleBag + 0x80 = ActionRequest module`; combat anims may live there for AI chars.

The fixture only contains 12.5KB across 12 regions. None of the captured surface contains the live anim ID. To find it, we need to capture wider — specifically the `ActionRequest` module body and the bodies of TimeActChild pointers that the TimeActModule head table points at.

## Codex's contributions beyond Claude DR

1. **The `ActionRequest` alternative path** — Erd-Tools points at `module_bag + 0x80 → +0x90` for the current animation, distinct from the `time_act` path. This is a fallback hypothesis Claude's axes didn't surface.
2. **Object-type ambiguity at the player pointer** — Codex flagged that the probe variable named `playerChrIns` might actually be a base `ChrIns*` (not `PlayerIns*`). If so, `+0x6A0` could be a `ChrIns`-level target field, and `+0x6B0` would only apply if the pointer is actually `PlayerIns*`. This needs a vtable-pointer check in the next probe.
3. **Conservative patch shape** — Codex's proposed diff keeps the old +0xD0 read AND adds the ActionRequest fallback when +0xD0 returns 0 — preserving the working player-side read while adding enemy-side recovery.

## Open contradictions (deliberately NOT resolved)

1. **Enemy anim_id path:** TarnishedTool says `TimeActModule + 0xD0` (works for player only in our probe); Erd-Tools says `ActionRequest module +0x90` (untested for c4382 in this fixture); fixture refutes both at the captured surface. **Best bet: capture the ActionRequest module body and re-verify.**

2. **Lock-on target struct base:** PostureBarMod/Erd-Tools-CPP put target handle at `ChrIns + 0x6A0` (chr-level); Erd-Tools and TGA put it at `PlayerIns + 0x6B0` (subclass-level). Reconciliation: ChrIns has a `+0x6A0` field for any chr's last-attacker / soft-target; PlayerIns extends ChrIns and adds a hard lock-on handle at `+0x6B0`. **Best bet: dual-read both; the toggle correlation will identify which is which.**

3. **vswarte struct layout vs runtime bytes:** the `anim_queue` at +0x20 is empty in our capture; layout might apply only to certain character classes or game states (paused, dead, transition).

## Confidence summary

- **World position fix:** HIGH confidence the chain `+0x190→+0x68→+0x70` is correct. Source consensus is 4 primary, byte-verified at 2 of 3 hops, leaf Vector3 unverified.
- **Lock-on fix:** MEDIUM confidence `+0x6B0` is the right answer. Source consensus is 4 primary; type-of-pointer ambiguity is the residual risk.
- **Enemy anim_id fix:** LOW confidence in any specific offset. Source consensus disagrees AND fixture refutes the strongest candidate. Need an instrumentation pass before any commitment.

---

# Plan for the next run (Probe v6.2 — Instrumentation Build)

**Goal:** ship a probe that captures enough data to definitively answer all three offset questions in ONE more capture session, then ship probe v6.3 with the answers locked in.

## What v6.2 will do differently from v6.1.1

### 1. World position — dual-path read + leaf capture

- Keep the existing `ChrIns + 0x6C0` read as `world_pos_legacy_x/y/z` (3 floats)
- ADD new read via `ChrIns → +0x190 → +0x68 → +0x70` to `world_pos_phys_x/y/z` (3 floats)
- Capture both per focused-row write
- Also capture the **CSChrPhysicsModule body** (256 bytes starting at the dereferenced pointer) as a new region payload — so if the leaf offset is wrong by a few bytes we have the surrounding context to find it

Expected: phys path will produce floats in the 0..2000m range matching minimap-displayed coords; legacy path will produce the noise we've already been seeing. After one capture confirms, v6.3 drops the legacy read.

### 2. Player lock-on — dual-path read + object-type sanity

- Keep existing `playerChrIns + 0x6A0` read as `lock_on_legacy` (u64)
- ADD `playerChrIns + 0x6B0` read as `lock_on_new` (u64) and `playerChrIns + 0x6B4` as `lock_on_area_new` (u32)
- ADD a **vtable read** at `playerChrIns + 0x00` (u64) — captured as `player_chr_ins_vtable_ptr`. This lets us check whether the probe is reading from a PlayerIns vtable or ChrIns vtable (cross-reference against vswarte's known vtable RVAs in the binary).
- Have Josh do a 60s capture where he intentionally locks on / off / cycles targets several times. Compare which field changes correctly with lock-on toggles.

Expected: one of the two will match the toggle pattern and produce FieldInsHandle-shaped values; the other will be a constant pointer. The toggle-correlation pattern identifies the right one. v6.3 drops the wrong one.

### 3. Enemy anim_id — instrument multiple paths + expand capture surface

This is the hard one. Three parallel reads + expanded capture:

- Path A (current): `TimeActModule + 0xD0` — kept, named `anim_id_path_a`
- Path B (vswarte queue): `TimeActModule + 0x20 + (read_idx@+0xC4)*16` — named `anim_id_path_b`
- Path C (Erd-Tools ActionRequest): `module_bag → +0x80 → +0x90` — named `anim_id_path_c`

ADD new region payload captures:
- **ActionRequest module body** (`module_bag + 0x80` dereferenced, 512 bytes) — region `12_action_request`
- **First child of TimeActModule head pointer table** — read u64 at `time_act_module + 0x00`, dereference, capture 512 bytes — region `13_time_act_child_0_body`
- **Second + third children** — same pattern at `+0x08`, `+0x10` — regions `14`, `15`

ADD a brute-force online scan inside the probe: for each focused-row write, scan the captured regions 4-byte-aligned u32 against an embedded list of c4380 anim IDs and emit `anim_id_scan_hits` field listing any (region, offset, anim_id) hits.

Expected: ONE of paths A/B/C will produce a valid anim_id, OR the online scan will find the field in one of the new wider regions. Either way, the next analysis run pinpoints the answer.

### 4. Other (free wins)

- **Move c-id read** to use `ChrIns + 0x60 // 10000` (already works) AND `ChrIns + 0x64` (also already works) — both already in v6.1.1. Just rename them in the wire format from generic `field_at_0xNN` to `c_id_family` and `c_id_direct` for clarity.
- **Increase capture rate from 60Hz to 120Hz** for one capture — give the analyzer 2x sample density so within-anim transitions are clearer. Already supported by the probe's tight loop; just an ini toggle.
- **Add `probe_v6_2_session.log`** with the timestamps at which Josh locked on/off — this is just a debug-print line every time the lock-on legacy or new value transitions. Helps correlate the dual-read offline.

## Build + deploy plan

1. **Edit `probe/probe.cpp`** locally — apply the dual-read pattern from Codex's diff as the starting point, then extend with paths B/C and the wider region captures
2. **Codex review** — `mcp_Codex_codex` second-opinion read on the probe.cpp diff before build (writer-pairing rule: I'm writing this so Codex reviews)
3. **Bump probe version** to v6.2
4. **SCP to station + MSBuild via SSH** (per the existing workflow)
5. **Read DLL back via SMB** to `probe/releases/probe-v6.2.tar.gz`
6. **Wait for Josh's "ready to reload"** signal (game must be closed)
7. **Deploy DLL** to `/mnt/station-mods/parry-tell-probe.dll`, delete stale `.csv`
8. **Josh runs the test:** ~60 seconds of combat with a c4380 family Knight at Gatefront, intentionally locks on / off multiple times during the fight
9. **Read capture back** via SMB, copy local per the SMB-perf rule, analyze

## What the next-run analysis will produce

A definitive 3-row answer table:

| Field | Probe v6.1.1 says | Probe v6.2 dual-read confirms | Probe v6.3 ships |
|---|---|---|---|
| World position | `ChrIns+0x6C0` (wrong) | `phys+0x70` produces valid coords | `phys+0x70` |
| Lock-on | `playerChrIns+0x6A0` (wrong) | `+0x6B0` (or `+0x6A0`) tracks toggles | confirmed value |
| Enemy anim_id | `time_act+0xD0` returns 0 | one of paths A/B/C OR scan hit | confirmed path |

After this, qualification can PASS (the join key already works at `field_at_0x064`; we just need the anim_id to feed `qualify_oracle.py` step 2).

## Time estimate

- v6.2 source edits: **45 min** (the patch is mechanical; the hard part is widening the capture serializer to emit 4 new region payloads + the path-A/B/C reads)
- Codex review + revisions: **20 min**
- Build + deploy: **20 min** (SCP + MSBuild + DLL copy)
- Josh plays one capture: **5-10 min**
- Analysis: **20 min** (the existing `inspect_capture.py` + `qualify_oracle.py` will spot the right paths immediately)
- v6.3 with confirmed offsets: **30 min** (delete dual-reads, keep winners)

**Total to qualification PASS: ~2.5 hours of work, ~10 min of Josh playing.**

Compare to the previous trajectory: 4 rebuild-replay cycles at ~30 min of Josh's time each (~2 hours), still no PASS. The instrumentation pass collapses ALL ambiguities in one capture instead of resolving them one at a time.

## What I need from Josh to start

- **Confirm the plan** — especially the "one more capture session with intentional lock-on toggles" framing. If you'd rather I bias toward LESS instrumentation (e.g. trust the 4-source consensus on world-pos + lock-on and only instrument anim_id), I can split v6.2a (world+lock fixes shipped immediately) and v6.2b (anim_id-only instrumentation).
- **SSH access to station** — the service should be up if it's still on from before; will verify with `station_ssh 'echo ok'` before starting the build.

Once green-lit, I'll start by editing the probe source.
