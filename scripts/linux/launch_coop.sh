#!/usr/bin/env bash
# Launch one end of a co-op session on this machine, so ONE person can test both.
#
#   scripts/linux/launch_coop.sh host     # the Steam install, through Steam
#   scripts/linux/launch_coop.sh join     # the clone from setup_join_install.sh
#
# The two clients talk over UDP on 127.0.0.1. That is not a workaround - UDP is
# the plugin's default transport (Config.cpp) and Steam P2P is the opt-in. So NO
# second Steam account is required, and nothing here touches Steam's networking.
#
# What this canNOT test: the Steam P2P tunnel itself. That path only exists
# between two Steam users on two machines. Everything above the transport - the
# whole replication layer, which is where the bugs live - is exercised fully.
#
# HOST goes through Steam normally, so the host end behaves exactly as a player's
# does. JOIN is a separate install Steam knows nothing about, so it is launched
# directly against a Proton runner with its own prefix.
set -euo pipefail

ROLE="${1:-}"
JOIN_DIR="${JOIN_DIR:-$HOME/Kenshi-Join}"
STEAM_ROOT="${STEAM_ROOT:-$HOME/.local/share/Steam}"
KENSHI_APPID=233860
PORT="${KENSHICOOP_PORT:-27800}"

die() { echo "ERROR: $*" >&2; exit 1; }

case "$ROLE" in host|join) ;; *) die "usage: $0 <host|join>" ;; esac

# Both ends must be on the UDP transport or they will never find each other. The
# shipped default is udp, but a config edited for a real Steam session says
# "steam", and the resulting failure looks like a network problem rather than a
# setting. Check rather than assume; do NOT rewrite the player's file.
check_transport() {
    local cfg="$1/mods/KenshiCoop/coop_config.json"
    [ -f "$cfg" ] || return 0
    if grep -q '"transport"[[:space:]]*:[[:space:]]*"steam"' "$cfg"; then
        echo "NOTE: $cfg has transport=steam."
        echo "      For a same-machine test, switch Transport to UDP in the F2 panel"
        echo "      (or set \"transport\": \"udp\" in that file). Two clients on the"
        echo "      Steam transport will not connect to each other over loopback."
    fi
}

if [ "$ROLE" = host ]; then
    KENSHI="$STEAM_ROOT/steamapps/common/Kenshi"
    [ -f "$KENSHI/kenshi_x64.exe" ] || die "no Kenshi at $KENSHI"
    check_transport "$KENSHI"
    echo "=== HOST: launching through Steam ==="
    echo "    in game: F2 -> Role HOST, Transport UDP, load a save, Connection ONLINE"
    setsid steam -applaunch "$KENSHI_APPID" >/dev/null 2>&1 < /dev/null &
    exit 0
fi

# ---- JOIN ---------------------------------------------------------------------
[ -f "$JOIN_DIR/kenshi_x64.exe" ] ||
    die "no join install at $JOIN_DIR - run scripts/linux/setup_join_install.sh first"
check_transport "$JOIN_DIR"

# Pick a Proton runner. Newest by name is a guess, so PROTON= overrides it; the
# choice barely matters here (Kenshi is undemanding) but a runner that cannot
# start is a confusing failure, so say which one was used.
if [ -z "${PROTON:-}" ]; then
    PROTON="$(ls -d "$STEAM_ROOT/steamapps/common/Proton - Experimental" \
                     "$STEAM_ROOT/steamapps/common/Proton "* \
                     "$HOME/.steam/steam/compatibilitytools.d/"* 2>/dev/null |
              while read -r d; do [ -x "$d/proton" ] && echo "$d"; done | tail -1)"
fi
[ -n "$PROTON" ] && [ -x "$PROTON/proton" ] ||
    die "no Proton runner found - set PROTON=/path/to/runner"

# Its OWN prefix. Sharing the host's compatdata would put both clients' registry
# and user profile in one tree, which is most of what the separate install exists
# to avoid.
export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-$HOME/.local/share/kenshicoop-join-prefix}"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_ROOT"
mkdir -p "$STEAM_COMPAT_DATA_PATH"

# Read by the plugin at startup (Config.cpp). The F2 panel can still override the
# role at runtime; this just means the join comes up already pointed at the host.
export KENSHICOOP_MODE=join
export KENSHICOOP_IP=127.0.0.1
export KENSHICOOP_PORT="$PORT"
export KENSHICOOP_LOG="$JOIN_DIR/KenshiCoop_join.log"

echo "=== JOIN: $JOIN_DIR ==="
echo "    proton: $(basename "$PROTON")"
echo "    prefix: $STEAM_COMPAT_DATA_PATH"
echo "    target: $KENSHICOOP_IP:$KENSHICOOP_PORT"
echo "    log:    $KENSHICOOP_LOG"
echo "    in game: F2 -> Role JOIN, Transport UDP, Connection ONLINE (no save needed)"
cd "$JOIN_DIR"
exec "$PROTON/proton" run "$JOIN_DIR/kenshi_x64.exe"
