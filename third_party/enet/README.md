# ENet (vendored dependency)

KenshiCoop uses [ENet](http://enet.bespin.org/) for UDP networking (reliable +
unreliable channels). ENet is a small, portable C library that compiles cleanly
under both the VS2010 (v100) plugin toolchain and modern compilers.

The source is not checked into this repo (`third_party/enet/enet/` is
gitignored). It is **pinned**: fetch exactly this revision, then apply both
patches.

## Fetch + patch (the recipe)

```bash
git clone https://github.com/lsalzman/enet.git third_party/enet/enet
git -C third_party/enet/enet checkout 5a9c537fd464b3c6d3c55e1d3bd47588faf71b42
# from the repo root:
git apply third_party/enet/patches/0001-enet-c89-for-loops.patch
git apply third_party/enet/patches/0002-enet-socket-hooks.patch
```

## Why the pin matters

`5a9c537f` is upstream master's tip as of 2026-06-23, **17 commits past the
v1.3.18 release**. Do NOT substitute the v1.3.18 release tarball: those commits
are mostly hardening in the code that parses untrusted packets — bounds checks
in the unreliable-fragment handler (`657eaf9`), underflow clamps on
`totalWaitingData` (`1e80a78`, `a52811e`), NULL-safety in `enet_packet_resize`
(`0b924c7`), signed-shift UB fixes in the fragment/window arithmetic
(`7c07702`), and a `fragmentOffset` byte-swap fix (`5a9c537`). The release
reproduces a different, less hardened network stack. Version macros cannot tell
the trees apart — upstream leaves `ENET_VERSION_*` at 1.3.18 between releases;
only the sha identifies the checkout.

After fetch + patch, `git status` in the checkout reports
`M include/enet/enet.h, M protocol.c, M win32.c` — that is the two patches, and
it is the healthy state. The plugin build stamps
`git describe --always --dirty` of this checkout into the DLL (via
`build/generated/DepsPin.h`, logged as `[host] ENet vendored=...`), so `-dirty`
in the pin is expected for the same reason it is for `KenshiLib_deps`.

Patch 0001 hoists C99 `for`-declarations for the C89-only v100 compiler; patch
0002 adds the `ENetSocketHooks` seam the Steam P2P tunnel and `tunneltest`
depend on. Neither is optional — the plugin does not compile without them.

## Build notes
- The plugin and `tunneltest` compile ENet's `.c` files directly from
  `third_party/enet/enet` (win32 backend only; `unix.c` is deliberately
  absent from every source list). No separate install, no prebuilt `enet.lib`.
- On Windows, ENet also needs `ws2_32.lib` and `winmm.lib`.
