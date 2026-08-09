# Handoff — 2026-08-09

**Disposable.** Session state, half-built things, and machine state outside the
repo. Delete once the loose ends are closed. Durable things live in `CLAUDE.md`
(doctrine, build) and `docs/ROADMAP.md` (plan — including the P0 result).

---

## Read this first

**The historic `0x40000015`-before-RE_Kenshi abort is solved.** It was never
the game and never our plugin: Proton's lsteamclient bridge `_wassert`s
(`steamclient_main.c:375, Expression: "!status"`) when it cannot dlopen the
native `steamclient.so`. Every "dies before RE_Kenshi initialises" sighting —
the desktop second instance, all of CT 203 — was this or its sibling, the bare
prefix missing the VC++ 2010 runtime (`c0000135`). Both fixes are now encoded
in `scripts/linux/launch_coop.sh`; the CT 203 recipe is below.

**The first two-client session ran tonight, and then it was PLAYED.** The four
defects that found are in `docs/ROADMAP.md` with causes and measured
before/after — project state, not session state.

**Where the remaining desync stands.** Much reduced, not gone. What is left is
believed to be *load*, not a replication bug: the join drives the whole town
while the host drives nothing (cell authority cannot split a cell both players
stand in), and a frame-starved client publishes a lumpy stream, so the host's
copy of the join's character is the one that suffers. Roadmap Phase 3b holds
the four items that attack it. Two are designed in full and deliberately NOT
built — read the task notes before starting either, because the obvious version
of each is wrong:
- **the time budget** must not skip an entry wholesale (`drivenChars_` is
  rebuilt every tick, so a skipped body reads as *not driven* and the authority
  pass suppresses it — an invisible solid body);
- **the authority split** must split publish AND census together (splitting
  publish alone leaves the reconciler switched off on the half you gave away:
  double simulation).

---

## Repo state

- Branch `linux-build`, pushed. Tonight's commits, in order: roadmap items
  47/48 (doc + comment corrections), 45/46/49 (ENet pin + stamp,
  full-include-path header scan, Windows DOA checks), the entity-batch receive
  clamp + `netlinktest`, the session tooling, then the played-session fixes —
  inventory cadence, drive catch-up, mid-band keepalive, interp keepalive
  handling, and the throttle/keepalive-wire/harness-capture batch.
- Gate green after every change: `731 prototest / 17 tunneltest /
  46 netlinktest / 33 contract`, `RESULT: PASS`.
- **The installed DLLs in both mods folders are one build BEHIND `HEAD`** — they
  carry the keepalive but not the last two commits. Rebuild and reinstall before
  the next session (`scripts/linux/build_plugin.sh Release`, copy
  `build/Release/KenshiCoop.{dll,map}` into both `mods/KenshiCoop/`).
- The `KenshiLib_deps` stash from the 0.4.0 bump is **dropped** (0.4.0 settled:
  two green gates, fork-7 built+installed+now session-proven).
- `build_plugin_direct.ps1` gained DepsPin/ENet stamps + map check but has
  **never run on a real Windows machine** — that is the open half of parity.

## Machine state outside the repo

### Desktop — the two-client session (was running when this was written)

- Host: Steam install, launched via `launch_coop.sh hostdirect` (Proton
  Experimental, prefix COPY at `~/.local/share/kenshicoop-host-prefix`, so the
  real save tree was never written). Log: `<Kenshi>/KenshiCoop_host.log`.
- Join: `~/Kenshi-Join`, prefix copy at `~/.local/share/kenshicoop-join-prefix`.
  Log: `~/Kenshi-Join/KenshiCoop_join.log`.
- Both configs sit on `"transport": "udp"` (`.bak-presolo` backups beside them).
  **Flip to `steam` before a session with the brother.**
- Kill order if needed: kill the `RE_Kenshi/kenshi_x64.exe` wine processes BY
  PID. `pkill -f` self-matches its own shell — it fired three times tonight;
  use a `[k]` bracket pattern or exact PIDs, and remember wine cmdlines use
  backslashes.
- Seeded prefixes are disposable: the sentinel check reseeds them from the live
  prefix whenever `mfc100u.dll` is missing.

### Traps found tonight (not in any doc before)

- **Relative exe path to `proton` = silent exit** one line in. Absolute only.
- **Proton on the LIVE compatdata while Steam runs = same silent exit.** Use a
  seeded copy.
- **Runner must match the seeding prefix.** The old `tail -1` runner pick chose
  UMU-Proton-9 against an 11.0 prefix — the "invalid version" corruption trap.
- **Backgrounded gate + piped output can hang after PASS**: leftover toolchain
  wine service processes (`services.exe`, `winedevice.exe`, `mspdbsrv.exe`)
  inherit the pipe and never exit. Kill by exact PID; the result was fine.

### fredj CT 203 (`kenshi-join`, 10.110.110.24) — one dialog short

Working launch chain (script at `/root/launch_proton.sh` in the CT):
`/opt/slr4/run -- /opt/proton-experimental/proton runinprefix ./kenshi_x64.exe`
with `unset DBUS_SESSION_BUS_ADDRESS`, `XDG_RUNTIME_DIR=/run/user/0`,
`~/.steam/sdk64/steamclient.so -> /opt/steamclient/linux64/steamclient.so`,
prefix at `/opt/compatdata/233860` (copy of the desktop's), everything
chowned `100000:100000` (unprivileged CT; rsync writes host-side uids).

It ends at Kenshi's own **"Steam dll error"**: `SteamAPI_Init` needs a running
Steam client and the CT has none. Decision (roadmap "Open decisions"): Steam
login in the CT / DRM-free build / `pct destroy 203`. `onboot: 0` still —
container does not survive a fredj reboot unless started.

### ydotoold

Still running as root from 2026-08-08 (`/tmp/.ydotool_socket`, dies with
reboot). Untouched tonight — the whole session was env-var-driven, no clicks.

## Known crash, reproducible

Talking to a **seated** NPC on the join crashed it twice (second attempt was
fatal), at 2x game speed. Unhandled C++ exception `0xE06D7363` thrown inside
the engine's dialogue path; **no KenshiCoop frame on the stack**, so the plugin
did not throw — but the line before it in the log is
`[census] FREEZE hand=... name='Holy Sentinel'`, i.e. we had suspended that
NPC's AI, and the engine's dialogue state machine appears not to tolerate a
frozen partner. Roadmap item 55; wants an interaction hold (a body in dialogue
with the local player is exempt from freeze/park/cull until it ends, same latch
shape as `xferLatch_`). Dump kept at `~/Kenshi-Join/crashDump1.0.65_x64.dmp`.

## Loose ends

- [ ] **Rebuild + reinstall the DLL** — both installs are one build behind HEAD
- [ ] Play again and re-read both logs against the `kenshicoop-logs` skill;
      relaunch is `launch_coop.sh hostdirect` then
      `KENSHICOOP_AUTOCONNECT=1 launch_coop.sh join`
- [ ] Decide fork-8 (everything since fork-7 is unreleased; wire unchanged,
      protocol 54)
- [ ] Decide CT 203 (Steam client / GOG / destroy)
- [ ] Flip both `coop_config.json` back to `steam` before a brother session
- [ ] Windows: run `build_plugin_direct.ps1` + `verify.ps1` once on a real
      Windows machine (brother's?) to exercise the ported checks
- [ ] Delete this file when the above are closed
