# KenshiCoop (fork)

Experimental 2-player co-op for **Kenshi**, built as an [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi)
plugin. `KenshiCoop.dll` is loaded into the game by RE_Kenshi, hooks the engine through
KenshiLib, and drives all game mutation on the main thread. Networking is ENet over UDP
with an optional Steam P2P tunnel.

**Its own project, derived from `nhoral/KenshiCoop`.** The GitHub fork link was
cut on 2026-08-09 (`isFork: false`) and the `upstream` remote is still there — his work is worth watching and a fix can still be
cherry-picked either way — but the two are no longer merge-compatible: protocol 56 vs
54, a different build system, a different release line. Attribution is permanent and
deliberate (README + AGPL-3.0 as a derived work), not a formality to be dropped later.
Releases are **semver (`v0.50`)**, not `fork-N`: the label names a release of THIS
project rather than a position relative to somebody else's repo. `origin` is
`CaptainVirgil/KenshiCoop`.

**VC++ 2010 RTM (v100) x64 is mandatory**, and the reason is not the one this file
used to give. `KenshiLib.lib` is a pure import library — no `.drectve`, no
`FAILIFMISMATCH`, no `DEFAULTLIB` — and MSVC name mangling is toolset-invariant, so a
**modern MSVC build links cleanly** and then reads the game wrong. The real coupling is
`sizeof(std::string)`: 40 bytes in VC10, 32 in VS2015+, and the vendored headers pin
members at literal game offsets that assume 40 (`kenshi/Character.h`: `sex` at 0x610 →
`nameTag` at 0x638). `movement` (0x640) and `body` (0x648) sit after it and are
dereferenced directly, so under a 32-byte string every one of those reads lands 8 bytes
early. The DLL also exchanges `std::string` with **Kenshi.exe itself** across
MSVCP100/MSVCR100. mingw fails loudly at link; modern MSVC fails **silently at runtime**.

What the toolset actually forbids: no `<thread>`/`<mutex>`/`<atomic>` (not shipped with
it), no `enum class` (`_MSC_VER` 1600). `_HAS_CPP0X` is on and `<cstdint>`, `<memory>`,
`<tuple>` and `<type_traits>` all exist — `<stdint.h>` is already compiled into the
shipping plugin via KenshiLib's own `core/Functions.h`. **Prefer plain C++03 style for
consistency**, but do not treat the wider ban as a constraint the compiler enforces; it
does not, and pretending otherwise cost this project a piece of fork-lore.

The "no `std::thread`" half is load-bearing for a different reason: all game mutation
happens on the main thread, and not having threads available is what keeps it that way.

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
| plugin | `scripts/linux/build_plugin.sh [Harness\|Release]` | `scripts\build_plugin_direct.ps1` (no VS2010 needed) or `scripts\build_plugin.cmd` (needs one) |
| gate | `scripts/linux/verify.sh` | `scripts\verify.ps1` |

**The two paths agreed when last compared, at `5a7902d` (2026-08-08), and that proof is
now stale.** Built from the same commit they produced identical `.text`, `.data`, `.pdata`
and `.reloc`, with `.rdata` differing only by the aligned length of the embedded PDB path.
Both scripts link objects in vcxproj source order — `/OPT:ICF` folding depends on link
order, so an alphabetical object list silently changes `.text`.

Since that proof the scripts diverged and re-converged: the `DepsPin.h` stamp (now
covering KenshiLib AND the vendored ENet), the map-based loadability check, and the
full-include-path header scan exist in **both** scripts as of 2026-08-08 (roadmap items
45/46) — but the Windows port has **not yet run on a real Windows machine**, and the
artifacts have not been re-compared since `5a7902d`. No automated parity gate exists, and
no Windows-built DLL has ever shipped — both `dist/release-fork-6/{win,linux}` DLLs are
the same Linux build. See the `kenshicoop-build` skill for the comparison procedure.

**The Linux gate builds both configurations**; the Windows gate does not build the plugin
at all (`verify.ps1` says so itself: "Requires NO Kenshi launch and NO KenshiCoop.dll").
That matters more than it sounds — the guard that refuses to emit a DLL whose main-loop
hook is not an import lives *inside* the plugin build (and, since 2026-08-08, in
`build_plugin_direct.ps1`'s post-link checks too), so a Windows `verify.ps1` PASS still
says nothing about a DLL nobody built with the direct script. Both gates run `prototest`
(exact packed sizes and field offsets for every struct in `Wire.h`, `PROTOCOL_VERSION`,
the interp buffer, the save-transfer receiver, the drive's band/convergence arithmetic,
the change-gate table, wire string termination, the targets_ age-out predicate),
`tunneltest` (ENet over the Steam socket hooks under loss and the 1200 B datagram
ceiling), and `netlinktest` (the REAL `NetLink.cpp` receive path over an in-memory
socket-hook pipe: the handshake gate, the entity-batch len/count caps, the census cap,
unknown-type survival), all under Wine on Linux. It launches no game. `Contract.Tests.ps1` runs too if a PowerShell host is installed;
on Linux that means `pwsh`, and it is skipped rather than failed when absent — it also
carries the source-drift checks a C++ test cannot do, such as verifying that every
`char[]` in `Wire.h` is named by a `wireSanitize` overload.

Check counts are deliberately NOT written down here. They move every time a test is
added, and a stale number in a context file is worse than no number — run the gate and
read the total it prints.

Both gates delete their test binaries before rebuilding. A build that fails otherwise
leaves the PREVIOUS binary on disk, the run step executes it, and the gate reports a
green suite for code that does not compile. That happened on 2026-08-08.

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

**Trap — the vcxproj hides one setting outside `<ItemDefinitionGroup>`.**
`<WholeProgramOptimization>` lives in the `Label="Configuration"`
`<PropertyGroup>`, not with the other compiler and linker settings, and it is
**load-bearing**: without `/GL` + `/LTCG`, KenshiLib's member-function stubs are
emitted as ordinary local functions, so `&GameWorld::_NV_mainLoop_GPUSensitiveStuff`
resolves inside `KenshiCoop.dll`, `KenshiLib::GetRealAddress` asserts, and the game
shows an assertion box and dies before the plugin finishes loading. Both build
scripts read it out of the project now — do not hardcode it, and do not drop it.

Check a suspect DLL without launching anything:

```
grep _NV_mainLoop_GPUSensitiveStuff build/Release/KenshiCoop.map
```

`__imp_...` (an import from KenshiLib.dll) is correct. A bare
`?_NV_mainLoop_GPUSensitiveStuff@GameWorld@@QEAAXM@Z` with an address is a local
definition, and that build will assert on startup. This is also why the kit ships
the `.map`.

**Deps are pinned to `b566d74` — KenshiLib 0.4.0, matching the runtime.**

This used to be pinned at `e75769b` (v0.1) with the note *"v0.4.0 breaks the plugin"*.
That was **fork-lore**: the pin went unrevisited while RE_Kenshi shipped 0.4.0, so the
build described Kenshi's memory layout three minor versions out of date from the library
actually serving it at runtime. Bumping cost exactly three mechanical things:

1. `kenshi/CombatClass.h` → `kenshi/combat/CombatClass.h` (one include).
2. `KenshiLib/Include/kenshi` must ALSO be on the include path — 0.4.0 moved headers into
   subdirectories but still includes siblings relative to `kenshi/` (`kenshi/combat/CombatClass.h`
   does `#include "Enums.h"`). Handled in `vcenv.sh` and `build_plugin_direct.ps1`.
3. The duplicate `enum BuildingDesignation` — already handled by `patch_vendored_headers.sh`.

Checked before trusting it: the RVAs we actually call are **unchanged** between the two
(`CharMovement::restore` `0x661810`, `teleportCollisionHull` `0x65D4E0`), so no address we
were using had silently drifted.

**The general rule this earned:** a pin justified by a story rather than a re-test is a
claim, not a constraint. Before repeating one — including one written here — check it.

**ENet is pinned too: `5a9c537f`** (lsalzman/enet master, 17 commits past v1.3.18).
Those commits are packet-parser hardening the 1.3.18 release lacks — never substitute
the release tarball. The checkout is gitignored, so the pin lives in
`third_party/enet/README.md` (fetch + patch recipe) and is stamped into the DLL next to
the KenshiLib pin (`[host] ENet vendored=...`; `-dirty` = the two patches, healthy).

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
- **A latch is a hold against a dropped batch, not memory that outlives the stream.** It
  exists so a body stays down when one lossy sample reads upright. Give every latch a
  second release path from the continuous stream and a horizon after which it expires —
  `koLatched` had neither, so a missing `EVT_REVIVE` pinned a body down for the session
  and its map entry never died.
- **The owner's word outranks a local divergence.** When the stream says a body is queued
  and this client's copy has independently engaged, the stream wins. Ordering a ternary
  the other way round is all it took to make `COMBAT_WAIT_DIST` unreachable.
- **A refusal is not a verdict.** A mint/place/apply that the engine declines usually means
  "not here, not yet" (an unloaded zone), not "never". Retain the intent and retry on a
  bounded schedule, and log the give-up — a silent permanent failure is unfixable from a
  bug report.
- **Validate untrusted floats before they reach the engine.** Use `!(v >= 0.0f)`, not
  `v < 0.0f`: NaN fails every comparison, so ordinary clamps decline to fire and pass it
  straight through. `SpeedPacket.speed` reaches the sim clock rate.
- **Do the safety thing where everything funnels, not at each call site.** Wire string
  termination was a rule to remember 23 times and was forgotten 5 times; it now lives in
  `readPacket`. Where a hand-written list remains (the `wireSanitize` overloads), a
  contract test reads the source and fails when the list falls behind.
- **A refactor that moves a literal into a struct must move it into the CONSTRUCTOR.**
  `abe12a2` lifted `5000/30000/24` out of `ReplicatorItems.cpp` into `SyncTuning` fields
  and never initialised them. Zero-initialised, the inventory resend interval was 0, so
  every container re-sent its whole snapshot every tick on the *reliable* channel — for
  a full session, undetected. `prototest` now pins those three defaults; pin any cadence
  field you add the same way, because zero is a plausible-looking value that means
  "as fast as possible".
- **A channel with a periodic leg needs observability on that leg.** The flood was
  invisible because `[inv] SEND` only logs the content-change branch, and the
  serializer's own comment claimed the channel "stays quiet". Quiet is not silent: emit
  a rollup (`[inv] resent N in 60s`) so the real rate is readable in any log.
- **A commanded speed the body cannot reach does not make it hurry - it makes it
  stall.** `currentSpeed` is what the engine integrates a body along its path
  with, so handing it an unreachable figure makes it clamp and cover no ground.
  Measured: a 2.5x catch-up boost took a driven copy's zero-movement share of
  active frames from 1% to 32%, and that stutter IS the teleporting the boost
  was meant to fix. Caps are 1.5x for the order and 1.25x for the per-frame
  mirror.
- **Two writers on one body is still the bug, even when both writers are ours.**
  The census freeze called `haltMovement()` every tick for 20 s while the drive
  walk-ordered the same body, because `drivenChars_` is rebuilt every tick and
  the exemption never held. One Holy Sentinel sat frozen for 97.6% of its active
  frames, then tracked 1008 consecutive frames cleanly once the hold expired.
  Anything that halts, parks or teleports must ask `drivenChars_` first.
- **A speed SETTING is not a measured velocity.** `EntityState::cSpeed` is the owner's
  `CharMovement::currentSpeed`; the engine translates a body faster than that (slope and
  other modifiers land outside it) — measured 64.7 u/s actual against 43.4 reported. A
  drive that commands a copy at the setting can never catch a source moving at the
  truth, and no lead distance fixes a pursuer that is simply slower.

## Build loop

Both build scripts are **incremental**: a translation unit is skipped when its object
is newer than its source, so a no-op rebuild is fast and a one-file edit is about one
second. Touching any header **on the include path** — `src/`, `vc10_compat`, the
KenshiLib deps (boost included) and ENet, ~11.6k headers checked in one scan pass per
build — forces a full rebuild, deliberately: those headers are load-bearing and a
source-only check would skip a TU a header change invalidated. That covers
`patch_vendored_headers.sh` rewriting vendored headers and the ENet socket-hook patch
being applied or reverted, which the old 34-header scan silently missed.

**`KC_REBUILD=1` is still the manual big hammer**, needed only for a change that moves no
header mtime forward: extracting an archive that preserves old timestamps (boost.zip's
headers carry 2016 dates) or an `rsync -a` from elsewhere. `git checkout` and the patch
scripts stamp fresh mtimes and are caught by the scan.

The log keeps one previous run as `.prev`, so relaunching after a crash no longer
destroys the log of the crash. The kit ships `KenshiCoop.map`, so a crash address in a
player's report resolves to a function.

`[net] bandwidth out=… in=…` appears every 5 s. Nothing measured its own traffic before,
which means any earlier claim about what a channel costs — including the ~40 KB/s figure
in `SyncTuning.h` — was arithmetic rather than measurement. Measured 2026-08-09 in a real
two-client session: **~36 KB/s steady state** with a 216-NPC town loaded, against a link
that moved 19.5 MB of save at ~900 KB/s. Bandwidth is not the constraint; the main
thread is.

## Diagnosing "it desyncs" — read the two [interp] lines FIRST

The first played session (2026-08-09) taught this and it generalises. Symptoms that
look like a replication bug are usually a **frame-starved client**, and the tell is the
asymmetry between the two logs:

| | host | join |
|---|---|---|
| `extrap/lerp` | ~4% | 20%+ |
| `jit` | 7.8 ms | 167–200 ms |
| `delay` | 86 ms | 320–777 ms |

**`extrap/lerp` is the signal; `jit` and `delay` are not.** `jit` measures deviation in
the *sender's* cadence (ring times are send-stamped), so a rotating mid band reads high
by construction and says nothing about this client's frames — `ReplicatorDrive.cpp` says
so in its own comment. `delay` is derived from `jit`. Read the extrapolation share and
treat the other two as context.

A starved client publishes a **lumpy stream**, so the *other* client's copy of its
character accrues gap and hard-snaps, while its own copy of the peer tracks fine. That
is why "his character never lags on my screen, mine lags on his" is a load report, not a
drive bug. Both directions run the same code; only the input quality differs.

**Why one client starves and the other idles:** cell authority splits work by SPACE.
Two players standing in one cell means the host wins it (`[cell] MAP cells=1 slots=1
24,22=0`), so the host authors every NPC and drives none, while the join drives every
NPC and authors none. There is no spatial split to make when both players occupy the
same space.

## Channels added 2026-08-09 (protocol 55 + 56)

Both were "never built" rather than broken, and both were called infeasible
before being checked properly - the same mistake twice in one session.

- **Weather (55).** Kenshi rolls weather per BIOME REGION from a weighted table
  with no exposed seed, so two clients in one biome diverge by construction.
  Host publishes the active region at ~1 Hz, change-gated; identity travels as
  the weather's **GameData stringID** (pointers differ between processes), and
  an id the receiver cannot resolve is DROPPED rather than guessed.
- **Dialogue (56).** A speech bubble spawns only on the machine whose AI ran the
  conversation. Capture hooks `DialogueSpeechBubble::setText`/`setPosition` and
  correlates on the bubble pointer - a workaround for `speechBubbleList` being
  the one dialogue symbol KenshiLib does NOT export. Sends a world position, not
  a speaker hand; the receiver attaches the text to the nearest character and
  drops the line if nothing is within 40 u. Symmetric, fire-and-forget.

**The lesson both taught: `_NV_` wrappers exist only to bypass a VTABLE.**
`KenshiLib::GetRealAddress` is a template over any non-virtual function pointer,
which is how this plugin already resolves `Character::setDestination` and
`GameWorld::getCharactersWithinSphere`. "No `_NV_` in the header" proves nothing
about reachability - check `KenshiLib.lib`'s exported symbols instead
(`strings -a KenshiLib.lib | grep <Class>`). Two subsystems were wrongly written
off on that reasoning before the check was run.

**Trap - `kenshi/Weather.h` cannot be included** in the engine prelude: the dump
defines `class WeatherRegion` in BOTH it and `PhysicsCollection.h`, and
`Weather.h` uses `WeatherRegion`/`Weather`/`Season` before declaring them. The
facade reads through local offset mirrors instead. Minimal re-declarations of
engine classes must sit at **global scope** - `GetRealAddress` resolves through
the mangled name, so a namespace changes the symbol.

## Plan

`docs/ROADMAP.md` is the ordered plan: what is left, in what order, and what has
been closed with a decision so it is not reopened. `HANDOFF.md`, when it exists,
is disposable session state - half-built things and machine state outside the
repo.

**The first two-client session ran 2026-08-09** and immediately produced four real
defects that unit tests, a launched game and log evidence had all missed for the
project's whole history — see `docs/ROADMAP.md`. What is still unproven is a session
played by two *humans*; one person driving both windows does not exercise the second
machine, the Steam P2P tunnel, or a real network path.

## Known open work

`docs/PROTOCOL_HISTORY.md` reconstructs the wire versions and marks what is evidenced
versus inferred — read it before any protocol change. A 12-dimension audit (2026-08-08)
produced a ranked backlog; the significant unfixed items are:

- **The 20 Hz near band has no change gate** — a stationary NPC costs exactly what a
  sprinting one does. The largest remaining bandwidth win, and deliberately not taken
  blind: it changes what the peer receives for a body that is not moving, which feeds
  the interpolator, the drive and the authority dwell at once. Judge it against the
  bandwidth telemetry from a real session first.
- **`applyTargets` (~1700 lines) is still one function.** The extraction is safe in
  principle but needs a test that can see the sync layer, which does not exist yet.
- **A stub-engine harness** would make most of `sync/` testable headlessly; the audit
  rated it the largest available coverage win. The cheap first step is done —
  `netlinktest` links the real `NetLink.cpp` and pins the receive-side bounds checks —
  and the pattern to extend toward `sync/` is in `scripts/linux/build_netlinktest.sh`.
- **The capability report does not gate.** `configureReplicator` warns when an enabled
  feature's `CAP_*` did not resolve but does not disable it, because `capEvaluate` is
  fail-closed and could turn off something that works. Needs one real sighting of a
  `[caps] WARNING` on a healthy install before it becomes a gate.
- **The Steam friend-picker is unreachable dead code**, but `SteamInvite.cpp` also holds
  `onP2PSessionRequest -> steamp2p::accept()` and the only `SteamAPI_RunCallbacks` pump in
  the process — both load-bearing for every Steam connection. Extract those into
  `SteamP2P` and verify on two machines before deleting anything.
- **`CATCHUP_K` has no stability headroom.** `K*dt = 2.0 x 0.5 s = exactly 1.0` on the mid
  band; a further band widening that slows the re-issue cadence makes the catch-up gain
  unstable and the copy walks through the source and back.
- **`gateShouldSend`'s effective resend interval is `max(minSendMs, resendMs)`** — the
  throttle is evaluated before the resend, so a channel configured the other way round
  silently loses its safety-resend cadence. Latent: only money passes a non-zero
  `minSendMs` today.

## Diagnosing a live session

Logs are `<Kenshi>/KenshiCoop_*.log`, one per client, flushed per line. The `kenshicoop-logs`
skill has the field glossary and the parse snippets; start there rather than reading raw.

The filename follows the role actually chosen in the F2 panel: pressing Connect renames
the log if the panel role differs from the configured one, and logs the rename. (It used
to reflect only the CONFIGURED role, so a `_host.log` could be the join's. If you are
reading an older log, confirm the role with `[net] connected to host; sent HELLO`.)

Lines worth knowing about, all added after the first play session:

| Line | Means |
|---|---|
| `[caps] WARNING <x>Sync is ON but ...` | this build could not resolve that engine capability; the feature will no-op silently. `[caps] 0 of 17` is the healthy case |
| `[ko] RELEASE hand=...` | a KO latch was cleared by the owner's stream reporting the body upright, because no `EVT_REVIVE` arrived. Climbing `koRel` in `[trust]` means revives are going missing |
| `[trust] ... koRel=N koExp=N` | `koExp` counts latched entries dropped because the owner stopped streaming them for ten minutes |
| `[build] MINT-RETRY OK/GIVEUP` | a building the peer placed could not be minted here at first. GIVEUP means it will never appear — the sid and key are on the line |
| `[authority] suppress MIGRATE REFUSED` | a hidden body's hand changed but its template sid did not match, so the hide was dropped rather than moved onto a possibly-different body |
| `unknown packet type=N` | version skew or a corrupted stream. There is no other symptom for either |
| `[role] panel role is X; log renamed to ...` | see above |

## Secrets, machine context

Machine-level context (GPU, Proton, Steam paths) is in `~/CLAUDE.md`. Kenshi lives at
`~/.local/share/Steam/steamapps/common/Kenshi`; the mod goes in its `mods/KenshiCoop/`.
