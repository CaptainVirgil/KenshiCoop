#!/usr/bin/env bash
# VC++ 2010 (v100) x64 toolchain, driven from Linux through Wine.
#
# Extracted from the Windows SDK 7.1 ISO (GRMSDKX_EN_DVD.iso) with `msiexec /a`
# administrative installs -- no installer logic, no registry keys, no VS2010.
# Source this, then call `vcl` (cl.exe) and `vclink` (link.exe).

TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"
WINE_BIN="${WINE_BIN:-$HOME/.local/share/lutris/runners/wine/wine-staging-11.2-x86_64/bin}"

export WINEPREFIX="$TOOLCHAIN/prefix"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="mscoree,mshtml="

VC="$TOOLCHAIN/VS10/VC"
SDK="$TOOLCHAIN/SDK"

# Translate a Linux path to the Z: drive Wine maps onto /.
winpath() { printf 'Z:%s' "$(printf '%s' "$1" | tr '/' '\\')"; }

# The compiler resolves c1.dll/c1xx.dll/c2.dll and mspdb100.dll next to itself,
# so bin/amd64 must be on the Windows PATH, not just used as an absolute exe.
export WINEPATH="$(winpath "$VC/bin/amd64");$(winpath "$SDK/Bin/x64");$(winpath "$SDK/Bin")"

vc_include() {
  local repo="$1"
  local kl="$repo/third_party/KenshiLib_deps"
  # KenshiLib/Include/kenshi is on the path DELIBERATELY, in addition to
  # KenshiLib/Include. From 0.4.0 the library's own headers moved into
  # subdirectories (kenshi/combat/, kenshi/AI/, kenshi/Animation/) but still
  # include their siblings as "Enums.h", "util/lektor.h" - paths relative to
  # kenshi/, not to the including file. Without this, kenshi/combat/CombatClass.h
  # cannot find kenshi/Enums.h and seven translation units fail.
  printf '%s;%s;%s;%s;%s;%s;%s;%s' \
    "$(winpath "$VC/include")" \
    "$(winpath "$SDK/Include")" \
    "$(winpath "$repo/third_party/vc10_compat")" \
    "$(winpath "$kl/KenshiLib/Include")" \
    "$(winpath "$kl/KenshiLib/Include/kenshi")" \
    "$(winpath "$kl/KenshiLib/Include/ogre")" \
    "$(winpath "$kl/boost_1_60_0")" \
    "$(winpath "$repo/third_party/enet/enet/include")"
}

vc_lib() {
  local repo="$1"
  printf '%s;%s;%s' \
    "$(winpath "$VC/lib/amd64")" \
    "$(winpath "$SDK/Lib/x64")" \
    "$(winpath "$repo/third_party/KenshiLib_deps/KenshiLib/Libraries")"
}

vcl()    { "$WINE_BIN/wine" "$VC/bin/amd64/cl.exe" "$@"; }
vclink() { "$WINE_BIN/wine" "$VC/bin/amd64/link.exe" "$@"; }
