// NetLinkFakes.cpp - link-time substitution of the NetLink members the
// publish/authority paths call (the ReplicatorStubs.cpp trick, applied to
// NetLink). The real NetLink.cpp is NOT linked; these definitions satisfy the
// same symbols and record into the netfake ledger instead of queueing for a
// network thread that does not exist here.
//
// The fake constructor initialises NOTHING. That is deliberate and safe: the
// only NetLink code that runs in this harness is the functions below, none of
// which touch a member - every real member (queues, ENet host, critical
// sections) stays untouched garbage, and the empty destructor leaves it that
// way. If a newly-linked Replicator path ever calls a NetLink member that is
// not defined here, the build fails with an unresolved external - which is
// the correct failure: it names the exact surface the fake must grow.
//
// C++03 / VC10.

#include "../plugin/net/NetLink.h"
#include "NetLinkFakes.h"

#include <cstring>

namespace coop {
namespace netfake {

std::vector<EntityState> lastOwned;
unsigned long            ownedCalls = 0;
std::vector<u32>         lastCensusHands;
unsigned int             lastCensusCount = 0;
bool                     lastCensusTrunc = false;
std::vector<u8>          lastCensusAuthors;
u32                      lastCensusMapGen = 0;
unsigned long            censusCalls = 0;
std::vector<AuthAssignPacket> assigns;
std::vector<EventPacket> events;
unsigned long            cellClaims = 0;
unsigned long            camHints = 0;

void reset() {
    lastOwned.clear();
    ownedCalls = 0;
    lastCensusHands.clear();
    lastCensusCount = 0;
    lastCensusTrunc = false;
    lastCensusAuthors.clear();
    lastCensusMapGen = 0;
    censusCalls = 0;
    assigns.clear();
    events.clear();
    cellClaims = 0;
    camHints = 0;
}

} // namespace netfake

// ---- substituted NetLink members -------------------------------------------

NetLink::NetLink() {}
NetLink::~NetLink() {}

void NetLink::setOwnedEntities(u32 ownerId, const EntityState* arr,
                               unsigned int count) {
    (void)ownerId;
    netfake::lastOwned.clear();
    if (arr && count > 0)
        netfake::lastOwned.assign(arr, arr + count);
    ++netfake::ownedCalls;
}

void NetLink::queueNpcCensus(u32 ownerId, const u32* hands, const float* pos,
                             unsigned int count, bool truncated,
                             const u8* authors, u32 mapGen) {
    (void)ownerId; (void)pos;
    netfake::lastCensusHands.clear();
    if (hands && count > 0)
        netfake::lastCensusHands.assign(hands, hands + count * 5);
    netfake::lastCensusCount = count;
    netfake::lastCensusTrunc = truncated;
    netfake::lastCensusAuthors.clear();
    if (authors && count > 0)
        netfake::lastCensusAuthors.assign(authors, authors + count);
    netfake::lastCensusMapGen = mapGen;
    ++netfake::censusCalls;
}

void NetLink::queueAuthAssign(const AuthAssignPacket& p) {
    netfake::assigns.push_back(p);
}

void NetLink::queueEvent(const EventPacket& ev) {
    netfake::events.push_back(ev);
}

void NetLink::queueCellClaim(const CellClaimPacket& p) {
    (void)p;
    ++netfake::cellClaims;
}

void NetLink::queueCamHint(const CamHintPacket& p) {
    (void)p;
    ++netfake::camHints;
}

} // namespace coop
