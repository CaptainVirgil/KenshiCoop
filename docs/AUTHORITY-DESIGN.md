# Authority split — the design decision (2026-08-16)

Status: **decided, not yet implemented**. Produced by a three-way adversarial
design panel (refined-spatial vs static-identity-host-asserted vs
dynamic-leases), each grounded in this repo and the measured 2026-08-16 session
data, each attacked against the Replication doctrine, then synthesized. The
sequence below begins with `authoritytest` (task #43) - nothing lands in
Authority/Publish before the harness exists.

The two findings that shaped the outcome:

- The pure spatial refinement was killed by an identity measurement: the host
  holds 524 hands, the join 312, only 153 shared. Dealing contested bodies by
  hash assigns ~106 host-only bodies to a client that cannot mint or census
  them, and the existing suppression machinery then hides the REAL bodies on
  the host within seconds - permanently. "Half the town vanished when my
  brother walked in."
- The dynamic-lease handoff was falsified by a constant already in the tree
  (MID_STILL_KEEPALIVE_MS=1500 exceeds its own 750 ms quiet window) plus six
  satellite channels hardwired to the host role.

The parity split (hContainer, 77/76, squad-coherent) survives - but as HOST
POLICY under assertion, never as distributed derivation. Derivation is the root
of every shipped authority bug: two machines computing one predicate from
diverged copies.

---

All load-bearing citations verified in-tree (`MID_STILL_KEEPALIVE_MS=1500` at `ReplicatorUtil.h:93`, `SUPPRESS_AFTER_MS=1000` / `CELL_YIELD_GRACE_MS=8000` in `ReplicatorAuthority.cpp`, the census-skip gate at `ReplicatorPublish.cpp:262-264`, the `nNotMine` authorship gate at `:1069`, `PKT_COMBAT_HIT` join-to-host at `Wire.h:79/927`, `splitAuthority` default-off at `Config.cpp:157`, pitfall #14 at `REPLICATION_PITFALLS.md:303`, task #43 at `HANDOFF.md:78`). The ruling follows.

---

# SYNTHESIS DECISION

## 1. THE RECOMMENDATION

**Build the STATIC IDENTITY split, host-asserted — amended exactly per its adversarial review (all 8 amendments, 1–3 blocking) — running as an overlay above the existing spatial cell verdict, with two grafts from the lease design's review (the migration-eligibility veto and the engagement-cluster no-split invariant) and one graft from the spatial design (contested-only scope: the overlay asserts only where space stops distinguishing the players).**

The reasoning for a skeptical maintainer, in three sentences. First: every authority bug this project has shipped — the 163 u bandit, 531 releases, incumbent-holds itself — is one derived predicate disagreeing across two machines, and pitfall #14 already states the fix as doctrine: *a predicate two clients must agree on has to be published, not derived*; host-assertion is the only one of the three designs where both-compute-"mine" and both-compute-"theirs" are unreachable by construction rather than damped after the fact. Second: it was the design whose adversarial review — the harshest attack set of the three — concluded "it is the right architecture" and whose amendments changed **zero wire bytes beyond one u32**, which is the strongest available evidence that the thesis is load-bearing and only the packaging was wrong; the spatial design's review found it deletes ~106 real bodies at flag-flip (deal domain exceeds the peer's holds), and the lease design's review found its handoff protocol falsified by a constant already in the tree (`MID_STILL_KEEPALIVE_MS=1500 > 750 ms quiet window`) plus six satellite channels hardwired to the host role. Third: it fails closed in every direction that matters — CAP interlock for mixed builds, `authAssert` config knob for rollback, arbiter starvation degrades to incumbents-keep-holding — in a codebase whose history says untested authority machinery ships player-caught regressions.

**What is taken from each non-winner, and why:**

- **From REFINED SPATIAL:** (a) *The squad as the unit* — `hContainer` is squad-coherent (104 groups, largest 12) and parity-measured 77/76; the host-asserted design already adopted it as v1 policy, keep it. (b) *Contested-only scope* — the overlay asserts only bodies in the co-location overlap; uncontested space keeps today's cell verdict, which handles the players-apart case at zero wire cost. The assign map overlays cell authority, never replaces it. (c) *Keep incumbent-holds alive underneath* — the review's amendment 3 (steady-state liveness release) restores the one-sided stream-stop handover the static design deleted; the shipped 8 s grace machinery is the reference implementation.
- **From DYNAMIC LEASED:** (a) *The migration-eligibility veto* (its review's amendment 2): never reassign a body that is fighting, bleeding/below blood floor, KO'd-on-the-wire, in dialogue, or within `FREEZE_PC_EXEMPT_DIST` of a PC — these are exactly the bodies where the census-freeze subsystem bled three exemptions and where the KO latch cannot survive an owner flip. (b) *The engagement-cluster invariant* (amendment 3): never split a fight across authors; build the cluster from published `TASK_COMBAT_*` edges — a shared fact, not a derived one — and this also answers the spatial review's "decide the fight question" amendment 6: fights coalesce to one owner, accepting un-halved load during fights. (c) *The state-machine rigor*: FLUSH-equivalent baseline seeding (the gainer seeds `hostBody_` so takeover emits no spurious KO/REVIVE edges) and "on gaining authorship, evict `targets_`/`drivenChars_`/the interp ring in the same breath" — the Holy Sentinel lesson made mechanical.
- **Explicitly rejected:** the spatial design's derived contested-cell deal (each side hashes against its own diverged copy — the root disagreement, relocated not removed) and the lease design's dynamic scoring engine (interest, margin, tenure, rate caps: three unevidenced tuning knobs plus a satellite-channel rewrite, landing in the two files with zero test coverage, to buy a rebalance that after the necessary vetoes covers only the idle majority — which the static split already covers with no knobs).

## 2. THE MECHANISM/POLICY SPLIT

**MECHANISM — on the wire, invariant, changes require a protocol decision:**

1. `PKT_AUTH_CAP` — capability announce after HELLO/WELCOME; host activates the overlay per-peer only after receipt. Fail-closed both mixed-build directions (verified against the `len >= need` census arm and unknown-type survival).
2. `PKT_AUTH_ASSIGN` — reliable, honored only from the host: canonical 5-tuple hand, `assignSeq` (monotonic per key, signed-diff wrap), `newOwner`/`prevOwner`, sid witness. Semantics: revocation of the old license and grant of the new — the KO/revive inversion. It designates, not merely permits (spatial review amendment 3): the taker takes on the event; grace is the backstop, not the mechanism.
3. Census under CAP — redefined once, deliberately: rows = "bodies I enumerate," each carrying an author byte + witness, plus **one u32 map-generation in the header** ordering the tail against `assignSeq` (static review amendments 1–2). `Publish.cpp:262`'s census-skip and `:1069`'s `nNotMine` gate are conditioned on the assign map, pinned in the harness as one invariant: *for every asserted body, exactly one client's publish gate passes.* Old-peer sessions keep today's census byte-for-byte.
4. The receive rule: entity rows for key K are applied only from K's effective author (or the previous author inside the handoff grace).
5. The state-machine invariants: stop-before-start; takeover on assign-or-stream-silence (stream progress, `interp.newestMs`, never wall clock alone); witness mismatch = refuse-and-retry-next-beat, logged; H-state bodies exempt from suppression judgment (dwell pinned, the dormancy idiom) — closes the 1000 ms-suppress vs 2000 ms-takeover race; A_mine evicts drive state and seeds the publish baseline; LEAVE reverts all of the leaver's records locally and deterministically; steady-state liveness: host revokes-to-self any assignment whose owner's stream AND census are silent ~10 s while connected.
6. `PROTOCOL_VERSION` 59 at the release where the default flips ON. The CAP interlock is the engineering; the bump is the policy (hard rule #3's hard-reject is the project norm, and kits ship to both players atomically). Carriers ship earlier without a bump because they change nothing observable.

**POLICY — host-side judgment, free to evolve every release, zero wire change.** This is where the user's "each body class can have a different method" lives, with no distributed-agreement risk because clients never compute policy — they only obey assertions; only one machine's policy ever runs, so cross-version policy disagreement is structurally impossible:

| Body class | v1 method | Free to evolve to |
|---|---|---|
| Player-squad tabs | `ownsTab`, outside the system | never changes |
| Fixtures/doors | cell claims (`Replicator.h:2417` already mandates it — they don't move, so the derived verdict is safe) | stays |
| Save-native NPCs, contested | `hContainer` parity, squad-coherent | salted/load-aware hashing, assign-at-first-census-sight for new platoons |
| Minted proxies | origin keeps minted, asserted with witness | reassignment to proxy-holder (v2 committed — see risk 5) |
| Combat-engaged / KO'd / bleeding / near-PC / dialogue | frozen with incumbent (eligibility veto); engagement clusters never split | cluster-granular co-assignment if straddle cost measures real |
| World singletons (money, time, weather, research) | host, always | never changes |

Satellite-channel routing (`PKT_COMBAT_HIT` becomes reporter-to-owner, `MedicalPacket` NPC rows follow body authorship) is mechanism, not policy — it must ship with the read-path flip or a host PC cannot damage a join-authored NPC.

## 3. THE N-PLAYER VERDICT

The user's instinct is **correct in one specific, checkable sense**: designing for N forced authority to become a published fact issued by one arbiter over canonical keys — and that is also exactly what fixes the 2-player co-location bug. The instinct navigated to the right structure. It does not follow that N-player *features* should be built.

**N-shaped parts, justified now because they are also the 2-player fix:** the host-as-arbiter over a star topology (assertion fan-out = the existing `enet_host_broadcast`); the canonical key space + sid witness (identity that survives per-process hands); the author byte naming ≤254 owners; per-peer CAP gating; `assignSeq`/map-generation ordering; the per-(body, owner) state machine with no `2` baked in; `EntityBatchHeader.ownerId` attribution, already shipped.

**Explicitly NOT built now, each with its blocker:**

- **Per-owner census sets / N-way fan-out** — `censusHands_`/`censusOwner_` is a single-owner set (`ReplicatorAuthority.cpp:94-116`); reliable census fan-out is O(N·bodies) on the host uplink. Mechanical, unneeded at N=2.
- **Join↔join stream relay through the host** — host uplink is the scaling wall; no measurement exists and no third player does either.
- **Late-join / save transfer to N** — save-xfer is a 1:1, 19.5 MB transfer; mid-session identity binding for a joiner whose save never contained the session's minted population is unsolved. Do not touch.
- **Holder-directed minting** — `PKT_SPAWN_REQ`/`INFO` is host↔join shaped; generalizing spawns per-minter id namespaces. Only needed when a non-host may author *new* spawns, which v1 policy forbids.
- **Dynamic load-balanced leasing** (`LEASE_REPORT`-style arbitration) — three tuning knobs with zero live evidence, in zero-coverage files, buying an idle-majority-only rebalance the static split already delivers. If the static split's telemetry ever shows sustained load skew that policy salting cannot fix, this is the v3 escape hatch — the assert mechanism is forward-compatible with it (a dynamic arbiter is just policy that reassigns more often).

## 4. THE SEQUENCE

Every step independently shippable. **Steps 3 onward hard-require #43** — every player-caught regression this week was in the files these steps edit.

**Step 1 (this week): `authoritytest` — task #43.** Link the real `ReplicatorAuthority.cpp` + `ReplicatorPublish.cpp` (the latter already compiles standalone, per HANDOFF) against the drivetest fake-engine pattern; two Replicator instances with mirrored claim fixtures. Pin CURRENT behavior before changing anything: cell verdict + `cellLastOwner_` memory, incumbent-holds + 8 s grace, census ⊆ authored lockstep, restore-on-authority-flip, trunc-bit handling, suppress dwell timing. (a) Gate: green pins of today's behavior; the gate script runs it like drivetest. (b) Harness: it IS the harness. No wire, no behavior.

**Step 2: shadow map, no wire.** Host computes the assign map at census cadence; logs `[auth] map` with parity balance, squad coherence, divergence-from-cell-verdict counts, witness stability, and combat-detach re-key sightings. Replay the archived 2026-08-16 logs (`dist/logs-2026-08-16/`) through the **actual shipped hash** — the 77/76 figure was measured on `hContainer` parity, not on the hash as specified; a pin justified by a story is a claim. (a) Gate: one live session + the replay; determinism and balance confirmed on the real function. (b) Harness: uses step 1 for determinism pins; shippable without behavior risk.

**Step 3: wire carriers, receive-and-store only.** CAP, ASSIGN, census author byte + map-generation u32 (emitted only under CAP). No behavior change. (a) Gate: prototest pins packed sizes/offsets AND every new default constant (the `SyncTuning` zero-init lesson — a zero grace is a plausible value meaning "churn as fast as possible"); netlinktest arms: tailless/tailed/trunc-bit/short-len attack, non-host-sender drop, old-arm unknown-type survival. (b) Harness: prototest/netlinktest; #43 for the census pins.

**Step 4: census semantic rework under CAP + read-path flip behind `authAssert: off|shadow|on`, default `shadow`.** Effective-author consulted at the `weAuthorBody` sites through canonical translation (`canonicalOf_`/`proxyByKey_`); combat-detached bodies fall to U-with-incumbent-holds until canonical publish of re-keyed hands exists; A_mine evicts `targets_`/`drivenChars_`/interp ring and seeds `hostBody_`. (a) Gate: the harness one-writer table — for each (assert schedule × loss pattern × drift pattern), never two writers, zero-writer windows bounded; the "exactly one publish gate passes" invariant; offline replay diff of old-vs-new predicate over archived logs. (b) Harness: mandatory.

**Step 5: handoff + liveness machinery.** Stop-on-ASSIGN, takeover on assign-or-silence, TAKEOVER-TIMEOUT with logged give-up, steady-state liveness revoke, LEAVE revert, witness refusal, recruit re-key follow, park→mint escalation gated on `isZoneLoadedAt` + save-native hType (kills the connect-time duplicate mint). H-state suppression exemption. (a) Gate: scripted harness sequences; contract test that the `[auth]` telemetry lines exist in Release builds (pitfall #9 — players can see this). (b) Harness: mandatory.

**Step 6: default ON, protocol 59, ship the kit.** (a) Gate — measured on BOTH clients, because the design's success metric was pointed at the wrong one: join `mine>0`/`drv` down on `[census] sent`; **host-side** renderDelay/snapSq on join-authored bodies as first-class pass/fail (the join's jit is 167–200 ms vs the host's 7.8 — if it does not fall, the correct outcome is rollback via `authAssert`, and the increment says so in writing); oscillation/revert counters ≈ 0. (b) Harness: already gated by 4–5.

**Step 7 (committed, not optional): proxy reassignment policy.** Join authors through its proxy under the canonical key (the `proxyKeyOf` publish rewrite already proves the pattern at `ReplicatorPublish.cpp:991-1045`); origin's real body becomes driven. Zero wire change — pure host policy — which is exactly why host-assertion was chosen. (a) Gate: harness pins the proxy-subject path by name (the `resolveCharByHand`-as-predicate family's next sighting lives here); live session confirms authored-count no longer decays. (b) Harness: mandatory.

## 5. THE RISK REGISTER

1. **The census's dual semantic bites anyway** — the overloaded meaning ("row = I author this") at `Publish.cpp:262`/`:1069` produces the 60 s oscillator or the zero-writer steady state on exactly the handed-over bodies. Watched by: the #43 invariant *exactly one publish gate passes per asserted body*, plus a live U-revert counter on the `[auth]` rollup. Doctrine: absence is not evidence — and a published predicate's meaning is part of the contract; it gets rewritten deliberately (under CAP) or not at all.
2. **Combat re-containering breaks the key space mid-fight** — `detachFromTownAI` re-containers a body per-process (`ReplicatorAuthority.cpp:438-440, 749-756`), so the split key mutates on precisely the bodies where authorship matters; the failure is the proxy-blind predicate family's next sighting (host reasserts-to-self while the join fights the same body under a hand the host can't resolve). Watched by: shadow-mode `DETACH-REKEY` sightings per session *before* any flip; the eligibility veto pins engaged bodies to their incumbent so the mutating field is never load-bearing while it mutates. Doctrine: a refusal is not a verdict + the MIGRATE REFUSED witness discipline.
3. **One writer who isn't writing is zero writers** — a 30 s join wedge (the 23:14 freeze class) with the host contractually forbidden to touch 76 asserted bodies. Watched by: the steady-state liveness revoke (~10 s stream-and-census silence while connected) and its counter; harness sequence pins it. Doctrine: every latch needs a second release path from the continuous stream and a horizon — `koLatched`'s lesson, applied to the AuthRec itself.
4. **Two writers, both ours, at the flip** — drive state not evicted on A_mine (interp re-serves the last host snapshot for seconds; `targets_` ages out on a horizon of minutes) or a stale census tail resurrecting a superseded owner. Watched by: the drivetest/#43 pin *zero drive actuations on K after an owner flip*; prototest pins map-generation ordering; a live rows-dropped-from-non-author counter. Doctrine: two writers on one body is still the bug even when both writers are ours (the Holy Sentinel, 97.6 % frozen).
5. **The payoff decays or merely migrates** — (a) every mid-session spawn is host-origin minted (158/312 after one evening, 1,679 combat snaps of turnover), so the 77/76 split is an opening balance decaying toward today's 0/112 until step 7 ships — hence step 7 is committed scope, watched by the per-session native-vs-minted authored counts on `[auth] map`; (b) the flicker complaint can migrate to the host's screen instead of shrinking (join jit 167–200 ms becomes the host's renderDelay input), watched by step 6's host-side gate with rollback pre-authorized. Doctrine: the rotation figure is arithmetic, not measurement — CLAUDE.md's own distinction — so the gate reads telemetry, never the prediction.

Key files for the implementer: `/home/virgil/Projects/KenshiCoop/src/plugin/sync/ReplicatorAuthority.cpp`, `/home/virgil/Projects/KenshiCoop/src/plugin/sync/ReplicatorPublish.cpp`, `/home/virgil/Projects/KenshiCoop/src/netproto/Wire.h`, `/home/virgil/Projects/KenshiCoop/src/plugin/sync/ReplicatorSpawn.cpp`, `/home/virgil/Projects/KenshiCoop/src/plugin/core/Config.cpp:157`, `/home/virgil/Projects/KenshiCoop/docs/REPLICATION_PITFALLS.md:303`, `/home/virgil/Projects/KenshiCoop/HANDOFF.md:78`.
