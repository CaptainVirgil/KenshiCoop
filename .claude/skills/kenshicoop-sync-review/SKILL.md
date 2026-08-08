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

### 3. Who authors this?

Ask `weAuthor(gw, localId, x, z)`. If both clients describe the same object, each engine's
local simulation contradicts the other forever. Doors shipped this way: both sides
published every door within 100 u, each engine opened them for its own characters, and the
result was a permanent open/close ping-pong that no echo guard could damp.

With cell authority off, `authorityFor` resolves to the host — which is what the world
stream already assumes. It never returns "nobody".

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
