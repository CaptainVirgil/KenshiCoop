#!/usr/bin/env bash
# Linux twin of scripts/build_prototest.cmd: builds dist/prototest.exe with the
# same v100 toolchain, through Wine.
#
# The v100 part is not incidental. prototest asserts the exact packed size and
# field offsets of every struct in Wire.h, and its job is to lock the layout THIS
# toolchain produces -- the layout the shipped DLL is compiled with. Building it
# with g++ would prove the wrong compiler's ABI.
#
# No game, no KenshiLib, no ENet: CRT only, so the resulting .exe runs under Wine
# with nothing else installed.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${TOOLCHAIN:-$HOME/.local/share/kenshicoop-toolchain}"
source "$TOOLCHAIN/vcenv.sh"

VC="$TOOLCHAIN/VS10/VC"
SDK="$TOOLCHAIN/SDK"
# Deliberately NOT vc_include(): prototest must not see KenshiLib or ENet.
export INCLUDE="$(winpath "$VC/include");$(winpath "$SDK/Include");$(winpath "$REPO/third_party/vc10_compat")"
export LIB="$(winpath "$VC/lib/amd64");$(winpath "$SDK/Lib/x64")"

mkdir -p "$REPO/dist" "$REPO/build/prototest"

echo "=== Building prototest.exe (Release|x64, v100 via Wine) ==="
# KENSHICOOP_PROTOTEST keeps SaveXfer.cpp CRT-only so the real save-transfer
# receiver can be exercised without ENet/KenshiLib.
vcl /nologo /O2 /EHsc /W3 /D KENSHICOOP_PROTOTEST \
    /Fo"$(winpath "$REPO/build/prototest/")" \
    /Fe"$(winpath "$REPO/dist/prototest.exe")" \
    "$(winpath "$REPO/src/prototest/main.cpp")" \
    "$(winpath "$REPO/src/plugin/sync/Interp.cpp")" \
    "$(winpath "$REPO/src/plugin/sync/SaveXfer.cpp")"

[ -f "$REPO/dist/prototest.exe" ] || { echo "prototest build FAILED" >&2; exit 1; }
echo "prototest built: $REPO/dist/prototest.exe"
