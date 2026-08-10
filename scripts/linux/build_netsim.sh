#!/usr/bin/env bash
# Linux twin of scripts/build_netsim.cmd: builds dist/netsim.exe with the same
# v100 toolchain, through Wine.
#
# netsim is a UDP relay proxy that applies delay/jitter/loss to every datagram in
# both directions, BELOW ENet -- so a WAN-variant run exercises real
# retransmission behaviour rather than a simulation of it. It is the only
# reproducer in the tree for the bug class that only appears on a lossy link, and
# it had no Linux build at all: reproducing a WAN bug meant finding a Windows box
# first.
#
# NOT part of scripts/linux/verify.sh. netsim is a tool, not a test -- it has no
# checks and no exit verdict; it sits between two clients and degrades the link.
# Building it in the gate would cost time and prove nothing.
#
#   usage: scripts/linux/build_netsim.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"
source "$TOOLCHAIN/vcenv.sh"

VC="$TOOLCHAIN/VS10/VC"
SDK="$TOOLCHAIN/SDK"
# No ENet include here, deliberately: netsim works on raw datagrams and must stay
# blind to what is inside them. It is below the protocol, not a peer in it.
export INCLUDE="$(winpath "$VC/include");$(winpath "$SDK/Include");$(winpath "$REPO/third_party/vc10_compat")"
export LIB="$(winpath "$VC/lib/amd64");$(winpath "$SDK/Lib/x64")"

mkdir -p "$REPO/dist" "$REPO/build/netsim"

echo "=== Building netsim.exe (Release|x64, v100 via Wine) ==="
vcl /nologo /O2 /EHsc /W3 /DWIN32 \
    /Fo"$(winpath "$REPO/build/netsim/")" \
    /Fe"$(winpath "$REPO/dist/netsim.exe")" \
    "$(winpath "$REPO/src/netsim/main.cpp")" \
    ws2_32.lib

[ -f "$REPO/dist/netsim.exe" ] || { echo "netsim build FAILED" >&2; exit 1; }
echo "netsim built: $REPO/dist/netsim.exe"
