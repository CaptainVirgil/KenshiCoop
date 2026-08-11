#!/usr/bin/env bash
# Swap the installed plugin to the HARNESS build with the buy probe armed, or
# back again. Answers one question: when a player buys a building, WHERE does
# Kenshi strip its pre-placed furniture, and how many internals does it remove?
#
# Why a swap and not a flag: the probe lives in EngineProbe.cpp, which the
# Release build deliberately excludes (the shipping DLL carries no probe
# trampolines). So the answer needs the Harness DLL for exactly one purchase.
#
#   scripts/linux/buyprobe.sh on     # back up Release, install Harness+probe
#   scripts/linux/buyprobe.sh off    # restore the Release DLL
#   scripts/linux/buyprobe.sh read   # show what the probe captured
#
# Then, in game: buy any for-sale building. Two lines land in the log:
#   [buyspy] buyMeCallback bld=... result=... internals N -> M (STRIP CONFIRMED
#   inside the callback)   or   (NO strip inside the callback - deferred)
# That names the engine primitive the peer-side strip has to reproduce, which
# is the one fact the headers cannot give (BuildingInterior is undumped).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODS="$HOME/.local/share/Steam/steamapps/common/Kenshi/mods/KenshiCoop"
LOG="$HOME/.local/share/Steam/steamapps/common/Kenshi"
BACKUP="$MODS/KenshiCoop.dll.release-backup"

case "${1:-}" in
on)
    [ -f "$REPO/build/Harness/KenshiCoop.dll" ] || {
        echo "no Harness DLL - run: scripts/linux/build_plugin.sh Harness" >&2; exit 1; }
    # Back up ONCE: running 'on' twice must not overwrite the real Release DLL
    # with the Harness one and strand the player on a probe build.
    [ -f "$BACKUP" ] || cp "$MODS/KenshiCoop.dll" "$BACKUP"
    cp "$REPO/build/Harness/KenshiCoop.dll" "$MODS/KenshiCoop.dll"
    cp "$REPO/build/Harness/KenshiCoop.map" "$MODS/KenshiCoop.map" 2>/dev/null || true
    echo "Harness+probe installed. Backup: $BACKUP"
    echo
    echo "NEXT:"
    echo "  1. Add to $MODS/coop_config.json (or set the env var):"
    echo "       KENSHICOOP_BUY_PROBE=1   (env only - the probe is a research"
    echo "       detour, deliberately not a config key)"
    echo "     Launch from a terminal with it set, e.g."
    echo "       KENSHICOOP_BUY_PROBE=1 %command%   in Steam launch options"
    echo "  2. Start Kenshi, buy ANY for-sale building."
    echo "  3. scripts/linux/buyprobe.sh read"
    echo "  4. scripts/linux/buyprobe.sh off"
    ;;
off)
    [ -f "$BACKUP" ] || { echo "no backup found - nothing to restore" >&2; exit 1; }
    mv "$BACKUP" "$MODS/KenshiCoop.dll"
    echo "Release DLL restored."
    ;;
read)
    grep -h "\[buyspy\]" "$LOG"/KenshiCoop_*.log "$LOG"/KenshiCoop_*.log.prev 2>/dev/null ||
        echo "no [buyspy] lines yet (probe not armed, or no purchase made)"
    ;;
*)
    echo "usage: $0 {on|off|read}" >&2; exit 2 ;;
esac
