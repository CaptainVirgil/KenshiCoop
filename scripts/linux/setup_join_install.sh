#!/usr/bin/env bash
# Create a second, fully independent Kenshi install so ONE person can run both
# ends of a co-op session on one machine.
#
# Linux twin of scripts/setup_join_install.cmd. The Windows side has had this for
# a long time; without it, testing anything two-player on Linux meant finding a
# second human, and half the bugs in this repo are only reachable with two
# clients talking to each other.
#
# NO SECOND STEAM ACCOUNT IS NEEDED. The plugin's transport defaults to UDP (see
# Config.cpp), so the two instances talk over 127.0.0.1 and Steam's P2P tunnel is
# never involved. That also means this setup canNOT exercise the Steam transport -
# for that you still need a second machine and a second account.
#
# The clone is a btrfs REFLINK copy where the filesystem supports it: 12 GB
# appears instantly and costs almost no disk, because the two installs share
# extents until one of them is written to. Falls back to a plain copy elsewhere.
#
# Safe to re-run. The first run clones; later runs rsync the GAME files over and
# deliberately leave the join copy's own save/, settings and logs alone.
#
#   usage: scripts/linux/setup_join_install.sh [src-kenshi-dir] [dst-dir]
set -euo pipefail

SRC="${1:-$HOME/.local/share/Steam/steamapps/common/Kenshi}"
DST="${2:-$HOME/Kenshi-Join}"

# Per-instance mutable state. These must NEVER be synced from the host copy after
# the first clone, or the two clients start sharing a world and a config and the
# whole point is lost.
MUTABLE=(save settings.cfg controls.cfg kenshi.cfg kenshi.log kenshi_info.log
         Havok.log FileIOLog.txt RE_Kenshi_log.txt KenshiCoop_host.log
         KenshiCoop_join.log KenshiCoop_host.log.prev KenshiCoop_join.log.prev)

die() { echo "ERROR: $*" >&2; exit 1; }

[ -f "$SRC/kenshi_x64.exe" ] || die "no Kenshi at '$SRC' (pass the install path as \$1)"
SRC="$(cd "$SRC" && pwd)"
[ "$SRC" != "$(cd "$DST" 2>/dev/null && pwd || echo /nonexistent)" ] ||
    die "source and destination are the same directory"
case "$DST" in
  "$SRC"/*) die "destination is inside the source install" ;;
esac

if [ ! -e "$DST" ]; then
    echo "=== cloning $SRC -> $DST ==="
    # --reflink=auto: instant + near-zero disk on btrfs, ordinary copy elsewhere.
    # Deliberately NOT a hardlink copy: hardlinks share the inode, so the game
    # writing to one install would silently corrupt the other.
    cp -a --reflink=auto "$SRC" "$DST"
    echo "    cloned"
    # Strip the host's mutable state out of the fresh clone so the join starts
    # with its own. Its save/ is seeded below.
    for m in "${MUTABLE[@]}"; do rm -rf "${DST:?}/$m"; done
else
    echo "=== re-syncing game files $SRC -> $DST (keeping the join's own state) ==="
    RSYNC_EXCLUDES=()
    for m in "${MUTABLE[@]}"; do RSYNC_EXCLUDES+=(--exclude "/$m"); done
    rsync -a --delete "${RSYNC_EXCLUDES[@]}" "$SRC/" "$DST/"
    echo "    synced"
fi

# Seed a world for the join to load, once. After this it owns an independent copy
# and the host's saves are never touched again.
if [ ! -d "$DST/save" ] && [ -d "$SRC/save" ]; then
    echo "=== seeding the join's initial save ==="
    cp -a --reflink=auto "$SRC/save" "$DST/save"
fi
mkdir -p "$DST/save"

# Kenshi looks for saves under the user profile unless told otherwise. The join
# copy must read and write saves INSIDE its own install directory, or both
# instances share one save tree through the Proton prefix and immediately fight
# over it. settings.cfg is intentionally not copied from the host, so the key is
# added to whatever the join generates.
touch "$DST/settings.cfg"
if ! grep -qi "^User save location=" "$DST/settings.cfg" 2>/dev/null; then
    printf 'User save location=1\n' >> "$DST/settings.cfg"
    echo "    set 'User save location=1' in the join's settings.cfg"
fi

echo
echo "JOIN install ready: $DST"
du -sh --apparent-size "$DST" 2>/dev/null | awk '{print "  apparent size: " $1}'
df -h --output=avail "$DST" 2>/dev/null | tail -1 | awk '{print "  disk free:     " $1}'
echo
echo "Next:"
echo "  1. Keep the mod in step in BOTH installs after every rebuild:"
echo "       scripts/linux/update-kenshicoop.sh --from-zip dist/KenshiCoop-kit-<label>.zip"
echo "       scripts/linux/update-kenshicoop.sh --from-zip dist/KenshiCoop-kit-<label>.zip --kenshi '$DST'"
echo "     A version mismatch between the two is a hard connection refusal."
echo "  2. Launch them:  scripts/linux/launch_coop.sh host"
echo "                   scripts/linux/launch_coop.sh join"
