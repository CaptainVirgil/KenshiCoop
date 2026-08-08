#!/usr/bin/env bash
# The commit gate, Linux side. Twin of scripts/verify.ps1: game-independent
# checks that every commit has to keep green.
#
#   1. the plugin builds in BOTH configurations, from the vcxproj's own source
#      lists -- Release must not pick up the scenario harness
#   2. prototest: the wire contract (exact packed sizes and field offsets),
#      PROTOCOL_VERSION, the interp buffer, the save-transfer receiver
#   3. tunneltest: ENet over the Steam socket hooks under loss and the 1200 B
#      datagram ceiling
#   4. Contract.Tests.ps1 when a PowerShell host is installed (scenario manifest
#      vs the makeScenario factory, oracle registry, verdict rule). Skipped, not
#      failed, when neither pwsh nor powershell is present -- it is a Windows-side
#      check that happens to be portable, not a Linux requirement.
#
# Nothing here launches Kenshi. The scenario tier needs two game installs and a
# display; it stays on the Windows side.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"

fail=0
step() {
    local name="$1"; shift
    printf '\n=== %s ===\n' "$name"
    if "$@"; then
        printf '  PASS  %s\n' "$name"
    else
        printf '  FAIL  %s\n' "$name"
        fail=$((fail + 1))
    fi
}

run_exe() {
    # shellcheck disable=SC1090
    source "$REPO/scripts/linux/vcenv.sh"
    "$WINE_BIN/wine" "$1" > /tmp/kc-verify-$$.log 2>&1
    local rc=$?
    tail -1 /tmp/kc-verify-$$.log
    rm -f /tmp/kc-verify-$$.log
    return $rc
}

step "build plugin (Release)"  ./scripts/linux/build_plugin.sh Release
step "build plugin (Harness)"  ./scripts/linux/build_plugin.sh Harness
step "build prototest"         ./scripts/linux/build_prototest.sh
step "prototest"               run_exe "$REPO/dist/prototest.exe"
step "build tunneltest"        ./scripts/linux/build_tunneltest.sh
step "tunneltest"              run_exe "$REPO/dist/tunneltest.exe"

PS_HOST="$(command -v pwsh || command -v powershell || true)"
if [ -n "$PS_HOST" ]; then
    step "Contract.Tests.ps1" "$PS_HOST" -NoProfile -File "$REPO/scripts/tests/Contract.Tests.ps1"
else
    printf '\n=== Contract.Tests.ps1 ===\n  SKIP  no PowerShell host (install pwsh to run it here)\n'
fi

printf '\n'
if [ "$fail" -eq 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL ($fail step(s))"
fi
exit "$fail"
