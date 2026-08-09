# Handoff — 2026-08-08

**Disposable.** This is session state, not project state: half-built things,
machine state outside the repo, and traps that would cost the next session an
hour to rediscover. Delete it once the loose ends below are closed. Durable
things live in `CLAUDE.md` (doctrine, build) and `docs/ROADMAP.md` (plan).

---

## Read this first

**Every fork build before fork-6 was dead on arrival.** Both hand-rolled build
scripts read the vcxproj's `<ItemDefinitionGroup>` for flags and never its
`Label="Configuration"` `<PropertyGroup>`, so `<WholeProgramOptimization>` was
never applied. Without `/GL`, KenshiLib's member-function stubs become local
functions, `&GameWorld::_NV_mainLoop_GPUSensitiveStuff` resolves inside our own
DLL, and `KenshiLib::GetRealAddress` asserts before the plugin finishes loading.
fork-1..fork-5 could not load. Nobody noticed because the session everyone
remembers was *upstream's* release, and the fork builds were published without
being launched.

The commit gate stayed green throughout. So did the Linux/Windows byte-parity
check — it only ever proved both paths were wrong the same way.

**A green gate is not a launched game.** The build now refuses to emit a DLL whose
main-loop hook is not an import, and the deps stamp must appear in the binary.
Both checks were falsified against known-bad inputs before being trusted.

---

## Repo state

- Branch `linux-build`, **7 commits ahead of `origin/linux-build`** — not pushed.
- Working tree clean.
- `fork-6` is the public release (3 assets). `fork-7` is built and installed
  locally but **not released**. They interoperate: the handshake compares
  `PROTOCOL_VERSION` (54), not the build label.
- `fork-5` was deleted from GitHub, tag included — it never loaded.

### Deps are bumped, and there is a stash

`third_party/KenshiLib_deps` now sits at **`b566d74` (KenshiLib 0.4.0)**, matching
the runtime RE_Kenshi ships. It was pinned at `e75769b` (v0.1) for this fork's
whole history on a justification that was never retested.

There is a stash holding the pre-bump working tree:

```
git -C third_party/KenshiLib_deps stash list
# stash@{0}: On (no branch): patch_vendored_headers output pre-0.4.0-bump
```

That stash is the **old** patched headers. Do not pop it onto the new checkout —
it belongs to `e75769b`. Drop it once you are happy with 0.4.0.

The deps checkout reads `-dirty` in the build stamp. That is **correct and
expected**: `patch_vendored_headers.sh` mutates it by design.

---

## Machine state outside the repo

### `~/Kenshi-Join` — second Kenshi install (12 GB, btrfs reflink)

Created by `scripts/linux/setup_join_install.sh`. Own saves/settings/logs, shares
extents with the Steam install so it costs almost nothing. Has fork-7 installed.
Both installs were switched to `"transport": "udp"` for loopback testing —
originals saved beside them as `coop_config.json.bak-presolo`. **Switch back to
`steam` before playing with the brother.**

### `ydotoold` is running as root

Started by hand for the Wayland pointer control used to click Kenshi's launcher:

```
setsid sudo ydotoold --socket-path=/tmp/.ydotool_socket --socket-own=$(id -u):$(id -g) &
export YDOTOOL_SOCKET=/tmp/.ydotool_socket
```

Not installed as a service on purpose — it dies with a reboot. `ydotool` itself
was installed with pacman and stays. **Trap:** `ydotool mousemove -a` lands in the
wrong place on this multi-monitor setup (asked for 1075,806 → got 230,1079). Use
`hyprctl dispatch movecursor X Y`, verify with `hyprctl cursorpos`, and use
ydotool only for the click. Written up in
`~/.claude/skills/virgils-machine/watching-gui-apps.md`.

### fredj (10.110.110.20) — CT 203 `kenshi-join`, HALF BUILT

A container intended as a permanent second test client. **`onboot: 0`, so it does
not survive a reboot of fredj unless started.** Fully reversible with
`pct destroy 203`; nothing else on the host was touched, and `.24` was verified
free in both `fqdn-index.md` and on the wire before use.

Working:

- Debian 12, 8 cores, 8 GB, 40 GB on `A01-roci`, IP `10.110.110.24`
- GPU passthrough copied verbatim from CT202's config (ADR 0004 cgroup pattern)
- `nvidia-smi` sees the Quadro P620; **Vulkan reaches it on the NVIDIA driver**
- Kenshi 12.4 GB at `/opt/Kenshi` (rsync'd, 1m48s at 108 MB/s)
- Wine 8.0 + WoW64, DXVK 2.3.1, VC++ 2010 runtime, Xvfb `:99`, x11vnc on `:5900`

**Important:** GLX under Xvfb falls back to llvmpipe, so **DXVK is the only viable
path** — D3D11 → Vulkan → P620. WineD3D would give software rendering, and a
frame-starved client produces false sync telemetry, since the drive and interp
math are frame-rate coupled.

Not working: Kenshi aborts with `0x40000015` (fatal app exit) before RE_Kenshi
initialises — no `RE_Kenshi_log.txt` is ever written.

**Next step, and the reason to stop hand-assembling a prefix:** rsync
`Proton - Experimental` (~1.5 GB) from the desktop into the container and run
Kenshi under it. Proton already has the runtime, DXVK and prefix layout that
demonstrably work for Kenshi on this hardware. Hand-building a Wine prefix to
match it is the wrong fight.

---

## The second-instance problem, and what was actually ruled out

Launching a second Kenshi on the desktop failed four ways. Do not re-derive this:

| tried | result |
|---|---|
| `proton run` | exits when the shim execs away |
| `proton waitforexitandrun` | same |
| Proton + skip the shim (`RE_Kenshi/kenshi_x64.exe --norestart`) | starts, dies before RE_Kenshi |
| plain Wine + skip the shim | identical |

**Ruled out:** hardcoded paths (the clone's `RE_Kenshi.ini` is byte-identical),
prefix corruption, Proton runner choice, missing Steam app identity (that part
worked — `AppID = 233860`, Steam ID cached).

**A theory that turned out to be wrong, recorded so it is not re-adopted:** Steam
refusing a second concurrent instance. The container has *no Steam at all* and
failed the same way.

**What it actually is, at least in part:** the **VC++ 2010 runtime**.
`mfc100u.dll not found`, status `c0000135`. Steam/Proton prefixes install it
automatically; a bare Wine prefix does not. Fixing it in CT203 got the game
further — from a missing-import failure to reaching game code. That is the third
VC++ 2010 coupling found today, after `cl.exe` importing MSVCR100 and the
`std::string` ABI constraint.

Two useful facts for whoever picks this up:

- RE_Kenshi keeps a **patched second exe** at `RE_Kenshi/kenshi_x64.exe`; the
  top-level exe is a shim that execs it with `--norestart`.
- `pgrep -f` self-matches your own command line — it reported Kenshi running when
  it was not, twice. Use `pgrep -x`. Confirmed correct against a live Wine Kenshi.

---

## Mistakes made this session, so they are not repeated

- **`pkill -f "proton waitforexitandrun"` killed the running host.** Steam
  launches games through that same verb. Match on something narrower.
- Switching Proton runners on an existing prefix corrupts it (`invalid version`).
  Delete the prefix when changing runner.
- `strings dll | grep -qF x` under `set -o pipefail` **fails on success** — grep
  exits at the first match, `strings` takes SIGPIPE, pipefail reports failure.
  Cost a full rebuild cycle to chase. Use `grep -qa` on the binary directly.
- `#if __has_include(...)` is C++17 and **silently evaluates to 0** on this C++03
  compiler, so a guarded include vanishes and the build still reports success.
- A wait loop that greps a log matched **leftover content from a previous run**
  and reported a build verified that had never loaded. Check the mtime, or move
  the file first.

---

## Loose ends

- [ ] `git push origin linux-build` (7 commits)
- [ ] Decide: release fork-7, or fold Phase 1 in and cut fork-8
- [ ] Restore `"transport": "steam"` in both installs before a Steam session
- [ ] Drop the `KenshiLib_deps` stash once 0.4.0 is settled
- [ ] Finish or destroy CT 203 (`pct destroy 203`)
- [ ] Delete this file when the above are done
