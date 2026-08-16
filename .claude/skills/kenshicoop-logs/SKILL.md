---
name: kenshicoop-logs
description: Read and diagnose a KenshiCoop session log — interpret the [audit], [interp], [census], [cell], [door], [carry], [furn] and [snap] lines, and turn a player complaint (desync, teleporting, duplicates, flapping doors, inverted actions, blocked stairs) into a root cause. Use whenever debugging a live or recorded co-op session.
---

# Reading a KenshiCoop session log

Logs live at `<Kenshi>/KenshiCoop_*.log`, one per client, flushed per line so the last
line before a crash is real. `<Kenshi>` is
`~/.local/share/Steam/steamapps/common/Kenshi`.

**The filename lies, and so does the per-line tag.** Both come from the configured role
(`Config.cpp`), not the negotiated one, so a `KenshiCoop_host.log` full of `[HOST]` lines is
regularly the join's. Establish the role from the handshake, never from either label:

```bash
grep -m2 "connected to host; sent HELLO\|peer connected id=\|local id=" "$L"
```

`connected to host; sent HELLO` and `local id=1` mean **this client is the join**; the
peer at id=0 is the host.

Never read these raw — they run to tens of thousands of lines. Aggregate first.

## First pass

```bash
L=~/.local/share/Steam/steamapps/common/Kenshi/KenshiCoop_host.log
head -1 "$L"; tail -1 "$L"                              # session span
grep -m1 "KenshiCoop: interp delay" "$L"                # the tuning line
grep "\[interp\] lerp=" "$L" | tail -1                  # cumulative motion health
grep "\[audit\] exist" "$L" | tail -1                   # world-state health
grep "\[census\] sent" "$L" | tail -3                   # what we are publishing
grep -c ERROR "$L"
```

Collapse repeated shapes to find what dominates:

```bash
grep -oE "(ERROR|WARN|INFO): \[[a-z-]+\][^0-9]{0,60}" "$L" |
  sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn | head -20
```

## `[interp]` — motion health (cumulative counters)

`lerp` `extrap` `clamp` `seg` `single` `snapSq` `snapNpc` `reissueSq` `reissueNpc`
`restFlip` `delay` `jit` `starve` `snapMid` `restFlipMid`

- `extrap / lerp` is the headline. Healthy is a few percent. **25%+ means the stream is
  starving** — bodies are being dead-reckoned because no sample arrived in time.
- `delay` is the adaptive render delay in ms; compare against the `interp delay=A-Bms`
  band on the tuning line. Sitting far above the band means the cadence controller is
  compensating for a slow send rate, not for jitter.
- `jit` well above RTT means a bursty path (Steam relay).
- `starve` counts buffer underruns.

```bash
python3 - "$L" <<'EOF'
import re,sys
rows=[dict((k,float(v)) for k,v in re.findall(r'(\w+)=(-?\d+\.?\d*)',l.split("[interp] ")[1]))
      for l in open(sys.argv[1],errors="replace") if "[interp] lerp=" in l]
last=rows[-1]
print(f"extrap/lerp = {100*last['extrap']/max(1,last['lerp']):.1f}%")
for k in ('delay','jit','starve','snapSq','snapMid','reissueNpc'):
    print(f"{k:<10} last={last[k]:.0f} max={max(r[k] for r in rows):.0f}")
EOF
```

## `[snap]` — hard position corrections

`[snap] squad|cover|mid hand=.. name='..' gap=N gate=N srcVel=N ... skipped=N`

`gap` is how far the local copy was from the authoritative position when it was snapped,
in world units, against the `snapDist` on the tuning line (default 8 u). Distribution
matters more than count:

```bash
grep -oP "\[snap\] \w+ hand=[\d,]+ name='[^']*' gap=\K[\d.]+" "$L" |
  sort -n | awk '{a[NR]=$1} END{print "n="NR, "med="a[int(NR/2)], "p90="a[int(NR*0.9)], "max="a[NR]}'
```

A median in the hundreds means bodies are running on local AI between updates — look at
the mid band next, not at the snap logic.

## `[census] sent` — what this client publishes about NPCs

`n=` rows sent · `radius=` census radius · `mid=` size of the mid band · `anchors=` ·
`enum=` bodies enumerated · `notmine=` skipped because another client authors that cell ·
`proxyrow=` · `attnR=` attention radius.

- `mid=` pinned at its cap with `enum=` far larger means **far NPCs are getting no motion
  updates at all** — the cap is `tuning_.midBandMax`, and everything past it runs on the
  peer's local AI until a census beat snaps it.
- `notmine=` equal to `enum=` means this client authors nothing here — normal when the
  peer owns the cell, but if *both* logs say it, nobody is publishing.

## `[audit] exist` — world-state health

| Field | Meaning |
|---|---|
| `near` / `wide` | bodies enumerated in the ~200 u stream bubble / out to the census radius |
| `drv` | streamed and driven by the peer this tick |
| `cen` | census-present but unstreamed — a local-sim copy the peer vouches for |
| `hid` | **we suppressed this one** (invisible; historically still solid) |
| `ghost` | census-absent but observed and not suppressed — visible on one client only; should be transient |
| `supp` | size of the suppressed map (superset of `hid`) |
| `census` | hands in the peer's latest existence claim |
| `parks` | cumulative divergence teleports; five figures means chronic divergence |
| `staleMs` | cumulative ms that **wide culling has been off** |
| `edges` | how many times culling dropped out — each edge leaks un-judged bodies |
| `nearCap`/`wideCap` | enumeration hit its buffer, so absence stopped meaning absent |
| `dormPc` | dormant bodies within the attention radius of a player character — **must be 0**, but see `pcs` |
| `pcs` | player characters the `dormPc` check was measured against. **`pcs=0` voids `dormPc`** — zero violations out of zero subjects is not a pass |
| `mine` / `skip` | bodies in cells we author / left unjudged because another author holds them |
| `cells` | cells we currently claim |
| `fresh` | census is ≤5 s old; when 0, `wide` is 0 and nothing is being culled |
| `ghostMax` / `ghostEdge` | furthest ghost seen, and how many sit in the outer band next to the un-enumerable horizon |
| `dorm` / `attnR` | attention-gated bodies, and the radius used |

## `[cell]` — presence authority

`CLAIM` (we claimed a cell) · `RECV` (the peer claimed one) · `MAP cells=N slots=N X,Y=owner`
· `yield` (the peer is streaming this body, so we stop authoring it).

`MAP` is the ground truth for "who owns where". **Host wins a contested cell**, so two
players standing in one cell means the join publishes nothing and everything depends on
the host's bands.

## Symptom → first place to look

| Player says | Look at |
|---|---|
| "enemies at 0 blood won't go down" | **Ask first: are they down on the OTHER player's screen?** Up on BOTH is not a sync bug - it is the AI suspend skipping the engine's per-character update. `frzAct=` / `frzHurt=` |
| "people are duplicating over and over" | `[spawn] proxy FAILED` and repeated `[spawn] REQ` for one hand. A mint whose liveness check false-negatives destroys the body and lets the request re-arm |
| "health syncs for my squad but not NPCs" | `noBody=`. A body that exists here only as a minted proxy cannot be addressed by `resolveCharByHand` |
| "everything is out of sync / teleporting" | `[interp]` extrap ratio, then `[census] sent mid=` against `enum=` |
| "things are duplicated" | `[audit] staleMs`/`edges` (culling off ⇒ local copy + minted proxy coexist), then `[spawn] census-missing`. Rule out a shared save folder first — `docs/REPLICATION_PITFALLS.md` §13 |
| "I can't get up the stairs, he can" | `[audit] hid`/`supp` — a suppressed body is invisible but was solid and cannot be shoved |
| "doors keep opening and closing" | `[door]` RECV/SEND alternating on one `hand=` |
| "he picked someone up but actually put them down" | `[carry] HEAL PICKUP` right after a drop — a self-heal reading the delayed stream |
| "prisoner jumps back into the cage" | `[furn] HEAL ENTER` following an exit; same bug class |
| "can't connect" | `[net] protocol mismatch`, then `[steam] session ... active=` |

## Lines added by the fork

Signals that do not exist upstream, and what each means when you see it.

| Line | Meaning |
|---|---|
| `[authority] restore MISS hand=..` | An un-hide failed at the engine call. The entry is deliberately kept so the ~2 s sweep retries — a body still invisible after several of these is a real problem. |
| `[leave] restored suppressed=N despawned=M` | Peer-leave teardown put N hidden bodies back; M had already been despawned by zone streaming. Before this existed, every hidden body stayed hidden and solid for the rest of the world. |
| `[census] re-judge sweep radius=..` | The census went stale and came back; one widened sweep runs to reach bodies that drifted out while nothing was being judged. |
| `[furn] SHACKLE RELEASE occ=..` | The stream stopped asserting `BODY_CHAINED`, so the local copy was unchained. This direction did not exist before — a caged prisoner's shackle emits no event either way, so a stale batch could re-lock one permanently. |
| `[save] REJECT begin: peer sent an unusable save name` | A peer's save name failed validation. It selects a directory that gets deleted, so this is refused rather than sanitised. |
| Panel: `Version mismatch: your friend is on protocol vN…` | The rejection is now surfaced instead of retried silently forever. |
| `[caps] N of 17 enabled features are missing their engine capability` | Printed once at startup. **`0` is the healthy line — read it first.** Any other number means a `[caps] WARNING` line above names a feature that will no-op in complete silence, which is otherwise undiagnosable: nothing errors, the feature simply never happens. |
| `[caps] WARNING <x>Sync is ON but the '<cap>' engine capability did not resolve` | That specific feature is dead in this build. It reports rather than disabling the feature, because the capability table is fail-closed and could be wrong in the safe direction. |
| `unknown packet type=N len=M total=K` | The two clients are not running the same build, or the stream is being corrupted. There is **no other symptom** for either — this line is the whole diagnostic. Rate-limited: once per distinct type, plus a 10 s rollup. |
| `[ko] RELEASE hand=..` | A KO latch was cleared because the owner's stream kept reporting the body upright and no `EVT_REVIVE` ever arrived. Occasional is fine. A stream of them means revives are being lost, which is worth chasing. |
| `[trust] .. koRel=N koExp=M` | `koRel` is the cumulative count of the above. `koExp` counts latched `targets_` entries dropped after ten minutes with no stream at all — non-zero means bodies are being abandoned mid-latch, which is normal after travel and suspicious while stationary. |
| `[build] MINT-RETRY OK key=.. try=N` | A building the peer placed could not be minted here on the first attempt (usually an unloaded zone) and succeeded on retry N. Purely informational. |
| `[build] MINT-RETRY GIVEUP key=.. sid='..'` | **Player-visible failure.** That building will never appear on this client. The sid is on the line; it usually means the template does not exist in this install. |
| `[authority] suppress MIGRATE REFUSED hand=.. (sid witness missing/disagrees)` | A hidden body's hand changed but its template sid did not match what was recorded when it was hidden, so the hide was dropped and the body restored instead of being moved onto what may be a different NPC. Rare; a run of them means bodies are being recycled underneath the suppression set. |
| `[role] panel role is X; log renamed to ..` | The panel role differed from the configured one and the log file was renamed to match. If it says `COULD NOT be renamed`, the file name is wrong but logging continues at the old path. |
| `[budget] pub avg=..us max=..us apply avg=.. max=.. frames=N over2ms=N` | **The first per-frame cost measurement this project has had.** `pub` is publish cost (paid by whichever client AUTHORS the bodies in interest), `apply` is drive cost (paid by whichever DRIVES them). With both players in one cell those are the same client for the whole town, which is how one machine stutters while the other idles. The mean hides the spikes that are felt — read `max` and `over2ms` together. |
| `[interp] .. haltDrv=N` | Times the census freeze or proxy reconcile declined to halt a body because the drive was walking it. This collision used to freeze driven NPCs for up to 20 s at a time (97% of active frames, measured) and is what "NPCs march in place" was. **Climbing = the bug is being prevented; 0 = it no longer occurs.** |
| `[inv] resent N unchanged snapshot(s) in 60s` | The inventory channel's periodic re-assert. Healthy is roughly one per container per 5 s (30 s for big ones), so a low-double-digit number. Hundreds means the resend cadence has collapsed again — that shipped once and flooded the reliable channel for a whole session, silently, because the SEND log only covers content-change. |
| `[weather] SEND '<sid>' str=.. eff=.. end=.. season=..` | Host published a weather change (protocol 55). Change-gated, so a settled sky is silent. |
| `[weather] APPLY '<sid>' ..` | Join wrote that weather onto its own active biome region. Logged only on a real EDGE — a matching sky prints nothing. If SEND appears and APPLY never does, the sid did not resolve in the receiver's season table and the line was deliberately dropped (a stale sky beats a wrong one). |
| `[dlg] SEND '<text>'` / `[dlg] RECV '<text>'` | A spoken line captured locally / shown from the peer (protocol 56). Symmetric — both clients do both. If RECV appears with no visible caption, nothing was within 40 u of the reported position, so the line was dropped rather than floated in mid-air. |
| `[dmg] suppressed-hit damage floaters ON` | The guard is drawing its own damage numbers, because skipping the engine's hit path also skips the number it would have drawn. |
| `[cell] contested-cell authority SPLIT by hand hash` | The hand-hash authority split is enabled (off by default). **Both clients must show this line** — each computes the partition independently, so a one-sided enable has both claiming the same bodies. |

Healthy-session expectations after the fork's fixes: `mid=` should climb well past 48,
`staleMs` should stay small, `parks` should not reach five figures, the `[door]`
channel should not alternate RECV/SEND on one hand, `[caps]` should read `0 of 17`, and
there should be no `unknown packet type` lines at all.

## Lines added 2026-08-15/16 (v0.62-v0.66)

These are the newest signals and several of them exist because a bug was
invisible without them. Read `[stage]`, `[q]` and `[net] mix` together: they
answer "was a stage SLOW, or was it HANDED forty thousand items", which nothing
could distinguish before.

| Line / field | Means |
|---|---|
| `[stage] lines=N/10s peak us: a=max/avg ...` | The five most expensive main-thread stages by PEAK, every 10 s. 42 stages each stamp their own name (28 publish + 14 apply). Sorted by max because the once-a-minute spike is what becomes a freeze. `frame-end` is Kenshi's own render and is normally the largest by far - measured ~9.9 ms avg, against a few hundred us for everything of ours combined |
| `[q] peak evt=.. ent=../4096 ..` | Inbound queue high-water marks per window. `DROPPING ent=N` appears only while it is actually happening; `(dropped ent=N earlier)` marks a past burst. A load-time burst of 74,097 was mistaken for an ongoing emergency before the two were separated |
| `[net] mix out/in <type>=<count>/<KB>` | Per-packet-type traffic, top six by bytes then the rest named by count. **The first thing this project ever had that measures what a CHANNEL costs.** Measured: `ent` was ~87% of outbound at ~94 KB/s |
| `nearSup=` on `[census] sent` | Near-band rows the v0.65 change gate suppressed as unchanged. Should be large; if it is zero the gate is not biting |
| `noBody=` on `[audit]` | Apply-path rows dropped because no local body exists for a streamed hand - **after** the proxy fallback. Non-zero means state is arriving for bodies this client does not have at all |
| `wdHold=` / `[wd] HOLD-origin` | A weapon drop held because neither the item nor its owner gave a usable position. Publishing it anyway put the peer's copy at world origin, which is the "he can see items I can't" report |
| `[wd] GIVEUP-nopos` | A drop that will never be mirrored. Unconditional by design - the dump-gated version of this line hid the bug that motivated it |
| `lostItems=` / `[xfer] FOLD-LOSS` | Items that left a container and arrived in none we watch. Added for the "transfer too far and it despawns" report, which had no evidence at all before |
| `frzAct=` on `[ai]` | Census-freeze ACTUATIONS, not log lines. The log is throttled to ~4/s, so 2,140 lines once stood for an unknown number. Measured 88,487 in one session |
| `frzHurt=` on `[ai]` | Freezes skipped because the body is bleeding or critical. **A suspended body cannot collapse from blood loss** - the suspend skips Kenshi's whole per-character update - so this exemption is what lets a dying enemy fall over |
| `snapVeto=` vs `snapCbt=` on `[ai]` | Combat snaps vetoed vs attempted. `snapVeto=0` with `snapCbt` climbing meant the veto could not fire at all (it reused a 20 u convergence constant as its range); it has its own 120 u ceiling since v0.66 |
| `truncHold=` on `[audit]` | Absence-culls suppressed because the peer's census was truncated |
| `[speed] WARNING intent drain hit its cap` | The engine reported a speed intent without explaining it. Should never fire |
| `[stage] WARNING stage table full` | More beat labels than the cost table holds; some stages are silently uncosted |

**A note on `[net] mix`:** it prints the top six by BYTES and then names the rest
by count. That tail exists because a ~30 B door row never outranks ent/census/med,
and the line was once unable to answer "did the door channel deliver" during an
investigation into exactly that.

## Cross-referencing the two logs

Both clients stamp `[net] CLOCKSYNC offset=` — apply it before comparing timestamps
across machines. A constant offset of exactly ±3600000/±7200000 ms is just a timezone
difference and is harmless.
