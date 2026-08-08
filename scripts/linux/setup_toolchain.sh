#!/usr/bin/env bash
# Bootstrap the VC++ 2010 (v100) x64 toolchain on Linux, for building
# KenshiCoop.dll without Windows.
#
# KenshiLib exports C++ classes, so the MSVC ABI must match Kenshi's own
# compiler -- v100, no substitutes. This script extracts that compiler from the
# Windows SDK 7.1 ISO using `msiexec /a` administrative installs, which unpack
# files without running installer logic or touching a registry. Nothing lands
# outside $TOOLCHAIN, and no sudo is required at any point.
#
# Wine comes from the Lutris runner directory rather than a system package for
# the same reason.
#
#   Usage: scripts/linux/setup_toolchain.sh
#   Then:  scripts/linux/build_plugin.sh Release
set -euo pipefail

TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"
WINE_BIN="${WINE_BIN:-$HOME/.local/share/lutris/runners/wine/wine-staging-11.2-x86_64/bin}"
SDK_ISO_URL="https://download.microsoft.com/download/F/1/0/F10113F5-B750-4969-A255-274341AC6BCE/GRMSDKX_EN_DVD.iso"

for tool in 7z curl python3; do
  command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 1; }
done
[ -x "$WINE_BIN/wine" ] || { echo "no wine at $WINE_BIN (set WINE_BIN)" >&2; exit 1; }

mkdir -p "$TOOLCHAIN"
cd "$TOOLCHAIN"

# ---- 1. Windows SDK 7.1 ISO ---------------------------------------------------
if [ ! -f GRMSDKX_EN_DVD.iso ]; then
  echo "=== downloading Windows SDK 7.1 ISO (~570 MB) ==="
  curl -L --fail -o GRMSDKX_EN_DVD.iso.part "$SDK_ISO_URL"
  mv GRMSDKX_EN_DVD.iso.part GRMSDKX_EN_DVD.iso
fi

if [ ! -d sdk_iso ]; then
  echo "=== extracting ISO ==="
  7z x -y -osdk_iso GRMSDKX_EN_DVD.iso > /dev/null
fi

# ---- 2. administrative installs ----------------------------------------------
export WINEPREFIX="$TOOLCHAIN/prefix"
export WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml="   # no Mono/Gecko install prompts

winpath() { printf 'Z:%s' "$(printf '%s' "$1" | tr '/' '\\')"; }

if [ ! -d "$WINEPREFIX" ]; then
  echo "=== creating wine prefix ==="
  "$WINE_BIN/wineboot" -u > /dev/null 2>&1
fi

extract_msi() {
  local msi="$1" dest="$2"
  [ -d "$dest" ] && return 0
  echo "=== extracting $(basename "$msi") ==="
  mkdir -p "$dest"
  "$WINE_BIN/wine" msiexec /a "$(winpath "$msi")" /qn TARGETDIR="$(winpath "$dest")" > /dev/null 2>&1
}

extract_msi "$TOOLCHAIN/sdk_iso/Setup/vc_stdamd64/vc_stdamd64.msi"           "$TOOLCHAIN/stage"
extract_msi "$TOOLCHAIN/sdk_iso/Setup/vc_stdx86/vc_stdx86.msi"               "$TOOLCHAIN/stage"
extract_msi "$TOOLCHAIN/sdk_iso/Setup/WinSDKBuild_amd64/WinSDKBuild_amd64.msi" "$TOOLCHAIN/stage-sdk"

# ---- 3. merge into a clean tree ----------------------------------------------
# The x86 package carries the headers, CRT sources and cross compilers; the
# amd64 package carries the native x64 bin/ and lib/. Merge x86 first, then
# overlay amd64 without clobbering.
if [ ! -d "$TOOLCHAIN/VS10" ]; then
  echo "=== merging VC10 tree ==="
  mkdir -p "$TOOLCHAIN/VS10"
  cp -r "$TOOLCHAIN/stage/Program Files/Microsoft Visual Studio 10.0/VC" "$TOOLCHAIN/VS10/VC"
  cp -rn "$TOOLCHAIN/stage/Program Files(64)/Microsoft Visual Studio 10.0/VC/." "$TOOLCHAIN/VS10/VC/" 2>/dev/null || true
fi
[ -d "$TOOLCHAIN/SDK" ] || cp -r "$TOOLCHAIN/stage-sdk/Program Files/Microsoft SDKs/Windows/v7.1" "$TOOLCHAIN/SDK"

[ -f "$TOOLCHAIN/VS10/VC/bin/amd64/cl.exe" ] || { echo "cl.exe missing after merge" >&2; exit 1; }

# ---- 4. CRT header fix --------------------------------------------------------
# The SDK 7.1 ISO ships the VC10 RTM CRT. Its <deque> declares two
# debugger-visualizer helpers at class scope:
#
#     static const int _EEM_DS = _DEQUESIZ;
#     enum {_EEN_DS = _DEQUESIZ};
#
# Both expand to sizeof(value_type), which is evaluated eagerly when the class
# is declared -- so a `std::deque<T>` member with T incomplete is a hard error
# (C2027). KenshiLib has exactly that: CraftingBuilding holds a
# std::deque<CraftingItem> while only forward-declaring CraftingItem.
#
# The VC2010 SP1 compiler update (KB2519277) is the blessed fix, but Microsoft
# has taken it offline. Dropping the two helpers is equivalent for codegen: they
# are static/enum constants with no storage and no effect on layout or ABI, used
# only by the debugger's expression evaluator.
DEQUE="$TOOLCHAIN/VS10/VC/include/deque"
if grep -q '_EEM_DS' "$DEQUE" && ! grep -q 'removed: debugger-visualizer' "$DEQUE"; then
  echo "=== patching <deque> (eager sizeof helpers) ==="
  cp "$DEQUE" "$DEQUE.orig"
  python3 - "$DEQUE" <<'PY'
import sys
path = sys.argv[1]
with open(path, encoding='utf-8', errors='surrogateescape') as fh:
    text = fh.read()
old = ("\tstatic const int _EEM_DS = _DEQUESIZ;\n"
       "\tenum {_EEN_DS = _DEQUESIZ};\t// helper for expression evaluator\n")
new = ("\t// _EEM_DS/_EEN_DS removed: debugger-visualizer helpers that force\n"
       "\t// sizeof(value_type) at class scope, which breaks deque<T> members\n"
       "\t// declared with T incomplete. See scripts/linux/setup_toolchain.sh.\n")
if old not in text:
    sys.exit("expected _EEM_DS/_EEN_DS block not found in <deque>")
with open(path, 'w', encoding='utf-8', errors='surrogateescape') as fh:
    fh.write(text.replace(old, new))
PY
fi

# ---- 5. env script ------------------------------------------------------------
cp "$(dirname "${BASH_SOURCE[0]}")/vcenv.sh" "$TOOLCHAIN/vcenv.sh"

cat <<EOF

toolchain ready at $TOOLCHAIN
  compiler : $("$WINE_BIN/wine" "$TOOLCHAIN/VS10/VC/bin/amd64/cl.exe" 2>&1 | head -1 | tr -d '\r')
  next     : scripts/linux/fetch_lfs.sh third_party/KenshiLib_deps
             scripts/linux/patch_vendored_headers.sh
             scripts/linux/build_plugin.sh Release
EOF
