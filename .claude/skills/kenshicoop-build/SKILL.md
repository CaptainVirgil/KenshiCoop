---
name: kenshicoop-build
description: Build, package and install KenshiCoop.dll on Linux (Wine-hosted VC++2010) or native Windows. Use when compiling the plugin, bootstrapping the toolchain, updating the vendored KenshiLib deps, packaging a kit for both players, or when a build fails with missing headers, LNK2005, C2011 enum redefinition, or C2027 on deque.
---

# Building KenshiCoop

KenshiLib exports C++ classes, so the MSVC ABI must match Kenshi's own compiler: **VC++
2010 (v100), x64**. No substitutes — mingw and modern MSVC both produce an ABI that
cannot link against `KenshiLib.lib`.

`src/plugin/KenshiCoop.vcxproj` is authoritative for sources, per-configuration exclusions
and flags. Both build paths read from it; keep them in sync.

## Configurations

| Config | Contains | Use for |
|---|---|---|
| `Release` | player build; `test/Scenario*.cpp` and `game/EngineProbe.cpp` excluded | anything handed to a player |
| `Harness` (default) | Release plus the scenario runner and probes | scenario/regression runs |
| `Debug` | unoptimized, debug CRT | stepping |

## Linux

One-time:

```bash
scripts/linux/setup_toolchain.sh                        # ~570 MB ISO download, ~5 min
scripts/linux/fetch_lfs.sh third_party/KenshiLib_deps
scripts/linux/patch_vendored_headers.sh
```

`setup_toolchain.sh` pulls Microsoft's Windows SDK 7.1 ISO and extracts the compiler with
`msiexec /a` administrative installs — no installer logic, no registry, nothing outside
`~/.local/share/kenshicoop-toolchain`. Wine comes from the Lutris runner directory, so no
sudo and no system packages. It also patches the SDK's `<deque>`; see the failure table.

Per build:

```bash
scripts/linux/build_plugin.sh Release     # -> build/Release/KenshiCoop.dll
```

## Windows

```
scripts\build_plugin.cmd Release
```

Needs Windows SDK 7.1 + KB2519277 for the v100 compiler, VS2022 Build Tools for MSBuild,
and the same two dependency steps (`git lfs pull`, then the header patch — the patch is a
no-op on a tree already patched from Linux).

## Dependencies

`third_party/KenshiLib_deps` is a separate checkout, **pinned to `e75769b`**, gitignored.
`third_party/enet/enet` is a clone of lsalzman/enet with the two patches in
`third_party/enet/patches/` applied.

**KenshiLib v0.4.0 breaks the plugin.** It moved `kenshi/CombatClass.h` under
`kenshi/combat/` and introduced a duplicate `enum BuildingDesignation`. Stay on the pin
unless you are deliberately porting.

After any deps checkout, **both** `fetch_lfs.sh` and `patch_vendored_headers.sh` must
re-run.

## Failure table

| Symptom | Cause | Fix |
|---|---|---|
| `Cannot open include file: 'boost/unordered_map.hpp'` | `boost.zip` is still an LFS pointer, or was never unzipped | `scripts/linux/fetch_lfs.sh third_party/KenshiLib_deps` then `unzip -o boost.zip` in `boost_1_60_0/` |
| `Cannot open include file: 'kenshi/CombatClass.h'` | deps are on v0.4.0, not the pin | `git -C third_party/KenshiLib_deps checkout -f e75769b`, then both post-checkout steps |
| `error C2011: 'BuildingDesignation' : 'enum' type redefinition` | header patch not applied | `scripts/linux/patch_vendored_headers.sh` |
| Same, or every type in a header defined twice, only under Wine | `#pragma once` does not dedup — Wine returns the same header as `Z:\...\Foo\Bar.h` and `z:\...\foo/bar.h` | same patch; it converts to include guards |
| `error C2027: use of undefined type 'CraftingItem'` in `<deque>` | SDK 7.1's RTM CRT forces `sizeof(value_type)` at class scope via `_EEM_DS`/`_EEN_DS`; `CraftingBuilding` holds a `std::deque<CraftingItem>` with the type forward-declared | `setup_toolchain.sh` patches it out (equivalent to KB2519277, which Microsoft has taken offline) |
| `error LNK2005: ... already defined` on ENet symbols | stale objects from an older naming scheme in `build/*/obj` | `rm -rf build/`; the link list is explicit, not a glob, so this should not recur |
| Windows and Linux Release DLLs differ in content | the Linux script is not honouring `<ExcludedFromBuild>` | fix the vcxproj parsing, not the symptom |

## Windows without a VS2010 install

Use `scripts\build_plugin_direct.ps1 -Config Release -Toolchain <root>`, not
`build_plugin.cmd`. Verified in a clean Windows 11 VM.

MSBuild is not an option there: `v100` selection needs the legacy toolset props a genuine
VS2010/SDK 7.1 installer writes under `MSBuild\Microsoft.Cpp\v4.0`, and
`Microsoft.Cpp.WindowsSDK.targets` fails with MSB8036 even when the SDK is physically
present and the version is passed explicitly. The direct script sets `INCLUDE`/`LIB`/`PATH`
and drives `cl.exe`/`link.exe`, reading the source list and per-config exclusions out of
the vcxproj so the project stays authoritative.

Three things a clean Windows machine needs that nothing documents:

| Symptom | Cause | Fix |
|---|---|---|
| `cl.exe` exits `0xC0000135` with no output at all | v100 `cl.exe` imports `MSVCR100.dll`; a clean Windows has no VC++ 2010 runtime. Wine ships a builtin, so Linux never hits it | install the VC++ 2010 **x64** redistributable |
| `error C2027: use of undefined type 'CraftingItem'` | SDK 7.1's RTM `<deque>`; KB2519277 is offline | apply the same `<deque>` patch `setup_toolchain.sh` does |
| `error MSB4019` / `MSB8036` from MSBuild | no real VS2010; `v100` props and SDK registration absent | use `build_plugin_direct.ps1` |

`vswhere` needs `-products *` to see the Build Tools SKU, or it reports no MSBuild at all.

## Packaging a kit for both players

The two players must run the **same build** — `PROTOCOL_VERSION` mismatch is a hard
reject. The kit is the mod folder: `KenshiCoop.dll`, `KenshiCoop.mod`, `RE_Kenshi.json`,
`coop_config.json`.

```bash
OUT=~/Downloads/KenshiCoop-fork-kit
mkdir -p $OUT/KenshiCoop
cp build/Release/KenshiCoop.dll $OUT/KenshiCoop/
cp dist/mods/KenshiCoop/{KenshiCoop.mod,RE_Kenshi.json,coop_config.json} $OUT/KenshiCoop/
(cd ~/Downloads && zip -qr KenshiCoop-fork-kit.zip KenshiCoop-fork-kit/KenshiCoop)
```

Install: quit Kenshi, delete `<Kenshi>/mods/KenshiCoop/`, drop the folder back, relaunch.
Replace the folder rather than merging — a stale DLL is what actually gets loaded.

Kenshi lives at `~/.local/share/Steam/steamapps/common/Kenshi`. RE_Kenshi loads the plugin
via `mods/KenshiCoop/RE_Kenshi.json`; if the panel says the plugin never started, check
`<Kenshi>/RE_Kenshi_log.txt` for `KenshiCoop`.
