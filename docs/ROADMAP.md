# KenshiCoop fork — roadmap

Current state, then what is left and in what order. Current-state only: when a
row is done it is deleted, not struck through. `CLAUDE.md` holds the doctrine and
the build commands; this file holds the plan.

---

## Where the fork is

**`v0.51` is the last public release, and it is the first build shaped by a
session two humans actually played on two machines.**

| | |
|---|---|
| Protocol | **56** — weather (55) and dialogue (56) on 2026-08-09, the first wire changes since fork-1. `v0.50` and `v0.51` interoperate; only the updated side gets v0.51's fixes |
| Public release | `v0.51` — five assets (per-OS archive, the kit zip the updaters resolve, and both updater scripts) |
| Local build | protocol 56, installed on the Steam install; sha256 checked against the gated artifact |
| Gate | 733 prototest / 17 tunneltest / 46 netlinktest / 33 contract, green |
| Interop | **`fork-6` will NOT connect to the current build** — protocol 54 vs 56. The handshake compares `PROTOCOL_VERSION`, not the label, and a mismatch is a hard reject |

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
  **Protocol is now 56**, so this is a hard cut: both players must update the
  same day. Since the bump is already spent, item 28's census `truncated` flag
  should land BEFORE the release rather than waiting for a bump of its own.
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
| 27 | Stub-engine harness for `sync/`, then extract `applyTargets` (~1700 lines) behind it. The first step shipped 2026-08-09: `netlinktest` links the real `NetLink.cpp` over an in-memory socket-hook pipe and pins the receive-side bounds ladder (handshake gate, batch len/count, census cap, unknown-type survival), wired into both gates. Extend the same shape toward `sync/` |

The audit's first concrete target is closed: `hdr.count <= ENTITY_BATCH_MAX`
is enforced at the batch receive arm (receive-side only, no wire change), and
the test that demanded it watched the unclamped receiver accept 255 entities
per packet before the clamp shipped.

---

## Phase 3 — protocol, when it is worth spending

| # | Item |
|---|---|
| 28 | One coordinated `PROTOCOL_VERSION` bump, batching every wire change worth making |
| 40 | Extract the P2P accept + Steam callback pump out of `SteamInvite`, then delete the unreachable lobby code. Needs a **2-minute Steam connect**, not a play-test, and must ship **alone** so a break is unambiguous |

A bump is a hard reject for anyone on the old build, so it is spent once. The
census `truncated` flag is the anchor: carry it in **bit 15 of the existing u16
`count`**, so `sizeof` stays 7, an old receiver fails `count <= NPC_CENSUS_MAX`,
drops the packet, and trips the census-STALE path that *disables* wide culling —
fail-safe rather than fail-destructive.

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
| 53 | **Split authority by hand-hash inside a contested cell.** Cell authority splits by SPACE, so two players in one cell means the host authors all ~216 NPCs and drives none while the join drives all of them. A stable hash of the (save-stable) hand splits the duty with no spatial boundary and therefore no handoff churn. Safest increment: split the DRIVE/publish duty only, leaving existence/census authority with the host, so neither client can cull the other's half |

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
