// DriveTestHooks.h - the drivetest classification ledger fed by the stubbed
// Replicator::debugMark (see ReplicatorStubs.cpp).
//
// applyTargets reports every per-tick classification through debugMark:
//   * "DRV"    - body driven this tick (pre-classification; ignored here)
//   * "PARKED" - the mid-rest release handed the body to local AI
//   * "MID"    - driven at mid-tier cadence
//   * "HI"     - driven at near-tier cadence
// The real debugMark paints HUD labels behind an env flag; the stub records
// the same edges into this ledger instead, which is what lets a headless test
// assert on tier journeys without reaching into Replicator's private life_ map.

#ifndef KENSHICOOP_DRIVETEST_HOOKS_H
#define KENSHICOOP_DRIVETEST_HOOKS_H

class Character;

namespace coop {
namespace drivehooks {

// Classification classes (the tags above, minus the ignored DRV).
enum { CLS_NONE = 0, CLS_PARKED = 1, CLS_MID = 2, CLS_HI = 3 };

void reset();

// Number of ENTRIES INTO a class for this body (a transition edge, not a
// per-tick count - staying PARKED for a minute is one entry).
unsigned long entries(Character* c, int cls);

// PARKED->MID plus MID->PARKED transition edges for this body: the tier-flap
// signature the v0.58 hysteresis exists to eliminate.
unsigned long parkedMidCycles(Character* c);

// The class this body was last reported in (CLS_NONE if never classified).
int lastClass(Character* c);

} // namespace drivehooks
} // namespace coop

#endif // KENSHICOOP_DRIVETEST_HOOKS_H
