---
name: kenshicoop-sync-review
description: Checklist and invariants for changing KenshiCoop replication code — anything under src/plugin/sync/, the wire format in src/netproto/Wire.h, or the engine-apply paths. Use before writing or reviewing a change to publishing, driving, authority, spawning, or any new synced state, and to decide whether a change needs a PROTOCOL_VERSION bump.
---

# Reviewing a replication change

Two clients, each authoritative for its own squad, host authoritative for the world. Every
bug in this subsystem so far has come from breaking one of the invariants below, so check
against them explicitly rather than reasoning from scratch.

## The five questions

### 1. Am I reading state that is stale?

`EntityInterp::sample` fills `out` by copying the last received sample wholesale and then
overwriting **only** `x/y/z/heading`. So in `applyTargets`:

- `out.x/y/z` lag by the render delay (adaptive, hundreds of ms, capped by
  `maxCadenceDelayMs`)
- `out.bodyState`, `out.task`, `out.cMoving` lag by a **send interval** — ~50 ms on the
  near band, up to ~1.5 s on the mid band

If a decision asks "did a transition happen?", `out` cannot answer it. Use
`EntityInterp::latest()` for the newest received values, or key off the reliable event.

### 2. Is my self-heal debounced in both directions?

A heal exists to repair a **lost reliable event**. It reads the stream and reproduces
state locally. Fired on the first mismatching tick, it will undo an event that just landed
— because the stream still describes the pre-event world.

Both directions need a "the stream must keep asserting this" debounce, not just a
throttle between attempts. A throttle on a first attempt that is already wrong does
nothing.

The reference pair is carry: `carrySeeTick` + `tuning_.carryHealDebounceMs` against
`carryNoSeeTick` + `CARRY_DROP_MS` (`sync/ReplicatorDrive.cpp`). Asymmetry here is the
single most common bug in this codebase, and it presents to players as **actions arriving
inverted** — one player sets a body down, the other watches him pick one up.

**And the debounce must count stream progress, not wall clock.** `sample()` keeps
re-serving the same snapshot for seconds after a peer goes quiet, so a purely time-based
window expires against the very sample it was meant to wait out. Use the `healDue()` helper
in `ReplicatorDrive.cpp`: it requires both the elapsed window *and* `interp.newestMs()` to
have advanced. Found the hard way — the first version of these debounces was time-only.

**Do not trust a list of the heals here — it went stale within hours of being written.**
The roster is `grep -n healDue src/plugin/sync/ReplicatorDrive.cpp`. Every heal that reads
the delayed stream must go through it, must reset on every path that makes the condition
moot (a `*SeeTick` left armed is a heal that never fires again), and needs both halves.

The bed **fast-exit** is armed by an event rather than by a sighting, so it does not call
`healDue()` — but it applies the same two terms inline: `furnEnterHoldMs` (the wall-clock
deadline) *and* `interp.newestMs() > furnEnterSample` (a sample actually arrived inside
that window). It used to be time-only, which is how a body landed on the floor at 78 ms
RTT on 2026-08-10 while its owner saw it in bed — the deadline expired against the very
snapshot it existed to wait out. **A hold armed by an event still needs the stream term;
wall clock alone is not a debounce.**

### 3. Who authors this?

Ask `weAuthor(gw, localId, x, z)`. If both clients describe the same object, each engine's
local simulation contradicts the other forever. Doors shipped this way: both sides
published every door within 100 u, each engine opened them for its own characters, and the
result was a permanent open/close ping-pong that no echo guard could damp.

**Gate the test on `cellAuth_`, as `cellAuth_ && !weAuthor(...)`.** With presence authority
off, `authorityFor` resolves to the HOST for everything, so an unconditional test silently
stops the join publishing that channel at all. The door fix shipped without the guard and
disabled join-side door publishing in exactly the configuration the whole scenario tier
runs under (`CoopHarness.psm1` pins `KENSHICOOP_CELL_AUTH=0` unless a scenario opts in),
where no test could see it. It never returns "nobody".

### 3b. How much of each body do you actually need?

`captureOne` is ~14 engine calls; `captureLite` is identity and transform. The enumerators
take a `full` flag — pass `auditRows_` so diagnostics keep the whole picture and the player
build does not pay for fields nobody reads. The authority passes were paying full price for
every body within 2000 u, every frame, on both clients, and discarding all but the hand and
the position.

### 3c. Can this lookup see a PROXY?

**`engine::resolveCharByHand` asks the ENGINE, and the engine cannot reliably
name a body that exists on this client only as a minted proxy.** A proxy is
every NPC the peer spawned mid-session: raids, patrols, wandering squads. Bodies
loaded from the shared save resolve fine, which is why this always presents as
"it works for our squads and not for NPCs".

Three player-visible bugs in three different files were all this one thing:

| Symptom | Site |
|---|---|
| NPC and enemy health never synced (v0.64) | `applyMedical`, `applyTreatments`, `applyStats` |
| Bodies stuck mid-carry or in furniture (v0.65) | `sweepCarries` |
| **"people/enemies are duplicating over and over"** (v0.66) | the post-mint liveness guard |

The third is the one to remember, because it shows the failure is not always
merely lossy. The guard verified a freshly minted proxy with
`resolveCharByHand`, so a false negative **destroyed the body**, never entered
it into `proxyByKey_`, and let the peer's request re-arm - 51 REQs for a single
hand on one host log, minting and discarding the same NPC over and over.

Use `Replicator::localCharForStreamed(hand)`, which tries the engine and then
falls back to `proxyByKey_`. For LIVENESS, use `engine::readHand`: it
dereferences the object under SEH, which is the actual proof, and it is what
`localCharForStreamed` and the drive's `viaProxy` path already use.

Two sites are legitimately exempt, and both say so in a comment: anything gated
on `ownHands_` (a body we own is never a proxy, so the engine lookup is
exhaustive) and anything that genuinely wants "does the ENGINE know this hand",
which is a different question.

**Before writing a fix, grep `resolveCharByHand` used as a predicate.** Every
remaining one is a candidate.

### 3d. Does this suspend the body's BRAIN?

`engine::addAiSuspend` routes through `periodicUpdate_hook`, which returns early
and skips `Character::_NV_periodicUpdate` **entirely** - the engine facade calls
it "suppressing the brain wholesale". That per-tick update is not just task
selection: it is where Kenshi turns blood loss into unconsciousness, and
plausibly where several other timers live.

Players, 2026-08-16: enemies at 0 blood stayed standing **on both clients**, so
it never looked like a sync bug. Both had suspended the same brain -
`frzAct=88487` on one side, `40601` on the other. Compounded by
`applyReportedDamage`, which writes flesh/blood as raw struct fields and never
runs the engine's hit path, so nothing evaluates "should this collapse" at write
time either.

The rule: **a body that needs the engine to notice something about itself must
keep its brain.** Combat was exempted in v0.56, injury in v0.66, both by
skipping only the ACTUATION and leaving the latch armed. If you suspend a new
category, ask what the engine was going to do for that body per tick.

### 4. Does this need a protocol bump?

`PROTOCOL_VERSION` is checked at handshake and a mismatch is a **hard reject with no
back-compat**, so a bump means every player needs a new kit the same day.

- Receive-side-only change (debounce, ordering, applying existing fields differently) →
  **no bump**. Prefer this shape.
- New field appended to a `*Header` followed by an array → the receiver already validates
  `len >= need`, so mixed caps interoperate; still bump, but it is the cheap kind.
- Changing the **meaning** of an existing field, or removing one, → bump, and note it:
  no length check can detect a reinterpretation. `MoneyPacket` did this at v52.

Whatever you do, `prototest` locks the exact packed size and field offsets of every
struct. If it fails after your change, the wire format moved — decide whether you meant
it.

### 5. Is absence really absence?

A capped enumeration, a truncated census or a stale stream means **unknown**, not "gone".
Publishing unknown as absence makes the peer delete real bodies; the codebase calls a
truncated census "an ACTIVE falsehood". If a list can hit its cap, say so on the wire and
have the receiver reconcile additively.

### 6. Is anything ELSE writing this body?

Added after a live session where two of OUR OWN mechanisms fought over the same
NPC. `censusFreezeDivergedAi` calls `haltMovement()` every tick for a 20 s hold,
`parkDivergedCopy` halts and teleports, and `applyRest`/`park` halt too — while
`applyTargets` may be walk-ordering that same body, because `drivenChars_` is
cleared and rebuilt every tick and the exemption never held. Measured: 97.6% of
one NPC's active frames frozen, then 1008 consecutive clean frames the moment
the hold expired. To the player that is "NPCs march in place".

**Anything that halts, parks, teleports or suspends must ask `drivenChars_`
first.** A body the owner is streaming has a position to be at; holding it still
is the divergence the mechanism exists to prevent. Keep the latch armed and skip
only the actuation, so the hold still expires on schedule.

And when you command a speed, command one the body can reach. `currentSpeed` is
what the engine integrates along the path — an unreachable figure makes it clamp
and cover no ground, so a "catch-up" boost that is too large produces exactly the
stutter it was meant to remove.

## Shapes that are already correct — copy these

- **Event grants permission, stream actuates.** Knockout/death/revive is immune to the
  staleness class entirely: `EVT_REVIVE` only clears a latch, and the stream is the sole
  thing that stands a body up. Prefer this inversion for any new transition.
- **Hold applied at both ends.** `xferLatch_` (`sync/ReplicatorItems.cpp`) adjusts source
  and destination symmetrically, on the author side *and* the receiver side, with a grace
  window and an idempotency set.
- **Sequence-gated reliable rows with a pre-write baseline update.** The door channel
  (`sync/ReplicatorChannels.cpp`): `gateSeqAccept` drops stale/dup rows, the baseline is
  updated before the engine write so our own change is not re-detected as news, and a
  post-apply hold keeps the non-authoring side quiet.

## Before committing

- Does it build `Release` on both OSes, and does `Release` still exclude the harness TUs?
- New tunable → is it in `SyncTuning`/`Config`, wired through `configureReplicator`,
  logged in `describeConfig`, and registered in `scripts/CoopHarness.psm1`? An unwired
  tunable is a constant with extra steps. **Give it a `coop_config.json` key, not just an
  env var**: the game is launched from Steam, so env means editing Steam launch options
  (`KENSHICOOP_X=1 %command%`) — fine for the harness, hostile for a player mid-session.
  Precedence is env > json > default.
- New env flag → default OFF, cached once via the `static int x = -1; getenv` idiom, and
  `#ifdef KENSHICOOP_HARNESS` if it injects faults.
- Does the change hold up when the *peer* does it to you, not just when you do it?
  Most of these bugs are only visible from the other client.
