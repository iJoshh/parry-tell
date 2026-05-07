# parry-tell — HANDOFF

**Last session:** 2026-05-07, ~16:15 CT. Phase 3 Session 1 (TAE extraction)
COMPLETE. Compacting before Session 1.5 (parsing).
**Status:** All 807 ER character anibnds extracted. 64,385 anim-*.xml files
staged on Projects share. Ready for VM-side parsing work.

## Where we are

- **Extraction done.** 807/807 anibnds (100% success, 0 failures in final run).
- **Data location for parsing:** `/mnt/station-projects/elden-ring/chr-extracted/`
  - 807 top-level dirs, one per ER character (named `c{NNNN}[_variant]-anibnd-dcx-wanibnd/`)
  - 106,493 files / 2.51 GB total
  - 64,385 `anim-*.xml` files (the actual TAE event-track data we came for)
  - Each character dir contains: `_witchy-anibnd4.xml` (binder manifest) + a tree
    of TAE XML files. Two layouts observed:
    - **Boss layout** (most c2xxx-c8xxx): `{outDir}/tae/{cNNNN}-tae/anim-NNNNNN.xml`
    - **Player layout** (c0000 family): `{outDir}/INTERROOT_win64/chr/c0000/tae/{a00,a01,...}-tae/anim-NNNNNN.xml`
  - `hkx_compendium/` and `Model/` subdirs were excluded during copy (skeleton
    physics + mesh data, not relevant to parry-window detection).
- **Sample anim XML head** (verified parses as valid):
    ```xml
    <?xml version="1.0" encoding="utf-8"?>
    <anim>
      <name>a000_000000.hkt</name>
      <header>...</header>
      <events>...</events>  <!-- THIS is what we care about -->
    </anim>
    ```

## Immediate next action — parsing

Goal: produce `data/parry_data.json` at `/home/joshua.blattner/claude/elden-ring/data/parry_data.json`
with `_meta` block per PHASE3-PLAN.md, then run sentinel-fixture validation.

### What to extract from each anim XML

The `<events>` block contains TAE event tracks. Per Phase 2 research
(`research/phase3-offsets-codex.md`), the events relevant to parry detection are:
- `InvokeParryHitback` / `InvokeParry*` event types — these mark the parry
  window
- Event start/end frame indices (or time, normalized to seconds)

For each event we want, capture: animId (filename), event type, start time,
end time, character ID (from parent dir name).

### Sentinel-fixture validation

Pick a known-stable boss whose parry window is documented in community wikis
(Crucible Knight kick / `c4100_a000_000040` is a strong candidate — has the
canonical kick parry and is well-documented). Confirm the parsed data shows
a parry window in approximately the right frames (~12-20f at 30fps based on
SoulsModding wiki references).

### Required `_meta` block (per PHASE3-PLAN.md)

```json
"_meta": {
  "game_version": "2.6.1.0",
  "extracted_at": "<from chr-extracted dir mtime>",
  "parser_version": "1.0.0",
  "extraction_method": "UXM Selective Unpack 2.4.2 + WitchyBND v3.0.0.1",
  "archive_sha256": { ... <hashes of source .anibnd.dcx if available> }
}
```

The `archive_sha256` field is harder now that source .dcx files weren't copied
to the share — they're still on Josh's `C:\` install. If we want hashes, I'd
need to SSH-list them. Lower priority than getting the parser working.

## Key file map

### On VM (`/home/joshua.blattner/claude/elden-ring/`)
- `PHASE3-PLAN.md` — production build plan (status: draft, awaiting Josh
  signoff). Ship strategy: MVP at week 1, full v1 at week 4.
- `EXTRACTION-PLAN.md` — extraction playbook (now complete).
- `SESSION-ASKS.md` — pre-batched playbook for upcoming sessions (Session 2
  is probe v6 multi-target offset hunt; not started yet).
- `HANDOFF.md` — this file.
- `research/phase3-architecture-codex.md` — D3D12/audio/state machine.
- `research/phase3-offsets-codex.md` — pointer chains for 5 fields.
- `research/phase3-{ceo,eng}-review.md` — adversarial review history.
- `probe/probe.cpp` — v5f source, ~860 lines. Working, currently disabled on
  station.

### On Projects share (`/mnt/station-projects/elden-ring/`)
- `chr-extracted/` — **THE INPUT FOR PARSING.** 807 character dirs.
- `extracted-tae-batch.log` — extraction batch log. Final: `Total: 807 |
  Processed: 574 | Skipped: 233 | Failed: 0 | Wall: 16.2 min`.
- `chr-copy.log` — robocopy log (mostly empty since `/LOG+` was buggy then
  fixed).

### Tools on station (in case of re-extraction need)
- `\\localhost\Projects\tools\UXM\` — UXM Selective Unpack 2.4.2
- `\\localhost\Projects\tools\WitchyBND\` — WitchyBND v3.0.0.1
- `\\localhost\Projects\tools\README.md` — install manifest with SHA256s

### Scripts on station (for re-extraction or post-mortem)
- `C:\Users\claude\witchy-batch-v4.ps1` — final working batch script
- `C:\Users\claude\witchy-press-enter-helper.ps1` — Enter-presser helper
- (Older: `witchy-batch.ps1`, `witchy-batch-v2.ps1`, `witchy-batch-v3.ps1` —
  superseded; ignore)

## What survives across compact (don't re-discover)

### Workflow rules
- **SSH access:** `claude@100.110.26.9` via `~/.ssh/station_key`. Non-admin
  account. Can read `C:\Projects\` (= `\\STATION\Projects\`), `C:\Program
  Files (x86)\Steam\steamapps\common\ELDEN RING\`, but NOT `C:\Users\Josh\`.
- **SMB topology:** VM has `/mnt/station-projects/` (RO) and `/mnt/station-mods/`
  (RW). Station has `\\localhost\Projects\` and `\\localhost\mods\` (both RW).
- **PowerShell over SSH gotcha:** WitchyBND v3 uses PromptPlus which hard-fails
  without TTY. Running `& exe args` from PowerShell over SSH bombs at init.
  Mitigation: `Start-Process -Wait -WindowStyle Normal` from an interactive
  PowerShell window works (drag-and-drop equivalent).
- **Helper-PowerShell pattern that worked:** Two-window setup with the
  Enter-presser helper running in Window 2, main batch in Window 1. Auto-press
  via `PostMessage(WM_KEYDOWN/UP, VK_RETURN)`. Window 2 cleared 800+ "press any
  key" prompts hands-off.
- **WitchyBND output layout VARIES BY CHARACTER** — don't assume
  `{outDir}/tae/...`. Always recursive-glob anim-*.xml. The completion marker
  is `_witchy-anibnd4.xml` at the top of the output dir.
- **WitchyBND legitimately produces dirs with ZERO anim-*.xml** for auxiliary
  binders (`c0000_a00_hi`, etc — skeleton/overlay variants of c0000). Use
  marker presence, not anim count, as completeness signal.

### Things I broke and fixed mid-session (don't repeat)
- `-WindowStyle Minimized` causes 48-min hangs. Use `Normal`.
- "Tighter resume check" using `{outDir}/tae/*.xml` glob misses player layout.
  Recursive `anim-*.xml` glob works for both layouts.
- Em-dashes (`—`) get UTF-8-mangled through the bash → scp → PowerShell stack
  and break PS scripts. Always ASCII-only in scripts.
- Settings in WitchyBND's `appsettings.json` (`EndDelay`, `PauseOnError`,
  `Offline`) DO persist correctly via JSON edit BUT do NOT disable the
  hard-coded "Press any key to continue..." gate.

### Critical decisions still active
- **Layered ship strategy** (CEO-reviewed): MVP audio-only week 1, target
  filter L1 week 2, hue L2 week 3, lock-on + INI v1 week 4. v1.0.0 is
  reserved for the full PHASE1-PLAN spec.
- **State machine handle-keyed** (eng-reviewed), reset on `(animId change)
  OR (animTime rewind)` with rewind threshold 0.05f.
- **Animation time read** is medium confidence (+0x24 chain not yet verified
  empirically). Fallback is TarnishedTool's lock-target code-cave hook.
- **Critic plugin auto-fires** after every Write/Edit. Verdicts append to
  tool result. Address before final summary per global protocol.

### Workflow lessons from this session (Josh-stated preferences)
- **Don't overengineer.** Josh: "Seems like it was working fine in the
  beginning, just needed a tweak or something. Instead we broke it
  completely. I can't tell if we just wrote over good extracts or not."
  When the first version mostly works, fix the specific bug; don't rewrite.
- **Codex consult mode is valuable** when stuck or about to overengineer.
  This session's Codex consult correctly identified that the bug was the
  success predicate, not the lack of automation.
- **Don't make Josh press Enter 800 times.** The auto-press helper was the
  right call after Josh asked for it.
- **Pre-batched session-asks are still the standard.** Josh confirmed twice
  this session.

## Open questions / followups (not blocking parsing)

1. **Source .anibnd.dcx hashing for `archive_sha256`:** files are on Josh's
  `C:\` ER install, not on Projects share. Need to SSH-hash them if we want
  the meta block fully populated. Lower priority than parser correctness.
2. **Steam file-verify timing:** Josh CAN do this now since we have a copy
  on the share, but he hasn't. Probe DLL is still disabled.
3. **`ersc_launcher.exe` is from June 2024** (~1.7.x era). Modern Seamless
  is 1.8+. Not blocking, but worth a Seamless update before live testing.
4. **Probe v6 design** (Phase 3.1) is unstarted. SESSION-ASKS.md Session 2
  documents what's needed.

## Confidence reminder (unchanged from pre-session)

- MVP audio-only by 2026-05-14: 80%
- Full v1 by 2026-06-04: 70%
- v1 ships eventually: 95%

Josh expects faster than 4 weeks.

## Next-session opening line (for post-compact me)

"You just compacted. Extraction Session 1 is complete. Read this HANDOFF.md.
The data is at `/mnt/station-projects/elden-ring/chr-extracted/` — 807 char
dirs, 64,385 anim-*.xml files. Goal: build the TAE parser, produce
`data/parry_data.json`, run sentinel validation. PHASE3-PLAN.md Phase 3.0
has the parser spec. Ask Josh before you start writing significant code; he
may have new direction since session close."
