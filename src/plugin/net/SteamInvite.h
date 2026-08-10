// SteamInvite - Steam overlay invite + lobby-based automatic SteamID exchange.
//
// ---- STATUS: the invite half of this module is UNREACHABLE -------------------
// beginInvite / inviteFriend / pickerActive / friendCount / friendId /
// friendName / friendState have NO callers anywhere outside this file. The F2
// panel never exposes the picker, so no player can start an invite, which means
// no player can receive one either. The whole flow described below is
// aspirational, not shipped - the working path is the one it was meant to
// replace: Copy my Steam ID, paste the friend's, Connect.
//
// It is NOT deleted, and must not be deleted casually, because the file also
// carries two things the WORKING Steam transport depends on:
//
//   * onP2PSessionRequest -> steamp2p::accept(). This is what opens the inbound
//     tunnel for EVERY Steam connection, invite or not. Delete it and the manual
//     flow stops connecting.
//   * the SteamAPI_RunCallbacks pump in tick(). Kenshi does not run one, so
//     nothing else in the process dispatches Steam callbacks - including the one
//     above.
//
// Removing the dead surface therefore means extracting those two into SteamP2P
// first, which is a change to the connect path and needs a real two-machine test
// to trust. Until then this notice is the honest version: the picker is not a
// feature, and the rest of the file is load-bearing.
//
// The two-code exchange (SteamP2P's setPeer) is callback-free but forces players
// to read out and type 17-digit SteamIDs. This module layers Steam's native
// invite flow on top of the SAME P2P tunnel so nobody types an ID:
//
//   HOST : creates a 2-player friends-only Steam lobby, then opens the Steam
//          overlay's invite dialog. When the friend accepts and enters the lobby
//          the host learns their SteamID from lobby membership and connects.
//   JOIN : Steam delivers GameLobbyJoinRequested when the friend clicks the
//          invite (both players must already be in-game - Kenshi's launcher does
//          not forward +connect_lobby from a cold start). We join the lobby,
//          read the owner (= host SteamID) and connect as the client.
//
// Everything runs on the MAIN thread: tick() drives the Steam callback pump
// itself (SteamAPI_RunCallbacks) because Kenshi does not - its async CreateLobby
// result never dispatched until we added the call. This is safe even if the game
// also pumped: RunCallbacks just drains the message queue, so sequential
// main-thread calls can't double-deliver a single posted callback. tick() also
// polls lobby membership. The
// resolved peer is handed back through the ConnectFn (the plugin's coopUiConnect),
// which reuses the normal Steam-transport connect path. Manual ID entry stays as
// a fallback for setups where the overlay is unavailable.

#ifndef KENSHICOOP_STEAMINVITE_H
#define KENSHICOOP_STEAMINVITE_H

namespace coop {
namespace steaminvite {

typedef unsigned long long SteamId;

// Fired (on the main thread, from the Steam callback pump / tick) when an invite
// resolves a peer. Matches Plugin.cpp's coopUiConnect(isHost, useSteam, peerId).
typedef void (*ConnectFn)(bool isHost, bool useSteam, SteamId peerId);

// Resolve ISteamMatchmaking/ISteamFriends from the game's steam_api64.dll and
// register the invite/lobby/P2P Steam callbacks. Idempotent; safe to call every
// frame. Requires steamp2p::init() to have succeeded first (shared Steam client).
// Returns false (and logs why) when the interfaces are unavailable.
bool init(ConnectFn onConnect);
bool ready();

// Host action: open the in-panel friend picker. Creates the lobby (async) and
// refreshes the friend list. (Replaces the Steam overlay invite dialog, whose
// friend list is broken by a Steam-client web-view bug on many titles.)
void beginInvite();

// Send a direct Steam lobby invite to a specific friend (from the picker). The
// friend gets a Steam notification; clicking it fires GameLobbyJoinRequested on
// their side and auto-joins. If the lobby is still being created, the invite is
// queued and sent on completion.
void inviteFriend(SteamId id);

// In-panel picker accessors (main thread). The list is cached in our own buffers
// (Steam's GetFriendPersonaName pointer is transient) and sorted in-Kenshi first.
// friendState: 0 = offline, 1 = online, 2 = currently playing Kenshi.
bool        pickerActive();
int         friendCount();
SteamId     friendId(int i);
const char* friendName(int i);
int         friendState(int i);

// Main-thread housekeeping: pumps Steam callbacks, refreshes the picker list, and
// polls host-side lobby membership for the arriving friend. Call every frame.
void tick();

// One-line human status for the panel/overlay (never null; "" when idle).
const char* status();

// Leave any lobby and clear invite state (called on disconnect / teardown).
void reset();

} // namespace steaminvite
} // namespace coop

#endif // KENSHICOOP_STEAMINVITE_H
