#!/usr/bin/env bash
# Builds and RUNS dist/drivetest.exe with the same v100 toolchain, through Wine.
# Exit code == suite result (0 = all checks passed).
#
# The first slice of the stub-engine sync harness (ROADMAP Phase 2 item 27):
# compiles the REAL src/plugin/sync/ReplicatorDrive.cpp AND ReplicatorCore.cpp
# (ctor, ingest, lifecycle audit) plus the real Interp.cpp / CoopLog.cpp, and
# links them against a FAKE coop::engine (src/drivetest/EngineFakes.cpp) and
# the three cross-TU Replicator member stubs (src/drivetest/ReplicatorStubs.cpp)
# - so receiver-drive behaviour (tier classification, mid-rest release,
# walk-hold) runs headless, no game, no KenshiLib, no Steam, no sockets.
#
# Deliberately NO KenshiLib on the include path: the sync drive layer must not
# need it (its engine access all funnels through the coop::engine facade), and
# this build proves it stays that way. ENet is on the path only because
# Replicator.h includes NetLink.h for the class declaration - no ENet symbol is
# ever linked.
#
# Every compile is niced: this harness is expected to run while Kenshi is.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"
source "$TOOLCHAIN/vcenv.sh"

VC="$TOOLCHAIN/VS10/VC"
SDK="$TOOLCHAIN/SDK"
ENET="$REPO/third_party/enet/enet"
export INCLUDE="$(winpath "$VC/include");$(winpath "$SDK/Include");$(winpath "$REPO/third_party/vc10_compat");$(winpath "$ENET/include")"
export LIB="$(winpath "$VC/lib/amd64");$(winpath "$SDK/Lib/x64")"

[ -d "$ENET/include" ] || {
  echo "ENet headers missing at $ENET/include - see third_party/enet/README.md for the pinned fetch + patch recipe" >&2
  exit 1
}

mkdir -p "$REPO/dist" "$REPO/build/drivetest"
rm -f "$REPO/dist/drivetest.exe"

echo "=== Building drivetest.exe (Release|x64, v100 via Wine) ==="
# Same defines as the shipping Release compile of these sources (see the
# vcxproj / build_netlinktest.sh): WIN32_LEAN_AND_MEAN because NetLink.h pulls
# <windows.h> before ENet's winsock2.h; NDEBUG to match Release.
# (vcl is a shell function; nice needs the real binary, so invoke wine+cl
# directly - same command vcenv.sh's vcl wraps.)
nice -n 19 "$WINE_BIN/wine" "$VC/bin/amd64/cl.exe" \
    /nologo /O2 /EHsc /W3 /DWIN32 /DWIN32_LEAN_AND_MEAN /DNDEBUG \
    /D_CRT_SECURE_NO_WARNINGS \
    /Fo"$(winpath "$REPO/build/drivetest/")" \
    /Fe"$(winpath "$REPO/dist/drivetest.exe")" \
    "$(winpath "$REPO/src/drivetest/main.cpp")" \
    "$(winpath "$REPO/src/drivetest/EngineFakes.cpp")" \
    "$(winpath "$REPO/src/drivetest/ReplicatorStubs.cpp")" \
    "$(winpath "$REPO/src/plugin/sync/ReplicatorDrive.cpp")" \
    "$(winpath "$REPO/src/plugin/sync/ReplicatorCore.cpp")" \
    "$(winpath "$REPO/src/plugin/sync/Interp.cpp")" \
    "$(winpath "$REPO/src/plugin/CoopLog.cpp")"

[ -f "$REPO/dist/drivetest.exe" ] || { echo "drivetest build FAILED" >&2; exit 1; }
echo "drivetest built: $REPO/dist/drivetest.exe"

echo "=== Running drivetest (Wine, ~20 s of real-time pacing) ==="
# Run from build/drivetest so drivetest.log lands there, not in the repo root.
cd "$REPO/build/drivetest"
set +e
nice -n 19 "$WINE_BIN/wine" "$(winpath "$REPO/dist/drivetest.exe")"
rc=$?
echo "drivetest exit code: $rc (log: $REPO/build/drivetest/drivetest.log)"
exit $rc
