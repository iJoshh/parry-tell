# Session Asks — Pre-batched checklists for Phase 3

**Purpose:** when Josh sits down for a session, this is the one-stop doc.
Each session is a numbered checklist with everything needed: setup, the
exact in-game steps, what to capture, where to drop it. No mid-session
"can you also..." asks.

**Read order:** sessions are numbered in the order they happen. Don't skip
ahead — each one's data feeds the next.

---

## Session 1 — TAE extraction (~90 min active)

**Status:** Ready. Doc: `EXTRACTION-PLAN.md`.

**One-line summary:** UXM unpack ER, WitchyBND extract `c*.anibnd.dcx`,
zip, drop at `C:\Projects\elden-ring\extracted\`, tell Claude.

**What I (Claude) will do after:** parse the TAE XML into
`data/parry_data.json` with version metadata. Run sentinel-fixture test
(check Crucible Knight idle animation ID matches multiple cheat-table
references). Tell you when safe to file-verify.

**Pre-session asks (none mid-session):**

- [ ] Free up ~10GB on the drive that has your ER install
- [ ] Have your Steam library path ready (if non-default)
- [ ] Have phone within reach in case `oo2core_8_win64.dll` prompt fires

**During-session asks (only if something fails):**

- Steam library path if non-default
- "Done — zip is on Projects share" message

---

## Session 2 — Probe v6 + multi-target offset hunt (~60-90 min)

**Status:** Blocked on Session 1. Will be ready when `parry_data.json`
exists.

**One-line summary:** install probe v6, run 3-4 short scenarios, save the
DebugView log + CSVs, tell Claude.

**What's different from probe v5f:** v6 adds 4 sample groups, each
behind its own hotkey. F11 stays master arm. Each group writes its own
CSV file.

| Hotkey | Group | What it samples | Test scenario |
|---|---|---|---|
| F11 | Master arm | (toggles all sampling on/off) | n/a |
| F8 | Boss-bar walk | CSFeManImp.bossHpBars[0..2] handle + resolved ChrIns | Trigger any boss fight |
| F9 | Animation read | Active boss's animation ID + animTime over 30s | Stand in front of Crucible Knight |
| F10 | Lock-on read | playerIns + 0x6A0 | Lock onto a target, hold 30s, break lock |
| F12 | Target/Mimic filter | aiThink target field, chrType, team-id, all visible-entity IDs | 2v1 fight: Mimic Tear + Crucible Knight, alternate boss focus |

**Pre-session asks (consolidated, do once at the start):**

- [ ] Confirm probe v5f's `parry-tell-probe.dll.disabled` (or `.old`) is
       still disabled if extraction was recent — I'll redeploy v6 fresh.
       (If you re-enabled it, doesn't matter; v6 will overwrite.)
- [ ] Have DebugView open: download from
       https://learn.microsoft.com/en-us/sysinternals/downloads/debugview
       if not already installed. Each scenario below saves to its OWN
       filename (`STATION-2A.log`, `STATION-2B.log`, etc.) — don't
       overwrite a single log; I need to read them as separate captures.
- [ ] **Recommended boss for all four scenarios:** Crucible Knight at
       Stormhill Evergaol. Reliable parryable kick, ~3 min from grace.
       Note: Seamless Co-op removes the vanilla "no summons in
       evergaols" restriction (Josh confirmed 2026-05-07), so spirit
       ashes work fine here for 2.D. If you ever test outside Seamless,
       switch 2.D to Tree Sentinel (Limgrave, open world).
- [ ] **Have spirit ashes equipped.** Mimic Tear preferred for 2.D;
       Lone Wolves Ash works as backup.
- [ ] **Disable Steam overlay** (right-click ER in library → Properties
       → uncheck "Enable Steam Overlay") if you haven't already — it can
       interfere with hotkeys in some cases.

**During-session asks (only if something fails):**

- "F11 doesn't toggle anything" → I'll have you check DebugView for
  errors at startup
- "Crashed" → save the .dmp from `C:\Projects\elden-ring\` and tell me
- Anything weird → screenshot DebugView and post it

**Per-scenario steps (run all 4, ~15 min each):**

### 2.A — Boss-bar walk (F8)

1. Load into world, head to your chosen test boss (see "recommended
   bosses" above — Stormhill Evergaol Crucible Knight is fine for 2.A)
2. Engage the fight
3. Press F11 to master-arm, then F8 to enable group A
4. Stand still for 5 seconds (let probe sample)
5. Trigger boss to start moving (run at it / hit it once)
6. Sample for 30 seconds
7. Press F8 to disable group A, F11 to disarm
8. Save DebugView log: `C:\Projects\elden-ring\STATION-2A.log`
9. The probe will have written `parry-tell-probe-A.csv` to mods folder —
   I'll read it directly via SMB

### 2.B — Animation read (F9)

1. Same fight, still alive (or reset and re-enter)
2. F11 + F9 to arm group B
3. Don't attack. Just watch the boss's idle + occasional swings
4. After 30 seconds, attack the boss to trigger an animation cycle
5. Get hit by a kick (canonical parryable) — let it land, don't roll
6. Sample for 60 seconds total
7. F9 + F11 to disarm
8. Save log: `STATION-2B.log`

### 2.C — Lock-on read (F10)

1. Same fight (or reset)
2. F11 + F10 to arm group C
3. Lock onto Crucible Knight (R3 / right-stick click)
4. Hold lock for 30 seconds, walk around, swing
5. Press R3 again to break lock
6. Wait 10 seconds with no lock
7. Re-lock for 10 seconds
8. F10 + F11 to disarm
9. Save log: `STATION-2C.log`

### 2.D — Target/Mimic filter (F12)

1. Re-enter Crucible Knight evergaol (or whichever boss you used for
   2.A-C — same one is fine, summons work in evergaols under Seamless)
2. Engage the fight
3. **Summon Mimic Tear** (Spirit Calling Bell + Mimic ash). If you
   don't have Mimic, use Lone Wolves Ash or any humanoid summon that
   fights.
3. F11 + F12 to arm group D
4. Stand back. Let Mimic engage Crucible Knight first.
5. Wait for boss to focus on Mimic (~10 sec)
6. Run at boss to make it switch focus to you (~10 sec)
7. Run away to make it switch back (~10 sec)
8. Repeat 2-3 alternations
9. F12 + F11 to disarm
10. Save log: `STATION-2D.log`

**Done when:** all four `STATION-2{A,B,C,D}.log` files exist on the
Projects share. Tell me. I'll read CSVs + logs directly via SMB and
report which offsets worked + which need a fallback.

---

## Session 3 — Friend co-op test (this week, opportunistic)

**Status:** Ready as soon as MVP ships (audio-only). Can ALSO be used
to gather Seamless-guest slot data even before MVP, if you want — see
"bonus mode" below.

**Why now:** you said you'll be playing with friends over the next couple
nights. While you're at it, I'd like to capture two things that need
real co-op (not Mimic):

### 3.A — Seamless guest slot probe (~10 min, bonus, can do BEFORE MVP)

**Question we're answering:** is the local player at `playerArray[0]`
when you're a Seamless guest, or are you at slot 1/2/3? PostureBarMod
assumes slot 0; we've only verified solo so far.

**Setup:**
- [ ] Re-enable probe v5f if disabled. PowerShell:
       `Rename-Item "C:\Program Files (x86)\Steam\steamapps\common\ELDENRING\Game\mods\parry-tell-probe.dll.disabled" "parry-tell-probe.dll"`
       Or via Explorer: rename to add the `.dll` back.
- [ ] Have DebugView running, capture-on
- [ ] Friend launches ER via Seamless as host. You join as guest.
- [ ] **Quick heads-up to friend:** "I'm running a guest-only diagnostic
       mod that captures memory state from my client. It logs entity
       positions and IDs that my client already renders for me — nothing
       leaves my machine, nothing affects your game. Cool?" If they say
       no, skip 3.A and 3.B until they're OK with it. (No real risk;
       just good practice.)

**Steps:**
1. Get into a session with your friend, both alive in the open world
2. Press F11 to arm v5f sampling
3. Stand still for 60 seconds in your friend's vicinity
4. F11 to disarm
5. Save DebugView log: `STATION-3A.log` on the Projects share
6. Tell me: "guest slot probe done"

**What I'll do:** read STATION-3A.log via SMB, check which slot has the
matching mapX/mapAngle to your character (which I'll cross-check against
your friend's slot). Update the v6 probe + production mod accordingly
if you're not at slot 0.

### 3.B — Live target-of-boss validation (~30 min, after L1 ships)

**Setup:**
- [ ] L1 release (`v0.2.0`) installed
- [ ] Friend in same session, both engaged with same boss

**Steps:**
1. Both engage a boss with bar (Margit, Crucible Knight in Caelid, etc.)
2. Take turns aggro-ing. You hit, friend backs off; friend hits, you
   back off.
3. Watch for the audio cue. It should fire ONLY on parryable attacks
   directed at YOU, not your friend.
4. After ~15 minutes, tell me what fired correctly vs incorrectly.

**What I'll do:** wire any false positives/negatives back into the L2
state machine before D3D12 work starts.

**Asks for friend (none — they don't install anything):**
- The mod is guest-side only. They don't need it. They don't need to
  know it exists. They will not see anything different on their screen.

---

## Session 4 — Hue overlay smoke test (~30 min, after L2 ships)

**Status:** Future. Documented here so you have the full pre-batched
ask list now.

**Pre-session asks:**
- [ ] Note your current resolution + monitor refresh rate
- [ ] Have at least one boss-fight save handy
- [ ] DebugView running
- [ ] **Backup save copy** before testing — D3D12 hooks are the
       highest-crash-risk thing we ship; not expecting issues, but a
       save backup is cheap insurance.

**Steps:** TBD when L2 is ready. Will follow same pattern as Sessions
2 + 3.

---

## Standing rules (apply to every session)

- **DebugView log naming:** `STATION-<session-id>.log` always. Drop on
  Projects share at `C:\Projects\elden-ring\STATION-<id>.log`. I read
  via SMB; no need to send.
- **Crash dumps:** if game crashes, the .dmp lands in
  `C:\Projects\elden-ring\eldenring.exe.<pid>.dmp`. Just leave it there;
  I'll see it. Don't delete.
- **CSVs:** probe writes them to the mods folder
  (`Game\mods\parry-tell-probe-*.csv`). I read those via SMB too. Don't
  copy them anywhere; the mods share is RW from my side.
- **"Done" signal:** just say "done with N" or "done with session N's
  scenario X." No need to summarize what happened — I'll read the data
  and tell you.
- **If something looks off mid-session:** stop, don't try to debug it
  yourself. Tell me what you saw, I'll triage.
- **Time of day for sessions:** doesn't matter. SMB mounts are persistent
  via fstab; I see new files within a few seconds of you saving them.
- **No need to keep Tailscale/SSH up** after a session. The build channel
  (SSH to station) only matters when I'm pushing fresh DLLs. The data
  channel (SMB) auto-mounts.

## What I'll proactively do between sessions

- After Session 1: parse TAE, build `parry_data.json`, message you
  "extraction parsed, safe to file-verify."
- After Session 2: analyze 4 CSVs + logs, update PHASE3-PLAN with which
  offsets worked, propose probe v7 if any group failed, OR proceed
  directly to Phase 3.2 (audio cue plumbing) if all worked.
- After Session 3.A: tell you the slot answer + update v6 if needed.
- After Session 3.B: state-machine fixes for any false +/-, before L2.
- After Session 4: D3D12 issue triage if any.

You'll get a Telegram or email message after each one (Chicago time
stamps, no surprises).
