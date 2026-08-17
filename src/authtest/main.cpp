// authoritytest - the authority/publish slice of the sync harness (auth step 1,
// task #43). Links the REAL ReplicatorAuthority.cpp + ReplicatorPublish.cpp
// (plus Drive/Core/Interp, as drivetest does) against the shared fake engine
// and a recording NetLink substitute.
//
// This suite PINS CURRENT BEHAVIOUR. Nothing here asserts how authority OUGHT
// to work; it asserts how it works TODAY, so the assertion overlay (auth steps
// 2-7) lands against a wall that screams when it moves something it did not
// mean to move. The pins were chosen from the doctrine's own load-bearing
// claims:
//
//   A. publishOwned: own squad always streams; the v0.65 near-band change gate
//      suppresses an unchanged NPC row, keeps a 200 ms keepalive, and passes a
//      moved row immediately.
//   B. publishNpcCensus: row count matches the world; the v58 truncation bit
//      is set when enumeration overflows and count stays <= NPC_CENSUS_MAX.
//   C. enforceHostAuthority: a census-absent body is suppressed only after the
//      1 s dwell; a truncated census suppresses NOTHING (absence is not
//      evidence); a body reappearing in the census is restored.
//
// C++03 / VC10.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../plugin/sync/Replicator.h" // pulls NetLink.h -> windows.h + enet
#include "../plugin/core/Inbound.h"
#include "../plugin/CoopLog.h"
#include "../plugin/game/EngineSync.h" // engine::setPeerCamHint (fixture attention)
#include "../drivetest/EngineFakes.h"
#include "NetLinkFakes.h"

using namespace coop;

// ---- Test reporting (drivetest's harness shape) -----------------------------

static int g_pass = 0, g_fail = 0, g_num = 0;
static void check(bool ok, const char* what) {
    ++g_num;
    std::printf("%s %d - %s\n", ok ? "ok  " : "FAIL", g_num, what);
    if (ok) ++g_pass; else ++g_fail;
}

static unsigned long qnow() {
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
            return GetTickCount();
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (unsigned long)(((unsigned __int64)c.QuadPart * 1000ULL) /
                           (unsigned __int64)freq.QuadPart);
}

static void spinMs(unsigned long ms) {
    unsigned long t0 = qnow();
    while (qnow() - t0 < ms) { /* real elapsed time, like drivetest */ }
}

// Host-flavoured Replicator: streams world NPCs, everything else ctor-default
// (cellAuth off = the live default; authority resolves to the host).
static Replicator* mkHost() {
    Replicator* r = new Replicator();
    r->setLeaderOnly(false);
    r->setStreamNpcs(true);
    r->setCensusRadius(2000.0f); // ctor default is 0 = census disabled
    r->setAuthArbiter(true);     // the host IS the arbiter (live: cfg.isHost)
    return r;
}

static unsigned int countSquadRows() {
    unsigned int n = 0;
    for (unsigned int i = 0; i < netfake::lastOwned.size(); ++i)
        if (netfake::lastOwned[i].hType == 9u) ++n; // squad fixture hType
    return n;
}

static unsigned int countNpcRows() {
    return (unsigned int)netfake::lastOwned.size() - countSquadRows();
}

// ---- A: publishOwned + the near-band change gate ----------------------------

static void tA_publishGate() {
    std::printf("\n-- A publishOwned: squad always, near-band gate on NPCs --\n");
    faketest::reset();
    netfake::reset();
    Replicator* r = mkHost();
    NetLink* net = new NetLink();
    GameWorld* gw = (GameWorld*)0x10;

    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    unsigned int S2[5] = { 9, 1, 1, 2, 102 };
    unsigned int N1[5] = { 1, 40, 7, 1, 5001 };
    unsigned int N2[5] = { 1, 40, 7, 2, 5002 };
    unsigned int N3[5] = { 1, 41, 7, 1, 5003 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    faketest::addChar(S2, 5.0f, 0.0f, 0.0f, true);
    Character* n1 = faketest::addChar(N1, 100.0f, 0.0f, 100.0f, false);
    faketest::addChar(N2, 110.0f, 0.0f, 100.0f, false);
    faketest::addChar(N3, 120.0f, 0.0f, 100.0f, false);

    r->publishOwned(gw, *net, 0);
    check(countSquadRows() == 2, "A: first publish carries both squad rows");
    check(countNpcRows() == 3, "A: first publish carries all three NPC rows");

    r->publishOwned(gw, *net, 0);
    check(countSquadRows() == 2, "A: squad rows never gated");
    check(countNpcRows() == 0,
          "A: unchanged NPC rows suppressed on the immediate re-publish");

    spinMs(250); // past the 200 ms keepalive
    r->publishOwned(gw, *net, 0);
    check(countNpcRows() == 3, "A: keepalive re-sends still NPCs after 200 ms");

    faketest::led(n1).x += 5.0f; // a real move
    r->publishOwned(gw, *net, 0);
    check(countNpcRows() == 1,
          "A: a moved NPC passes the gate immediately, still ones stay held");

    delete r;
    delete net;
}

// ---- B: census count + truncation bit ---------------------------------------

static void tB_censusTrunc() {
    std::printf("\n-- B publishNpcCensus: count + v58 truncation bit --\n");
    faketest::reset();
    netfake::reset();
    Replicator* r = mkHost();
    NetLink* net = new NetLink();
    GameWorld* gw = (GameWorld*)0x10;

    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    for (unsigned int i = 0; i < 3; ++i) {
        unsigned int H[5] = { 1, 50, 7, i + 1, 6000 + i };
        faketest::addChar(H, 50.0f + i, 0.0f, 50.0f, false);
    }

    r->publishNpcCensus(gw, *net, 0);
    check(netfake::censusCalls == 1, "B: census sent on the first beat");
    check(netfake::lastCensusCount == 3, "B: census row count matches the world");
    check(!netfake::lastCensusTrunc, "B: small world carries no truncation bit");

    // Overflow world: more bodies than NPC_CENSUS_MAX (512).
    faketest::reset();
    netfake::reset();
    delete r;
    r = mkHost();
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    for (unsigned int i = 0; i < 600; ++i) {
        unsigned int H[5] = { 1, 60 + (i / 200), 7, i + 1, 7000 + i };
        faketest::addChar(H, 10.0f + (float)i, 0.0f, 10.0f, false);
    }
    r->publishNpcCensus(gw, *net, 0);
    check(netfake::censusCalls == 1, "B: overflow census still sent");
    check(netfake::lastCensusTrunc,
          "B: overflowed enumeration sets the truncation bit");
    check(netfake::lastCensusCount <= 512,
          "B: truncated count stays within NPC_CENSUS_MAX");

    delete r;
    delete net;
}

// ---- C: suppression dwell, truncation hold, restore -------------------------

// Drive one census into the join and tick enforceHostAuthority over a window.
static void censusOf(Inbound* in, const unsigned int (*hands)[5],
                     const float (*pos)[3], unsigned int n, bool trunc) {
    static u32 h[512 * 5];
    static float p[512 * 3];
    for (unsigned int i = 0; i < n; ++i) {
        for (int j = 0; j < 5; ++j) h[i * 5 + j] = hands[i][j];
        for (int j = 0; j < 3; ++j) p[i * 3 + j] = pos[i][j];
    }
    in->pushNpcCensus(0, h, p, n, trunc);
}

static void tC_suppressDwell() {
    std::printf("\n-- C enforceHostAuthority: dwell, trunc hold, restore --\n");
    faketest::reset();
    netfake::reset();
    Replicator* r = new Replicator();
    r->setLeaderOnly(false);
    // Join-flavoured: does not stream world NPCs; judges them against the
    // host's census, exactly the live join configuration.
    r->setStreamNpcs(false);
    r->setCensusRadius(2000.0f);
    Inbound* in = new Inbound();
    GameWorld* gw = (GameWorld*)0x10;

    // Own squad at the origin: the attention anchor everything is judged near.
    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    // Two local world bodies inside the attention radius.
    unsigned int A[5] = { 1, 70, 7, 1, 8001 };
    unsigned int B[5] = { 1, 70, 7, 2, 8002 };
    Character* ca = faketest::addChar(A, 100.0f, 0.0f, 100.0f, false);
    Character* cb = faketest::addChar(B, 120.0f, 0.0f, 100.0f, false);

    const unsigned int handsA[1][5] = { { 1, 70, 7, 1, 8001 } };
    const float posA[1][3] = { { 100.0f, 0.0f, 100.0f } };

    // Census names only A. B is census-absent - but for less than the dwell.
    censusOf(in, handsA, posA, 1, false);
    r->applyNpcCensus(*in);
    unsigned long t0 = qnow();
    while (qnow() - t0 < 400) {
        r->enforceHostAuthority(gw, 1);
        spinMs(20);
    }
    check(faketest::led(cb).suppress == 0,
          "C: census-absent body is NOT suppressed before the 1 s dwell");

    // Keep judging past the dwell (census stays fresh via re-push).
    t0 = qnow();
    while (qnow() - t0 < 1500 && faketest::led(cb).suppress == 0) {
        censusOf(in, handsA, posA, 1, false);
        r->applyNpcCensus(*in);
        r->enforceHostAuthority(gw, 1);
        spinMs(20);
    }
    check(faketest::led(cb).suppress > 0,
          "C: census-absent body IS suppressed after the dwell");
    check(faketest::led(ca).suppress == 0,
          "C: census-present body is never suppressed");

    // Restore: B reappears in the census.
    const unsigned int handsAB[2][5] = {
        { 1, 70, 7, 1, 8001 }, { 1, 70, 7, 2, 8002 }
    };
    const float posAB[2][3] = {
        { 100.0f, 0.0f, 100.0f }, { 120.0f, 0.0f, 100.0f }
    };
    // RESTORE_AFTER_MS is 2000 of accumulated census-present dwell; give the
    // loop real headroom past it so the pin tests the dwell, not the margin.
    t0 = qnow();
    while (qnow() - t0 < 5000 && faketest::led(cb).restore == 0) {
        censusOf(in, handsAB, posAB, 2, false);
        r->applyNpcCensus(*in);
        r->enforceHostAuthority(gw, 1);
        spinMs(20);
    }
    check(faketest::led(cb).restore > 0,
          "C: reappearing in the census restores the body");

    // Truncation hold: fresh instance, same absent-B shape, but the census
    // carries the truncation bit. Nothing may be suppressed, ever.
    faketest::reset();
    delete r;
    delete in;
    r = new Replicator();
    r->setLeaderOnly(false);
    r->setStreamNpcs(false);
    in = new Inbound();
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    ca = faketest::addChar(A, 100.0f, 0.0f, 100.0f, false);
    cb = faketest::addChar(B, 120.0f, 0.0f, 100.0f, false);
    t0 = qnow();
    while (qnow() - t0 < 1800) {
        censusOf(in, handsA, posA, 1, true); // TRUNCATED
        r->applyNpcCensus(*in);
        r->enforceHostAuthority(gw, 1);
        spinMs(20);
    }
    check(faketest::led(cb).suppress == 0,
          "C: a truncated census suppresses NOTHING (absence is not evidence)");

    delete r;
    delete in;
}

// ---- D: the shadow assign map (auth step 2) ---------------------------------
// Policy v1: hContainer parity + the eligibility veto. Deterministic by
// construction; these pins hold the three properties the design leans on -
// parity split, squad coherence (one container = one owner), and the veto
// keeping a bleeding body with the incumbent regardless of parity.

static void tD_assignMap() {
    std::printf("\n-- D shadow assign map: parity, coherence, veto --\n");
    faketest::reset();
    netfake::reset();
    Replicator* r = mkHost();
    NetLink* net = new NetLink();
    GameWorld* gw = (GameWorld*)0x10;

    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    // Container 40 (even -> host): three bodies. Container 41 (odd -> join): two.
    Character* hurtOdd = 0;
    for (unsigned int i = 0; i < 3; ++i) {
        unsigned int H[5] = { 1, 40, 7, i + 1, 9100 + i };
        faketest::addChar(H, 50.0f + i, 0.0f, 50.0f, false);
    }
    for (unsigned int i = 0; i < 2; ++i) {
        unsigned int H[5] = { 1, 41, 7, i + 1, 9200 + i };
        Character* c = faketest::addChar(H, 60.0f + i, 0.0f, 50.0f, false);
        if (i == 1) hurtOdd = c;
    }

    r->publishNpcCensus(gw, *net, 0);
    unsigned int nh = 0, nj = 0; unsigned long veto = 0;
    unsigned long gen1 = r->debugAssignCounts(&nh, &nj, &veto);
    check(gen1 == 1, "D: map computed on the census beat");
    check(nh == 3 && nj == 2,
          "D: hContainer parity - even container to host, odd to join");
    check(veto == 0, "D: healthy world takes no vetoes");

    // Same world, recomputed: identical split (determinism), generation bumps.
    spinMs(1100); // past the census 1 Hz gate
    r->publishNpcCensus(gw, *net, 0);
    unsigned int nh2 = 0, nj2 = 0;
    unsigned long gen2 = r->debugAssignCounts(&nh2, &nj2, 0);
    check(gen2 == 2 && nh2 == nh && nj2 == nj,
          "D: recompute is deterministic and bumps the generation");

    // A bleeding body in the ODD container: the veto pins it to the host.
    faketest::led(hurtOdd).bleedRate = 2.0f;
    spinMs(1100);
    r->publishNpcCensus(gw, *net, 0);
    r->debugAssignCounts(&nh2, &nj2, &veto);
    check(nh2 == 4 && nj2 == 1 && veto == 1,
          "D: a bleeding body stays with the incumbent regardless of parity");

    delete r;
    delete net;
}

// ---- E: assertion carriers (auth step 3) ------------------------------------
// The census beat must EMIT the author tail + changed-ownership ASSIGN rows,
// and the receive side must STORE assertions behind the seq guard - with no
// behaviour attached to any of it yet.

static void tE_assignCarriers() {
    std::printf("\n-- E assertion carriers: emit, store, seq guard --\n");
    faketest::reset();
    netfake::reset();
    Replicator* r = mkHost();
    NetLink* net = new NetLink();
    GameWorld* gw = (GameWorld*)0x10;

    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    unsigned int HE[5] = { 1, 40, 7, 1, 9301 }; // even container -> host
    unsigned int HO[5] = { 1, 41, 7, 1, 9302 }; // odd container -> join
    faketest::addChar(HE, 50.0f, 0.0f, 50.0f, false);
    faketest::addChar(HO, 60.0f, 0.0f, 50.0f, false);

    r->publishNpcCensus(gw, *net, 0);
    check(netfake::lastCensusAuthors.size() == 2,
          "E: census carries one author byte per row");
    check(netfake::lastCensusMapGen == 1,
          "E: census carries the assign-map generation");
    check(netfake::assigns.size() == 2,
          "E: first beat asserts every ownership as an ASSIGN row");

    // Second beat, same world: authors tail rides again, but no ownership
    // changed, so no new ASSIGN rows.
    spinMs(1100);
    r->publishNpcCensus(gw, *net, 0);
    check(netfake::assigns.size() == 2,
          "E: unchanged ownership emits no further ASSIGN rows");

    // Receive-and-store on a join-flavoured instance.
    Replicator* rj = new Replicator();
    rj->setLeaderOnly(false);
    Inbound* in = new Inbound();
    AuthAssignPacket ap;
    std::memset(&ap, 0, sizeof(ap));
    ap.type = (u8)PKT_AUTH_ASSIGN;
    ap.newOwner = 1; ap.prevOwner = 0xFF;
    ap.ownerId = 0; ap.assignSeq = 5; ap.mapGen = 3;
    ap.hand[0] = 1; ap.hand[1] = 41; ap.hand[2] = 7;
    ap.hand[3] = 1; ap.hand[4] = 9302;
    ap.sidWitness = 0; // 0 = sender read no witness; the store proceeds
    in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    unsigned char owner = 9; u32 seq = 0;
    check(rj->debugAssignRecv(HO, &owner, &seq) && owner == 1 && seq == 5,
          "E: an assertion is stored with its owner and seq");

    // A row whose witness names a DIFFERENT template than the local body must
    // be refused outright - hands get recycled, and following one onto a
    // different NPC is the MIGRATE REFUSED lesson applied to ownership.
    AuthAssignPacket bad = ap;
    bad.assignSeq = 50;
    bad.newOwner = 0;
    bad.sidWitness = 0xDEAD; // cannot match hash("fake-sid-1-9302")
    in->pushAuthAssign(bad);
    rj->applyAuthAssigns(*in, 1);
    rj->debugAssignRecv(HO, &owner, &seq);
    check(owner == 1 && seq == 5,
          "E: a witness mismatch refuses the row - stored assertion unmoved");

    // A stale row (lower seq) must be dropped by the guard.
    ap.newOwner = 0; ap.assignSeq = 4;
    in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    rj->debugAssignRecv(HO, &owner, &seq);
    check(owner == 1 && seq == 5,
          "E: a stale seq is dropped - the stored owner does not move");

    // A newer row supersedes.
    ap.newOwner = 0; ap.assignSeq = 6;
    in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    rj->debugAssignRecv(HO, &owner, &seq);
    check(owner == 0 && seq == 6,
          "E: a newer seq supersedes the stored assertion");

    delete rj;
    delete in;
    delete r;
    delete net;
}

// ---- F: the read-path flip (auth step 4) ------------------------------------
// The design's core invariant, pinned: for every asserted body, EXACTLY ONE
// client's publish gate passes. Both compute "mine" = two writers (the oldest
// bug in the file); both compute "theirs" = zero writers (a frozen body).
// Assertion makes both unreachable BY CONSTRUCTION - this is the table that
// proves it, plus the shadow mode's do-nothing guarantee and the A_mine drive
// eviction.

static void tF_readPathFlip() {
    std::printf("\n-- F read-path flip: one writer per asserted body --\n");
    faketest::reset();
    netfake::reset();

    // The HOST instance streams and asserts; cellAuth ON so the cell verdict
    // is live underneath (both squads in one cell = host wins everything
    // without the overlay - the exact live degenerate case).
    Replicator* rh = mkHost();
    rh->setCellAuth(true);
    rh->setAuthAssertMode(2);
    NetLink* net = new NetLink();
    GameWorld* gw = (GameWorld*)0x10;

    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    unsigned int HE[5] = { 1, 40, 7, 1, 9401 }; // even -> host
    unsigned int HO[5] = { 1, 41, 7, 1, 9402 }; // odd  -> join
    faketest::addChar(HE, 50.0f, 0.0f, 50.0f, false);
    faketest::addChar(HO, 60.0f, 0.0f, 50.0f, false);

    // The near-band attention gate drops NPC rows the PEER cannot see; give
    // the peer a camera anchor over the fixture so streaming is warranted.
    engine::setPeerCamHint(true, 55.0f, 0.0f, 50.0f);
    // Census beat computes + asserts the map; then a publish.
    rh->publishNpcCensus(gw, *net, 0);
    netfake::lastOwned.clear();
    rh->publishOwned(gw, *net, 0);
    unsigned int hostNpc = countNpcRows();
    check(hostNpc == 1,
          "F: host ON - publishes exactly its asserted body, not the join's");

    // The JOIN instance: same world registry, receives the assertions.
    Replicator* rj = new Replicator();
    rj->setLeaderOnly(false);
    rj->setStreamNpcs(true);     // symmetric publish, as the overlay intends
    rj->setCensusRadius(2000.0f);
    rj->setCellAuth(true);
    rj->setAuthAssertMode(2);
    Inbound* in = new Inbound();
    for (unsigned int i = 0; i < netfake::assigns.size(); ++i)
        in->pushAuthAssign(netfake::assigns[i]);
    rj->applyAuthAssigns(*in, 1);
    netfake::lastOwned.clear();
    rj->publishOwned(gw, *net, 1);
    unsigned int joinNpc = countNpcRows();
    check(joinNpc == 1,
          "F: join ON - publishes exactly the body asserted to it");
    check(hostNpc + joinNpc == 2,
          "F: one writer per body - the two publish sets partition the world");

    // SHADOW is a no-op on behaviour: same world, mode 1, the join publishes
    // NOTHING (cell verdict: host wins the contested cell) - bit-identical to
    // v0.66 - while the divergence counter records what ON would have changed.
    Replicator* rs = new Replicator();
    rs->setLeaderOnly(false);
    rs->setStreamNpcs(true);
    rs->setCensusRadius(2000.0f);
    rs->setCellAuth(true);
    rs->setAuthAssertMode(1);
    Inbound* in2 = new Inbound();
    for (unsigned int i = 0; i < netfake::assigns.size(); ++i)
        in2->pushAuthAssign(netfake::assigns[i]);
    rs->applyAuthAssigns(*in2, 1);
    netfake::lastOwned.clear();
    rs->publishOwned(gw, *net, 1);
    check(countNpcRows() == 0,
          "F: SHADOW keeps the cell verdict - join publishes nothing");

    // A_mine eviction: give the join drive state for its asserted body, then
    // re-assert it to the join; the store pass must evict the drive entry.
    EntityState e;
    std::memset(&e, 0, sizeof(e));
    e.hType = 1; e.hContainer = 41; e.hContainerSerial = 7;
    e.hIndex = 1; e.hSerial = 9402;
    e.x = 60.0f; e.z = 50.0f; e.cSpeed = 5.0f; e.cMoving = 1;
    in->pushEntity(0, (u32)1000, e);
    rj->ingest(*in);
    rj->applyTargets(gw);
    // Eviction fires on a FLIP toward us, not on a re-assert of what we
    // already hold - so take the body away first, then grant it back.
    AuthAssignPacket ap;
    std::memset(&ap, 0, sizeof(ap));
    ap.type = (u8)PKT_AUTH_ASSIGN;
    ap.newOwner = 0; ap.prevOwner = 1;
    ap.ownerId = 0; ap.assignSeq = 999; ap.mapGen = 8;
    ap.hand[0] = 1; ap.hand[1] = 41; ap.hand[2] = 7;
    ap.hand[3] = 1; ap.hand[4] = 9402;
    in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    ap.newOwner = 1; ap.prevOwner = 0;
    ap.assignSeq = 1000; ap.mapGen = 9;
    in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    check(rj->debugInterpDelayMs(HO) == 0,
          "F: gaining authorship evicts the drive state in the same breath");

    delete rh; delete rj; delete rs;
    delete in; delete in2;
    delete net;
}

// ---- G: handoff + liveness (auth step 5) ------------------------------------

static void tG_handoffLiveness() {
    std::printf("\n-- G handoff + liveness: losing side stops, silence reverts --\n");
    faketest::reset();
    netfake::reset();
    engine::setPeerCamHint(true, 55.0f, 0.0f, 50.0f);

    // Host with a short liveness horizon so the pin does not wait 10 s.
    Replicator* rh = mkHost();
    rh->setCellAuth(true);
    rh->setAuthAssertMode(2);
    rh->setAuthLivenessMs(400);
    NetLink* net = new NetLink();
    GameWorld* gw = (GameWorld*)0x10;

    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    unsigned int HO[5] = { 1, 41, 7, 1, 9501 }; // odd -> join by parity
    faketest::addChar(HO, 60.0f, 0.0f, 50.0f, false);

    rh->publishNpcCensus(gw, *net, 0);
    unsigned int nh = 0, nj = 0;
    rh->debugAssignCounts(&nh, &nj, 0);
    check(nj == 1, "G: parity hands the odd body to the join");

    // The join never streams it. Recompute past the horizon: the assignment
    // must revert to the host - one writer who is not writing is zero writers.
    unsigned long t0 = qnow();
    bool reverted = false;
    while (qnow() - t0 < 3000) {
        spinMs(1100); // census cadence
        rh->publishNpcCensus(gw, *net, 0);
        rh->debugAssignCounts(&nh, &nj, 0);
        if (nj == 0) { reverted = true; break; }
    }
    check(reverted, "G: a silent join-side assignment reverts to the host");

    // The revoke must also have gone out as a fresh ASSIGN row (owner 0 with a
    // newer seq than the original grant).
    bool sawRevoke = false;
    for (unsigned int i = 0; i < netfake::assigns.size(); ++i) {
        const AuthAssignPacket& a = netfake::assigns[i];
        if (a.hand[4] == 9501 && a.newOwner == 0 && a.assignSeq > 1)
            sawRevoke = true;
    }
    check(sawRevoke, "G: the revoke is asserted on the wire, not just locally");

    // Losing-side hygiene on the join: gain, publish, lose, re-gain - the
    // near-band gate must treat the re-gain as a first sighting.
    Replicator* rj = new Replicator();
    rj->setLeaderOnly(false);
    rj->setStreamNpcs(true);
    rj->setCensusRadius(2000.0f);
    rj->setCellAuth(true);
    rj->setAuthAssertMode(2);
    Inbound* in = new Inbound();
    AuthAssignPacket ap;
    std::memset(&ap, 0, sizeof(ap));
    ap.type = (u8)PKT_AUTH_ASSIGN;
    ap.ownerId = 0;
    ap.hand[0] = 1; ap.hand[1] = 41; ap.hand[2] = 7;
    ap.hand[3] = 1; ap.hand[4] = 9501;
    ap.newOwner = 1; ap.assignSeq = 2001; in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    netfake::lastOwned.clear();
    rj->publishOwned(gw, *net, 1);
    check(countNpcRows() == 1, "G: granted body publishes");
    ap.newOwner = 0; ap.assignSeq = 2002; in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    netfake::lastOwned.clear();
    rj->publishOwned(gw, *net, 1);
    check(countNpcRows() == 0, "G: revoked body stops publishing at once");
    ap.newOwner = 1; ap.assignSeq = 2003; in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    netfake::lastOwned.clear();
    rj->publishOwned(gw, *net, 1);
    check(countNpcRows() == 1,
          "G: a re-gain publishes immediately (near-gate memory was cleared)");

    delete rh; delete rj;
    delete in; delete net;
}

// ---- H: proxy reassignment (auth step 7) ------------------------------------

static void tH_proxyReassign() {
    std::printf("\n-- H proxy reassignment: grantable spawns, canonical rows --\n");
    faketest::reset();
    netfake::reset();
    engine::setPeerCamHint(true, 55.0f, 0.0f, 50.0f);
    GameWorld* gw = (GameWorld*)0x10;
    NetLink* net = new NetLink();

    // HOST policy: an answered spawn is granted to the join even when parity
    // says host (even container), and a liveness revoke backs off rather than
    // re-granting at the next beat.
    Replicator* rh = mkHost();
    rh->setCellAuth(true);
    rh->setAuthAssertMode(2);
    rh->setAuthLivenessMs(400);
    unsigned int S1[5] = { 9, 1, 1, 1, 101 };
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    unsigned int HE[5] = { 1, 40, 7, 1, 9601 }; // EVEN container: parity=host
    faketest::addChar(HE, 50.0f, 0.0f, 50.0f, false);
    rh->noteSpawnAnswered(HE);
    rh->publishNpcCensus(gw, *net, 0);
    unsigned int nh = 0, nj = 0;
    rh->debugAssignCounts(&nh, &nj, 0);
    check(nj == 1,
          "H: an answered spawn is granted to the join over parity");

    // Never streamed -> liveness revoke -> and the BACKOFF holds: further
    // beats must not re-grant inside the backoff window.
    unsigned long t0 = qnow();
    bool reverted = false;
    while (qnow() - t0 < 3000) {
        spinMs(1100);
        rh->publishNpcCensus(gw, *net, 0);
        rh->debugAssignCounts(&nh, &nj, 0);
        if (nj == 0) { reverted = true; break; }
    }
    check(reverted, "H: the unexercised grant is revoked");
    spinMs(1100);
    rh->publishNpcCensus(gw, *net, 0);
    rh->debugAssignCounts(&nh, &nj, 0);
    check(nj == 0,
          "H: the backoff keeps a revoked grant from flapping straight back");

    // JOIN translation: a body held only as a proxy publishes under its
    // CANONICAL hand once asserted - never the local one.
    faketest::reset();
    netfake::reset();
    engine::setPeerCamHint(true, 55.0f, 0.0f, 50.0f);
    Replicator* rj = new Replicator();
    rj->setLeaderOnly(false);
    rj->setStreamNpcs(true);
    rj->setCensusRadius(2000.0f);
    rj->setCellAuth(true);
    rj->setAuthAssertMode(2);
    Inbound* in = new Inbound();
    faketest::addChar(S1, 0.0f, 0.0f, 0.0f, true);
    // The local proxy body: registered under a LOCAL hand.
    unsigned int LOCALH[5] = { 1, 900, 3, 4, 111222 };
    Character* proxy = faketest::addChar(LOCALH, 60.0f, 0.0f, 50.0f, false);
    // Its canonical identity, as the host knows the body.
    unsigned int CANONH[5] = { 1, 41, 7, 9, 654321 };
    rj->debugBindProxy(CANONH, proxy);
    // Host asserts the canonical body to the join.
    AuthAssignPacket ap;
    std::memset(&ap, 0, sizeof(ap));
    ap.type = (u8)PKT_AUTH_ASSIGN;
    ap.ownerId = 0; ap.newOwner = 1; ap.assignSeq = 3001;
    ap.hand[0] = CANONH[0]; ap.hand[1] = CANONH[1]; ap.hand[2] = CANONH[2];
    ap.hand[3] = CANONH[3]; ap.hand[4] = CANONH[4];
    in->pushAuthAssign(ap);
    rj->applyAuthAssigns(*in, 1);
    netfake::lastOwned.clear();
    rj->publishOwned(gw, *net, 1);
    bool canonRow = false, localRow = false;
    for (unsigned int i = 0; i < netfake::lastOwned.size(); ++i) {
        const EntityState& e = netfake::lastOwned[i];
        if (e.hSerial == 654321u) canonRow = true;
        if (e.hSerial == 111222u) localRow = true;
    }
    check(canonRow,
          "H: the proxy publishes under the CANONICAL hand the peer resolves");
    check(!localRow,
          "H: the local hand never reaches the wire");

    // The NON-arbiter must never arbitrate: a join-side census beat computes
    // no map and asserts nothing, even with everything else identical. Found
    // live in v0.67's first minute - under cell authority both sides stream,
    // so gating the map on streamNpcs_ made BOTH sides arbiters, and the
    // join's self-computed map fought the host's.
    netfake::reset();
    unsigned int nh2 = 0, nj2 = 0;
    unsigned long genJ = rj->debugAssignCounts(&nh2, &nj2, 0);
    rj->publishNpcCensus(gw, *net, 1);
    genJ = rj->debugAssignCounts(&nh2, &nj2, 0);
    check(genJ == 0 && nh2 == 0 && nj2 == 0,
          "H: the join computes NO map on its census beat");
    check(netfake::assigns.empty(),
          "H: the join emits NO assertions");
    check(netfake::lastCensusAuthors.empty(),
          "H: the join's census carries no author tail");

    delete rh; delete rj;
    delete in; delete net;
}

// ---- main -------------------------------------------------------------------

int main() {
    std::printf("authoritytest: REAL ReplicatorAuthority/Publish against a "
                "fake engine\n");
    coop::logInit("authtest", "authtest"); // harmless if it lands in cwd

    tA_publishGate();
    tB_censusTrunc();
    tC_suppressDwell();
    tD_assignMap();
    tE_assignCarriers();
    tF_readPathFlip();
    tG_handoffLiveness();
    tH_proxyReassign();

    std::printf("\nauthoritytest: %d/%d checks passed - %s\n",
                g_pass, g_num, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
