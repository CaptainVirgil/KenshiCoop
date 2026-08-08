#!/usr/bin/env bash
# Build the two per-OS downloads for a release.
#
# One kit per platform, each carrying an INSTALL file written for THAT platform
# only. A single download with both sets of instructions makes the reader work out
# which half applies to them before they can start, and the Windows player - who is
# usually the one who did not build any of this - should not have to read a Proton
# path to install a mod.
#
# The mod payload is identical in both: the same DLL, byte for byte. Only the
# instructions and the archive format differ (zip for Windows, tar.gz for Linux).
#
#   usage: scripts/linux/make_release.sh <label>
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LABEL="${1:?usage: make_release.sh <label>}"
OUT="$REPO/dist/release-$LABEL"

"$REPO/scripts/linux/make_kit.sh" "$LABEL" > /dev/null
SRC="$REPO/dist/kit/KenshiCoop"
[ -f "$SRC/KenshiCoop.dll" ] || { echo "kit build produced no DLL" >&2; exit 1; }

PROTO="$(grep -oP '"protocolVersion":\s*\K[0-9]+' "$SRC/kit.json")"
SHA="$(sha256sum "$SRC/KenshiCoop.dll" | cut -d' ' -f1)"

rm -rf "$OUT"; mkdir -p "$OUT/win/KenshiCoop" "$OUT/linux/KenshiCoop"
cp "$SRC"/* "$OUT/win/KenshiCoop/"
cp "$SRC"/* "$OUT/linux/KenshiCoop/"
cp "$REPO/scripts/update-kenshicoop.ps1"      "$OUT/win/"
cp "$REPO/scripts/linux/update-kenshicoop.sh" "$OUT/linux/"

# ---- Windows ------------------------------------------------------------------
cat > "$OUT/win/INSTALL-WINDOWS.txt" <<TXT
KenshiCoop ($LABEL) - Windows install
=====================================

Co-op Kenshi for two people. Both of you must install THIS SAME BUILD: the two
copies check each other's version when they connect, and refuse if they differ.

  Build: $LABEL      Protocol: $PROTO

WHAT YOU NEED FIRST
-------------------
  1. Kenshi on Steam.
  2. RE_Kenshi 0.3.1 or newer - it is what loads this mod into the game.
     https://github.com/BFrizzleFoShizzle/RE_Kenshi/releases/latest
     Download the zip, extract anywhere, run the installer inside.
     Check it worked: start Kenshi, the main menu shows an RE_Kenshi version line.
  3. Steam running and online. The connection is Steam peer-to-peer, so there is
     no port forwarding and no IP addresses to exchange.

INSTALL - THE EASY WAY (recommended)
------------------------------------
Close Kenshi first. Then, in PowerShell:

    powershell -ExecutionPolicy Bypass -File update-kenshicoop.ps1

That script is in this folder. It finds Kenshi across all your Steam library
folders, checks the download against a published checksum, backs up whatever mod
folder you already have, and installs this one. Run it again any time to update.

  Undo it:            add  -Rollback
  Kenshi elsewhere:   add  -KenshiPath "D:\SteamLibrary\steamapps\common\Kenshi"

INSTALL - BY HAND
-----------------
Copy the KenshiCoop folder from this archive into your Kenshi mods folder, so you
end up with:

    <Kenshi>\mods\KenshiCoop\KenshiCoop.dll

The usual location is
C:\Program Files (x86)\Steam\steamapps\common\Kenshi\mods\

YOUR SAVE GAMES ARE NOT TOUCHED
-------------------------------
Kenshi keeps saves in %LOCALAPPDATA%\kenshi\save - a completely different folder
from mods\. The installer only ever writes inside <Kenshi>\mods\KenshiCoop and
its own backup folder, and it refuses to run at all if the path it worked out is
not that. Your previous mod folder is copied to <Kenshi>\KenshiCoop-backups\
before anything is replaced, and the last five are kept.

PLAYING
-------
  1. Launch Kenshi. In the Mods menu, tick KenshiCoop. This sticks.
  2. Press F2. The co-op panel opens - it works at the main menu too.
  3. Both click "Copy my Steam ID" and send it to the other person.
  4. Each of you copies the OTHER person's ID and clicks "Paste friend's Steam
     ID". The panel shows the last four digits of what it captured - check those
     match. This is per-session, so re-paste after restarting Kenshi.
  5. Leave Transport on STEAM.
  6. HOST: load a save (or start a new game), set Role: HOST, Connection: ONLINE.
  7. JOIN: from the main menu, no save needed. Role: JOIN, Connection: ONLINE.
     The host's world streams over and you drop into it.

One of you must be HOST and the other JOIN. Both default to HOST, and two hosts
will wait for each other forever - the panel now tells you so after 20 seconds.

New game? The mod adds two starts to the New Game list, both the Wanderer start
with the two characters already split into separate squads.

IF IT GOES WRONG
----------------
  F2 does nothing, or "the co-op plugin has not started"
      RE_Kenshi did not load it. Check KenshiCoop is ticked in the Mods menu, and
      search <Kenshi>\RE_Kenshi_log.txt for KenshiCoop. Reinstalling RE_Kenshi
      usually fixes it.

  "Version mismatch" on the panel
      You are on different builds. Both re-run the installer and compare the
      build label and protocol number it prints.

  Stuck on "Connecting..."
      Both Steam clients must be running and not in offline mode, and each of you
      must have pasted the OTHER person's ID. The panel will tell you what Steam
      reports after a few seconds.

  The log is <Kenshi>\KenshiCoop_host.log or KenshiCoop_join.log. Note the file
  name comes from the Role you picked in the panel, not the one actually
  negotiated, so a file called _host.log can easily be the joining player's.

WHAT TO EXPECT
--------------
This is experimental. Desyncs happen, crashes happen, two players is the design
target. The host owns the world: they pick the save, and saves are shared - when
either of you saves, that save goes to both machines.
TXT

# ---- Linux --------------------------------------------------------------------
cat > "$OUT/linux/INSTALL-LINUX.txt" <<TXT
KenshiCoop ($LABEL) - Linux install (Kenshi via Steam/Proton)
=============================================================

Both players must install THIS SAME BUILD: the two copies check each other's
version when they connect, and refuse if they differ.

  Build: $LABEL      Protocol: $PROTO

WHAT YOU NEED FIRST
-------------------
  1. Kenshi on Steam, running under Proton.
  2. RE_Kenshi 0.3.1 or newer, installed INTO the Kenshi prefix - it is what
     loads this mod. https://github.com/BFrizzleFoShizzle/RE_Kenshi/releases/latest
     Its installer is a Windows program, so run it through Proton against the
     Kenshi prefix (compatdata/233860), not against a fresh one.
  3. Steam running and online.

INSTALL
-------
Close Kenshi first, then:

    ./update-kenshicoop.sh

It reads your Steam library folders to find Kenshi, verifies the download against
a published checksum, backs up your current mod folder and installs this one.

  Undo it:            ./update-kenshicoop.sh --rollback
  Kenshi elsewhere:   ./update-kenshicoop.sh --kenshi /path/to/Kenshi

By hand instead: copy the KenshiCoop folder into
  ~/.local/share/Steam/steamapps/common/Kenshi/mods/

YOUR SAVE GAMES ARE NOT TOUCHED
-------------------------------
Saves live inside the Proton prefix, under
  .../compatdata/233860/pfx/drive_c/users/steamuser/AppData/Local/kenshi/save
which is a completely different tree from mods/. The installer only writes inside
<Kenshi>/mods/KenshiCoop and its own backup folder, and refuses to run if the
path it worked out is not that. Your previous mod folder is copied to
<Kenshi>/KenshiCoop-backups/ first, and the last five are kept.

PLAYING
-------
Identical to Windows: enable KenshiCoop in the Mods menu, press F2, swap Steam
IDs, one player HOSTs and loads a save, the other JOINs from the main menu. One
of you must be HOST and the other JOIN - both default to HOST.

NOTES SPECIFIC TO PROTON
------------------------
  * Steam must be the Linux Steam client, running and online; the mod talks to it
    through the game's Steam API, so offline mode will not connect.
  * The log is <Kenshi>/KenshiCoop_host.log or _join.log, written next to the
    game rather than inside the prefix. The file name comes from the Role you
    picked, not the one negotiated, so _host.log may be the join's.
  * If you built this yourself, scripts/linux/ has the whole toolchain: no
    Windows and no sudo required.

WHAT TO EXPECT
--------------
Experimental. Desyncs happen, crashes happen, two players is the design target.
The host owns the world and saves are shared between both machines.
TXT

WIN_ZIP="$REPO/dist/KenshiCoop-$LABEL-windows.zip"
LIN_TGZ="$REPO/dist/KenshiCoop-$LABEL-linux.tar.gz"
rm -f "$WIN_ZIP" "$LIN_TGZ"
(cd "$OUT/win"   && zip -qr "$WIN_ZIP" KenshiCoop INSTALL-WINDOWS.txt update-kenshicoop.ps1)
(cd "$OUT/linux" && tar czf "$LIN_TGZ" KenshiCoop INSTALL-LINUX.txt update-kenshicoop.sh)

echo "windows: $WIN_ZIP"
echo "linux:   $LIN_TGZ"
echo "protocol $PROTO   dll $SHA"
