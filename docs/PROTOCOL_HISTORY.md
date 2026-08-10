# KenshiCoop protocol history

`src/netproto/Wire.h` points here for the version-by-version story. No such file
existed in the tree, so **this file is a reconstruction from code archaeology**,
not a recovered original. Read the preamble before trusting a row.

Current `PROTOCOL_VERSION`: **56**.

> **Why it went missing, and why it will not again.** `Wire.h` used to point at
> `resources/PROTOCOL_HISTORY.md`, and `resources/` is gitignored - so it named a
> path **no clone of this repository has ever contained**, on either side of the
> fork. That is why the original was lost rather than merely stale. This file now
> lives in `docs/`, which is tracked, so it can be reviewed alongside the change
> it documents and a fresh clone gets it.

---

## 1. What this is, and how much to trust it

Rebuilt from three sources, in descending order of authority:

| Tier | Source | Why it is trustworthy |
|---|---|---|
| **G** | `git log -p -- src/netproto/Wire.h` across all refs | The bytes themselves. The `PROTOCOL_VERSION` value and the struct diff are in the same commit; nothing is being remembered. |
| **C** | `// protocol NN` labels in `Wire.h` and `src/prototest/main.cpp` | Written by the author at the time. Names the feature reliably; the *number* is occasionally off by one (see §2.2). |
| **I** | Inference | Reasoning from surrounding evidence. Always marked. Never used to invent a row. |

Every struct size and field offset quoted below was **measured**, not computed:
each historical `Wire.h` was checked out and compiled (`g++ -std=c++98`, the
`#pragma pack(1)` layout is identical to the v100 build) and `sizeof`/`offsetof`
printed. Where a size is stated, it is a fact.

**What is certain:** the `PROTOCOL_VERSION` value at every commit, the exact
struct diff at every commit, every struct size and field offset, and every
append-vs-insert-vs-repurpose classification in §3. All of that is tier G.

**What is inferred:** the mapping of some feature *labels* onto individual
version *numbers* inside the big squashed commits (§2.1). Those rows say what
changed and are correct about the bytes; they are less certain about which of
several consecutive numbers a given change "was".

**What is missing:** see §6. Holes are named rather than filled.

### Keeping it current

When you bump `PROTOCOL_VERSION`, add a row to §3 in the same commit. Fill in the
**Class** column honestly — it is the only column that cannot be recovered later
by reading the current header, because a repurposed field looks exactly like a
field that was always there. If the change is a repurpose, also add a line to
§4.3.

---

## 2. The two facts that matter most

### 2.1 Most version numbers were never on the wire

`PROTOCOL_VERSION` jumped in blocks. Development bumped a "protocol NN" label per
feature, but many features shipped in one commit that set the constant straight to
the top of the range. **A peer can only ever advertise a value the constant
actually held in some commit.**

Values `PROTOCOL_VERSION` has actually held, in order:

```
1, 2, 3, 4, 11, 18, 23, 36, 37, 38, 39, 40, 41, 43, 44, 45, 48, 50, 51, 52, 53, 54, 55, 56
```

Values that **never existed** on this line: `5–10`, `12–17`, `19–22`, `24–35`,
`42`, `46`, `47`, `49`. A build advertising one of those is not a KenshiCoop build
from this history — but see §5, because unmerged fork branches *do* advertise
46, 47 and 49 for entirely different wire formats.

Values that reached players as tagged kits (`git tag` → `PROTOCOL_VERSION`):

| Tag | Protocol | Tag | Protocol |
|---|---|---|---|
| `v0.36` | 36 | `v0.45` | 43 |
| `v0.40` … `v0.44` | 40 | `v0.46` | 45 |
| | | `v0.47`, `v0.48` | 50 |
| | | `v0.49` | 52 |

37, 38, 41, 44, 48, 51, 53 and 54 existed only between releases.

### 2.2 The numbering is not perfectly self-consistent

Three sources name versions and they do not always agree. Known disagreements
(all tier G on the git column):

| Thing | `Wire.h` label | `prototest` label | `PROTOCOL_VERSION` in git when the bytes landed |
|---|---|---|---|
| `InvSnapshotHeader::keyKind` | protocol 34 | `v33` | 36 |
| `EntityBatchHeader::sendMs` | v35 | `v35` | 36 |
| `EVT_SQUAD_MOVE` | protocol 35 | "protocol 35, v34" | 36 |
| `PKT_NPC_CENSUS` | protocol 36 | `v35` | 36 |
| `PKT_INV_XFER` | protocol 37 | `v36` | 36 |
| `PKT_RESEARCH` | protocol 38 | `v37` | 37 |
| `InvItemEntry::locked` | protocol 42 | `v42` | 43 |

The table in §3 is keyed by the **label** (that is what you will grep for) and
carries a separate "landed at" column with the git-observed constant. Where they
differ, the "landed at" column is the one a version check sees.

---

## 3. Version table

**Class** is the column this document exists for:

- **APPEND** — new field(s) at the *end* of a struct. Old bytes remain a valid
  prefix of the new layout. This is the only class a length check can survive.
- **INSERT** — new field(s) in the *middle*. Every following field's offset moves.
  A length check does **not** catch this: the header still "fits", it just decodes
  to the wrong values.
- **REPURPOSE** — the same bytes, a different meaning. Undetectable by *any*
  length or size check. These are the rows that make tolerant decoding dangerous.
- **REMOVE** — a field or packet deleted.
- **NEW** — a new packet type; no existing layout touched.
- **VALUES** — new enum/bit/sentinel values in an existing field. Layout unchanged,
  meaning widened. Old receivers see an unknown value.
- **NONE** — version bumped with no wire change at all.

| Ver | Landed at | What changed | Struct(s) | Class | Ev |
|---|---|---|---|---|---|
| 1 | 1 | Initial wire: `HelloPacket`, `WelcomePacket`, `EntityState` (hand + transform + locomotion + task), `EntityBatchHeader`. `EntityState` = 75 B. | all | NEW | G |
| 1 | 1 | **`EntityState::rawTask` appended with no version bump.** 75 → 77 B. Two different builds both advertised `PROTOCOL_VERSION = 1` with incompatible `EntityState`. | `EntityState` | APPEND | G |
| 2 | 2 | `EntityState::bodyState` appended (77 → **79 B**, stable ever since). `BODY_DOWN/RAGDOLL/DEAD/CRAWL` bits 0–3. | `EntityState` | APPEND | G |
| 3 | 3 | Reliable event channel. `PKT_EVENT` (5), `EventPacket` (54 B, never changed since), `EventType` = NONE/KNOCKOUT/DEATH/REVIVE. | `EventPacket` | NEW | G |
| 4 | 4 | `TASK_COMBAT_MELEE` = `0xFE00` — a synthetic value of the existing `EntityState::task`. | `EntityState` | VALUES | G |
| 5–10 | 11 | Never on the wire. Content landed in the 4 → 11 jump; see the v11 row and §6. | — | — | G |
| 11 | 11 | Inventory + world items in one jump: `PKT_INV_SNAPSHOT`(6), `PKT_WORLD_ITEM`(7), `PKT_WORLD_ITEM_REMOVE`(8), `PKT_WORLD_DROP`(9), `PKT_WORLD_PICKUP`(10). New: `InvItemEntry` (156 B), `InvSnapshotHeader` (26 B), `WorldItemEntry` (73 B), `WorldItemSnapshotHeader` (6 B), `WorldItemRemoveHeader` (6 B), `WorldDropPacket`, `WorldPickupPacket` (83 B). `INV_ITEMS_MAX` = 20, `WORLD_ITEMS_MAX` = 16. | many | NEW | G |
| 12–14 | 18 | Never on the wire; unattributed. See §6. | — | — | G |
| 15 | 18 | `TASK_COMBAT_WAIT` = `0xFE01` (the AttackSlotManager menace-ring stance). | `EntityState` | VALUES | C |
| 16 | 18 | Per-part anatomy on the medical channel: `MedPartEntry`, `MED_PARTS_MAX` = 12, `limbState[4]`, `limbSid[4][48]`; `MedicalPacket` also began carrying combat-scoped world-NPC vitals. `MedicalPacket` = 459 B. `TreatmentPacket` levels re-keyed to anatomy index. | `MedicalPacket`, `TreatmentPacket` | NEW | C |
| 17 | 18 | `PKT_STATS`(17) + `StatsPacket` (194 B; `STATS_SLOT_MAX` = 40). | `StatsPacket` | NEW | C |
| 18 | 18 | Carried bodies: `EVT_PICKUP_BODY`(6), `EVT_DROP_BODY`(7), `TASK_CARRY_BODY` = `0xFE02`, `BODY_CARRIED` bit 4. | `EntityState`, `EventPacket` | VALUES | G+C |
| 19 | 23 | Furniture occupancy: `EVT_ENTER_FURNITURE`(8), `EVT_EXIT_FURNITURE`(9), `BODY_IN_BED` bit 5, `BODY_IN_CAGE` bit 6. | `EntityState`, `EventPacket` | VALUES | C |
| 20 | 23 | Stealth: `PKT_STEALTH`(18), `StealthPacket` (427 B) + `StealthSeerEntry`, `BODY_SNEAK` bit 7. | `StealthPacket` | NEW + VALUES | C |
| 21 | 23 | Runtime-spawn proxies: `PKT_SPAWN_REQ`(19), `PKT_SPAWN_INFO`(20); `SpawnReqPacket` (25 B), `SpawnInfoPacket` (139 B). | new | NEW | C |
| 22 | 23 | `PKT_MONEY`(21) + `MoneyPacket` (13 B) = `{ownerId, tabRank, money}`, where `money` was `Ownerships::money` for one squad tab. **Repurposed wholesale at 52.** | `MoneyPacket` | NEW | C |
| 23 | 23 | `EVT_RECRUIT`(10) — re-key a body from its old hand to its new one. | `EventPacket` | VALUES | G+C |
| 24 | 36 | `PKT_FACTION`(22) + `FactionPacket` (61 B). | new | NEW | C |
| 25 | 36 | `PKT_TIME`(23) + `TimePacket` (17 B); `f64` typedef added. | new | NEW | C |
| 26 | 36 | `PKT_DOOR`(24) + `DoorPacket` (31 B). | new | NEW | C |
| 27 | 36 | Placed buildings: `PKT_BUILD_PLACE`(25), `PKT_BUILD_STATE`(26); `BuildPlacePacket` (94 B), `BuildStatePacket` (34 B). Introduces the *placer-key* identity scheme. | new | NEW | C |
| 28 | 36 | `PKT_BUILD_DOOR`(27), `PKT_BUILD_REMOVE`(28); `BuildDoorPacket` (32 B), `BuildRemovePacket` (29 B). | new | NEW | C |
| 29 | 36 | **`MedicalPacket::hunger` + `::fed` inserted between `bleedRate` and `flags`.** 459 → **467 B**; `flags`, `nParts`, `parts[]`, `limbState[]`, `limbSid[][]` all shifted 8 bytes. | `MedicalPacket` | **INSERT** | C |
| 30 | 36 | Connect-edge resync (re-announce placed buildings on a peer connect). **Explicitly "no wire change"** — `Config.h:540`, `Replicator.h:568`. | — | NONE | C |
| 31 | 36 | Coordinated save transfer: `PKT_SAVE_REQ`(29) … `PKT_SAVE_ACK`(33); `SaveReqPacket` (57 B), `SaveBeginPacket` (67 B), `SaveFileHeader` (19 B), `SaveDoneHeader` (11 B), `SaveAckPacket` (20 B); `SAVE_NAME_LEN`/`SAVE_CHUNK_MAX`/`SAVE_PATH_MAX`. | new | NEW | C |
| 32 | 36 | Coordinated load: `PKT_LOAD_GO`(34), `PKT_LOAD_REQ`(35), `PKT_LOAD_NACK`(36); `LoadGoPacket`/`LoadNackPacket` (61 B), `LoadReqPacket` (57 B). | new | NEW | C |
| 33 | 36 | `PKT_PROD`(37) + `ProdPacket` (109 B); `i8` typedef added. | new | NEW | C |
| 34 | 36 | **`InvSnapshotHeader::keyKind` inserted between `ownerId` and `cType`.** 26 → **27 B**; `count` moved from offset 25 → 26 and the whole `InvItemEntry` array shifted one byte. Also a REPURPOSE: with `keyKind = 1` the `c*` fields are a protocol-27 *placer key*, not a save-stable hand — same five u32s, different meaning. | `InvSnapshotHeader` | **INSERT + REPURPOSE** | C |
| 35 | 36 | **`EntityBatchHeader::sendMs` inserted between `ownerId` and `count`.** 6 → **10 B**; `count` moved from offset 5 → 9. `ENTITY_BATCH_MAX` 18 → 17 to pay for it; `ENTITY_BATCH_MAX_STEAM` = 14 added (sender-side only). Also `EVT_SQUAD_MOVE`(11). | `EntityBatchHeader` | **INSERT** | G+C |
| 36 | 36 | `PKT_NPC_CENSUS`(38) + `NpcCensusHeader` (7 B), trailing `[u32 hand[5] * count]`; `NPC_CENSUS_MAX` = 512. Also **REPURPOSE**: world items became bidirectional — `WorldItemSnapshotHeader::ownerId` / `WorldItemRemoveHeader::ownerId` changed from "the host" to "the authoring sender, whose *private* netId space these ids belong to". Identical bytes, different key space. | `NpcCensusHeader`, `WorldItem*Header` | NEW + **REPURPOSE** | G+C |
| 37 | 36 | `PKT_INV_XFER`(39) + `InvXferPacket` (201 B) — cross-owner transfer intent. Shipped *inside* the 23 → 36 commit; see §2.2. | new | NEW | C |
| 38 | 37, then 38 | Two commits: `PKT_RESEARCH`(40) + `ResearchPacket` (57 B) at constant 37; then at constant 38 the **NPC census trailing payload changed from `[hands]` to `[hands][positions]`** — `NpcCensusHeader` stayed byte-identical at 7 B, only the framing behind it changed. | `ResearchPacket`, census framing | NEW + **REPURPOSE (framing)** | G+C |
| 39 | 39 | `SpawnInfoPacket::age` **appended** (139 → 143 B). `<= 0` means unreadable, so a zero tail decodes as the documented default. | `SpawnInfoPacket` | APPEND | G |
| 40 | 40 | `WorldPickupPacket::refDropOwnerId` + `::refDropId` **appended** (83 → 91 B). `refDropId == 0` is the documented "could not correlate an instance" fallback, so a zero tail is a legal value. | `WorldPickupPacket` | APPEND | G |
| 41 | 41 | `BODY_CHAINED` bit 8; `bodyInFurniture()` widened to include it. | `EntityState` | VALUES | G+C |
| 42 | 43 | **`InvItemEntry::locked` + `::lockReserved` inserted between `slot` and `section`.** 156 → **158 B**; `section`, `manufacturer[48]`, `material[48]` all shifted 2 bytes. Labelled protocol 42 but shipped in the commit that set the constant to 43. | `InvItemEntry` | **INSERT** | G+C |
| 43 | 43 | `PKT_CAM_HINT`(41) + `CamHintPacket` (17 B). | new | NEW | G+C |
| 44 | 44 | `EntityBatchHeader::epoch` **inserted between `sendMs` and `count`.** 10 → **14 B**; `count` moved from offset 9 → 13. Also `ObjectHand` (a header-only helper — not on the wire). | `EntityBatchHeader` | **INSERT** | G |
| 45 | 45 | `PKT_COMBAT_HIT`(42) + `CombatHitPacket` (37 B). **Added, reverted (constant rolled 45 → 44, packet deleted), then re-added.** Both 45 states are byte-identical, so a peer advertising 45 is unambiguous — but 44 is not: one 44 has the packet's tag free, the other never had it. | `CombatHitPacket` | NEW / REMOVE / NEW | G |
| 46 | 48 | `InvSnapshotHeader::flags` + `INV_FLAG_TRUNCATED`; `INV_ITEMS_MAX` 20 → 64. See the 48 row — flags is an INSERT and they landed together. | `InvSnapshotHeader` | **INSERT** | G+C |
| 47 | 48 | `PKT_WORLD_ITEM_CLAIM`(43) + `WorldItemClaimHeader` (10 B), trailing `[u32 netId * count]`. | new | NEW | G+C |
| 48 | 48 | **`InvItemEntry::lockReserved` → `parentIdx`.** Same offset, same 158 B, entirely different meaning: 0 = top level, N = nested inside entry N−1. Benign old→new (an old sender always wrote 0 = top level); **new→old silently flattens every bagged item into its wearer's inventory.** Also, in the same commit, `InvSnapshotHeader::flags` inserted between `keyKind` and `cType` — 27 → **28 B**, `count` 26 → 27, container key shifted. | `InvItemEntry`, `InvSnapshotHeader` | **REPURPOSE + INSERT** | G+C |
| 48 | 48 | (Unversioned, same constant.) `PKT_CAM_HINT` went **unidirectional → bidirectional**. No bytes moved; only who may send it. The `PacketType` enum comment at `Wire.h:72` still says "join -> host" and is stale. | `CamHintPacket` | **REPURPOSE (direction)** | G |
| 49 | 50 | `PKT_CELL_CLAIM`(44) + `CellClaimPacket` (21 B); `i32` typedef added. | new | NEW | G+C |
| 50 | 50 | `PKT_INV_XFER_ACK`(45) + `InvXferAckPacket` (18 B) + `XferAckVerdict`. | new | NEW | G+C |
| 51 | 51 | `GRADE_NA` = 0xFF. **`InvItemEntry::level` inserted between `section` and `manufacturer`** (158 → **159 B**, both 48-byte sid buffers shifted) and **`InvXferPacket::level` inserted between `quality` and `manufacturer`** (201 → **202 B**, same shift). `InvItemEntry::quality`'s comment corrected: it is *condition*, never was the grade. | `InvItemEntry`, `InvXferPacket` | **INSERT** | G+C |
| 52 | 52 | **The canonical repurpose. `MoneyPacket::tabRank` → `ackSeq`.** 13 B before and after; every offset identical. `money` changed from "one tab's `Ownerships::money`" to "the authoritative shared pool total", and `PKT_MONEY`'s direction narrowed to host→join. New `PKT_MONEY_DELTA`(46) + `MoneyDeltaPacket` (13 B) carries the join's signed change. `prototest` records it: *"which is why the old `tabRank` field is gone"*. | `MoneyPacket` | **REPURPOSE + REMOVE** | G+C |
| 53 | 53 | **`EntityState::bodyState` bits 9–11 became a 3-bit prone FIELD** (`BODY_PRONE_SHIFT/MASK`, `PRONE_NORMAL`…`PRONE_KO`). Those bits were previously always zero, so no size moved — but every pre-53 call site testing `bodyState != 0` now reads a merely crouching body as down/dead. `bodyFlags()` exists exactly to strip it. Also `MED_CRIPPLED` = bit 2 of `MedicalPacket::flags`, which is *authoritative*, not advisory. | `EntityState`, `MedicalPacket` | **REPURPOSE** | G+C |
| 54 | 54 | `PKT_DEED`(47) + `DeedPacket` (78 B) — building ownership, on the latched-set shape rather than the door shape. | new | NEW | G+C |

---

## 4. Tolerant decoding: what could work, and what cannot

Today there is no tolerance at all: `NetLink.cpp:469` and `:515` reject any
`HelloPacket`/`WelcomePacket` whose `version != PROTOCOL_VERSION`, and
`SteamInvite.cpp:281` rejects the lobby the same way. That is currently correct.
This section exists so that a future relaxation is done with evidence rather than
optimism.

### 4.1 The two receive paths

**Path A — trailing-array packets.** These read a fixed header, compute
`need = sizeof(header) + count * sizeof(element)`, and require `len >= need`
(`NetLink.cpp` ≈ lines 541–655 and 868–895):

`PKT_ENTITY_BATCH`, `PKT_INV_SNAPSHOT`, `PKT_WORLD_ITEM`,
`PKT_WORLD_ITEM_REMOVE`, `PKT_WORLD_ITEM_CLAIM`, `PKT_NPC_CENSUS`,
`PKT_SAVE_FILE`, `PKT_SAVE_DONE`.

**Path B — everything else**, via `readPacket<T>()` at the bottom of `Wire.h`, which requires
`len >= sizeof(T)` and `memcpy`s exactly `sizeof(T)` bytes. Note the asymmetry
this already gives you for free: a **longer** packet is accepted and its tail
ignored, a **shorter** one is dropped. That is precisely the tolerance an
append-only change needs, in the newer-sender → older-receiver direction —
**but it is a Path-B property only.** A Path-A packet's trailing arrays start
at `data + sizeof(header)`, so a byte appended to a Path-A *header* lands
between the header and the arrays and shifts every element read; it does not
fall into an ignored tail. The census flag in §"Next bump worth spending" is
the worked example of how destructive that gets.

### 4.2 Safe candidates

A struct is a safe tolerant-decode candidate when older bytes are a valid
**prefix** of the current layout and the missing tail has a defined "absent"
value.

**Genuinely safe — APPEND with a defined zero-tail meaning:**

| Struct | Tolerant from | Why |
|---|---|---|
| `SpawnInfoPacket` | 21 → 38 | `age` appended at 39; `<= 0` already means "unreadable, use the adult default". Zero-filling the tail *is* the documented behaviour. |
| `WorldPickupPacket` | 11 → 39 | `refDropOwnerId`/`refDropId` appended at 40; `refDropId == 0` already means "could not correlate an instance, fall back to the oldest same-sid copy". |
| `EntityState` | 2 → current | 79 B and byte-stable since v2. **Caveat:** `bodyState` semantics widened at 18/19/20/41 and were repurposed at 53 — see §4.3. |
| `EventPacket` | 3 → current | 54 B and byte-stable since v3. Only new `EventType` values were added (16, 18, 19, 23, 35). An unknown event id must be *ignored*, never guessed. |

**Safe because they never changed** (Path B, fixed size, no field ever moved
since introduction): `HelloPacket` (4 B), `WelcomePacket` (7 B), `SpawnReqPacket`,
`StealthPacket`, `StatsPacket`, `TreatmentPacket`, `SpeedPacket`, `FactionPacket`,
`TimePacket`, `DoorPacket`, `BuildPlacePacket`, `BuildStatePacket`,
`BuildDoorPacket`, `BuildRemovePacket`, all five `Save*`/`Load*` packets,
`ProdPacket`, `ResearchPacket`, `CamHintPacket` (bytes only — direction changed),
`CombatHitPacket`, `CellClaimPacket`, `InvXferAckPacket`, `MoneyDeltaPacket`,
`DeedPacket`, `TimePingPacket`, `TimePongPacket`.

**Path A families whose header never moved** — these are the real "`*Header` +
trailing array where the receiver already validates `len >= need`" candidates:

| Family | Header | Stable since |
|---|---|---|
| `WorldItemSnapshotHeader` + `WorldItemEntry[]` | 6 B | 11 (but see the 36 `ownerId` repurpose) |
| `WorldItemRemoveHeader` + `u32[]` | 6 B | 11 (same caveat) |
| `WorldItemClaimHeader` + `u32[]` | 10 B | 47 |
| `SaveFileHeader` + path + payload | 19 B | 31 |
| `SaveDoneHeader` + `u32[]` | 11 B | 31 |

### 4.3 NOT safe, and why

**The `*Header` family is not automatically safe — two of its members are the
worst offenders in the whole protocol.**

| Struct | Problem | Consequence |
|---|---|---|
| `EntityBatchHeader` | `count` moved twice: offset **5** (≤ 35) → **9** (36–43) → **13** (44+). Both were INSERTs before `count`. | The `len >= need` guard reads `count` from the wrong offset — for an old peer it lands inside the first `EntityState`'s `hType`. It *usually* yields a huge `count` and the packet is dropped, but that is luck, not a check. This is the single most dangerous struct to tolerate, precisely because it looks like the safe pattern. |
| `InvSnapshotHeader` + `InvItemEntry` | Header `count` moved from **25** (≤ 34) → **26** (36–47) → **27** (48+). The *element* also grew twice (156 → 158 at 42/43, → 159 at 51), both mid-struct. | Both the framing and the stride are wrong. Nothing detects it: a mismatched stride still produces a plausible `need`. |
| `NpcCensusHeader` | Header byte-identical (7 B) since 36, but the **payload behind it** changed at 38 from `[hands]` to `[hands][positions]`. | The header cannot tell you which layout follows. The current `need` includes the positions, so a pre-38 census is dropped (fail-safe) — but any relaxation of that arithmetic reads 12 bytes per NPC of whatever follows the buffer. |
| `MedicalPacket` | `hunger`/`fed` INSERTed at 29 (459 → 467 B), shifting `flags`, `nParts`, `parts[]`, `limbState[]`, `limbSid[][]`. | A pre-29 packet is 8 B short and dropped by `readPacket`; forcing it through misreads every part of the anatomy model. |
| `InvXferPacket` | `level` INSERTed at 51 before `manufacturer` (201 → 202 B). | Both 48-byte sid buffers shift by one; the item is looked up under a garbage stringID. |
| `MoneyPacket` | **`tabRank` → `ackSeq` at 52. 13 bytes before and after; every offset identical.** `money` also changed from a per-tab wallet to the shared pool total, and the direction narrowed to host→join. | **No length check can ever detect this.** A pre-52 peer's tab rank is read as an ack sequence and a per-tab balance as the shared purse. This is the reference example for why the Class column exists. |
| `InvItemEntry::parentIdx` | `lockReserved` (reserved, always 0) → `parentIdx` at 48. Same offset, same size. | Old→new is benign (0 = top level). **New→old is not:** a nested item's parent reference is read as a reserved byte and ignored, so every bagged item is flattened into its wearer's inventory — the exact backpack item-loss class of bug the field was added to fix. |
| `EntityState::bodyState` | Bits 9–11 became the prone FIELD at 53; previously always 0. | A pre-53 receiver testing `bodyState != 0` reads a crouching or crippled body as down/dead, and pins a conscious crawler to the ground. `Wire.h` documents this at the `bodyFlags()` helper. |
| `InvSnapshotHeader` `keyKind` / `ProdPacket` `keyKind` | `keyKind = 1` reinterprets the five `c*`/`key[]` u32s from a save-stable hand into a protocol-27 placer key. | Same bytes, different identity space. A receiver that does not understand `keyKind` resolves a placer key as a hand and touches the wrong object — or nothing. |
| `WorldItemSnapshotHeader::ownerId`, `WorldItemRemoveHeader::ownerId` | Changed at 36 from "the host" to "the authoring sender, whose *private* netId space these ids name". | netIds are only unique per author. A pre-36 peer's ids collide with a post-36 peer's, and proxies are culled or reattached to the wrong ground item. |
| `CamHintPacket` | Unidirectional → bidirectional at constant 48, with no bump. | A pre-change host does not expect a hint from itself's direction and may treat it as noise; harmless in practice, but it is a real unversioned semantic change. |
| `HelloPacket` | Byte-stable **on this line**. See §5 — a fork branch inserts `ownRank` into the middle of it. | — |

### 4.4 The rule this all points at

Tolerating protocol *N* means asserting that every struct you will decode had the
current layout **and the current meaning** at *N*. Given §3, the only version at
which that is currently true for the whole protocol is 54. Any narrower tolerance
must be **per packet type**, gated on the peer's advertised version, using §3's
Class column — never a blanket `version >= X`.

---

## 5. Cross-fork hazard: the same number means different things

This repo is a hard fork of `nhoral/KenshiCoop` and keeps upstream PR branches
fetched. **Unmerged upstream branches assign the same protocol numbers — and in
one case the same packet tag — to entirely different wire formats.** All tier G.

| Branch | Advertises | What it actually is | Collides with our |
|---|---|---|---|
| `upstream-pr/9` | **44** | `MoneyPacket::money` repurposed from an absolute balance to a **signed delta**, `tabRank` → unused. **No version bump at all** — it stays 44. | 44 (= `epoch`). Two different `MoneyPacket` meanings both advertising 44. |
| `upstream-pr/18` | **45** | `PKT_BOUNTY = 42` + `BountyPacket` (bounty/crime rows). | 45 (= `CombatHitPacket`), and **tag 42 is `PKT_COMBAT_HIT` here**. Same tag, same version, different struct and different size. |
| `upstream-pr/28` | **46** | `SpawnInfoPacket` gains `name[48]` and `age`. | our 46 (`INV_FLAG_TRUNCATED`, `INV_ITEMS_MAX` 64). |
| `upstream-pr/35`, `/36` | **46** | `InvItemEntry` gains a nested `NestedInvKey` member. | our 46, *and* the other 46 above. |
| `upstream-pr/36` | **47** | `CombatHitPacket` gains `flags` + `koSkill`. | our 47 (`WorldItemClaimHeader`). |
| `upstream-pr/50` | **49** | Three-player support: `PKT_PEER_STATUS = 44` + `PeerStatusPacket`, and **`u32 ownRank` INSERTed into `HelloPacket`** between `version` and `nameLen` (4 → 8 B). | our 49 (`PKT_CELL_CLAIM = 44`). **Same tag 44, same version 49, different struct.** |

The saving grace is that `HelloPacket::version` sits at offset 1 in both layouts,
so the existing hard reject still fires across the fork boundary. That reject is
load-bearing. **A version number is not an identity** — if tolerance is ever
added, it needs a build/fork identifier alongside the number.

---

## 6. Gaps — what is still unknown

Named so the table above is not mistaken for complete.

1. **Versions 5–10 (`PROTOCOL_VERSION` 4 → 11).** Six numbers, one commit
   (`2ae8514`). The content is bounded and known — `PKT_INV_SNAPSHOT`(6),
   `PKT_WORLD_ITEM`(7), `PKT_WORLD_ITEM_REMOVE`(8), `PKT_WORLD_DROP`(9),
   `PKT_WORLD_PICKUP`(10) and their structs — but the source labels them by
   *phase* ("Phase 4a", "W1", "W2", "W3"), never by version, so **which feature
   was which number is unrecoverable**. It is tempting to read the packet tags
   6–10 as the version numbers 6–10; there is no evidence for that and it is not
   asserted here. Practically harmless: none of 5–10 was ever on the wire.

2. **Versions 12–14 (`PROTOCOL_VERSION` 11 → 18).** Three unattributed numbers.
   The commit `476c078` introduces, among labelled things, `TimePingPacket` /
   `TimePongPacket` (wall-clock sync, labelled by *purpose* only) and
   `SpeedPacket` + `SpeedFlags` (labelled not at all). *Inference (I), stated as
   inference:* those are the natural occupants of 12–14. Not evidenced; do not
   promote to the table.

3. **The 24–36 label-to-number mapping.** Thirteen constant values were skipped in
   one commit (`59b8632`) while the code labels fourteen features 24–37. The
   *features* are all evidenced by name; the *assignment of a number to each* is
   the code's own claim and is internally inconsistent by one somewhere in the
   range (§2.2). Rows 24–37 are correct about what changed and about the bytes;
   treat their numbering as approximate.

4. **Which "44" a peer means.** The revert at `53a4ca4` rolled the constant back
   from 45 to 44, producing a second, *different* 44: the first has
   `PKT_COMBAT_HIT`/tag 42 unused, the second had it used and then removed. On
   this line only the reverted 44 is reachable from `main`, but a build compiled
   from `98e48ba` and one from `53a4ca4` both say 44 with different tag
   allocations. And see §5 — a third 44 exists upstream.

5. **Anything before the clean rebuild.** `e4c8087` is described as "clean rebuild,
   v1"; whatever protocol existed before it is not in this repository.

6. **Semantics changed without a bump.** Two are evidenced here (`rawTask` at v1,
   `PKT_CAM_HINT` direction at 48) and one upstream (`MoneyPacket` at 44). **No
   systematic search for others was performed** — a comment-only diff that changes
   meaning is invisible to `PROTOCOL_VERSION` and to `prototest`, which asserts
   sizes and offsets, not meanings. If tolerance is ever built, this class needs
   its own audit.

---

## 7. Adding an entry

1. Bump `PROTOCOL_VERSION` in `src/netproto/Wire.h`.
2. Update the size/offset assertions in `src/prototest/main.cpp` and the
   `PROTOCOL_VERSION` check at `main.cpp:312`.
3. Add a row to §3 **in the same commit**, with the Class column filled honestly.
   If it is an INSERT, say which field's offset moved and to where. If it is a
   REPURPOSE, add it to §4.3 as well — that is the only record that will survive,
   because the current header cannot show you a meaning that used to be different.
4. Run the gate (`scripts/linux/verify.sh` or `scripts\verify.ps1`) and ship a kit
   to both players: a mismatch is a hard connection reject.

Nothing mechanically gates step 3: `prototest` asserts the `PROTOCOL_VERSION` literal, not the §3 row. Keeping them together is a discipline, not a check.

---

## Next bump worth spending, if one is spent

`NpcCensusHeader` has no truncation flag. A census row's absence is read by the peer
as "this body does not exist", and it culls its real local copy against that — so when
the enumeration hits `NPC_CENSUS_MAX` the remainder is broadcast as absent. The publish
code has always said so in its own comment.

The sender now orders the census nearest-first while truncated, which puts the
sacrificed rows in the 25% margin the peer does not cull against. That narrows the
window; it does not close it. If the body count inside the peer's own cull radius
exceeds the cap, real bodies are still declared absent and no ordering can help.

Closing it needs one bit on the wire — a `truncated` flag, with the receiver
suppressing culls for that beat. It is **not** the cheap append it looks like:
`PKT_NPC_CENSUS` is a Path-A packet, so a byte appended to `NpcCensusHeader`
sits between the header and the trailing arrays. An old receiver still parses
the header correctly (type/ownerId/count offsets are unchanged), still passes
`len >= need` — the appended byte is exactly one byte of slack — and then reads
every hand and position one byte early. Garbage keys arrive at 1 Hz and are
stamped fresh, so the STALE fail-open (which would *disable* wide culling)
never fires, and the wide pass judges every real local NPC against a set
containing no real keys. The "safe append" is a mass-cull of the loaded area.

Carry the flag in **bit 15 of the existing u16 `count`** instead: `sizeof`
stays 7, an old receiver sees `count > NPC_CENSUS_MAX`, drops the packet, and
its census goes STALE — fail-safe rather than fail-destructive. Costs a bump
either way, and every player must update the same day, which is the only
reason it has not been done.

## v55 — host-authoritative weather (2026-08-09)

Adds `PKT_WEATHER` (48) / `WeatherPacket` (81 B). Host publishes the ACTIVE
biome region's weather at ~1 Hz, change-gated; the join applies it.

**Why a push and not a seed.** Kenshi rolls weather per biome region from a
probability-weighted table (`Season::getNewWeather`), and the dump exposes no
seed field anywhere — so two clients standing in the same biome diverge by
construction and nothing converges them. Observed live: one player in rain, the
other clear, metres apart. Not cosmetic — acid rain damages non-Skeletons and
storms affect ranged combat, so divergent weather is divergent gameplay.

**Identity is the weather's GameData stringID**, not its name and not its
pointer. Pointers differ between processes; the stringID is what every other
channel here already uses to mean "the same data object on both machines". An
id the receiver cannot resolve in its own current-season table is DROPPED, not
guessed — a stale sky beats a wrong one.

**`weatherTime` is deliberately outside the change gate.** It advances every
tick, so including it would send every beat and make the gate decorative. The
receiver still gets a fresh clock whenever anything else moves, and any real
transition moves the id or the strengths.

Engine note for whoever touches this next: `kenshi/Weather.h` cannot be
included in the engine prelude — the dump defines `class WeatherRegion` in both
that header and `PhysicsCollection.h`, and `Weather.h` uses `WeatherRegion`,
`Weather` and `Season` before declaring them. The facade therefore reads through
local offset mirrors quoted from the dump, and re-declares `WeatherSystem` at
GLOBAL scope (GetRealAddress resolves through the mangled name, so the namespace
matters).


## v56 — dialogue relay (2026-08-09)

Adds `PKT_DIALOGUE` (49) / `DialoguePacket` (145 B). Both clients publish lines
spoken locally and display lines the peer reports.

**Why it never worked.** A `DialogueSpeechBubble` is spawned only on the machine
whose AI ran the conversation, so the host sees dialogue the peer never does.
Nothing upstream or in this fork ever carried it — not a regression, simply
never built.

**Capture hooks `setText` and `setPosition(Vector3)`** and correlates on the
bubble pointer. That is a workaround for the one dialogue symbol KenshiLib does
NOT export: `DialogueSpeechBubble::speechBubbleList`, the static set of live
bubbles. Without it there is no way to ask what is currently displayed, so we
catch each bubble as it is populated instead. Both halves must arrive before a
line emits — the engine sets them in either order.

**Position, not a speaker hand.** The capture point IS the placement call, and a
world position needs no cross-machine identity resolution. The receiver attaches
the text to the character nearest that spot so it tracks the speaker, and DROPS
the line when nothing is within 40 u — our copy of that NPC may not exist, and
an orphan caption in mid-air reads as a bug.

**Symmetric and fire-and-forget.** Neither side is authoritative for speech, so
both forward what they witnessed and skip their own echo. No seq, no change
gate, no re-assert: a spoken line is an EVENT, and re-showing a stale one is
worse than missing it.

---

## 57 — the mod-set fingerprint (2026-08-10) — EVIDENCED

`HelloPacket` and `WelcomePacket` each gain `u32 modsHash` + `u16 modsCount`
(4 → 10 bytes and 7 → 13 bytes respectively). Both sides carry it, because
either player can be the one with the odd load order.

**Why it earned a bump.** Two players shared a SAVE while loading it against
DIFFERENT mod sets, and spent most of a play session reporting what looked like
replication bugs: furniture that vanished on one screen and not the other, NPCs
that existed for one player, a weather stringID the peer could not resolve.
Every one of those is a real consequence of two different worlds, and none is a
network fault. Nobody thought to diff the two `mods.cfg` files for hours.

Kenshi bakes mod content into a save by index, so a shared save plus differing
mods is not a soft mismatch — it is two worlds. Protocol version already gets a
hard reject for a far less damaging condition (packets the peer cannot parse).

**It WARNS, it does not reject.** A version mismatch is unarguable: the bytes
cannot be read. A mods mismatch parses perfectly and then describes something
the peer does not have — and some load-order differences are genuinely harmless
(cosmetic mods, retextures). That makes it the players' call, so the handshake
states the fact loudly and continues.

**The fingerprint is order-sensitive and normalised.** Later mods override
earlier ones, so order is content: lines are folded in file order. CR, blank
lines, `#` comments and surrounding whitespace are ignored, and the text is
case-folded, so two byte-different files describing the same load order still
match. `0` means the file could not be read — UNKNOWN, never reported as a
mismatch, on the same rule the census follows about absence.
