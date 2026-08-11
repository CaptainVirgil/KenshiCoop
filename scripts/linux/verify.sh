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
        return 0
    fi
    printf '  FAIL  %s\n' "$name"
    fail=$((fail + 1))
    # Explicit, because a function returns the status of its LAST command - which
    # would be the printf or the arithmetic, i.e. always success - and callers now
    # branch on this to avoid running a binary whose build just failed.
    return 1
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
# Delete the binary before rebuilding, and skip the run when the build failed.
# Otherwise a build error leaves the PREVIOUS binary on disk, the run step
# executes it, and the gate reports a green test suite for code that does not
# compile - which is exactly what happened on 2026-08-08.
rm -f "$REPO/dist/prototest.exe" "$REPO/dist/tunneltest.exe" "$REPO/dist/netlinktest.exe"

if step "build prototest" ./scripts/linux/build_prototest.sh; then
    step "prototest"           run_exe "$REPO/dist/prototest.exe"
else
    printf '  SKIP  prototest (its build failed)\n'
fi
if step "build tunneltest" ./scripts/linux/build_tunneltest.sh; then
    step "tunneltest"          run_exe "$REPO/dist/tunneltest.exe"
else
    printf '  SKIP  tunneltest (its build failed)\n'
fi
if step "build netlinktest" ./scripts/linux/build_netlinktest.sh; then
    step "netlinktest"         run_exe "$REPO/dist/netlinktest.exe"
else
    printf '  SKIP  netlinktest (its build failed)\n'
fi
# drivetest builds AND runs in one script (it carries the same delete-first
# stale-binary guard internally), so it is ONE step rather than the build/run
# pair above. It is the first slice of the sync harness: the REAL
# ReplicatorDrive.cpp + ReplicatorCore.cpp + Interp.cpp against a fake engine,
# which is the only test in this gate that can see BEHAVIOUR - every other
# suite checks wire layout, framing or source drift. It exists because two
# receiver-drive regressions (the v0.51 mid-rest hold, the v0.57 tier
# hysteresis) shipped green through this gate and were caught by players.
# Costs ~20 s of real-time pacing; that is the price of the only net under
# the sync layer.
step "drivetest" ./scripts/linux/build_drivetest.sh

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
