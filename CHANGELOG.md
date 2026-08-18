# Changelog

All notable changes to KenshiCoop are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) with a beta pre-release line —
the next release is `v1.0.0-beta.1`, and `v1.0.0` lands when the two people
who actually play this call it stable.

**SemVer labels the release; `PROTOCOL_VERSION` governs interop.** Two builds
connect if and only if their protocol versions match, regardless of what the
tags say. A protocol bump is always at least a minor bump and is called out
in the notes. Releases before this file are v0.50–v0.73 on the
[releases page](https://github.com/CaptainVirgil/KenshiCoop/releases);
`docs/PROTOCOL_HISTORY.md` reconstructs the wire history.

## [Unreleased]

## [1.0.0-beta.1] - 2026-08-17 (protocol 59)

First release of the beta line. Same code as v0.73 — this release IS the
versioning change, plus the tooling that makes the beta line installable.

### Added
- SemVer beta versioning: `-alpha./-beta./-rc.` tags publish as GitHub
  pre-releases automatically; this changelog.

### Fixed
- Both updaters select the newest published release from the release LIST
  instead of `/releases/latest`, which excludes pre-releases and would have
  pinned every player to v0.73 forever. **One-time step for anyone on a
  v0.73-or-earlier kit:** run the updater once with the explicit tag
  (`-Tag v1.0.0-beta.1` / `--tag v1.0.0-beta.1`); the kit it installs
  carries the fixed updater, and every later beta updates automatically.

## [0.73] - 2026-08-17 (protocol 59)

The 2026-08-16 relay outage, fixed four ways. A Steam session flip left the
two ends disagreeing on the route; every ENet connect completed while the
one-shot HELLO died, and the join half-engaged over and over.

### Fixed
- A half-open connection is no longer a session: `localId()` reads a sentinel
  until WELCOME, and arbiter/authority/census/cell-claim work gates on
  `sessionUp()`. Ends the visible NPC blink (suppress/restore churn
  2710/1853 in ten minutes).
- HELLO re-sends every 500 ms inside a held connection instead of one shot
  per connection.
- Reconnect id allocation: the join slot is id 1 and is reused; the old
  counter would have rejected the first same-run reconnect as a third player.
- Five dead handshakes in a row now log loudly, slow the redial to 10 s, and
  put a plain explanation on the F2 panel.

## [0.72] - 2026-08-16 (protocol 59)

### Fixed
- Pause obeys the player: speed votes moved to the unreliable channel with a
  three-tick change burst. They had queued up to 27 s behind bulk reliable
  traffic ("we can't pause the game").

### Changed
- The `[wi]` ground-item rollup reports real channel rates
  (tracks/drops/sent/culls/defer), not just steady-state-zero discovery
  counts.

## [0.71] - 2026-08-16 (protocol 59)

### Fixed
- A truncated inventory capture is never diffed: boundary items in >64-slot
  containers no longer read as vanished, and a phantom loss can no longer
  pair with a real gain and move an item nobody dragged.

## [0.70] - 2026-08-16 (protocol 59)

### Fixed
- The authority arbiter follows the NEGOTIATED role, re-evaluated per tick,
  and a demoted arbiter clears its state. Configured-role wiring had made a
  panel-switched client a false arbiter whose map decayed to nothing
  (1000+ liveness revokes/hour).
- Proxy-drift telemetry thresholded and rolled up (was 57% of the join log).

## [0.69] - 2026-08-16 (protocol 59)

### Fixed
- Park/freeze exemptions are PAIRED: injured and down/dead bodies are exempt
  from the census park exactly as they are from the freeze. Un-paired, the
  park composed into the teleport-run loop ("enemies teleporting out of one
  another") and the corpse fountain.

## [0.68] - 2026-08-16 (protocol 59)

### Fixed
- Exactly one authority arbiter: the assign map is emitted only by the
  arbiter flag, not by every client streaming under cell authority.

## [0.67] - 2026-08-16 (protocol 59 - hard cut)

### Added
- The full authority redesign (docs/AUTHORITY-DESIGN.md): host-asserted
  per-body ownership (`PKT_AUTH_ASSIGN` + census author tail), hContainer
  parity policy, eligibility veto, spawn grants, liveness revoke, and the
  `authAssert off|shadow|on` rollback lever. Protocol 58 -> 59.

[Unreleased]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.73...HEAD
[0.73]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.72...v0.73
[0.72]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.71...v0.72
[0.71]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.70...v0.71
[0.70]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.69...v0.70
[0.69]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.68...v0.69
[0.68]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.67...v0.68
[0.67]: https://github.com/CaptainVirgil/KenshiCoop/compare/v0.66...v0.67
