// EngineFakes.cpp - the fake coop::engine drivetest links instead of the real
// adapter (game/Engine*.cpp). Implements exactly the surface the compiled TUs
// (ReplicatorDrive.cpp + ReplicatorCore.cpp) reference; iterate on LNK2019 to
// grow it. Every fake either records into the CharLedger or is a safe inert
// default, and each carries a comment saying why that default is safe for
// receiver-drive tests.
//
// Defaults at a glance (the "quiet world" contract):
//   * bodies are upright, un-carried, un-caged, un-chained, not sneaking, not
//     prone, not fighting - so applyTargets falls straight through the
//     carve-outs to the locomotion/rest drive, which is what this slice tests;
//   * reads that mean "could not read" return their documented failure value
//     (false / -1), which every drive call site treats as "skip the branch";
//   * walkTo obeys instantly (teleports to the destination) so the snap gates
//     measure the DRIVE's decisions, not a fake pathfinder's lag.

#include "../plugin/game/EngineSync.h"
#include "EngineFakes.h"

#include <map>
#include <set>
#include <cstring>
#include <cstdio>

namespace coop {
namespace faketest {

CharLedger::CharLedger()
    : x(0), y(0), z(0), heading(0), isSquad(false),
      motMoving(false), motSpeed(0.0f),
      walkTo(0), park(0), endAction(0), clearGoals(0), applyRaw(0),
      applyMotion(0), applyPhysMotion(0), applyTaskOrder(0),
      knockDown(0), holdDown(0), aiSuspendAdd(0), dmgGuardAdd(0),
      detach(0), applyFurniture(0), applyPickup(0), applyDrop(0),
      applyStealth(0), applyProne(0), applyCombat(0), forceAttack(0),
      lastWalkX(0), lastWalkY(0), lastWalkZ(0), lastWalkSpeed(0) {
    hand[0] = hand[1] = hand[2] = hand[3] = hand[4] = 0;
}

namespace {

// Registered fake world. Keys mirror engine::resolve's identity: the 5-field
// hand. Character* handles are minted from a counter - they are NEVER
// dereferenced (the real drive passes them to engine calls only, which is a
// property this whole harness exists to keep true).
struct HandKey {
    unsigned int h[5];
    bool operator<(const HandKey& o) const {
        for (int i = 0; i < 5; ++i)
            if (h[i] != o.h[i]) return h[i] < o.h[i];
        return false;
    }
};

std::map<HandKey, Character*>     g_byHand;
std::map<Character*, CharLedger>  g_led;
std::set<Character*>              g_suspend;
std::set<Character*>              g_guard;
unsigned long                     g_nextHandle = 0x1000;

} // namespace

void reset() {
    g_byHand.clear();
    g_led.clear();
    g_suspend.clear();
    g_guard.clear();
    g_nextHandle = 0x1000;
}

Character* addChar(const unsigned int hand[5], float x, float y, float z,
                   bool isSquad) {
    Character* c = (Character*)(g_nextHandle);
    g_nextHandle += 0x10;
    HandKey k;
    for (int i = 0; i < 5; ++i) k.h[i] = hand[i];
    g_byHand[k] = c;
    CharLedger& l = g_led[c];
    for (int i = 0; i < 5; ++i) l.hand[i] = hand[i];
    l.x = x; l.y = y; l.z = z; l.heading = 0.0f;
    l.isSquad = isSquad;
    return c;
}

CharLedger& led(Character* c) { return g_led[c]; }

unsigned int suspendCount() { return (unsigned int)g_suspend.size(); }

} // namespace faketest

namespace engine {

using faketest::CharLedger;

namespace {
CharLedger* rec(Character* c) {
    if (!c) return 0;
    return &faketest::led(c);
}
} // namespace

// ---- resolve / identity ----------------------------------------------------

// The real resolve walks the engine's object tables; the fake answers from the
// registry. An unregistered hand returns 0, which the drive treats as an
// unresolved (unloaded) body - useful later for mint-path tests.
Character* resolve(const EntityState& e) {
    faketest::HandKey k;
    k.h[0] = e.hType; k.h[1] = e.hContainer; k.h[2] = e.hContainerSerial;
    k.h[3] = e.hIndex; k.h[4] = e.hSerial;
    std::map<faketest::HandKey, Character*>::iterator it =
        faketest::g_byHand.find(k);
    return it == faketest::g_byHand.end() ? 0 : it->second;
}

Character* resolveCharByHand(unsigned int idx, unsigned int ser,
                             unsigned int type, unsigned int cont,
                             unsigned int contSer) {
    faketest::HandKey k;
    k.h[0] = type; k.h[1] = cont; k.h[2] = contSer; k.h[3] = idx; k.h[4] = ser;
    std::map<faketest::HandKey, Character*>::iterator it =
        faketest::g_byHand.find(k);
    return it == faketest::g_byHand.end() ? 0 : it->second;
}

bool readHand(Character* c, unsigned int out[5]) {
    CharLedger* l = rec(c);
    if (!l) return false;
    for (int i = 0; i < 5; ++i) out[i] = l->hand[i];
    return true;
}

bool readPos(Character* c, float* x, float* y, float* z) {
    CharLedger* l = rec(c);
    if (!l) return false;
    if (x) *x = l->x;
    if (y) *y = l->y;
    if (z) *z = l->z;
    return true;
}

// Squad classification comes straight off the registration flag: the drive
// only ever uses it to pick the PC-vs-NPC policy fork.
bool isLocalPlayerChar(GameWorld* gw, Character* c) {
    (void)gw;
    CharLedger* l = rec(c);
    return l ? l->isSquad : false;
}

// No own squad in the fake world: the drive uses this only to scope its
// smoothness oracles to bodies near the local squad, and with n=0 that scoping
// is skipped - the oracles still run, they just don't distance-filter.
unsigned int captureSquad(GameWorld* gw, bool leaderOnly,
                          EntityState* out, unsigned int maxOut) {
    (void)gw; (void)leaderOnly; (void)out; (void)maxOut;
    return 0;
}

// reportCombat_ is off in every test, so this is never reached; 0 keeps the
// attacker-set rebuild a no-op if a test ever flips it on.
unsigned int listPlayerChars(GameWorld* gw, Character** out,
                             unsigned int maxOut) {
    (void)gw; (void)out; (void)maxOut;
    return 0;
}

void charName(Character* c, char* out, unsigned int cap) {
    CharLedger* l = rec(c);
    if (!out || cap == 0) return;
    if (l) _snprintf(out, cap - 1, "fake-%u,%u", l->hand[3], l->hand[4]);
    else   _snprintf(out, cap - 1, "fake-null");
    out[cap - 1] = '\0';
}

// ---- locomotion ------------------------------------------------------------

// A perfectly obedient engine: the body arrives at the ordered destination
// immediately and reports a live walk motion. See EngineFakes.h for why.
bool walkTo(Character* c, float x, float y, float z, float speed) {
    CharLedger* l = rec(c);
    if (!l) return false;
    ++l->walkTo;
    l->lastWalkX = x; l->lastWalkY = y; l->lastWalkZ = z; l->lastWalkSpeed = speed;
    l->x = x; l->y = y; l->z = z;
    l->motMoving = true; l->motSpeed = speed;
    return true;
}

bool park(Character* c, float x, float y, float z, float heading) {
    CharLedger* l = rec(c);
    if (!l) return false;
    ++l->park;
    l->x = x; l->y = y; l->z = z; l->heading = heading;
    l->motMoving = false; l->motSpeed = 0.0f;
    return true;
}

bool applyRaw(Character* c, const EntityState& e) {
    CharLedger* l = rec(c);
    if (!l) return false;
    ++l->applyRaw;
    l->x = e.x; l->y = e.y; l->z = e.z; l->heading = e.heading;
    return true;
}

bool applyMotion(Character* c, bool moving, float speed,
                 float mx, float my, float mz) {
    (void)mx; (void)my; (void)mz;
    CharLedger* l = rec(c);
    if (!l) return false;
    ++l->applyMotion;
    l->motMoving = moving; l->motSpeed = speed;
    return true;
}

bool readMotion(Character* c, bool* moving, float* speed) {
    CharLedger* l = rec(c);
    if (!l) return false;
    if (moving) *moving = l->motMoving;
    if (speed)  *speed  = l->motSpeed;
    return true;
}

// endAction drops the residual walk: mirror that so applyRest's relapse
// re-quiet (which reads motion right back) sees a quieted body.
bool endAction(Character* c) {
    CharLedger* l = rec(c);
    if (!l) return false;
    ++l->endAction;
    l->motMoving = false; l->motSpeed = 0.0f;
    return true;
}

void clearGoals(Character* c) {
    CharLedger* l = rec(c);
    if (l) ++l->clearGoals;
}

bool applyPhysMotion(Character* c, float dirX, float dirY, float dirZ,
                     float speed) {
    (void)dirX; (void)dirY; (void)dirZ; (void)speed;
    CharLedger* l = rec(c);
    if (!l) return false;
    ++l->applyPhysMotion;
    return true;
}

// Every fake body has a live physics character, so the crawl-restore branch
// never fires (no crawlers in this slice's inputs anyway).
bool hasPhysicsBody(Character* c) { (void)c; return true; }
bool restoreMovement(Character* c) { (void)c; return false; }

// ---- body state / posture reads -------------------------------------------

// Upright and healthy: the down/KO override, crawl carve-out and death veto
// all stay quiet, which is the regime the locomotion tests need.
unsigned short readBodyState(Character* c) { (void)c; return 0; }

// -1 = "could not read", which the sneak/prone apply paths treat as skip.
int readStealthMode(Character* c) { (void)c; return -1; }
int readProneState(Character* c) { (void)c; return -1; }
int readSlaveState(Character* c) { (void)c; return -1; }

// Agrees with the tests' rawTask (TASK_NONE) so the [gate] divergence log
// stays quiet; gateAuthority_ is off, so nothing else consumes it.
int readTaskKey(Character* c) { (void)c; return (int)0xFFFF; }

bool taskIsBedPose(int t) { (void)t; return false; }
bool isBuildSiteTask(int taskKey) { (void)taskKey; return false; }

// ---- combat ---------------------------------------------------------------

// Nobody fights in the fake world: valid=false makes every combat read fail,
// so logHardSnap's in-combat census and the disarm paths see a peaceful body.
bool readCombat(Character* c, CombatRead* out) {
    (void)c;
    if (out) std::memset(out, 0, sizeof(*out));
    return false;
}

int applyCombat(Character* c, const EntityState& e, bool breakOrder) {
    (void)e; (void)breakOrder;
    CharLedger* l = rec(c);
    if (l) ++l->applyCombat;
    return 0; // no-op: tests never stream a combat stance in this slice
}

int forceAttack(Character* c, const EntityState& e) {
    (void)e;
    CharLedger* l = rec(c);
    if (l) ++l->forceAttack;
    return 0;
}

// ---- carry / furniture / shackle ------------------------------------------

// Nothing is carried or caged: the carve-outs fall through to the drive.
bool readCarry(Character* c, CarryRead* out) {
    (void)c;
    if (out) std::memset(out, 0, sizeof(*out));
    return false;
}

bool applyPickup(GameWorld* gw, Character* carrier,
                 const unsigned int carriedHand[5]) {
    (void)gw; (void)carriedHand;
    CharLedger* l = rec(carrier);
    if (l) ++l->applyPickup;
    return false;
}

bool applyDrop(Character* carrier, bool ragdoll) {
    (void)ragdoll;
    CharLedger* l = rec(carrier);
    if (l) ++l->applyDrop;
    return false;
}

bool readFurniture(Character* c, FurnitureRead* out) {
    (void)c;
    if (out) std::memset(out, 0, sizeof(*out));
    return false;
}

bool readShackle(Character* c, ShackleRead* out) {
    (void)c;
    if (out) std::memset(out, 0, sizeof(*out));
    return false;
}

bool applyFurniture(GameWorld* gw, Character* occupant,
                    const unsigned int furnHand[5], int kind, bool on) {
    (void)gw; (void)furnHand; (void)kind; (void)on;
    CharLedger* l = rec(occupant);
    if (l) ++l->applyFurniture;
    return false;
}

bool enterFurnitureNearPos(GameWorld* gw, Character* occupant, int kind,
                           float x, float y, float z, float radius) {
    (void)gw; (void)kind; (void)x; (void)y; (void)z; (void)radius;
    CharLedger* l = rec(occupant);
    if (l) ++l->applyFurniture;
    return false;
}

// ---- stealth / prone applies ----------------------------------------------

bool applyStealth(Character* c, bool on) {
    (void)on;
    CharLedger* l = rec(c);
    if (l) ++l->applyStealth;
    return true;
}

bool applyProneState(Character* c, int p) {
    (void)p;
    CharLedger* l = rec(c);
    if (l) ++l->applyProne;
    return true;
}

// ---- down / death ----------------------------------------------------------

bool knockDown(Character* c, bool on) {
    (void)on;
    CharLedger* l = rec(c);
    if (l) ++l->knockDown;
    return true;
}

bool holdDown(Character* c) {
    CharLedger* l = rec(c);
    if (l) ++l->holdDown;
    return true;
}

// The fake body is never locally dead (readBodyState 0), so the veto is
// unreachable; false = "nothing vetoed" if it ever is.
bool vetoLocalDeath(Character* c) { (void)c; return false; }

// ---- AI suspend / damage guard --------------------------------------------

void clearAiSuspend() { faketest::g_suspend.clear(); }

void addAiSuspend(Character* c) {
    if (!c) return;
    faketest::g_suspend.insert(c);
    CharLedger* l = rec(c);
    if (l) ++l->aiSuspendAdd;
}

unsigned int aiSuspendCount() {
    return (unsigned int)faketest::g_suspend.size();
}

void clearDamageGuard() { faketest::g_guard.clear(); }

void addDamageGuard(Character* c) {
    if (!c) return;
    faketest::g_guard.insert(c);
    CharLedger* l = rec(c);
    if (l) ++l->dmgGuardAdd;
}

void damageGuardStats(unsigned long* outGuarded, unsigned long* outPassed) {
    if (outGuarded) *outGuarded = 0;
    if (outPassed)  *outPassed  = 0;
}

// ---- join-dealt damage report (protocol 45) --------------------------------
// reportCombat_ is off in every test; inert no-ops keep the symbols resolvable.

void setCombatReport(bool on) { (void)on; }
void clearReportAttackers() {}
void addReportAttacker(Character* c) { (void)c; }

bool takeReportedDamage(Character* c, float* outFlesh, float* outBlood) {
    (void)c;
    if (outFlesh) *outFlesh = 0.0f;
    if (outBlood) *outBlood = 0.0f;
    return false;
}

// ---- pose / task -----------------------------------------------------------

// 0 = "leave unapplied this frame": applyRest then falls through to the park,
// which is the pose-less regime these tests stream (task always TASK_NONE).
int applyTaskOrder(Character* c, const EntityState& e) {
    (void)e;
    CharLedger* l = rec(c);
    if (l) ++l->applyTaskOrder;
    return 0;
}

bool detachFromTownAI(Character* c) {
    CharLedger* l = rec(c);
    if (l) ++l->detach;
    return true; // latches d.detached so it fires at most once per body
}

bool recruitNpc(GameWorld* gw, Character* c) {
    (void)gw; (void)c;
    return false; // probeRecruit_ is off in every test
}

// ---- Core-side lifecycle helpers ------------------------------------------
// Referenced by ReplicatorCore.cpp (lifeSweep / resetSession /
// clearPeerReplicationState / logSmoothSummary). The tests never build the
// state that would make these matter; inert defaults keep those paths safe if
// a test calls them.

bool isZoneLoadedAt(GameWorld* gw, float x, float y, float z) {
    (void)gw; (void)x; (void)y; (void)z;
    return true;
}

void markerDestroy(void* label) { (void)label; }
void clearSquadRoster() {}

bool despawnProxyNpc(GameWorld* gw, Character* proxy) {
    (void)gw; (void)proxy;
    return false;
}

bool removeWorldItemProxy(GameWorld* gw, RootObject* proxy) {
    (void)gw; (void)proxy;
    return false;
}

bool restoreNpc(GameWorld* gw, Character* c) {
    (void)gw; (void)c;
    return false;
}

} // namespace engine
} // namespace coop
