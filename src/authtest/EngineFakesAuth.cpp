// EngineFakesAuth.cpp - the AUTHORITY/PUBLISH slice of the fake engine
// (authoritytest, auth step 1). Defines only the engine functions that
// ReplicatorAuthority.cpp / ReplicatorPublish.cpp need and EngineFakes.cpp
// does not already provide; a duplicate definition is a link error by design,
// so the two files cannot drift into shadowing each other.
//
// Same conventions as EngineFakes.cpp: Character* is an opaque handle, every
// mutating call is recorded into the per-character ledger, and the fakes are
// deliberately obedient (suppress always lands, enumeration is total) so pins
// read cause->effect without an engine arguing back.
//
// C++03 / VC10. No game, no KenshiLib.

#include "../plugin/game/EngineSync.h"
#include "../drivetest/EngineFakes.h"

#include <cstring>
#include <cstdio>
#include <cmath>

namespace coop {
namespace engine {

namespace {

using faketest::CharLedger;

CharLedger* rec(Character* c) {
    if (!c) return 0;
    return &faketest::led(c);
}

// Fill an EntityState from a ledger record, the way the real enumerators do.
void fillState(const CharLedger& l, EntityState* out) {
    std::memset(out, 0, sizeof(*out));
    out->hType = l.hand[0]; out->hContainer = l.hand[1];
    out->hContainerSerial = l.hand[2];
    out->hIndex = l.hand[3]; out->hSerial = l.hand[4];
    out->x = l.x; out->y = l.y; out->z = l.z; out->heading = l.heading;
    out->cSpeed = l.motSpeed;
    out->cMoving = l.motMoving ? 1 : 0;
    out->task = TASK_NONE;
    out->rawTask = TASK_NONE;
    out->bodyState = 0;
}

// Enumerate registry bodies matching (squad?) into the out arrays. Suppressed
// bodies stay enumerable: the real engine still lists a hidden body - being
// invisible is not being gone - and the authority sweep depends on seeing it
// to restore it.
unsigned int enumerate(bool squad, Character** outChars, EntityState* outStates,
                       unsigned int maxOut, bool* outTruncated) {
    static Character* all[2048];
    unsigned int total = faketest::allChars(all, 2048);
    unsigned int n = 0;
    bool trunc = false;
    for (unsigned int i = 0; i < total; ++i) {
        CharLedger& l = faketest::led(all[i]);
        if (l.isSquad != squad) continue;
        if (n >= maxOut) { trunc = true; break; }
        if (outChars)  outChars[n] = all[i];
        if (outStates) fillState(l, &outStates[n]);
        ++n;
    }
    if (outTruncated) *outTruncated = trunc;
    return n;
}

// Camera / peer-cam globals (set*, peek* below).
bool  g_peerCamValid = false;
float g_peerCam[3] = { 0, 0, 0 };

} // namespace

// ---- enumeration -----------------------------------------------------------

unsigned int captureSquadAuthFakeGuard; // silences "empty TU" on some linkers

unsigned int captureNpcs(GameWorld* gw, EntityState* out, unsigned int maxOut) {
    (void)gw;
    return enumerate(false, 0, out, maxOut, 0);
}

unsigned int listNpcs(GameWorld* gw, Character** outChars, EntityState* outStates,
                      unsigned int maxOut, bool* outTruncated, bool full) {
    (void)gw; (void)full;
    return enumerate(false, outChars, outStates, maxOut, outTruncated);
}

unsigned int listNpcsWide(GameWorld* gw, float radius, Character** outChars,
                          EntityState* outStates, unsigned int maxOut,
                          bool* outTruncated, bool full) {
    (void)gw; (void)radius; (void)full;
    // Radius deliberately ignored: the fake world is small and pins want the
    // deterministic total. A radius-sensitive fake can grow here when a pin
    // needs one.
    return enumerate(false, outChars, outStates, maxOut, outTruncated);
}

bool captureNpcByHand(GameWorld* gw, unsigned int hIndex, unsigned int hSerial,
                      unsigned int hType, unsigned int hContainer,
                      unsigned int hContainerSerial, EntityState* out) {
    (void)gw;
    EntityState probe;
    std::memset(&probe, 0, sizeof(probe));
    probe.hType = hType; probe.hContainer = hContainer;
    probe.hContainerSerial = hContainerSerial;
    probe.hIndex = hIndex; probe.hSerial = hSerial;
    Character* c = resolve(probe);
    if (!c) return false;
    if (out) fillState(faketest::led(c), out);
    return true;
}

// ---- authority actuators ---------------------------------------------------

bool suppressNpc(GameWorld* gw, Character* c) {
    (void)gw;
    CharLedger* l = rec(c);
    if (!l) return false;
    ++l->suppress;
    l->suppressed = true;
    return true;
}

// restoreNpc + charName live in EngineFakes.cpp already (LNK2005 said so).

bool haltMovement(Character* c) {
    CharLedger* l = rec(c);
    if (l) ++l->halt;
    return l != 0;
}

// ---- reads -----------------------------------------------------------------

bool readMedical(Character* c, MedicalRead* out) {
    CharLedger* l = rec(c);
    if (!l || !out) return false;
    std::memset(out, 0, sizeof(*out));
    out->valid = true;
    out->blood = l->blood;
    out->bleedRate = l->bleedRate;
    for (int i = 0; i < 4; ++i) {
        out->limbFlesh[i] = 100.0f; out->limbBand[i] = 0.0f;
        out->limbMax[i] = 100.0f;
    }
    return true;
}

bool readPoseState(Character* c, float* pelvis, int* idle, int* crouched,
                   int* task) {
    (void)c;
    if (pelvis) *pelvis = 0.0f;
    if (idle) *idle = 0;
    if (crouched) *crouched = 0;
    if (task) *task = -1;
    return false; // "could not read" - the pose paths treat it as skip
}

bool charHandOf(Character* c, ObjectHand& out) {
    CharLedger* l = rec(c);
    if (!l) return false;
    out.type = l->hand[0]; out.container = l->hand[1];
    out.containerSerial = l->hand[2];
    out.index = l->hand[3]; out.serial = l->hand[4];
    return true;
}

bool describeCharacter(Character* c, char* charSid, unsigned int charSidLen,
                       char* facSid, unsigned int facSidLen,
                       float* x, float* y, float* z, float* heading, bool* dead,
                       float* age) {
    CharLedger* l = rec(c);
    if (!l) return false;
    // A stable per-body sid: the suppression witness compares this across
    // sweeps, so it must be deterministic for one body and distinct per hand.
    if (charSid && charSidLen) {
        _snprintf(charSid, charSidLen - 1, "fake-sid-%u-%u",
                  l->hand[3], l->hand[4]);
        charSid[charSidLen - 1] = '\0';
    }
    if (facSid && facSidLen) {
        _snprintf(facSid, facSidLen - 1, "fake-fac");
        facSid[facSidLen - 1] = '\0';
    }
    if (x) *x = l->x;
    if (y) *y = l->y;
    if (z) *z = l->z;
    if (heading) *heading = l->heading;
    if (dead) *dead = false;
    if (age) *age = 1.0f;
    return true;
}

// ---- world geometry --------------------------------------------------------

// 1000-unit cells. Any deterministic mapping works for the harness - both
// Replicator instances in a test share this one - and 1000 keeps coordinates
// legible in failures.
bool cellAt(GameWorld* gw, float x, float z, int* outCx, int* outCz) {
    (void)gw;
    if (outCx) *outCx = (int)std::floor(x / 1000.0f);
    if (outCz) *outCz = (int)std::floor(z / 1000.0f);
    return true;
}

bool cameraCenter(GameWorld* gw, float out[3]) {
    (void)gw;
    // The first squad body is the camera, which is what the attention system
    // treats it as anyway.
    static Character* all[2048];
    unsigned int total = faketest::allChars(all, 2048);
    for (unsigned int i = 0; i < total; ++i) {
        CharLedger& l = faketest::led(all[i]);
        if (!l.isSquad) continue;
        out[0] = l.x; out[1] = l.y; out[2] = l.z;
        return true;
    }
    return false;
}

unsigned int interestAnchors(GameWorld* gw, float out[12]) {
    (void)gw;
    static Character* all[2048];
    unsigned int total = faketest::allChars(all, 2048);
    unsigned int n = 0;
    for (unsigned int i = 0; i < total && n < 4; ++i) {
        CharLedger& l = faketest::led(all[i]);
        if (!l.isSquad) continue;
        out[n * 3 + 0] = l.x; out[n * 3 + 1] = l.y; out[n * 3 + 2] = l.z;
        ++n;
    }
    return n;
}

void setLocalCamAnchor(bool valid, float x, float y, float z) {
    (void)valid; (void)x; (void)y; (void)z;
}

void setPeerCamHint(bool valid, float x, float y, float z) {
    g_peerCamValid = valid;
    g_peerCam[0] = x; g_peerCam[1] = y; g_peerCam[2] = z;
}

bool peerCamAnchor(float out[3]) {
    if (!g_peerCamValid) return false;
    out[0] = g_peerCam[0]; out[1] = g_peerCam[1]; out[2] = g_peerCam[2];
    return true;
}

// ---- debug markers ---------------------------------------------------------

void* markerCreate(Character* c, const char* text, int colorId) {
    (void)c; (void)text; (void)colorId;
    return (void*)1; // alive, never dereferenced
}

bool markerUpdate(void* label, const char* text, int colorId) {
    (void)label; (void)text; (void)colorId;
    return true;
}

bool markerAlive(void* label) { return label != 0; }

} // namespace engine
} // namespace coop
