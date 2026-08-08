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

# The vcxproj is authoritative for the source list on both build paths, including
# its per-configuration <ExcludedFromBuild> markers -- Release must not contain
# the scenario harness or the EngineProbe detours. ENet's unix.c is absent from
# the project entirely (Windows socket backend only).
mapfile -t SOURCES < <(
  python3 "$REPO/scripts/linux/vcxproj_sources.py" \
    "$REPO/src/plugin/KenshiCoop.vcxproj" "$CONFIG" x64
)
[ "${#SOURCES[@]}" -gt 0 ] || { echo "no sources for $CONFIG" >&2; exit 1; }

echo "=== KenshiCoop.dll ($CONFIG|x64, v100 via Wine) - ${#SOURCES[@]} translation units ==="

obj_for() {
  # Flatten the path into the object name, dropping the leading ../.. of the
  # vendored ENet sources so nothing lands as a dotfile.
  echo "$OBJ/$(echo "${1%.*}" | tr '/' '_' | sed 's/^[._]*//').obj"
}

# Recompile only when the source is newer than its object, unless KC_REBUILD=1.
#
# Deliberately a timestamp check on the .cpp alone and NOT a header dependency
# scan: this codebase's headers are load-bearing (Replicator.h is included by
# nearly everything and changes constantly), so a naive source-only check would
# happily skip a TU that a header change had invalidated. Touching any header
# therefore forces a full rebuild - correctness first, and the win is still real
# for the common case of editing one .cpp.
newest_header_ms() {
  find "$REPO/src" "$REPO/third_party/vc10_compat" -name '*.h' -newer "$1" -print -quit 2>/dev/null
}

compile_one() {
  local src="$1"
  local abs="$REPO/src/plugin/$src"
  local obj="$(obj_for "$src")"

  if [ -z "${KC_REBUILD:-}" ] && [ -f "$obj" ] && [ "$obj" -nt "$abs" ] &&
     [ -z "$(newest_header_ms "$obj")" ]; then
    return 0   # object is newer than the source AND every header
  fi
  # Delete first. The success test below is "does the object exist", and without
  # this a compile that FAILS leaves the previous run's object sitting there and
  # passes it -- so the link succeeds against stale code and verify.sh reports a
  # green gate for a plugin that does not contain the change being tested. The
  # grep that strips cl.exe's filename echo also swallows its exit status, which
  # is why existence is the test at all.
  rm -f "$obj"
  # /EHsc: the plugin mixes C++ objects with __try frames; /W3 matches Level3.
  vcl /nologo /c /EHsc /W3 /Gy /Oi $OPT $DEFS \
      /Fo"$(winpath "$obj")" "$(winpath "$abs")" 2>&1 |
    grep -vE '^[A-Za-z0-9_./\\-]+\.(cpp|c)$' || true
  [ -f "$obj" ] || { echo "FAILED: $src" >&2; return 1; }
}
export -f compile_one obj_for winpath vcl newest_header_ms
export REPO OBJ OPT DEFS VC WINE_BIN

printf '%s\n' "${SOURCES[@]}" |
  xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {}

# Link exactly what this run produced. Globbing the object directory instead
# would sweep up stale objects from an earlier layout and hand the linker two
# definitions of everything (LNK2005).
OBJS=()
for src in "${SOURCES[@]}"; do
  obj="$(obj_for "$src")"
  [ -f "$obj" ] || { echo "missing object for $src -- compile failed" >&2; exit 1; }
  OBJS+=("$(winpath "$obj")")
done

echo "=== linking ${#OBJS[@]} objects ==="
vclink /nologo /DLL /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /DEBUG \
  /OUT:"$(winpath "$OUT/KenshiCoop.dll")" \
  /PDB:"$(winpath "$OUT/KenshiCoop.pdb")" \
  /MAP:"$(winpath "$OUT/KenshiCoop.map")" \
  "${OBJS[@]}" \
  kenshilib.lib OgreMain_x64.lib MyGUIEngine_x64.lib ws2_32.lib winmm.lib \
  user32.lib kernel32.lib advapi32.lib shell32.lib ole32.lib

ls -la "$OUT/KenshiCoop.dll"
sha256sum "$OUT/KenshiCoop.dll"
