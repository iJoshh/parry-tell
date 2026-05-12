---
status: draft
phase: 3
started: 2026-05-07
revised: 2026-05-07 (post Codex CEO + eng reviews — audio-first MVP, layered ship, handle-keyed state)
project: elden-ring-parry-indicator-mod
codename: parry-tell
supersedes: PHASE1-PLAN.md (kept for product-spec reference; build plan in this doc)
---

# Phase 3 Plan — `parry-tell.dll` Production Mod

## Read this first

PHASE1-PLAN.md was the original product spec. PHASE2 (probe v2-v5f) closed
2026-05-06: probe works, player-chain access pattern locked, empirical data in
hand. This Phase 3 plan replaces PHASE1's build-plan section but inherits its
product spec verbatim — **goal, non-goals, cue state machine wiring,
co-op-safety constraints, and quality bar are unchanged.**

This plan was rewritten 2026-05-07 after a CEO-mode review by Codex flagged
that the original "ship full hue+audio v1 together" ordering frontloaded
the highest-risk work (D3D12 rendering) and gave Josh nothing usable until
week 4. The revision: **ship audio-only MVP at week 1, layer in hue + target
awareness across weeks 2-4.**

## Confidence (revised post-CEO-review)

- **MVP audio-only ships within 1 week** (by 2026-05-14): **80%**
- **Full v1 (hue + audio + target awareness) ships within 4 weeks** (by 2026-06-04): **70%**
- **v1 ships eventually**: **95%**

The MVP gives Josh visible progress fast and proves the data pipeline. Each
subsequent layer adds functionality without invalidating prior work.

## Layered ship strategy

**Important version-naming rule (eng-review feedback):** `v1.0.0` is reserved
for the full PHASE1-PLAN spec — hue + audio + target + lock-on. ANY release
that ships less than that gets a `v0.x` tag and is labeled "stable" but not
"v1." We will not ship a degraded path under the v1 name.

**No cosmetic version bumps:** if a layer's offset hunting falls to a
fallback that makes the layer functionally identical to the prior layer
(e.g., 3.1.B falls to Option A → L1 has no new behavior over MVP), we skip
that release tag and roll directly to the next real capability layer.

| Ship | Date target | What ships | Why this layer |
|---|---|---|---|
| **MVP** (`v0.1.0-alpha`) | 2026-05-14 | TAE data + animation read + boss-bar walk + audio cue at window-open | Proves the data pipeline + delivers core utility (timing) |
| **L1** (`v0.2.0`) | 2026-05-21 | + per-boss target-aware filter (only cue when boss targets Josh) | Cuts false positives. **Skip if 3.1.B → Option A.** |
| **L2** (`v0.3.0`) | 2026-05-28 | + D3D12 hue overlay (Primary color only, no Alert variant yet) | Adds visual cue without lock-on complexity |
| **v1** (`v1.0.0`) | 2026-06-04 | + lock-on awareness (Primary vs Alert), + INI config, + hardening + GitHub release | Full PHASE1-PLAN v1 spec |
| **v0.x-stable** (fallback) | TBD | The highest layer that actually shipped | Used if D3D12 (3.5) fails — we ship audio+target as `v0.3.0-stable`, NOT `v1.0.0` |

If D3D12 (Phase 3.5) fails, we ship the highest working layer as v0.x-stable.
"v1" requires hue. This is non-negotiable per PHASE1-PLAN.md product spec.

## Phase overview

| Phase | What | Time est. | Josh time | Gate to next | Ship layer |
|---|---|---|---|---|---|
| 3.0 | TAE extraction (one-and-done, per PHASE1) | 0.5 day Josh + 1 day Claude | ~3hr | `parry_data.json` exists with version metadata | MVP prereq |
| 3.1.A | Probe v6: boss-bar + animation read offsets | 1-2 days | ~1hr | Boss-bar walk works; animation ID + time read confirmed | MVP prereq |
| 3.2 | Audio cue plumbing (PlaySoundW + WAV) | 0.5 day | 15min | Tone plays on demand | → MVP ship |
| 3.3 | MVP wiring + smoke test + release | 1 day | 1hr | Audio cue fires on Crucible Knight kick | **MVP shipped** |
| 3.1.B | Probe v6 cont'd: target-of-boss offset | 1-2 days | ~1hr | Target field found OR Option A fallback adopted | → L1 ship |
| 3.4 | L1 wiring + co-op test + release | 1 day | 1.5hr | False positives cut; co-op session clean | **L1 shipped** |
| 3.5 | D3D12 rendering hook + hue overlay | 2-3 days | 1hr | Hue draws cleanly, no crash, alt-tab safe | → L2 ship |
| 3.6 | L2 wiring + release | 0.5 day | 30min | Hue draws on real attacks | **L2 shipped** |
| 3.1.C | Probe v6 cont'd: lock-on target offset | 1-2 days | ~1hr | Lock-on read works OR fallback decided | → v1 ship |
| 3.7 | v1 wiring: lock-on, Primary vs Alert, INI, polish | 2 days | 1hr | Per-attack color decisions match spec | → v1 ship |
| 3.8 | Production hardening (test matrix from PHASE1) | 2-3 days | ~3hr | All matrix entries pass | → v1 ship |
| 3.9 | GitHub release v1.0.0 | 0.5 day | 15min | Tag + binary + README live | **v1 shipped** |

**Sum: ~14-19 days calendar, ~13 hours Josh's keyboard time, four shipping milestones.**

---

## Phase 3.0 — TAE extraction (the data prerequisite)

**Why this comes first:** without parry-window frame data per attack, even the
MVP can't fire the audio cue at the right time.

**Reference:** `EXTRACTION-PLAN.md` (already written, 13KB).

**Steps:**

1. Josh installs UXM Selective Unpacker on station (~15 min)
2. Josh runs UXM against installed Elden Ring (~15 min)
3. Josh installs WitchyBND, unpacks the .anibnd archives in EXTRACTION-PLAN.md (~30 min)
4. Josh zips the extracted XML and drops it in `/mnt/station-projects/elden-ring/extracted/` (~5 min)
5. Claude parses the XML offline into `data/parry_data.json`. **Required JSON
   metadata block** (CEO review feedback):
   ```json
   {
     "_meta": {
       "game_version": "2.6.1.0",
       "extracted_at": "2026-05-07T14:00:00-05:00",
       "parser_version": "1.0.0",
       "extraction_method": "UXM 0.34 + WitchyBND 2.13",
       "archive_sha256": { "c0000.anibnd.dcx": "abc...", ... }
     },
     "attacks": { "60100": { ... }, ... }
   }
   ```
6. Josh runs Steam "verify integrity of game files" to restore vanilla install (~15 min)

**Phase 3.0 success gates:**
- `data/parry_data.json` committed with `_meta` block populated (including archive sha256s)
- Entries for at least: Margit, Crucible Knight, Banished Knight, Godrick phase 1
- Steam file-verify completed and Josh confirms vanilla install restored

**Mismatch remediation branch (eng-review feedback):**
If 3.1.A's animation-ID-reading probe finds IDs that don't match
`parry_data.json`, walk this 3-step decision tree before proceeding:
1. **Version check:** does `parry_data._meta.game_version` match the live
   ER version? If not → re-extract on the current version.
2. **Archive hash check:** do `archive_sha256` entries match the runtime
   .anibnd files (Josh runs a one-line PowerShell hash, sends back). If
   not → wrong archive set was extracted; re-run extraction with the
   right files.
3. **Sentinel fixture test:** verify a known-stable animation ID (e.g.,
   Crucible Knight idle stance, ID known from multiple cheat tables).
   If sentinel matches but parryable attacks don't → TAE format drift in
   THIS patch; escalate to soulstruct parser fallback.

**Risk:** TAE format may have changed across patches. Mitigation: WitchyBND
is community-maintained. Fallback: soulstruct (Python). DO NOT proceed past
3.0 without verified parry-window timing for at least Crucible Knight (the
canonical MVP test target).

**Co-op safety:** UXM extraction modifies `regulation.bin` and other base
game files. Josh MUST run Steam file-verify before any future online play.
Encode in EXTRACTION-PLAN.md as the final step. Claude asks Josh to confirm
completion before declaring 3.0 done.

---

## Phase 3.1 — Probe v6: focused offset hunting (split per-target)

**CEO-review change:** split into 3.1.A/B/C, gated to ship layers. We don't
hunt all offsets up front — we hunt what each layer needs and ship.

**Architecture:** v6 is v5f + N additional armed sample groups, one hotkey
per group. Hook target unchanged (UpdateUIBarStructs); SEH-wrapped reads
unchanged; module-pin unchanged.

### Phase 3.1.A — MVP prereq (boss-bar + animation read)

**Confidence:** Medium-high for boss-bar walk and animation ID. **Medium
for animation time** (eng-review correction — only practice-tool uses
+0x24, via a different hook chain than ours; treat as needing empirical
confirmation, not a sure thing).

| Target | Why | First-try | Fallback |
|---|---|---|---|
| **Boss-bar walk via CSFeManImp** | Detect when a boss-bar is shown + which ChrIns each slot maps to | Sig-scan singleton; walk `+0x5BF0 + i*0x20 + 0x8` for i in 0..2; sentinel = UINT64_MAX | None — fully documented in PostureBarMod |
| **Animation ID on a ChrIns** | Detect which attack a boss is doing | `chrIns + 0x190 → +0x18 → +0xD0` | None — confirmed by 4 sources |
| **Animation time on a ChrIns** | Detect when parry window opens | `chrIns + 0x190 → +0x18 → +0x24` (and +0x2C for length) | TarnishedTool's lock-target code-cave hook at `er.exe + 0x717372` (invasive). **NOT raw UI-hook-tick frame-counting** — eng review rejected as not FPS-stable enough for parry timing. |

**Hard kill criterion (eng-review feedback):** if after 2 days of 3.1.A we
don't have a working animation-time read, escalate to Josh with three
options: (a) commit to the code-cave hook fallback (~1-2 extra days), (b)
defer hue+audio v1 until time-read works, (c) ship audio at +0x24's
best-effort accuracy with a known-error-bar caveat. Default in absence of
Josh: option (a).

Hotkey assignments: F8 (boss walk), F9 (animation), F10 (animation time).
F11 stays master arm.

**Test plan:**
- F8 + Crucible Knight fight: bossHpBars[0] populates with non-sentinel handle within 5-10 frames of bar appearing
- F9 + same fight: animation ID changes when boss starts a swing
- F10 + same fight: animation time field climbs from ~0 to ~0.8s during a kick

**Phase 3.1.A success gates:**
- Boss handle resolves through GetChrInsFromHandle to a real ChrIns
- Animation ID values match what `parry_data.json` predicts for known attacks
- Animation time monotonically increases during attack OR frame-counting
  fallback decided (Codex notes practice-tool uses +0x24 in production)

### Phase 3.1.B — L1 prereq (target-of-boss + Mimic-filter offsets)

**Eng-review feedback:** the Mimic/ally PvE-only filter needs three offsets
none of which are confirmed in v5f probe data: `chrType` (PostureBarMod
says +0x64 on ChrIns), `team-id` (offset unknown), and a way to identify
known summoned-ally entity-IDs. 3.1.B now bundles all three with the
target-of-boss search.

| Target | Why | First-try | Fallback |
|---|---|---|---|
| **Target-of-boss field on AI struct** | "Is this boss targeting Josh?" — Gate 0.B | `bossChrIns + aiThink + SpEffectObserveEntry.Target` per archaeology/09 | Dump `aiThink + 0xE000..0xF000` and diff during target-switch events |
| **chrType offset on a resolved ChrIns** | PvE-only filter: skip if entity is a player-class | `chrIns + 0x64` (PostureBarMod) | TarnishedTool says `chrIns + 0x64` (ChrId in their naming) — same offset, cross-confirmed |
| **team-id offset** | Belt-and-suspenders for Mimic Tear (team-type matches host even when chrType is "phantom-like") | `chrIns + 0x6C` (TarnishedTool TeamType) | Probe-walk near +0x6C for plausible byte values |
| **Summoned-ally entity-ID list** | Final fallback: hardcoded do-not-cue list of known IDs (Mimic Tear = 5354000, etc.) | Capture during testing; build list from observed values | None needed — list grows organically |

**Test plan:** in solo, summon Mimic Tear in a 2v1 fight. Watch boss switch
focus. Hotkey F12 captures target-id field plus chrType + team-id of every
non-player ChrIns visible. Cross-reference with mimic vs Josh visible
target on screen.

**Phase 3.1.B success gates:**
- Target field flips when ground-truth target switches OR Option A fallback
  decided. **If Option A: skip the v0.2.0 release tag entirely** (eng-review
  feedback — no cosmetic bumps); roll into L2 directly.
- chrType + team-id + at-least-Mimic-Tear-ID confirmed

### Phase 3.1.C — v1 prereq (lock-on target)

| Target | Why | First-try | Fallback |
|---|---|---|---|
| **Lock-on target on PlayerIns** | "Is Josh locked onto this attacking boss?" Primary vs Alert color | Read `playerIns + 0x6A0` as uint64_t handle | TarnishedTool code-cave hook at `er.exe + 0x717372` (invasive, adds ~1 day) |

**Test plan:** lock onto Crucible Knight, sample for 10s, break lock, sample
for 10s. Expect non-zero handle then zero/UINT64_MAX.

**Phase 3.1.C success gates:**
- Lock-on read works OR Option B fallback adopted (no Alert color, only
  Primary — single-color hue regardless of which boss you're looking at)

### Phase 3.1 off-ramps

If 3.1.B or 3.1.C primary AND fallback both fail, escalate to Josh with
explicit choice:
- (a) accept reduced scope and ship the layer below (e.g., L1 ships without
  target-of-boss → cue fires for any boss parryable attack)
- (b) commit another 1-2 days of probe iteration

Default if Josh is away: take (a). Reduced scope still beats no mod.

---

## Phase 3.2 — Audio cue plumbing

**Moved before D3D12 per CEO review.** Audio is the core utility (timing) and
has zero rendering risk.

**API:** `PlaySoundW(L"#101", g_module, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT)`.
Asset embedded as Win32 resource ID 101. Per Codex architecture research:
PlaySoundW is the simplest API that satisfies all constraints (async,
thread-safe, lazy-init winmm, no DllMain interaction).

**Cooldown:** simple 80ms minimum between triggers. NOT a configurable INI
knob in MVP (CEO review: cut INI complexity for v1).

**Threading:** trigger from the state-machine tick on the game thread (via
UpdateUIBarStructs hook). PlaySoundW is async by default.

**Phase 3.2 success gates:**
- Tone plays on demand from a test-only F12 trigger
- Cooldown works (rapid-fire triggers don't stack tones)
- No crash on game shutdown

---

## Phase 3.3 — MVP wiring + ship

**Goal:** simplest possible working mod. Ship it.

**State machine (eng-review-corrected: handle-keyed, animTime-rewind aware):**

```cpp
struct BossState {
    uint32_t prevAnimId = 0;
    float prevAnimTime = -1.0f;
    bool windowOpenPrev = false;
    bool consumed = false;        // one-shot per animation instance
};

// Keyed by bossHandle, NOT slot index — survives boss-bar reorder
std::unordered_map<uint64_t, BossState> states;

void Tick() {
    auto bars = SampleBossHpBars();

    // Build set of handles seen this frame
    std::unordered_set<uint64_t> activeHandles;
    for (int i = 0; i < 3; i++) {
        if (bars[i].handle != UINT64_MAX) activeHandles.insert(bars[i].handle);
    }

    // Garbage-collect entries whose boss-bar disappeared
    for (auto it = states.begin(); it != states.end(); ) {
        if (!activeHandles.count(it->first)) it = states.erase(it);
        else ++it;
    }

    // Process each active boss
    for (uint64_t h : activeHandles) {
        auto& s = states[h];  // creates default-initialized if new
        void* chr = ResolveChrIns(h);
        if (!chr) continue;

        uint32_t animId = ReadAnimId(chr);
        float animTime = ReadAnimTime(chr);

        // Reset consumption on:
        //   (a) animation ID change (new attack)
        //   (b) animation TIME rewind (same anim ID replayed — game looped it)
        bool animChanged = (animId != s.prevAnimId);
        bool animRewound = (s.prevAnimTime > 0 && animTime < s.prevAnimTime - 0.05f);
        if (animChanged || animRewound) {
            s.consumed = false;
            s.windowOpenPrev = false;
        }
        s.prevAnimId = animId;
        s.prevAnimTime = animTime;

        auto* atk = LookupAttack(animId);  // parry_data.json
        if (!atk || atk->disable_parry) continue;

        bool windowOpenNow = animTime >= atk->window_start_sec;
        if (windowOpenNow && !s.windowOpenPrev && !s.consumed) {
            audio.Trigger();
            s.consumed = true;
        }
        s.windowOpenPrev = windowOpenNow;
    }
}
```

**Why handle-keyed and animTime-rewind aware** (eng-review feedback):
- Boss-bar slot reorder (e.g., one of 3 simultaneous bosses dies) used to
  reset our state in v1's by-slot code. Handle-keyed survives that.
- Same boss replaying the same parryable animation back-to-back used to
  suppress the second cue (because animId didn't change so consumed stayed
  true). animTime-rewind detection fixes that.

That's it for MVP. No target-of-boss filter (cue fires whenever ANY
boss-bar boss does a parryable attack). No hue. No lock-on.

**Required pre-ship test (eng-review feedback):**
1. Same boss, same parryable attack, twice in <2s — expect TWO audio events
2. 30/60/120 FPS capped runs of the same attack — cue timestamp drift <50ms
   across all three FPS settings (this is the test that validates we're
   reading real animTime, not accumulating UI-hook ticks)

**Phase 3.3 success gates:**
- Audio cue fires within ~50ms of parry window opening on Crucible Knight kick
- Audio cue does NOT fire on non-parryable swings
- 5-min combat session, no crash, no false positives ratio worse than 1:5
- README + LICENSE + tagged GitHub release `v0.1.0-alpha` with binary

---

## Phase 3.4 — L1: target-of-boss filter + ship

**Add:** read target-of-boss field from each boss ChrIns. Only fire audio if
`bossTargetHandle == playerHandle`.

**Phase 3.4 success gates (eng-review-amended):**
- **Solo+Mimic test (BLOCKING for ship):** cue does not fire when boss
  targets the Mimic; cue fires when boss targets Josh
- **Co-op test (post-ship validation, NOT blocking):** Josh + 1 friend, cue
  fires only when boss targets Josh. Run when friend is available; if not
  available within 1 week of L1 ship, ship anyway and follow up.

**Release:** `v0.2.0` with notes "added target awareness — no more cues for
your friend's beating." Do NOT ship if 3.1.B fell to Option A (no new
behavior over MVP); roll into L2 instead.

---

## Phase 3.5 — D3D12 rendering hook + hue overlay

**Strategy:** PostureBarMod's pattern.

### Hooks

- `IDXGISwapChain::Present` (vtable index 140) — actual draw point
- `ID3D12CommandQueue::ExecuteCommandLists` (vtable index 54) — one-time
  capture of the direct command queue pointer
- `IDXGISwapChain::ResizeTarget` (vtable index 146) — invalidate cached
  resources on resolution change

All three: MinHook, sig-scanned via dummy-device pattern (PostureBarMod's
`D3DRenderer.cpp:303` builds a hidden window + dummy device to get vtable
addresses). Vendored MinHook is already in `probe/vendor/`.

### Caveat

PostureBarMod's `DisableAll()` has a hook-cleanup bug. Don't copy. Per CEO
review: "no dynamic unload support; process-lifetime only" — we don't need
clean unhooking. Process exit reaps everything.

### Render path

ImGui ≥1.90 vendored locally. Use `ImDrawList::AddRect` + `AddRectFilledMultiColor`
on the foreground draw list. ~80 LOC for the entire overlay.

### Animation

- Fade in over 60ms when state goes On
- Hold at `opacity_max` (default 0.55)
- Fade out over 440ms when state goes Off OR audio cue fires

### Color (L2 only Primary)

L2 ships single Primary color. L1's target filter applies (only when boss
targets Josh). Lock-on awareness deferred to v1.

**Phase 3.5 success gates:**
- Hue draws on screen during a controlled F12-held trigger
- No frame-rate hitch
- Survives alt-tab, resolution change, 30+ minute draw session
- No GPU resource leak (steady VRAM)

---

## Phase 3.6 — L2 ship

**Add:** wire hue overlay to state machine. Hue On when boss targets Josh
during parryable attack; Off otherwise. Single Primary color.

**Release:** `v0.3.0` with notes "visual cue added — screen-edge hue when boss
targets you with a parryable attack."

---

## Phase 3.7 — v1 wiring: lock-on, Primary vs Alert, INI

**Add:**

- Lock-on read (3.1.C result)
- Color logic per PHASE1-PLAN.md goals #4:
  - locked onto attacker → Primary
  - locked onto different boss while attacker targets Josh → Alert
  - no lock-on, attacker targets Josh → Primary
- INI config (mINI vendored from `posturebarmod/Source/Ini/ini.h`)

**INI schema (CEO-review-trimmed to 5 knobs):**

```ini
[overlay]
primary_rgb = 80,170,255
alert_rgb = 255,90,60
opacity_max = 0.55

[audio]
enabled = true
volume = 0.80
```

That's it. Thickness, fade times, cooldown, hotkey — hardcoded.

**Validation:** clamp opacity to [0,1], volume to [0,1]. Invalid value →
default + log warning.

---

## Phase 3.8 — Production hardening

Test matrix (from PHASE1-PLAN, with CEO-review additions):

| Test | Duration | Pass criteria |
|---|---|---|
| Solo Crucible Knight, alt-tab repeatedly | 10 min | No crash, hue resumes |
| Solo Godrick (multi-phase) | 15 min | Phase transitions handled |
| Resolution change mid-fight | once | No crash, overlay re-inits |
| Co-op as guest with Mimic Tear | 30 min | Mimic doesn't trigger PvE-only filter false-positives |
| Co-op as guest with friend | 60 min | No desync, friend's view unaffected |
| Stress: parry every parryable attack | 10 min | Audio tracks reliably |
| Crash recovery: kill via Task Manager | once | Game survives DLL crash |
| FileVersion mismatch (force-load on wrong patch) | once | Mod refuses to attach + logs reason |
| Mimic + summoned phantom + invader (chaos test) | 15 min | Cue fires only for boss-bar entities targeting Josh |

**CEO-review additions:**
- FileVersion fail-closed gate (already in v5f, promote to README + tested)
- "Process-lifetime only, no unload" stated in README
- Mimic-team-type filter: PostureBarMod uses chrType. We add chrType + team-id
  + a do-not-cue list of known summoned-ally entity-IDs as belt-and-suspenders

**Parry tools for testing:** Parrying Dagger from Twin Maiden Husks
(Roundtable Hold, 1600 runes), or Buckler from Limgrave nomadic merchant
(1800 runes).

---

## Phase 3.9 — GitHub release v1.0.0

**Deliverables:**

- `README.md`: install instructions (drop into EML's Game\mods\), Defender
  exclusion note, supported-bosses list, screenshot/video, **explicit "No
  runtime DLL reload/unload supported; any DLL or INI change requires full
  game restart" warning** (eng-review feedback)
- `LICENSE`: MIT (already in repo)
- `CHANGELOG.md`: full layered ship history (v0.1.0-alpha → v1.0.0)
- Pre-built `parry-tell.dll` attached
- Source tag: `v1.0.0`
- `version.txt`: `parry-tell v1.0.0 — built for ER 2.6.1.0`

**Nexus:** out of v1 scope. Decide post-ship.

---

## What survives intact from PHASE1-PLAN.md

- Product spec (cue state machine, color wiring per goals #4)
- Non-goals (no regulation.bin, no PvP, no host-side, no telemetry)
- PvE-only filter philosophy
- Quality bar checklist
- Risks table

## What's stale in PHASE1-PLAN.md (do not use)

- Lines 67-117: pseudocode shows TarnishedTool's PlayerIns access pattern. We
  use PostureBarMod's `WCM + 0x10EF8` + handle round-trip.
- Step-0 preflight gates: all completed.
- Step-1 "Gate 0 spike": that was Phase 2, done.
- "TarnishedTool offsets" in the State block: module-base-relative, we
  resolve via signature scan instead.
- Original 70%/85% confidence: replaced with the layered 80%/70%/95% in
  this plan.
- "Single ship" assumption: replaced with layered MVP/L1/L2/v1 ship strategy.

## Open questions for Josh (need answers before kicking off Phase 3.0)

1. **Approve the layered ship strategy?** MVP at week 1, full v1 by week 4.
2. **Is now the right time for TAE extraction (Phase 3.0)?** ~3hr Josh
   keyboard time + Steam file-verify after. Probe v5f stays clean if we
   delay; nothing breaks.
3. **For probe v6 (Phase 3.1), are you OK running ~3 short test sessions
   spread across the week** (one per offset target group), or batched?
4. **L1's co-op test needs a friend's session.** Available in week 2-3?

## Approval checklist before Phase 3.0 kicks off

- [ ] Josh accepts the layered MVP/L1/L2/v1 strategy
- [ ] Josh accepts 80%/70%/95% confidence and 14-19 day window
- [ ] Open questions 1-4 answered
- [ ] Phase 2 probe disabled before TAE extraction (rename to `.dll.old` so
       it doesn't interact with UXM-modified game files)
- [ ] HANDOFF.md updated to point at this plan as active
- [ ] Frontmatter status flipped from `draft` to `accepted`

## Session Log

### 2026-05-08 — v6 probe built, staged, and analysis pipeline complete

**Accomplishments**

1. **v6 source written and built** — ~3,076 lines of C++ implementing the
   locked v6 spec. INI config parser (fail-closed), 64 MB SPSC ring buffer,
   worker thread with binary + CSV + diagnostics output, CSFeManImp sig-scan,
   WCM enemy roster behind 7-check quarantine, TimeAct + ai_struct walks,
   three-tier sampling, adaptive stepdown, session manifest, smoke calibration
   report. Built clean via MSBuild first try.
2. **Codex review addressed** — 6 fixes applied, 1 blocker fixed (roster
   pass split), 1 blocker declined with documented reasoning (detour compute
   rule), 1 TODO deferred to v6.1 (delta encoding).
3. **Post-capture analysis pipeline written** — 7 files in `tools/`:
   `probe_bin.py`, `probe_status.py`, `qualify_oracle.py`,
   `analyze_discovery.py`, `probe_diag.py`, `rebuild-and-stage.sh`,
   plus self-tests `test_probe_bin.py` (PASS) and `test_qualify_oracle.py`
   (PASS, 8 real DB parry windows).
4. **Self-service tooling pre-empted friction** — `swap-mode.bat` for INI
   swaps; three `GAMEPLAY-*.txt` phone scripts; `probe_status.py` top-line
   VERDICT; `probe_diag.py` log aggregator.
5. **Staged on station** — DLL + smoke INI dropped into `Game\mods\`; v5f
   preserved as `.dll.disabled`; logs dir created; stage dir populated.
6. **Wrap-up email sent to Josh** via Resend with all playtest steps.

**Current state**

- DLL is live in `Game\mods\`, smoke INI loaded. Josh can launch the game.
- All analysis tools tested with synthetic data; both self-tests PASS.
- HEAD `6db35ca` pushed to `origin/main`; working tree clean.

**Next steps (priority order)**

1. Josh runs smoke test (60 s at any Grace, 8-step deliberate-action script
   from `GAMEPLAY-smoke.txt`).
2. Josh tells Claude "smoke done"; Claude runs `probe_status.py` + reads
   `.calibration.txt`.
3. If smoke PASS → Josh runs `swap-mode.bat qualification`, then 2–3 min vs
   Banished Knight.
4. Josh tells Claude "qualification done"; Claude runs `qualify_oracle.py`.
5. If qualification PASS → Josh runs `swap-mode.bat discovery`, then ~1 hr
   Stormveil + boss.
6. Claude runs `analyze_discovery.py` on the ~5–10 GB capture.
7. If discovery finds the parry-active flag → production mod uses Path B;
   otherwise Path A (database lookup) per this plan.

**Ruled out this session**

- `D:\parry-tell-logs\` — D: drive does not exist on station; switched to
  `C:\Projects\elden-ring\logs\`.
- `E:\parry-tell-logs\` — E: not writable by `claude` user.
- `GetChrInsFromHandle(wcm, &stack_handle_copy)` for boss-bar handle
  resolution outside the roster — v5e debugging proved the function returns
  input unchanged when given a stack pointer; documented as known limitation.
- Worker-side delta encoding in v6 — deferred to v6.1; not blocking.

### 2026-05-11 — Probe v6.4 production; three offset questions resolved; co-op tooling shipped

**Accomplishments**

1. **Research-006 dispatched + completed.** Dual deep-research (Claude skill + Codex CLI) across five axes: vswarte/eldenring-rs, TGA Cheat Engine Table v1.17, Erd-Tools, TarnishedTool, Mordrog PostureBarMod. Investigated three ER 2.6.1 ChrIns offset bugs from v6.1.1: world position (`+0x6C0` noise), enemy anim_id (`TimeAct+0xD0` returning 0), player lock-on (`+0x6A0` pointer-shaped value). Synthesis written to `research/006-SYNTHESIS.md`.

2. **Fixture verification refuted the vswarte anim_queue model.** Proposed `TimeActModule + 0x20 + read_idx*16` queue was all sentinels in the c4382 fixture bytes. Bundle-fix approach abandoned; instrumentation build commissioned instead.

3. **Probe v6.2 instrumentation build.** Schema v2. 48-byte Tier 1 player block + 40-byte enemy header; three new region IDs (6/7/8). Codex deep-critic pre-deploy: CSV header drift (P1), region 4/8 overlap (P1), comment-vs-code drift (P2) — all fixed before capture.

4. **Research-007 (v6.2 capture analysis).** 8,773 focused rows of c4382 Knight at Stormveil Gatefront. Q1 (world pos) → phys-chain wins. Q3 (lock-on) → `+0x6B0` wins (17 transitions vs 0 for `+0x6A0`). Q2 (enemy anim_id) → dead end; all three paths sentinel/zero — stationary-enemy artifact.

5. **Probe v6.3 module-bag-wide instrumentation.** REGION_MODULE_BAG_MEMBER (9) wide-scans ChrModuleBag[0..0x100]. Switched `in_lock_on` to `+0x6B0` — fixed `focus_reason=3-always` bug AND silently-broken boss-bar gating.

6. **Research-008 (v6.3 capture analysis) — Q2 SOLVED.** 12,467 focused rows, ~144 s, c4311 Godrick Soldier (74%) + c4382 Knight (10%). Path A (`TimeAct + 0xD0`) was always correct: 9,265 nonzero reads, 89 transitions, clean anim_time monotonicity. v6.2 zeros were a stationary-enemy artifact. **The probe was never the bug.**

7. **Probe v6.4 production build** deployed to `/mnt/station-mods/`. Drops instrumentation regions 6/7/8/9. Co-op safety: 8 WCM_PLAYER_ARRAY slots scanned, all friendly `chr_ins` excluded from roster sweeps. Audible F11 feedback via `Beep()`. All v6.2/v6.3 wire-format additions retained.

8. **Supporting tools shipped:** `tools/probe-status.ps1` (PowerShell tailer, deployed to station), `tools/archive_session.sh` (SMB → local archive with shard support), `tools/segment_by_f11.py` (per-F11-cycle segment manifests with epoch translation). All three had Codex deep-critic passes; P1 findings fixed before deploy.

9. **HANDOFF.md** rewritten with full tonight-session operating manual at top.

**Deep-critic gatekeeping summary**

- v6.2: 3 findings (2 P1, 1 P2) — all fixed before capture session
- v6.3: 2 findings (2 P1) — all fixed before capture session
- v6.4 tooling: 3 findings (2 P1, 1 P2) — all fixed before deploy

**Current state**

- Probe v6.4 live at `/mnt/station-mods/parry-tell-probe.dll` (228 KB).
- PowerShell tailer live at `C:\Projects\elden-ring\probe-status.ps1`.
- Three offset questions resolved with HIGH confidence; probe is functionally correct for tonight's multi-boss co-op session.
- 19 commits unpushed on `main`; session-close tag pending.

**Next steps (priority order)**

1. Tonight: Josh plays multi-boss co-op session; Claude archives + segments per boss-done report.
2. Next session: implement DB join-key fuzzy mapping in `qualify_oracle.py` (`c4382` individual variant → `c4380` parent family). ~30-line Python change.
3. Once join-key works: achieve qualification PASS on c2130 Banished Knight (79 parry windows) or c4380 Knight (53 windows).
4. Build the actual parry-prediction analyzer.

**Ruled out this session**

- `TimeActModule + 0x20 + read_idx*16` (vswarte anim_queue) for enemy anim_id: refuted by v6.2 fixture (all sentinels), confirmed v6.3 (still sentinels — queue not used for AI-controlled enemies).
- `ActionRequestModule + 0x90` (Erd-Tools path) for enemy anim_id: sentinel in both v6.2 and v6.3.
- Module-bag-wide brute-force for c4380 anim IDs: v6.3 found only stable structural fields, not anim_id.
- Path A "TimeAct + 0xD0" tentatively concluded WRONG in research-006/v6.2 — that conclusion was wrong; v6.2 sample was stationary. v6.3 confirmed path A is correct.
