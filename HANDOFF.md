# Handoff — 2026-08-09 (early hours)

**Disposable.** Session state, half-built things, machine state outside the
repo, and traps that would cost the next session an hour. Delete once the loose
ends close. Durable things live in `CLAUDE.md` (doctrine, build, the new
channels) and `docs/ROADMAP.md` (plan).

---

## Read this first

**The fork was played by two clients for the first time, and it found more in
one hour than every automated signal this project has.** All of it is fixed,
gated and pushed. The details with measured before/after are in
`docs/ROADMAP.md` — that part is project state, not session state.

**Nothing below has been seen in a running game.** The last build was gated
(733 prototest / 17 tunneltest / 46 netlinktest / 33 contract) and installed,
but the session ended before it was launched. Treat every item in "what to
watch" as unverified.

---

## Repo state

- Branch `linux-build`, pushed and current.
- **Protocol is now 56** (was 54 all session, unchanged since fork-1). Two wire
  changes: weather (55) and dialogue (56). Both ride the same unreleased cut, so
  the cost is one hard update, not two.
- Gate green after every change: `733 / 17 / 46 / 33`, `RESULT: PASS`.
- Both installed DLLs match `build/Release` — verified by sha256.

## What to watch on the next launch

Relaunch is `scripts/linux/launch_coop.sh hostdirect` then
`KENSHICOOP_AUTOCONNECT=1 scripts/linux/launch_coop.sh join`.

| Watch | Signal | Meaning |
|---|---|---|
| Town NPCs marching in place | `[interp] .. haltDrv=N` | The freeze-vs-drive collision. Climbing = being prevented; 0 = gone |
| Where the frame goes | `[budget] pub .. apply ..` | First per-frame cost numbers this project has had. `pub` = publish (authoring client), `apply` = drive |
| Weather | `[weather] SEND` then `[weather] APPLY` | SEND with no APPLY = the sid did not resolve, line deliberately dropped |
| Dialogue | `[dlg] SEND` / `[dlg] RECV` | RECV with no visible caption = nothing within 40 u of the reported spot |
| Damage numbers | floaters on suppressed hits | Guard now draws what the skipped hit path would have |

**The riskiest two, because they write engine state directly:**

- **Weather** writes `WeatherInstance` fields at hardcoded offsets plus
  `requestUpdateEffects`. Whether the *renderer* follows is unproven — the
  engine's own `setupWeather` is private and was not called.
- **Dialogue** hooks two `DialogueSpeechBubble` methods. Whether they catch
  *every* speech path (shouts, "important" lines) is unproven.

**Off by default, needs deliberate testing:** the hand-hash authority split.
`KENSHICOOP_SPLIT_AUTHORITY=1` on **BOTH** clients — each computes the partition
independently, so a one-sided enable has both claiming the same bodies. The A/B
worth running is one session off, one on, comparing the `[budget]` numbers and
whether the host starts driving (`drv>0` in its audit line).

## Machine state outside the repo

- Both installs sit on `"transport": "udp"` (`.bak-presolo` beside them).
  **Flip to `steam` before a session with the brother.**
- Seeded Proton prefixes at `~/.local/share/kenshicoop-{host,join}-prefix`.
  Disposable — the sentinel check reseeds from the live prefix when
  `mfc100u.dll` is missing.
- `ydotoold` still running as root from 2026-08-08 (`/tmp/.ydotool_socket`,
  dies with a reboot). Untouched — the whole session was env-var driven.
- **fredj CT 203** unchanged: launch chain proven (SLR4 pressure-vessel +
  `proton runinprefix` + `~/.steam/sdk64` link), stops at Kenshi's own "Steam
  dll error" because `SteamAPI_Init` needs a running Steam client. `onboot: 0`.

## Traps found this session

- **`pkill -f` self-matches its own shell.** Fired five times, twice after it
  was written down, once killing two long gate builds. Bracket the pattern
  (`pgrep -f "[k]enshi"`) or resolve PIDs first. Wine cmdlines use backslashes,
  so forward-slash patterns silently miss.
- **A relative exe path to `proton` exits silently** one line into wine init.
  Absolute only.
- **A second Proton session on the LIVE compatdata** exits the same silent way.
  Seed a copy.
- **The runner must match the seeding prefix** — an older Proton against an
  11.0 prefix is the "invalid version" corruption trap.
- **Backticks in a `git commit -m` heredoc get shell-substituted.** One commit
  message lost a word that way and had to be amended.

## Loose ends

- [ ] Launch and work through "what to watch" above
- [ ] Decide fork-8. Everything since fork-6 is unreleased and protocol is now
      56, so it is a hard cut. **Item 28's census `truncated` flag should land
      before the release** rather than waiting for a bump of its own — the bump
      is already spent
- [ ] Flip both configs back to `"transport": "steam"` for a brother session
- [ ] Decide CT 203 (Steam client / GOG build / `pct destroy 203`)
- [ ] Windows: run `build_plugin_direct.ps1` + `verify.ps1` once on a real
      Windows machine — the ported DOA checks have never executed there
- [ ] Delete this file when the above are closed
