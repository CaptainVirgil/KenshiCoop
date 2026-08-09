#!/usr/bin/env bash
# Assemble the two-player kit: the mod folder both players install, plus a
# manifest that lets them prove they are running the same build.
#
# That last part is not decoration. PROTOCOL_VERSION mismatch is a hard
# connection reject with no back-compat, so "are we on the same build?" is the
# first question of every failed session. kit.json answers it without a rebuild.
#
#   usage: scripts/linux/make_kit.sh [version-label]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LABEL="${1:-dev}"
OUT="$REPO/dist/kit"
DLL="$REPO/build/Release/KenshiCoop.dll"

[ -f "$DLL" ] || { echo "no Release DLL - run scripts/linux/build_plugin.sh Release" >&2; exit 1; }

PROTO="$(grep -oP 'PROTOCOL_VERSION\s*=\s*\K[0-9]+' "$REPO/src/netproto/Wire.h" | head -1)"
[ -n "$PROTO" ] || { echo "could not read PROTOCOL_VERSION from Wire.h" >&2; exit 1; }
SHA="$(sha256sum "$DLL" | cut -d' ' -f1)"
COMMIT="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
BUILT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

rm -rf "$OUT"
mkdir -p "$OUT/KenshiCoop"
cp "$DLL" "$OUT/KenshiCoop/"
cp "$REPO/dist/mods/KenshiCoop/KenshiCoop.mod" "$OUT/KenshiCoop/"
cp "$REPO/dist/mods/KenshiCoop/RE_Kenshi.json" "$OUT/KenshiCoop/"

# Shipped as coop_config.default.json, NOT coop_config.json: the updater copies it
# into place only when the player has none, so an existing file - which may carry a
# LAN address or hand-tuned values - is never overwritten by an update.
cat > "$OUT/KenshiCoop/coop_config.default.json" <<'JSON'
{
  // KenshiCoop config. For a normal Steam game you do NOT need to edit this file:
  // your friend's Steam ID is entered in-game (press F2).
  //
  // LAN / direct-UDP instead: set "transport": "udp" and put the host's address
  // in "ip". Both are re-read whenever you go ONLINE, so no restart is needed.
  "transport": "steam",
  "ip": "127.0.0.1",
  "port": 27800,
  "autoConnect": false

  // Optional tuning - delete any line to get the built-in default back.
  //
  // "midBandMax": 256    how many distant NPCs get motion updates, and how many
  // "midSliceMax": 32    go out per tick. Raise if far-off NPCs teleport instead
  //                      of walking; lower if a busy town costs too much bandwidth.
  //
  // "carryHealDebounceMs": 1500   how long this client waits, while the incoming
  // "furnHealDebounceMs": 1500    stream insists a body is carried or caged,
  //                      before reproducing that locally. Do NOT set to 0: the
  //                      stream renders slightly in the past, so a zero wait lets
  //                      it undo what your friend just did.
  //
  // "doorEchoHoldMs": 5000  how long to stay quiet about a door after your friend
  //                      reports its state.
  //
  // "koReleaseDebounceMs": 3000  how long the incoming stream must keep
  //                      showing a knocked-out body UPRIGHT before this client
  //                      releases the knockout itself. It exists because the
  //                      "they got up" event can go missing. Longer than the
  //                      heals above on purpose - this overrides a reliable
  //                      event rather than repairing a lost one. Do NOT set 0.
}
JSON

cat > "$OUT/KenshiCoop/kit.json" <<JSON
{
  "fork": "CaptainVirgil/KenshiCoop",
  "label": "$LABEL",
  "commit": "$COMMIT",
  "protocolVersion": $PROTO,
  "dllSha256": "$SHA",
  "builtUtc": "$BUILT"
}
JSON

# Ship the linker map alongside the DLL. The Release build already emits one and
# it was being thrown away, so a crash address from a player's report could not be
# resolved to a function by anyone - including whoever reads the report. It is a
# text file, it compresses to almost nothing, and it is the difference between
# "it crashed" and "it crashed in applyTargets".
if [ -f "$REPO/build/Release/KenshiCoop.map" ]; then
    cp "$REPO/build/Release/KenshiCoop.map" "$OUT/KenshiCoop/"
fi

ZIP="$REPO/dist/KenshiCoop-kit-$LABEL.zip"
rm -f "$ZIP"
(cd "$OUT" && zip -qr "$ZIP" KenshiCoop)

echo "kit:      $ZIP"
echo "protocol: $PROTO   commit: $COMMIT"
echo "dll:      $SHA"
echo "zip:      $(sha256sum "$ZIP" | cut -d' ' -f1)"
