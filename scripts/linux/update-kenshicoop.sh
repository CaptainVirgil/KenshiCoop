#!/usr/bin/env bash
# Install or update the KenshiCoop co-op mod (CaptainVirgil fork) on Linux.
#
# The twin of scripts/update-kenshicoop.ps1, same behaviour and same guarantees.
# Both players must run the same build: a protocol mismatch is a hard connection
# reject, so this prints the build identity for you to compare.
#
# YOUR SAVES ARE NOT TOUCHED. Kenshi keeps them inside the Proton prefix, under
# .../compatdata/233860/pfx/drive_c/users/steamuser/AppData/Local/kenshi/save,
# which is nowhere near the mod folder. This script only ever writes inside
# <Kenshi>/mods/KenshiCoop and its own backup folder, and refuses to run if the
# path it resolved does not look like that. Your coop_config.json survives updates.
#
#   usage: scripts/linux/update-kenshicoop.sh [--kenshi DIR] [--tag TAG]
#                                             [--from-zip FILE] [--rollback]
set -euo pipefail

REPO_SLUG="CaptainVirgil/KenshiCoop"
KENSHI=""
TAG=""
FROM_ZIP=""
ROLLBACK=0

while [ $# -gt 0 ]; do
    case "$1" in
        --kenshi)    KENSHI="${2:?}"; shift 2 ;;
        --tag)       TAG="${2:?}"; shift 2 ;;
        --from-zip)  FROM_ZIP="${2:?}"; shift 2 ;;
        --rollback)  ROLLBACK=1; shift ;;
        -h|--help)   sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

ok()   { printf '  OK   %s\n' "$1"; }
note() { printf '       %s\n' "$1"; }
warn() { printf '  WARN %s\n' "$1"; }
die()  { printf '  STOP %s\n' "$1" >&2; exit 1; }

printf '\nKenshiCoop updater  (%s)\n' "$REPO_SLUG"
printf '============================================\n'

# ---- 1. locate Kenshi ---------------------------------------------------------
find_kenshi() {
    local roots=()
    local steam
    for steam in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root"; do
        [ -d "$steam" ] || continue
        roots+=("$steam/steamapps/common/Kenshi")
        # Extra library folders (a second drive is the normal case).
        local vdf="$steam/steamapps/libraryfolders.vdf"
        if [ -f "$vdf" ]; then
            while IFS= read -r lib; do
                [ -n "$lib" ] && roots+=("$lib/steamapps/common/Kenshi")
            done < <(grep -oP '"path"\s+"\K[^"]+' "$vdf" 2>/dev/null || true)
        fi
    done
    local r
    for r in "${roots[@]:-}"; do
        if [ -f "$r/kenshi_x64.exe" ] && [ -d "$r/mods" ]; then printf '%s' "$r"; return 0; fi
    done
    return 1
}

[ -n "$KENSHI" ] || KENSHI="$(find_kenshi || true)"
[ -n "$KENSHI" ] || die "could not find Kenshi. Re-run with --kenshi /path/to/Kenshi"
[ -f "$KENSHI/kenshi_x64.exe" ] || die "no kenshi_x64.exe in '$KENSHI' - that is not a Kenshi install"
ok "Kenshi: $KENSHI"

MOD_DIR="$KENSHI/mods/KenshiCoop"
BACKUP_DIR="$KENSHI/KenshiCoop-backups"

# Guard rail: everything destructive below happens under $MOD_DIR.
case "$MOD_DIR" in
    */mods/KenshiCoop) : ;;
    *) die "refusing to touch '$MOD_DIR' - expected <Kenshi>/mods/KenshiCoop" ;;
esac

SAVE_DIR="$HOME/.local/share/Steam/steamapps/compatdata/233860/pfx/drive_c/users/steamuser/AppData/Local/kenshi/save"
if [ -d "$SAVE_DIR" ]; then
    note "saves: $SAVE_DIR ($(find "$SAVE_DIR" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | wc -l) found) - not touched"
else
    note "saves: inside the Proton prefix - not touched by this script"
fi

# ---- 2. refuse while the game is running --------------------------------------
# RE_Kenshi holds KenshiCoop.dll open; swapping it under a live game leaves the
# old code running against the new file on disk.
# -x (exact process name), not -f. Matching the whole command line means any
# process that merely MENTIONS kenshi_x64.exe counts as the game running - a text
# editor with the file open, or the very shell that invoked this script - and the
# player is told to close a game they do not have open.
if pgrep -x "kenshi_x64.exe" > /dev/null 2>&1; then
    die "Kenshi is running - close it completely, then re-run"
fi

# ---- 3. rollback --------------------------------------------------------------
# Never delete the live mod folder before a VALID restore source has been chosen.
# The first version of this did, and picked the newest backup by name without
# checking it contained anything: an aborted earlier run leaves an empty directory
# that is newest, so rollback destroyed a working install and reported success
# while four good backups sat unused next to it.
if [ "$ROLLBACK" -eq 1 ]; then
    [ -d "$BACKUP_DIR" ] || die "no backups in $BACKUP_DIR"
    chosen=""
    while IFS= read -r cand; do
        [ -f "$cand/KenshiCoop.dll" ] || { note "skipping incomplete backup $(basename "$cand")"; continue; }
        chosen="$cand"; break
    done < <(find "$BACKUP_DIR" -maxdepth 1 -mindepth 1 -type d -not -name '*.partial' | sort -r)
    [ -n "$chosen" ] || die "no usable backup in $BACKUP_DIR (none contains KenshiCoop.dll)"

    # Stage beside the target, then swap. The staging area lives OUTSIDE mods/ so
    # Kenshi never scans a half-built folder as a mod.
    STAGING="$BACKUP_DIR/.restoring"
    rm -rf "$STAGING"
    cp -r "$chosen" "$STAGING" || die "could not stage the restore; nothing changed"
    [ -f "$STAGING/KenshiCoop.dll" ] || { rm -rf "$STAGING"; die "staged restore is incomplete; nothing changed"; }
    if [ -d "$MOD_DIR" ]; then
        rm -rf "$MOD_DIR.prev"
        mv "$MOD_DIR" "$MOD_DIR.prev"
    fi
    mv "$STAGING" "$MOD_DIR"
    rm -rf "$MOD_DIR.prev"
    ok "restored $(basename "$chosen")"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- 4. fetch the kit ---------------------------------------------------------
ZIP="$TMP/kit.zip"
if [ -n "$FROM_ZIP" ]; then
    [ -f "$FROM_ZIP" ] || die "no such file: $FROM_ZIP"
    cp "$FROM_ZIP" "$ZIP"
    ok "using local kit: $FROM_ZIP"
else
    if [ -n "$TAG" ]; then
        API="https://api.github.com/repos/$REPO_SLUG/releases/tags/$TAG"
    else
        API="https://api.github.com/repos/$REPO_SLUG/releases/latest"
    fi
    note "checking $API"
    URL="$(curl -fsSL "$API" 2>/dev/null |
           python3 -c 'import json,sys
try:
    rel = json.load(sys.stdin)
except Exception:
    sys.exit(1)
for a in rel.get("assets", []):
    if a["name"].startswith("KenshiCoop-kit") and a["name"].endswith(".zip"):
        print(a["browser_download_url"]); break
' || true)"
    [ -n "$URL" ] || die "no KenshiCoop-kit*.zip asset found (no releases yet, or GitHub unreachable)"
    note "downloading $(basename "$URL")"
    curl -fL --progress-bar -o "$ZIP" "$URL" || die "download failed"
    ok "downloaded $(du -h "$ZIP" | cut -f1)"
fi

# ---- 5. unpack and verify -----------------------------------------------------
STAGE="$TMP/stage"
mkdir -p "$STAGE"
unzip -q "$ZIP" -d "$STAGE" || die "could not unpack the kit"
NEW_MOD="$STAGE/KenshiCoop"
[ -f "$NEW_MOD/KenshiCoop.dll" ] || die "kit has no KenshiCoop/KenshiCoop.dll - refusing to install it"

LABEL="unknown"; PROTO=""
if [ -f "$NEW_MOD/kit.json" ]; then
    LABEL="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("label","unknown"))' "$NEW_MOD/kit.json")"
    PROTO="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("protocolVersion",""))' "$NEW_MOD/kit.json")"
    WANT="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("dllSha256",""))' "$NEW_MOD/kit.json")"
    GOT="$(sha256sum "$NEW_MOD/KenshiCoop.dll" | cut -d' ' -f1)"
    if [ -n "$WANT" ] && [ "$WANT" != "$GOT" ]; then
        die "checksum mismatch - the download is corrupt or tampered with. Nothing changed."
    fi
    ok "verified $LABEL (protocol $PROTO)"
else
    warn "kit has no manifest - cannot verify checksum or protocol version"
fi

# ---- 6. back up, preserving the player's config -------------------------------
KEEP_CFG=""
if [ -d "$MOD_DIR" ]; then
    [ -f "$MOD_DIR/coop_config.json" ] && KEEP_CFG="$(cat "$MOD_DIR/coop_config.json")"
    # Write to <stamp>.partial and rename only once the copy has returned. An
    # interrupted backup therefore leaves a directory that the rollback path skips
    # by name, instead of a plausible-looking empty one that outranks every good
    # backup because its timestamp is newest.
    STAMP="$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$BACKUP_DIR/$STAMP.partial"
    if cp -r "$MOD_DIR/." "$BACKUP_DIR/$STAMP.partial/" &&
       [ -f "$BACKUP_DIR/$STAMP.partial/KenshiCoop.dll" ]; then
        mv "$BACKUP_DIR/$STAMP.partial" "$BACKUP_DIR/$STAMP"
        ok "backed up current mod to KenshiCoop-backups/$STAMP"
    else
        rm -rf "$BACKUP_DIR/$STAMP.partial"
        die "could not back up the current mod folder; nothing changed"
    fi
    # Keep the last 5 COMPLETE backups. The folder sits OUTSIDE mods/ so Kenshi
    # never scans it as a mod.
    find "$BACKUP_DIR" -maxdepth 1 -mindepth 1 -type d -not -name '*.partial' | sort -r | tail -n +6 |
        while IFS= read -r old; do rm -rf "$old"; done
fi

# ---- 7. swap -------------------------------------------------------------------
# Stage, then swap by rename. delete-then-copy leaves a window where the player has
# no mod folder at all, and if the copy fails there they have nothing and no
# instruction beyond "run it again".
STAGING="$BACKUP_DIR/.installing"
rm -rf "$STAGING"
cp -r "$NEW_MOD" "$STAGING" || die "could not stage the new mod; nothing changed"
if [ -d "$MOD_DIR" ]; then
    rm -rf "$MOD_DIR.prev"
    mv "$MOD_DIR" "$MOD_DIR.prev"
fi
mv "$STAGING" "$MOD_DIR"
rm -rf "$MOD_DIR.prev"

# Verify what actually landed, not what we believe we copied.
if [ -n "${WANT:-}" ]; then
    LANDED="$(sha256sum "$MOD_DIR/KenshiCoop.dll" | cut -d' ' -f1)"
    [ "$LANDED" = "$WANT" ] || die "installed DLL does not match the manifest - run --rollback"
fi

if [ -n "$KEEP_CFG" ]; then
    printf '%s' "$KEEP_CFG" > "$MOD_DIR/coop_config.json"
    ok "kept your existing coop_config.json"
elif [ -f "$MOD_DIR/coop_config.default.json" ]; then
    cp "$MOD_DIR/coop_config.default.json" "$MOD_DIR/coop_config.json"
    ok "wrote a default coop_config.json"
fi

ok "installed to $MOD_DIR"
printf '\nDone.\n'
printf '  Build:  %s%s\n' "$LABEL" "$([ -n "$PROTO" ] && printf '  (protocol %s)' "$PROTO")"
printf '  Both players must be on this same build, or the connection is refused.\n'
printf '  Saves untouched.\n'
printf '  Roll back with:  scripts/linux/update-kenshicoop.sh --rollback\n\n'
printf '  In game: enable KenshiCoop in the Mods menu, then press F2.\n\n'
