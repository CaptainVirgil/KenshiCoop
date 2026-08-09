#!/usr/bin/env bash
# Launch one end of a co-op session on this machine, so ONE person can test both.
#
#   scripts/linux/launch_coop.sh host       # the Steam install, through Steam
#   scripts/linux/launch_coop.sh hostdirect # the Steam install, direct Proton
#   scripts/linux/launch_coop.sh join       # the clone from setup_join_install.sh
#
# hostdirect is what the first automated two-client session (2026-08-09) used:
# same mechanics as the join (Proton Experimental, seeded prefix, patched exe,
# absolute path), so KENSHICOOP_AUTOCONNECT=1 KENSHICOOP_SAVE=<name> gives a
# zero-click host. It runs on a COPY of the host prefix, so the test never
# writes the real save tree.
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

case "$ROLE" in host|hostdirect|join) ;; *) die "usage: $0 <host|hostdirect|join>" ;; esac

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

if [ "$ROLE" = hostdirect ]; then
    KENSHI="$STEAM_ROOT/steamapps/common/Kenshi"
    [ -x "$KENSHI/RE_Kenshi/kenshi_x64.exe" ] || die "no patched exe at $KENSHI/RE_Kenshi"
    check_transport "$KENSHI"
    PROTON_DIR="$STEAM_ROOT/steamapps/common/Proton - Experimental"
    [ -x "$PROTON_DIR/proton" ] || die "hostdirect needs Proton Experimental (matches the seeded prefix)"
    # A second Proton session on the LIVE compatdata exits silently one line in;
    # a seeded copy works and keeps the real save tree read-only for this test.
    export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-$HOME/.local/share/kenshicoop-host-prefix}"
    HOST_PFX="$STEAM_ROOT/steamapps/compatdata/$KENSHI_APPID"
    if [ ! -f "$STEAM_COMPAT_DATA_PATH/pfx/drive_c/windows/system32/mfc100u.dll" ]; then
        [ -d "$HOST_PFX/pfx" ] || die "host prefix missing at $HOST_PFX - run Kenshi once through Steam first"
        echo "    seeding host-direct prefix from the live one"
        rm -rf "$STEAM_COMPAT_DATA_PATH"
        cp -a --reflink=auto "$HOST_PFX" "$STEAM_COMPAT_DATA_PATH"
    fi
    export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_ROOT"
    export SteamAppId=$KENSHI_APPID SteamGameId=$KENSHI_APPID SteamOverlayGameId=$KENSHI_APPID
    export KENSHICOOP_MODE=host
    export KENSHICOOP_PORT="$PORT"
    export KENSHICOOP_LOG="${KENSHICOOP_LOG:-$KENSHI/KenshiCoop_host.log}"
    echo "=== HOST (direct): $KENSHI ==="
    echo "    prefix: $STEAM_COMPAT_DATA_PATH"
    echo "    log:    $KENSHICOOP_LOG"
    cd "$KENSHI"
    # Absolute path is load-bearing: a relative exe arg makes Proton exit
    # silently right after wine init, with nothing in any log.
    exec "$PROTON_DIR/proton" waitforexitandrun "$KENSHI/RE_Kenshi/kenshi_x64.exe" --norestart
fi

# ---- JOIN ---------------------------------------------------------------------
[ -f "$JOIN_DIR/kenshi_x64.exe" ] ||
    die "no join install at $JOIN_DIR - run scripts/linux/setup_join_install.sh first"
check_transport "$JOIN_DIR"

# Pick a Proton runner. NOT a free choice: the join prefix is seeded from the
# host's compatdata (below), so the runner must match the one that built that
# prefix - Kenshi runs under Proton Experimental on this machine, and pointing
# an older runner at an 11.x prefix is the "invalid version" prefix-corruption
# trap. Prefer Experimental when present; PROTON= still overrides.
if [ -z "${PROTON:-}" ]; then
    if [ -x "$STEAM_ROOT/steamapps/common/Proton - Experimental/proton" ]; then
        PROTON="$STEAM_ROOT/steamapps/common/Proton - Experimental"
    else
        PROTON="$(ls -d "$STEAM_ROOT/steamapps/common/Proton "* \
                         "$HOME/.steam/steam/compatibilitytools.d/"* 2>/dev/null |
                  while read -r d; do [ -x "$d/proton" ] && echo "$d"; done | tail -1)"
    fi
fi
[ -n "$PROTON" ] && [ -x "$PROTON/proton" ] ||
    die "no Proton runner found - set PROTON=/path/to/runner"

# Its OWN prefix. Sharing the host's compatdata would put both clients' registry
# and user profile in one tree, which is most of what the separate install exists
# to avoid.
#
# But NOT a bare prefix. A prefix Proton mints from nothing is missing what
# Steam's install script put in the host's: the VC++ 2010 runtime above all
# (kenshi_x64.exe imports MSVCR100/MFC100 - in a bare prefix it dies c0000135
# before RE_Kenshi writes a single log line, which is exactly what the
# 2026-08-08 "second instance will not start" mystery looked like), plus the
# steamclient registry wiring. So seed the join prefix from the host's proven
# one. mfc100u.dll is the sentinel for that whole class; if it is missing the
# prefix is the bare kind and gets rebuilt from the host's. Reflink copy -
# instant and nearly free on btrfs.
export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-$HOME/.local/share/kenshicoop-join-prefix}"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_ROOT"
HOST_PFX="$STEAM_ROOT/steamapps/compatdata/$KENSHI_APPID"
if [ ! -f "$STEAM_COMPAT_DATA_PATH/pfx/drive_c/windows/system32/mfc100u.dll" ]; then
    [ -d "$HOST_PFX/pfx" ] || die "host prefix missing at $HOST_PFX - run Kenshi once through Steam first"
    echo "    seeding join prefix from the host's (VC2010 runtime + steam wiring)"
    rm -rf "$STEAM_COMPAT_DATA_PATH"
    mkdir -p "$(dirname "$STEAM_COMPAT_DATA_PATH")"
    cp -a --reflink=auto "$HOST_PFX" "$STEAM_COMPAT_DATA_PATH"
fi
mkdir -p "$STEAM_COMPAT_DATA_PATH"

# Read by the plugin at startup (Config.cpp). The F2 panel can still override the
# role at runtime; this just means the join comes up already pointed at the host.
# Kenshi is a Steam build: steam_api64 wants an app identity, and without one the
# game exits during startup instead of erroring. The clone carries steam_appid.txt
# already; these make the running Steam client answer for it.
export SteamAppId=$KENSHI_APPID
export SteamGameId=$KENSHI_APPID
export SteamOverlayGameId=$KENSHI_APPID

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

# Launch RE_Kenshi's PATCHED exe directly, with the flag that tells it not to
# relaunch. This is the whole trick, and without it the join never starts.
#
# RE_Kenshi installs a second, patched kenshi_x64.exe under RE_Kenshi/. The
# top-level exe is a shim: it execs ./RE_Kenshi/kenshi_x64.exe --norestart and
# exits. Under Steam that hand-off survives because Steam's own launch wrapper
# holds the session open; under a bare `proton run` the tracked process is gone
# the moment the shim execs, wineserver tears the prefix down, and the real game
# dies before RE_Kenshi writes a single log line - which is exactly what it looked
# like, an instant silent exit with no RE_Kenshi_log.txt anywhere.
#
# Skipping the shim removes the hand-off entirely. `--norestart` is the same
# argument the shim passes, so this is the process the host ends up running too.
#
# waitforexitandrun (not run) is Steam's own verb and waits for the whole tree.
GAME_EXE="$JOIN_DIR/kenshi_x64.exe"
GAME_ARGS=()
if [ -x "$JOIN_DIR/RE_Kenshi/kenshi_x64.exe" ]; then
    GAME_EXE="$JOIN_DIR/RE_Kenshi/kenshi_x64.exe"
    GAME_ARGS=(--norestart)
    echo "    exe:    RE_Kenshi/kenshi_x64.exe --norestart (skipping the relaunch shim)"
fi
exec "$PROTON/proton" waitforexitandrun "$GAME_EXE" "${GAME_ARGS[@]}"
