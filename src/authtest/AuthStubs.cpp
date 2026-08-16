// AuthStubs.cpp - the cross-TU Replicator members authoritytest must satisfy
// that live in sync .cpps this harness deliberately does NOT link
// (ReplicatorItems.cpp, ReplicatorChannels.cpp, ReplicatorSpawn.cpp).
//
// drivetest's ReplicatorStubs.cpp is NOT linked here: it stubs debugMark,
// whose real definition arrives with ReplicatorAuthority.cpp. These are the
// remainder, each stubbed in the safe direction.
//
// C++03 / VC10.

#include "../plugin/sync/Replicator.h"

namespace coop {

// Real home: ReplicatorChannels.cpp (protocol 27/28 building-key translation).
// false = "not a session-placed building", so publish paths treat every hand
// as a plain runtime hand - the harness worlds never place buildings.
bool Replicator::buildKeyForLocalHand(const Key& local, Key& outWire) const {
    (void)local; (void)outWire;
    return false;
}

// Real home: ReplicatorItems.cpp. Same stub + reasoning as drivetest's.
bool Replicator::localHandForBuildKey(const Key& wire, unsigned int out[5]) const {
    (void)wire; (void)out;
    return false;
}

RootObject* Replicator::liveWorldProxy(const WorldProxy& wp) {
    (void)wp;
    return 0;
}

} // namespace coop
