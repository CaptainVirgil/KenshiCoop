# Handoff — 2026-08-17 (the night ended by a relay flip)

> **v1.0.0-beta.1 IS THE RELEASE TO TAKE — installed on this machine
> already. Same code as v0.73; the release IS the SemVer-beta adoption.**
> The brother's updater copy is from an old kit and CANNOT SEE pre-releases:
> he re-downloads `update-kenshicoop.ps1` from the release page once (or runs
> his old copy with `-Tag v1.0.0-beta.1`). From then on, betas auto-update.
>
> Previously current: Protocol 59 (v0.67+ interoperate); each release
> since v0.67 fixed a defect the previous one shipped, so both players run the
> newest.
>
> **The second session on 2026-08-16 never actually reconnected, and nobody
> could tell.** Post-mortem from BOTH logs (his clock runs 2 h behind):
> mid-play the Steam P2P session dropped and re-established with the ends
> DISAGREEING on the route — host `relay=1`, join `relay=0`. Every ENet
> connect then completed while the one-shot HELLO never arrived (host:
> `peer connecting (awaiting HELLO)` → dead ~100 ms later, every 2 s, six
> minutes). The players discovered it when a beakthing fight happened on one
> screen only. Meanwhile each half-open window ran the join's authority sweep
> as a FALSE HOST — a client's `localId()` read 0 (the host's id) until
> WELCOME — hiding NPCs and un-hiding them on teardown: the visible
> "NPCs disappearing and reappearing" (churn 2710/1853).
>
> v0.73 is that incident, fixed four ways (no wire change):
> 1. `sessionUp()` — a half-open connection is not a session. localId() is
>    OWNER_ID_ALL until WELCOME; arbiter, authority/census arms, cell claims
>    and cam hints all gate on a COMPLETED handshake.
> 2. HELLO re-sends every 500 ms inside a held connection (was one-shot).
> 3. The join slot is id 1, reused — the old `nextId++` would have rejected
>    the first same-run reconnect as a "third player".
> 4. Five dead handshakes → loud log + F2 panel names the failure + redial
>    slows to 10 s. WELCOME clears it.
>
> **Untested live:** the whole v0.73 batch, plus v0.72's pause fix and the
> [wi] rates. The v0.72 live gate below still applies, plus: on any future
> transport outage expect `handshake failing: 5 connections in a row` and the
> panel message, NOT silent NPC blinking.

# Previous handoff — 2026-08-16 late night (the live-iteration ladder)

> **v0.72 WAS the release to take. Skip v0.67–v0.71 — all six are protocol 59
> and interoperate, but each fixed a defect the previous one shipped, and only
> v0.72 has all of them.** The local install is PENDING: Kenshi was running at
> handoff time — run `scripts/linux/update-kenshicoop.sh` once it closes. The
> brother runs his updater as usual.
>
> The ladder, one live session per rung, each defect caught by the monitor or
> the players within minutes of going live:
>
> | | shipped | fixed |
> |---|---|---|
> | v0.67 | all 7 authority steps | — |
> | v0.68 | `authArbiter_` flag | v0.67's DUAL ARBITER: both clients emitted assign maps because the gate was `streamNpcs_`, true on both under cellAuth |
> | v0.69 | paired park exemptions | v0.68's TELEPORT-RUN LOOP (freeze exempted injured bodies, park did not — the two act on the same divergence signal, so the un-paired half fired alone) + the corpse fountain (parks on down/dead bodies) |
> | v0.70 | arbiter = negotiated role | v0.68/69 keyed the arbiter on the CONFIGURED role. Virgil configures HOST and panel-switches to join ⇒ false arbiter on his machine, map decays (revokes 1000+, join→2 by gen=991, watched live). Now `g_net.localId() == 0` per tick, and a demoted arbiter wipes assignMap_/assignSent_/liveness state |
> | v0.71 | xfer truncation guard | the transfer detector diffed a 64-entry capture and ignored the truncated flag — boundary items "vanish" between scans, and a phantom loss can pair with a real gain and MOVE an item nobody dragged. Best mechanism yet for #48. Truncated ⇒ skip that bag this tick, `xferTrunc=` counts |
> | v0.72 | speed → unreliable channel + 3-tick change burst; `[wi]` rollup carries real rates | "we can't pause the game … well it did let us eventually": pause votes queued 27 s behind the assign/spawn backlog on CH_RELIABLE (five REQ paused=1 22:33:56–:04, SET back 22:34:23); the impossible 0↔1 SET flapping was that queue draining. Speed is idempotent state — seq guards + safety resend + enforcement already existed, so unreliable is strictly better. Also: the [wi] rollup read 0/0 in steady state by construction; now reports tracks/drops/sent/culls/defer |
>
> **The design is validated.** v0.69 ran ~10 minutes with a true arbiter before
> the decay set in: join-side delay 50 ms (from 741), snapSq 0 (from 11,483),
> parks 5 (from 456), map split 48/54. That window is the number the redesign
> was built to produce.
>
> **The live gate for v0.71, in order:**
> 1. `[auth] map` join= holding ~half the population for a full hour (decay
>    to single digits = the v0.70 fix did not land).
> 2. `LIVENESS revoke` total near ZERO (1050 in tonight's broken session).
> 3. Join `[interp]` delay sustained 50–200 ms, snapSq ~0.
> 4. Host's own numbers NOT degraded (flicker must shrink, not migrate).
> 5. `xferTrunc=` on [audit] — nonzero while looting big containers is the
>    guard working, not a bug.
>
> **Item-loss triage now starts with a question: "did you sell anything?"**
> Tonight's four FOLD-LOSS sightings were a shop sale — a vendor counter is a
> container the tracker does not watch, so a sale reads as loss by
> construction. Third time this week a one-line player answer beat a theory.
>
> Open and unexplained: `[wi] scan` rollup shows `baselined=0 streamed=0` all
> session — the ground-item channel is enumerating nothing. Needs both logs
> from a session where loot demonstrably hit the ground.

# Previous handoff — 2026-08-16 (authority overhaul day)

> **v0.67 IS RELEASED — PROTOCOL 59, A HARD CUT. Both players must update
> before they can connect at all.** Installed and verified on this machine;
> the brother runs his existing updater.
>
> It ships the FULL authority redesign (docs/AUTHORITY-DESIGN.md, all seven
> steps): host-ASSERTED per-body ownership replacing the derived spatial
> verdict wherever an assertion exists. authAssert defaults ON; rollback is
> `"authAssert": "off"` (v0.66 byte-for-byte) or `"shadow"` (counts what ON
> would change) in coop_config.json — config, never a re-release.
>
> **The live gate is still open.** Next session, read in order:
> 1. `[auth] map n= host= join= veto=` — the split existing at all is step 1.
>    join= should be roughly half the shared-identity population.
> 2. Join-side `snapSq` and `delay=` on `[interp]` — the flicker's numbers.
>    delay should fall from ~740-887 toward the 50-200 band.
> 3. **The HOST's same numbers must not degrade** — the risk register's
>    sharpest entry is the flicker migrating, not shrinking.
> 4. `[auth] LIVENESS revoke` rate and `refuse=` — revoke churn or witness
>    churn means the policy needs tuning, not that the mechanism is wrong.
> 5. `[auth] map` join= over an hour — decay toward zero would mean the
>    proxy-reassignment leg (step 7) is not landing.
>
> The authoritytest harness (46 pins, in the gate) found four real bugs while
> being built — including two shipped v0.65 defects and the design's own
> predicted census-dual-semantic trap. Extend it before touching Items or
> Channels; the pattern is proven twice now.

# Previous handoff — 2026-08-16 (night session, two players live)

> **v0.66 IS RELEASED AND INSTALLED HERE.** Protocol 58 throughout, so v0.61-v0.66
> interoperate. The brother still needs to run his updater.
>
> **The 0-blood bug is SOLVED, not just diagnosed.** It was ours and it was on
> BOTH clients: the AI suspend skips Kenshi's whole per-character update, which
> is where blood loss becomes unconsciousness. Asking "is he down on YOUR
> screen?" is what cracked it - the answer (up on both) ruled out replication
> entirely. Ask that class of question FIRST.
>
> Two things to read on the next session, both of which decide whether these
> landed: `frzHurt=` on `[ai]` (bleeding bodies now keep their brain) and
> `snapVeto=` (the combat veto had NEVER fired - 727 snaps, 0 vetoes - because
> it reused a 20 u convergence constant as its range; now 120 u with its own).

> **FIRST ACTION NEXT SESSION — one question, ten seconds.**
> Enemies at 0 blood stay standing on the JOIN's screen. Ask the host:
> **is that same enemy down on YOUR screen?**
> - down on his, up on the join -> ours, and task #50 names the cause and the fix.
> - up on both -> not sync at all. Kenshi does not knock a body over for blood
>   alone; it bleeds out over time. Two of my three theories tonight died to
>   evidence I could have gathered first. Ask before building.

## Where the code is

`v0.65` is the last RELEASE. `main` is **four commits past it and unreleased**,
held at the player's request:

| | |
|---|---|
| `e2b0384` | duplication fix (proxy-blind post-mint liveness), slew per-frame ramp |
| `d7a335a` | correctness warnings promoted to errors; a no-op "fix" reverted |

Gate green: prototest 745, tunneltest 17, netlinktest 48, drivetest 24,
contract 38. Both players are on **v0.64** - they never updated to v0.65, so the
near-band change gate has NEVER RUN LIVE. Its first reading should be a large
`nearSup=` on `[census] sent` and a fall in the `ent=` share of `[net] mix out`.

## Tonight's logs

Archived to `dist/logs-2026-08-16/` (join 14 MB, host 9 MB, plus a 2-minute
probe digest). The `.prev` rotation bug that ate the previous session's log is
fixed in v0.65, but these are kept because the fix is not in the players' build.

## The one shape behind almost everything

`engine::resolveCharByHand` **cannot address a minted proxy**. Three separate
player-visible bugs were all this, in three different files:

- v0.64 - NPC and enemy health never applied (apply paths).
- v0.65 - bodies stuck mid-carry or in furniture (`sweepCarries`).
- unreleased - **"people/enemies are duplicating over and over"**: the post-mint
  liveness guard verified a fresh proxy with `resolveCharByHand`, so a false
  negative DESTROYED the body, never bound it, and let the peer re-request -
  51 REQs for one hand on the host log. Liveness is now `readHand`, which is
  what `localCharForStreamed` and the drive's viaProxy path already use.

**Before writing another fix, grep for `resolveCharByHand` used as a predicate.**
Every remaining one is a candidate.

## Two things I got wrong, recorded so they are not repeated

1. **A fix that fixed nothing.** I rerouted the drive's DOWN test through
   `interp.latest()`. `sample()` already copies every non-positional field from
   the newest received snapshot, so it was the same value by a longer path. The
   drivetest case I wrote passed with the fix reverted - that is what exposed
   it. Reverted; reasoning left in the source.
2. **A vacuous test.** The first version of that pin could not fail. Deleting it
   was better than keeping a green check. The second version asserts the harness
   forced a large render delay BEFORE testing anything against it.

## Still open, and what each needs

| # | Item | Needs |
|---|---|---|
| 50 | 0-blood enemies stay up - KO edge detected by diffing the PUBLISHED buffer, so a mid-band body cannot be noticed for up to ~1.5 s | the question at the top, then a publish-side fix |
| 43 | Publish harness | the big unlock; `ReplicatorPublish.cpp` compiles standalone (verified), so the remaining work is engine fakes + a NetLink sink |
| 48 | Items despawn on a too-far transfer | one reproduction on v0.65+; `[xfer] FOLD-LOSS` and `lostItems=` will now record it |
| 46 | Render delay runs 4-5x its band (740-887 ms vs 50-200) | root is architectural: both players in ONE cell means the host authors everything and the join drives 112 bodies |
| 34 | Buy-probe | one building purchase on a Harness build |
| 41 | The publish wedge | only closable if it recurs; all 42 stages now name themselves |

## Live numbers worth keeping

- `ent` is ~87% of outbound; 472 KB per 5 s window, 94 KB/s, ~3x the 36 KB/s
  CLAUDE.md records as steady state.
- `noBody=471` - state arriving for bodies this client has no copy of at all.
  Different from the v0.64 bug and only visible because v0.64 started counting.
- `frzAct=88487` census freeze actuations.
- `DROPPED ent=74097`, flat all session - one load-time burst, not ongoing.
  (v0.65's per-window field will say which without ambiguity.)

---

# Handoff — 2026-08-11 (overnight, unattended)

> **v0.63 supersedes v0.62. Take it.** v0.62 shipped a regression in its own
> item fix (details below); v0.63 corrects it, and fixes a freeze watchdog that
> stopped working at midnight. Protocol is 58 throughout, so v0.61/62/63 all
> interoperate and a partial rollout is safe.


**Disposable.** Session state, half-built things, and traps that would cost the
next session an hour. Durable things live in `CLAUDE.md` (doctrine, build) and
`docs/ROADMAP.md` (plan, and the audit's hold list).

---

## Read this first

**v0.63 is released, installed on this machine, and unplayed.** Protocol 58 —
unchanged since v0.61 — so it is **not a hard cut**: any two of v0.61/62/63 will
connect. Both players should still update, because several fixes only do
anything when both ends have them, and because v0.62 carries a known regression.

The brother needs to run his updater; he does not need a new updater script.

Everything below came from the 2026-08-10 evening session logs and an audit run
after the players went offline. **None of it has been seen in a running game.**

---

## Repo state

**Branch is `main`** (not `linux-build` - it changed earlier in the session, and
one push went to the wrong branch before it was caught; the v0.62 tag initially
pointed at v0.61's commit and was retargeted). Pushed. Gate green after each:

| | |
|---|---|
| `b2e8768` | v0.62 origin-guard regression fixed; apply-tick sub-phased; `[net] mix`; monotonic watchdog clock |
| `8cacf6c` | `[q]` inbound queue depth + drop counter |

Earlier, released as v0.62:

| | |
|---|---|
| `bd2a0ea` | carry-via-pointer, publish sub-phasing, two loop bounds, origin-drop guard |
| `3449fc0` | scenario telemetry out of Release builds |
| `57db44d` | per-stage cost table, log line-rate counter, `addAiSuspend` guard, `resetSession`, config knobs |
| `a4e3780` | `frzAct=` and `truncHold=` counters |

Releases: <https://github.com/CaptainVirgil/KenshiCoop/releases/tag/v0.63>
(current) and v0.62 (superseded). Five assets each. Local install is v0.63,
verified by the updater.

---

## Two defects found by auditing v0.62 itself

**1. The origin guard threw away the good case.** v0.62 refused the whole drop
beat when the owner's position read (0,0,0) - but the per-item read below
already falls back to the item's own transform and is already guarded against
the same sentinel. Worse, the refusal was permanent (it skipped the baseline
hold, so the count committed and the intent was lost) and its give-up log was
dump-gated, i.e. invisible in player builds - deleting the very evidence that
made the bug findable. Fixed in v0.63: the test moved to the point of use, the
remainder is held, and the give-up logs unconditionally.

**2. The freeze watchdog was blind across midnight.** `mainThreadStalledMs`
differenced two samples of `wallClockMs()`, which is `GetLocalTime` reduced
modulo 24 h. Every backward step - midnight, DST, any NTP correction - made
`(now > beat) ? (now - beat) : 0` return **0, "no stall"**. This game is played
late; the 21.9 s freeze that motivated the whole instrumentation push happened
at **23:14**. A forward step fabricates a 3,600,000 ms stall instead.

Now a monotonic `GetTickCount64` truncated to 32 bits, read with a plain
unsigned difference and **no ordering test** - which is what makes it exact
across the 49.7-day wrap. The two halves cannot be landed separately: unsigned
difference on the old clock yields 4,208,569,296 ms at midnight, and an ordering
guard on the new clock re-blinds it at the wrap.

**Process note worth keeping.** Sub-agents in that audit edited the shared
working tree, and one of their changes (the clock fix) was swept into a commit
whose message did not mention it. It was caught, reviewed and the message
amended - but the lesson is that a read-only audit is not read-only unless the
agent is told so, or given a worktree.

---

## The freeze — what is now known, and what is not

The 23:14 freeze was **not** like the earlier three. The watchdog caught it in
`phase='publish'` — our code, not Kenshi's tick — and the main thread **never
recovered**: `beats=376716` identical across both watchdog reports and still
frozen 25 s later, while the net thread kept logging and inbound held at
47 KB/s undrained.

**Localised, not solved.** The stall onset back-dates to 23:14:05.846, which is
exactly the timestamp of the last main-thread line, `[wd] DROP id=21`. So the
wedge is at or after `detectAndPublishWeaponDrops`.

Two audit hypotheses were **killed by the log**, which is worth as much as the
fix that did land:

- *Event storm → unbounded `evt_` drained by `applyEvents`.* Ruled out:
  `applyEvents` runs **before** the weapon-drop publish, and `[wd] DROP id=21`
  was emitted, so that frame's `applyEvents` completed.
- *Log-flush storm (44k lines ≈ 21.9 s under Wine).* Ruled out: the line rate
  was flat at ~140/s for the 90 s before the wedge. No spike.

What shipped is instrumentation to name it next time, plus caps on the two
unbounded main-thread loops found on the way (neither proven to be the cause).

**If it recurs, the log now names the stage.** 42 stages each stamp their own
beat (28 publish + 14 apply), and `[q]` says how deep the queues were when it
happened.

---

## What to watch, in priority order

1. **`[stage] lines=N/10s peak us: ...`** — every 10 s, five most expensive
   stages by PEAK. v0.63 covers the apply half too (42 stages total), which is
   where the heavy work lives: `app:targets` (~1700 lines, no cadence gate),
   `app:authority` (~800), and `app:pubCensus`, whose once-a-second O(n^2) sort
   has never been visible. Expect those three plus `pub:worldItems` near the
   top; anything else there is news.
1b. **`[net] mix out=... in=...`** — per-packet-type traffic. 86% of the last
   session's bytes were unattributed. Also the direct test of the held
   event-storm theory: watch `evt=` on the AUTHORING client.
1c. **`[q] peak ...`** — inbound queue high-water marks. A `DROPPED` field
   appears only when something was discarded; its presence is the alarm.
   Together with `[stage]` this answers the question last night could not:
   was a stage slow, or was it handed forty thousand items?
2. **`snapVeto=` vs `snapCbt=` on `[ai]`.** This decides an open question. The
   indoor-combat veto only fires within 20 u, but the sighting that motivated it
   was gap=38–75. **If `snapVeto` stays near zero while `snapCbt` climbs, the
   ceiling is wrong** — and the fix is to raise it, since `readCombat` is doing
   the real discrimination. Do not raise it before seeing the numbers.
3. **`[wd] DROP ... pos=`** — should no longer be `0.00,0.00,0.00`. Two of 21
   were, last session. If the item desync persists *without* origin drops, it is
   a different bug.
4. **`frzAct=`** — the census freeze's real actuation count. 2,140 log lines
   last session stood for an unknown, much larger number. If this is enormous,
   the freeze/park loop is the next thing to attack.
5. **`[wd] WARNING implausible drop delta=`** and **`[speed] WARNING intent
   drain hit its cap`** — either one firing means a bound I added was actually
   needed, which would also name the freeze.
6. **`truncHold=`** — culls suppressed because the peer's census was capped.

---

## The escape hatch, if a session goes wrong mid-play

`coop_config.json` in the mod folder now reaches two knobs that were
environment-variable-only (and therefore unreachable under Steam):

```json
"censusPark": 0,
"censusFreezeAi": "0"
```

`censusPark: 0` disables the divergence teleport **and** the census freeze at
all four call sites. That is the single most likely thing to want to turn off if
NPC behaviour degrades. It costs more visible divergence between the two
worlds — that is the trade.

---

## Traps and loose ends

- **`make_kit.sh` builds Release twice** (a duplicated block, lines ~33–50).
  Harmless — the second is an incremental no-op — but it prints the banner
  twice, which looks like a bug. Deliberately not touched on release night.
  Delete one block.
- **`dormPc=1 pcs=3`** in the last `[audit]` line. The glossary says `dormPc`
  **must be 0**, and `pcs=3` makes it a valid measurement, so this is a real
  violation nobody has chased: a dormant body inside the attention radius of a
  player character.
- **The freeze→park feedback loop is undiagnosed.** `[proxy] drift` shows the
  local copy pinned at a fixed position while the host's walks away, drift
  growing until a park teleports it. Freezing a body means it can never follow,
  so divergence grows by construction. 3,545 freezes and 1,175 parks in 25 min.
  Whether that is the design working or eating itself is an open question — do
  not "fix" it without deciding which.
- **Mods still differ.** Same 60 mods, different order: `2d49eaad` here vs
  `4f1e54f1` there. The panel says `MODS DIFFER (both 60 - check ORDER)`. He
  should send his `mods.cfg`.
- **Task #34 (buy-probe) is still open** and needs one deliberate in-game
  purchase on a Harness build. `scripts/linux/buyprobe.sh on|read|off`.

---

## The audit's hold list

About a dozen further candidate fixes were found and **deliberately not
shipped**. They are in `docs/ROADMAP.md` under "Held for daylight", each with
the evidence it needs first.

The rule that decided it, worth keeping: `drivetest` links `ReplicatorDrive`,
`ReplicatorCore` and `Interp`, so a fix there is gate-testable.
`ReplicatorAuthority`, `ReplicatorPublish`, `ReplicatorItems` and
`ReplicatorChannels` are **linked by no test at all** — and all four regressions
the players caught this week were in that second group. Extending the harness to
one of those files is worth more than any single fix on the held list.

---

## One thing that nearly shipped broken

Sub-phasing the watchdog meant prefixing `coop::mainThreadBeat("pub:x");` onto
each stage. Twelve stages were **braceless guarded statements**, so the prefix
made the *beat* the guarded statement and the publish unconditional — every
config gate in the tick silently defeated. The compiler caught exactly one of
the twelve (the only one with an `else`); the full C++ gate passed on the other
eleven, because nothing in it exercises a disabled feature.

Caught by scanning for the pattern rather than by any test. There is now a
contract check for it, verified by re-injecting the regression in both forms.

**The lesson is the one this project keeps re-learning: a green gate is not a
launched game.**
