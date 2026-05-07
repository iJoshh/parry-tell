# Part A - Get-Parried Window verdict

**Verdict:** "Get-Parried Window" (`ChrActionFlag`, event type `0`, `FlagType=5`) means: the current animation is in the window where this character's attack can be parried by an opponent. It is the parry-ee/vulnerable-attacker side, not the parry-er/deflecting side.
**Confidence:** HIGH.

## Evidence
1. Local `c4100` data places `FlagType=5` on Crucible Knight weapon attack animations, not on guard/parry-looking ranges. A direct scan found `FlagType=5` in `000003000` through `000003009`, `000003016`, `001003000` through `001003008`, and `002003000` through `002003008` under `/mnt/station-projects/elden-ring/chr-extracted/c4100-anibnd-dcx-wanibnd/INTERROOT_win64/chr/c4100/tae/c4100-tae/`. These are attack-combo ranges. In `anim-000003000.xml`, event type `1` `AttackBehavior` runs `0.6333333 -> 0.73333335` with `BehaviorJudgeID=110`, and `FlagType=5` runs `0.6333333 -> 0.7`, i.e. a sub-window overlapping the actual hit behavior.
2. Community move data independently says Crucible Knight weapon swings are parryable while shield bash, stomp, flying lunge, and tail attacks are not. Fextra's Crucible Knight page marks most phase 1 sword attacks as parryable and explicitly says shield bash/seismic wave are exceptions: https://eldenring.wiki.fextralife.com/Crucible%2BKnight. Wiki.gg/Fandom's Crucible Knight moveset likewise lists sword slash, sword lunge, dragging slash, and seismic sword slam as "Can Be Parried", while shield bash, foot stomp, flying lunge, and tail are "Cannot Be Parried": https://eldenring.wiki.gg/wiki/Crucible_Knight and https://eldenring.fandom.com/wiki/Crucible_Knight.
3. The parry-er side is represented elsewhere. Soulsmodding's TAE docs describe TAE as the event system for animation windows including "parry windows" and document type `1` `Attack Behavior`; its `Attack Type` enum includes `64: Parry`, and `BehaviorJudgeID` is the action judgment ID used to look up behavior params: https://soulsmodding.wikidot.com/format:tae and https://soulsmodding.com/doku.php?id=format:tae. WitchyBND's ER template also has distinct parry-side-ish action flags: `63` (`AI Parry Signal`), `73` (`Parry Possible State`), and `119` (`TryToInvokeForceParryMode`), plus type `1` `AttackType=64`. That division makes `FlagType=5` being the receiving/attacker-vulnerable window the coherent reading.
4. The player skeleton cross-check supports the same conclusion. You already observed `c0000` has zero `FlagType=5` events even though player parry is a core mechanic. That is strong evidence against "FlagType=5 means I am attempting a parry"; player parry attempts are not encoded there.

## Sentinel claim I can verify hands-on
"Crucible Knight `c4100` animation `anim-000003000.xml` should be a parryable sword attack. In local TAE data, its damage/hit event is `AttackBehavior` (`type=1`, `BehaviorJudgeID=110`) at `0.6333333 -> 0.73333335`, and its get-parried window is `ChrActionFlag FlagType=5` at `0.6333333 -> 0.7` (about frames 19-21 at 30fps). A successful player parry should only register during the early part of that hit window, not during windup or late recovery."

# Part B - Event-type roadmap

| Ship layer | Mod needs | TAE event type(s) | Param key(s) | Confidence | Sentinel |
|---|---|---|---|---|---|
| MVP | Parry window open/close for boss attacks | `0` (`ChrActionFlag`) | `FlagType=5`; use event `startTime`/`endTime`. Keep `ArgC` as unknown metadata if present. | HIGH | `c4100` `anim-000003000.xml`: `FlagType=5` spans `0.6333333 -> 0.7`, overlapping `AttackBehavior` `0.6333333 -> 0.73333335`. |
| L1 | Whether the boss is targeting the player vs summon/NPC | Not carried by TAE. Runtime only. | TAE `AttackBehavior` has `AttackType`, `AttackIndex`, `BehaviorJudgeID`, `DirectionType`, `Source`, `StateInfo`; none identify the current target actor. | HIGH | Start the same attack while the boss targets Mimic Tear: the same animation and same TAE events fire, but the target actor differs in runtime AI/Chr state. The mod must read target/aggro/attack target from process memory. |
| L2 hue | Attack incoming / active damage window | `1` (`AttackBehavior`) for melee hit behavior; `2` (`BulletBehavior`) for projectile/spawned hit behavior; optionally `5` (`CommonBehavior`) and `304` (`ThrowAttackBehavior`) for special cases. | `AttackType`, `AttackIndex`, `BehaviorJudgeID`, `DirectionType`, `Source`, `StateInfo`; for bullets: `DummyPolyID`, `AttackIndex`, `BehaviorJudgeID`, `AttachmentType`, `Enable`, `Source`. | HIGH for active hit windows; MEDIUM for "incoming" because windup is inferred from animation start or earlier SFX/turn events. | In `c4100` `anim-000003000.xml`, `AttackBehavior` starts exactly with `FlagType=5`. For an incoming hue, cue before `AttackBehavior.startTime`; for active/danger hue, cue on `AttackBehavior` windows. |
| L2 hue | Weapon trail / readable swing visual | `118` (`SpawnFFX_Blade`) when present; also `96`, `110`, `112`, `119`, `128/129` as optional correlation signals. | `FFXID`, `DummyPolySource`, `DummyPolyBladeBaseID`, `DummyPolyBladeTipID`, `SlotID`; for one-shot/general FFX use `FFXID`, `DummyPolyID`, `SlotID`. | MEDIUM | If a parryable weapon swing has `SpawnFFX_Blade`, it should broadly bracket visible blade trail, but do not treat it as authoritative damage or parry timing. |
| L2 hue | Hyperarmor / poise active | `0` (`ChrActionFlag`) | `FlagType=24` (`Super Armor`), event `startTime`/`endTime`; possibly also `FlagType=71`/`102` for poise-break state transitions, but not as normal hyperarmor windows. | MEDIUM | Pick a boss attack known to ignore light stagger; `FlagType=24` should cover the time the attack cannot be interrupted normally. If player hits before/after that interval, stagger behavior should differ. |
| v1 | Lock-on aware cue priority | Not carried by TAE. Runtime only. | Read player lock-on target / camera target / target handle from process memory. TAE only has animation-local flags such as `49` disable lock-on and `55` disable ability to lock-on. | HIGH | Lock on and unlock during the same boss animation: TAE event stream does not change; priority must change from runtime lock-on state. |
| v1 | INI configurability and Primary/Alert tiers | Not TAE data. | Config layer maps extracted event classes to colors/sounds. | HIGH | Same extracted `FlagType=5` and `AttackBehavior` rows should drive different colors after only INI changes. |
| v1 | Dodge/i-frame and player-side cue families | `0` for character action flags; `1` `AttackType=64` for parry attempts; `67` `AddSpEffect` can matter for buffs/status. | Useful flags include `8` dodging, `94` perfect invincibility, `132` lower-body jump i-frames, `143` PvE-only i-frames, `63` AI parry signal, `73` parry possible state, `119` force parry mode. | MEDIUM | On a known player roll/backstep/jump/parry animation, the player-side defensive window should be represented by these flags/type `1`, not by `FlagType=5`. |

## Other event types I scanned

| Event type | Useful for parry-tell? | Why |
|---|---|---|
| `0` (`ChrActionFlag`) | yes | Primary source for parryable window (`FlagType=5`) and likely hyperarmor (`FlagType=24`), plus defensive/cancel/lock-on flags. |
| `1` (`AttackBehavior`) | yes | Authoritative animation-timed melee attack behavior. `BehaviorJudgeID` bridges from TAE to BehaviorParam/AttackParam, but not to current target selection. |
| `2` (`BulletBehavior`) | yes | Needed for projectile/magic/breath-style boss attacks where the damage carrier is a bullet rather than direct melee hitbox. |
| `5` (`CommonBehavior`) | maybe | Behavior dispatch without the full type `1` shape; scan as a special-case attack source, but not the main path. |
| `16` (`Blend`) | no | Transition/blend control; not player-facing attack, parry, targeting, or hyperarmor data. |
| `64` (`CastHighlightedMagic`) | maybe | Useful for spell/bullet bosses if you later cue magic casts; not relevant to melee parry windows. |
| `67` (`AddSpEffect`) | maybe | Buff/status application can indicate charged states, armor changes, or special attack phases; requires SpEffectParam interpretation. |
| `96` (`SpawnOneShotFFX`) | maybe | Good perceptual/windup correlation, but cosmetic; not authoritative for hit/parry timing. |
| `110` (`SpawnFFX_General`) | maybe | Same as other FFX: useful as supplemental visual intent, not hit logic. |
| `112` (`SpawnFFX_FloorDetermined`) | maybe | Can mark ground effects/AOE tells; validate per boss. |
| `118` (`SpawnFFX_Blade`) | maybe | Best FFX candidate for visible weapon swing/trail, but still cosmetic relative to `AttackBehavior` and `FlagType=5`. |
| `128`/`129` (`Wwise_PlaySound_*`) | maybe | Useful for boss-authored audio tells or debug correlation; not authoritative timing for parry or damage. |
| `144`/`145` (`RumbleCam_*`) | maybe | Good signal for heavy impact/AOE emphasis, not parryability. |
| `224` (`SetTurnSpeed`) | maybe | Can help identify windup/commitment and tracking changes. In `c4100` `anim-000003000.xml`, turn-speed changes run through windup and recovery, but they do not identify parry windows. |
| `304` (`ThrowAttackBehavior`) | maybe | Needed for grab/throw attacks; normally not parryable, but important to suppress misleading parry cues. |
| `704`/`706` (`ChrTurnSpeedEX` / `ChrTurnSpeed_ForLock`) | maybe | Lock/tracking tuning inside animation. Useful for threat quality, not for knowing whether the player is locked on. |

## Anything else I should know

The clean extraction model is: use `FlagType=5` as the parry-window source, use `AttackBehavior`/`BulletBehavior` as the damage-window source, and use runtime memory for target/lock-on priority. Do not try to infer "boss is targeting player" from TAE; the same TAE timeline can be used against the player, Mimic Tear, co-op phantom, or NPC ally.

For the roadmap, extract at least these `ChrActionFlag` values into the database even if MVP only consumes `5`: `5`, `24`, `49`, `55`, `63`, `71`, `73`, `78`, `79`, `86`, `94`, `102`, `119`, `132`, `143`. The cancel flags (`78/79/86`) are not cue-worthy by themselves, but they help segment recovery/combo-queue phases when you later tune alert timing.

Watch the distinction between "parryable attack" and "parryable right now." `AttackBehavior` can identify the active damaging behavior and link to params; `FlagType=5` is the narrow vulnerability interval that matters for the beep. For attacks with multiple hits, multiple `AttackBehavior` and `FlagType=5` intervals may exist in one animation.
