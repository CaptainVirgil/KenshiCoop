# KenshiCoop (fork)

Experimental 2-player co-op for **Kenshi**, built as an [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi)
plugin. `KenshiCoop.dll` is loaded into the game by RE_Kenshi, hooks the engine through
KenshiLib, and drives all game mutation on the main thread. Networking is ENet over UDP
with an optional Steam P2P tunnel.

This is a **hard fork** of `nhoral/KenshiCoop` (`upstream` remote). We diverge freely, but
stay merge-compatible on purpose: file layout and protocol numbering track upstream so his
ongoing work can be cherry-picked. `origin` is `CaptainVirgil/KenshiCoop`.

C++03 only — KenshiLib requires the **VC++ 2010 (v100) x64** toolset, so no C++11, no
`<cstdint>`, no `std::thread`, no scoped enums, no smart pointers.

---

## Hard rules

1. **`src/plugin/KenshiCoop.vcxproj` is authoritative** for the source list,
   per-configuration exclusions and compiler flags. Both build paths read from it. A new
   source file goes in the vcxproj, not just in a shell script.
2. **Linux and native Windows are both first-class.** The Wine toolchain is an addition,
   never a replacement. Anything that breaks `scripts/build_plugin.cmd` or the PowerShell
   harness is a bug. PowerShell changes must stay Windows PowerShell 5.1 compatible.
3. **Both players must run the same build.** `PROTOCOL_VERSION` mismatch is a hard
   connection reject with no back-compat. Do not bump it without shipping a kit to
   everyone who plays.
4. **Never edit `third_party/KenshiLib_deps/` by hand.** It is a pinned checkout plus two
   generated patches — see the trap below.
5. **Prefer receive-side fixes.** A fix that changes no wire bytes needs no protocol bump
   and stays cherry-pickable in both directions.

## Build

One-time bootstrap (Linux), no sudo and no Windows needed:

```bash
scripts/linux/setup_toolchain.sh                        # extracts v100 + SDK 7.1 from MS's ISO
scripts/linux/fetch_lfs.sh third_party/KenshiLib_deps   # LFS objects without git-lfs
scripts/linux/patch_vendored_headers.sh                 # see trap below
```

Then, on either OS:

| | Linux | Windows |
|---|---|---|
| plugin | `scripts/linux/build_plugin.sh [Harness\|Release\|Debug]` | `scripts\build_plugin.cmd [Harness\|Release\|Debug]` |
| gate | `scripts/linux/verify.sh` | `scripts\verify.ps1` |

The gate builds both configurations and runs `prototest` (517 checks: exact packed
sizes and field offsets for every struct in `Wire.h`, `PROTOCOL_VERSION`, the interp
buffer, the save-transfer receiver) and `tunneltest` (17 checks: ENet over the Steam
socket hooks under loss and the 1200 B datagram ceiling), both under Wine on Linux.
It launches no game. `Contract.Tests.ps1` runs too if a PowerShell host is installed;
on Linux that means `pwsh`, and it is skipped rather than failed when absent.

`Harness` (the default) includes the scenario runner. `Release` is the player build and
excludes `test/Scenario*.cpp` and `game/EngineProbe.cpp`.

**Deps are pinned to `e75769b`.** KenshiLib **v0.4.0 breaks the plugin**: it moved
`kenshi/CombatClass.h` under `kenshi/combat/` and added a second, byte-identical
`enum BuildingDesignation`. Do not bump the pin casually.

**Trap — two post-checkout steps, or the build fails confusingly.** After any checkout or
update of the deps, both `fetch_lfs.sh` (the `.lib` files and `boost.zip` are LFS
pointers) and `patch_vendored_headers.sh` must re-run. The header patch is idempotent and
semantically neutral — `#pragma once` becomes include guards, because MSVC dedups
`#pragma once` by canonical path string and Wine hands it the same header spelled two
ways; plus a shared guard over the duplicated enum. It is a no-op for a Windows build of
the same tree.

## Layout

| Path | What lives there |
|---|---|
| `src/netproto/Wire.h` | The entire wire contract: packed structs, `PROTOCOL_VERSION`, packet/event enums. Read this first. |
| `src/plugin/net/` | `NetLink` (ENet thread + packet dispatch), Steam P2P tunnel, invites. |
| `src/plugin/sync/` | The replicator: `Publish` (what we send), `Drive` (how peer state is applied), `Authority` (who owns what, suppression), `Spawn` (proxy minting), `Items`, `Channels`, `SaveXfer`, `Interp`. |
| `src/plugin/game/` | The engine facade — everything that touches Kenshi's own classes, behind resolved function pointers and SEH. |
| `src/plugin/core/` | Config, logging, inbound queue, crash tracer. |
| `src/plugin/test/` | Scenario harness, `Harness` builds only. |
| `scripts/`, `scripts/linux/` | Windows (PowerShell/cmd) and Linux (bash) tooling. |
| `docs/REPLICATION_PITFALLS.md` | Hard-won failure modes. Read before debugging a desync. |

Expect god files: `sync/ReplicatorDrive.cpp::applyTargets` is ~1700 lines,
`net/NetLink.cpp::threadLoop` ~1500 with a 43-arm packet switch,
`sync/ReplicatorAuthority.cpp::enforceHostAuthority` ~800. Splitting them is deferred
until the commit gate can catch what it breaks.

## Replication doctrine

The rules that actually prevent bugs. Every one of them was learned by shipping the
violation.

- **State is unreliable, events are reliable.** Continuous state (position, `bodyState`)
  self-heals at 20 Hz on the unreliable channel. A transition that must be observed
  exactly once (death, KO, pickup) rides the reliable channel.
- **The interpolated sample is stale for everything except position.**
  `EntityInterp::sample` copies the last received state wholesale and then overwrites only
  `x/y/z/heading`. So `out.bodyState` and `out.task` lag by a send interval — up to ~1.5 s
  on the mid band — while position lags by the render delay. **Never treat `out` as
  current when deciding whether a transition happened.**
- **Events and batches are not ordered relative to each other.** They ride different ENet
  channels. `applyEvents` runs pre-engine, `applyTargets` post-engine, so an event applied
  at the top of a frame can be undone at the bottom of the same frame.
- **Any self-heal must be debounced in BOTH directions.** A heal that repairs a lost event
  by reading the delayed stream will undo a fresh event unless the stream has to keep
  asserting the condition first. One-directional debounces are the single most common bug
  in this codebase: they produce actions that appear *inverted* to the other player.
- **Prefer the inversion.** Knockout/death/revive is immune by design: the event grants
  permission (clears a latch) and the stream is the sole actuator. Model new channels on
  that rather than on "apply the event, then reconcile".
- **One writer per thing.** Ask `weAuthor(gw, localId, x, z)`. If both clients describe
  the same object, each engine's local simulation will contradict the other forever —
  doors did exactly this.
- **A hold must be applied at both ends.** `xferLatch_` in `ReplicatorItems.cpp` is the
  reference implementation.
- **Absence is not evidence.** A truncated census or a capped enumeration means "unknown",
  not "gone". Broadcasting it as absence makes the peer delete real bodies.

## Diagnosing a live session

Logs are `<Kenshi>/KenshiCoop_*.log`, one per client, flushed per line. The `kenshicoop-logs`
skill has the field glossary and the parse snippets; start there rather than reading raw.

Note: the log filename reflects the **panel Role**, not the negotiated one — a file named
`_host.log` may be the join's. Confirm with `[net] connected to host; sent HELLO`.

## Secrets, machine context

Machine-level context (GPU, Proton, Steam paths) is in `~/CLAUDE.md`. Kenshi lives at
`~/.local/share/Steam/steamapps/common/Kenshi`; the mod goes in its `mods/KenshiCoop/`.
