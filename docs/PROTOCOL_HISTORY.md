# KenshiCoop protocol history

`src/netproto/Wire.h` points here for the version-by-version story. No such file
existed in the tree, so **this file is a reconstruction from code archaeology**,
not a recovered original. Read the preamble before trusting a row.

Current `PROTOCOL_VERSION`: **54**.

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
1, 2, 3, 4, 11, 18, 23, 36, 37, 38, 39, 40, 41, 43, 44, 45, 48, 50, 51, 52, 53, 54
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
append-only change needs, in the newer-sender → older-receiver direction.

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

**Step 3 cannot currently be honoured.** `resources/` is gitignored, so this file
is not in any commit and "in the same commit" is not something the repo can hold
you to. Until that is resolved one of two ways — move the document somewhere
tracked and repoint `Wire.h:23-27`, or accept that it is a personal working note
and stop `Wire.h` from promising it — every future bump risks re-creating the hole
this reconstruction just filled. The reconstruction was possible only because
`Wire.h` and `prototest` are unusually well commented and the git history is
intact; a repurpose landed without a comment would leave no trace at all.
