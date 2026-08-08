# Playing KenshiCoop

Co-op Kenshi for two people. One of you hosts a world, the other drops into it and plays
their own squad. Both of you do everything below — there is no separate "client version",
you run the same mod.

## What you need

| | Where | Notes |
|---|---|---|
| **Kenshi** | Steam | The kit is built against Kenshi 1.0.65. |
| **RE_Kenshi 0.3.1+** | [GitHub releases](https://github.com/BFrizzleFoShizzle/RE_Kenshi/releases/latest) | Free. It is what loads the co-op plugin into the game. The Nexus page is the link people usually pass around; GitHub serves the same files and needs no account. |
| **Steam running and online** | — | The connection is Steam peer-to-peer. No port forwarding, no IP addresses, no router settings. |

## 1. Install RE_Kenshi

Download `RE_Kenshi_v*.zip` from the releases page above, extract it anywhere, and run
the installer inside. Then start Kenshi: the main menu should say
`RE_Kenshi v0.3.x - Kenshi 1.0.x - x64` near the version text. If it does not, RE_Kenshi
is not installed and nothing below will work.

## 2. Install KenshiCoop

Close Kenshi first — the updater refuses to run while the game is open.

1. Go to <https://github.com/CaptainVirgil/KenshiCoop/releases/latest>.
2. Download **`update-kenshicoop.ps1`**.
3. Open PowerShell and run it:

```powershell
powershell -ExecutionPolicy Bypass -File "$HOME\Downloads\update-kenshicoop.ps1"
```

It finds your Kenshi install (all Steam library folders, not only the default one),
downloads the mod, checks it against a published checksum, and installs it to
`<Kenshi>\mods\KenshiCoop`. Run the same command again any time to update. If it cannot
find Kenshi, add `-KenshiPath "D:\SteamLibrary\steamapps\common\Kenshi"`.

**About your saves.** The updater does not touch them. Kenshi keeps saves in
`%LOCALAPPDATA%\kenshi\save`, a completely different folder from `mods\`, and the script
refuses to run at all if the path it resolved is not `<Kenshi>\mods\KenshiCoop`. It also
copies your existing mod folder to `<Kenshi>\KenshiCoop-backups\<timestamp>` before
replacing anything, and keeps the last five.

**To undo an update:**

```powershell
powershell -ExecutionPolicy Bypass -File "$HOME\Downloads\update-kenshicoop.ps1" -Rollback
```

That restores the most recent backup. To remove the mod entirely, delete
`<Kenshi>\mods\KenshiCoop`. Nothing else is affected.

*(Playing on Linux through Proton? The repo has `scripts/linux/update-kenshicoop.sh`,
same behaviour, `--rollback` instead of `-Rollback`.)*

## 3. Confirm you are both on the same build

This is not optional. If your protocol versions differ the connection is rejected
outright — no fallback, and the failure just looks like "it won't connect."

When the updater finishes it prints something like:

```
  Build:  fork-1  (protocol 54)
```

Send that to the other player. The label and the protocol number must match. Both values
are also in `<Kenshi>\mods\KenshiCoop\kit.json` if you need them later.

## 4. Play

1. Launch Kenshi. In the **Mods** menu, tick **KenshiCoop**. (Do this once; it sticks.)
2. Press **F2**. The Co-op panel opens — at the main menu as well as in-game.
3. Both of you click **Copy my Steam ID** and send it to the other over Discord.
4. Copy the ID your friend sent you, then click **Paste friend's Steam ID**. The panel
   shows the last four digits of what it captured — check those against what they sent.
   This is per-session and is not saved, so re-paste after every Kenshi restart.
5. Leave **Transport** on **STEAM**.
6. **Host:** load a save (or start a new game), set **Role: HOST**, then switch
   **Connection** to **ONLINE**.
7. **Join:** from the main menu, no save needed. Set **Role: JOIN**, switch
   **Connection** to **ONLINE**. The host's world streams over and you load straight
   into it. Watch `Streaming host world... NN%` on the panel and in the top-left banner.
8. Switch **Connection** to **OFFLINE** to leave.

Rows in the panel marked `(switch)` are clickable. Steam IDs always display masked
(`****1234`) — the full number only ever goes via the clipboard.

**Starting fresh?** The mod adds two game starts to the New Game list, both the vanilla
Wanderer start with the two wanderers already split into separate squads, so the host
gets squad 1 and the joining player squad 2 with no manual tab-shuffling:

- **Multiplayer (Wanderer x2)** — plain, vanilla in every other way.
- **Wanderer+ x2** — the same, plus 500,000 cats and both characters at 50 in every stat.
  Kenshi has one wallet and co-op shares it, so that 500,000 is your combined purse.

If you use an existing save instead, it needs at least two squad tabs — move some units
into a second squad so the joining player has a crew.

## When it goes wrong

| Symptom | What it means | What to do |
|---|---|---|
| "The co-op plugin has not started", or F2 does nothing | RE_Kenshi did not load the plugin. | Check KenshiCoop is ticked in the Mods menu. Search `<Kenshi>\RE_Kenshi_log.txt` for `KenshiCoop`. Reinstalling RE_Kenshi usually fixes it. |
| `protocol mismatch` in the log | You are on different builds. | Both re-run the updater, then compare the build label and protocol number before trying again. |
| Won't connect over Steam | One Steam is offline, or an ID is wrong. | Both Steam clients must be running and **not** in offline mode. Each of you must have pasted the *other* person's ID. Look for `[steam] session ... active=1` in the log. |
| `Your Steam ID: (Steam not running)` | The plugin cannot see Steam. | Start Steam, then restart Kenshi. |
| "clipboard was not a Steam ID" | You copied something else. | Have your friend click **Copy my Steam ID** again and resend. |
| Updater: "Kenshi is running" | The game still has the DLL open. | Close Kenshi completely, including any hung `kenshi_x64.exe`, then re-run. |

**The log** is `<Kenshi>\KenshiCoop_host.log` or `KenshiCoop_join.log`, written line by
line as you play. One warning: the filename comes from whatever **Role** the panel was
set to, not the role actually negotiated. A file called `_host.log` can easily be the
joining player's. Confirm by looking for `[net] connected to host; sent HELLO` — that
line only appears in the join's log.

## What to expect

This is a hobby project and it is genuinely experimental. Desyncs happen. Crashes happen.
Two players is the design target; do not plan around more.

**The host owns the world.** They pick the save, and their copy is the truth. Saves are
shared: whenever either of you saves, that save is streamed to both machines and becomes
the one you both continue from. Next time, the host loads it and goes online, and you
reconnect from the main menu.

You each drive your own squad. Your friend's people are visible and synced on your screen
but take orders only from them.
