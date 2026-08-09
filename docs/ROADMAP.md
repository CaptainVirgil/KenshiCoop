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
2. **Two clients on this machine, UDP loopback.** The 2026-08-08 "second
   instance will not start" mystery is solved: the join launch minted a BARE
   Proton prefix — no VC++ 2010 runtime (instant `c0000135` death before
   RE_Kenshi logs a line) and no steamclient wiring (the lsteamclient bridge
   `_wassert`s, which is the `0x40000015` abort). `launch_coop.sh` now seeds
   the join prefix from the host's proven one (reflink copy, `mfc100u.dll` is
   the sentinel). See the handoff for the probe state.
3. **CT 203 on fredj as the join.** The launch chain is proven mechanically
   (SLR4 pressure-vessel + `proton runinprefix` + `~/.steam/sdk64` link — the
   handoff has the full recipe) and ends at Kenshi's own **"Steam dll error"**:
   `SteamAPI_Init` needs a *running* Steam client, and the container has none.
   **Blocked on a decision**: log a Steam client in inside CT 203 (account
   conflicts with the desktop), use a DRM-free Kenshi build if owned, or
   accept route 2 instead and destroy the container.

What to read afterwards, either way — the log signals are listed in the
`kenshicoop-logs` skill, and `[net] bandwidth out=/in=` is the number nothing has
ever measured in a real session.

---

## Open decisions (cheap, need Virgil)

- **Release fork-8, or hold.** Phase 1 (items 44–49) landed 2026-08-08/09:
  instruction audit, ENet pin + stamp, full-include-path header scan, the
  Windows DOA checks, the wire-claim corrections, and the entity-batch
  receive clamp with `netlinktest` pinning it. fork-7 predates all of it and
  was never released. None of it changes the wire (protocol stays 54).
- **Detach the GitHub fork** (cosmetic — AGPL attribution stays either way;
  upstream dormant).
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
