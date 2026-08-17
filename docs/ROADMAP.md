# KenshiCoop fork — roadmap

Current state, then what is left and in what order. Current-state only: when a
row is done it is deleted, not struck through. `CLAUDE.md` holds the doctrine and
the build commands; this file holds the plan.

---

## Where the fork is

**`v0.71` is the current public release — protocol 59. The authority redesign
(docs/AUTHORITY-DESIGN.md, all seven steps) shipped as v0.67 and was then
hardened across four live-iteration releases in one night — v0.68 (single
arbiter), v0.69 (paired park exemptions), v0.70 (arbiter follows the
NEGOTIATED role, not the configured one), v0.71 (truncated inventory captures
are not diffed). The ladder and the live gate are in HANDOFF.md.** The design
is validated by measurement: a ~10-minute true-arbiter window on v0.69 took the
join's render delay from 741 ms to 50, squad snaps from 11,483 to 0, parks
from 456 to 5. v0.62 remains the only cut made with no player at the keyboard;
every other release was shaped by what two humans hit while actually playing.

| | |
|---|---|
| Protocol | **59** — authority assertions: `PKT_AUTH_ASSIGN` + census author tail (2026-08-16), after 58 (census truncation bit), 57 (mod fingerprint), 55/56 (weather/dialogue). Each bump is a hard cut; both players update together |
| Public release | `v0.72` (protocol 59) — five assets. **Hard cut**: v0.66 and earlier cannot connect. v0.67–v0.71 CAN connect but are superseded; each shipped a defect the next one fixed |
| Local build | protocol 59; the banner's build stamp is the LINK's (BuildStamp.cpp, both scripts), so "which build is this?" is answerable from the log |
| Gate | run it and read the totals it prints — the counts move too fast to record here (that is CLAUDE.md rule material, and this table violated it) |
| Interop | any release before the current protocol will NOT connect — the handshake compares `PROTOCOL_VERSION`, not the label |

fork-1 through fork-5 were dead on arrival (no `/GL`) and are withdrawn or
superseded; fork-5 was deleted outright, tag included.

**First two-client session ran 2026-08-09** — automated, one machine, UDP
loopback, save transfer + drive + authority all live (see P0 below). Still
never play-tested with two humans.

---

## The one thing that gates everything else

### P0 — a two-client session: ACHIEVED 2026-08-09 (automated, one machine)

Host + join, both fork-7, protocol 54 at the time, UDP loopback, zero clicks:
`launch_coop.sh hostdirect` with `KENSHICOOP_AUTOCONNECT=1
KENSHICOOP_SAVE=coop4`, then `KENSHICOOP_AUTOCONNECT=1 launch_coop.sh join`.
The session did everything the doctrine promises: handshake + clock sync
(offset 0 on loopback), host auto-baked the save and pushed it (256 files /
19.3 MB in 9.1 s, CRC clean), join committed and auto-loaded it, then live
replication — drive (`PARKED→MID`), pose orders, authority restores, trust
gating, inventory, camera hints. `[caps] 0 of 17` on both sides; zero
`[ko] RELEASE`, zero `MINT-RETRY GIVEUP`, zero `unknown packet` in session one.

**The number nothing had ever measured:** steady state with the save loaded,
host `out≈166 KB/s in≈118 KB/s` (join mirrors it). The near-band change-gate
decision below now has its real-session baseline.

**First-session finding:** `[inv] APPLY` repeats identically ~14/s on BOTH
sides (~2100 lines in 2.5 min) — the inventory snapshot channel looks
ungated. Judge against `gateShouldSend` before the next tuning pass.

What unblocked it (was: "second instance will not start"): the join launch
minted a BARE Proton prefix — no VC++ 2010 runtime (`c0000135` before
RE_Kenshi logs a line) and no steamclient wiring (the lsteamclient bridge
`_wassert` is the historic `0x40000015` abort). `launch_coop.sh` now seeds
both direct-launch prefixes from the live one (reflink; `mfc100u.dll` is the
sentinel), pins the runner to Proton Experimental, and passes the exe path
absolute (relative dies silently one line into Proton).

### It was then PLAYED, and found four defects in one sitting

Everything below was invisible to unit tests, a launched game and log
evidence — the exact verification the fork had relied on for its whole
history. All four are fixed, gated and pushed; none changed the wire.

| Found | Cause |
|---|---|
| Reliable channel flooded, ~166 KB/s | `abe12a2` moved the inventory resend literals into `SyncTuning` without ctor initialisers → interval 0 → every container re-sent every tick, silently (the SEND log only covers content-change). Now 36 KB/s; `prototest` pins the defaults |
| Peer's character teleported every ~4 s while running | The catch-up boost was overwritten by the gait mirror the same frame, and the commanded speed used the owner's speed *setting* (43) not its measured translation (65). Squad snaps 9 → 2 per window |
| Town NPCs flapping, pose mismatch, seats broken | A stationary mid-band body was never streamed at all, so the peer read silence as "at rest", released it to local AI, and it wandered to the 120 u park. Keepalive + rest re-assert: cull 903→35, restore 891→22, park 828→197, freeze 1452→423 |
| Join crash on dialogue with a seated NPC | `censusFreezeAi` had frozen the NPC's AI; the engine's dialogue state machine threw. Fix pending (task: interaction hold) |

**The lesson worth keeping:** a green gate and a launched game agree with each
other and can still both be wrong about what two clients do. The first hour of
real play outproduced every automated signal this project has.

Still open, in cost order:

1. **A session played by two HUMANS.** One person driving both windows found
   the above, but does not exercise a second machine, the Steam P2P tunnel, or
   a real network path. Log signals: the `kenshicoop-logs` skill.
2. **The brother, over Steam.** The only route that exercises the Steam P2P
   tunnel and yields a second machine's `[engine] CAPS`. Flip both configs
   back to `"transport": "steam"` first — both sit on `udp` today.
3. **CT 203 on fredj.** Launch chain proven end-to-end (SLR4 pressure-vessel +
   `proton runinprefix` + `~/.steam/sdk64` link; recipe in the handoff), and
   stops exactly one dialog short: Kenshi's own **"Steam dll error"** —
   `SteamAPI_Init` wants a *running* Steam client and the container has none.
   Decision in "Open decisions" below.

---

## Open decisions (cheap, need Virgil)

- ~~**Cut `v0.50`, or hold.**~~ **Done** — `v0.50` shipped 2026-08-09, then
  `v0.51` the same night with the fixes the two-machine session produced.
  Protocol has since moved 56 -> 57 -> 58; every bump is a hard cut and both
  players update the same day. Item 28's census flag landed with 58.
- ~~Detach the GitHub fork.~~ **Done 2026-08-09** — `isFork: false`,
  `parent: null`. Standalone repo. Attribution to nhoral and
  RE_Kenshi/KenshiLib is in the README's opening block and Credits, and
  AGPL-3.0 is kept deliberately as a derived work, so nothing is owed less by
  standing alone.
- **CT 203**: Steam-client decision above, or `pct destroy 203`.

---

## Phase 2 — make the sync layer testable

| # | Item |
|---|---|
| 27 | Stub-engine harness for `sync/`, then extract `applyTargets` (~1700 lines) behind it. **Second slice shipped 2026-08-10: `drivetest`** links the REAL `ReplicatorDrive.cpp` + `ReplicatorCore.cpp` + `Interp.cpp` against a fake engine and asserts BEHAVIOUR (tier hysteresis monotonicity, mid-rest release/re-adopt, walker steady-state), feeding through the public surface only (`Inbound::pushEntity` → `ingest` → `applyTargets`) with a 3-member stub seam. In the gate. Remaining: a fake-clock seam (real-time pacing costs ~20 s and cannot fast-forward the minute-scale windows), a pathfinder-lag knob (the fake `walkTo` obeys instantly, so catch-up/snap dynamics are untested), then the extraction itself |

The audit's first concrete target is closed: `hdr.count <= ENTITY_BATCH_MAX`
is enforced at the batch receive arm (receive-side only, no wire change), and
the test that demanded it watched the unclamped receiver accept 255 entities
per packet before the clamp shipped.

---

## Phase 3 — protocol, when it is worth spending

| # | Item |
|---|---|
| 40 | Extract the P2P accept + Steam callback pump out of `SteamInvite`, then delete the unreachable lobby code. Needs a **2-minute Steam connect**, not a play-test, and must ship **alone** so a break is unambiguous |

A bump is a hard reject for anyone on the old build, so it is spent once.
Item 28 (the census truncation bit) shipped as protocol 58 on 2026-08-10 —
see `docs/PROTOCOL_HISTORY.md` §58 for what the receiver now does with it.

---

## Phase 3b — main-thread relief (opened by the played session)

The join client is frame-starved while the host idles, and that imbalance is
what the remaining desync reduces to: a starved client publishes a lumpy
stream, so the *other* client's copy of its character accrues gap and snaps.
Bandwidth is not the constraint (36 KB/s measured on a link that moved a save
at ~900 KB/s); Kenshi's per-frame engine-call budget is.

| # | Item |
|---|---|
| 50 | **Per-frame time budget.** Plugin cost scales with town size. The mid band already round-robins and the wide sweep is throttled, but there is no global budget: stamp a counter at tick entry, stop issuing engine calls past ~2 ms, resume next frame from a cursor. Makes plugin cost independent of how many NPCs are loaded |
| 51 | **Cache hand→`Character*` resolution.** Every pass re-resolves each hand from scratch (deliberate: a despawn degrades to a skip). Hundreds per frame in a town. Wants a generation-stamped cache with a cheap validity check, invalidated where the session reset already clears pointer caches |
| 52 | **`captureLite` audit.** Doctrine says the authority passes pay ~14 engine calls per body and discard all but hand and position. Verify per call site, flip the ones that over-capture |

## Phase 4 — known bugs, not yet worth their fix

| # | Item |
|---|---|
| 56 | **Weather sync SHIPPED (protocol 55).** An earlier entry here called it blocked because `Weather.h` has no `_NV_` wrappers — wrong: those exist only to bypass a vtable, and `GetRealAddress` takes any non-virtual function pointer, which is how this codebase already resolves `Character::setDestination`. Host publishes the active biome region's weather at ~1 Hz, change-gated; identity travels as the GameData stringID. `Weather.h` itself is unincludable (duplicate `WeatherRegion` across two dump headers, plus missing forward declarations) so the facade uses local offset mirrors. **Unverified in game:** whether writing the instance fields + `requestUpdateEffects` is enough for the visuals to follow |
| 54 | **Dialogue relay SHIPPED (protocol 56).** Capture hooks `DialogueSpeechBubble::setText`/`setPosition` and correlates on the bubble pointer — a workaround for `speechBubbleList` being the one dialogue symbol KenshiLib does not export. Sends the world position rather than a speaker hand; the receiver attaches the text to the nearest character so it tracks the speaker, and drops the line when nothing is within 40 u. Symmetric and fire-and-forget. **Unverified in game:** whether the hooks fire for every speech path, and whether 40 u is the right catch radius |
| 55 | **Dialogue with a frozen NPC crashes the client.** `censusFreezeAi` suspends a parked NPC's AI; starting dialogue with one throws an unhandled C++ exception inside the engine (`0xE06D7363`, no KenshiCoop frame on the stack). Reproduced twice on a seated NPC. Wants an interaction hold: a body in dialogue with the local player is exempt from freeze/park/cull until it ends, same latch shape as `xferLatch_` |
| 37 | `gateShouldSend`'s effective resend interval is `max(minSendMs, resendMs)` — the throttle is evaluated before the resend. Latent: only money passes a non-zero `minSendMs` |
| 38 | `CATCHUP_K` has zero stability headroom — `K·dt = 2.0 × 0.5 s = exactly 1.0` on the mid band. Not broken today; one band retune from a copy that walks through the source and back |

Both are pinned by tests that describe the current behaviour, so a fix fails a
check and forces the decision to be made deliberately.

---

## Closed with a decision (do not reopen without new evidence)

- **Near-band change gate** — measured from a real session: `near=41` peak ×
  20 Hz × 79 B ≈ 65 KB/s, ~5% of budget. Would save part of that while changing
  what the interpolator, the drive and the authority dwell all read. No payoff,
  real risk.
- **Capability report → gate** — all 25 capabilities resolve. Gating buys nothing:
  a missing capability already no-ops in the wrappers, so disabling the feature
  produces the identical player outcome, against a real risk of switching off
  something that works.
- **Windows VM** — the native Windows *build* is proven; what needs a Windows
  machine now is a *run*, and the brother's machine is that for free. The
  dockur recipe stays documented in the build skill.

---

## Closed since that audit (do not reopen)

| Item | Where it went |
|---|---|
| **20 Hz near-band change gate** | DONE v0.65. `[net] mix` measured ent at ~94 KB/s / 87% of outbound - the evidence the hold required. 200 ms keepalive, under the receiver's 250 ms tier line; four bounds pinned in prototest |
| **Raise `combatSnapDist_` (H8)** | DONE v0.66, but NOT by raising it. The veto had reused a 20 u CONVERGENCE constant as its range and could never fire (1,679 snaps, median 89.9 u, none <= 20; `snapCbt=727 snapVeto=0`). It has its own `COMBAT_VETO_MAX_DIST` = 120 u; `COMBAT_SNAP_DIST` is untouched. Pinned in Contract.Tests.ps1, because ReplicatorUtil.h is not includable from prototest |
| **Time-slew ramp hoist (H6)** | DONE v0.66. The ramp sat after `syncTime`'s "no new sample" return, so it fired at ~1 Hz with `dt` clamped to 1.0 and applied its full 0.35 as a STEP. Now ramps per frame toward a stored target |
| **Log burst suppressor (H1)** | REJECTED permanently. Per-line flush is what makes a crash keep its tail. v0.63 shipped the line-rate COUNTER only; fix a storm, never hide one |
| **Hand-hash authority split in a contested cell (was 53)** | SUPERSEDED by the authority redesign, v0.67–v0.71. The shipped design is that item grown up: hContainer PARITY (77/76 measured, squad-coherent — hSerial parity was 153/0, pointer-aligned garbage) instead of a raw hash, ASSERTED by the negotiated host over the wire instead of derived independently on each end, with an eligibility veto, spawn grants, and liveness revoke. docs/AUTHORITY-DESIGN.md records the killed alternatives |

## The one shape behind most of v0.64-v0.66

`engine::resolveCharByHand` cannot address a minted proxy. Three player-visible
bugs, three files: NPC health never applying, bodies stuck mid-carry, and
"people are duplicating over and over" - the last because a post-mint liveness
guard used it as a predicate, so a false negative destroyed the body and let the
peer re-request (51 REQs for one hand). **Grep for it used as a predicate before
writing the next fix.**

Second shape, found 2026-08-16: `addAiSuspend` skips the engine's whole
per-character update, which is where blood loss becomes unconsciousness. A
suspended body never collapses - on BOTH clients, so it does not present as a
sync bug. Combat exempted v0.56, injury v0.66.

## Held for daylight (2026-08-10 audit, after v0.62)

An audit ran while the players were offline: three parallel auditors over
tonight's diffs, the publish-phase freeze and the census/authority machinery,
then a ship-safety pass ranking every proposed fix by blast radius. What
shipped in v0.62 is below; **this list is what was deliberately NOT shipped
unattended**, with the evidence each one needs first. It is a decision record —
do not re-derive these, and do not implement one without its listed evidence.

The rule the ranking used: `drivetest` links `ReplicatorDrive.cpp`,
`ReplicatorCore.cpp` and `Interp.cpp`, so a fix there is gate-testable.
`ReplicatorAuthority.cpp`, `ReplicatorPublish.cpp`, `ReplicatorItems.cpp` and
`ReplicatorChannels.cpp` are **linked by no test at all** — a fix there ships on
reasoning alone. All four regressions live players caught this week were in that
second group.

| Held | Where | Needs first |
|---|---|---|
| Event-storm debounce | `ReplicatorPublish.cpp:517-558`, `:577-612`, `:636-680` — unthrottled per-frame edge detectors authored for every streamed NPC (`carryAuthor` at `:572`), feeding an uncapped `evt_` (`Inbound.h:428-443`) | Queue-depth-at-drain telemetry. It is the only signal separating "a stage got slow" from "a stage was handed 40,000 items" |
| Log burst suppressor | `CoopLog.cpp:25-42` | Nothing — **rejected**. Per-line flush is what makes a crash keep its tail. Fix the storm, never hide it. v0.62 ships the line-rate COUNTER only |
| `MID_RESEND_MIN_MS` vs small bands | `ReplicatorUtil.h:105`, `ReplicatorPublish.cpp:407`. `quota=(sz+9)/10`, so for `sz<10` the rotation is 50-450 ms and a 350 ms suppression demotes near-tier movers into the whole mid-tier machinery | A publish-side harness. This changes what the publisher sends, which feeds the interpolator, the tier classifier and the census park at once — the exact triple that produced v0.51 and v0.57 |
| First-sighting `stillQuota` | `ReplicatorPublish.cpp:379-387` | Same harness. Changes send composition right after a save load, when the join is most fragile |
| Truncation-hold ceiling | `ReplicatorAuthority.cpp:472`, `:627` | One session's `truncHold=` numbers. A ceiling re-arms the unbounded over-cull — "absence is not evidence" |
| Time-slew ramp hoist | `ReplicatorChannels.cpp` — the ramp sits after `if (!newest) return;`, so it steps 0.35 once per second rather than ramping | A channel test or an attended A/B. A 0.35 step is survivable; an oscillating game clock in a live session is not |
| Census freeze latch semantics | `ReplicatorAuthority.cpp` — the hold-expiry branch is unreachable behind the combat early-return, and a >30 s fight loses an armed latch to the `PRUNE_MS` sweep | An authority harness. `endAction` drops an in-progress attack; this is latch-lifetime surgery in an 800-line uncovered function |
| Raise `combatSnapDist_` | `ReplicatorDrive.cpp` — the veto only fires within `COMBAT_SNAP_DIST=20`, but the motivating sighting was gap=38-75 | One session where `snapVeto=` is non-trivial. **If `snapVeto` stays near zero while `snapCbt=` climbs, the 20 u ceiling is the bug** and `readCombat` is doing the discrimination |
| `cellAt` memoization, `authCount_` prune | `ReplicatorAuthority.cpp` | These change who owns which body. Authority harness |
| `publishWorldItems` / `publishInventories` cadence gates | `ReplicatorItems.cpp:417`, `:98` — neither has any entry throttle; three spatial queries per frame | The new `[stage]` peak-µs line proving they are the cost |
| ~20 unbounded maps | `combatCapMs_`, `invPub_`, `weaponCensus_`, `parkMs_`, `censusPos_`, … no erase site | Batch behind a harness. Low confidence as a freeze cause; real as a leak |
| `modsFingerprint` static buffer race | `CoopLog.cpp` — `static char data[262144]` shared main/net thread | Nothing but daylight. It edits the handshake — the code deciding whether two players can connect at all |

**The one recurring shape, again.** Every item above is the same defect class
this project keeps meeting: *two different facts treated identically*. A
truncated census as absence, a stale latch as a live one, a body with no rcon
as an unreachable one. It is worth reading that list before writing the next
fix, because the next one will be in it too.

---

## Deferred with findings recorded (2026-08-10, v0.56 planning)

- **House furniture strip on purchase.** Buying a house makes Kenshi empty its
  pre-placed furniture; the deed channel replicates the ownership and not the
  consequence, so the peer keeps furnished walls. The internals container is
  behind `Building::myInterior` (`BuildingInterior*` @0x1F0) and
  **BuildingInterior is undumped** — no strip primitive is nameable from
  headers. Recorded path: (1) Harness-only detour on `Building::buyMeCallback`
  (RVA 0x7AC460, the whole purchase transaction — deliberately not called by
  the deed channel, double-charge) to trace what the strip actually invokes;
  (2) candidate reproduction: `ZoneMapContent::findAllBuildings` (RVA
  0x9FA220, lektor out) + `Building::isFurniture` (0x296440) +
  `isIndoors` (0x546DB0) == house, then `removeAnInternalBuilding` (0x7CC2E0)
  + `LevelEditor::deleteObject` (0x776690), verified against
  `getNumInternalBuildings` before/after. Remember the lektor layout lesson
  from the weather fix before walking anything.
- **NPC shuffling — ROOT-CAUSED AND FIXED (v0.57, 2026-08-10).** After three
  theories died by measurement (starvation, two-writer, fixture-pose), a
  five-agent deep dive with adversarial verification found it: the
  hysteresis-free mid-tier classifier (`segMs > 250`) flapping on the
  publisher's scan-overlap double-sends (50 ms segments), driving 4,592
  PARKED↔MID cycles in 50 min, each one a clearGoals+endAction+halt+teleport+
  release. Decisive number: 3,398 demotes vs 66 walk-hold edges. Fixes:
  receiver demote-hysteresis (2 consecutive sub-250 arrivals to leave mid),
  publisher one-send-per-rotation (MID_RESEND_MIN_MS), stillness by position
  not flags. Live verdicts must move TOGETHER: ledger pair rate, reissueNpc,
  [census] park, worstZero, resendSup=. The RELEASED-body two-writer family
  (doors at night) is a separate open item — fix direction A in the deep-dive
  record (restructure the release; v0.51's attempt failed on shape, not
  concept).
- **`~/Kenshi-Join` holds two different game versions** — kenshi_x64.exe
  1.0.68 at its root, 1.0.65 under `RE_Kenshi/`. Any future on-disk RVA work
  must name which binary it measured (and note `GetRealAddress`'s space is
  not `base + header RVA` regardless — see `EngineInventory.cpp`'s
  prologue-scan).
