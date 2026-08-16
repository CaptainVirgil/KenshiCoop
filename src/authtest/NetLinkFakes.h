// NetLinkFakes.h - the authoritytest NetLink ledger.
//
// The harness links ReplicatorPublish.cpp, which hands its output to a
// NetLink&. Instead of linking the real NetLink.cpp (ENet, Steam, a thread),
// NetLinkFakes.cpp substitutes the handful of NetLink member functions the
// publish path calls, and each records here - so a pin can read exactly what
// was published without parsing logs.
//
// C++03 / VC10.

#ifndef KENSHICOOP_AUTHTEST_NETLINKFAKES_H
#define KENSHICOOP_AUTHTEST_NETLINKFAKES_H

#include "../netproto/Wire.h"
#include <vector>

namespace coop {
namespace netfake {

// The most recent setOwnedEntities payload, plus cumulative call count.
extern std::vector<EntityState> lastOwned;
extern unsigned long            ownedCalls;

// The most recent census: 5-u32 hands back to back, count, truncated flag,
// plus the protocol-59 author tail and map generation.
extern std::vector<u32>         lastCensusHands;
extern unsigned int             lastCensusCount;
extern bool                     lastCensusTrunc;
extern std::vector<u8>          lastCensusAuthors;
extern u32                      lastCensusMapGen;
extern unsigned long            censusCalls;

// Reliable authority assertions, in order (protocol 59).
extern std::vector<AuthAssignPacket> assigns;

// Reliable one-shot events, in order.
extern std::vector<EventPacket> events;

extern unsigned long cellClaims;
extern unsigned long camHints;

void reset();

} // namespace netfake
} // namespace coop

#endif // KENSHICOOP_AUTHTEST_NETLINKFAKES_H
