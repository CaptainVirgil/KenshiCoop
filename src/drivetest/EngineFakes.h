// EngineFakes.h - the drivetest fake-engine ledger (ROADMAP Phase 2 item 27,
// first slice).
//
// drivetest compiles the REAL ReplicatorDrive.cpp / ReplicatorCore.cpp against
// a FAKE coop::engine (EngineFakes.cpp). The fakes never touch a game: every
// Character* is an opaque handle minted here, and every engine call the drive
// makes is RECORDED into a per-character ledger so tests can assert "this body
// was parked N times / walk-ordered to X / never teleported".
//
// The one behavioural choice: fake walkTo() teleports the body to the ordered
// destination (a perfectly obedient engine). That keeps a driven walker's gap
// near zero so the hard-snap gates stay quiet in the steady-walk test, which is
// exactly the regime that test pins. Tests that WANT a disobedient engine can
// grow a knob here later; the first slice does not need one.
//
// C++03 / VC10, no game, no KenshiLib, no windows dependencies of its own.

#ifndef KENSHICOOP_DRIVETEST_ENGINEFAKES_H
#define KENSHICOOP_DRIVETEST_ENGINEFAKES_H

class Character;

namespace coop {
namespace faketest {

// Per-character call ledger + fake world state. Counters are cumulative since
// the last reset(); tests snapshot and diff them around their assertion window.
struct CharLedger {
    unsigned int hand[5];      // registered identity (readObjectHand layout)
    float x, y, z, heading;    // fake world position (what readPos serves)
    bool  isSquad;             // what isLocalPlayerChar reports
    bool  motMoving;           // what readMotion serves back
    float motSpeed;

    unsigned long walkTo, park, endAction, clearGoals, applyRaw;
    unsigned long applyMotion, applyPhysMotion, applyTaskOrder;
    unsigned long knockDown, holdDown, aiSuspendAdd, dmgGuardAdd;
    unsigned long detach, applyFurniture, applyPickup, applyDrop;
    unsigned long applyStealth, applyProne, applyCombat, forceAttack;

    float lastWalkX, lastWalkY, lastWalkZ, lastWalkSpeed;

    CharLedger();
};

// Drop every registered body, ledger and suspend/guard set.
void reset();

// Mint + register a fake body at (x,y,z). The returned opaque Character* is
// what engine::resolve() answers for this hand from now on.
Character* addChar(const unsigned int hand[5], float x, float y, float z,
                   bool isSquad);

// The ledger for a body (asserts never fault: an unknown pointer gets a
// zeroed record).
CharLedger& led(Character* c);

// Size of the fake AI-suspend set right now (mirrors engine::aiSuspendCount).
unsigned int suspendCount();

} // namespace faketest
} // namespace coop

#endif // KENSHICOOP_DRIVETEST_ENGINEFAKES_H
