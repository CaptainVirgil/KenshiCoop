// ReplicatorStubs.cpp - the Replicator member functions the linker demands
// that live in Replicator TUs drivetest does NOT compile (Authority, Channels,
// Items, Publish, Spawn). Member definitions may live in any TU, so each stub
// is defined here with a note on why a no-op (or a recording shim) is safe for
// receiver-drive tests.
//
// drivetest compiles ReplicatorDrive.cpp + ReplicatorCore.cpp for real; the
// class's other ~90 members are simply never CALLED by those two TUs, so only
// the three below need to exist at all. If a future slice compiles more real
// TUs, delete the matching stub here - a duplicate definition is a hard LNK2005,
// so the build itself polices the seam.

#include "../plugin/sync/ReplicatorUtil.h"
#include "DriveTestHooks.h"

#include <map>

namespace coop {

// ---- drivehooks: the classification ledger fed by the debugMark stub -------

namespace drivehooks {
namespace {

struct Rec {
    int           cls;                 // last classification (CLS_*)
    unsigned long entered[4];          // entries into CLS_NONE..CLS_HI
    unsigned long parkedMid;           // PARKED<->MID transition edges
    Rec() : cls(CLS_NONE), parkedMid(0) {
        entered[0] = entered[1] = entered[2] = entered[3] = 0;
    }
};

std::map<Character*, Rec> g_rec;

int classify(const char* tag) {
    if (!tag) return CLS_NONE;
    if (tag[0] == 'P') return CLS_PARKED; // "PARKED"
    if (tag[0] == 'M') return CLS_MID;    // "MID"
    if (tag[0] == 'H') return CLS_HI;     // "HI"
    return CLS_NONE;                      // "DRV", "UNKNOWN", ... - ignored
}

} // namespace

void reset() { g_rec.clear(); }

unsigned long entries(Character* c, int cls) {
    std::map<Character*, Rec>::iterator it = g_rec.find(c);
    if (it == g_rec.end() || cls < 0 || cls > 3) return 0;
    return it->second.entered[cls];
}

unsigned long parkedMidCycles(Character* c) {
    std::map<Character*, Rec>::iterator it = g_rec.find(c);
    return it == g_rec.end() ? 0 : it->second.parkedMid;
}

int lastClass(Character* c) {
    std::map<Character*, Rec>::iterator it = g_rec.find(c);
    return it == g_rec.end() ? CLS_NONE : it->second.cls;
}

} // namespace drivehooks

// ---- Replicator member stubs ----------------------------------------------

// Real home: ReplicatorAuthority.cpp. The real one paints a HUD label per body
// behind KENSHICOOP_DEBUG_MARKERS (single env check, else no-op) - pure
// diagnostics, zero drive-behaviour impact, so replacing it cannot change what
// the tests measure. The drive calls it at every classification point with the
// life-state tag, which makes it the perfect headless observation seam: record
// the class transitions into the drivehooks ledger instead of painting them.
void Replicator::debugMark(Character* c, int colorId, const char* tag) {
    (void)colorId;
    int cls = drivehooks::classify(tag);
    if (cls == drivehooks::CLS_NONE) return; // "DRV" pre-tag and friends
    drivehooks::Rec& r = drivehooks::g_rec[c];
    if (r.cls == cls) return;
    ++r.entered[cls];
    if ((r.cls == drivehooks::CLS_PARKED && cls == drivehooks::CLS_MID) ||
        (r.cls == drivehooks::CLS_MID && cls == drivehooks::CLS_PARKED))
        ++r.parkedMid;
    r.cls = cls;
}

// Real home: ReplicatorChannels.cpp (protocol 27/28 build-map translation).
// applyRest consults it only when the streamed task is a BUILD-SITE pose
// (engine::isBuildSiteTask), and the fake engine answers false for every task,
// so this is unreachable in the current tests. "false" is also the real
// answer for a world with no session-placed buildings: the pose is retried,
// never faulted.
bool Replicator::localHandForBuildKey(const Key& wire, unsigned int out[5]) const {
    (void)wire; (void)out;
    return false;
}

// Real home: ReplicatorItems.cpp (world-item proxy liveness gate). Only
// clearPeerReplicationState in ReplicatorCore.cpp consults it, and only over
// worldProxies_ - a map nothing in these tests ever fills. 0 = "object gone",
// the safe direction: the caller then drops its mapping and touches nothing.
RootObject* Replicator::liveWorldProxy(const WorldProxy& wp) {
    (void)wp;
    return 0;
}

} // namespace coop
