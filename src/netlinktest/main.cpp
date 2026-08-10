// netlinktest - links the REAL NetLink receive path (src/plugin/net/NetLink.cpp,
// not a model of it) against the vendored ENet, drives it over the tunneltest-
// style in-memory socket-hook pipe, and pins its receive-side bounds checks:
//
//   * the handshake gate: nothing but HELLO/WELCOME is honoured before the
//     peer is established (NetLink.cpp's ev.peer->data / myId_ gate);
//   * PKT_ENTITY_BATCH: the len >= need pin, and the hdr.count cap against
//     ENTITY_BATCH_MAX. The cap checks were written BEFORE the clamp existed
//     and watched the unclamped receiver accept 18 and 255 entities per
//     packet (falsification first); the clamp at the NetLink.cpp batch arm
//     is what keeps them green;
//   * PKT_NPC_CENSUS: the existing hdr.count <= NPC_CENSUS_MAX cap, pinned
//     as-is (positive + negative, ordered on one reliable channel so the
//     negative is deterministic, not a timing guess);
//   * an unknown packet type is dropped and the net thread survives it.
//
// Differences from tunneltest's pipe, both deliberate: no loss injection, and
// no 1200-byte datagram ceiling - the count=255 batch is ~20 KB and must ride
// ENet's reliable fragmentation through the pipe intact (the NetLink hosts set
// maximumPacketSize to 64 KB; the pipe must not be the thing that caps it).
// The pipe IS thread-safe here, which tunneltest's is not: NetLink services
// the server host on its own net thread while main() services the raw client.
//
// Delivery is asserted on the real Inbound queues (drainEntities/drainNpcCensus
// /drainConnects), because that is the only door out of the net thread.
// Negatives use an ordering sentinel: a bad packet followed by a good one on
// the same reliable channel means "the good one arrived, so the bad one was
// definitively processed (and dropped), not still in flight".
//
// No game, no KenshiLib, no Steam, no real sockets. VC10 / C++03, same
// toolchain as the plugin.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1 // NetLink.h includes <windows.h> BEFORE enet's
#endif                        // winsock2.h; without this they collide.

#include "../plugin/net/NetLink.h"   // pulls windows.h, enet, Wire.h, Inbound.h
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <vector>

using namespace coop;

// ---- Test reporting -------------------------------------------------------------

static int g_pass = 0, g_fail = 0, g_num = 0;
static void check(bool ok, const char* what) {
    ++g_num;
    std::printf("%s %d - %s\n", ok ? "ok  " : "FAIL", g_num, what);
    if (ok) ++g_pass; else ++g_fail;
}

// ---- The in-memory pipe (tunneltest scaffolding, made thread-safe) --------------
// Two endpoints: the NetLink host's socket (created FIRST, on the net thread)
// and the raw client's (created second, on the main thread). Create order is
// enforced by main() waiting for the net thread's create before making the
// client. Zero loss, no datagram cap - determinism is the point here, the
// Steam-pipe constraints are tunneltest's job.

static const ENetSocket SOCK_SERVER = (ENetSocket)1001;
static const ENetSocket SOCK_CLIENT = (ENetSocket)1002;
static const unsigned short SERVER_PORT = 27815;

struct Datagram { std::vector<unsigned char> bytes; };
static std::deque<Datagram> g_toServer, g_toClient;
static CRITICAL_SECTION     g_pipeCs;   // two threads share these queues
static int                  g_created = 0;

static int createdCount() {
    EnterCriticalSection(&g_pipeCs);
    int n = g_created;
    LeaveCriticalSection(&g_pipeCs);
    return n;
}

static ENetSocket ENET_CALLBACK hookCreate(ENetSocketType type) {
    (void)type;
    EnterCriticalSection(&g_pipeCs);
    ++g_created;
    ENetSocket s = (g_created == 1) ? SOCK_SERVER : SOCK_CLIENT;
    LeaveCriticalSection(&g_pipeCs);
    return s;
}

static int ENET_CALLBACK hookBind(ENetSocket s, const ENetAddress* a) {
    (void)s; (void)a;
    return 0;
}

static int ENET_CALLBACK hookSend(ENetSocket s, const ENetAddress* addr,
                                  const ENetBuffer* buffers, size_t count) {
    // One datagram is at most the ENet MTU (~1400 B default; nobody clamps it
    // here) - large reliable packets arrive as many datagrams, each well under
    // this buffer. 8 KB of headroom, same as tunneltest.
    unsigned char buf[8192];
    unsigned int len = 0;
    size_t i;
    (void)addr; // single peer each way; routing is by socket
    for (i = 0; i < count; ++i) {
        if (len + buffers[i].dataLength > sizeof(buf)) return -1;
        std::memcpy(buf + len, buffers[i].data, buffers[i].dataLength);
        len += (unsigned int)buffers[i].dataLength;
    }
    Datagram d;
    d.bytes.assign(buf, buf + len);
    EnterCriticalSection(&g_pipeCs);
    if (s == SOCK_SERVER) g_toClient.push_back(d); else g_toServer.push_back(d);
    LeaveCriticalSection(&g_pipeCs);
    return (int)len;
}

static int ENET_CALLBACK hookReceive(ENetSocket s, ENetAddress* addr,
                                     ENetBuffer* buffers, size_t count) {
    if (count < 1) return 0;
    Datagram d;
    EnterCriticalSection(&g_pipeCs);
    {
        std::deque<Datagram>& q = (s == SOCK_SERVER) ? g_toServer : g_toClient;
        if (q.empty()) { LeaveCriticalSection(&g_pipeCs); return 0; }
        d = q.front();
        q.pop_front();
    }
    LeaveCriticalSection(&g_pipeCs);
    unsigned int n = (unsigned int)d.bytes.size();
    if (n > buffers[0].dataLength) return -2;
    if (n > 0) std::memcpy(buffers[0].data, &d.bytes[0], n);
    if (addr != 0) {
        // Fabricated identities: the client must see the exact address it
        // connected to, or ENet discards the datagram as a stranger's.
        if (s == SOCK_CLIENT) { addr->host = 0x01000001u; addr->port = SERVER_PORT; }
        else                  { addr->host = 0x01000002u; addr->port = 34567; }
    }
    return (int)n;
}

static bool pipeHasData(ENetSocket s) {
    EnterCriticalSection(&g_pipeCs);
    bool has = !((s == SOCK_SERVER) ? g_toServer.empty() : g_toClient.empty());
    LeaveCriticalSection(&g_pipeCs);
    return has;
}

static int ENET_CALLBACK hookWait(ENetSocket s, enet_uint32* condition, enet_uint32 timeout) {
    // Two threads share the pipe (unlike tunneltest's single-thread pump), so
    // this may briefly yield instead of spinning: NetLink's net thread calls
    // enet_host_service(host, ev, 50) which loops on this wait until data or
    // timeout, and a hard busy-wait would pin a core for the whole run.
    enet_uint32 want = *condition;
    bool has = pipeHasData(s);
    if (!has && (want & ENET_SOCKET_WAIT_RECEIVE) && timeout > 0) {
        Sleep(1);
        has = pipeHasData(s);
    }
    *condition = ENET_SOCKET_WAIT_NONE;
    if (want & ENET_SOCKET_WAIT_SEND) *condition |= ENET_SOCKET_WAIT_SEND;
    if ((want & ENET_SOCKET_WAIT_RECEIVE) && has) *condition |= ENET_SOCKET_WAIT_RECEIVE;
    return 0;
}

static void ENET_CALLBACK hookDestroy(ENetSocket s) { (void)s; }

// ---- Packet builders ------------------------------------------------------------

static const u32 OWNER = 1; // the id the host assigns to its first (only) join
static const u32 EPOCH = 1; // shared by every batch: acceptEpoch is >=, so all pass

// [EntityBatchHeader][EntityState * count], exactly need bytes. Entities are
// zero-filled except a distinct identity per element, which is all the drain
// assertions read.
static void buildEntityBatch(std::vector<unsigned char>& out, unsigned count, u32 firstSerial) {
    EntityBatchHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.type    = (u8)PKT_ENTITY_BATCH;
    hdr.ownerId = OWNER;
    hdr.sendMs  = 1000;
    hdr.epoch   = EPOCH;
    hdr.count   = (u8)count;
    out.resize(sizeof(hdr) + count * sizeof(EntityState));
    std::memcpy(&out[0], &hdr, sizeof(hdr));
    unsigned i;
    for (i = 0; i < count; ++i) {
        EntityState e;
        std::memset(&e, 0, sizeof(e));
        e.hType   = 1;
        e.hSerial = firstSerial + i;
        e.task    = TASK_NONE;
        e.rawTask = TASK_NONE;
        std::memcpy(&out[sizeof(hdr) + i * sizeof(e)], &e, sizeof(e));
    }
}

// [NpcCensusHeader][u32 hand[5] * count][f32 pos[3] * count], exactly need bytes.
// trunc sets the v58 truncation bit on the wire count; the payload still
// carries `count` real rows - the flag says the list is incomplete, not short.
static void buildCensus(std::vector<unsigned char>& out, unsigned count,
                        bool trunc = false) {
    NpcCensusHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.type    = (u8)PKT_NPC_CENSUS;
    hdr.ownerId = OWNER;
    hdr.count   = (u16)(count | (trunc ? NPC_CENSUS_TRUNC : 0));
    const unsigned rowHands = 5 * sizeof(u32);
    const unsigned rowPos   = 3 * sizeof(f32);
    out.resize(sizeof(hdr) + count * (rowHands + rowPos));
    std::memcpy(&out[0], &hdr, sizeof(hdr));
    unsigned char* p = &out[sizeof(hdr)];
    unsigned i;
    for (i = 0; i < count; ++i) {
        u32 hand[5];
        hand[0] = 2; hand[1] = 0; hand[2] = 0; hand[3] = i; hand[4] = 1000 + i;
        std::memcpy(p + i * rowHands, hand, rowHands);
    }
    for (i = 0; i < count; ++i) {
        f32 pos[3];
        pos[0] = (f32)i; pos[1] = (f32)(2 * i); pos[2] = (f32)(3 * i);
        std::memcpy(p + count * rowHands + i * rowPos, pos, rowPos);
    }
}

static bool sendRaw(ENetPeer* peer, const void* data, unsigned len) {
    ENetPacket* p = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    if (!p) return false;
    if (enet_peer_send(peer, 0 /*CH_RELIABLE - dispatch is by TYPE, and one
                                 reliable channel gives ordered negatives*/, p) < 0) {
        enet_packet_destroy(p);
        return false;
    }
    return true;
}

// ---- Client pump + Inbound drains -----------------------------------------------

struct ClientGot {
    bool          connected;
    bool          gotWelcome;
    WelcomePacket welcome;
    ClientGot() : connected(false), gotWelcome(false) {
        std::memset(&welcome, 0, sizeof(welcome));
    }
};

static void pumpClient(ENetHost* h, ClientGot& got) {
    ENetEvent ev;
    while (enet_host_service(h, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: got.connected = true; break;
        case ENET_EVENT_TYPE_RECEIVE:
            if (ev.packet->dataLength >= sizeof(WelcomePacket) &&
                ev.packet->data[0] == (u8)PKT_WELCOME) {
                std::memcpy(&got.welcome, ev.packet->data, sizeof(got.welcome));
                got.gotWelcome = true;
            }
            enet_packet_destroy(ev.packet);
            break;
        default: break;
        }
    }
}

static void drainEnt(Inbound& in, std::vector<InboundEntity>& acc) {
    std::deque<InboundEntity> q;
    in.drainEntities(q);
    size_t i;
    for (i = 0; i < q.size(); ++i) acc.push_back(q[i]);
}

static void drainCensus(Inbound& in, std::vector<InboundNpcCensus>& acc) {
    std::deque<InboundNpcCensus> q;
    in.drainNpcCensus(q);
    size_t i;
    for (i = 0; i < q.size(); ++i) acc.push_back(q[i]);
}

// Pump + drain until an entity with hSerial == sentinel shows up (or deadline).
static bool waitSentinel(ENetHost* client, ClientGot& got, Inbound& in,
                         std::vector<InboundEntity>& acc, u32 sentinel, DWORD timeoutMs) {
    DWORD deadline = GetTickCount() + timeoutMs;
    for (;;) {
        pumpClient(client, got);
        drainEnt(in, acc);
        size_t i;
        for (i = 0; i < acc.size(); ++i)
            if (acc[i].e.hSerial == sentinel) return true;
        if ((long)(GetTickCount() - deadline) >= 0) return false;
        Sleep(2);
    }
}

// Send one entity batch built to 'count'/'firstSerial'; optionally shave bytes
// off the end (lenAdjust < 0) to violate len >= need.
static bool sendBatch(ENetPeer* peer, unsigned count, u32 firstSerial, int lenAdjust) {
    std::vector<unsigned char> pkt;
    buildEntityBatch(pkt, count, firstSerial);
    unsigned len = (unsigned)((int)pkt.size() + lenAdjust);
    return sendRaw(peer, &pkt[0], len);
}

// ---- The test -------------------------------------------------------------------

int main() {
    std::printf("netlinktest: the REAL NetLink receive path over an in-memory pipe\n"
                "(handshake gate + entity-batch/census receive-side bounds pins)\n\n");

    // 0) Layout pins every byte offset below depends on. prototest asserts the
    //    full field maps; these four sizes are what THIS file's arithmetic uses.
    check(sizeof(EntityState) == 79,       "sizeof(EntityState) == 79");
    check(sizeof(EntityBatchHeader) == 14, "sizeof(EntityBatchHeader) == 14");
    check(sizeof(NpcCensusHeader) == 7,    "sizeof(NpcCensusHeader) == 7");
    check(sizeof(HelloPacket) == 10,       "sizeof(HelloPacket) == 10");
    check(sizeof(WelcomePacket) == 13,     "sizeof(WelcomePacket) == 13");

    InitializeCriticalSection(&g_pipeCs);
    check(enet_initialize() == 0, "enet_initialize");

    ENetSocketHooks hooks;
    std::memset(&hooks, 0, sizeof(hooks));
    hooks.socket_create  = &hookCreate;
    hooks.socket_bind    = &hookBind;
    hooks.socket_send    = &hookSend;
    hooks.socket_receive = &hookReceive;
    hooks.socket_wait    = &hookWait;
    hooks.socket_destroy = &hookDestroy;
    enet_set_socket_hooks(&hooks);

    // 1) The real thing: NetLink hosting over the pipe, delivering into a real
    //    Inbound. Its net thread owns the server host from here on.
    Inbound inbound;
    NetLink link;
    check(link.startHost((int)SERVER_PORT, &inbound), "NetLink::startHost launched the net thread");

    // Create-order determinism: the net thread's enet_host_create must claim
    // SOCK_SERVER before the raw client claims SOCK_CLIENT.
    {
        DWORD deadline = GetTickCount() + 10000;
        while (createdCount() < 1 && (long)(GetTickCount() - deadline) < 0) Sleep(2);
    }
    check(createdCount() == 1, "net thread created the server socket first");

    // 2) A raw ENet client - no NetLink, so this side can speak arbitrary bytes.
    ENetHost* client = enet_host_create(0, 1, 3 /*NetLink's CH_COUNT*/, 0, 0);
    check(client != 0, "raw client enet_host_create over hooks");
    if (!client) return g_fail + 1;
    ENetAddress addr;
    addr.host = 0x01000001u; // must match what hookReceive fabricates
    addr.port = SERVER_PORT;
    ENetPeer* peer = enet_host_connect(client, &addr, 3, 0);
    check(peer != 0, "enet_host_connect to the NetLink host");
    if (!peer) return g_fail + 1;

    ClientGot got;
    {
        DWORD deadline = GetTickCount() + 15000;
        while (!got.connected && (long)(GetTickCount() - deadline) < 0) {
            pumpClient(client, got);
            Sleep(2);
        }
    }
    check(got.connected, "client CONNECT event (ENet handshake over the pipe)");

    std::vector<InboundEntity>    ents;
    std::vector<InboundNpcCensus> census;

    // 3) Pre-handshake gate. A well-formed ENTITY_BATCH sent BEFORE HELLO, then
    //    the HELLO, on the same reliable channel: ENet delivers them in order,
    //    so once the WELCOME is back the host has definitively processed (and
    //    per NetLink's established-gate, dropped) the early batch - no timing
    //    window in the negative.
    check(sendBatch(peer, 3, 100, 0), "pre-HELLO well-formed ENTITY_BATCH sent");
    {
        HelloPacket h;
        h.type = (u8)PKT_HELLO; h.version = PROTOCOL_VERSION; h.nameLen = 0;
        check(sendRaw(peer, &h, sizeof(h)), "HELLO sent (version + empty name)");

        DWORD deadline = GetTickCount() + 15000;
        while (!got.gotWelcome && (long)(GetTickCount() - deadline) < 0) {
            pumpClient(client, got);
            Sleep(2);
        }
        check(got.gotWelcome, "WELCOME received");
        check(got.gotWelcome && got.welcome.version == PROTOCOL_VERSION,
              "  ... echoes PROTOCOL_VERSION");
        check(got.gotWelcome && got.welcome.playerId == OWNER,
              "  ... assigns playerId 1");

        // The game thread's view of the join arriving.
        bool sawConn = false;
        DWORD d2 = GetTickCount() + 5000;
        while (!sawConn && (long)(GetTickCount() - d2) < 0) {
            std::deque<u32> conns;
            inbound.drainConnects(conns);
            size_t i;
            for (i = 0; i < conns.size(); ++i) if (conns[i] == OWNER) sawConn = true;
            if (!sawConn) Sleep(2);
        }
        check(sawConn, "Inbound connect event for id 1");

        drainEnt(inbound, ents);
        check(ents.empty(), "pre-handshake ENTITY_BATCH never reached Inbound (established gate)");
    }

    // 4) Valid batch: count=3, len == need, epoch accepted - exactly 3 delivered,
    //    owner-tagged and send-stamped, identities intact.
    {
        ents.clear();
        check(sendBatch(peer, 3, 200, 0), "valid ENTITY_BATCH count=3 sent");
        DWORD deadline = GetTickCount() + 10000;
        while (ents.size() < 3 && (long)(GetTickCount() - deadline) < 0) {
            pumpClient(client, got);
            drainEnt(inbound, ents);
            Sleep(2);
        }
        // Short grace so a phantom extra would be caught, not raced past.
        DWORD grace = GetTickCount() + 300;
        while ((long)(GetTickCount() - grace) < 0) {
            pumpClient(client, got);
            drainEnt(inbound, ents);
            Sleep(2);
        }
        check(ents.size() == 3, "exactly 3 entities delivered");
        bool ids = ents.size() == 3 &&
                   ents[0].e.hSerial == 200 && ents[1].e.hSerial == 201 &&
                   ents[2].e.hSerial == 202;
        check(ids, "  ... in order, identities intact");
        check(ents.size() == 3 && ents[0].ownerId == OWNER && ents[0].sendMs == 1000,
              "  ... owner-tagged + send-stamped from the header");
    }

    // 5) Short batch: count=17 declared, one byte missing. len >= need must
    //    drop the WHOLE batch (no partial parse). Sentinel proves it was
    //    processed, not lost.
    {
        ents.clear();
        check(sendBatch(peer, 17, 300, -1), "count=17 batch with len == need-1 sent");
        check(sendBatch(peer, 1, 9001, 0), "  ... ordering sentinel sent behind it");
        bool s = waitSentinel(client, got, inbound, ents, 9001, 10000);
        check(s, "  ... sentinel arrived");
        check(s && ents.size() == 1 && ents[0].e.hSerial == 9001,
              "short batch dropped whole (len >= need pin)");
    }

    // 6) Over-cap: count = ENTITY_BATCH_MAX+1 with len == need. There is NO
    //    receive-side count cap today (hdr.count is a u8 the sender controls),
    //    so this check FAILS against current NetLink BY DESIGN - it encodes the
    //    post-fix contract (drop at the gate) and goes green when the clamp
    //    `hdr.count <= ENTITY_BATCH_MAX` lands next to acceptEpoch.
    {
        ents.clear();
        check(sendBatch(peer, ENTITY_BATCH_MAX + 1, 400, 0),
              "count=ENTITY_BATCH_MAX+1 batch (len == need) sent");
        check(sendBatch(peer, 1, 9002, 0), "  ... ordering sentinel sent behind it");
        bool s = waitSentinel(client, got, inbound, ents, 9002, 10000);
        check(s, "  ... sentinel arrived");
        check(s && ents.size() == 1 && ents[0].e.hSerial == 9002,
              "over-cap batch dropped (hdr.count <= ENTITY_BATCH_MAX clamp)");
    }

    // 7) Max abuse: count=255, len == need (14 + 255*79 = 20159 B). Reliable, so
    //    ENet fragments it through the pipe and reassembles under the 64 KB
    //    maximumPacketSize NetLink sets. Same post-fix expectation as (6).
    {
        ents.clear();
        check(sendBatch(peer, 255, 500, 0), "count=255 batch (20159 B, reliable/fragmented) sent");
        check(sendBatch(peer, 1, 9003, 0), "  ... ordering sentinel sent behind it");
        bool s = waitSentinel(client, got, inbound, ents, 9003, 15000);
        check(s, "  ... sentinel arrived (fragmentation survived the pipe)");
        check(s && ents.size() == 1 && ents[0].e.hSerial == 9003,
              "count=255 batch dropped (hdr.count <= ENTITY_BATCH_MAX clamp)");
    }

    // 8) Census cap - EXISTS today, pin it as-is. Over-cap first, valid second,
    //    same reliable channel: when the valid one is in and the over-cap one is
    //    not, the cap provably fired.
    {
        census.clear();
        std::vector<unsigned char> bad;
        buildCensus(bad, NPC_CENSUS_MAX + 1);
        check(sendRaw(peer, &bad[0], (unsigned)bad.size()),
              "census count=NPC_CENSUS_MAX+1 (len == need) sent");
        std::vector<unsigned char> good;
        buildCensus(good, 2);
        check(sendRaw(peer, &good[0], (unsigned)good.size()),
              "  ... valid census count=2 sent behind it");
        bool validIn = false;
        DWORD deadline = GetTickCount() + 10000;
        while (!validIn && (long)(GetTickCount() - deadline) < 0) {
            pumpClient(client, got);
            drainCensus(inbound, census);
            size_t i;
            for (i = 0; i < census.size(); ++i)
                if (census[i].hands.size() == 2 * 5) validIn = true;
            if (!validIn) Sleep(2);
        }
        check(validIn, "  ... valid census delivered");
        check(validIn && census.size() == 1,
              "over-cap census dropped (hdr.count <= NPC_CENSUS_MAX pinned)");
        check(validIn && census.size() == 1 &&
              census[0].ownerId == OWNER && census[0].pos.size() == 2 * 3,
              "  ... delivered whole: hands + positions, owner-tagged");
    }

    // 8b) v58 truncation flag: a flagged count is masked before the bound
    //     check, delivered whole, and the flag survives to the Inbound record.
    {
        census.clear();
        std::vector<unsigned char> tr;
        buildCensus(tr, 3, true);
        check(sendRaw(peer, &tr[0], (unsigned)tr.size()),
              "census count=3|TRUNC sent");
        bool truncIn = false;
        DWORD deadline = GetTickCount() + 10000;
        while (!truncIn && (long)(GetTickCount() - deadline) < 0) {
            pumpClient(client, got);
            drainCensus(inbound, census);
            size_t i;
            for (i = 0; i < census.size(); ++i)
                if (census[i].hands.size() == 3 * 5 && census[i].truncated)
                    truncIn = true;
            if (!truncIn) Sleep(2);
        }
        check(truncIn, "flagged census delivered with truncated=true (v58)");
    }

    // 9) Unknown packet type: dropped (and logged/rate-limited inside NetLink),
    //    Inbound untouched, net thread alive - the sentinel behind it proves all
    //    three at once.
    {
        ents.clear();
        unsigned char junk[8];
        std::memset(junk, 0, sizeof(junk));
        junk[0] = 250; // no PacketType arm goes this high
        check(sendRaw(peer, junk, sizeof(junk)), "unknown packet type=250 sent");
        check(sendBatch(peer, 1, 9004, 0), "  ... ordering sentinel sent behind it");
        bool s = waitSentinel(client, got, inbound, ents, 9004, 10000);
        check(s, "  ... sentinel arrived (net thread survived the unknown type)");
        check(s && ents.size() == 1 && ents[0].e.hSerial == 9004,
              "unknown type dropped; Inbound unchanged");
        std::deque<InboundNpcCensus> cq;
        inbound.drainNpcCensus(cq);
        check(cq.empty(), "  ... and no stray census either");
        check(link.isRunning(), "NetLink still running");
    }

    // Teardown: NetLink's stop() joins the net thread (which destroys the server
    // host); then the raw client and the hooks come down.
    link.stop();
    check(!link.isRunning(), "NetLink::stop joined the net thread");
    enet_host_destroy(client);
    enet_set_socket_hooks(0);
    enet_deinitialize();
    DeleteCriticalSection(&g_pipeCs);

    std::printf("\nnetlinktest: %d/%d checks passed - %s\n",
                g_pass, g_pass + g_fail, g_fail == 0 ? "PASS" : "FAIL");
    return g_fail;
}
