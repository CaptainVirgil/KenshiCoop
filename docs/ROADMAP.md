# KenshiCoop fork — roadmap

Current state, then what is left and in what order. Current-state only: when a
row is done it is deleted, not struck through. `CLAUDE.md` holds the doctrine and
the build commands; this file holds the plan.

---

## Where the fork is

**fork-6 is the public release. fork-7 is built and installed locally, unreleased.**

| | |
|---|---|
| Protocol | 54 (unchanged since fork-1 — nothing on the wire has moved) |
| Public release | `fork-6` — three assets (per-OS archive + the kit zip the updaters resolve) |
| Local build | `fork-7` — first build against KenshiLib 0.4.0; installed on both the Steam install and `~/Kenshi-Join` |
| Gate | 727 prototest / 17 tunneltest / 33 contract, green |
| Interop | fork-6 ↔ fork-7 **do** connect: the handshake compares `PROTOCOL_VERSION`, not the build label |

fork-1 through fork-5 were dead on arrival (no `/GL`) and are withdrawn or
superseded; fork-5 was deleted outright, tag included.

**Never play-tested with two humans.** Every gameplay fix in fork-6/7 is verified
by unit tests, a launched game, and log evidence — not by two people playing.

---

## The one thing that gates everything else

### P0 — a two-client session

Nothing else in this file changes a player's experience as much as finding out
what actually breaks with two clients talking. Three routes, in cost order:

1. **The brother, over Steam.** The only route that exercises the Steam P2P
   tunnel. Also the only one that yields a second machine's `[engine] CAPS` line.
   Blocks nothing else; needs him online.
2. **Two clients on this machine, UDP loopback.** `setup_join_install.sh` +
   `launch_coop.sh` are written and the clone exists. **Blocked**: the second
   instance will not start — see the handoff for the exact state.
3. **CT 203 on fredj as the join.** Half-built, see the handoff. Real two-machine
   UDP over the LAN, no second Steam account, and a permanently available second
   client. Most valuable, most remaining work.

What to read afterwards, either way — the log signals are listed in the
`kenshicoop-logs` skill, and `[net] bandwidth out=/in=` is the number nothing has
ever measured in a real session.

---

## Phase 1 — correctness of the record (cheap, do first)

Everything here is a doc or a script saying something untrue about the code. All
were found by an adversarial audit on 2026-08-08 and each survived an independent
verification pass. Costs are minutes to low hours; none needs a game.

**The instruction surface was audited and corrected on 2026-08-08** — `CLAUDE.md`,
all three `.claude/skills`, `README.md`, `Wire.h`'s header comment, and the four
docs with dead `resources/` pointers. Item 44 is closed by that pass. What is left
below is the *code* half of 45/46 and the two wire claims in 47.

| # | Item | Why it matters |
|---|---|---|
| 45 | Widen the incremental rebuild's header scan | **Docs corrected; code not.** The scan is still 34 headers of the 11,576 on the include path, so a KenshiLib or ENet header change rebuilds nothing. `CLAUDE.md` now says so and tells you to pass `KC_REBUILD=1` by hand — widening the scan removes the footgun |
| 46 | Port the Linux-only build features to Windows | The parity *claim* is corrected everywhere; the *gap* remains. `DepsPin.h` stamping and the map-based loadability check exist only in `build_plugin.sh`, so the dead-on-arrival class that produced fork-1..5 is still uncovered on Windows |
| 47 | Two wire claims that are actively wrong | The doc-rot half is done (BUILD_SETUP restored from upstream `30bf8ea`, `resources/` pointers repointed). Left: growing `NpcCensusHeader` is documented as a safe append and is a **mass-cull**; `ENTITY_BATCH_MAX` is called a receive-side bound and is not enforced (`hdr.count` is a `u8`, the loop runs to 255) |
| 48 | Two comments backwards about their own mechanism | The Ogre shims and the damage guard. **Keep both behaviours** — the risk is a credibility cascade where someone disproves the stated reason and deletes working code |
| 49 | ENet revision recorded nowhere | It is compiled into every shipped kit. On-disk is 17 commits past `v1.3.18`, and those commits include bounds checks in the untrusted packet parser. The documented recipe reproduces a *different, less hardened* network stack |

Also here: **detach the GitHub fork** (cosmetic — AGPL attribution stays either
way; 41 commits ahead, 0 behind, upstream dormant) and decide whether to keep
`fork-7` or cut `fork-8` after Phase 1 lands.

---

## Phase 2 — make the sync layer testable

| # | Item |
|---|---|
| 27 | Stub-engine harness for `sync/`, then extract `applyTargets` (~1700 lines) behind it. `NetLink.cpp` is the cheap first step — it includes only `NetLink.h`, `SteamP2P.h`, `CoopLog.h` and the CRT, and `tunneltest` already links ENet the same way |

The audit handed this phase its first concrete target: `ENTITY_BATCH_MAX` is
documented as a receive-side bound and is not enforced (`hdr.count` is a `u8` and
the loop runs to 255). One token at `NetLink.cpp:593` fixes it — receive-side
only, no wire change — but it wants a test that can see the receive path.

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

## Phase 4 — known bugs, not yet worth their fix

| # | Item |
|---|---|
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
