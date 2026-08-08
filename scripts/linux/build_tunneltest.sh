#!/usr/bin/env bash
# Linux twin of scripts/build_tunneltest.cmd: builds dist/tunneltest.exe with the
# same v100 toolchain, through Wine.
#
# Proves the ENet-over-socket-hooks tunnel survives the Steam P2P constraints --
# a 1200-byte datagram ceiling and deterministic loss -- in one process, with a
# fake in-memory pipe. No game, no Steam, no real sockets, so it runs anywhere.
# It cannot prove NAT punch or relay brokering; nothing offline can.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"
source "$TOOLCHAIN/vcenv.sh"

VC="$TOOLCHAIN/VS10/VC"
SDK="$TOOLCHAIN/SDK"
ENET="$REPO/third_party/enet/enet"
export INCLUDE="$(winpath "$VC/include");$(winpath "$SDK/Include");$(winpath "$REPO/third_party/vc10_compat");$(winpath "$ENET/include")"
export LIB="$(winpath "$VC/lib/amd64");$(winpath "$SDK/Lib/x64")"

[ -f "$ENET/host.c" ] || {
  echo "ENet sources missing at $ENET - see CLAUDE.md for the fetch + patch steps" >&2
  exit 1
}

mkdir -p "$REPO/dist" "$REPO/build/tunneltest"

echo "=== Building tunneltest.exe (Release|x64, v100 via Wine) ==="
vcl /nologo /O2 /EHsc /W3 /DWIN32 \
    /Fo"$(winpath "$REPO/build/tunneltest/")" \
    /Fe"$(winpath "$REPO/dist/tunneltest.exe")" \
    "$(winpath "$REPO/src/tunneltest/main.cpp")" \
    "$(winpath "$ENET/callbacks.c")" "$(winpath "$ENET/compress.c")" \
    "$(winpath "$ENET/host.c")" "$(winpath "$ENET/list.c")" \
    "$(winpath "$ENET/packet.c")" "$(winpath "$ENET/peer.c")" \
    "$(winpath "$ENET/protocol.c")" "$(winpath "$ENET/win32.c")" \
    ws2_32.lib winmm.lib

[ -f "$REPO/dist/tunneltest.exe" ] || { echo "tunneltest build FAILED" >&2; exit 1; }
echo "tunneltest built: $REPO/dist/tunneltest.exe"
