---
name: kenshicoop-build
description: Build, package and install KenshiCoop.dll on Linux (Wine-hosted VC++2010) or native Windows. Use when compiling the plugin, bootstrapping the toolchain, updating the vendored KenshiLib deps, packaging a kit for both players, or when a build fails with missing headers, LNK2005, C2011 enum redefinition, or C2027 on deque.
---

# Building KenshiCoop

**VC++ 2010 RTM (v100), x64. No substitutes** — but not for the reason this file used to
give, and the wrong reason is dangerous because anyone who checks it concludes the rule is
stale.

`KenshiLib.lib` is a pure import library: no `.drectve`, no `FAILIFMISMATCH`, no
`DEFAULTLIB`, so there is no `detect_mismatch` record and no LNK2038 path. MSVC mangled
names are toolset-invariant. **A modern-MSVC build links cleanly.** It then reads the game
wrong: `sizeof(std::string)` is 40 bytes under VC10 and 32 under VS2015+, and the vendored
headers pin members at literal game offsets assuming 40 — `kenshi/Character.h` has `sex` at
0x610 and `nameTag` at 0x638, and `movement` (0x640) / `body` (0x648) follow it and are
dereferenced directly. Under a 32-byte string every one of those reads lands 8 bytes early.
The DLL also exchanges `std::string` with **Kenshi.exe itself** across MSVCP100/MSVCR100,
so the coupling is to the game binary, not to `KenshiLib.lib`.

mingw fails loudly at link (Itanium mangling never matches). Modern MSVC fails **silently
at runtime**, which is worse than a link error.

Zero-build check that settles it:
`ls ~/.local/share/kenshicoop-toolchain/VS10/VC/include/{cstdint,memory,thread}` — the
first two exist, the third does not. `<cstdint>` and smart pointers are *available*; the
real bans are `<thread>`/`<mutex>`/`<atomic>` and `enum class`.

`src/plugin/KenshiCoop.vcxproj` is authoritative for sources, per-configuration exclusions
and flags. Both build paths read from it; keep them in sync.

## Configurations

| Config | Contains | Use for |
|---|---|---|
| `Release` | player build; `test/Scenario*.cpp` and `game/EngineProbe.cpp` excluded | anything handed to a player |
| `Harness` (default) | Release plus the scenario runner and probes | scenario/regression runs |
| ~~`Debug`~~ | **does not build — do not use** | — |

`Debug` is broken three independent ways and is not worth fixing: the vcxproj has no
`<WholeProgramOptimization>` for it (so no `/GL`, so the loadability gate deletes the DLL),
two TUs fail outright with C2712, and `/D_DEBUG` flips `_ITERATOR_DEBUG_LEVEL` 0→2, which
adds 8 bytes to every `std::string`/`vector`/`deque` member — exactly the offsets the
vendored headers pin to literal game addresses. **Do not "fix" it by adding
`WholeProgramOptimization` to the Debug PropertyGroup**: that clears the gate and leaves
the offset mismatch, i.e. a DLL that loads and reads the game wrong. Use `Harness`.

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

Needs Windows SDK 7.1 for the v100 compiler and VS2022 Build Tools for MSBuild.
(KB2519277 is **no longer obtainable** — Microsoft took it offline. `setup_toolchain.sh`
patches the one `<deque>` defect it fixed; see the failure table below.)
and the same two dependency steps (`git lfs pull`, then the header patch — the patch is a
no-op on a tree already patched from Linux).

## Dependencies

`third_party/KenshiLib_deps` is a separate checkout, **pinned to `b566d74`**
(KenshiLib 0.4.0 — the version RE_Kenshi actually ships), gitignored.
`third_party/enet/enet` is a clone of lsalzman/enet with the two patches in
`third_party/enet/patches/` applied.

This used to be pinned at `e75769b` with the note *"v0.4.0 breaks the plugin"*. It did
not: the move of `kenshi/CombatClass.h` under `kenshi/combat/` cost one include line plus
one include-path entry, and the "duplicate `enum BuildingDesignation`" predates the pin
entirely — `patch_vendored_headers.sh` has always deduped it (`git -C
third_party/KenshiLib_deps grep -c 'enum BuildingDesignation' <any ref>` returns 2 at
every revision). Building against v0.1 while running 0.4.0 was the actual risk.

After any deps checkout, **both** `fetch_lfs.sh` and `patch_vendored_headers.sh` must
re-run.

## Failure table

| Symptom | Cause | Fix |
|---|---|---|
| `Cannot open include file: 'boost/unordered_map.hpp'` | `boost.zip` is still an LFS pointer, or was never unzipped | `scripts/linux/fetch_lfs.sh third_party/KenshiLib_deps` then `unzip -o boost.zip` in `boost_1_60_0/` |
| `Cannot open include file: 'kenshi/CombatClass.h'` | deps are on the OLD v0.1 pin | `git -C third_party/KenshiLib_deps checkout b566d74`, then both post-checkout steps. Do NOT go back to `e75769b` — that is the stale pin, and the header lives at `kenshi/combat/CombatClass.h` from 0.4.0 on. |
| `Cannot open include file: 'Enums.h'` from inside `kenshi/combat/CombatClass.h` | `KenshiLib/Include/kenshi` is missing from the include path | 0.4.0 headers sit in subdirectories but include siblings relative to `kenshi/`. All four entry points must carry it: `vcenv.sh`, `build_plugin_direct.ps1`, `build_plugin.cmd`, `KenshiCoop.vcxproj`. |
| `error C2011: 'BuildingDesignation' : 'enum' type redefinition` | header patch not applied | `scripts/linux/patch_vendored_headers.sh` |
| Same, or every type in a header defined twice, only under Wine | `#pragma once` does not dedup — Wine returns the same header as `Z:\...\Foo\Bar.h` and `z:\...\foo/bar.h` | same patch; it converts to include guards |
| `error C2027: use of undefined type 'CraftingItem'` in `<deque>` | SDK 7.1's RTM CRT forces `sizeof(value_type)` at class scope via `_EEM_DS`/`_EEN_DS`; `CraftingBuilding` holds a `std::deque<CraftingItem>` with the type forward-declared | `setup_toolchain.sh` patches it out (equivalent to KB2519277, which Microsoft has taken offline) |
| `error LNK2005: ... already defined` on ENet symbols | stale objects from an older naming scheme in `build/*/obj` | `rm -rf build/`; the link list is explicit, not a glob, so this should not recur |
| Windows and Linux Release DLLs differ in content | the Linux script is not honouring `<ExcludedFromBuild>` | fix the vcxproj parsing, not the symptom |

| Assertion box at launch: `Incorrect address in KenshiLib::GetRealAddress()`, `Function address: KenshiCoop.dll+0x...` | The DLL was built without `/GL` + `/LTCG`, so KenshiLib's member-function stubs became local functions and `&GameWorld::_NV_mainLoop_GPUSensitiveStuff` points into our own module. | The vcxproj asks for `<WholeProgramOptimization>` in its `Label="Configuration"` `<PropertyGroup>` — outside the `<ItemDefinitionGroup>` every other flag lives in. Both scripts read it from the project; if you are running a modified script, make sure it still does. Verify without launching: `grep _NV_mainLoop_GPUSensitiveStuff build/Release/KenshiCoop.map` must show `__imp_...`, an import, not a local definition. |

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

**Link in source order.** `/OPT:ICF` folds identical COMDATs and the result depends on the
order the linker sees objects, so linking a directory listing (alphabetical) instead of the
vcxproj's source order produces a subtly different `.text`. Both build scripts link in
source order for this reason — it is what makes the two artifacts comparable at all.

## Verifying the two builds agree

**The last proof is stale.** It held at `5a7902d` (2026-08-08); since then the generated
`DepsPin.h` stamp and the map-based loadability check landed on the **Linux script only**
(`grep -c DEPS_PIN scripts/build_plugin_direct.ps1` → 0, against 7 for the shell script),
and the stamp changes a string literal's length. No Windows-built DLL has ever shipped.
Port those two features across before trusting a fresh comparison — roadmap item 46.

The procedure itself is still correct:

```bash
# same commit both sides, then compare PE sections
python3 - <<'EOF'
import struct
def info(p):
    d=open(p,'rb').read(); pe=struct.unpack_from('<I',d,0x3c)[0]
    n=struct.unpack_from('<H',d,pe+6)[0]; o=pe+24+struct.unpack_from('<H',d,pe+20)[0]
    return [(d[o+i*40:o+i*40+8].rstrip(b'\x00').decode(),
             struct.unpack_from('<I',d,o+i*40+8)[0]) for i in range(n)]
for p in ("KenshiCoop-windows.dll","build/Release/KenshiCoop.dll"):
    print(p, info(p))
EOF
```

Expected result: `.text`, `.data`, `.pdata`, `.reloc` **identical**; `.rdata` differs by the
aligned length of the embedded PDB path (the two builds sit at different absolute paths).
Anything else is a real divergence — check the flags and the link order first.

Do not compare hashes: the PDB path and build timestamp guarantee they differ.

## Building on Windows without owning a Windows machine

A throwaway VM is enough, and the whole loop is scriptable:

```bash
docker run -d --name kenshicoop-win --device=/dev/kvm --device=/dev/net/tun \
  --cap-add NET_ADMIN -p 8006:8006 -p 3389:3389 \
  -e VERSION=11 -e RAM_SIZE=12G -e CPU_CORES=6 -e DISK_SIZE=96G \
  -v <storage>:/storage -v <data>:/data -v <oem>:/oem dockurr/windows
```

Traps worth knowing, all of which cost a reinstall to learn:

- **dockur bakes `/oem` into the install ISO.** Preserving `win11x64.iso` across a rebuild
  to save the download also preserves the *old* OEM scripts, so fixes silently do not run.
  Wipe storage entirely when the OEM changes.
- `/oem/install.bat` runs **once**, on first boot. Install a small polling task that watches
  the shared folder for a script and runs it, or every iteration means reinstalling Windows.
- Hand the guest **archives, not trees**: the deps are ~15k small files (Boost) and copying
  them file-by-file over the share takes longer than the rest of the build.
- `ZipFile::ExtractToDirectory` throws on a collision; `Expand-Archive -Force` overwrites.
- `msiexec /a` marks everything read-only, which makes the `<deque>` patch fail with access
  denied.

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
