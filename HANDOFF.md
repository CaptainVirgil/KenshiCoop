# Handoff — 2026-08-11 (overnight, unattended)

**Disposable.** Session state, half-built things, and traps that would cost the
next session an hour. Durable things live in `CLAUDE.md` (doctrine, build) and
`docs/ROADMAP.md` (plan, and the audit's hold list).

---

## Read this first

**v0.62 is released, installed on this machine, and unplayed.** Protocol 58 —
unchanged from v0.61 — so it is **not a hard cut**: a v0.61 client and a v0.62
client still connect. Both should still update, because two of the fixes only
work when both ends have them.

The brother needs to run his updater; he does not need a new updater script.

Everything below came from the 2026-08-10 evening session logs and an audit run
after the players went offline. **None of it has been seen in a running game.**

---

## Repo state

Branch `linux-build`, pushed. Four commits tonight, gate green after each:

| | |
|---|---|
| `bd2a0ea` | carry-via-pointer, publish sub-phasing, two loop bounds, origin-drop guard |
| `3449fc0` | scenario telemetry out of Release builds |
| `57db44d` | per-stage cost table, log line-rate counter, `addAiSuspend` guard, `resetSession`, config knobs |
| `a4e3780` | `frzAct=` and `truncHold=` counters |

Release: <https://github.com/CaptainVirgil/KenshiCoop/releases/tag/v0.62>
(five assets, matching v0.61's set). Local install verified by the updater.

---

## The freeze — what is now known, and what is not

The 23:14 freeze was **not** like the earlier three. The watchdog caught it in
`phase='publish'` — our code, not Kenshi's tick — and the main thread **never
recovered**: `beats=376716` identical across both watchdog reports and still
frozen 25 s later, while the net thread kept logging and inbound held at
47 KB/s undrained.

**Localised, not solved.** The stall onset back-dates to 23:14:05.846, which is
exactly the timestamp of the last main-thread line, `[wd] DROP id=21`. So the
wedge is at or after `detectAndPublishWeaponDrops`.

Two audit hypotheses were **killed by the log**, which is worth as much as the
fix that did land:

- *Event storm → unbounded `evt_` drained by `applyEvents`.* Ruled out:
  `applyEvents` runs **before** the weapon-drop publish, and `[wd] DROP id=21`
  was emitted, so that frame's `applyEvents` completed.
- *Log-flush storm (44k lines ≈ 21.9 s under Wine).* Ruled out: the line rate
  was flat at ~140/s for the 90 s before the wedge. No spike.

What shipped is instrumentation to name it next time, plus caps on the two
unbounded main-thread loops found on the way (neither proven to be the cause).

**If it recurs, the log now names the stage.** 28 stages each stamp their own
beat.

---

## What to watch, in priority order

1. **`[stage] lines=N/10s peak us: ...`** — new, every 10 s. First real numbers
   on where the frame goes. Sorted by peak, not mean, because the once-a-minute
   spike is the thing that becomes a freeze. Expect `pub:worldItems`,
   `pub:inventories` and `pub:owned` near the top; if something else is, that is
   news.
2. **`snapVeto=` vs `snapCbt=` on `[ai]`.** This decides an open question. The
   indoor-combat veto only fires within 20 u, but the sighting that motivated it
   was gap=38–75. **If `snapVeto` stays near zero while `snapCbt` climbs, the
   ceiling is wrong** — and the fix is to raise it, since `readCombat` is doing
   the real discrimination. Do not raise it before seeing the numbers.
3. **`[wd] DROP ... pos=`** — should no longer be `0.00,0.00,0.00`. Two of 21
   were, last session. If the item desync persists *without* origin drops, it is
   a different bug.
4. **`frzAct=`** — the census freeze's real actuation count. 2,140 log lines
   last session stood for an unknown, much larger number. If this is enormous,
   the freeze/park loop is the next thing to attack.
5. **`[wd] WARNING implausible drop delta=`** and **`[speed] WARNING intent
   drain hit its cap`** — either one firing means a bound I added was actually
   needed, which would also name the freeze.
6. **`truncHold=`** — culls suppressed because the peer's census was capped.

---

## The escape hatch, if a session goes wrong mid-play

`coop_config.json` in the mod folder now reaches two knobs that were
environment-variable-only (and therefore unreachable under Steam):

```json
"censusPark": 0,
"censusFreezeAi": "0"
```

`censusPark: 0` disables the divergence teleport **and** the census freeze at
all four call sites. That is the single most likely thing to want to turn off if
NPC behaviour degrades. It costs more visible divergence between the two
worlds — that is the trade.

---

## Traps and loose ends

- **`make_kit.sh` builds Release twice** (a duplicated block, lines ~33–50).
  Harmless — the second is an incremental no-op — but it prints the banner
  twice, which looks like a bug. Deliberately not touched on release night.
  Delete one block.
- **`dormPc=1 pcs=3`** in the last `[audit]` line. The glossary says `dormPc`
  **must be 0**, and `pcs=3` makes it a valid measurement, so this is a real
  violation nobody has chased: a dormant body inside the attention radius of a
  player character.
- **The freeze→park feedback loop is undiagnosed.** `[proxy] drift` shows the
  local copy pinned at a fixed position while the host's walks away, drift
  growing until a park teleports it. Freezing a body means it can never follow,
  so divergence grows by construction. 3,545 freezes and 1,175 parks in 25 min.
  Whether that is the design working or eating itself is an open question — do
  not "fix" it without deciding which.
- **Mods still differ.** Same 60 mods, different order: `2d49eaad` here vs
  `4f1e54f1` there. The panel says `MODS DIFFER (both 60 - check ORDER)`. He
  should send his `mods.cfg`.
- **Task #34 (buy-probe) is still open** and needs one deliberate in-game
  purchase on a Harness build. `scripts/linux/buyprobe.sh on|read|off`.

---

## The audit's hold list

About a dozen further candidate fixes were found and **deliberately not
shipped**. They are in `docs/ROADMAP.md` under "Held for daylight", each with
the evidence it needs first.

The rule that decided it, worth keeping: `drivetest` links `ReplicatorDrive`,
`ReplicatorCore` and `Interp`, so a fix there is gate-testable.
`ReplicatorAuthority`, `ReplicatorPublish`, `ReplicatorItems` and
`ReplicatorChannels` are **linked by no test at all** — and all four regressions
the players caught this week were in that second group. Extending the harness to
one of those files is worth more than any single fix on the held list.

---

## One thing that nearly shipped broken

Sub-phasing the watchdog meant prefixing `coop::mainThreadBeat("pub:x");` onto
each stage. Twelve stages were **braceless guarded statements**, so the prefix
made the *beat* the guarded statement and the publish unconditional — every
config gate in the tick silently defeated. The compiler caught exactly one of
the twelve (the only one with an `else`); the full C++ gate passed on the other
eleven, because nothing in it exercises a disabled feature.

Caught by scanning for the pattern rather than by any test. There is now a
contract check for it, verified by re-injecting the regression in both forms.

**The lesson is the one this project keeps re-learning: a green gate is not a
launched game.**
