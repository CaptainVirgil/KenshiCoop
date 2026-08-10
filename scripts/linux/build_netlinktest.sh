#!/usr/bin/env bash
# Builds dist/netlinktest.exe with the same v100 toolchain, through Wine.
#
# Links the REAL src/plugin/net/NetLink.cpp receive path - plus its two link
# mates, SteamP2P.cpp (resolves steam_api64.dll dynamically: no Steam SDK, fails
# gracefully without Steam) and CoopLog.cpp (no-ops while uninitialised) - and
# drives it over a tunneltest-style in-memory socket-hook pipe. Pins the
# receive-side bounds checks: the pre-handshake gate, the entity-batch len/count
# gates (the count cap was falsified against the unclamped receiver before the
# clamp shipped), the existing NPC-census cap, and unknown-type survival.
# No game, no KenshiLib, no Steam, no real sockets.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"
source "$TOOLCHAIN/vcenv.sh"

VC="$TOOLCHAIN/VS10/VC"
SDK="$TOOLCHAIN/SDK"
ENET="$REPO/third_party/enet/enet"
# Deliberately NO KenshiLib on the include path: NetLink must not need it, and
# this build proves it stays that way. NetLink.h's "../core/Inbound.h" and
# "../../netproto/Wire.h" resolve relative to the including file, so the four
# dirs below are the whole include story.
export INCLUDE="$(winpath "$VC/include");$(winpath "$SDK/Include");$(winpath "$REPO/third_party/vc10_compat");$(winpath "$ENET/include")"
export LIB="$(winpath "$VC/lib/amd64");$(winpath "$SDK/Lib/x64")"

[ -f "$ENET/host.c" ] || {
  echo "ENet sources missing at $ENET - see third_party/enet/README.md for the pinned fetch + patch recipe" >&2
  exit 1
}

mkdir -p "$REPO/dist" "$REPO/build/netlinktest"

echo "=== Building netlinktest.exe (Release|x64, v100 via Wine) ==="
# WIN32_LEAN_AND_MEAN matches the plugin's own compile of NetLink.cpp (see the
# vcxproj): NetLink.h and SteamP2P.cpp include <windows.h> BEFORE <enet/enet.h>,
# and without it windows.h drags in winsock.h, which collides with ENet's
# winsock2.h. NDEBUG matches the shipping Release compile of the same sources.
vcl /nologo /O2 /EHsc /W3 /DWIN32 /DWIN32_LEAN_AND_MEAN /DNDEBUG \
    /Fo"$(winpath "$REPO/build/netlinktest/")" \
    /Fe"$(winpath "$REPO/dist/netlinktest.exe")" \
    "$(winpath "$REPO/src/netlinktest/main.cpp")" \
    "$(winpath "$REPO/src/plugin/net/NetLink.cpp")" \
    "$(winpath "$REPO/src/plugin/net/SteamP2P.cpp")" \
    "$(winpath "$REPO/src/plugin/CoopLog.cpp")" \
    "$(winpath "$ENET/callbacks.c")" "$(winpath "$ENET/compress.c")" \
    "$(winpath "$ENET/host.c")" "$(winpath "$ENET/list.c")" \
    "$(winpath "$ENET/packet.c")" "$(winpath "$ENET/peer.c")" \
    "$(winpath "$ENET/protocol.c")" "$(winpath "$ENET/win32.c")" \
    ws2_32.lib winmm.lib

[ -f "$REPO/dist/netlinktest.exe" ] || { echo "netlinktest build FAILED" >&2; exit 1; }
echo "netlinktest built: $REPO/dist/netlinktest.exe"
