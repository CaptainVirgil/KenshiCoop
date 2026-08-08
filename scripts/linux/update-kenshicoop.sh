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
if pgrep -f "kenshi_x64.exe" > /dev/null 2>&1; then
    die "Kenshi is running - close it completely, then re-run"
fi

# ---- 3. rollback --------------------------------------------------------------
if [ "$ROLLBACK" -eq 1 ]; then
    [ -d "$BACKUP_DIR" ] || die "no backups in $BACKUP_DIR"
    latest="$(find "$BACKUP_DIR" -maxdepth 1 -mindepth 1 -type d | sort | tail -1)"
    [ -n "$latest" ] || die "no backups in $BACKUP_DIR"
    rm -rf "$MOD_DIR"
    cp -r "$latest" "$MOD_DIR"
    ok "restored $(basename "$latest")"
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
    STAMP="$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$BACKUP_DIR/$STAMP"
    cp -r "$MOD_DIR/." "$BACKUP_DIR/$STAMP/"
    ok "backed up current mod to KenshiCoop-backups/$STAMP"
    [ -f "$MOD_DIR/coop_config.json" ] && KEEP_CFG="$(cat "$MOD_DIR/coop_config.json")"
    # Keep the last 5. The folder sits OUTSIDE mods/ so Kenshi never scans it.
    find "$BACKUP_DIR" -maxdepth 1 -mindepth 1 -type d | sort -r | tail -n +6 |
        while IFS= read -r old; do rm -rf "$old"; done
fi

# ---- 7. swap -------------------------------------------------------------------
rm -rf "$MOD_DIR"
cp -r "$NEW_MOD" "$MOD_DIR"

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
