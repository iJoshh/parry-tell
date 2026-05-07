# TASK: Map ALL Elden Ring TAE event types relevant to the parry-tell.dll mod across its full release roadmap

## Context

I'm building a client-side Elden Ring + Shadow of the Erdtree mod called `parry-tell.dll` that gives the player visual + audio cues during boss fights. The full release roadmap spans four ship layers:

- **MVP (week 1):** audio-only beep when a parryable boss attack is about to enter its parry window
- **L1 (week 2):** target filter so the cue only fires when the boss is targeting THE PLAYER (not their summon / Mimic Tear / NPC ally)
- **L2 (week 3):** screen-edge hue overlay (different colors for attack-incoming vs parry-window-open)
- **v1 (week 4):** lock-on aware (cue priority adjusts based on whether you're locked onto the boss), INI configurability, distinct Primary/Alert color tiers

I have all 807 ER character anibnd files extracted via UXM Selective Unpack 2.4.2 + WitchyBND v3.0.0.1, totalling 64,385 `anim-*.xml` files. They live at `/mnt/station-projects/elden-ring/chr-extracted/`.

Anim XML format:
```xml
<anim>
  <name>a000_003000.hkt</name>
  <events>
    <event>
      <type>0</type>
      <startTime>0.6333</startTime>
      <endTime>0.7000</endTime>
      <params>
        <param name="FlagType" value="5" />
      </params>
    </event>
    ...
  </events>
</anim>
```

The canonical event-type catalog WitchyBND used to parse the binary TAE format is at `/mnt/station-projects/tools/WitchyBND/Assets/Templates/TAE.Template.ER.xml`. It defines event id 0 ("ChrActionFlag") with a FlagType enum, where for example FlagType=5 is named "Get-Parried Window" — but Souls modder naming is loose and ambiguous, and not every named entry actually exists in shipped data.

## Your job

Two things, in order:

### Part A: Resolve the "Get-Parried Window" semantic ambiguity

The obvious-looking candidate for "this attack is parryable" is event `<type>0</type>` (ChrActionFlag) with `FlagType=5` ("Get-Parried Window"). But the name is ambiguous:

- Does "Get-Parried Window" mean **"the window during which this character can BE parried by an opponent"**? (= boss is parryable, this is what the mod wants)
- Or does it mean **"the window during which this character is ATTEMPTING a parry"**? (= inverted — character is the parry-er, not the parry-ee)

Investigate independently and tell me which interpretation is correct. Do NOT just re-read TAE.Template.ER.xml's `name=` attribute — that's the source of the ambiguity. Find external grounding.

### Part B: Map the full event-type surface for the mod's roadmap

For each ship layer (MVP / L1 / L2 / v1), tell me which TAE event types and which event-param values actually carry the data the mod needs to read. I'd rather have ONE comprehensive investigation now than redispatch you three times over the next month. Specifically:

| Ship layer | What the mod needs to know | Likely TAE event types |
|---|---|---|
| MVP | When does the parry window open and close on a boss attack? | event type 0 + FlagType=? (this is Part A) |
| L1 | Who is this attack targeting? Is it the player or someone else? | AttackBehavior (event type 1?) — does the TAE data carry a target hint, or is targeting computed at runtime only? |
| L2 (hue) | When does the *attack itself* start (so we can cue "an attack is incoming" before the parry window opens)? | Some event type marks attack-active windows / damage windows. SpawnFFX_Blade (118)? AttackBehavior (1)? Damage hitbox tags? |
| L2 (hue) | When does the boss's hyperarmor / poise activate (so the cue can dim if a counter would be wasted)? | "Super Armor" FlagType=24 in ChrActionFlag — but is that the right one? |
| v1 | Are there other player-relevant cues — hyperarmor, dodge windows, summons, weapon-arts, charged attacks? | varies |

For each one, give me:

1. The **event type ID(s)** and **param key(s)** that carry that data
2. **Confidence level** (HIGH / MEDIUM / LOW)
3. **One sentinel claim** I can verify hands-on against a known-frame-data attack

If a layer's data is NOT carried in TAE event tracks (e.g. targeting is purely runtime), say so explicitly — that's just as useful, because it tells us the mod has to read it from process memory at runtime, not the TAE database.

## Likely productive evidence paths (pick what's productive — don't be exhaustive)

### 1. Cross-check with community move-frame data

Pick one or two well-documented bosses. **Crucible Knight (c4100)** is iconic and has 31 anims carrying FlagType=5. **Margit / Morgott (c4710 / c4711)** is also extensively wiki-documented with explicit per-attack parry flags.

For each:
- Which anim IDs carry FlagType=5? (Examples I've extracted: c4100 has a000_003000 through a000_003016, plus a001_003000 onward.)
- What does FextraLife / soulsmodding.wiki / fandom wiki / Reddit boss-parry-guides say about which attacks are parryable?
- Does the boss have visible **parry-attempt** animations? Crucible Knight has a shield-bash that may or may not be a parry mechanically. If the parry-attempt anim ALSO carries FlagType=5, that strongly supports the parry-er reading. If only attack anims carry it, that supports the parry-ee reading.

### 2. Find Souls modder docs that document FlagType=5 explicitly

WitchyBND inherited this template lineage from earlier titles. Search for:
- "ChrActionFlag FlagType 5"
- "Get-Parried Window TAE event"
- "soulsmodding.wiki ChrActionFlag"
- "TAE event FromSoftware parry"
- "DS3 TAE FlagType 5" / "Bloodborne TAE Get-Parried"

The ChrActionFlag system is shared across DS2-Bloodborne-Sekiro-DS3-ER, so even DS3 modder writeups should resolve the ambiguity.

### 3. Player anim cross-check (c0000)

I've confirmed `c0000-anibnd-dcx-wanibnd/` (player skeleton) anims have **zero** events with FlagType=5. Two interpretations:

- If FlagType=5 means "I can be parried": player has no FlagType=5 because players are normally parry-ers, not parry-ees. Consistent but soft.
- If FlagType=5 means "I am parrying": player has no FlagType=5 — but players CAN parry (it's a core mechanic). **Strong evidence AGAINST** the parry-er interpretation.

What about FlagType 63, 73, 119 in c0000? If the player anims ARE the place those parry-er flags live, that's elimination evidence FlagType=5 is parry-ee.

### 4. Humanoid NPC enemy cross-check

Check NPC enemies that visibly do BOTH attack and parry. Available locally at `/mnt/station-projects/elden-ring/chr-extracted/`:
- `c5040-anibnd-dcx-wanibnd/tae/c5040-tae/` (Banished Knight or similar — flat layout)
- `c4090-anibnd-dcx-wanibnd/INTERROOT_win64/chr/c4090/tae/` etc

Note: WitchyBND output layout VARIES by character (some flat `tae/cNNNN-tae/`, some nested `INTERROOT_win64/chr/cNNNN/tae/cNNNN-tae/`). Recursive `**/anim-*.xml` glob finds all of them.

For one of these, look at anim ID naming. ER's animation ID convention typically:
- `a000_*` through `a3xx_*` = various attack / locomotion classes
- `a350_*` / `a360_*` / `a370_*` ranges typically = guard / parry / dodge

If FlagType=5 events cluster in **attack** anim ID ranges, that supports "I can be parried."
If they cluster in **parry/guard** ranges, that supports "I am parrying."

### 5. AttackBehavior and behavior_id correlation

In ER, parryable attacks are typically tagged at the AttackBehavior layer (likely event type 1, "AttackBehavior") with a specific behavior_id that references `regulation.bin`'s AttackParam table. Some attacks are flagged "parryable" in their AttackParam.

If a given anim's FlagType=5 event window (start_time → end_time) closely overlaps with that anim's AttackBehavior window, AND the AttackBehavior's behavior_id is one tagged "parryable" in community AttackParam dumps, that's strong **mechanistic** evidence that FlagType=5 marks "I can be parried during this sub-window of my attack."

Community AttackParam dumps live in places like https://github.com/JKAnderson/Yabber data or Smithbox / DSMapStudio resources, ParamRows.csv, etc.

### 6. Other ChrActionFlag values

The ER template defines:
- FlagType=5 ("Get-Parried Window")
- FlagType=24 ("Super Armor") — relevant for L2 hue (hyperarmor windows)
- FlagType=63 ("AI Parry Signal")
- FlagType=73 ("Parry Possible State")
- FlagType=119 ("TryToInvokeForceParryMode")

Plus dozens of other FlagTypes per the template. For Part B, scan which ChrActionFlag values are common in BOSS anims that the mod will care about, and tell me which ones I should be extracting beyond just FlagType=5.

The fact that distinct named flags exist for the parry-er side (63 / 73 / 119) **suggests by elimination** that FlagType=5 is the parry-ee side. Investigate whether 63 / 73 / 119 appear in c0000 (player) anims.

### 7. Other event types that MAY matter for the roadmap

Beyond ChrActionFlag (event type 0), the template defines events like:

- 1: AttackBehavior — likely carries the parryable-or-not + damage-window data
- 16: Blend (animation transitions — probably not relevant)
- 96: SpawnOneShotFFX (visual effect — usually correlates with attack windups)
- 110: SpawnFFX_General
- 112: SpawnFFX_FloorDetermined
- 118: SpawnFFX_Blade — interesting, may mark weapon swing windows
- 128: Wwise_PlaySound_CenterBody
- 129: Wwise_PlaySound_BySlot
- 144: RumbleCam_Local
- 224: TurnSpeed / movement constraints — possibly useful for "boss is winding up an attack" detection

For each, give me a one-line "useful for parry-tell? yes/no/maybe — why."

## Deliverable

Write your full report to `/tmp/codex-tae-investigation-result.md`. Structure:

```markdown
# Part A — Get-Parried Window verdict

**Verdict:** "Get-Parried Window" (ChrActionFlag FlagType=5) means: <interpretation>.
**Confidence:** HIGH | MEDIUM | LOW.

## Evidence
1. <evidence item with citation/URL/file path>
2. ...

## Sentinel claim I can verify hands-on
<one specific, falsifiable check, e.g.>
"Crucible Knight kick anim a420_003020 should be parryable per the FextraLife
boss page at <URL>. In the local TAE data, that anim has FlagType=5 spanning
frames 14-16 at 30fps, which matches the wiki's documented parry window of
roughly 3-5 frames into the kick."

# Part B — Event-type roadmap

| Ship layer | Mod needs | TAE event type(s) | Param key(s) | Confidence | Sentinel |
|---|---|---|---|---|---|
| MVP | Parry window | 0 (ChrActionFlag) | FlagType=5 | HIGH | <sentinel> |
| L1 | Targeting | <answer or "runtime only"> | ... | ... | ... |
| L2 hue | Attack window | <answer> | ... | ... | ... |
| L2 hue | Hyperarmor | 0 (ChrActionFlag) FlagType=24? | ... | ... | ... |
| v1 | <other> | <answer> | ... | ... | ... |

## Other event types I scanned

| Event type | Useful for parry-tell? | Why |
|---|---|---|
| 1 (AttackBehavior) | yes/no/maybe | <one line> |
| 96 (SpawnOneShotFFX) | yes/no/maybe | <one line> |
| 118 (SpawnFFX_Blade) | yes/no/maybe | <one line> |
| 224 (TurnSpeed) | yes/no/maybe | <one line> |
| ... | ... | ... |

## Anything else I should know
<free text — surprises, watch-outs, schema issues, evidence-of-absence findings, etc.>
```

## Constraints

- **Do NOT write a parser yet.** This is a semantics + schema investigation only.
- **Do NOT trust a single source.** Cross-check at least two independent paths from the list above.
- **Do NOT take TAE.Template.ER.xml's `name=` attribute as authoritative.** That is the ambiguity I'm asking you to resolve.
- Web access: yes, fetch wiki pages, GitHub modder repos, Reddit threads, Smithbox/DSMapStudio docs, anything with independent grounding.
- Local data access: yes — `/mnt/station-projects/elden-ring/chr-extracted/` (807 character dirs), and `/mnt/station-projects/tools/WitchyBND/Assets/Templates/` (TAE templates for ER and earlier titles, useful for cross-game lineage).
- **No need to be exhaustive.** Two productive evidence paths with strong findings beats five shallow ones. Length limit: ~3 pages. Tables are good.
- Time budget: spend up to ~25 minutes. If you're past that, stop and write up what you have.

## Output

Write your final report to `/tmp/codex-tae-investigation-result.md`. The workspace is now writable for `/tmp` so you can also scratch intermediate notes there if helpful. Anything else important goes in your final stdout message.
