// drivetest - the first slice of the stub-engine sync harness (ROADMAP Phase 2
// item 27). Links the REAL receiver drive - ReplicatorDrive.cpp and
// ReplicatorCore.cpp (ctor, ingest, lifecycle audit), plus the real Interp.cpp
// and CoopLog.cpp - against the fake engine in EngineFakes.cpp, and drives it
// through the same public sequence Plugin.cpp uses each tick:
//
//     inbound.pushEntity(...)   (what NetLink's batch arm does)
//     repl.ingest(inbound)      (pre-engine)
//     repl.applyTargets(gw)     (post-engine)
//
// No game, no KenshiLib, no ENet traffic, no Steam. Time is real (the drive's
// nowMs() is QPC), so tests pace their sample schedules in wall clock and stay
// under ~10 s each.
//
// What this slice pins:
//   T1  tier-hysteresis-monotone - the v0.58 MID_EXIT_QUIET_MS rule: a still
//       body on a bursty 50,50,900 ms cadence latches MID once and stays
//       PARKED, instead of flapping PARKED<->MID per rotation (the v0.57 raw
//       segment read flapped at hundreds of cycles/min on this input shape).
//   T2  release-then-still - one mid-rest release for a still 500 ms-cadence
//       body, no re-adopt, and the CURRENT per-keepalive re-park documented as
//       a pin (the baseline any release-restructure must be measured against).
//   T3  walker-steady - a 5 u/s mid-cadence walker is walk-driven with zero
//       parks/endActions/hard-snaps (the shuffle-regression tripwire), and a
//       driven squad walker gets its locomotion mirror.
//
// Observation seams (no header edits anywhere):
//   * EngineFakes ledger - per-body counts of every engine call the drive made;
//   * drivehooks - classification edges recorded by the stubbed debugMark
//     (applyTargets reports PARKED/MID/HI through it every tick).

#include <cstdio>
#include <cstring>
#include <vector>

#include "../plugin/sync/Replicator.h" // pulls NetLink.h -> windows.h + enet
#include "../plugin/core/Inbound.h"
#include "../plugin/CoopLog.h"
#include "EngineFakes.h"
#include "DriveTestHooks.h"

using namespace coop;

// ---- Test reporting (netlinktest's harness shape) ---------------------------

static int g_pass = 0, g_fail = 0, g_num = 0;
static void check(bool ok, const char* what) {
    ++g_num;
    std::printf("%s %d - %s\n", ok ? "ok  " : "FAIL", g_num, what);
    if (ok) ++g_pass; else ++g_fail;
}

// ---- Clock ------------------------------------------------------------------
// Same formula as ReplicatorUtil.h's nowMs() (which is anonymous-namespace and
// unreachable from here): QPC floored to ms. Using the same derivation keeps
// the sendMs stamps this file writes and the clock ingest() maps them against
// in near-perfect agreement, so ring segments equal the planned gaps.

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

// ---- Inputs -----------------------------------------------------------------

static EntityState mkState(const unsigned int h[5], float x, float y, float z,
                           float cSpeed, bool cMoving) {
    EntityState e;
    std::memset(&e, 0, sizeof(e));
    e.hType = h[0]; e.hContainer = h[1]; e.hContainerSerial = h[2];
    e.hIndex = h[3]; e.hSerial = h[4];
    e.x = x; e.y = y; e.z = z; e.heading = 0.0f;
    e.cSpeed = cSpeed;
    e.cMoving = cMoving ? 1 : 0;
    e.task = TASK_NONE;
    e.rawTask = TASK_NONE;
    e.bodyState = 0;
    return e;
}

struct Send {
    unsigned long t;   // ms offset from test start
    EntityState   e;
};

// Join-flavoured Replicator: the config Plugin.cpp installs on a join client
// for the levers this slice exercises (AI suspend + damage guard default ON;
// everything else keeps its ctor default).
static Replicator* mkRepl() {
    Replicator* r = new Replicator();
    r->setLeaderOnly(false);
    r->setAiSuspend(true);
    r->setDamageGuard(true);
    return r;
}

// ---- T1: tier-hysteresis-monotone ------------------------------------------
// One still NPC (x creeps at 0.05 u/s - distinct samples, far under
// NPC_MOVE_VEL so the walk/rest classifier reads AT REST; a genuinely
// advancing body never reaches PARKED at all, which is why the still shape is
// the one that can observe the flap). Segment pattern 50,50,900 repeating:
// the publisher-scan-overlap burst shape from the 2026-08-10 deep dive. The
// v0.58 rule (midHeld latches on any >250 ms segment; exits only after
// MID_EXIT_QUIET_MS=1500 with no long segment, and the 900 ms segment arrives
// every 1000 ms) must hold MID indefinitely. The v0.57 raw per-tick read was
// bimodal on this input and cycled the body through the mid-rest release once
// per flap.

static void t1_tierHysteresis() {
    std::printf("\n-- T1 tier-hysteresis-monotone (50,50,900 ms bursts, still body) --\n");
    faketest::reset();
    drivehooks::reset();
    Replicator* r = mkRepl();
    Inbound* in = new Inbound();
    GameWorld* gw = (GameWorld*)0x10;
    unsigned int H[5] = { 2, 7, 1, 100, 5000 };
    Character* c = faketest::addChar(H, 100.0f, 0.0f, 100.0f, false);

    std::vector<Send> plan;
    {
        const unsigned long off[3] = { 0, 50, 100 };
        for (unsigned int k = 0; k < 8; ++k) {
            for (int j = 0; j < 3; ++j) {
                Send s;
                s.t = k * 1000 + off[j];
                float x = 100.0f + 0.05f * ((float)s.t / 1000.0f);
                s.e = mkState(H, x, 0.0f, 100.0f, 0.0f, false);
                plan.push_back(s);
            }
        }
    }

    const unsigned long DUR = 8200;
    unsigned long t0 = qnow();
    size_t next = 0;
    unsigned long ticks = 0, latchedAt = 0, cyclesAtLatch = 0;
    for (;;) {
        unsigned long el = qnow() - t0;
        while (next < plan.size() && plan[next].t <= el) {
            in->pushEntity(0, (u32)(t0 + plan[next].t), plan[next].e);
            ++next;
        }
        r->ingest(*in);
        r->applyTargets(gw);
        ++ticks;
        if (latchedAt == 0 &&
            drivehooks::entries(c, drivehooks::CLS_PARKED) > 0) {
            latchedAt = el;
            cyclesAtLatch = drivehooks::parkedMidCycles(c);
        }
        if (el >= DUR) break;
        Sleep(5);
    }

    unsigned long cycles = drivehooks::parkedMidCycles(c) - cyclesAtLatch;
    faketest::CharLedger& L = faketest::led(c);
    std::printf("   ticks=%lu samples=%u latchedAt=%lums cyclesAfterLatch=%lu "
                "parks=%lu clearGoals=%lu endAction=%lu walkTo=%lu snaps=%lu\n",
                ticks, (unsigned)next, latchedAt, cycles,
                L.park, L.clearGoals, L.endAction, L.walkTo, L.applyRaw);

    check(latchedAt != 0, "T1: mid tier latched (first PARKED release seen)");
    // The monotone pin. Spec bound: no more than one PARKED<->MID cycle per
    // 10 s once latched; this run's post-latch window is ~7 s. v0.57's raw
    // segment read would fail this input at roughly a cycle per rotation.
    check(cycles <= 1, "T1: <=1 PARKED<->MID cycle after latch (v0.58 MID_EXIT_QUIET_MS rule)");
    // The release re-runs applyRest once per received keepalive - so engine
    // acts scale with SAMPLES, never with TICKS. A flapping classifier fails
    // this by re-entering the per-tick rest path.
    check(L.park <= (unsigned long)plan.size() + 3,
          "T1: parks bounded by keepalive count (applyRest per sample, not per tick)");
    check(ticks > L.park * 4, "T1: tick count dwarfs park count");
    check(L.applyRaw == 0, "T1: no hard snaps on a still body");

    delete r;
    delete in;
}

// ---- T2: release-then-still -------------------------------------------------
// Still NPC, clean 500 ms mid cadence, 5 s. Pins the mid-rest release exactly
// once (one PARKED entry), no re-adopt, and DOCUMENTS current post-release
// behaviour: applyRest re-fires once per received keepalive (clearGoals +
// endAction + park + release again), because the release resets d.parked every
// tick. These per-keepalive counts are a pin of CURRENT behaviour - the
// baseline the future release-restructure must be measured against - not an
// endorsement. (The task brief expected zero parks after the release; the code
// deliberately re-asserts the rest pose per keepalive - see the stillPoseMs
// gate in ReplicatorDrive.cpp - so the pin records what IS.)

static void t2_releaseThenStill() {
    std::printf("\n-- T2 release-then-still (500 ms cadence, zero velocity) --\n");
    faketest::reset();
    drivehooks::reset();
    Replicator* r = mkRepl();
    Inbound* in = new Inbound();
    GameWorld* gw = (GameWorld*)0x10;
    unsigned int H[5] = { 2, 7, 1, 101, 5001 };
    Character* c = faketest::addChar(H, 200.0f, 0.0f, 200.0f, false);

    std::vector<Send> plan;
    for (unsigned int k = 0; k <= 10; ++k) {
        Send s;
        s.t = k * 500;
        s.e = mkState(H, 200.0f, 0.0f, 200.0f, 0.0f, false);
        plan.push_back(s);
    }

    const unsigned long DUR = 5300;
    unsigned long t0 = qnow();
    size_t next = 0;
    unsigned long ticks = 0, releaseAt = 0;
    unsigned long parksAtRelease = 0, endAtRelease = 0;
    size_t sentAtRelease = 0;
    for (;;) {
        unsigned long el = qnow() - t0;
        while (next < plan.size() && plan[next].t <= el) {
            in->pushEntity(0, (u32)(t0 + plan[next].t), plan[next].e);
            ++next;
        }
        r->ingest(*in);
        r->applyTargets(gw);
        ++ticks;
        if (releaseAt == 0 &&
            drivehooks::entries(c, drivehooks::CLS_PARKED) > 0) {
            releaseAt = el;
            parksAtRelease = faketest::led(c).park;
            endAtRelease   = faketest::led(c).endAction;
            sentAtRelease  = next;
        }
        if (el >= DUR) break;
        Sleep(5);
    }

    faketest::CharLedger& L = faketest::led(c);
    unsigned long parksAfter = L.park - parksAtRelease;
    unsigned long endsAfter  = L.endAction - endAtRelease;
    unsigned long samplesAfter =
        (unsigned long)(plan.size() > sentAtRelease ? plan.size() - sentAtRelease
                                                    : 0);
    std::printf("   ticks=%lu samples=%u releaseAt=%lums samplesAfter=%lu "
                "parksAfter=%lu endActionsAfter=%lu clearGoals=%lu "
                "parkedEntries=%lu midEntries=%lu suspendNow=%u\n",
                ticks, (unsigned)next, releaseAt, samplesAfter,
                parksAfter, endsAfter, L.clearGoals,
                drivehooks::entries(c, drivehooks::CLS_PARKED),
                drivehooks::entries(c, drivehooks::CLS_MID),
                faketest::suspendCount());

    check(drivehooks::entries(c, drivehooks::CLS_PARKED) == 1,
          "T2: exactly one mid-rest release (one PARKED entry)");
    check(drivehooks::parkedMidCycles(c) == 0,
          "T2: no re-adopt while samples stay still (zero PARKED<->MID cycles)");
    check(drivehooks::lastClass(c) == drivehooks::CLS_PARKED,
          "T2: body still released at end of run");
    // Pins of CURRENT behaviour (defect-adjacent, documented, not desired):
    // the release re-parks once per keepalive because it resets d.parked every
    // tick, and it leaves the body's AI UNSUSPENDED (the known open two-writer
    // defect: ~100 released bodies ran local AI in the 2026-08-10 session).
    check(parksAfter >= 1,
          "T2 pin(current): re-park fires per keepalive after release (not zero)");
    check(parksAfter <= samplesAfter + 2,
          "T2 pin(current): re-park is per keepalive, never per tick");
    check(endsAfter <= samplesAfter + 2,
          "T2 pin(current): endAction is per keepalive, never per tick");
    check(faketest::suspendCount() == 0,
          "T2 pin(current): released body's AI is NOT suspended (open defect, documented)");

    delete r;
    delete in;
}

// ---- T3: walker-steady ------------------------------------------------------
// A 5 u/s walker on the 500 ms mid cadence, plus a driven SQUAD walker at the
// same pace. Assertion window opens after the second sample has settled
// (1.2 s): from there the drive must keep issuing walk orders and never park,
// endAction, or hard-snap either body - the shuffle-regression tripwire. The
// fake walkTo obeys instantly, so any applyRaw here is the DRIVE deciding to
// teleport, not a lagging path.

static void t3_walkerSteady() {
    std::printf("\n-- T3 walker-steady (500 ms cadence, 5 u/s, NPC + squad) --\n");
    faketest::reset();
    drivehooks::reset();
    Replicator* r = mkRepl();
    Inbound* in = new Inbound();
    GameWorld* gw = (GameWorld*)0x10;
    unsigned int HN[5] = { 2, 7, 1, 102, 5002 };
    unsigned int HS[5] = { 1, 3, 0, 1, 9001 };
    Character* cn = faketest::addChar(HN, 300.0f, 0.0f, 300.0f, false);
    Character* cs = faketest::addChar(HS, 400.0f, 0.0f, 400.0f, true);

    std::vector<Send> plan;
    for (unsigned int k = 0; k <= 10; ++k) {
        Send a, b;
        a.t = k * 500;
        a.e = mkState(HN, 300.0f + 2.5f * k, 0.0f, 300.0f, 5.0f, true);
        plan.push_back(a);
        b.t = k * 500;
        b.e = mkState(HS, 400.0f + 2.5f * k, 0.0f, 400.0f, 5.0f, true);
        plan.push_back(b);
    }

    const unsigned long DUR = 5300;
    const unsigned long WINDOW = 1200;
    unsigned long t0 = qnow();
    size_t next = 0;
    unsigned long ticks = 0;
    bool snapped = false;
    faketest::CharLedger n0, s0; // ledger snapshots at window start
    for (;;) {
        unsigned long el = qnow() - t0;
        while (next < plan.size() && plan[next].t <= el) {
            in->pushEntity(0, (u32)(t0 + plan[next].t), plan[next].e);
            ++next;
        }
        r->ingest(*in);
        r->applyTargets(gw);
        ++ticks;
        if (!snapped && el >= WINDOW) {
            snapped = true;
            n0 = faketest::led(cn);
            s0 = faketest::led(cs);
        }
        if (el >= DUR) break;
        Sleep(5);
    }

    faketest::CharLedger& N = faketest::led(cn);
    faketest::CharLedger& S = faketest::led(cs);
    std::printf("   ticks=%lu samples=%u | npc: walkTo=+%lu park=+%lu end=+%lu "
                "snap=+%lu | squad: walkTo=+%lu motion=+%lu snap=+%lu park=+%lu "
                "suspendNow=%u\n",
                ticks, (unsigned)next,
                N.walkTo - n0.walkTo, N.park - n0.park,
                N.endAction - n0.endAction, N.applyRaw - n0.applyRaw,
                S.walkTo - s0.walkTo, S.applyMotion - s0.applyMotion,
                S.applyRaw - s0.applyRaw, S.park - s0.park,
                faketest::suspendCount());

    check(snapped, "T3: assertion window opened");
    check(N.walkTo - n0.walkTo >= 5,
          "T3: NPC walk orders keep issuing (re-issue as the lead point moves)");
    check(N.park - n0.park == 0, "T3: zero NPC parks while walking");
    check(N.endAction - n0.endAction == 0, "T3: zero NPC endActions while walking");
    check(N.applyRaw - n0.applyRaw == 0, "T3: zero NPC hard snaps (steady walker)");
    check(S.walkTo - s0.walkTo >= 5, "T3: squad walk orders keep issuing");
    check(S.applyMotion - s0.applyMotion > 0,
          "T3: squad locomotion mirror fires (applyMotion)");
    check(S.applyRaw - s0.applyRaw == 0, "T3: zero squad hard snaps");
    check(S.park - s0.park == 0, "T3: zero squad parks while walking");
    check(faketest::suspendCount() >= 2,
          "T3: both driven walkers AI-suspended this tick");

    // ---- T4: a DOWN transition is read from the NEWEST sample --------------
    // EntityInterp::sample copies the last received state wholesale and then
    // overwrites only x/y/z/heading, so out.bodyState lags by the render delay.
    // The drive's down test used to read exactly that, so a knockout landed a
    // render delay late - measured delay=748 ms on a live join, reported as
    // "enemies with 0 blood are staying up too long".
    //
    // The FIRST attempt at this pin passed with the fix reverted, i.e. proved
    // nothing, because a fast cadence keeps renderDelay tiny and out.bodyState
    // == latest().bodyState. renderDelay is avgInterval + 2*jitter + lag with a
    // ceiling that SCALES on avgInterval (Interp.cpp), so the delay is forced
    // here by feeding a deliberately SLOW mid-band-like cadence - and the test
    // asserts the delay actually got large before trusting its own conclusion.
    // A pin that cannot fail is worse than no pin.
    {
        faketest::reset();
        static const unsigned int HD[5] = { 1u, 77u, 4242u, 1u, 990099u };
        Character* cd = faketest::addChar(HD, 500.0f, 0.0f, 500.0f, false);
        const unsigned long t0d = qnow();
        const unsigned long STEP = 400;   // mid-band-ish cadence
        unsigned int pushed = 0;
        bool sentDown = false;
        faketest::CharLedger beforeDown;
        std::memset(&beforeDown, 0, sizeof(beforeDown));
        unsigned long delaySeen = 0;
        for (;;) {
            const unsigned long el = qnow() - t0d;
            while (!sentDown && pushed < 9 && (pushed * STEP) <= el) {
                EntityState e = mkState(HD, 500.0f + 4.0f * pushed, 0.0f, 500.0f,
                                        10.0f, true);
                in->pushEntity(0, (u32)(t0d + pushed * STEP), e);
                ++pushed;
            }
            if (!sentDown && pushed >= 9 && el >= 9 * STEP) {
                delaySeen = r->debugInterpDelayMs(HD);
                beforeDown = faketest::led(cd);
                EntityState down = mkState(HD, 500.0f + 4.0f * 9, 0.0f, 500.0f,
                                           0.0f, false);
                down.bodyState = (unsigned short)BODY_DOWN;
                in->pushEntity(0, (u32)(t0d + 9 * STEP), down);
                sentDown = true;
            }
            r->ingest(*in);
            r->applyTargets(gw);
            if (sentDown) break;          // assert on the FIRST tick after DOWN
            if (el > 8000) break;
        }
        const faketest::CharLedger afterDown = faketest::led(cd);
        // Guard the guard: without a large delay this test is vacuous.
        check(delaySeen >= 250,
              "T4: the slow cadence actually forced a large render delay");
        check((afterDown.knockDown + afterDown.holdDown) >
              (beforeDown.knockDown + beforeDown.holdDown),
              "T4: a DOWN sample collapses the copy on the tick it arrives");
    }

    // ---- T4 note -----------------------------------------------------------
    // A pin for "the DOWN transition is read from interp.latest(), not from the
    // interpolated sample" was written here and then REMOVED, deliberately.
    //
    // It passed with the fix AND with the fix reverted, i.e. it proved nothing.
    // The reason is a real limit of this harness: EntityInterp::sample falls
    // back to the newest snapshot whenever the ring is short, so out.bodyState
    // and latest().bodyState are the same value here. The defect only appears
    // once the adaptive render delay has grown large (measured 748 ms on a live
    // join), and nothing in this harness drives the delay up.
    //
    // Keeping a green test that cannot fail would be worse than having none -
    // it is the "a green gate is not a launched game" trap this project is
    // named after. To pin it, the harness first needs a way to force a large
    // render delay (feed a jittery cadence until interp reports one, then
    // assert against it). Recorded rather than faked.

    delete r;
    delete in;
}

// ---- main -------------------------------------------------------------------

int main() {
    std::printf("drivetest: REAL ReplicatorDrive/Core against a fake engine\n"
                "(tier hysteresis, mid-rest release, walk-hold - no game)\n");
    coop::logInit("drivetest.log", "TEST");

    t1_tierHysteresis();
    t2_releaseThenStill();
    t3_walkerSteady();

    std::printf("\ndrivetest: %d/%d checks passed - %s\n",
                g_pass, g_pass + g_fail, g_fail == 0 ? "PASS" : "FAIL");
    coop::logClose();
    return g_fail;
}
