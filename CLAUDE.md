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
| plugin | `scripts/linux/build_plugin.sh [Harness\|Release\|Debug]` | `scripts\build_plugin_direct.ps1` (no VS2010 needed) or `scripts\build_plugin.cmd` (needs one) |
| gate | `scripts/linux/verify.sh` | `scripts\verify.ps1` |

**The two paths are verified to agree.** Built from the same commit they produce identical
`.text`, `.data`, `.pdata` and `.reloc`; `.rdata` differs only by the aligned length of the
embedded PDB path. Both scripts link objects in vcxproj source order — `/OPT:ICF` folding
depends on link order, so an alphabetical object list silently changes `.text`. See the
`kenshicoop-build` skill for the comparison procedure and the Windows VM harness.

The gate builds both configurations and runs `prototest` (612 checks: exact packed
sizes and field offsets for every struct in `Wire.h`, `PROTOCOL_VERSION`, the interp
buffer, the save-transfer receiver) and `tunneltest` (17 checks: ENet over the Steam
socket hooks under loss and the 1200 B datagram ceiling), both under Wine on Linux.
It launches no game. `Contract.Tests.ps1` runs too if a PowerShell host is installed;
on Linux that means `pwsh`, and it is skipped rather than failed when absent.

`Harness` (the default) includes the scenario runner. `Release` is the player build and
excludes `test/Scenario*.cpp` and `game/EngineProbe.cpp`.

**Windows without a VS2010 install** — verified end to end in a clean Windows 11 VM:

```
scripts\build_plugin_direct.ps1 -Config Release -Toolchain <extracted-root>
```

Use that, not `build_plugin.cmd`. MSBuild cannot build this project without a genuine
Visual Studio 2010: the `v100` toolset selection needs the legacy props a real
VS2010/SDK 7.1 installer writes under `MSBuild\Microsoft.Cpp\v4.0`, and
`Microsoft.Cpp.WindowsSDK.targets` wants a Windows SDK registered its way — MSB8036
persists with the SDK physically present and the version passed explicitly. The compiler
itself only ever needed `INCLUDE`, `LIB` and `PATH`, which is what the direct script sets.
`KenshiCoop.vcxproj` stays authoritative: the script reads the source list and the
per-configuration exclusions out of it.

Two prerequisites that a clean machine hits and no upstream doc mentions:

- **The VC++ 2010 x64 redistributable.** `cl.exe` v100 imports `MSVCR100.dll`, so without
  it the compiler does not start — exit `0xC0000135`, no output, no diagnostic. Wine ships
  a builtin, which is why the Linux path never sees this.
- **KB2519277 is gone.** The VC2010 SP1 compiler update is no longer downloadable, so
  extracting the compiler from the SDK 7.1 ISO and patching its `<deque>` (see
  `setup_toolchain.sh`) is now the only way to stand this toolchain up at all. The same
  `<deque>` fix is required on Windows.

`KC_TOOLCHAIN` also redirects `build_plugin.cmd` and `build_prototest.cmd` at an extracted
toolchain, which is enough for the direct-compiler paths (prototest builds and runs
natively this way).

## Shipping a build

```bash
scripts/linux/make_kit.sh <label>     # dist/KenshiCoop-kit-<label>.zip
gh release create <tag> dist/KenshiCoop-kit-<label>.zip --repo CaptainVirgil/KenshiCoop
```

Players then run `scripts/update-kenshicoop.ps1` (Windows) or
`scripts/linux/update-kenshicoop.sh`, which pull the latest release, verify it against the
`kit.json` manifest, back up the existing mod folder to `<Kenshi>/KenshiCoop-backups/`,
preserve `coop_config.json`, and print the protocol version. `--rollback` restores.

Saves are never touched: they live in `AppData\Local\kenshi\save` (inside the Proton prefix
on Linux), a different tree from `mods/`. Both updaters assert the path they resolved really
is `<Kenshi>/mods/KenshiCoop` before deleting anything.

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
  not "gone". Broadcasting it as absence makes the peer delete real bodies. (The census
  publish path still does this — it is a known open bug, not a rule being followed.)
- **A debounce counts stream progress, not wall clock.** `sample()` re-serves the same
  snapshot for seconds after a peer goes quiet, so a time-only window expires against the
  very sample it exists to wait out. `healDue()` requires `interp.newestMs()` to advance.
- **Authorship gates are `cellAuth_ && !weAuthor(...)`.** Unconditional, they disable the
  channel entirely on the join whenever presence authority is off.
- **Frame counts are not time.** `SUPPRESS_AFTER_FRAMES` and friends make behaviour depend
  on frame rate; `docs/REPLICATION_PITFALLS.md` §1 says stop writing them.

## Build loop

Both build scripts are **incremental**: a translation unit is skipped when its object
is newer than its source, so a no-op rebuild is under a second and a one-file edit is
about one. Touching **any** header forces a full rebuild — deliberately, because the
headers here are load-bearing and a source-only check would skip a TU a header change
invalidated. `KC_REBUILD=1` forces a full rebuild.

The log keeps one previous run as `.prev`, so relaunching after a crash no longer
destroys the log of the crash. The kit ships `KenshiCoop.map`, so a crash address in a
player's report resolves to a function.

`[net] bandwidth out=… in=…` appears every 5 s. Nothing measured its own traffic before,
which means any earlier claim about what a channel costs — including the ~40 KB/s figure
in `SyncTuning.h` — was arithmetic rather than measurement.

## Known open work

`docs/PROTOCOL_HISTORY.md` reconstructs the wire versions and marks what is evidenced
versus inferred — read it before any protocol change. A 12-dimension audit (2026-08-08)
produced a ranked backlog; the significant unfixed items are:

- **Truncated NPC census is broadcast as complete**, so the peer culls real bodies.
- **Unbounded growth** over a long session: KO/death-latched `targets_` entries are
  immortal, `invRecv_` retains ~10 KB per container ever seen, the save/load queues are
  never drained on the opposite role, and `resetSession` misses six members.
- **Peer trust**: packets are dispatched before the handshake completes, save receive does
  no size accounting, and fixed-size wire `char[]` fields are read as C strings.
- **The 20 Hz near band has no change gate** — a stationary NPC costs exactly what a
  sprinting one does. The largest remaining bandwidth win, and deliberately not taken
  blind: it changes what the peer receives for a body that is not moving, which feeds
  the interpolator, the drive and the authority dwell at once. Judge it against the
  bandwidth telemetry from a real session first.
- **`applyTargets` (~1700 lines) is still one function.** The extraction is safe in
  principle but needs a test that can see the sync layer, which does not exist yet.
- **A stub-engine harness** would make most of `sync/` testable headlessly; the audit
  rated it the largest available coverage win.

## Diagnosing a live session

Logs are `<Kenshi>/KenshiCoop_*.log`, one per client, flushed per line. The `kenshicoop-logs`
skill has the field glossary and the parse snippets; start there rather than reading raw.

Note: the log filename reflects the **panel Role**, not the negotiated one — a file named
`_host.log` may be the join's. Confirm with `[net] connected to host; sent HELLO`.

## Secrets, machine context

Machine-level context (GPU, Proton, Steam paths) is in `~/CLAUDE.md`. Kenshi lives at
`~/.local/share/Steam/steamapps/common/Kenshi`; the mod goes in its `mods/KenshiCoop/`.
