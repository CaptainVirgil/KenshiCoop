#!/usr/bin/env bash
# Build KenshiCoop.dll from Linux, driving the VC++ 2010 (v100) x64 toolchain
# through Wine. Companion to scripts/build_plugin.cmd, which needs Windows.
#
# The toolchain is extracted from the Windows SDK 7.1 ISO -- see
# scripts/linux/setup_toolchain.sh. Nothing is installed system-wide and no
# sudo is required; Wine comes from the Lutris runner directory.
#
#   Usage: scripts/linux/build_plugin.sh [Harness|Release|Debug]
set -euo pipefail

CONFIG="${1:-Harness}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"

source "$TOOLCHAIN/vcenv.sh"

OUT="$REPO/build/$CONFIG"
OBJ="$OUT/obj"
mkdir -p "$OBJ"

case "$CONFIG" in
  Release) DEFS="/DNDEBUG /DKENSHICOOP_EXPORTS"; OPT="/O2 /MD" ;;
  Harness) DEFS="/DNDEBUG /DKENSHICOOP_EXPORTS /DKENSHICOOP_HARNESS"; OPT="/O2 /MD" ;;
  Debug)   DEFS="/D_DEBUG /DKENSHICOOP_EXPORTS /DKENSHICOOP_HARNESS"; OPT="/Od /MDd" ;;
  *) echo "unknown config: $CONFIG (Harness|Release|Debug)" >&2; exit 2 ;;
esac
# CharacterSet=Unicode in the vcxproj: KenshiLib headers pass L"" literals to
# Win32 calls, so the W variants must be selected.
DEFS="$DEFS /D_WINDOWS /D_USRDLL /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE"

export INCLUDE="$(vc_include "$REPO")"
export LIB="$(vc_lib "$REPO")"

# Source set mirrors the <ClCompile> list in src/plugin/KenshiCoop.vcxproj.
# ENet's unix.c is intentionally excluded (Windows socket backend only).
mapfile -t SOURCES < <(
  grep -oP '(?<=<ClCompile Include=")[^"]+' "$REPO/src/plugin/KenshiCoop.vcxproj" |
    tr '\\' '/'
)

echo "=== KenshiCoop.dll ($CONFIG|x64, v100 via Wine) - ${#SOURCES[@]} translation units ==="

compile_one() {
  local src="$1"
  local abs="$REPO/src/plugin/$src"
  # Flatten the path into the object name, dropping the leading ../.. of the
  # vendored ENet sources so nothing lands as a dotfile.
  local obj="$OBJ/$(echo "${src%.*}" | tr '/' '_' | sed 's/^[._]*//').obj"
  # /EHsc: the plugin mixes C++ objects with __try frames; /W3 matches Level3.
  vcl /nologo /c /EHsc /W3 /Gy /Oi $OPT $DEFS \
      /Fo"$(winpath "$obj")" "$(winpath "$abs")" 2>&1 |
    grep -vE '^[A-Za-z0-9_./\\-]+\.(cpp|c)$' || true
  [ -f "$obj" ] || { echo "FAILED: $src" >&2; return 1; }
}
export -f compile_one winpath vcl
export REPO OBJ OPT DEFS VC WINE_BIN

printf '%s\n' "${SOURCES[@]}" |
  xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {}

echo "=== linking ==="
vclink /nologo /DLL /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /DEBUG \
  /OUT:"$(winpath "$OUT/KenshiCoop.dll")" \
  /PDB:"$(winpath "$OUT/KenshiCoop.pdb")" \
  /MAP:"$(winpath "$OUT/KenshiCoop.map")" \
  "$(winpath "$OBJ")"\\*.obj \
  kenshilib.lib OgreMain_x64.lib MyGUIEngine_x64.lib ws2_32.lib winmm.lib \
  user32.lib kernel32.lib advapi32.lib shell32.lib ole32.lib

ls -la "$OUT/KenshiCoop.dll"
sha256sum "$OUT/KenshiCoop.dll"
