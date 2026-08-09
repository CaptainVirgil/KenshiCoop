// prototest - the asserting unit layer for the KenshiCoop wire protocol.
//
// Runs in milliseconds, before any game launch, as step 0 of every regression
// tier (scripts/regress.ps1). Locks three things:
//   1. The WIRE CONTRACT: exact packed sizes + field offsets of every packet in
//      src/netproto/Wire.h. A padding/reorder slip silently desyncs both
//      clients (they memcpy struct bytes); this catches it at compile-run time.
//   2. The CONTENT HASH (src/netproto/ContentHash.h): the inventory-sync
//      convergence key. Must be deterministic, field-sensitive, and
//      order-independent across entries - cross-client equality of these sums
//      IS the inv oracle's proof.
//   3. The INTERPOLATION BUFFER (src/plugin/sync/Interp.cpp): bracketing,
//      clamping, dead-reckoning cap, staleness, teleport snap - and, since the
//      2026-08-07 session audit, what the buffer is NOT current for. sample()
//      copies the last RECEIVED snapshot wholesale and overwrites only the
//      transform, so its bodyState/task lag real time by a send interval; four
//      self-heals read that as current and undid reliable events that had
//      already applied. testInterpStaleness pins that staleness (real, bounded,
//      and NOT fixed by switching to latest()), testInterpDelayBand pins the
//      cadence-scaled render-delay ceiling that keeps a mid-band body
//      interpolating instead of dead-reckoning, and testSyncTuning pins the
//      arithmetic tying the mid band's width to both.
//   4. The pure DECISION ARITHMETIC the sync layer runs on: the change gate every
//      sampled channel routes its send through (ChangeGate.h - genuinely CRT-only,
//      so those checks run the real code), and the drive's convergence ladder,
//      which is not (ReplicatorUtil.h pulls in ENet and the engine facade, so
//      testDriveBands/testDriveConvergence MIRROR it - read their preambles).
//
// Zero game dependencies. Exit code = number of failed checks (0 = PASS).
//
// Build: cmd /c scripts\build_prototest.cmd  ->  dist\prototest.exe

#define _CRT_SECURE_NO_WARNINGS 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <cstdio>
#include <cstring>

#include "../netproto/Wire.h"
#include "../netproto/ContentHash.h"
#include "../plugin/sync/Interp.h"
#include "../plugin/core/OwnRanks.h"
#include "../plugin/core/SteamId.h"
#include "../plugin/core/WorkPose.h"
#include "../plugin/core/DeathLatch.h"
#include "../plugin/core/Inbound.h" // Phase 0 queue-lifecycle fixes (header-only)
#include "../plugin/game/EngineFaults.h" // Phase 5c: fault throttle (pure inline)
#include "../plugin/game/EngineCaps.h"   // Phase 5d: capability registry (pure inline)
#include "../plugin/sync/ChangeGate.h"   // Phase 6: change-gated send/accept policy
#include "../plugin/sync/SyncTuning.h"   // mid-band sizing + the self-heal debounce windows
#include "../plugin/sync/SaveXfer.h"     // Part A: real save-transfer receiver end-to-end

#include <set>
#include <string>
#include <vector>
#include <windows.h>

using namespace coop;

// SaveXfer.cpp logs through coop::logLine/logErrLine; CoopLog.cpp is NOT part of
// this CRT-only build, so provide inert definitions to satisfy the linker (the
// round-trip test only cares about the staged/committed bytes, not the log).
namespace coop {
    void logLine(const char*) {}
    void logErrLine(const char*) {}
}

static int g_failed = 0;
static int g_total  = 0;

#define CHECK(name, cond) do { \
    ++g_total; \
    if (cond) { std::printf("  ok   %s\n", name); } \
    else      { std::printf("  FAIL %s\n", name); ++g_failed; } \
} while (0)

#define CHECK_EQ(name, actual, expected) do { \
    ++g_total; \
    unsigned long long a_ = (unsigned long long)(actual); \
    unsigned long long e_ = (unsigned long long)(expected); \
    if (a_ == e_) { std::printf("  ok   %s (= %llu)\n", name, a_); } \
    else { std::printf("  FAIL %s (actual %llu != expected %llu)\n", name, a_, e_); ++g_failed; } \
} while (0)

// ---- 1. Wire contract: packed sizes ------------------------------------------

static void testSizes() {
    std::printf("== wire struct sizes (the packed contract both clients memcpy) ==\n");
    CHECK_EQ("sizeof(HelloPacket)",             sizeof(HelloPacket),             4);
    CHECK_EQ("sizeof(WelcomePacket)",           sizeof(WelcomePacket),           7);
    CHECK_EQ("sizeof(EventPacket)",             sizeof(EventPacket),             54);
    CHECK_EQ("sizeof(EntityState)",             sizeof(EntityState),             79);
    CHECK_EQ("sizeof(EntityBatchHeader)",       sizeof(EntityBatchHeader),       14); // v35: +sendMs; v44: +epoch
    CHECK_EQ("sizeof(InvItemEntry)",            sizeof(InvItemEntry),            159); // v42: +locked, v48: reserved byte became parentIdx (size unchanged), v51: +level (craft grade)
    CHECK_EQ("sizeof(InvSnapshotHeader)",       sizeof(InvSnapshotHeader),       28); // v33: +keyKind; v46: +flags
    CHECK_EQ("sizeof(WorldItemEntry)",          sizeof(WorldItemEntry),          73);
    CHECK_EQ("sizeof(WorldItemSnapshotHeader)", sizeof(WorldItemSnapshotHeader), 6);
    CHECK_EQ("sizeof(WorldItemRemoveHeader)",   sizeof(WorldItemRemoveHeader),   6);
    CHECK_EQ("sizeof(WorldItemClaimHeader)",    sizeof(WorldItemClaimHeader),    10); // v47
    CHECK_EQ("sizeof(WorldDropPacket)",         sizeof(WorldDropPacket),         191);
    CHECK_EQ("sizeof(WorldPickupPacket)",       sizeof(WorldPickupPacket),       91); // v40: +item identity
    CHECK_EQ("sizeof(InvXferPacket)",           sizeof(InvXferPacket),           202); // v36; v51: +level

    CHECK_EQ("sizeof(MedPartEntry)",            sizeof(MedPartEntry),            19);
    CHECK_EQ("sizeof(MedicalPacket)",           sizeof(MedicalPacket),           467);
    CHECK_EQ("sizeof(TreatmentPacket)",         sizeof(TreatmentPacket),         77);
    CHECK_EQ("sizeof(CombatHitPacket)",         sizeof(CombatHitPacket),         37);
    CHECK_EQ("sizeof(SpeedPacket)",             sizeof(SpeedPacket),             14);
    CHECK_EQ("sizeof(StatsPacket)",             sizeof(StatsPacket),             194);
    CHECK_EQ("sizeof(StealthPacket)",           sizeof(StealthPacket),           427);
    CHECK_EQ("sizeof(SpawnReqPacket)",          sizeof(SpawnReqPacket),          25);
    CHECK_EQ("sizeof(SpawnInfoPacket)",         sizeof(SpawnInfoPacket),         143);
    CHECK_EQ("sizeof(MoneyPacket)",             sizeof(MoneyPacket),             13);
    CHECK_EQ("sizeof(MoneyDeltaPacket)",        sizeof(MoneyDeltaPacket),        13);
    CHECK_EQ("sizeof(FactionPacket)",           sizeof(FactionPacket),           61);
    CHECK_EQ("sizeof(TimePacket)",              sizeof(TimePacket),              17);
    CHECK_EQ("sizeof(DoorPacket)",              sizeof(DoorPacket),              31);
    CHECK_EQ("sizeof(BuildPlacePacket)",        sizeof(BuildPlacePacket),        94);
    CHECK_EQ("sizeof(BuildStatePacket)",        sizeof(BuildStatePacket),        34);
    CHECK_EQ("sizeof(BuildDoorPacket)",         sizeof(BuildDoorPacket),         32);
    CHECK_EQ("sizeof(BuildRemovePacket)",       sizeof(BuildRemovePacket),       29);
    CHECK_EQ("sizeof(SaveReqPacket)",           sizeof(SaveReqPacket),           57);
    CHECK_EQ("sizeof(SaveBeginPacket)",         sizeof(SaveBeginPacket),         67);
    CHECK_EQ("sizeof(SaveFileHeader)",          sizeof(SaveFileHeader),          19);
    CHECK_EQ("sizeof(SaveDoneHeader)",          sizeof(SaveDoneHeader),          11);
    CHECK_EQ("sizeof(SaveAckPacket)",           sizeof(SaveAckPacket),           20);
    CHECK_EQ("sizeof(LoadGoPacket)",            sizeof(LoadGoPacket),            61);
    CHECK_EQ("sizeof(LoadReqPacket)",           sizeof(LoadReqPacket),           57);
    CHECK_EQ("sizeof(LoadNackPacket)",          sizeof(LoadNackPacket),          61);
    CHECK_EQ("sizeof(ProdPacket)",              sizeof(ProdPacket),              109);
    CHECK_EQ("sizeof(NpcCensusHeader)",         sizeof(NpcCensusHeader),         7); // v35: census
    CHECK_EQ("sizeof(ResearchPacket)",          sizeof(ResearchPacket),          57); // v37: research
    CHECK_EQ("sizeof(DeedPacket)",              sizeof(DeedPacket),              78); // v54: deeds
    // v55: host-authoritative weather. 1+4+4+48+4+4+4+4+4+4 packed.
    CHECK_EQ("sizeof(WeatherPacket)",           sizeof(WeatherPacket),           81);
    CHECK_EQ("sizeof(CamHintPacket)",           sizeof(CamHintPacket),           17); // v43: camera hint
    CHECK_EQ("sizeof(CellClaimPacket)",         sizeof(CellClaimPacket),         21); // v49: cell claim
    CHECK_EQ("sizeof(InvXferAckPacket)",        sizeof(InvXferAckPacket),        18); // v50: transfer verdict
    // A full entity batch must fit one ~1400 B datagram (NetLink chunking cap).
    CHECK("entity batch fits datagram",
          sizeof(EntityBatchHeader) + ENTITY_BATCH_MAX * sizeof(EntityState) <= 1428);
    // The Steam sender chunk must fit the 1200 B clamped Steam MTU with room
    // for ENet's per-packet overhead (an oversized UNRELIABLE packet would be
    // sent as RELIABLE fragments - motion-stream stalls; review 2026-07-10).
    CHECK("steam entity batch fits clamped MTU",
          sizeof(EntityBatchHeader) + ENTITY_BATCH_MAX_STEAM * sizeof(EntityState) <= 1150);
    CHECK("steam cap under hard receive bound",
          ENTITY_BATCH_MAX_STEAM <= ENTITY_BATCH_MAX);
    CHECK("world-item batch fits datagram",
          sizeof(WorldItemSnapshotHeader) + WORLD_ITEMS_MAX * sizeof(WorldItemEntry) <= 1400);

    // Carried-body sync (protocol 18): the synthetic carry task must never
    // collide with TASK_NONE, the combat stances, or a real engine task key
    // (small ints), and it must classify as carry but NOT as combat.
    CHECK("TASK_CARRY_BODY != TASK_NONE",         TASK_CARRY_BODY != TASK_NONE);
    CHECK("TASK_CARRY_BODY != TASK_COMBAT_MELEE", TASK_CARRY_BODY != TASK_COMBAT_MELEE);
    CHECK("TASK_CARRY_BODY != TASK_COMBAT_WAIT",  TASK_CARRY_BODY != TASK_COMBAT_WAIT);
    CHECK("TASK_CARRY_BODY above engine keys",    TASK_CARRY_BODY >= 0xFE00u);
    CHECK("taskIsCarry(TASK_CARRY_BODY)",         taskIsCarry(TASK_CARRY_BODY));
    CHECK("!taskIsCombat(TASK_CARRY_BODY)",       !taskIsCombat(TASK_CARRY_BODY));
    CHECK("!taskIsCarry(TASK_COMBAT_MELEE)",      !taskIsCarry(TASK_COMBAT_MELEE));
    // BODY_CARRIED is a distinct bit, EXCLUDED from bodyIsDown (the receiver
    // checks bodyIsCarried FIRST and skips the down path for a carried body).
    CHECK("BODY_CARRIED distinct bit",
          BODY_CARRIED != BODY_DOWN && BODY_CARRIED != BODY_RAGDOLL &&
          BODY_CARRIED != BODY_DEAD && BODY_CARRIED != BODY_CRAWL);
    CHECK("bodyIsDown excludes BODY_CARRIED",     !bodyIsDown(BODY_CARRIED));
    CHECK("bodyIsCarried(BODY_CARRIED)",          bodyIsCarried(BODY_CARRIED));
    CHECK("carried+down still reads down",        bodyIsDown(BODY_CARRIED | BODY_DOWN));
    CHECK("carried+down still reads carried",     bodyIsCarried(BODY_CARRIED | BODY_RAGDOLL));
    CHECK("!bodyIsCarried(BODY_DOWN)",            !bodyIsCarried(BODY_DOWN));
    // The new reliable events must be distinct from the existing set.
    CHECK("EVT_PICKUP_BODY distinct",
          EVT_PICKUP_BODY != EVT_NONE && EVT_PICKUP_BODY != EVT_KNOCKOUT &&
          EVT_PICKUP_BODY != EVT_DEATH && EVT_PICKUP_BODY != EVT_REVIVE &&
          EVT_PICKUP_BODY != EVT_AMPUTATE && EVT_PICKUP_BODY != EVT_CRUSH);
    CHECK("EVT_DROP_BODY distinct",
          EVT_DROP_BODY != EVT_PICKUP_BODY && EVT_DROP_BODY != EVT_NONE &&
          EVT_DROP_BODY != EVT_CRUSH);

    // Furniture occupancy (protocol 19): the new bodyState bits are distinct
    // and EXCLUDED from bodyIsDown (the receiver checks bodyInFurniture FIRST,
    // like the carried carve-out).
    CHECK("BODY_IN_BED distinct bit",
          BODY_IN_BED != BODY_DOWN && BODY_IN_BED != BODY_RAGDOLL &&
          BODY_IN_BED != BODY_DEAD && BODY_IN_BED != BODY_CRAWL &&
          BODY_IN_BED != BODY_CARRIED);
    CHECK("BODY_IN_CAGE distinct bit",
          BODY_IN_CAGE != BODY_IN_BED && BODY_IN_CAGE != BODY_DOWN &&
          BODY_IN_CAGE != BODY_RAGDOLL && BODY_IN_CAGE != BODY_DEAD &&
          BODY_IN_CAGE != BODY_CRAWL && BODY_IN_CAGE != BODY_CARRIED);
    CHECK("bodyIsDown excludes occupancy",   !bodyIsDown(BODY_IN_BED | BODY_IN_CAGE));
    CHECK("bodyInFurniture(BODY_IN_BED)",    bodyInFurniture(BODY_IN_BED));
    CHECK("bodyInFurniture(BODY_IN_CAGE)",   bodyInFurniture(BODY_IN_CAGE));
    CHECK("!bodyInFurniture(down|carried)",  !bodyInFurniture(BODY_DOWN | BODY_CARRIED));
    CHECK("occupant+down still reads down",  bodyIsDown(BODY_IN_CAGE | BODY_DOWN));
    // Chained/pole prisoner (protocol 41): distinct bit, rides the furniture
    // carve-out (bodyInFurniture true) but still reads down when KO'd.
    CHECK("BODY_CHAINED distinct bit",
          BODY_CHAINED != BODY_IN_BED && BODY_CHAINED != BODY_IN_CAGE &&
          BODY_CHAINED != BODY_DOWN && BODY_CHAINED != BODY_RAGDOLL &&
          BODY_CHAINED != BODY_DEAD && BODY_CHAINED != BODY_CRAWL &&
          BODY_CHAINED != BODY_CARRIED && BODY_CHAINED != BODY_SNEAK);
    CHECK("bodyChained(BODY_CHAINED)",       bodyChained(BODY_CHAINED));
    CHECK("bodyInFurniture(BODY_CHAINED)",   bodyInFurniture(BODY_CHAINED));
    CHECK("!bodyChained(down|carried)",      !bodyChained(BODY_DOWN | BODY_CARRIED));
    CHECK("chained+down still reads down",   bodyIsDown(BODY_CHAINED | BODY_DOWN));
    // The new reliable events are distinct from the whole existing set.
    CHECK("EVT_ENTER_FURNITURE distinct",
          EVT_ENTER_FURNITURE != EVT_NONE && EVT_ENTER_FURNITURE != EVT_KNOCKOUT &&
          EVT_ENTER_FURNITURE != EVT_DEATH && EVT_ENTER_FURNITURE != EVT_REVIVE &&
          EVT_ENTER_FURNITURE != EVT_AMPUTATE && EVT_ENTER_FURNITURE != EVT_CRUSH &&
          EVT_ENTER_FURNITURE != EVT_PICKUP_BODY && EVT_ENTER_FURNITURE != EVT_DROP_BODY);
    CHECK("EVT_EXIT_FURNITURE distinct",
          EVT_EXIT_FURNITURE != EVT_ENTER_FURNITURE && EVT_EXIT_FURNITURE != EVT_NONE &&
          EVT_EXIT_FURNITURE != EVT_PICKUP_BODY && EVT_EXIT_FURNITURE != EVT_DROP_BODY);

    // Stealth sync (protocol 20).
    CHECK("BODY_SNEAK distinct bit",
          BODY_SNEAK != BODY_DOWN && BODY_SNEAK != BODY_RAGDOLL &&
          BODY_SNEAK != BODY_DEAD && BODY_SNEAK != BODY_CRAWL &&
          BODY_SNEAK != BODY_CARRIED && BODY_SNEAK != BODY_IN_BED &&
          BODY_SNEAK != BODY_IN_CAGE);
    CHECK("bodyIsDown excludes BODY_SNEAK", !bodyIsDown(BODY_SNEAK));
    CHECK("bodySneaking(BODY_SNEAK)",       bodySneaking(BODY_SNEAK));
    CHECK("!bodySneaking(BODY_CRAWL)",      !bodySneaking(BODY_CRAWL));
    CHECK("sneak+crawl still reads sneak",  bodySneaking((u16)(BODY_SNEAK | BODY_CRAWL)));

    // Prone posture (protocol 53). The prone value is a FIELD sharing the
    // bodyState word with the flag bits, so the two must not be able to corrupt
    // each other: the mask must miss every flag, a stamp must preserve the flags,
    // and a re-stamp must REPLACE the previous posture rather than OR into it.
    CHECK("prone mask clears every BODY_ flag",
          (BODY_PRONE_MASK & (BODY_DOWN | BODY_RAGDOLL | BODY_DEAD | BODY_CRAWL |
                              BODY_CARRIED | BODY_IN_BED | BODY_IN_CAGE |
                              BODY_SNEAK | BODY_CHAINED)) == 0);
    CHECK("prone field fits u16",           (BODY_PRONE_MASK >> BODY_PRONE_SHIFT) == 7);
    CHECK("prone round-trip NORMAL",        bodyProne(bodyWithProne(0, PRONE_NORMAL)) == PRONE_NORMAL);
    CHECK("prone round-trip CRIPPLED",      bodyProne(bodyWithProne(0, PRONE_CRIPPLED)) == PRONE_CRIPPLED);
    CHECK("prone round-trip KO (max)",      bodyProne(bodyWithProne(0, PRONE_KO)) == PRONE_KO);
    CHECK("prone values are distinct",
          PRONE_NORMAL != PRONE_STAYING_LOW && PRONE_STAYING_LOW != PRONE_CRIPPLED &&
          PRONE_CRIPPLED != PRONE_PLAYING_DEAD && PRONE_PLAYING_DEAD != PRONE_KO);
    {
        // A crippled crawler as the wire really carries it: BODY_CRAWL (which
        // cannot say WHICH posture) alongside the posture that can.
        u16 s = bodyWithProne((u16)BODY_CRAWL, PRONE_CRIPPLED);
        CHECK("prone stamp preserves flags",    (s & BODY_CRAWL) != 0);
        CHECK("prone stamp reads back",         bodyProne(s) == PRONE_CRIPPLED);
        CHECK("crippled crawler is not down",   !bodyIsDown(s));
        CHECK("crippled crawler is not sneaking", !bodySneaking(s));
        CHECK("bodyFlags strips the posture",   bodyFlags(s) == BODY_CRAWL);
        // Re-stamp: PS_CRIPPLED -> PS_NORMAL must leave 0, not 2|0.
        u16 up = bodyWithProne(s, PRONE_NORMAL);
        CHECK("prone re-stamp replaces",        bodyProne(up) == PRONE_NORMAL);
        CHECK("prone re-stamp keeps flags",     (up & BODY_CRAWL) != 0);
        // Out-of-range degrades to upright rather than corrupting the flags.
        u16 bad = bodyWithProne(s, (u8)7);
        CHECK("prone out-of-range = NORMAL",    bodyProne(bad) == PRONE_NORMAL);
        CHECK("prone out-of-range keeps flags", (bad & BODY_CRAWL) != 0);
    }
    // A posture must never make a body read as down/dead: that is what the
    // `bodyState != 0` call sites (victim pick, downed-enemy count) test.
    CHECK("bodyFlags(prone only) == 0",     bodyFlags(bodyWithProne(0, PRONE_CRIPPLED)) == 0);
    CHECK("prone KO alone is not down",     !bodyIsDown(bodyWithProne(0, PRONE_KO)));
    CHECK("down+prone still reads down",
          bodyIsDown(bodyWithProne((u16)BODY_DOWN, PRONE_KO)));

    // The crawl carve-out, on the EXACT words the engine was measured to stream
    // (run 20260805_152546, leg amputation): 2051 = DOWN|RAGDOLL|PS_KO for the
    // ~2 s collapse, then 1033 = DOWN|CRAWL|PS_CRIPPLED with unc=0 for the crawl.
    // Both are "down" to Character::isDown(), and treating the second as such is
    // what pinned the copy to the ground while its owner crawled away.
    {
        const u16 COLLAPSED = 2051; // DOWN|RAGDOLL, prone=PS_KO
        const u16 CRAWLING  = 1033; // DOWN|CRAWL,   prone=PS_CRIPPLED
        CHECK("measured collapsed word decodes",
              bodyIsDown(COLLAPSED) && bodyProne(COLLAPSED) == PRONE_KO &&
              (COLLAPSED & BODY_RAGDOLL) != 0);
        CHECK("measured crawling word decodes",
              bodyIsDown(CRAWLING) && bodyProne(CRAWLING) == PRONE_CRIPPLED &&
              (CRAWLING & BODY_CRAWL) != 0);
        CHECK("collapsed is not crawling",      !bodyIsCrawling(COLLAPSED));
        CHECK("crawling is crawling",           bodyIsCrawling(CRAWLING));
        CHECK("collapsed is down-not-crawling", bodyDownNotCrawling(COLLAPSED));
        CHECK("crawler is NOT down-not-crawling", !bodyDownNotCrawling(CRAWLING));
        // The four KO/REVIVE edges the publisher derives from that predicate.
        // Getting any of these backwards is how the copy stayed pinned.
        CHECK("upright -> crawl is no KO",
              !(bodyDownNotCrawling(CRAWLING) && !bodyDownNotCrawling(0)));
        CHECK("crawl -> upright is no REVIVE",
              !(!bodyDownNotCrawling(0) && bodyDownNotCrawling(CRAWLING)));
        CHECK("crawl -> collapse IS a KO",
              bodyDownNotCrawling(COLLAPSED) && !bodyDownNotCrawling(CRAWLING));
        CHECK("collapse -> crawl IS a REVIVE",
              !bodyDownNotCrawling(CRAWLING) && bodyDownNotCrawling(COLLAPSED));
        // A dead body is never a crawler, whatever the posture field says.
        CHECK("dead crawler is not crawling",
              !bodyIsCrawling((u16)(CRAWLING | BODY_DEAD)));
        CHECK("dead crawler stays down-not-crawling",
              bodyDownNotCrawling((u16)(CRAWLING | BODY_DEAD)));
        // A sneaker must not be mistaken for a crawler (PS_STAYING_LOW, upright).
        CHECK("low-crouch sneaker is not crawling",
              !bodyIsCrawling(bodyWithProne((u16)(BODY_SNEAK | BODY_CRAWL),
                                            PRONE_STAYING_LOW)));
    }

    // Protocol 53: the crippled CAUSE flag on the medical packet's flags byte.
    CHECK("MED_CRIPPLED distinct bit",
          MED_CRIPPLED != MED_UNCONSCIOUS && MED_CRIPPLED != MED_DEAD &&
          (MED_CRIPPLED & (MED_UNCONSCIOUS | MED_DEAD)) == 0);

    // Recruitment sync (protocol 23): the new reliable event is distinct from
    // the whole existing set (it rides the EventPacket shape unchanged).
    CHECK("EVT_RECRUIT distinct",
          EVT_RECRUIT != EVT_NONE && EVT_RECRUIT != EVT_KNOCKOUT &&
          EVT_RECRUIT != EVT_DEATH && EVT_RECRUIT != EVT_REVIVE &&
          EVT_RECRUIT != EVT_AMPUTATE && EVT_RECRUIT != EVT_CRUSH &&
          EVT_RECRUIT != EVT_PICKUP_BODY && EVT_RECRUIT != EVT_DROP_BODY &&
          EVT_RECRUIT != EVT_ENTER_FURNITURE && EVT_RECRUIT != EVT_EXIT_FURNITURE);

    // Squad management sync (protocol 35, v34): the move re-key event rides
    // the EventPacket shape unchanged; both ends must agree on its id, and
    // the HELLO version gates the mismatch.
    CHECK_EQ("EVT_SQUAD_MOVE id", (int)EVT_SQUAD_MOVE, 11);
    CHECK("EVT_SQUAD_MOVE distinct", EVT_SQUAD_MOVE != EVT_RECRUIT &&
          EVT_SQUAD_MOVE != EVT_NONE && EVT_SQUAD_MOVE != EVT_EXIT_FURNITURE);
    CHECK_EQ("PROTOCOL_VERSION (v55: host-authoritative weather)",
             (int)PROTOCOL_VERSION, 55);

    // Protocol 52: the shared money pool. The two players spend from ONE wallet,
    // so the join reports CHANGES and the host the authoritative TOTAL - swap
    // those roles and concurrent purchases silently mint or burn cats. The
    // shapes are locked here because both halves must stay distinguishable:
    // MoneyPacket carries an ack of the join's delta sequence (which is why the
    // old tabRank field is gone), MoneyDeltaPacket a signed change.
    CHECK("PKT_MONEY_DELTA distinct", PKT_MONEY_DELTA != PKT_MONEY &&
          (int)PKT_MONEY_DELTA == 46);
    {
        MoneyPacket total; std::memset(&total, 0, sizeof(total));
        total.type = (u8)PKT_MONEY; total.ackSeq = 7u; total.money = 4000;
        MoneyDeltaPacket d; std::memset(&d, 0, sizeof(d));
        d.type = (u8)PKT_MONEY_DELTA; d.seq = 8u; d.delta = -250;
        CHECK("pool total carries ack", total.ackSeq == 7u && total.money == 4000);
        CHECK("pool delta is signed", d.delta < 0 && d.seq == 8u);
    }

    // Protocol 48: the parent reference. A worn backpack owns a PRIVATE inventory, so a bagged
    // item is described by no snapshot unless it can name its container. The byte was already
    // reserved, so the entry must not have grown, and index 0 must keep meaning "top level" or
    // every existing entry would silently claim a parent.
    {
        InvItemEntry pe; std::memset(&pe, 0, sizeof(pe));
        CHECK_EQ("parentIdx defaults to top-level (0)", (int)pe.parentIdx, 0);
        CHECK("parentIdx addresses every entry INV_ITEMS_MAX allows", INV_ITEMS_MAX < 256);
    }

    // Protocol 51: the craft GRADE. Kenshi's named grades (Prototype 5 ... Masterwork 95)
    // are points on a 1..100 craft level held in Gear, and `quality` is CONDITION on a
    // different scale entirely - so the grade needs its own field, must be able to express
    // the whole range, and must have a not-applicable value distinct from every real level
    // (level 0 would otherwise read as "mint this at the worst possible grade").
    {
        CHECK_EQ("GRADE_NA is out of the 0..100 craft-level range", (int)GRADE_NA, 255);
        CHECK("GRADE_NA cannot collide with a real craft level", (int)GRADE_NA > 100);
        InvItemEntry a; std::memset(&a, 0, sizeof(a));
        InvItemEntry b = a;
        a.level = 95; b.level = 20;                  // Masterwork vs Shoddy, all else equal
        CHECK("a re-graded item is a CONTENT change (fingerprint moves)",
              invEntryHash(a) != invEntryHash(b));
        // Items with no craft level must hash exactly as they did before the field existed,
        // or protocol 51 would republish every stack of food in the game once.
        InvItemEntry na = a; na.level = GRADE_NA;
        InvItemEntry zero = a; zero.level = 0;
        CHECK("GRADE_NA folds in as 0 (no spurious resend for non-gear)",
              invEntryHash(na) == invEntryHash(zero));
        // The grade must survive a wire round-trip in the same bytes it was written to.
        InvItemEntry rt; std::memset(&rt, 0, sizeof(rt));
        rt.level = 95;
        unsigned char buf[sizeof(InvItemEntry)];
        std::memcpy(buf, &rt, sizeof(rt));
        InvItemEntry back; std::memcpy(&back, buf, sizeof(back));
        CHECK_EQ("InvItemEntry::level round-trips", (int)back.level, 95);
        InvXferPacket xp; std::memset(&xp, 0, sizeof(xp));
        xp.level = 80;
        unsigned char xbuf[sizeof(InvXferPacket)];
        std::memcpy(xbuf, &xp, sizeof(xp));
        InvXferPacket xback; std::memcpy(&xback, xbuf, sizeof(xback));
        CHECK_EQ("InvXferPacket::level round-trips", (int)xback.level, 80);
    }

    // Protocol 46 (inventory item-loss fixes). The entry cap must match the receiver's
    // own read depth (MAXC in applyContainerContents) or a snapshot silently describes
    // less than the peer holds, and it must stay inside the u8 `count` field.
    CHECK_EQ("INV_ITEMS_MAX raised to the receiver's read depth", (int)INV_ITEMS_MAX, 64);
    CHECK("INV_ITEMS_MAX fits the u8 count field", INV_ITEMS_MAX <= 255);
    // The TRUNCATED bit is what stops a partial snapshot being read as a delete. It must
    // be a single low bit so future flags can share the byte.
    CHECK_EQ("INV_FLAG_TRUNCATED value", (int)INV_FLAG_TRUNCATED, 1);
    CHECK("INV_FLAG_TRUNCATED is a single bit",
          (INV_FLAG_TRUNCATED & (u8)(INV_FLAG_TRUNCATED - 1)) == 0);
    // `count` must remain the LAST header field: the entry array is framed immediately
    // after it, so inserting `flags` anywhere else would shift the payload.
    {
        InvSnapshotHeader h;
        std::memset(&h, 0, sizeof(h));
        const unsigned char* base = reinterpret_cast<const unsigned char*>(&h);
        std::size_t offCount = (std::size_t)(reinterpret_cast<const unsigned char*>(&h.count) - base);
        std::size_t offFlags = (std::size_t)(reinterpret_cast<const unsigned char*>(&h.flags) - base);
        CHECK_EQ("InvSnapshotHeader::count is the last field",
                 offCount, sizeof(InvSnapshotHeader) - 1);
        CHECK("InvSnapshotHeader::flags precedes the container key", offFlags < offCount);
    }

    // Protocol 47 (world-item CLAIM: the W1 pickup mirror). The tag must be unique or a
    // claim would be decoded as some other packet and silently destroy the wrong thing.
    CHECK_EQ("PKT_WORLD_ITEM_CLAIM id", (int)PKT_WORLD_ITEM_CLAIM, 43);
    CHECK("PKT_WORLD_ITEM_CLAIM distinct",
          PKT_WORLD_ITEM_CLAIM != PKT_WORLD_ITEM &&
          PKT_WORLD_ITEM_CLAIM != PKT_WORLD_ITEM_REMOVE &&
          PKT_WORLD_ITEM_CLAIM != PKT_COMBAT_HIT &&
          PKT_WORLD_ITEM_CLAIM != PKT_WORLD_DROP &&
          PKT_WORLD_ITEM_CLAIM != PKT_WORLD_PICKUP);
    {
        // The netId array is framed immediately after `count`, exactly as in the cull
        // header, so `count` must stay LAST. A claim also carries authorId (the netIds
        // live in the AUTHOR's id space, not the claimer's) - it must sit BEFORE count.
        WorldItemClaimHeader h;
        std::memset(&h, 0, sizeof(h));
        const unsigned char* base = reinterpret_cast<const unsigned char*>(&h);
        std::size_t offCount  = (std::size_t)(reinterpret_cast<const unsigned char*>(&h.count) - base);
        std::size_t offAuthor = (std::size_t)(reinterpret_cast<const unsigned char*>(&h.authorId) - base);
        std::size_t offOwner  = (std::size_t)(reinterpret_cast<const unsigned char*>(&h.ownerId) - base);
        CHECK_EQ("WorldItemClaimHeader::count is the last field",
                 offCount, sizeof(WorldItemClaimHeader) - 1);
        CHECK("WorldItemClaimHeader::authorId precedes count", offAuthor < offCount);
        CHECK("WorldItemClaimHeader::ownerId precedes authorId", offOwner < offAuthor);
    }
    // A claim batch is capped by the u8 count; even a full one must fit a datagram.
    CHECK("full world-item claim fits datagram",
          sizeof(WorldItemClaimHeader) + 255 * sizeof(u32) <= 1400);
}

// ---- 2. readPacket / packetType round-trips -----------------------------------

// Fill a struct with a deterministic byte pattern (distinct per offset).
template <typename T>
static void fillPattern(T* p, unsigned char seed) {
    unsigned char* b = reinterpret_cast<unsigned char*>(p);
    for (unsigned i = 0; i < sizeof(T); ++i) b[i] = (unsigned char)(seed + i * 7);
}

template <typename T>
static void roundTrip(const char* name, u8 typeTag) {
    T in;
    fillPattern(&in, (unsigned char)(typeTag * 31));
    in.type = typeTag;
    unsigned char buf[512];
    std::memcpy(buf, &in, sizeof(T));

    char label[128];

    T out;
    std::memset(&out, 0, sizeof(T));
    bool okRead = readPacket(buf, (unsigned)sizeof(T), &out);
    // readPacket NUL-terminates every char[] the packet carries (Wire.h,
    // wireSanitize) - a sender that omits the terminator must not be able to
    // walk a receiver off the end of the struct. So the expectation is the sent
    // bytes WITH that applied, not the raw bytes: `buf` above deliberately still
    // holds the unterminated pattern, which is what a hostile sender puts on the
    // wire. That the termination actually happened is proven separately, by
    // testWireTermination - this comparison alone would also pass if the field
    // were zeroed wholesale.
    T expect = in;
    wireSanitize(expect);
    std::sprintf(label, "%s round-trip read", name);
    CHECK(label, okRead && std::memcmp(&expect, &out, sizeof(T)) == 0);

    std::sprintf(label, "%s packetType tag", name);
    CHECK(label, packetType(buf, (unsigned)sizeof(T)) == typeTag);

    // Truncated by one byte: the reader MUST reject (never a partial fill).
    std::sprintf(label, "%s rejects truncated buffer", name);
    CHECK(label, !readPacket(buf, (unsigned)sizeof(T) - 1, &out));
}

static void testRoundTrips() {
    std::printf("== readPacket round-trips + truncation rejection ==\n");
    roundTrip<HelloPacket>("HelloPacket", (u8)PKT_HELLO);
    roundTrip<WelcomePacket>("WelcomePacket", (u8)PKT_WELCOME);
    roundTrip<EventPacket>("EventPacket", (u8)PKT_EVENT);
    roundTrip<WorldDropPacket>("WorldDropPacket", (u8)PKT_WORLD_DROP);
    roundTrip<WorldPickupPacket>("WorldPickupPacket", (u8)PKT_WORLD_PICKUP);
    roundTrip<InvXferPacket>("InvXferPacket", (u8)PKT_INV_XFER);
    roundTrip<MedicalPacket>("MedicalPacket", (u8)PKT_MEDICAL);
    roundTrip<TreatmentPacket>("TreatmentPacket", (u8)PKT_TREATMENT);
    roundTrip<CombatHitPacket>("CombatHitPacket", (u8)PKT_COMBAT_HIT);
    roundTrip<SpeedPacket>("SpeedPacket(REQ)", (u8)PKT_SPEED_REQ);
    roundTrip<SpeedPacket>("SpeedPacket(SET)", (u8)PKT_SPEED_SET);
    roundTrip<StatsPacket>("StatsPacket", (u8)PKT_STATS);
    roundTrip<MoneyPacket>("MoneyPacket", (u8)PKT_MONEY);
    roundTrip<MoneyDeltaPacket>("MoneyDeltaPacket", (u8)PKT_MONEY_DELTA);
    roundTrip<FactionPacket>("FactionPacket", (u8)PKT_FACTION);
    roundTrip<TimePacket>("TimePacket", (u8)PKT_TIME);
    roundTrip<DoorPacket>("DoorPacket", (u8)PKT_DOOR);
    roundTrip<BuildPlacePacket>("BuildPlacePacket", (u8)PKT_BUILD_PLACE);
    roundTrip<BuildStatePacket>("BuildStatePacket", (u8)PKT_BUILD_STATE);
    roundTrip<BuildDoorPacket>("BuildDoorPacket", (u8)PKT_BUILD_DOOR);
    roundTrip<BuildRemovePacket>("BuildRemovePacket", (u8)PKT_BUILD_REMOVE);
    roundTrip<StealthPacket>("StealthPacket", (u8)PKT_STEALTH);
    roundTrip<SpawnReqPacket>("SpawnReqPacket", (u8)PKT_SPAWN_REQ);
    roundTrip<SpawnInfoPacket>("SpawnInfoPacket", (u8)PKT_SPAWN_INFO);
    roundTrip<SaveReqPacket>("SaveReqPacket", (u8)PKT_SAVE_REQ);
    roundTrip<SaveBeginPacket>("SaveBeginPacket", (u8)PKT_SAVE_BEGIN);
    roundTrip<SaveAckPacket>("SaveAckPacket", (u8)PKT_SAVE_ACK);
    roundTrip<LoadGoPacket>("LoadGoPacket", (u8)PKT_LOAD_GO);
    roundTrip<LoadReqPacket>("LoadReqPacket", (u8)PKT_LOAD_REQ);
    roundTrip<LoadNackPacket>("LoadNackPacket", (u8)PKT_LOAD_NACK);
    roundTrip<ProdPacket>("ProdPacket", (u8)PKT_PROD);
    roundTrip<ResearchPacket>("ResearchPacket", (u8)PKT_RESEARCH);
    roundTrip<DeedPacket>("DeedPacket", (u8)PKT_DEED);
    roundTrip<CellClaimPacket>("CellClaimPacket", (u8)PKT_CELL_CLAIM);
    roundTrip<InvXferAckPacket>("InvXferAckPacket", (u8)PKT_INV_XFER_ACK);

    CHECK("packetType(null) == 0", packetType(0, 10) == 0);
    unsigned char b0[1] = { 0 };
    CHECK("packetType(len 0) == 0", packetType(b0, 0) == 0);
    CHECK("readPacket(null) rejected", !readPacket<HelloPacket>(0, 4, (HelloPacket*)b0) || true);
}

// ---- 2b. every char[] on the wire is terminated on receipt ---------------------
//
// The threat is a peer that fills a fixed-size field to its last byte with no
// terminator. Every one of these fields then reaches a std::string(...) or a
// strcmp(...), and one family of them (the save/load names) is used to build a
// filesystem path - so the read runs off the end of the packet struct into
// whatever the caller had on its stack.
//
// This used to be the receive site's job, one wireTerm() per field at each arm
// of NetLink's dispatch, and it was forgotten for five packets. It now happens
// inside readPacket(), which every receive path already funnels through.
//
// The buffer here is filled with 0xFF, NOT with a pattern: it is the exact
// hostile input - no byte anywhere is a terminator - so a field that comes out
// terminated can only have been terminated by us.
//
// Contract.Tests.ps1 checks the other half, the half a C++ test cannot see: that
// no char[] in Wire.h is MISSING from the wireSanitize overload set. Both are
// needed - this file proves the mechanism works, that one proves it is complete.
#define TERM_CHECK(TYPE, TAG, FIELD)                                          \
    do {                                                                      \
        unsigned char b[512];                                                 \
        std::memset(b, 0xFF, sizeof(b));                                      \
        b[0] = (unsigned char)(TAG);                                          \
        TYPE p;                                                               \
        std::memset(&p, 0, sizeof(p));                                        \
        bool r = readPacket(b, (unsigned)sizeof(TYPE), &p);                   \
        char lbl[160];                                                        \
        std::sprintf(lbl, "%s.%s terminated on receipt", #TYPE, #FIELD);      \
        CHECK(lbl, r && p.FIELD[sizeof(p.FIELD) - 1] == '\0');                \
    } while (0)

static void testWireTermination() {
    std::printf("== wire char[] termination on receipt ==\n");

    TERM_CHECK(WorldDropPacket,   PKT_WORLD_DROP,   stringID);
    TERM_CHECK(WorldDropPacket,   PKT_WORLD_DROP,   manufacturer);
    TERM_CHECK(WorldDropPacket,   PKT_WORLD_DROP,   material);
    TERM_CHECK(WorldPickupPacket, PKT_WORLD_PICKUP, stringID);
    TERM_CHECK(InvXferPacket,     PKT_INV_XFER,     stringID);
    TERM_CHECK(InvXferPacket,     PKT_INV_XFER,     manufacturer);
    TERM_CHECK(InvXferPacket,     PKT_INV_XFER,     material);
    TERM_CHECK(MedicalPacket,     PKT_MEDICAL,      limbSid[0]);
    TERM_CHECK(MedicalPacket,     PKT_MEDICAL,      limbSid[1]);
    TERM_CHECK(MedicalPacket,     PKT_MEDICAL,      limbSid[2]);
    TERM_CHECK(MedicalPacket,     PKT_MEDICAL,      limbSid[3]);
    TERM_CHECK(FactionPacket,     PKT_FACTION,      sid);
    TERM_CHECK(DeedPacket,        PKT_DEED,         ownerSid);
    TERM_CHECK(BuildPlacePacket,  PKT_BUILD_PLACE,  sid);
    TERM_CHECK(SpawnInfoPacket,   PKT_SPAWN_INFO,   charSid);
    TERM_CHECK(SpawnInfoPacket,   PKT_SPAWN_INFO,   facSid);
    TERM_CHECK(ProdPacket,        PKT_PROD,         outSid);
    TERM_CHECK(ResearchPacket,    PKT_RESEARCH,     sid);

    // The save/load name family - the five that were missed when this was done
    // per-receive-site, and the ones whose field becomes a path.
    TERM_CHECK(SaveReqPacket,     PKT_SAVE_REQ,     name);
    TERM_CHECK(SaveBeginPacket,   PKT_SAVE_BEGIN,   name);
    TERM_CHECK(LoadGoPacket,      PKT_LOAD_GO,      name);
    TERM_CHECK(LoadReqPacket,     PKT_LOAD_REQ,     name);
    TERM_CHECK(LoadNackPacket,    PKT_LOAD_NACK,    name);

    // Negative control: the mechanism must not be zeroing the whole field. Byte 0
    // of a 0xFF buffer stays 0xFF, so a wholesale memset would fail this - which
    // is the difference between "terminated" and "destroyed".
    unsigned char b[512];
    std::memset(b, 0xFF, sizeof(b));
    b[0] = (u8)PKT_SAVE_BEGIN;
    SaveBeginPacket sb;
    std::memset(&sb, 0, sizeof(sb));
    bool okSb = readPacket(b, (unsigned)sizeof(SaveBeginPacket), &sb);
    CHECK("termination truncates, it does not clear the field",
          okSb && sb.name[0] == (char)0xFF && sb.name[46] == (char)0xFF);
}
#undef TERM_CHECK

// ---- 3. Field-offset lock (HELLO version + batch framing) -----------------------

static void testFraming() {
    std::printf("== field offsets + batch framing ==\n");

    // HELLO: [u8 type][u16 version][u8 nameLen] - the version check that rejects
    // mismatched builds depends on this exact layout.
    unsigned char hello[4];
    hello[0] = (unsigned char)PKT_HELLO;
    hello[1] = (unsigned char)(PROTOCOL_VERSION & 0xFF);
    hello[2] = (unsigned char)((PROTOCOL_VERSION >> 8) & 0xFF);
    hello[3] = 0;
    HelloPacket h;
    CHECK("HELLO parses from raw bytes", readPacket(hello, 4, &h));
    CHECK_EQ("HELLO version field offset", h.version, PROTOCOL_VERSION);
    CHECK("HELLO version mismatch detectable", ((u16)(PROTOCOL_VERSION + 1)) != h.version);

    // Entity batch framing: [EntityBatchHeader][EntityState*count], the exact
    // bounds check NetLink applies ("len >= need") must hold for a full batch
    // and reject a batch whose count field overruns the actual payload.
    const unsigned N = 3;
    unsigned char buf[sizeof(EntityBatchHeader) + 3 * sizeof(EntityState)];
    EntityBatchHeader hdr;
    hdr.type = (u8)PKT_ENTITY_BATCH; hdr.ownerId = 42; hdr.sendMs = 123456u;
    hdr.epoch = 7u; hdr.count = (u8)N;
    std::memcpy(buf, &hdr, sizeof(hdr));
    EntityState src[N];
    for (unsigned i = 0; i < N; ++i) {
        fillPattern(&src[i], (unsigned char)(i * 13 + 1));
        std::memcpy(buf + sizeof(hdr) + i * sizeof(EntityState), &src[i], sizeof(EntityState));
    }
    unsigned len = (unsigned)sizeof(buf);
    EntityBatchHeader rh;
    std::memcpy(&rh, buf, sizeof(rh));
    unsigned need = (unsigned)sizeof(EntityBatchHeader) + (unsigned)rh.count * (unsigned)sizeof(EntityState);
    CHECK("entity batch: full payload accepted",
          len >= need && rh.count == N && rh.ownerId == 42 && rh.sendMs == 123456u
          && rh.epoch == 7u);
    bool all = true;
    for (unsigned i = 0; i < N; ++i) {
        EntityState e;
        std::memcpy(&e, buf + sizeof(rh) + i * sizeof(EntityState), sizeof(e));
        if (std::memcmp(&e, &src[i], sizeof(e)) != 0) all = false;
    }
    CHECK("entity batch: entries round-trip", all);
    // Lying count: header claims one more entity than the datagram carries.
    rh.count = (u8)(N + 1);
    need = (unsigned)sizeof(EntityBatchHeader) + (unsigned)rh.count * (unsigned)sizeof(EntityState);
    CHECK("entity batch: overrun count rejected by len>=need", !(len >= need));

    // NPC census framing (protocol 36): [NpcCensusHeader][u32 hand[5] * count],
    // the exact "len >= need" bound NetLink applies plus the NPC_CENSUS_MAX cap.
    {
        const unsigned CN = 4;
        unsigned char cbuf[sizeof(NpcCensusHeader) + CN * 5 * sizeof(u32)];
        NpcCensusHeader ch;
        ch.type = (u8)PKT_NPC_CENSUS; ch.ownerId = 1; ch.count = (u16)CN;
        std::memcpy(cbuf, &ch, sizeof(ch));
        u32 hands[CN * 5];
        for (unsigned i = 0; i < CN * 5; ++i) hands[i] = 1000u + i;
        std::memcpy(cbuf + sizeof(ch), hands, sizeof(hands));
        NpcCensusHeader cr;
        std::memcpy(&cr, cbuf, sizeof(cr));
        unsigned clen  = (unsigned)sizeof(cbuf);
        unsigned cneed = (unsigned)sizeof(NpcCensusHeader) + (unsigned)cr.count * 5 * (unsigned)sizeof(u32);
        CHECK("npc census: full payload accepted",
              clen >= cneed && cr.count == CN && cr.count <= NPC_CENSUS_MAX);
        u32 back[CN * 5];
        std::memcpy(back, cbuf + sizeof(cr), sizeof(back));
        CHECK("npc census: hands round-trip", std::memcmp(back, hands, sizeof(hands)) == 0);
        cr.count = (u16)(CN + 1);
        cneed = (unsigned)sizeof(NpcCensusHeader) + (unsigned)cr.count * 5 * (unsigned)sizeof(u32);
        CHECK("npc census: overrun count rejected by len>=need", !(clen >= cneed));
        CHECK("npc census: cap sane", NPC_CENSUS_MAX >= 256 && NPC_CENSUS_MAX <= 2048);
    }

    // Save-file chunk framing (protocol 31): [SaveFileHeader][path][payload],
    // the exact "len >= need" bound NetLink applies, plus the pathLen/dataLen
    // sanity caps that reject a malformed chunk.
    {
        const char* relPath = "platoon\\Drifters_0.platoon";
        const unsigned pl = (unsigned)std::strlen(relPath);
        const unsigned dl = 100;
        unsigned char sbuf[sizeof(SaveFileHeader) + 64 + 100];
        SaveFileHeader fh;
        fh.type = (u8)PKT_SAVE_FILE; fh.ownerId = 0; fh.xferId = 7;
        fh.fileIdx = 3; fh.pathLen = (u16)pl; fh.offset = 4096; fh.dataLen = (u16)dl;
        std::memcpy(sbuf, &fh, sizeof(fh));
        std::memcpy(sbuf + sizeof(fh), relPath, pl);
        for (unsigned i = 0; i < dl; ++i) sbuf[sizeof(fh) + pl + i] = (unsigned char)i;
        unsigned slen = (unsigned)(sizeof(fh) + pl + dl);

        SaveFileHeader rfh;
        std::memcpy(&rfh, sbuf, sizeof(rfh));
        unsigned sneed = (unsigned)sizeof(SaveFileHeader) + rfh.pathLen + rfh.dataLen;
        CHECK("save chunk: full payload accepted",
              slen >= sneed && rfh.pathLen > 0 && rfh.pathLen <= SAVE_PATH_MAX &&
              rfh.dataLen <= SAVE_CHUNK_MAX);
        CHECK("save chunk: path bytes at header end",
              std::memcmp(sbuf + sizeof(SaveFileHeader), relPath, pl) == 0);
        CHECK("save chunk: payload follows path",
              sbuf[sizeof(SaveFileHeader) + pl + 42] == 42);
        // Lying dataLen: claims more payload than the packet carries.
        rfh.dataLen = (u16)(dl + 1);
        sneed = (unsigned)sizeof(SaveFileHeader) + rfh.pathLen + rfh.dataLen;
        CHECK("save chunk: overrun dataLen rejected by len>=need", !(slen >= sneed));
        // Oversized dataLen: above the chunk cap even if the bytes were there.
        rfh.dataLen = (u16)(SAVE_CHUNK_MAX + 1);
        CHECK("save chunk: dataLen above SAVE_CHUNK_MAX rejected",
              !(rfh.dataLen <= SAVE_CHUNK_MAX));
        // Zero pathLen: a chunk with no relative path is malformed.
        rfh.pathLen = 0;
        CHECK("save chunk: zero pathLen rejected", !(rfh.pathLen > 0));
    }

    // Save-done framing: [SaveDoneHeader][u32 crc * fileCount].
    {
        const unsigned FC = 5;
        unsigned char dbuf[sizeof(SaveDoneHeader) + FC * sizeof(u32)];
        SaveDoneHeader dh;
        dh.type = (u8)PKT_SAVE_DONE; dh.ownerId = 0; dh.xferId = 7; dh.fileCount = FC;
        std::memcpy(dbuf, &dh, sizeof(dh));
        u32 crcs[FC] = { 1, 2, 3, 4, 5 };
        std::memcpy(dbuf + sizeof(dh), crcs, sizeof(crcs));
        unsigned dlen = (unsigned)sizeof(dbuf);
        SaveDoneHeader rdh;
        std::memcpy(&rdh, dbuf, sizeof(rdh));
        unsigned dneed = (unsigned)sizeof(SaveDoneHeader) + rdh.fileCount * (unsigned)sizeof(u32);
        CHECK("save done: full CRC table accepted", dlen >= dneed && rdh.fileCount == FC);
        rdh.fileCount = (u16)(FC + 1);
        dneed = (unsigned)sizeof(SaveDoneHeader) + rdh.fileCount * (unsigned)sizeof(u32);
        CHECK("save done: overrun fileCount rejected by len>=need", !(dlen >= dneed));
    }
}

// ---- 3b. Save-transfer CRC (protocol 31): incremental FNV-1a-32 ------------------
// The receiver folds each arriving chunk into the file's running CRC; the
// sender does the same while reading. Chunk-split invariance IS the
// reassembly correctness proof: however the file is cut into chunks, the
// final CRC equals the whole-file hash the sender put in the DONE table.

static void testSaveCrc() {
    std::printf("== save-transfer CRC (fnv1a incremental) ==\n");
    unsigned char data[10000];
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = (unsigned char)(i * 31 + (i >> 8));

    // One-shot reference.
    unsigned ref = fnv1aUpdate(fnv1aInit(), data, sizeof(data));
    CHECK("crc deterministic", fnv1aUpdate(fnv1aInit(), data, sizeof(data)) == ref);

    // 4 KB chunking (the wire chunk size) folds to the same value.
    unsigned h = fnv1aInit();
    for (unsigned off = 0; off < sizeof(data); off += SAVE_CHUNK_MAX) {
        unsigned n = sizeof(data) - off;
        if (n > SAVE_CHUNK_MAX) n = SAVE_CHUNK_MAX;
        h = fnv1aUpdate(h, data + off, n);
    }
    CHECK("crc chunk-split invariant (4 KB chunks)", h == ref);

    // Pathological 1-byte chunks fold to the same value too.
    h = fnv1aInit();
    for (unsigned i = 0; i < sizeof(data); ++i) h = fnv1aUpdate(h, data + i, 1);
    CHECK("crc chunk-split invariant (1 B chunks)", h == ref);

    // A single flipped byte perturbs the CRC (corruption is caught).
    data[5000] ^= 1;
    CHECK("crc detects a flipped byte", fnv1aUpdate(fnv1aInit(), data, sizeof(data)) != ref);
    data[5000] ^= 1;

    // Empty file: CRC = the FNV offset basis, same on both ends.
    CHECK("crc of empty file = fnv basis", fnv1aInit() == 2166136261u);
}

// ---- 3b. Folder fingerprint (protocol 32 coordinated load) --------------------
// The join compares the host's LOAD_GO fingerprint against its own on-disk
// copy - equality must mean "byte-identical folder" regardless of directory
// enumeration order or path case, and any divergence must perturb it.

static void testFolderFingerprint() {
    std::printf("== folder fingerprint (coordinated load) ==\n");
    const char* paths[4] = { "quick.save", "platoon\\a.platoon",
                             "platoon\\b.platoon", "zone\\zone.1.2.zone" };
    unsigned int crcs[4] = { 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u };
    unsigned ref = folderFingerprintOf(paths, crcs, 4);

    CHECK("fp deterministic", folderFingerprintOf(paths, crcs, 4) == ref);
    CHECK("fp nonzero (0 reserved for missing)", ref != 0);

    // Enumeration-order invariance: FindFirstFile order differs by filesystem;
    // the same (path, crc) SET must fingerprint identically.
    const char* paths2[4] = { paths[2], paths[0], paths[3], paths[1] };
    unsigned int crcs2[4] = { crcs[2], crcs[0], crcs[3], crcs[1] };
    CHECK("fp enumeration-order invariant",
          folderFingerprintOf(paths2, crcs2, 4) == ref);

    // Windows path case-insensitivity: the same folder listed with different
    // case must agree cross-machine.
    const char* paths3[4] = { "QUICK.SAVE", "Platoon\\A.platoon",
                              "platoon\\b.PLATOON", "zone\\ZONE.1.2.zone" };
    CHECK("fp path-case invariant", folderFingerprintOf(paths3, crcs, 4) == ref);

    // Sensitivity: one changed file content, a renamed path, a missing file
    // and an added file must all perturb the value.
    unsigned int crcs4[4] = { crcs[0], crcs[1] ^ 1u, crcs[2], crcs[3] };
    CHECK("fp detects changed file content",
          folderFingerprintOf(paths, crcs4, 4) != ref);
    const char* paths5[4] = { "quick.save", "platoon\\a.platoon",
                              "platoon\\c.platoon", "zone\\zone.1.2.zone" };
    CHECK("fp detects renamed path", folderFingerprintOf(paths5, crcs, 4) != ref);
    CHECK("fp detects missing file", folderFingerprintOf(paths, crcs, 3) != ref);
    const char* paths6[5] = { paths[0], paths[1], paths[2], paths[3], "extra.bin" };
    unsigned int crcs6[5] = { crcs[0], crcs[1], crcs[2], crcs[3], 0x55555555u };
    CHECK("fp detects added file", folderFingerprintOf(paths6, crcs6, 5) != ref);

    // Empty folder = 0 (the "missing/unreadable" sentinel).
    CHECK("fp of empty set = 0", folderFingerprintOf(paths, crcs, 0) == 0);
}

// ---- 3c. Save-transfer receiver round-trip (protocol 31) ----------------------
// The tests above lock the wire framing + CRC math in isolation. This one drives
// the REAL receiver in SaveXfer.cpp (onSaveBegin/onSaveFile/onSaveDone ->
// stage/verify/commit) end-to-end: it builds the BEGIN/FILE/DONE stream a host
// would send from an in-memory file set, feeds it to the receiver against a temp
// save-root, and asserts the committed folder is byte-identical - the very step
// players report failing ("the initial save didn't transfer"). A second transfer
// with a corrupted chunk must FAIL the commit and leave the prior save untouched.

struct XferSrcFile { const char* rel; const unsigned char* data; unsigned len; };

static bool xferReadWhole(const std::string& path, std::vector<unsigned char>* out) {
    out->clear();
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, 0) && got > 0)
        out->insert(out->end(), buf, buf + got);
    CloseHandle(h);
    return true;
}

static void xferNukeDir(const std::string& dir) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.' && (fd.cFileName[1] == '\0' ||
                (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0'))) continue;
            std::string child = dir + "\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) xferNukeDir(child);
            else { SetFileAttributesA(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                   DeleteFileA(child.c_str()); }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

// Feed one transfer (xferId) for 'name' built from srcs[nsrc]. corruptFileIdx>=0
// flips one received payload byte of that file so the receiver's CRC won't match
// the DONE table (a wire-corruption simulation). Returns onSaveDone (1/0).
static int xferRun(const char* name, const XferSrcFile* srcs, unsigned nsrc,
                   u32 xferId, int corruptFileIdx) {
    SaveBeginPacket bp;
    std::memset(&bp, 0, sizeof(bp));
    bp.type = (u8)PKT_SAVE_BEGIN; bp.ownerId = 1; bp.xferId = xferId;
    std::strncpy(bp.name, name, sizeof(bp.name) - 1);
    bp.fileCount = (u16)nsrc;
    unsigned __int64 total = 0;
    for (unsigned i = 0; i < nsrc; ++i) total += srcs[i].len;
    bp.totalBytes = total;
    savexfer::onSaveBegin(bp);

    std::vector<u32> crcs(nsrc, 0);
    for (unsigned i = 0; i < nsrc; ++i) {
        const XferSrcFile& f = srcs[i];
        crcs[i] = fnv1aUpdate(fnv1aInit(), f.data, f.len); // whole-file CRC (DONE table)
        unsigned off = 0;
        do {
            unsigned n = f.len - off;
            if (n > SAVE_CHUNK_MAX) n = SAVE_CHUNK_MAX;
            std::vector<unsigned char> chunk;
            if (n > 0) chunk.assign(f.data + off, f.data + off + n);
            if ((int)i == corruptFileIdx && off == 0 && n > 0) chunk[0] ^= 0xFF;
            SaveFileHeader fh;
            fh.type = (u8)PKT_SAVE_FILE; fh.ownerId = 1; fh.xferId = xferId;
            fh.fileIdx = (u16)i; fh.pathLen = (u16)std::strlen(f.rel);
            fh.offset = off; fh.dataLen = (u16)n;
            savexfer::onSaveFile(fh, f.rel,
                                 n ? &chunk[0] : (const unsigned char*)"");
            off += n;
        } while (off < f.len);
    }

    SaveDoneHeader dh;
    dh.type = (u8)PKT_SAVE_DONE; dh.ownerId = 1; dh.xferId = xferId;
    dh.fileCount = (u16)nsrc;
    u16 of = 0; unsigned __int64 ob = 0;
    return savexfer::onSaveDone(dh, crcs.empty() ? (const u32*)0 : &crcs[0], &of, &ob);
}

static void testSaveXferRoundTrip() {
    std::printf("== save-transfer receiver round-trip (stage/verify/commit) ==\n");

    // Temp save-root so the receiver never touches the real save folder.
    char tmp[MAX_PATH]; tmp[0] = '\0';
    GetTempPathA(sizeof(tmp), tmp);
    char root[MAX_PATH];
    _snprintf(root, sizeof(root) - 1, "%skc_xfer_test_%lu", tmp,
              (unsigned long)GetCurrentProcessId());
    root[sizeof(root) - 1] = '\0';
    std::string rootStr = root;
    xferNukeDir(rootStr);                 // best-effort clean from a prior run
    CreateDirectoryA(rootStr.c_str(), 0);
    savexfer::setSaveRootForTest(rootStr);

    // A representative save: a multi-chunk core, two subdir files, and an empty
    // file (exercises subdir creation + the dataLen=0 chunk path).
    std::vector<unsigned char> quick(5000), plat(1234), zone(1);
    for (unsigned i = 0; i < quick.size(); ++i) quick[i] = (unsigned char)(i * 7 + 3);
    for (unsigned i = 0; i < plat.size();  ++i) plat[i]  = (unsigned char)(i * 13 + 1);
    zone[0] = 0xAB;
    XferSrcFile srcs[4];
    srcs[0].rel = "quick.save";                  srcs[0].data = &quick[0]; srcs[0].len = (unsigned)quick.size();
    srcs[1].rel = "platoon\\Drifters_0.platoon"; srcs[1].data = &plat[0];  srcs[1].len = (unsigned)plat.size();
    srcs[2].rel = "zone\\zone.1.2.zone";         srcs[2].data = &zone[0];  srcs[2].len = (unsigned)zone.size();
    srcs[3].rel = "meta\\empty.dat";             srcs[3].data = (const unsigned char*)""; srcs[3].len = 0;

    // 1) Clean transfer -> commit, byte-identical folder.
    int r1 = xferRun("coopresume", srcs, 4, /*xferId*/1, /*corrupt*/-1);
    CHECK("xfer clean commit returns ok", r1 == 1);
    CHECK("xfer lastCommitResult ok", savexfer::lastCommitResult() == 1);
    CHECK("xfer commitSeq advanced", savexfer::commitSeq() >= 1);

    std::string commit = savexfer::saveFolderFor("coopresume");
    bool allMatch = true;
    for (unsigned i = 0; i < 4; ++i) {
        std::vector<unsigned char> got;
        bool ok = xferReadWhole(commit + "\\" + srcs[i].rel, &got);
        bool same = ok && got.size() == srcs[i].len &&
                    (srcs[i].len == 0 ||
                     std::memcmp(&got[0], srcs[i].data, srcs[i].len) == 0);
        if (!same) allMatch = false;
    }
    CHECK("xfer committed folder is byte-identical (incl. subdirs + empty file)",
          allMatch);

    std::string staging = savexfer::saveFolderFor(std::string("coopresume") + "__incoming");
    CHECK("xfer staging removed after commit",
          GetFileAttributesA(staging.c_str()) == INVALID_FILE_ATTRIBUTES);

    // 2) Corrupted chunk -> commit FAILS, prior save untouched, staging discarded.
    int r2 = xferRun("coopresume", srcs, 4, /*xferId*/2, /*corrupt file*/0);
    CHECK("xfer corrupt chunk fails commit", r2 == 0);
    CHECK("xfer corrupt lastCommitResult fail", savexfer::lastCommitResult() == 0);
    {
        std::vector<unsigned char> got;
        bool ok = xferReadWhole(commit + "\\quick.save", &got);
        bool intact = ok && got.size() == quick.size() &&
                      std::memcmp(&got[0], &quick[0], quick.size()) == 0;
        CHECK("xfer failed commit leaves the previous save intact", intact);
    }
    CHECK("xfer failed commit discards staging",
          GetFileAttributesA(staging.c_str()) == INVALID_FILE_ATTRIBUTES);

    savexfer::setSaveRootForTest(std::string()); // unpin
    xferNukeDir(rootStr);
}

// ---- 4. Content hash (the inventory convergence key) -----------------------------

static InvItemEntry makeEntry() {
    InvItemEntry e;
    std::memset(&e, 0, sizeof(e));
    std::strcpy(e.stringID, "wooden_sandals");
    e.itemType = 7; e.quantity = 2; e.quality = 150;
    e.equipped = 0; e.slot = 0; e.section = 0;
    std::strcpy(e.manufacturer, "");
    std::strcpy(e.material, "");
    return e;
}

static void testContentHash() {
    std::printf("== content hash (ContentHash.h) ==\n");
    InvItemEntry a = makeEntry();
    InvItemEntry b = makeEntry();
    CHECK("hash deterministic (equal entries equal)", invEntryHash(a) == invEntryHash(b));

    // Every field that defines content identity must perturb the hash.
    unsigned base = invEntryHash(a);
    b = makeEntry(); std::strcpy(b.stringID, "wooden_sandalz");
    CHECK("stringID perturbs hash",     invEntryHash(b) != base);
    b = makeEntry(); b.itemType = 8;
    CHECK("itemType perturbs hash",     invEntryHash(b) != base);
    b = makeEntry(); b.quantity = 3;
    CHECK("quantity perturbs hash",     invEntryHash(b) != base);
    b = makeEntry(); b.quality = 151;
    CHECK("quality perturbs hash",      invEntryHash(b) != base);
    b = makeEntry(); b.equipped = 1;
    CHECK("equipped perturbs hash",     invEntryHash(b) != base);
    b = makeEntry(); b.slot = 5;
    CHECK("slot perturbs hash",         invEntryHash(b) != base);
    b = makeEntry(); b.locked = 1;
    CHECK("locked perturbs hash",       invEntryHash(b) != base);
    b = makeEntry(); b.section = 1234;
    CHECK("section perturbs hash",      invEntryHash(b) != base);
    // WHERE it sits is content: the same stack loose on the character vs inside a worn bag is
    // otherwise field-for-field identical, so without this the move never publishes.
    b = makeEntry(); b.parentIdx = 1;
    CHECK("parentIdx perturbs hash",    invEntryHash(b) != base);
    b = makeEntry(); std::strcpy(b.manufacturer, "cross");
    CHECK("manufacturer perturbs hash", invEntryHash(b) != base);
    b = makeEntry(); std::strcpy(b.material, "iron");
    CHECK("material perturbs hash",     invEntryHash(b) != base);

    // Order independence: the container fingerprint is the SUM of entry hashes,
    // so any permutation of the same multiset must produce the same sum.
    InvItemEntry e1 = makeEntry();
    InvItemEntry e2 = makeEntry(); std::strcpy(e2.stringID, "iron_katana"); e2.equipped = 1;
    InvItemEntry e3 = makeEntry(); e3.quantity = 9;
    unsigned s123 = invEntryHash(e1) + invEntryHash(e2) + invEntryHash(e3);
    unsigned s312 = invEntryHash(e3) + invEntryHash(e1) + invEntryHash(e2);
    CHECK("container sum order-independent", s123 == s312);

    // Section-name hash: '' reserved as 0 (loose); non-empty never 0; stable.
    CHECK("sectionNameHash('') == 0",      sectionNameHash("") == 0);
    CHECK("sectionNameHash(null) == 0",    sectionNameHash(0) == 0);
    CHECK("sectionNameHash nonzero",       sectionNameHash("hip") != 0);
    CHECK("sectionNameHash deterministic", sectionNameHash("hip") == sectionNameHash("hip"));
    CHECK("sectionNameHash distinguishes weapon slots", sectionNameHash("hip") != sectionNameHash("back"));

    // Canonical vector: print (not assert) so the baseline doc can record it and
    // a future intentional change is visible in the diff.
    std::printf("  note canonical invEntryHash(wooden_sandals x2 q150) = %u\n", base);
}

// ---- 5. Interpolation buffer invariants -------------------------------------------

static EntityState entAt(float x) {
    EntityState e;
    std::memset(&e, 0, sizeof(e));
    e.hIndex = 1; e.hSerial = 2; e.task = TASK_NONE;
    e.x = x; e.y = 0.0f; e.z = 0.0f; e.heading = 0.0f;
    return e;
}

static void testInterp() {
    std::printf("== interpolation buffer (Interp.cpp) ==\n");
    InterpConfig cfg; // min 50 / max 200 delay, extrap 250, snap 50u, stale 2000

    // Bracketed interpolation: 20 Hz feed moving +1u per 50ms tick.
    {
        EntityInterp it;
        for (int i = 0; i <= 10; ++i) it.push(entAt((float)i), 1000 + i * 50);
        // nowMs=1550 -> renderTime = 1550 - delay(>=50,<=200) = [1350,1500]
        // -> x must interpolate inside [7,10] and never exceed the newest.
        EntityState out;
        bool ok = it.sample(1550, cfg, &out);
        CHECK("bracketed sample returns data", ok);
        CHECK("bracketed sample within segment bounds", ok && out.x >= 6.9f && out.x <= 10.01f);

        // Monotonic advance: successive sample times never move the body backwards.
        float prev = -1.0f; bool mono = true;
        for (unsigned long t = 1400; t <= 1550; t += 10) {
            EntityState o;
            if (it.sample(t, cfg, &o)) { if (o.x < prev - 0.001f) mono = false; prev = o.x; }
        }
        CHECK("sampled position monotonic for monotone source", mono);
    }

    // Dead-reckoning cap: starved buffer extrapolates at most maxExtrapMs beyond
    // the newest snapshot (here: 1u/50ms -> cap = +5u over newest).
    {
        EntityInterp it;
        it.push(entAt(0.0f), 1000);
        it.push(entAt(1.0f), 1050);
        EntityState out;
        bool ok = it.sample(2900, cfg, &out); // renderTime far past newest, still < stale
        CHECK("starved sample still returns (dead-reckon)", ok);
        CHECK("dead-reckoning capped at maxExtrapMs", ok && out.x <= 1.0f + 5.0f + 0.01f);
    }

    // Staleness: a stream older than staleMs releases the body (sample -> false).
    {
        EntityInterp it;
        it.push(entAt(0.0f), 1000);
        EntityState out;
        CHECK("stale stream releases body", !it.sample(1000 + cfg.staleMs + 500, cfg, &out));
    }

    // Teleport snap: a segment step beyond snapDist snaps to the newer end
    // instead of smearing the body across the gap.
    {
        EntityInterp it;
        it.push(entAt(0.0f), 1000);
        it.push(entAt(1000.0f), 1050); // 1000u jump >> 50u snap distance
        it.push(entAt(1001.0f), 1100);
        EntityState out;
        bool ok = it.sample(1120, cfg, &out); // renderTime ~1070 -> inside the jump segment... 
        // renderTime lands in [1000,1050] or [1050,1100] depending on adaptive delay;
        // in the jump segment we must NOT see a smeared mid-point (x in ~[100,900]).
        bool smeared = ok && out.x > 100.0f && out.x < 900.0f;
        CHECK("teleport does not smear", ok && !smeared);
    }

    // Dead-reckon past a teleport: when the LAST segment is a jump (a park /
    // fast-travel) and the render time runs past the newest sample, the buffer
    // must HOLD the newest pose - never dead-reckon ALONG the jump vector, which
    // multiplies the delta by ahead/seg and flings the body thousands of units
    // past its real position (the roaming/fast-travel warp fixed 2026-07-20).
    {
        EntityInterp it;
        it.push(entAt(0.0f), 1000);
        it.push(entAt(1000.0f), 1050); // last segment = 1000u jump >> 50u snap
        EntityState out;
        // nowMs=1350 -> renderTime = 1350 - delay(<=200) >= 1150, past newest
        // (1050) but < staleMs, so we hit the extrapolation branch.
        bool ok = it.sample(1350, cfg, &out);
        CHECK("dead-reckon past teleport returns", ok);
        // Without the guard this overshoots to ~3000u; the guard holds newest.
        CHECK("dead-reckon past teleport holds newest (no overshoot)",
              ok && out.x <= 1000.5f && out.x >= 999.5f);
    }

    // Single snapshot: returns that pose verbatim.
    {
        EntityInterp it;
        it.push(entAt(7.0f), 1000);
        EntityState out;
        bool ok = it.sample(1040, cfg, &out);
        CHECK("single snapshot returns pose", ok && out.x == 7.0f);
    }

    // Identity/locomotion passthrough: sample carries the latest full state.
    {
        EntityInterp it;
        EntityState e = entAt(3.0f);
        e.bodyState = BODY_DOWN; e.cMoving = 1; e.task = 42;
        it.push(e, 1000);
        EntityState out;
        bool ok = it.sample(1030, cfg, &out);
        CHECK("identity+state passthrough", ok && out.bodyState == BODY_DOWN && out.cMoving == 1 && out.task == 42 && out.hIndex == 1);
    }
}

// ---- 5b. Interp staleness: what sample() is and is NOT current for --------------
// The bug class every one of these locks (ce43a4a, then three more in 6e668de):
// a self-heal read the INTERPOLATED sample to decide whether a transition had
// happened, and undid a reliable event that had already applied. sample() copies
// the last RECEIVED snapshot wholesale and then overwrites only x/y/z/heading, so
// the pose it returns is from the past while `bodyState`/`task` are simply the
// newest thing the wire delivered - which on the mid band is up to a send interval
// (and, if the stream hiccups, up to the whole stale window) behind real time.
// Four separate places read that and reproduced it locally: re-lifting a body the
// peer had just put down, re-caging a freed prisoner, re-locking a released
// shackle, dumping a KO'd body out of a bed.
//
// So: prove the staleness is REAL (the two fields disagree about which snapshot
// they came from), prove it is BOUNDED (it survives exactly the stale window and
// no longer), and prove that latest() - the fix for the POSITION half - does not
// fix the STATE half at all, because it copies the very same last_.
static EntityState entAtState(float x, u16 bodyState, u16 task) {
    EntityState e = entAt(x);
    e.bodyState = bodyState;
    e.task      = task;
    return e;
}

static void testInterpStaleness() {
    std::printf("== interp staleness: sample() vs latest() (the self-heal bug class) ==\n");
    InterpConfig cfg;

    // A clean 20 Hz feed: 16 snapshots, +1 u per 50 ms, every one of them
    // reporting the body as CARRIED. A constant interval means the EMA holds
    // avg=50/jitter=0 exactly, so renderDelay is exactly minDelayMs and every
    // number below is exact rather than approximate.
    EntityInterp it;
    for (int i = 0; i <= 15; ++i)
        it.push(entAtState((float)i, (u16)BODY_CARRIED, TASK_CARRY_BODY),
                1000 + (unsigned long)i * 50);
    const unsigned long newestT = 1750;

    EntityState s, l;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    bool okS = it.sample(newestT, cfg, &s);
    bool okL = it.latest(&l, &vx, &vy, &vz);
    CHECK("sample() and latest() both answer", okS && okL);
    CHECK_EQ("clean 20 Hz feed renders exactly minDelayMs in the past",
             it.lastDelayMs(), 50);
    CHECK_EQ("newestMs() reports the newest ring time", it.newestMs(), newestT);

    // POSITION: sample() is one render delay behind; latest() is the newest pose
    // the wire actually delivered. This is the half the furniture fixture search
    // got wrong - it hunted for a cage around where the body USED to be.
    CHECK("sample() position comes from the past",     okS && s.x == 14.0f);
    CHECK("latest() position is the newest received",  okL && l.x == 15.0f);
    CHECK("the gap between them is the render delay's worth of travel",
          okS && okL && (l.x - s.x) == 1.0f);
    CHECK("latest() reports the source's own velocity (20 u/s)",
          okL && vx == 20.0f && vy == 0.0f && vz == 0.0f);

    // STATE: both are the same wholesale copy of the last received snapshot.
    // Switching a STATE read from sample() to latest() therefore fixes nothing -
    // only the transform differs. The fix for a state read is the debounce below.
    CHECK("sample() carries the newest RECEIVED bodyState",
          okS && s.bodyState == (u16)BODY_CARRIED && s.task == TASK_CARRY_BODY);
    CHECK("latest() carries the SAME bodyState as sample()",
          okL && l.bodyState == s.bodyState);
    CHECK("latest() carries the SAME task as sample()", okL && l.task == s.task);
    CHECK("the source reads as moving", it.sourceMoving());

    // THE SHIPPED BUG, in the buffer. A reliable EVT_DROP_BODY lands and applies
    // here at once, but the newest snapshot was captured BEFORE the drop. Until
    // the next one arrives - one whole send interval - every sample() still
    // reports the carry, and nothing in the buffer ages it out. Meanwhile the
    // POSITION keeps moving (dead reckoning), so a reader watching the transform
    // has every reason to believe it is looking at fresh data.
    const unsigned long frozenNewest = it.newestMs();
    bool heldCarry = true, posMoved = false;
    for (unsigned long t = newestT; t <= newestT + 500; t += 50) {
        EntityState o;
        if (!it.sample(t, cfg, &o)) { heldCarry = false; break; }
        if (o.bodyState != (u16)BODY_CARRIED || o.task != TASK_CARRY_BODY) heldCarry = false;
        if (o.x > 15.0f) posMoved = true;
    }
    CHECK("a stale carry survives a whole send interval of samples", heldCarry);
    CHECK("...while the sampled position keeps advancing", posMoved);
    // And this is the one number that does NOT move while the peer is quiet, which
    // is why healDue() keys the debounce on it rather than on wall clock: sampling
    // a hundred times tells you nothing about whether the stream said anything.
    CHECK("newestMs() does not advance while the stream is quiet",
          it.newestMs() == frozenNewest);

    // A RECEIVED snapshot is the only thing that clears it.
    it.push(entAtState(15.0f, 0, TASK_NONE), newestT + 500);
    {
        EntityState o;
        bool okA = it.sample(newestT + 500, cfg, &o);
        CHECK("only a received snapshot clears the stale state",
              okA && o.bodyState == 0 && o.task == TASK_NONE);
        CHECK_EQ("newestMs() advances only on a received snapshot",
                 it.newestMs(), newestT + 500);
    }

    // latest() has NO staleness guard: it answers after sample() has given the
    // body up. That is deliberate (the starve-hold path in applyTargets reads it),
    // but it means a read moved from sample() to latest() also loses the
    // stream-dropped release - worth knowing before making that move again.
    {
        EntityInterp st;
        st.push(entAtState(3.0f, (u16)BODY_IN_CAGE, TASK_NONE), 1000);
        EntityState o, n;
        CHECK("sample() gives up once the stream goes stale",
              !st.sample(1000 + cfg.staleMs + 1, cfg, &o));
        CHECK("latest() still answers past the stale window (no release guard)",
              st.latest(&n, 0, 0, 0) && n.bodyState == (u16)BODY_IN_CAGE);
        CHECK_EQ("newestMs() answers past the stale window too", st.newestMs(), 1000);
    }

    // A source at rest hides the whole problem: sample() and latest() agree
    // exactly while nothing moves, which is why every one of these bugs only ever
    // showed on a body that had walked away from where it used to be.
    {
        EntityInterp rest;
        for (int i = 0; i <= 15; ++i) rest.push(entAt(42.0f), 1000 + (unsigned long)i * 50);
        EntityState rs, rl;
        bool okR = rest.sample(1750, cfg, &rs) && rest.latest(&rl, 0, 0, 0);
        CHECK("at rest sample() and latest() agree exactly",
              okR && rs.x == rl.x && rs.x == 42.0f);
        CHECK("at rest the source reads as not moving", !rest.sourceMoving());
    }

    // Nothing to read yet: both refuse, rather than handing back a zeroed pose.
    {
        EntityInterp e0;
        EntityState o;
        CHECK("empty buffer: sample() refuses", !e0.sample(1000, cfg, &o));
        CHECK("empty buffer: latest() refuses", !e0.latest(&o, 0, 0, 0));
        CHECK("empty buffer reports empty",     e0.empty() && e0.samples() == 0);
        CHECK_EQ("empty buffer: newestMs() is 0", e0.newestMs(), 0);
    }
}

// ---- 5c. Render delay band + the cadence-scaled ceiling -------------------------
// renderDelay's ceiling has to scale with the cadence THIS entity is sent at. A
// flat 200 ms is sized for the 20 Hz near band; a mid-band body sent every 500 ms
// rendered against that ceiling has renderTime past its newest snapshot for
// 300 ms of every 500 ms segment, so the dead-reckoning branch runs 60% of the
// time by arithmetic - no packet loss required. Then maxExtrapMs freezes the body
// partway and the next real sample lands half a segment on, which is the drive's
// hard snap. The remote session on 2026-08-04 recorded exactly that duty cycle
// (extrap 1018228 vs lerp 844769 = 55%). The sweep below measures it on both
// ceilings, so the fix cannot be quietly reverted.
static void testInterpDelayBand() {
    std::printf("== interp render delay band + cadence-scaled ceiling ==\n");
    InterpConfig cfg;

    // The band itself, at every cadence a real session produces: the 20 Hz near
    // tier and the round-robin mid tier at 100/250/500 ms.
    {
        const unsigned long cad[4] = { 50, 100, 250, 500 };
        bool inBand = true, withinRing = true, nearUnderFlat = true, sparseAboveFlat = false;
        for (int c = 0; c < 4; ++c) {
            EntityInterp it;
            for (int i = 0; i <= 15; ++i)
                it.push(entAt((float)i), 1000 + (unsigned long)i * cad[c]);
            const unsigned long newest = 1000 + 15 * cad[c];
            EntityState o;
            if (!it.sample(newest + cad[c] / 2, cfg, &o)) inBand = false;
            const unsigned long d = it.lastDelayMs();
            if (d < cfg.minDelayMs || d > cfg.maxCadenceDelayMs) inBand = false;
            // Never ask for more history than the ring actually holds.
            if (d > ((15 * cad[c]) * 9) / 10 + 1) withinRing = false;
            if (cad[c] <= 100 && d > cfg.maxDelayMs) nearUnderFlat = false;
            if (cad[c] >= 250 && d > cfg.maxDelayMs) sparseAboveFlat = true;
        }
        CHECK("render delay stays inside [minDelayMs, maxCadenceDelayMs] at every cadence",
              inBand);
        CHECK("render delay never exceeds the ring span it has to index", withinRing);
        CHECK("a near-band cadence stays under the flat maxDelayMs", nearUnderFlat);
        CHECK("the ceiling RISES above the flat maxDelayMs for a sparse stream",
              sparseAboveFlat);
    }

    // The duty cycle, measured. One 500 ms mid-band segment swept at 10 ms, under
    // the pre-fix flat ceiling (cadenceDelayK = 0 leaves cap = maxDelayMs = 200,
    // so renderTime runs past the newest snapshot for the last 300 ms of the
    // segment) and under the shipped cadence-scaled one.
    {
        EntityInterp it;
        for (int i = 0; i <= 15; ++i) it.push(entAt((float)i), 1000 + (unsigned long)i * 500);
        const unsigned long newest = 8500;

        InterpConfig flat = cfg;
        flat.cadenceDelayK = 0.0f; // the ceiling this stream used to get

        int n = 0, flatExtrap = 0, flatLerp = 0, scaledExtrap = 0, scaledLerp = 0;
        for (unsigned long t = newest; t < newest + 500; t += 10) {
            EntityState o;
            ++n;
            if (it.sample(t, flat, &o)) {
                if (it.lastMode() == EntityInterp::SM_EXTRAP)     ++flatExtrap;
                else if (it.lastMode() == EntityInterp::SM_LERP)  ++flatLerp;
            }
            if (it.sample(t, cfg, &o)) {
                if (it.lastMode() == EntityInterp::SM_EXTRAP)     ++scaledExtrap;
                else if (it.lastMode() == EntityInterp::SM_LERP)  ++scaledLerp;
            }
        }
        CHECK_EQ("segment sweep sample count", n, 50);
        CHECK_EQ("flat ceiling dead-reckons 60% of a 500 ms segment", flatExtrap, 30);
        CHECK_EQ("flat ceiling interpolates only the remaining 40%",  flatLerp,   20);
        CHECK_EQ("cadence-scaled ceiling never dead-reckons on that stream",
                 scaledExtrap, 0);
        CHECK_EQ("cadence-scaled ceiling interpolates the whole segment",
                 scaledLerp, 50);
        CHECK("the scaled delay covers at least one send interval",
              it.lastDelayMs() >= 500);

        // maxCadenceDelayMs is the hard bound on that scaling: a pathological
        // stream can never end up rendering minutes in the past.
        InterpConfig tight = cfg;
        tight.maxCadenceDelayMs = 300;
        EntityState o;
        it.sample(newest + 250, tight, &o);
        CHECK_EQ("maxCadenceDelayMs hard-bounds the cadence-scaled ceiling",
                 it.lastDelayMs(), 300);
    }

    // The ring-span clamp: with only three snapshots the ceiling is cut to 90% of
    // the span whatever the config says, or renderTime predates the oldest entry
    // and every sample clamp-holds instead of interpolating. Here a late arrival
    // (800 ms of queueing lag) would otherwise ask for ~1150 ms of history from a
    // ring that holds 1000.
    {
        EntityInterp it;
        it.push(entAt(0.0f), 1000, 1000);
        it.push(entAt(1.0f), 1500, 1500);
        it.push(entAt(2.0f), 2000, 2800); // arrived 800 ms after its send stamp
        InterpConfig wide = cfg;
        wide.maxDelayMs = 5000;           // a ceiling far above what the ring backs
        EntityState o;
        const unsigned long span = 2000 - 1000;
        bool ok = it.sample(2100, wide, &o);
        CHECK("span-clamped sample answers", ok);
        CHECK("render delay clamped to 90% of the ring span",
              it.lastDelayMs() <= (span * 9) / 10);
        CHECK("...and not clamped away to nothing",
              it.lastDelayMs() >= (span * 8) / 10);
    }
}

// ---- 5d. Buffer boundaries: clamp-old, extrapolation cap, stale window ----------
// The exact edges of every branch sample() can take. minDelayMs is applied LAST,
// after the ceiling, so setting it pins the render delay exactly - which is what
// makes these boundary cases deterministic rather than approximate.
static void testInterpBoundaries() {
    std::printf("== interp boundaries: clamp-old, extrap cap, stale window ==\n");
    InterpConfig cfg;

    EntityInterp it;
    for (int i = 0; i <= 15; ++i) it.push(entAt((float)i), 1000 + (unsigned long)i * 50);
    const unsigned long newestT = 1750, oldestT = 1000; // x = 15 and x = 0
    EntityState o;

    // LERP / EXTRAP edge: renderTime exactly AT the newest snapshot.
    bool ok1 = it.sample(newestT + 49, cfg, &o); // renderTime 1749: inside the ring
    CHECK("one ms before the newest snapshot: interpolates",
          ok1 && it.lastMode() == EntityInterp::SM_LERP);
    CHECK("...and has not yet reached the newest pose",
          ok1 && o.x > 14.9f && o.x < 15.0f);
    bool ok2 = it.sample(newestT + 50, cfg, &o); // renderTime 1750: at the newest
    CHECK("exactly at the newest snapshot: dead-reckons with zero lead",
          ok2 && it.lastMode() == EntityInterp::SM_EXTRAP);
    CHECK("...which is the newest pose itself", ok2 && o.x == 15.0f);

    // The dead-reckoning cap. Past maxExtrapMs the lead stops growing, so a
    // starved buffer freezes a bounded distance out instead of running away.
    bool ok3 = it.sample(newestT + 50 + 200, cfg, &o);              // 200 ms ahead
    CHECK("under the cap the lead grows with the gap", ok3 && o.x == 19.0f);
    bool ok4 = it.sample(newestT + 50 + cfg.maxExtrapMs, cfg, &o);  // exactly at it
    float atCap = ok4 ? o.x : -1.0f;
    CHECK("at the cap the lead is maxExtrapMs of source travel", ok4 && atCap == 20.0f);
    bool ok5 = it.sample(newestT + 50 + 1500, cfg, &o);             // far past it
    CHECK("past the cap the lead stops growing", ok5 && o.x == atCap);
    CHECK("a starved buffer reports EXTRAP", it.lastMode() == EntityInterp::SM_EXTRAP);

    // CLAMP_OLD: a render time predating the whole ring holds the oldest pose
    // rather than extrapolating backwards off the front of the buffer.
    InterpConfig deep = cfg;
    deep.minDelayMs = newestT - oldestT;      // 750 ms: exactly the ring span
    bool ok6 = it.sample(newestT, deep, &o);
    CHECK("renderTime at the oldest entry clamps",
          ok6 && it.lastMode() == EntityInterp::SM_CLAMP_OLD);
    CHECK("...to the oldest pose", ok6 && o.x == 0.0f);
    deep.minDelayMs = newestT - oldestT - 1;  // 749 ms: one ms inside the ring
    bool ok7 = it.sample(newestT, deep, &o);
    CHECK("one ms inside the ring interpolates instead",
          ok7 && it.lastMode() == EntityInterp::SM_LERP);
    CHECK("...just past the oldest pose", ok7 && o.x > 0.0f && o.x < 0.1f);

    // The stale window bounds how long a sample can keep answering - and
    // therefore how long the bodyState it carries can be wrong. One snapshot
    // gets the flat staleMs.
    {
        EntityInterp one;
        one.push(entAt(0.0f), 1000);
        EntityState s;
        CHECK("single snapshot answers at exactly staleMs",
              one.sample(1000 + cfg.staleMs, cfg, &s));
        CHECK("single snapshot gives up one ms later",
              !one.sample(1000 + cfg.staleMs + 1, cfg, &s));
    }
    // A sparse stream scales it to four of its OWN segments, so a mid-band body
    // is not released on every rotation hiccup.
    {
        EntityInterp mid;
        mid.push(entAt(0.0f), 1000);
        mid.push(entAt(1.0f), 2500);          // one 1500 ms mid-band segment
        EntityState s;
        CHECK_EQ("newest segment reports the stream's own cadence",
                 mid.lastSegMs(), 1500);
        CHECK("a sparse stream answers out to four of its own segments",
              mid.sample(2500 + 6000, cfg, &s));
        CHECK("...and gives up past them", !mid.sample(2500 + 6001, cfg, &s));
    }
    // ...but the scaled window is hard-capped at 6 s, so a genuinely abandoned
    // body still releases promptly.
    {
        EntityInterp slow;
        slow.push(entAt(0.0f), 1000);
        slow.push(entAt(1.0f), 4000);         // a 3 s segment: 4x would be 12 s
        EntityState s;
        CHECK("the cadence-scaled stale window is hard-capped at 6 s",
              !slow.sample(4000 + 6001, cfg, &s));
        CHECK("...and honours that cap's full extent",
              slow.sample(4000 + 6000, cfg, &s));
    }
}

// ---- 5e. Self-heal debounce contract (a MIRROR, not coverage) -------------------
// READ THIS BEFORE TRUSTING IT: HealDebounce below is a hand-written MIRROR of the
// state machine in ReplicatorDrive.cpp (carrySeeTick / furnSeeTick / chainSeeTick).
// It is NOT the production code and it exercises none of it - the real copy lives
// in a translation unit that needs the whole engine facade, which this CRT-only
// binary deliberately cannot link. What this test locks is the CONTRACT those
// three call sites are supposed to implement, written down so a future edit that
// reintroduces a fire-on-sight heal has a standing statement of why that is wrong.
// If you change the real state machine, change this one in the same commit.
//
// The contract, in two parts. A self-heal exists only to repair a LOST reliable
// event, so waiting is free - and firing early is not, because the stream it reads
// is delayed (see testInterpStaleness). So (1) it must not fire until the stream
// has kept asserting the condition for a whole debounce window, and (2) the window
// must be measured against the STREAM, not the wall clock: sample() re-serves the
// same snapshot for seconds after a peer goes quiet, so a purely time-based window
// expires against the very sample it was meant to wait out. That is what
// EntityInterp::newestMs() is for, and what healDue() in ReplicatorDrive.cpp
// checks. Any tick where the stream stops asserting, or the local copy already
// agrees, re-arms both halves.
struct HealDebounce {
    unsigned long seeTick;    // first tick the stream asserted a state we lack
    unsigned long seeSample;  // interp.newestMs() when that streak armed
    unsigned long healTick;   // last tick this heal actually fired
    HealDebounce() : seeTick(0), seeSample(0), healTick(0) {}

    // One drive tick. streamAsserts: the (delayed) sample still reports the state.
    // localRead: our own read of the local body succeeded - absence is not
    // evidence. localAgrees: the local body already matches, nothing to heal.
    // newestSampleMs: EntityInterp::newestMs(). requireNewerSample selects part (2)
    // of the contract; passing false models the older time-only window, kept only
    // so the tests below can show what it let through.
    bool tick(bool streamAsserts, bool localRead, bool localAgrees,
              unsigned long now, unsigned long newestSampleMs,
              unsigned long debounceMs, bool requireNewerSample,
              unsigned long healGapMs) {
        if (!streamAsserts) { seeTick = 0; seeSample = 0; return false; }
        if (localAgrees)    { seeTick = 0; seeSample = 0; return false; }
        if (!localRead)     return false;
        if (seeTick == 0) { seeTick = now; seeSample = newestSampleMs; }
        if ((now - seeTick) < debounceMs) return false;
        if (requireNewerSample && newestSampleMs <= seeSample) return false;
        if ((now - healTick) < healGapMs) return false;
        healTick = now;
        return true;
    }
};

static void testHealDebounce() {
    std::printf("== self-heal debounce contract (MIRRORS ReplicatorDrive, does not run it) ==\n");
    SyncTuning tun;
    const unsigned long DEB = tun.carryHealDebounceMs; // 1500 (furn's is the same)
    const unsigned long GAP = 1500;                    // mirror of CARRY_HEAL_MS
    const bool NEWER = true, TIME_ONLY = false;

    // A genuinely LOST reliable event, with the peer still talking: snapshots keep
    // arriving every 500 ms and every one of them still reports the carry. The
    // heal must still happen - just not until the window has elapsed AND a
    // snapshot newer than the one that armed it has said so.
    {
        HealDebounce h;
        int fires = 0; unsigned long firstFire = 0;
        for (unsigned long t = 10000; t <= 10000 + DEB; t += 100) {
            const unsigned long newest = 10000 + ((t - 10000) / 500) * 500;
            if (h.tick(true, true, false, t, newest, DEB, NEWER, GAP)) {
                ++fires; if (!firstFire) firstFire = t;
            }
        }
        CHECK_EQ("a lost event still heals", fires, 1);
        CHECK_EQ("...but only once the debounce window has elapsed",
                 firstFire, 10000 + DEB);
    }

    // THE SHIPPED BUG. A reliable EVT_DROP_BODY arrives and applies here at
    // t=10000. The unreliable stream is one send interval behind, so its newest
    // snapshot still reports the carry - ~500 ms of ticks on the mid band - and
    // then a fresh one contradicts it. Debounced: nothing happens and the peer's
    // drop stands. Fire-on-sight: the very first tick re-lifts the body, which is
    // what the other player sees as US picking it back up.
    {
        HealDebounce armed, naive;
        int armedFires = 0, naiveFires = 0;
        bool naiveFiredFirstTick = false;
        for (unsigned long t = 10000; t < 10500; t += 50) {
            if (armed.tick(true, true, false, t, 10000, DEB, NEWER, GAP)) ++armedFires;
            if (naive.tick(true, true, false, t, 10000, 0, TIME_ONLY, GAP)) {
                ++naiveFires;
                if (t == 10000) naiveFiredFirstTick = true;
            }
        }
        // The next snapshot lands; the stream stops asserting the carry.
        armed.tick(false, true, false, 10500, 10500, DEB, NEWER, GAP);
        naive.tick(false, true, false, 10500, 10500, 0, TIME_ONLY, GAP);
        CHECK_EQ("a debounced heal does not undo a fresh reliable event", armedFires, 0);
        CHECK("a fire-on-sight heal WOULD have undone it (the regression)",
              naiveFires > 0);
        // CARRY_HEAL_MS / FURN_HEAL_MS are a gap between ATTEMPTS, measured from
        // the last fire - they cannot help when the first attempt is already
        // wrong, which is why the debounce had to be added alongside them.
        CHECK("the heal gap alone is not a debounce (it lets the first shot through)",
              naiveFiredFirstTick);
    }

    // Part (2), and the hole a time-only window still leaves. If the peer goes
    // quiet - a mid-band rotation stall, an interest hiccup - sample() keeps
    // re-serving the same pre-drop snapshot for seconds. Wall clock then expires
    // the window against the very sample it was meant to wait out, and the heal
    // fires anyway. Requiring the ring to have ADVANCED ties the wait to the peer
    // still talking, which is the thing the debounce was always trying to say.
    {
        HealDebounce timeOnly, shipped;
        int timeOnlyFires = 0, shippedFires = 0;
        for (unsigned long t = 10000; t <= 20000; t += 100) {
            if (timeOnly.tick(true, true, false, t, 10000, DEB, TIME_ONLY, GAP)) ++timeOnlyFires;
            if (shipped.tick(true, true, false, t, 10000, DEB, NEWER, GAP)) ++shippedFires;
        }
        CHECK("a time-only window expires against a FROZEN stream", timeOnlyFires > 0);
        CHECK_EQ("requiring a newer snapshot closes that hole", shippedFires, 0);
        CHECK("...and a newer snapshot that still asserts releases the heal",
              shipped.tick(true, true, false, 20100, 20100, DEB, NEWER, GAP));
    }

    // The window RE-ARMS. A stream that stops asserting clears the streak, so the
    // next divergence waits the full window again - a counter that only ever
    // counted up would fire instantly after any blip.
    {
        HealDebounce h;
        for (unsigned long t = 10000; t < 10000 + DEB; t += 100)
            h.tick(true, true, false, t, t, DEB, NEWER, GAP);
        h.tick(false, true, false, 10000 + DEB, 10000 + DEB, DEB, NEWER, GAP);
        CHECK("a stream that stops asserting clears the streak",
              h.seeTick == 0 && h.seeSample == 0);
        CHECK("...so the next divergence starts a fresh window",
              !h.tick(true, true, false, 10000 + DEB + 100, 10000 + DEB + 100,
                      DEB, NEWER, GAP));
        CHECK("...and fires only after the FULL window from that restart",
              h.tick(true, true, false, 10000 + DEB + 100 + DEB,
                     10000 + DEB + 100 + DEB, DEB, NEWER, GAP));
    }

    // The local copy agreeing clears it too (the carryingRight / localKind ==
    // streamKind reset in the real code).
    {
        HealDebounce h;
        h.tick(true, true, false, 10000, 10000, DEB, NEWER, GAP);
        CHECK("a divergent tick arms the streak",
              h.seeTick == 10000 && h.seeSample == 10000);
        h.tick(true, true, true, 10100, 10100, DEB, NEWER, GAP);
        CHECK("a tick where the local copy already agrees clears it",
              h.seeTick == 0 && h.seeSample == 0);
    }

    // Absence is not evidence: a FAILED local read must neither arm the streak nor
    // fire the heal, however long the stream keeps asserting.
    {
        HealDebounce h;
        int fires = 0;
        for (unsigned long t = 10000; t <= 20000; t += 100)
            if (h.tick(true, false, false, t, t, DEB, NEWER, GAP)) ++fires;
        CHECK_EQ("an unreadable local body never heals", fires, 0);
        CHECK("...and never arms the streak", h.seeTick == 0);
    }
}

// ---- 5f. Sync tuning: the mid band, and everything sized against it -------------
// SyncTuning.h is pure POD with no engine dependency, so the unit layer can pin
// the band the 2026-08-07 session had to widen and the relationships that widening
// has to keep. midBandMax/midSliceMax were 48/16, sized when the mid band was a
// bandwidth experiment: a shared-cell fight enumerated 220 NPCs with 116 authored
// locally, so 68 authored bodies got no motion at all and the join drove them from
// its own AI until a census beat snapped them (median 128 u, p90 1322 u).
//
// The band's width sets a body's SEND INTERVAL, and the send interval is what both
// of the other fixes are sized against: every self-heal debounce has to outlast it
// (or the heal reads a snapshot the peer already superseded), and the interp's
// delay ceiling has to cover it (or dead reckoning becomes structural). Widening
// the band without touching either re-opens both bugs, so the arithmetic lives here.
static void testSyncTuning() {
    std::printf("== sync tuning: mid band sizing + the windows sized against it ==\n");
    SyncTuning tun;
    InterpConfig cfg;

    CHECK("mid band covers a shared-cell fight (measured 220 NPCs enumerated)",
          tun.midBandMax >= 220);
    CHECK("the per-tick slice cap is not the binding constraint",
          tun.midBandMax <= 10 * tun.midSliceMax);

    // publishOwned: quota = ceil(|band|/10) capped at midSliceMax, one slice
    // advanced per 50 ms net tick, so the whole band cycles in ceil(|band|/quota)
    // slices. That cycle time IS a mid-band body's send interval.
    unsigned int quota = (tun.midBandMax + 9) / 10;
    if (quota > tun.midSliceMax) quota = tun.midSliceMax;
    CHECK("a slice is actually published", quota > 0);
    const unsigned long midIntervalMs =
        (unsigned long)((tun.midBandMax + quota - 1) / quota) * 50;
    CHECK_EQ("mid-band per-body send interval (ms)", midIntervalMs, 500);

    // Send interval -> the self-heal debounces. A heal that fires inside one send
    // interval is acting on a snapshot the peer has already superseded.
    CHECK("the carry heal debounce outlasts a mid-band send interval",
          tun.carryHealDebounceMs >= midIntervalMs);
    CHECK("the furniture/chain heal debounce outlasts it too",
          tun.furnHealDebounceMs >= midIntervalMs);
    CHECK("...both with margin for a slice the wire dropped",
          tun.carryHealDebounceMs >= 2 * midIntervalMs &&
          tun.furnHealDebounceMs  >= 2 * midIntervalMs);

    // The inventory resend cadence must never be zero. abe12a2 moved the
    // 5000/30000/24 literals out of ReplicatorItems.cpp into these fields
    // without ctor initializers; zero-initialized, resendMs collapsed to 0 and
    // every sent container re-queued its full snapshot EVERY TICK on the
    // reliable channel - silently, because the periodic leg logs nothing per
    // send. One live session (2026-08-09) starved the motion stream this way
    // and presented as pose/position desync. An uninitialized cadence field
    // must never pass this gate again.
    CHECK_EQ("inv resend cadence, small snapshots (ms)", tun.invResendMs, 5000);
    CHECK_EQ("inv resend cadence, big snapshots (ms)", tun.invResendBigMs, 30000);
    CHECK_EQ("inv big-snapshot threshold (entries)", tun.invResendBigN, 24);
    CHECK("a periodic inv resend is never sub-second",
          tun.invResendMs >= 1000 && tun.invResendBigMs >= 1000);

    // Send interval -> the interp render delay. The flat ceiling alone does NOT
    // cover the mid band; that is precisely why the cadence-scaled one exists, and
    // the hard bound on it must still clear one send interval.
    CHECK("the flat delay ceiling alone would not cover the mid band",
          (unsigned long)cfg.maxDelayMs < midIntervalMs);
    CHECK("the cadence-scaled ceiling covers at least one send interval",
          cfg.cadenceDelayK >= 1.0f);
    CHECK("the hard bound on that ceiling still clears one send interval",
          (unsigned long)cfg.maxCadenceDelayMs >= midIntervalMs);

    // Send interval -> the stale window. A mid-band body must never be released
    // between its own slices, or it flaps out of the driven set every rotation.
    CHECK("a mid-band body is not released between its own slices",
          midIntervalMs < (unsigned long)cfg.staleMs);

    // Bandwidth was never the constraint, but a future widening should have to
    // re-justify the budget rather than inherit it silently.
    CHECK_EQ("a full mid band at 2 Hz costs one EntityState per body, twice a second",
             (unsigned long)tun.midBandMax * 2 * (unsigned long)sizeof(EntityState), 40448);
    CHECK("...which is inside a sane per-second budget",
          (unsigned long)tun.midBandMax * 2 * (unsigned long)sizeof(EntityState) <= 64000);
}

// ---- 5g. Suppressed-body collision capability (the invisible wall) --------------
// Bug class (2): engine::suppressNpc was removeUpdate + clearGoals +
// setVisible(false). setVisible is the Ogre RENDER virtual and nothing more, so
// the Havok capsule stayed exactly where it was - and because the body was off the
// update list it no longer ran its own collision response either, meaning it could
// not be shoved aside the way a live NPC can. An immovable invisible pillar,
// re-asserted every 2 s, and stairwells and doorways are the narrowest corridors in
// the game. Only the client that suppressed the body was blocked, which is the
// "I can't get up the stairs but the host can" report.
//
// Parking the hull needs the engine, but the registry that gates it does not. What
// IS testable here: suppression's collision half is a NAMED, separately resolvable
// capability rather than an assumed side effect of hiding the mesh - and appending
// it did not break the name table. That last one is the exact failure mode of
// adding a capability: kNames ends up one entry short, capName returns a null
// pointer, and the "[engine] CAP-MISS op=... cap=%s" formatter takes it.
//
// Every token comparison below goes through this guard rather than handing a
// possible null straight to strcmp - the miss we are testing for would otherwise
// take the whole test binary down instead of naming itself.
static bool capTokIs(int c, const char* want) {
    const char* n = coop::engine::capName((coop::engine::Capability)c);
    return n != 0 && std::strcmp(n, want) == 0;
}

static void testSuppressionCaps() {
    std::printf("== suppressed-body collision capability (EngineCaps) ==\n");
    using namespace coop::engine;

    // Whole-table integrity first, so the NEXT appended capability is caught here
    // rather than inside the diagnostic that was supposed to explain it.
    bool allNamed = true, allUnique = true;
    for (int i = 0; i < (int)CAP_COUNT; ++i) {
        const char* ni = capName((Capability)i);
        if (ni == 0 || ni[0] == '\0' || std::strcmp(ni, "unknown") == 0) allNamed = false;
        for (int j = i + 1; j < (int)CAP_COUNT; ++j) {
            const char* nj = capName((Capability)j);
            if (ni != 0 && nj != 0 && std::strcmp(ni, nj) == 0) allUnique = false;
        }
    }
    CHECK("every capability has a token", allNamed);
    CHECK("every capability token is unique", allUnique);

    CHECK("CAP_HULL names the collision-hull park", capTokIs(CAP_HULL, "hull"));
    CHECK("CAP_HULL was appended, not inserted", (int)CAP_HULL > (int)CAP_DEED);
    CHECK("appending it did not shift the existing tokens",
          (int)CAP_SAVELOAD == 0 && (int)CAP_HAND_RESOLVE == 2 &&
          capTokIs(CAP_SAVELOAD, "saveload"));

    // Fail-closed, per capability: an image without the hull entry point reports
    // CAP_HULL off while everything suppression already had stays on. That is the
    // difference between "I hid the body" and "I hid the body AND moved what it
    // collides with" - the plugin can now tell those apart instead of assuming.
    void* pHand   = (void*)1;
    void* pStream = (void*)1;
    void* pHull   = (void*)1;
    const CapRow rows[] = {
        { &pHand,   "hand::getCharacter",                  CAP_HAND_RESOLVE, true },
        { &pStream, "getCharactersWithinSphere",           CAP_NPC_STREAM,   true },
        { &pHull,   "CharMovement::teleportCollisionHull", CAP_HULL,         true }
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    bool avail[CAP_COUNT];

    capEvaluate(rows, n, avail);
    CHECK("hull capability up when its entry point resolved", avail[CAP_HULL]);
    pHull = 0;
    capEvaluate(rows, n, avail);
    CHECK("hull capability off when it did not", !avail[CAP_HULL]);
    CHECK("...without disabling the rest of suppression",
          avail[CAP_HAND_RESOLVE] && avail[CAP_NPC_STREAM]);
    CHECK("...and the runtime image is still usable", capCoreOk(avail));
}

// ---- 5h. Drive decision arithmetic: the combat drift bands (a MIRROR) -----------
// READ THIS BEFORE TRUSTING IT, same caveat as testHealDebounce and for the same
// reason. The constants live in src/plugin/sync/ReplicatorUtil.h, which includes
// Replicator.h -> NetLink.h (ENet) and EngineSync.h (the engine facade), so this
// CRT-only binary cannot see them at all. DriveTuning and driveDecide below are a
// hand-written MIRROR of those constants and of the band block in
// ReplicatorDrive.cpp (applyTargets, the combat `if (haveActual)` arm). They
// exercise NONE of the production code. If you retune the real constants, retune
// these in the same commit.
//
// What this locks is the RELATIONSHIPS, because those are what a tuning pass
// silently breaks and the constants themselves are meant to move. The ladder has
// been reverted once already: the pre-2026-07-16 gate teleported the instant a copy
// passed COMBAT_SNAP_DIST, which was the visible warp during dense fights, and the
// convergence-first correction replaced it with a fast slide plus a much narrower
// definition of a genuine "leave". This decides whether a driven body warps in
// front of the other player, and nothing else in the tree tests it.
struct DriveTuning {
    float         catchupK;      // CATCHUP_K
    float         snapDist;      // SNAP_DIST          (locomotion distance floor)
    float         snapSeconds;   // SNAP_SECONDS       (locomotion time allowance)
    float         combatWait;    // COMBAT_WAIT_DIST
    float         combatSoft;    // COMBAT_SOFT_DIST
    float         combatSnap;    // COMBAT_SNAP_DIST   (the churn ceiling)
    float         combatBigSnap; // COMBAT_BIG_SNAP_DIST (the true-leave distance)
    float         combatSlideMax;// COMBAT_SLIDE_MAX
    float         combatSnapVel; // COMBAT_SNAP_VEL
    unsigned long combatConvergeMs; // COMBAT_CONVERGE_MS
    DriveTuning()
        : catchupK(2.0f), snapDist(8.0f), snapSeconds(0.75f),
          combatWait(3.0f), combatSoft(6.0f), combatSnap(20.0f),
          combatBigSnap(60.0f), combatSlideMax(60.0f), combatSnapVel(8.0f),
          combatConvergeMs(400) {}
};

// Ordered on purpose: the response may only get MORE aggressive as the drift
// grows, and the monotonicity check below compares these as integers.
enum DriveAction { DA_HOLD = 0, DA_SLIDE = 1, DA_WARP = 2 };

// MIRROR of the combat band decision. overBandMs is how long the drift has SAT
// above the churn ceiling (the real code's `now - d.combatOverTick`, armed the
// first frame drift exceeds combatSnapDist_ and cleared whenever it does not);
// snapCooled is COMBAT_SNAP_COOL_MS having elapsed since this body's last warp.
static DriveAction driveDecide(const DriveTuning& t, float drift, float srcVel,
                               bool localFighting, bool wrongLocalTgt,
                               bool hostWaiting, bool srcTeleport,
                               unsigned long overBandMs, bool snapCooled) {
    const bool correctFight = localFighting && !wrongLocalTgt;
    const float softBand  = hostWaiting ? t.combatWait : t.combatSoft;
    // hostWaiting first: the owner calling the body slot-QUEUED outranks this
    // client's copy having independently engaged. See the note in ReplicatorDrive.
    const float leaveBand = hostWaiting  ? t.combatWait
                          : correctFight ? t.combatSnap
                                         : softBand;
    const bool sustained  = (drift > t.combatSnap) &&
                            overBandMs >= t.combatConvergeMs;
    const bool trueLeave  = !hostWaiting &&
                            (drift > t.combatBigSnap || srcTeleport ||
                             (sustained && srcVel >= t.combatSnapVel));
    if (trueLeave && snapCooled) return DA_WARP;
    if (drift > leaveBand)       return DA_SLIDE;
    return DA_HOLD;
}

// MIRROR of the fast catch-up slide speed the converge branch commands.
static float driveSlideSpeed(const DriveTuning& t, float srcSpeed, float drift) {
    const float base = (srcSpeed > 1.0f) ? srcSpeed : 12.0f;
    float spd = base + drift;
    float cap = base * 2.5f;
    if (cap < t.combatSlideMax) cap = t.combatSlideMax;
    if (spd > cap) spd = cap;
    return spd;
}

static void testDriveBands() {
    std::printf("== drive combat bands (MIRRORS ReplicatorUtil/ReplicatorDrive) ==\n");
    DriveTuning t;
    const bool FIGHTING = true,  IDLE      = false;
    const bool WRONGTGT = true,  RIGHTTGT  = false;
    const bool WAITING  = true,  ENGAGED   = false;
    const bool WARPED   = true,  NOWARP    = false;
    const bool COOLED   = true,  COOLING   = false;

    // (1) The ordering the whole ladder rests on. Each band must be strictly wider
    // than the last or a drift falls into two of them at once and which branch runs
    // depends on the order they happen to be tested in.
    CHECK("drift bands strictly ordered: wait < soft < churn ceiling < true leave",
          t.combatWait < t.combatSoft &&
          t.combatSoft < t.combatSnap &&
          t.combatSnap < t.combatBigSnap);

    // (2) Inside the soft band nothing happens at all - not a walk, not a warp. A
    // fight legitimately moves the body; correcting inside this band is what made
    // the waiting crowd twitch.
    {
        bool quiet = true;
        for (float d = 0.0f; d <= t.combatSoft; d += 0.25f)
            if (driveDecide(t, d, 50.0f, IDLE, RIGHTTGT, ENGAGED, NOWARP,
                            10000, COOLED) != DA_HOLD) quiet = false;
        CHECK("a body inside the soft band is left alone", quiet);
    }
    // ...with exactly one exception, pinned so it is a decision and not a surprise:
    // a source that TELEPORTED is a leave at any drift, because the copy is being
    // placed rather than corrected.
    CHECK("a source teleport is a leave even inside the soft band",
          driveDecide(t, 0.5f, 0.0f, IDLE, RIGHTTGT, ENGAGED, WARPED, 0, COOLED)
              == DA_WARP);

    // (3) Past the true-leave distance a warp is unconditional, whatever the copy
    // is doing and however slowly the source is moving.
    {
        bool warps = true;
        for (float d = t.combatBigSnap + 0.5f; d <= 200.0f; d += 0.5f) {
            if (driveDecide(t, d, 0.0f, FIGHTING, RIGHTTGT, ENGAGED, NOWARP,
                            0, COOLED) != DA_WARP) warps = false;
            if (driveDecide(t, d, 0.0f, IDLE, WRONGTGT, ENGAGED, NOWARP,
                            0, COOLED) != DA_WARP) warps = false;
        }
        CHECK("a body past the true-leave distance always warps", warps);
    }

    // (4) The middle band is the whole point of the 2026-07-16 pass: a correctly
    // engaged fight owns its own footwork up to the churn ceiling (measured: a
    // driven brawl legitimately churns 12-18 u), and every other copy - arming,
    // idle, queued, or swinging at the wrong body - converges above the soft band.
    CHECK("a correct fight owns its footwork up to the churn ceiling",
          driveDecide(t, 18.0f, 0.0f, FIGHTING, RIGHTTGT, ENGAGED, NOWARP,
                      0, COOLED) == DA_HOLD);
    CHECK("an arming/idle copy at the same drift converges instead",
          driveDecide(t, 18.0f, 0.0f, IDLE, RIGHTTGT, ENGAGED, NOWARP,
                      0, COOLED) == DA_SLIDE);
    CHECK("a copy fighting the WRONG body converges too",
          driveDecide(t, 18.0f, 0.0f, FIGHTING, WRONGTGT, ENGAGED, NOWARP,
                      0, COOLED) == DA_SLIDE);

    // (5) Over the ceiling the copy SLIDES; an instant warp needs the drift to have
    // sat there for the converge window AND the source to be genuinely moving. A
    // big drift on a near-stationary source is melee churn or a wrong-place body -
    // converge, never warp.
    CHECK("over the churn ceiling a fight slides rather than warping",
          driveDecide(t, 30.0f, 50.0f, FIGHTING, RIGHTTGT, ENGAGED, NOWARP,
                      t.combatConvergeMs - 1, COOLED) == DA_SLIDE);
    CHECK("...and warps only once the drift has SAT there for the converge window",
          driveDecide(t, 30.0f, 50.0f, FIGHTING, RIGHTTGT, ENGAGED, NOWARP,
                      t.combatConvergeMs, COOLED) == DA_WARP);
    CHECK("a sustained drift on a near-stationary source is churn, not a chase",
          driveDecide(t, 30.0f, t.combatSnapVel - 0.1f, FIGHTING, RIGHTTGT,
                      ENGAGED, NOWARP, 10000, COOLED) == DA_SLIDE);
    CHECK("...and on a source genuinely moving it is a leave",
          driveDecide(t, 30.0f, t.combatSnapVel, FIGHTING, RIGHTTGT,
                      ENGAGED, NOWARP, 10000, COOLED) == DA_WARP);

    // (6) A WAITING (slot-queued) stance has no chase to justify a warp, at any
    // drift, however fast the source or however long the drift has persisted.
    {
        bool everWarps = false;
        for (float d = 0.0f; d <= 200.0f; d += 1.0f)
            if (driveDecide(t, d, 50.0f, IDLE, RIGHTTGT, WAITING, WARPED,
                            10000, COOLED) == DA_WARP) everWarps = true;
        CHECK("a WAITING stance never warps, at any drift", !everWarps);
    }
    CHECK("a waiting copy converges at the tighter wait band",
          driveDecide(t, t.combatWait + 0.5f, 0.0f, IDLE, RIGHTTGT, WAITING,
                      NOWARP, 0, COOLED) == DA_SLIDE &&
          driveDecide(t, t.combatWait - 0.5f, 0.0f, IDLE, RIGHTTGT, WAITING,
                      NOWARP, 0, COOLED) == DA_HOLD);
    // ...INCLUDING when this copy has independently engaged. That is the case the
    // band exists for: the owner says the body is queued and standing still, our
    // copy picked its own fight, and the divergence is ours - so the owner's word
    // wins and the copy converges rather than claiming a fighting copy's 20 u of
    // footwork. This check was originally written the other way round, pinning the
    // code as it read; the order in leaveBand was the bug, and both moved together.
    CHECK("the wait band applies even to a copy that is itself fighting",
          driveDecide(t, t.combatWait + 0.5f, 0.0f, FIGHTING, RIGHTTGT, WAITING,
                      NOWARP, 0, COOLED) == DA_SLIDE);
    CHECK("a fighting copy under a waiting owner still holds inside the wait band",
          driveDecide(t, t.combatWait - 0.5f, 0.0f, FIGHTING, RIGHTTGT, WAITING,
                      NOWARP, 0, COOLED) == DA_HOLD);
    // The tight band must not leak into the ordinary engaged case - a real fight
    // owns its footwork to the churn ceiling, which is the whole reason
    // combatSnapDist_ is 20 u and not 3.
    CHECK("an engaged owner still lets a correct fight own its footwork",
          driveDecide(t, t.combatWait + 0.5f, 0.0f, FIGHTING, RIGHTTGT, ENGAGED,
                      NOWARP, 0, COOLED) == DA_HOLD &&
          driveDecide(t, t.combatSnap - 0.5f, 0.0f, FIGHTING, RIGHTTGT, ENGAGED,
                      NOWARP, 0, COOLED) == DA_HOLD &&
          driveDecide(t, t.combatSnap + 0.5f, 0.0f, FIGHTING, RIGHTTGT, ENGAGED,
                      NOWARP, 0, COOLED) == DA_SLIDE);

    // (7) The per-body snap cooldown. A warp that cannot stick - stale interp, a
    // staggered body - must not re-fire every frame; while it is cooling the copy
    // converges instead, which is the strictly gentler answer.
    CHECK("a body still inside the snap cooldown converges instead of re-warping",
          driveDecide(t, 100.0f, 50.0f, FIGHTING, RIGHTTGT, ENGAGED, NOWARP,
                      10000, COOLING) == DA_SLIDE);

    // (8) Monotonicity across every stance. The response may only escalate as the
    // drift grows: hold -> converge -> warp, never back. A band ordering slip shows
    // up here as a body that converges at 10 u and is left alone at 25.
    {
        bool monotone = true;
        for (int f = 0; f < 2; ++f)
            for (int w = 0; w < 2; ++w) {
                int prev = -1;
                for (float d = 0.0f; d <= 120.0f; d += 0.5f) {
                    int a = (int)driveDecide(t, d, 50.0f, f != 0, RIGHTTGT,
                                             w != 0, NOWARP, 10000, COOLED);
                    if (a < prev) monotone = false;
                    prev = a;
                }
            }
        CHECK("the response never softens as the drift grows", monotone);
    }

    // (9) The converge SPEED. A correction that does not exceed the source's own
    // pace never closes: the copy trails at a fixed gap, stays over the band and
    // eventually teleports anyway, which is the driver this cap was added for.
    {
        bool closes = true, capped = true;
        for (float src = 0.0f; src <= 60.0f; src += 0.5f)
            for (float d = t.combatSoft; d <= t.combatBigSnap; d += 0.5f) {
                const float spd = driveSlideSpeed(t, src, d);
                if (spd <= src) closes = false;
                const float base = (src > 1.0f) ? src : 12.0f;
                float cap = base * 2.5f;
                if (cap < t.combatSlideMax) cap = t.combatSlideMax;
                if (spd > cap + 0.001f) capped = false;
            }
        CHECK("a converge always commands more than the source's own pace", closes);
        CHECK("...and never more than the slide cap", capped);
    }
    // And the cap has to be able to CLOSE the widest drift that is not allowed to
    // teleport, or a legitimately drifted body crawls back in plain sight for
    // seconds. At the cap that is roughly a second of travel.
    CHECK("the slide cap closes the widest non-teleporting drift in about a second",
          t.combatSlideMax >= t.combatBigSnap);
}

// ---- 5g-bis. The peer speed multiplier, sanitized -------------------------------
//
// MIRROR of sanePeerSpeed (ReplicatorChannels.cpp) and of slewedEffective's
// clamps (Replicator.h) - both take a GameWorld-bearing Replicator, so they are
// restated. Retune together.
//
// SpeedPacket.speed is untrusted f32 off the wire and the only wire field that
// reaches the engine's own clock rate. Worth pinning because the two values that
// got through are the two nobody writes a test for: a NaN (every comparison
// against it is false, so BOTH of slewedEffective's clamps decline to fire and it
// is returned unchanged) and a negative (it takes the `eff <= 0.01f` paused
// early-out and is returned unclamped).
static float mirrorSanePeerSpeed(float v) {
    if (!(v >= 0.0f)) return 0.0f;
    if (v > 5.0f) return 5.0f;
    return v;
}

// MIRROR of Replicator::slewedEffective, as it reads TODAY - including the two
// holes above, so this file records that the sanitizer is what closes them and
// not a second clamp downstream.
static float mirrorSlewedEffective(float eff, float slew) {
    if (eff <= 0.01f) return eff;
    float s = eff * slew;
    if (s > 5.0f)  s = 5.0f;
    if (s < 0.05f) s = 0.05f;
    return s;
}

static void testPeerSpeedSanity() {
    std::printf("== peer speed multiplier sanity ==\n");

    // Ordinary values pass through untouched - a sanitizer that changes the
    // normal case is a bug of its own.
    CHECK("pause (0) survives",          mirrorSanePeerSpeed(0.0f) == 0.0f);
    CHECK("1x survives",                 mirrorSanePeerSpeed(1.0f) == 1.0f);
    CHECK("5x survives (the ceiling)",   mirrorSanePeerSpeed(5.0f) == 5.0f);

    CHECK("above the ceiling clamps",    mirrorSanePeerSpeed(1000.0f) == 5.0f);
    CHECK("negative becomes pause",      mirrorSanePeerSpeed(-1.0f) == 0.0f);
    CHECK("a large negative becomes pause",
          mirrorSanePeerSpeed(-1.0e30f) == 0.0f);

    // NaN. Built by division rather than a literal: C++03 has no std::nanf, and
    // 0.0f/0.0f is the portable way to get one out of a v100 compiler.
    // volatile so the compiler cannot constant-fold the division (v100 warns
    // C4723 on a literal 0/0, and folding it would produce the NaN at compile
    // time rather than exercising the runtime path the receive site takes).
    volatile float zero = 0.0f;
    float nan = zero / zero;
    CHECK("the NaN is actually a NaN (self-comparison fails)", !(nan == nan));
    CHECK("NaN becomes pause", mirrorSanePeerSpeed(nan) == 0.0f);

    // The two holes this closes, stated as the tests that FAIL without it. If
    // slewedEffective is ever given its own guards, these become redundant rather
    // than wrong - but they must not be deleted before that happens.
    float leakedNan = mirrorSlewedEffective(nan, 1.0f);
    CHECK("without sanitizing, a NaN passes slewedEffective unchanged",
          !(leakedNan == leakedNan));
    CHECK("without sanitizing, a negative passes slewedEffective unclamped",
          mirrorSlewedEffective(-1000.0f, 1.0f) == -1000.0f);

    // ...and the same two values, sanitized first, are safe by the time they
    // reach it. This is the actual contract of the receive path.
    CHECK("sanitized NaN reaches the engine as pause",
          mirrorSlewedEffective(mirrorSanePeerSpeed(nan), 1.0f) == 0.0f);
    CHECK("sanitized negative reaches the engine as pause",
          mirrorSlewedEffective(mirrorSanePeerSpeed(-1000.0f), 1.0f) == 0.0f);

    // Nothing that survives sanitizing can drive the clock outside the band
    // slewedEffective promises. Swept, including the unpaused floor.
    bool inBand = true;
    for (int i = -50; i <= 200; ++i) {
        float v   = (float)i * 0.1f;
        float out = mirrorSlewedEffective(mirrorSanePeerSpeed(v), 1.0f);
        if (out == 0.0f) continue;                 // pause is legitimate
        if (!(out >= 0.05f && out <= 5.0f)) inBand = false;
    }
    CHECK("every sanitized input lands in [0.05, 5] or is a pause", inBand);
}

// ---- 5h-bis. targets_ age-out: the three-way stale/latched/gone predicate -------
//
// MIRROR of Replicator::ageOutStaleTargets (ReplicatorDrive.cpp) - it takes a
// GameWorld-bearing Replicator, so the predicate is restated here rather than
// linked. Retune both in the same commit.
//
// Worth pinning because it is three interacting booleans guarding a map erase,
// and the two failure directions are opposite and both bad: too eager drops a
// death latch and a corpse stands back up; too lazy is the leak it was written to
// fix - a targets_ entry, and a per-tick iteration of it, for every NPC ever
// knocked out near a player.
static const unsigned long AGE_TARGET_STALE_MS = 30000;
static const unsigned long AGE_LATCH_STALE_MS  = 600000;

static bool ageOutErases(unsigned long now, unsigned long lastSeenMs, bool latched) {
    bool stale = (lastSeenMs == 0) || (now - lastSeenMs > AGE_TARGET_STALE_MS);
    bool gone  = (lastSeenMs != 0) && (now - lastSeenMs > AGE_LATCH_STALE_MS);
    return stale && (!latched || gone);
}

static void testTargetAgeOut() {
    std::printf("== targets_ age-out predicate ==\n");

    const unsigned long NOW = 5000000;   // well past both horizons

    CHECK("the latched horizon is longer than the ordinary one",
          AGE_LATCH_STALE_MS > AGE_TARGET_STALE_MS);

    // Unlatched: the ordinary 30 s rule, unchanged by any of this.
    CHECK("unlatched + fresh is kept",
          !ageOutErases(NOW, NOW - 1000, false));
    CHECK("unlatched + exactly at the stale horizon is kept",
          !ageOutErases(NOW, NOW - AGE_TARGET_STALE_MS, false));
    CHECK("unlatched + one ms past the stale horizon is erased",
          ageOutErases(NOW, NOW - AGE_TARGET_STALE_MS - 1, false));

    // Latched: survives the ordinary horizon. This is the part that must not
    // regress - a corpse stays a corpse while its owner keeps streaming it.
    CHECK("latched + fresh is kept",
          !ageOutErases(NOW, NOW - 1000, true));
    CHECK("latched + past the ordinary horizon is still kept",
          !ageOutErases(NOW, NOW - AGE_TARGET_STALE_MS - 1, true));
    CHECK("latched + one ms short of the latch horizon is kept",
          !ageOutErases(NOW, NOW - AGE_LATCH_STALE_MS, true));
    CHECK("latched + one ms past the latch horizon is erased",
          ageOutErases(NOW, NOW - AGE_LATCH_STALE_MS - 1, true));

    // lastSeenMs == 0 means "no ingest has ever been dated for this hand". It
    // cannot be aged as latched, because there is no timestamp to measure from -
    // which is exactly why applyEvents and the dead-on-arrival mint both stamp it.
    // Without those stamps this row is the leak, and it is invisible: the entry
    // reads as maximally stale and is still never dropped.
    CHECK("never-dated + unlatched is erased",
          ageOutErases(NOW, 0, false));
    CHECK("never-dated + latched is kept (nothing to date it by)",
          !ageOutErases(NOW, 0, true));

    // Monotonicity in age: once a row erases, no OLDER row of the same kind is
    // kept. A predicate this shape can be written so a middle band survives, and
    // that would strand rows forever at one particular age.
    bool erasedLatched = false, erasedPlain = false;
    bool monoLatched = true, monoPlain = true;
    for (unsigned long age = 0; age <= 700000; age += 5000) {
        bool eL = ageOutErases(NOW, NOW - age, true);
        bool eP = ageOutErases(NOW, NOW - age, false);
        if (erasedLatched && !eL) monoLatched = false;
        if (erasedPlain   && !eP) monoPlain   = false;
        if (eL) erasedLatched = true;
        if (eP) erasedPlain   = true;
    }
    CHECK("latched rows only ever escalate toward erasure with age", monoLatched);
    CHECK("unlatched rows only ever escalate toward erasure with age", monoPlain);
    CHECK("both kinds do erase somewhere in the swept range",
          erasedLatched && erasedPlain);

    // A latched row is never erased before an unlatched row of the same age: the
    // latch may only ever DELAY the drop, never cause one.
    bool latchNeverEarlier = true;
    for (unsigned long age = 0; age <= 700000; age += 5000) {
        if (ageOutErases(NOW, NOW - age, true) && !ageOutErases(NOW, NOW - age, false))
            latchNeverEarlier = false;
    }
    CHECK("a latch only ever delays the drop, never causes one", latchNeverEarlier);
}

// ---- 5i. Drive convergence: snap distance vs cadence, and the catch-up gain -----
// The other half of the drive's pure arithmetic, and the half that is coupled to
// something this binary CAN see: the mid band's send interval, derived from
// SyncTuning exactly as testSyncTuning derives it, so a retune of the band moves
// this test with it. Still a MIRROR of ReplicatorUtil.h / ReplicatorDrive.cpp for
// the drive side - see testDriveBands.
static float driveSnapGate(const DriveTuning& t, float velPeak,
                           unsigned long segMs, float speedMult) {
    const float mult = (speedMult > 1.0f) ? speedMult : 1.0f;
    float gate = t.snapDist * mult;
    float gateSec = t.snapSeconds + (float)segMs / 1000.0f;
    if (gateSec > 2.5f) gateSec = 2.5f;
    if (velPeak * gateSec > gate) gate = velPeak * gateSec;
    return gate;
}

// MIRROR of the walk-drive catch-up speed: source pace plus a gap-proportional
// boost, capped at 2.5x the source's own pace (12 u/s standing in for a body whose
// streamed speed is unusable).
static float driveCatchupSpeed(float srcSpeed, float gap, float k) {
    float spd = srcSpeed + gap * k;
    const float base = (srcSpeed > 1.0f) ? srcSpeed : 12.0f;
    const float cap  = base * 2.5f;
    if (spd > cap) spd = cap;
    return spd;
}

// One correction interval: the copy runs at the commanded speed while the source
// runs at its own, so the gap closes at (commanded - source) for dt seconds.
static float driveCatchupStep(float srcSpeed, float gap, float k, float dt) {
    return gap - (driveCatchupSpeed(srcSpeed, gap, k) - srcSpeed) * dt;
}

static void testDriveConvergence() {
    std::printf("== drive convergence: snap distance vs cadence, catch-up gain ==\n");
    DriveTuning t;
    SyncTuning tun;

    unsigned int quota = (tun.midBandMax + 9) / 10;
    if (quota > tun.midSliceMax) quota = tun.midSliceMax;
    const unsigned long midIntervalMs =
        (unsigned long)((tun.midBandMax + quota - 1) / quota) * 50;
    const float midIntervalSec = (float)midIntervalMs / 1000.0f;
    CHECK_EQ("mid-band send interval this test is sized against (ms)",
             midIntervalMs, 500);

    // ASSUMPTION, stated rather than measured: an ordinary NPC on foot covers no
    // more than ~12 u/s. That number is not invented here - it is the drive's OWN
    // stand-in for a body whose streamed speed is unusable (`base = (out.cSpeed >
    // 1.0f) ? out.cSpeed : 12.0f`, in both the locomotion and the combat slide), so
    // it is the codebase's own idea of a walking pace. A sprinter is ~50 u/s and is
    // covered by the velocity-aware half of the gate below, not by this floor.
    // If that assumption is ever measured and comes back higher than
    // SNAP_DIST / 0.5 s = 16 u/s, this check is the thing that should fail.
    const float WALK_U_PER_S = 12.0f;
    CHECK("the snap floor covers an ordinary walk between two mid-band sends",
          t.snapDist >= WALK_U_PER_S * midIntervalSec);

    // The velocity-aware half. gateSec is the base allowance PLUS one stream
    // segment, and the gate is the larger of the distance floor and velPeak*gateSec.
    // A driven body legitimately trails its source by a whole segment of travel -
    // the lead point it is walking toward was computed a segment ago - so a gate
    // that did not cover one hard-snapped mid-band walkers chronically (4,259
    // teleports in 30 s, bodies at gap 20-40 u against a gate of 18-25).
    {
        const unsigned long cad[4] = { 50, 100, 250, 500 };
        bool coversSegment = true, neverBelowFloor = true;
        for (int c = 0; c < 4; ++c) {
            const float segSec = (float)cad[c] / 1000.0f;
            for (float vel = 0.0f; vel <= 60.0f; vel += 1.0f) {
                const float g = driveSnapGate(t, vel, cad[c], 1.0f);
                if (g < t.snapDist)      neverBelowFloor = false;
                if (g < vel * segSec)    coversSegment = false;
            }
        }
        CHECK("the snap gate never drops below its distance floor", neverBelowFloor);
        CHECK("...and always covers a full send segment of source travel",
              coversSegment);
    }
    // That guarantee survives only while the 2.5 s clamp does not bite: past it the
    // allowance stops growing with the cadence, so a band widened until its interval
    // exceeded (2.5 s - SNAP_SECONDS) would start teleporting bodies for merely
    // being one send behind. The mid band has to stay inside that.
    CHECK("the mid-band interval leaves the base allowance intact under the clamp",
          midIntervalSec + t.snapSeconds <= 2.5f);
    CHECK("...and the clamp really is the edge: a 3 s cadence loses the guarantee",
          driveSnapGate(t, 40.0f, 3000, 1.0f) < 40.0f * 3.0f);
    // The distance floor scales with the consensus game speed, because at 5x every
    // trailing distance is 5x in world units for the same time lag.
    CHECK("the distance floor scales with the consensus game speed",
          driveSnapGate(t, 0.0f, 50, 5.0f) == t.snapDist * 5.0f);

    // ---- the catch-up gain -----------------------------------------------------
    // (a) The 2.5x cap must never command LESS than the source's own pace. A chase
    // that merely matches the source never closes; the copy sits at a fixed trail,
    // stays over the snap gate, and gets teleported instead of converging.
    {
        bool alwaysGains = true;
        for (float src = 0.0f; src <= 80.0f; src += 0.5f)
            for (float gap = 0.0f; gap <= 100.0f; gap += 1.0f)
                if (driveCatchupSpeed(src, gap, t.catchupK) < src)
                    alwaysGains = false;
        CHECK("the 2.5x cap never commands less than the source's own pace",
              alwaysGains);
    }

    // (b) Convergence proper. Applying the gain repeatedly must shrink the gap on
    // every step, never cross to the far side of the target, and actually finish.
    // Modelled at the near band's cadence: a fresh sample, and so a fresh commanded
    // speed, every 50 ms.
    {
        float gap = t.snapDist;
        bool mono = true, signKept = true;
        int steps = 0;
        while (gap > 1.0f && steps < 200) {
            const float next = driveCatchupStep(12.0f, gap, t.catchupK, 0.05f);
            if (next >= gap) mono = false;
            if (next < 0.0f) signKept = false;
            gap = next; ++steps;
        }
        CHECK("the gap shrinks on every catch-up tick", mono);
        CHECK("...without ever crossing to the far side of the target", signKept);
        CHECK("...and closes a full snap distance inside 1.5 s of near-band ticks",
              gap <= 1.0f && steps <= 30);
    }

    // (c) The stability bound, which is the part a retune has to notice. The step is
    // gap * K * dt, so the correction is stable only while K * dt <= 1: at exactly 1
    // the body lands on the target, past it the correction is larger than the error
    // and the copy walks through the source and back. dt is the SLOWEST cadence at
    // which the speed is recomputed - the mid band's send interval, since the walk
    // is re-issued only when the lead point moves and the lead point moves only when
    // a sample lands. CATCHUP_K = 2 against a 500 ms mid band sits exactly on that
    // boundary: widening the band slows dt, and slowing dt is what turns the gain
    // unstable. There is no headroom left in this one.
    CHECK("the catch-up gain is stable at the slowest re-issue cadence",
          t.catchupK * midIntervalSec <= 1.0f);
    {
        bool noOvershoot = true;
        for (float gap = 0.5f; gap <= 60.0f; gap += 0.5f)
            if (driveCatchupStep(12.0f, gap, t.catchupK, midIntervalSec) < -0.001f)
                noOvershoot = false;
        CHECK("...so a mid-band correction never overshoots the target", noOvershoot);
    }
    // The counterexample, kept for the same reason testHealDebounce keeps its
    // fire-on-sight heal: at twice this gain the same arithmetic inverts the error
    // every tick and the body oscillates across the source instead of converging.
    // The speed cap bounds the amplitude but does not damp it.
    {
        const float bigK = t.catchupK * 2.0f;
        float g = 8.0f, amp = 0.0f;
        int flips = 0;
        for (int i = 0; i < 20; ++i) {
            const float next = driveCatchupStep(12.0f, g, bigK, midIntervalSec);
            if ((next < 0.0f) != (g < 0.0f)) ++flips;
            g = next;
            if (i >= 10) { const float a = (g < 0.0f) ? -g : g; if (a > amp) amp = a; }
        }
        CHECK("double the gain flips the error across the target every tick",
              flips == 20);
        CHECK("...and the oscillation never decays away", amp >= 0.9f);
    }
}

// ---- 6. Ownership rank resolution (OwnRanks.h) ----------------------------------
// Guards the squad-tab ownership partition, especially the F2-panel role switch
// regression (2026-07-14): a session launched as HOST resolves ranks to {0};
// switching to JOIN must re-resolve to {1}, or the client claims the host's
// rank-0 player squad and that unit never moves. An explicit env override is
// preserved across the switch.

static bool ranksAre(const std::set<unsigned int>& r, int a, int b) {
    if (b < 0) return r.size() == 1 && r.count((unsigned)a) == 1;
    return r.size() == 2 && r.count((unsigned)a) == 1 && r.count((unsigned)b) == 1;
}

static void testOwnRanks() {
    std::printf("== ownership rank resolution (OwnRanks.h) ==\n");

    // Role defaults from a clean slate.
    {
        std::set<unsigned int> r;
        resolveOwnRanks(r, true, false);
        CHECK("host default owns {0}", ranksAre(r, 0, -1));
        r.clear();
        resolveOwnRanks(r, false, false);
        CHECK("join default owns {1}", ranksAre(r, 1, -1));
    }

    // THE FIX: a session that started HOST (ranks {0}) switches to JOIN via the
    // panel and MUST end up owning {1}, not the host's {0}.
    {
        std::set<unsigned int> r;
        resolveOwnRanks(r, true, false);          // launched HOST -> {0}
        CHECK("pre-switch ranks are {0}", ranksAre(r, 0, -1));
        resolveOwnRanks(r, false, false);         // panel switch to JOIN
        CHECK("HOST->JOIN switch re-resolves to {1}", ranksAre(r, 1, -1));
        resolveOwnRanks(r, true, false);          // and back to HOST
        CHECK("JOIN->HOST switch re-resolves to {0}", ranksAre(r, 0, -1));
    }

    // An explicit env override is preserved across a role switch (the user asked
    // for a specific partition; the panel must not clobber it).
    {
        std::set<unsigned int> r;
        r.insert(2u); r.insert(3u);
        resolveOwnRanks(r, false, true);          // fromEnv -> untouched
        CHECK("env override preserved as JOIN", ranksAre(r, 2, 3));
        resolveOwnRanks(r, true, true);           // still untouched as HOST
        CHECK("env override preserved as HOST", ranksAre(r, 2, 3));
    }

    // CSV parse (KENSHICOOP_OWN_SQUAD/OWN_RANK surface).
    {
        std::set<unsigned int> r;
        CHECK("parse '' -> no ranks",        !parseRankList("", r) && r.empty());
        r.clear();
        CHECK("parse '0' -> {0}",            parseRankList("0", r) && ranksAre(r, 0, -1));
        r.clear();
        CHECK("parse '1,2' -> {1,2}",        parseRankList("1,2", r) && ranksAre(r, 1, 2));
        r.clear();
        CHECK("parse ' 3 ; 5 ' tolerant",    parseRankList(" 3 ; 5 ", r) && ranksAre(r, 3, 5));
        r.clear();
        CHECK("parse '2,2' dedups to {2}",   parseRankList("2,2", r) && ranksAre(r, 2, -1));
    }

    // Config-style resolution: env-provided ranks set fromEnv true and survive;
    // empty env falls back to the role default.
    {
        std::set<unsigned int> r;
        bool fromEnv = parseRankList("1", r);
        resolveOwnRanks(r, true, fromEnv);        // env said {1} even though HOST
        CHECK("env {1} wins over HOST default", fromEnv && ranksAre(r, 1, -1));
        r.clear();
        fromEnv = parseRankList("", r);
        resolveOwnRanks(r, false, fromEnv);       // no env -> JOIN default {1}
        CHECK("empty env -> JOIN default {1}", !fromEnv && ranksAre(r, 1, -1));
    }
}

// ---- 7. SteamID64 parse + mask (SteamId.h) --------------------------------------
// Guards the F2 panel "Paste friend's Steam ID" button: clipboard text is noisy
// (surrounding whitespace, a trailing newline, or a "Steam ID: 7656..." wrapper),
// so parseSteamId64 keeps only digits and requires a 17-digit community ID
// (76561... prefix). Arbitrary clipboard junk must be rejected.
//
// maskSteamId64 is the other half: the panel rows show only the last 4 digits so
// a streamed screen leaks no account. A leaked prefix would defeat the point, so
// the exact output shape is pinned here.

static void testSteamIdParse() {
    std::printf("== SteamID64 parse (SteamId.h) ==\n");
    unsigned long long id = 0;

    id = 0;
    CHECK("clean 17-digit id accepted",
          coop::parseSteamId64("76561198000000000", id) && id == 76561198000000000ull);
    id = 0;
    CHECK("surrounding whitespace/newline stripped",
          coop::parseSteamId64("  76561198012345678 \r\n", id) && id == 76561198012345678ull);
    id = 0;
    CHECK("wrapper text 'Steam ID: <n>' stripped",
          coop::parseSteamId64("Steam ID: 76561198012345678", id) && id == 76561198012345678ull);

    // Rejections leave the caller's value untouched.
    id = 123ull;
    CHECK("empty string rejected",        !coop::parseSteamId64("", id) && id == 123ull);
    CHECK("non-numeric rejected",         !coop::parseSteamId64("not-an-id", id) && id == 123ull);
    CHECK("too short (16 digits) rejected",
          !coop::parseSteamId64("7656119800000000", id) && id == 123ull);
    CHECK("too long (18 digits) rejected",
          !coop::parseSteamId64("765611980000000000", id) && id == 123ull);
    CHECK("17 digits, wrong prefix rejected",
          !coop::parseSteamId64("12345678901234567", id) && id == 123ull);

    // Masked display: "****" + the last 4 digits, nothing else.
    CHECK("full id masked to last 4",
          coop::maskSteamId64(76561198012345678ull) == "****5678");
    CHECK("trailing zeros kept as digits",
          coop::maskSteamId64(76561198000000000ull) == "****0000");
    // Not real ids, but steamPeer from the config is never length-checked.
    CHECK("short value masked, not padded", coop::maskSteamId64(42ull) == "****42");
    CHECK("zero masked", coop::maskSteamId64(0ull) == "****0");
}

// ---- 8. Pose-fixture acceptance (WorkPose.h) ------------------------------------
// Guards the mining-sync fix (2026-07-14): a player mining an ore node operates a
// mine building. A single 6 m seat gate rejected the CORRECT mine as "far"
// (applyTaskOrder -> park, no mining animation on the peer). Field distances varied
// wildly (one mine ~8.9 m from origin, a larger one 57 m host / 104 m join), so no
// fixed radius covers both. Work fixtures are unique buildings with reliable
// cross-client hands, so they are TRUSTED (ungated); only seats are distance-gated
// (they mis-resolve to a wrong nearby prop).

static void testWorkPoseMatch() {
    std::printf("== pose-fixture acceptance (WorkPose.h) ==\n");

    // Gate applies to seats, never to work fixtures.
    CHECK("seat radius 6 m",            SEAT_MATCH_DIST == 6.0f);
    CHECK("seat is distance-gated",     poseIsDistanceGated(false));
    CHECK("work is NOT distance-gated", !poseIsDistanceGated(true));

    // THE FIX: work fixtures are accepted at ANY resolved distance (the mine origin
    // can sit 8.9 m, 57 m or 104 m from the operate spot), while a seat at those
    // distances is rejected as a mis-resolved wrong prop.
    CHECK("mining 8.9 m accepted as work",  poseFixtureAccepted(true,  8.9f));
    CHECK("mining 57 m accepted as work",   poseFixtureAccepted(true,  57.0f));
    CHECK("mining 104 m accepted as work",  poseFixtureAccepted(true,  104.0f));
    CHECK("mining 8.9 m rejected as seat", !poseFixtureAccepted(false, 8.9f));

    // Medic sync (2026-07-15): a first-aid subject is the PATIENT (a character), also
    // identity-trusted (isWorkFixtureTask || isMedicTask -> the boolean below), so a
    // patient whose driven copy is mid-motion (metres from the streamed transform) is
    // still accepted, exactly like a work fixture; a seat at the same range is not.
    CHECK("medic 12 m accepted (identity-trusted)",  poseFixtureAccepted(true,  12.0f));
    CHECK("medic 12 m rejected as seat",            !poseFixtureAccepted(false, 12.0f));

    // Seat still tight: a fixture right under the body is accepted, a far stool not.
    CHECK("seat 3 m accepted",   poseFixtureAccepted(false, 3.0f));
    CHECK("seat 6 m boundary",   poseFixtureAccepted(false, 6.0f));
    CHECK("seat 6.1 m rejected", !poseFixtureAccepted(false, 6.1f));

    // Squared-distance form (the engine gate) agrees with the metres form.
    CHECK("sq: work 104 m accepted",   poseFixtureAcceptedSq(true,  104.0f * 104.0f));
    CHECK("sq: seat 3 m accepted",     poseFixtureAcceptedSq(false, 3.0f * 3.0f));
    CHECK("sq: seat 6 m boundary",     poseFixtureAcceptedSq(false, 6.0f * 6.0f));
    CHECK("sq: seat 6.1 m rejected",  !poseFixtureAcceptedSq(false, 6.1f * 6.1f));
}

// ---- 9. Debounced task-clear (WorkPose.h poseClearElapsed) ----------------------
// Guards the job-removal fix (2026-07-14): removing a job on the host while the
// character stays STATIONARY streams task=NONE continuously (the movement re-arm
// never fires), so the join must release the held mine/operate pose after a
// sustained-NONE window instead of holding it forever. Transient NONE blips (1-2
// capture frames) must NOT clear a committed pose, so the release is DEBOUNCED.
// clearMs mirrors TASK_CLEAR_MS in ReplicatorUtil.h (game-coupled, so not included
// here); keep the literal in sync with that constant.
static void testTaskClear() {
    std::printf("== debounced task-clear (WorkPose.h) ==\n");
    const unsigned long clearMs = 1200; // mirror of TASK_CLEAR_MS

    // No streak in progress (noneTick == 0) never clears, regardless of 'now'.
    CHECK("no streak never clears",       !poseClearElapsed(0,     999999, clearMs));

    // A transient blip below the window holds (anti-oscillation guarantee).
    CHECK("blip 0 ms holds",              !poseClearElapsed(10000, 10000,  clearMs));
    CHECK("blip 1199 ms holds",           !poseClearElapsed(10000, 11199,  clearMs));

    // Sustained NONE at/after the window releases (genuine stationary un-assign).
    CHECK("streak 1200 ms clears",         poseClearElapsed(10000, 11200,  clearMs));
    CHECK("streak 5 s clears",             poseClearElapsed(10000, 15000,  clearMs));

    // Unsigned tick wrap (GetTickCount rollover): now - noneTick still yields the
    // elapsed delta, so a streak spanning the wrap boundary still clears on time.
    // 'now' values are written pre-wrapped (as GetTickCount would report post-rollover)
    // so the arithmetic under test is the real subtraction, not a constant overflow.
    const unsigned long nearMax   = 0xFFFFFFFFul - 100; // streak started 100 ms before wrap
    const unsigned long stillPre  = nearMax + 50;       // 50 ms later, before wrap (no overflow)
    const unsigned long postWrap  = 1199UL;             // (nearMax + 1300) mod 2^32: 1300 ms later
    CHECK("wrap: 50 ms elapsed holds",    !poseClearElapsed(nearMax, stillPre, clearMs));
    CHECK("wrap: 1300 ms elapsed clears",  poseClearElapsed(nearMax, postWrap, clearMs));
}

// ---- 10. Death/KO latch carry across re-key (DeathLatch.h rekeyCarryLatch) ------
// Guards the death-consistency fix (2026-07-15): a dead/KO'd body that RE-KEYS
// (owner re-containers it - squad move / recruit) must keep its down/death pin,
// or the peer stands the corpse back up under the new hand ("dead on one game,
// alive on the other"). rekeyPeerBody snapshots the OLD key's latch and OR-merges
// it onto the new key; this locks that merge (monotone: never loses a pin, never
// clears a latch already present on the new key).
static void testDeathRekey() {
    std::printf("== death/KO latch carry on re-key (DeathLatch.h) ==\n");

    // Dead old key, fresh new key -> death carries.
    LatchState r1 = rekeyCarryLatch(LatchState(true, true, true), LatchState());
    CHECK("dead old -> new death latched",  r1.death);
    CHECK("dead old -> new ko latched",      r1.ko);
    CHECK("dead old -> new down carried",    r1.down);

    // KO-only old key -> ko carries, death stays clear.
    LatchState r2 = rekeyCarryLatch(LatchState(false, true, true), LatchState());
    CHECK("ko old -> new ko latched",        r2.ko);
    CHECK("ko old -> new death still clear", !r2.death);

    // Alive old key, alive new key -> nothing invented.
    LatchState r3 = rekeyCarryLatch(LatchState(), LatchState());
    CHECK("alive+alive -> no death",         !r3.death);
    CHECK("alive+alive -> no ko",            !r3.ko);

    // New key already has a fresh EVT_DEATH (beat the re-key edge): OR-merge must
    // PRESERVE it even though the old key was alive.
    LatchState r4 = rekeyCarryLatch(LatchState(), LatchState(true, true, false));
    CHECK("alive old + dead new -> death kept", r4.death);
    CHECK("alive old + dead new -> ko kept",    r4.ko);

    // Monotone: merging can only ADD pins, never remove one present on either key.
    LatchState r5 = rekeyCarryLatch(LatchState(true, false, false),
                                    LatchState(false, true, false));
    CHECK("merge keeps old death", r5.death);
    CHECK("merge keeps new ko",    r5.ko);
}

// ---- 11. Inbound queue lifecycle (Inbound.h) ------------------------------------
// Locks the Phase 0 correctness fixes against regression:
//  (a) flushWorldState() drops a queued cross-owner invXfer intent - the bug was
//      that a transfer enqueued before a world reload SURVIVED it (invXfer_ was
//      missing from the clear list), applying against the fresh world.
//  (b) sawRemote_ (peer-readiness) clears on the session-reset edge, so a new
//      scenario cannot arm on a departed peer's stale readiness.
//  (c) the internal session generation advances on every flush (the Phase 0 seed
//      for the Phase 4 wire epoch).
// It also proves the world-state vs session-preserving split: a world-state queue
// is dropped while a coordinated-load queue survives the same flush.
static void testInboundLifecycle() {
    std::printf("== inbound queue lifecycle (Inbound.h) ==\n");
    Inbound in;

    CHECK_EQ("initial session generation", in.sessionGeneration(), 0);
    CHECK("sawRemote false before any entity", !in.sawRemoteEntity());

    EntityState e; std::memset(&e, 0, sizeof(e));
    in.pushEntity(1, 1000, e);
    CHECK("sawRemote true after owned-entity batch", in.sawRemoteEntity());

    // (a) THE FIX: a cross-owner transfer intent must not survive a reload.
    InvXferPacket xf; std::memset(&xf, 0, sizeof(xf));
    xf.type = (u8)PKT_INV_XFER; xf.ownerId = 1;
    in.pushInvXfer(1, xf);
    {
        std::deque<InboundInvXfer> peek;
        in.drainInvXfers(peek);
        CHECK("invXfer enqueues normally", peek.size() == 1);
    }
    in.pushInvXfer(1, xf);          // re-enqueue, then hit the reload edge
    in.flushWorldState();
    {
        std::deque<InboundInvXfer> after;
        in.drainInvXfers(after);
        CHECK("invXfer dropped by flushWorldState (was the bug)", after.empty());
    }

    // (b) + (c): the same flush cleared readiness and advanced the generation.
    CHECK("sawRemote cleared by flushWorldState", !in.sawRemoteEntity());
    CHECK_EQ("generation advanced by flush", in.sessionGeneration(), 1);

    // world-state vs session-preserving: a world event drops, a LOAD_GO survives.
    EventPacket ev; std::memset(&ev, 0, sizeof(ev)); ev.type = (u8)PKT_EVENT; ev.ownerId = 1;
    in.pushEvent(1, ev);
    LoadGoPacket lg; std::memset(&lg, 0, sizeof(lg)); lg.type = (u8)PKT_LOAD_GO; lg.ownerId = 0;
    in.pushLoadGo(0, lg);
    in.flushWorldState();
    {
        std::deque<InboundEvent> evOut; in.drainEvents(evOut);
        std::deque<InboundLoadGo> lgOut; in.drainLoadGos(lgOut);
        CHECK("world-state event dropped by flush", evOut.empty());
        CHECK("coordinated-load GO survives flush (session-preserving)", lgOut.size() == 1);
    }
    CHECK_EQ("generation advanced again", in.sessionGeneration(), 2);
}

// ---- 11b. flushWorldState() full-coverage contract (Inbound.h) ------------------
// The invXfer_ bug (a world-state queue silently missing from the clear list) is a
// CLASS of bug: every queue Inbound owns must be classified as either WORLD-STATE
// (dropped on the reload/reconnect/disconnect edge) or SESSION-PRESERVING (kept
// because the connection outlives the world swap). This test pushes a sentinel
// into EVERY queue, hits one flush, and asserts the split for all of them, so a
// queue added later without being classified in flushWorldState() fails here.
//
// WHEN YOU ADD A NEW INBOUND QUEUE: add its push here and assert it in the correct
// group. A queue absent from both groups is unverified - that is the bug.
static void testFlushWorldStateContract() {
    std::printf("== flushWorldState full-coverage contract (Inbound.h) ==\n");
    Inbound in;

    // Zeroed payloads - the flush contract is about queue membership, not content.
    EntityState     e;   std::memset(&e,   0, sizeof(e));
    EventPacket     ev;  std::memset(&ev,  0, sizeof(ev));
    u32             cKey[5]; std::memset(cKey, 0, sizeof(cKey));
    WorldDropPacket wdp; std::memset(&wdp, 0, sizeof(wdp));
    WorldPickupPacket wpp; std::memset(&wpp, 0, sizeof(wpp));
    InvXferPacket   xf;  std::memset(&xf,  0, sizeof(xf));
    MedicalPacket   mp;  std::memset(&mp,  0, sizeof(mp));
    TreatmentPacket tp;  std::memset(&tp,  0, sizeof(tp));
    CombatHitPacket chp; std::memset(&chp, 0, sizeof(chp));
    SpeedPacket     sp;  std::memset(&sp,  0, sizeof(sp));
    StatsPacket     stp; std::memset(&stp, 0, sizeof(stp));
    MoneyPacket     mo;  std::memset(&mo,  0, sizeof(mo));
    MoneyDeltaPacket md; std::memset(&md,  0, sizeof(md));
    FactionPacket   fa;  std::memset(&fa,  0, sizeof(fa));
    TimePacket      ti;  std::memset(&ti,  0, sizeof(ti));
    DoorPacket      dp;  std::memset(&dp,  0, sizeof(dp));
    ProdPacket      pr;  std::memset(&pr,  0, sizeof(pr));
    ResearchPacket  rp;  std::memset(&rp,  0, sizeof(rp));
    DeedPacket      de;  std::memset(&de,  0, sizeof(de));
    BuildPlacePacket  bp; std::memset(&bp,  0, sizeof(bp));
    BuildStatePacket  bs; std::memset(&bs,  0, sizeof(bs));
    BuildDoorPacket   bd; std::memset(&bd,  0, sizeof(bd));
    BuildRemovePacket br; std::memset(&br,  0, sizeof(br));
    StealthPacket   sl;  std::memset(&sl,  0, sizeof(sl));
    SpawnReqPacket  sq;  std::memset(&sq,  0, sizeof(sq));
    SpawnInfoPacket si;  std::memset(&si,  0, sizeof(si));
    CamHintPacket   ch;  std::memset(&ch,  0, sizeof(ch));
    CellClaimPacket cc;  std::memset(&cc,  0, sizeof(cc));
    InvXferAckPacket xa; std::memset(&xa,  0, sizeof(xa));
    // Session-preserving payloads.
    SaveReqPacket   srq; std::memset(&srq, 0, sizeof(srq));
    SaveBeginPacket sbg; std::memset(&sbg, 0, sizeof(sbg));
    SaveFileHeader  sfh; std::memset(&sfh, 0, sizeof(sfh)); // pathLen/dataLen = 0
    SaveDoneHeader  sdh; std::memset(&sdh, 0, sizeof(sdh)); // fileCount = 0
    SaveAckPacket   sak; std::memset(&sak, 0, sizeof(sak));
    LoadGoPacket    lg;  std::memset(&lg,  0, sizeof(lg));
    LoadReqPacket   lrq; std::memset(&lrq, 0, sizeof(lrq));
    LoadNackPacket  lnk; std::memset(&lnk, 0, sizeof(lnk));

    // --- Push one sentinel into every WORLD-STATE queue (33).
    in.pushEntity(1, 0, e);
    in.pushEvent(1, ev);
    in.pushInv(1, 0, cKey, 0, 0);
    in.pushWorldItems(1, 0, 0);
    in.pushWorldRemove(1, 0, 0);
    in.pushWorldClaim(1, 2, 0, 0);
    in.pushNpcCensus(1, 0, 0, 0);
    in.pushWorldDrop(1, wdp);
    in.pushWorldPickup(1, wpp);
    in.pushInvXfer(1, xf);
    in.pushMedical(1, mp);
    in.pushTreatment(1, tp);
    in.pushCombatHit(1, chp);
    in.pushSpeed(1, sp);
    in.pushStats(1, stp);
    in.pushMoney(1, mo);
    in.pushMoneyDelta(1, md);
    in.pushFaction(1, fa);
    in.pushTime(1, ti);
    in.pushDoor(1, dp);
    in.pushProd(1, pr);
    in.pushResearch(1, rp);
    in.pushDeed(1, de);
    in.pushBuildPlace(1, bp);
    in.pushBuildState(1, bs);
    in.pushBuildDoor(1, bd);
    in.pushBuildRemove(1, br);
    in.pushStealth(1, sl);
    in.pushSpawnReq(1, sq);
    in.pushSpawnInfo(1, si);
    in.pushCamHint(1, ch);
    in.pushCellClaim(1, cc);
    in.pushInvXferAck(1, xa);

    // --- Push one sentinel into every SESSION-PRESERVING queue (10).
    in.pushConnect(0);
    in.pushLeave(0);
    in.pushSaveReq(1, srq);
    in.pushSaveBegin(1, sbg);
    in.pushSaveFile(1, sfh, "", 0);
    in.pushSaveDone(1, sdh, 0);
    in.pushSaveAck(1, sak);
    in.pushLoadGo(0, lg);
    in.pushLoadReq(1, lrq);
    in.pushLoadNack(1, lnk);

    in.flushWorldState();

    // --- Every WORLD-STATE queue must now be empty.
    #define WS_EMPTY(name, type, drain) do { \
        std::deque<type> out; in.drain(out); \
        CHECK("world-state dropped: " name, out.empty()); } while (0)
    WS_EMPTY("entity",      InboundEntity,      drainEntities);
    WS_EMPTY("event",       InboundEvent,       drainEvents);
    WS_EMPTY("inv",         InboundInv,         drainInv);
    WS_EMPTY("worldItems",  InboundWorldItems,  drainWorldItems);
    WS_EMPTY("worldRemove", InboundWorldRemove, drainWorldRemove);
    WS_EMPTY("worldClaim",  InboundWorldClaim,  drainWorldClaim);
    WS_EMPTY("npcCensus",   InboundNpcCensus,   drainNpcCensus);
    WS_EMPTY("worldDrop",   InboundWorldDrop,   drainWorldDrops);
    WS_EMPTY("worldPickup", InboundWorldPickup, drainWorldPickups);
    WS_EMPTY("invXfer",     InboundInvXfer,     drainInvXfers);
    WS_EMPTY("medical",     InboundMedical,     drainMedical);
    WS_EMPTY("treatment",   InboundTreatment,   drainTreatments);
    WS_EMPTY("combatHit",   InboundCombatHit,   drainCombatHits);
    WS_EMPTY("speed",       InboundSpeed,       drainSpeed);
    WS_EMPTY("stats",       InboundStats,       drainStats);
    WS_EMPTY("money",       InboundMoney,       drainMoney);
    WS_EMPTY("moneyDelta",  InboundMoneyDelta,  drainMoneyDeltas);
    WS_EMPTY("faction",     InboundFaction,     drainFaction);
    WS_EMPTY("time",        InboundTime,        drainTime);
    WS_EMPTY("door",        InboundDoor,        drainDoor);
    WS_EMPTY("prod",        InboundProd,        drainProd);
    WS_EMPTY("research",    InboundResearch,    drainResearch);
    WS_EMPTY("deed",        InboundDeed,        drainDeed);
    WS_EMPTY("buildPlace",  InboundBuildPlace,  drainBuildPlace);
    WS_EMPTY("buildState",  InboundBuildState,  drainBuildState);
    WS_EMPTY("buildDoor",   InboundBuildDoor,   drainBuildDoor);
    WS_EMPTY("buildRemove", InboundBuildRemove, drainBuildRemove);
    WS_EMPTY("stealth",     InboundStealth,     drainStealth);
    WS_EMPTY("spawnReq",    InboundSpawnReq,    drainSpawnReqs);
    WS_EMPTY("spawnInfo",   InboundSpawnInfo,   drainSpawnInfos);
    WS_EMPTY("camHint",     InboundCamHint,     drainCamHints);
    WS_EMPTY("cellClaim",   InboundCellClaim,   drainCellClaims);
    WS_EMPTY("invXferAck",  InboundInvXferAck,  drainInvXferAcks);
    #undef WS_EMPTY

    // --- Every SESSION-PRESERVING queue must still hold its sentinel.
    #define SP_KEPT(name, type, drain) do { \
        std::deque<type> out; in.drain(out); \
        CHECK("session-preserving kept: " name, out.size() == 1); } while (0)
    { std::deque<u32> out; in.drainConnects(out);
      CHECK("session-preserving kept: connect", out.size() == 1); }
    { std::deque<u32> out; in.drainLeaves(out);
      CHECK("session-preserving kept: leave", out.size() == 1); }
    SP_KEPT("saveReq",   InboundSaveReq,   drainSaveReqs);
    SP_KEPT("saveBegin", InboundSaveBegin, drainSaveBegins);
    SP_KEPT("saveFile",  InboundSaveFile,  drainSaveFiles);
    SP_KEPT("saveDone",  InboundSaveDone,  drainSaveDones);
    SP_KEPT("saveAck",   InboundSaveAck,   drainSaveAcks);
    SP_KEPT("loadGo",    InboundLoadGo,    drainLoadGos);
    SP_KEPT("loadReq",   InboundLoadReq,   drainLoadReqs);
    SP_KEPT("loadNack",  InboundLoadNack,  drainLoadNacks);
    #undef SP_KEPT
}

// ---- 12. Worker-teardown ordering (models NetLink::stop()) -----------------------
// The NetLink::stop() fix: ENet teardown (enet_deinitialize + CloseHandle) must
// happen ONLY after the net worker has fully exited - the worker owns transport
// cleanup, so deinitializing while it still runs is a use-after-free / double
// free. The old code tore down unconditionally on a 2 s wait TIMEOUT. This locks
// the ordering invariant with a bare Win32 worker (no ENet dependency in the unit
// layer): stop() waits for the thread to exit FIRST, and the worker's cleanup
// strictly precedes the post-wait teardown.
static volatile LONG g_teardownSeq   = 0;
static LONG          g_workerCleanup = 0;
static LONG          g_teardown      = 0;
static DWORD WINAPI teardownWorker(LPVOID) {
    Sleep(40); // simulate the service loop draining + transport cleanup
    g_workerCleanup = InterlockedIncrement(&g_teardownSeq);
    return 0;
}
static void testTeardownOrdering() {
    std::printf("== worker-teardown ordering (NetLink::stop contract) ==\n");
    g_teardownSeq = 0; g_workerCleanup = 0; g_teardown = 0;
    HANDLE th = CreateThread(0, 0, &teardownWorker, 0, 0, 0);
    CHECK("worker thread created", th != 0);
    if (th) {
        DWORD wr = WaitForSingleObject(th, INFINITE); // stop(): wait for full exit
        CHECK("wait returns signalled (worker exited)", wr == WAIT_OBJECT_0);
        CloseHandle(th);
        g_teardown = InterlockedIncrement(&g_teardownSeq); // "enet_deinitialize" AFTER
        CHECK("worker cleanup precedes teardown", g_workerCleanup < g_teardown);
    }
}

// ---- ObjectHand: dual-layout unification contract (Phase 5b) ----------------
// Locks the ONE typed identity's two legacy array orders so a future edit can't
// silently reorder a field (the exact "dual hand[5] layout" desync footgun).
static void testObjectHandLayout() {
    std::printf("\n== ObjectHand layout (Phase 5b) ==\n");
    ObjectHand h;
    h.type = 11; h.container = 22; h.containerSerial = 33; h.index = 44; h.serial = 55;

    // OBJECT order  = {type, container, containerSerial, index, serial}
    u32 obj[5];
    h.toObjOrder(obj);
    CHECK("objOrder[0]=type",            obj[0] == 11);
    CHECK("objOrder[1]=container",       obj[1] == 22);
    CHECK("objOrder[2]=containerSerial", obj[2] == 33);
    CHECK("objOrder[3]=index",           obj[3] == 44);
    CHECK("objOrder[4]=serial",          obj[4] == 55);

    // CHAR-KEY order = {index, serial, type, container, containerSerial}
    u32 ck[5];
    h.toCharKey(ck);
    CHECK("charKey[0]=index",           ck[0] == 44);
    CHECK("charKey[1]=serial",          ck[1] == 55);
    CHECK("charKey[2]=type",            ck[2] == 11);
    CHECK("charKey[3]=container",       ck[3] == 22);
    CHECK("charKey[4]=containerSerial", ck[4] == 33);

    // The two legacy orders are genuinely different layouts (the footgun itself).
    CHECK("obj order != char-key order", std::memcmp(obj, ck, sizeof(obj)) != 0);

    // Round-trips: from*(to*(h)) == h for both orders.
    CHECK("fromObjOrder round-trips",  ObjectHand::fromObjOrder(obj).equals(h));
    CHECK("fromCharKey round-trips",   ObjectHand::fromCharKey(ck).equals(h));

    // Cross-order remap through the POD reproduces the manual [3][4][0][1][2]
    // char-key remap of an object-order array (the exact call-site footgun).
    u32 remap[5];
    ObjectHand::fromObjOrder(obj).toCharKey(remap);
    CHECK("obj->charkey remap [0]=obj[3]", remap[0] == obj[3]);
    CHECK("obj->charkey remap [1]=obj[4]", remap[1] == obj[4]);
    CHECK("obj->charkey remap [2]=obj[0]", remap[2] == obj[0]);
    CHECK("obj->charkey remap [3]=obj[1]", remap[3] == obj[1]);
    CHECK("obj->charkey remap [4]=obj[2]", remap[4] == obj[2]);

    // EntityState's named hand fields ARE object order: an ObjectHand built from
    // them must serialize to the same object-order array.
    EntityState e;
    std::memset(&e, 0, sizeof(e));
    e.hType = 11; e.hContainer = 22; e.hContainerSerial = 33; e.hIndex = 44; e.hSerial = 55;
    ObjectHand eh;
    eh.type = e.hType; eh.container = e.hContainer; eh.containerSerial = e.hContainerSerial;
    eh.index = e.hIndex; eh.serial = e.hSerial;
    CHECK("EntityState hand == object order", eh.equals(h));

    // resolvable(): the engine's null handle is all-zero; a non-zero index or
    // serial names a live object, type/container alone never do.
    ObjectHand z; z.type = z.container = z.containerSerial = z.index = z.serial = 0;
    CHECK("all-zero hand not resolvable",       !z.resolvable());
    ObjectHand idxOnly = z; idxOnly.index = 1;
    CHECK("index-only hand resolvable",          idxOnly.resolvable());
    ObjectHand serOnly = z; serOnly.serial = 1;
    CHECK("serial-only hand resolvable",         serOnly.resolvable());
    ObjectHand tcOnly = z; tcOnly.type = 7; tcOnly.container = 9;
    CHECK("type/container-only NOT resolvable", !tcOnly.resolvable());

    // equals() is field-sensitive on every one of the five fields.
    ObjectHand d;
    d = h; d.type++;            CHECK("equals detects type diff",      !d.equals(h));
    d = h; d.container++;       CHECK("equals detects container diff", !d.equals(h));
    d = h; d.containerSerial++; CHECK("equals detects cser diff",      !d.equals(h));
    d = h; d.index++;           CHECK("equals detects index diff",     !d.equals(h));
    d = h; d.serial++;          CHECK("equals detects serial diff",    !d.equals(h));
}

// ---- Engine fault throttle contract (Phase 5c) ------------------------------
// Locks the pure throttle decision that gates the "[engine] FAULT" oracle line:
// always emit the first hit, then at most once per interval, tolerating the
// wall-clock midnight wrap by erring toward an extra emit (never silent forever).
static void testEngineFaults() {
    std::printf("\n== engine fault throttle (Phase 5c) ==\n");
    using coop::engine::faultShouldLog;
    using coop::engine::FAULT_OP_COUNT;
    using coop::engine::FAULT_RESOLVE_CHAR;
    using coop::engine::FAULT_RESOLVE_OBJECT;

    CHECK("FAULT_OP_COUNT > 0",   (int)FAULT_OP_COUNT > 0);
    CHECK("resolve ops ordered",  FAULT_RESOLVE_CHAR == 0 && FAULT_RESOLVE_OBJECT == 1);

    unsigned long last = 0;
    CHECK("first hit logs",             faultShouldLog(1, 5000, &last, 1000));
    CHECK("first hit stamps lastMs",    last == 5000);
    CHECK("hit within interval quiet",  !faultShouldLog(2, 5500, &last, 1000));
    CHECK("lastMs unchanged in quiet",  last == 5000);
    CHECK("hit at interval logs",       faultShouldLog(3, 6000, &last, 1000));
    CHECK("lastMs advanced",            last == 6000);
    CHECK("just-before-boundary quiet", !faultShouldLog(4, 6999, &last, 1000));

    // Midnight wrap of wallClockMs: unsigned delta stays huge -> emit (never
    // permanently suppress across the wrap).
    unsigned long wrapLast = 86399000UL;
    CHECK("wrap boundary logs", faultShouldLog(5, 1000, &wrapLast, 1000));

    // Null lastMs (defensive): only the very first hit logs.
    CHECK("null lastMs first logs",  faultShouldLog(1, 0, 0, 1000));
    CHECK("null lastMs later quiet", !faultShouldLog(2, 0, 0, 1000));
}

static void testEngineCaps() {
    std::printf("\n== engine capability registry (Phase 5d) ==\n");
    using namespace coop::engine;

    // capName tokens are the oracle contract: stable, in enum order, guarded.
    CHECK("CAP_COUNT > 0",          (int)CAP_COUNT > 0);
    CHECK("cap core is hand",       std::strcmp(capName(CAP_HAND_RESOLVE), "hand_resolve") == 0);
    CHECK("cap saveload token",     std::strcmp(capName(CAP_SAVELOAD), "saveload") == 0);
    CHECK("cap faction token",      std::strcmp(capName(CAP_FACTION), "faction") == 0);
    CHECK("cap out-of-range low",   std::strcmp(capName((Capability)-1), "unknown") == 0);
    CHECK("cap out-of-range high",  std::strcmp(capName(CAP_COUNT), "unknown") == 0);

    // Synthetic resolved slots: two caps, one with a redundant required row.
    void* pA = (void*)1;  // saveload row 1
    void* pB = (void*)1;  // saveload row 2
    void* pH = (void*)1;  // hand_resolve (core)
    void* pF = (void*)1;  // faction (unrelated)
    const CapRow rows[] = {
        { &pA, "SaveManager::get",  CAP_SAVELOAD,     true },
        { &pB, "SaveManager::load", CAP_SAVELOAD,     true },
        { &pH, "hand::getCharacter",CAP_HAND_RESOLVE, true },
        { &pF, "FactionRelations",  CAP_FACTION,      true }
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    bool avail[CAP_COUNT];

    // (1) Everything resolved -> the three exercised caps are available; every
    // untouched cap (no rows) stays fail-closed false; core is OK.
    capEvaluate(rows, n, avail);
    CHECK("all-resolved saveload on",   avail[CAP_SAVELOAD]);
    CHECK("all-resolved hand on",       avail[CAP_HAND_RESOLVE]);
    CHECK("all-resolved faction on",    avail[CAP_FACTION]);
    CHECK("untouched cap fail-closed",  !avail[CAP_DOOR]);
    CHECK("core ok when hand resolved", capCoreOk(avail));

    // (2) One of saveload's two required rows drops -> the WHOLE cap fails, but
    // the other caps are untouched (no cross-contamination).
    pB = 0;
    capEvaluate(rows, n, avail);
    CHECK("partial-miss fails cap",     !avail[CAP_SAVELOAD]);
    CHECK("sibling cap unaffected",     avail[CAP_HAND_RESOLVE]);
    CHECK("unrelated cap unaffected",   avail[CAP_FACTION]);
    pB = (void*)1;

    // (3) Core hand-resolve missing -> capCoreOk trips (unsupported image),
    // while an unrelated cap can still be up.
    pH = 0;
    capEvaluate(rows, n, avail);
    CHECK("core down when hand missing", !capCoreOk(avail));
    CHECK("hand cap off",                !avail[CAP_HAND_RESOLVE]);
    CHECK("faction still up",            avail[CAP_FACTION]);
    pH = (void*)1;

    // (4) capRowResolved: null slot and null pointer both read as unresolved.
    void* live = (void*)1;
    void* dead = 0;
    CapRow rLive = { &live, "x", CAP_SAVELOAD, true };
    CapRow rDead = { &dead, "y", CAP_SAVELOAD, true };
    CapRow rNoSlot = { 0, "z", CAP_SAVELOAD, true };
    CHECK("row resolved (live ptr)",  capRowResolved(rLive));
    CHECK("row unresolved (null ptr)", !capRowResolved(rDead));
    CHECK("row unresolved (no slot)",  !capRowResolved(rNoSlot));
}

// Phase 6: the shared change-gated send/accept policy (ChangeGate.h). This
// locks the exact decisions the money + door channels used to inline by hand,
// so a future consolidation can't silently drift the wire cadence.
static void testChangeGate() {
    std::printf("\n== change-gate policy (Phase 6) ==\n");
    using namespace coop::sync;

    // --- gateSampleDue: first pass always samples, then once per sampleMs. ----
    CHECK("sample due first pass",      gateSampleDue(50000, 0, 1000));
    CHECK("sample not due within win", !gateSampleDue(50500, 50000, 1000));
    CHECK("sample due at interval",     gateSampleDue(51000, 50000, 1000));
    CHECK("sample due past interval",   gateSampleDue(52000, 50000, 1000));

    // --- gateSeqAccept: monotonic per-sender, first sight always accepted. ---
    CHECK("seq accept first sight",   gateSeqAccept(0, 1));
    CHECK("seq accept first sight hi",gateSeqAccept(0, 999));
    CHECK("seq accept newer",         gateSeqAccept(5, 6));
    CHECK("seq drop equal",          !gateSeqAccept(5, 5));
    CHECK("seq drop older",          !gateSeqAccept(5, 4));

    // --- gateShouldSend, MONEY flavor (minSend=1000, resend=5000, unsent=1) ---
    // A never-sent row streams once even unchanged (no silent seed).
    CHECK("money unsent unchanged sends",
          gateShouldSend(false, 90000, 0, 1000, 5000, true));
    // A change always crosses (row sent long ago, past the throttle).
    CHECK("money change sends",
          gateShouldSend(true, 90000, 80000, 1000, 5000, true));
    // ...but not within the min-send throttle window after a send.
    CHECK("money change throttled",
          !gateShouldSend(true, 80500, 80000, 1000, 5000, true));
    // Unchanged + sent recently (past throttle, before resend) holds.
    CHECK("money unchanged holds pre-resend",
          !gateShouldSend(false, 82000, 80000, 1000, 5000, true));
    // Unchanged + resend window elapsed -> safety resend.
    CHECK("money unchanged resends",
          gateShouldSend(false, 86000, 80000, 1000, 5000, true));

    // --- gateShouldSend, DOOR flavor (minSend=0, resend=10000, unsent=0) -----
    // A silently-seeded, never-sent, unchanged row HOLDS (no first-sight send).
    CHECK("door unsent unchanged holds",
          !gateShouldSend(false, 90000, 0, 0, 10000, false));
    // A real change crosses immediately (no throttle).
    CHECK("door change sends",
          gateShouldSend(true, 90000, 0, 0, 10000, false));
    // Unchanged, sent within resend window -> hold.
    CHECK("door unchanged holds pre-resend",
          !gateShouldSend(false, 85000, 80000, 0, 10000, false));
    // Unchanged, resend window elapsed -> safety resend.
    CHECK("door unchanged resends",
          gateShouldSend(false, 90000, 80000, 0, 10000, false));
    // A change sent 1ms ago still crosses under a zero throttle.
    CHECK("door change no throttle",
          gateShouldSend(true, 80001, 80000, 0, 10000, false));
}

// The same header, walked as a PREDICATE rather than as two shipped flavours.
// testChangeGate above pins money and doors at the values they run with, which
// catches a drift in those two channels; this walks the decision itself - each
// factor isolated, both sides of every boundary, and the unsigned tick wrap.
//
// Worth the checks because of the blast radius: money, factions, baked doors,
// placed buildings, placed-building doors, production, research and deeds all
// route their send decision through this ONE function, and unlike a wire-format
// slip a fault here is silent. Nothing fails; a channel simply stops transmitting,
// or transmits at a cadence nobody chose, and the first symptom is a peer whose
// doors are wrong an hour into a session.
//
// Unlike ReplicatorUtil.h this header is genuinely CRT-only (that is a stated goal
// in its own preamble), so every check below runs the PRODUCTION code.
static void testChangeGateTable() {
    std::printf("\n== change-gate truth table + tick wraparound (ChangeGate.h) ==\n");
    using namespace coop::sync;

    const unsigned long NOW_MS = 100000;          // "now"
    const unsigned long MIN_MS = 1000, RES_MS = 5000;
    // Last representable tick. nowMs() is QueryPerformanceCounter floored to ms in
    // an unsigned long, which on this toolset is 32 bits - so the clock wraps after
    // ~49.7 days of uptime, and every window here is an unsigned SUBTRACTION
    // precisely so that it keeps meaning the same thing across that wrap.
    const unsigned long WRAP_MS = (unsigned long)~0UL;

    // --- resendUnsent: what it decides, and what it must not touch --------------
    // The whole reason the parameter exists. Money has no silent seed step, so a
    // never-sent row must stream once; doors and factions seed their baseline
    // silently first, so a never-sent row must hold. That is the ONLY case it may
    // speak to.
    CHECK("a never-sent unchanged row is exactly what resendUnsent decides",
          gateShouldSend(false, NOW_MS, 0, MIN_MS, RES_MS, true) &&
          !gateShouldSend(false, NOW_MS, 0, MIN_MS, RES_MS, false));
    CHECK("a never-sent CHANGED row sends either way",
          gateShouldSend(true, NOW_MS, 0, MIN_MS, RES_MS, true) &&
          gateShouldSend(true, NOW_MS, 0, MIN_MS, RES_MS, false));
    {
        bool sentRowUnaffected = true;
        for (unsigned long age = 1; age <= 20000; age += 250)
            if (gateShouldSend(false, NOW_MS, NOW_MS - age, MIN_MS, RES_MS, true) !=
                gateShouldSend(false, NOW_MS, NOW_MS - age, MIN_MS, RES_MS, false))
                sentRowUnaffected = false;
        CHECK("...and it changes nothing at all for a row that HAS been sent",
              sentRowUnaffected);
    }

    // A never-sent row is never THROTTLED either: lastSendMs == 0 is the
    // never-sent sentinel, not a timestamp, so minSendMs has nothing to measure
    // against. That is what lets a channel seed its first row in the first tick of
    // a session instead of waiting out a window that never started.
    {
        bool neverThrottled = true;
        for (unsigned long m = 0; m <= 10000; m += 500)
            if (!gateShouldSend(true, 1, 0, m, RES_MS, false)) neverThrottled = false;
        CHECK("a never-sent row is never throttled, whatever minSendMs says",
              neverThrottled);
    }

    // --- both boundaries, both sides -------------------------------------------
    // The throttle is strict-less-than and the resend is greater-or-equal, so the
    // boundary tick itself lands on the SEND side of both. An off-by-one either way
    // costs a whole window on a channel nobody would think to look at.
    CHECK("throttle: one ms under minSendMs holds a change",
          !gateShouldSend(true, NOW_MS, NOW_MS - (MIN_MS - 1), MIN_MS, RES_MS, true));
    CHECK("throttle: exactly minSendMs releases it",
          gateShouldSend(true, NOW_MS, NOW_MS - MIN_MS, MIN_MS, RES_MS, true));
    CHECK("throttle: one ms over releases it",
          gateShouldSend(true, NOW_MS, NOW_MS - (MIN_MS + 1), MIN_MS, RES_MS, true));
    CHECK("throttle: a row sent this very millisecond is held",
          !gateShouldSend(true, NOW_MS, NOW_MS, MIN_MS, RES_MS, true));
    CHECK("throttle: minSendMs == 0 never throttles anything",
          gateShouldSend(true, NOW_MS, NOW_MS - 1, 0, RES_MS, false));
    CHECK("resend: one ms under resendMs holds an unchanged row",
          !gateShouldSend(false, NOW_MS, NOW_MS - (RES_MS - 1), MIN_MS, RES_MS, true));
    CHECK("resend: exactly resendMs fires",
          gateShouldSend(false, NOW_MS, NOW_MS - RES_MS, MIN_MS, RES_MS, true));
    CHECK("resend: one ms over fires",
          gateShouldSend(false, NOW_MS, NOW_MS - (RES_MS + 1), MIN_MS, RES_MS, true));

    // A change caught by the throttle is HELD, not dropped: the caller advances its
    // baseline only when the gate says send, so the same change is re-offered every
    // pass and crosses the moment the window expires. Dropping it would be a
    // permanent divergence on a channel whose only other corrective is the resend.
    {
        unsigned long crossed = 0;
        for (unsigned long age = 0; age <= 3000; age += 10)
            if (gateShouldSend(true, NOW_MS + age, NOW_MS, MIN_MS, RES_MS, true)) { crossed = age; break; }
        CHECK_EQ("a change held by the throttle crosses the moment it expires",
                 crossed, MIN_MS);
    }

    // The ordering relationship between the two windows, which is the one a retune
    // can silently break. The throttle is evaluated BEFORE the resend, so a
    // minSendMs longer than resendMs does not disable the safety resend - it slows
    // it to the throttle's cadence. The interval a peer actually waits for a lost
    // row is max(minSendMs, resendMs), never resendMs on its own.
    {
        unsigned long firstSend = 0;
        for (unsigned long age = 1; age <= 40000; ++age)
            if (gateShouldSend(false, NOW_MS + age, NOW_MS, 12000, 5000, true)) { firstSend = age; break; }
        CHECK_EQ("a throttle longer than the resend slows the resend to the throttle",
                 firstSend, 12000);
    }
    {
        SyncTuning tun;
        CHECK("the shipped money tuning keeps the throttle under the resend",
              tun.moneyMinSendMs < tun.moneyResendMs);
    }

    // --- unsigned tick wraparound ----------------------------------------------
    // A row sent 100 ms before the wrap, judged after it. The unsigned subtraction
    // has to report the TRUE age (151 ms below), not a 49-day one - a gate that got
    // this wrong would flush every channel's whole row set in one pass, or hold
    // every row for another 49 days, depending on which way it broke.
    CHECK("throttle holds correctly across the tick wrap",
          !gateShouldSend(true, 50, WRAP_MS - 100, MIN_MS, RES_MS, true));
    CHECK("throttle: one ms under the window, measured across the wrap",
          !gateShouldSend(true, 898, WRAP_MS - 100, MIN_MS, RES_MS, true));
    CHECK("throttle: releases exactly at the window across the wrap",
          gateShouldSend(true, 899, WRAP_MS - 100, MIN_MS, RES_MS, true));
    CHECK("resend: one ms under its window across the wrap",
          !gateShouldSend(false, 4898, WRAP_MS - 100, 0, RES_MS, true));
    CHECK("resend: fires exactly at its own window past the wrap",
          gateShouldSend(false, 4899, WRAP_MS - 100, 0, RES_MS, true));
    // The one place the wrap is NOT transparent: 0 is the never-sent sentinel, so a
    // row whose send lands on the single millisecond the clock reads 0 reads as
    // never-sent on the next pass. That is one tick per ~50 days of uptime, per row,
    // and it fails toward an extra send - the harmless direction. It is pinned
    // because it is the reason lastSendMs can never be repurposed into a field where
    // 0 is a legal timestamp.
    CHECK("a send stamped at tick 0 reads as never-sent (the sentinel's cost)",
          gateShouldSend(false, 5000, 0, 0, RES_MS, true) &&
          !gateShouldSend(false, 5000, 0, 0, RES_MS, false));

    // --- gateSampleDue: the same two properties at the pass level ---------------
    CHECK("sample: one ms under the interval holds", !gateSampleDue(NOW_MS + 999, NOW_MS, 1000));
    CHECK("sample: exactly at the interval is due",   gateSampleDue(NOW_MS + 1000, NOW_MS, 1000));
    CHECK("sample: a zero interval samples every pass", gateSampleDue(NOW_MS, NOW_MS, 0));
    CHECK("sample: the interval is measured correctly across the wrap",
          !gateSampleDue(898, WRAP_MS - 100, 1000) && gateSampleDue(899, WRAP_MS - 100, 1000));
    CHECK("sample: lastSampleMs 0 is the never-sampled sentinel, not a timestamp",
          gateSampleDue(0, 0, 1000) && gateSampleDue(5000, 0, 60000));

    // --- gateSeqAccept: the direction that actually bites -----------------------
    // seq is u32 on the wire and monotonic per sender, so an arithmetic wrap needs
    // billions of rows from one sender and is not the exposure. A sender whose
    // counter RESTARTS is: the receiver's seqSeen lives in the per-channel row map
    // (facRows_, doorRows_, ...), the outbound counters deliberately do not reset,
    // and the two only stay compatible because resetSession clears those maps. If a
    // future member escapes that sweep, the channel goes silent with no error.
    CHECK("a restarted or wrapped counter reads as stale, not as new",
          !gateSeqAccept(4000000000u, 1u));
    CHECK("...and a receiver that cleared its seen state accepts it again",
          gateSeqAccept(0u, 1u));
}

int main() {
    std::printf("prototest: KenshiCoop wire/hash/interp unit layer (protocol v%u)\n",
                (unsigned)PROTOCOL_VERSION);
    testSizes();
    testObjectHandLayout();
    testEngineFaults();
    testEngineCaps();
    testChangeGate();
    testChangeGateTable();
    testRoundTrips();
    testWireTermination();
    testFraming();
    testSaveCrc();
    testFolderFingerprint();
    testSaveXferRoundTrip();
    testContentHash();
    testInterp();
    testInterpStaleness();
    testInterpDelayBand();
    testInterpBoundaries();
    testHealDebounce();
    testSyncTuning();
    testSuppressionCaps();
    testDriveBands();
    testPeerSpeedSanity();
    testTargetAgeOut();
    testDriveConvergence();
    testOwnRanks();
    testSteamIdParse();
    testWorkPoseMatch();
    testTaskClear();
    testDeathRekey();
    testInboundLifecycle();
    testFlushWorldStateContract();
    testTeardownOrdering();
    std::printf("\nprototest: %d/%d checks passed%s\n",
                g_total - g_failed, g_total, g_failed ? " - FAIL" : " - PASS");
    return g_failed;
}
