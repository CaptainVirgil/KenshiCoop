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

Healthy-session expectations after the fork's fixes: `mid=` should climb well past 48,
`staleMs` should stay small, `parks` should not reach five figures, and the `[door]`
channel should not alternate RECV/SEND on one hand.

## Cross-referencing the two logs

Both clients stamp `[net] CLOCKSYNC offset=` — apply it before comparing timestamps
across machines. A constant offset of exactly ±3600000/±7200000 ms is just a timezone
difference and is harmless.
