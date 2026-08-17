#define _CRT_SECURE_NO_WARNINGS 1 // getenv/atoi are fine; silence VC10 C4996

#include "Config.h"
#include "OwnRanks.h"
#include <cstdlib>
#include <cstdio>
#include <map>
#include <fstream>
#include <iterator>
#include <windows.h>

namespace coop {
namespace {

std::string envOr(const char* key, const char* def) {
    const char* v = std::getenv(key);
    return std::string(v ? v : def);
}

// ---- coop_config.json (flat, tolerant) --------------------------------------
// The interactive install configures the friend code (steamPeer) + connection
// defaults in coop_config.json next to the DLL. This is a deliberately small
// parser: flat "key": value pairs only (string / number / bool), // line
// comments stripped. Values feed loadConfig as the DEFAULTS for envOr(), so the
// env-var test harness still wins (env > file > hard-coded).

std::map<std::string, std::string> parseFlatJson(const std::string& text) {
    std::map<std::string, std::string> m;
    // Strip // line comments first (our values never contain "//").
    std::string s;
    size_t i = 0;
    while (i <= text.size()) {
        size_t nl = text.find('\n', i);
        std::string line = text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        size_t c = line.find("//");
        if (c != std::string::npos) line = line.substr(0, c);
        s += line;
        s += '\n';
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    // Scan "key" : <value>. Quoted values read to the closing quote; bare values
    // (numbers/bools) read to the next , } or newline.
    size_t p = 0;
    while (true) {
        size_t q1 = s.find('"', p);
        if (q1 == std::string::npos) break;
        size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string key = s.substr(q1 + 1, q2 - q1 - 1);
        size_t colon = s.find(':', q2 + 1);
        if (colon == std::string::npos) break;
        size_t v = colon + 1;
        while (v < s.size() && (s[v] == ' ' || s[v] == '\t' || s[v] == '\r' || s[v] == '\n')) ++v;
        std::string val;
        if (v < s.size() && s[v] == '"') {
            size_t ve = s.find('"', v + 1);
            if (ve == std::string::npos) break;
            val = s.substr(v + 1, ve - v - 1);
            p = ve + 1;
        } else {
            size_t ve = v;
            while (ve < s.size() && s[ve] != ',' && s[ve] != '}' && s[ve] != '\n' && s[ve] != '\r') ++ve;
            val = s.substr(v, ve - v);
            while (!val.empty() && (val[val.size() - 1] == ' ' || val[val.size() - 1] == '\t')) val.erase(val.size() - 1);
            p = ve;
        }
        m[key] = val;
    }
    return m;
}

// Absolute path to coop_config.json next to KenshiCoop.dll (fallback: cwd).
std::string configFilePath() {
    char buf[MAX_PATH];
    HMODULE h = GetModuleHandleA("KenshiCoop.dll");
    DWORD n = GetModuleFileNameA(h, buf, MAX_PATH); // h == 0 would give the exe path
    if (h == 0 || n == 0 || n >= MAX_PATH) return "coop_config.json";
    std::string p(buf, n);
    size_t slash = p.find_last_of("\\/");
    p = (slash != std::string::npos) ? p.substr(0, slash + 1) : std::string();
    return p + "coop_config.json";
}

std::map<std::string, std::string> readConfigFile() {
    std::map<std::string, std::string> m;
    std::ifstream f(configFilePath().c_str(), std::ios::binary);
    if (!f) return m;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseFlatJson(text);
}

// File value for 'key' if present + non-empty, else 'def'. Used as the default
// argument to envOr() so precedence is env > file > hard-coded.
std::string fileOr(const std::map<std::string, std::string>& f, const char* key, const char* def) {
    std::map<std::string, std::string>::const_iterator it = f.find(key);
    if (it != f.end() && !it->second.empty()) return it->second;
    return std::string(def);
}

// One unsigned tunable, resolved env > coop_config.json > the value already in the
// struct (i.e. the SyncTuning constructor default). Non-numeric or negative input
// keeps the default rather than silently becoming 0, because a 0 cadence here means
// "send every tick" and a 0 debounce means "the bug is back".
void tuneU(unsigned long& slot, const std::map<std::string, std::string>& f,
           const char* envKey, const char* jsonKey) {
    char def[32];
    _snprintf(def, sizeof(def) - 1, "%lu", slot);
    def[sizeof(def) - 1] = '\0';
    std::string v = envOr(envKey, fileOr(f, jsonKey, def).c_str());
    long parsed = std::atol(v.c_str());
    if (parsed > 0 || (parsed == 0 && v == "0")) slot = (unsigned long)(parsed < 0 ? 0 : parsed);
}

void tuneUInt(unsigned int& slot, const std::map<std::string, std::string>& f,
              const char* envKey, const char* jsonKey) {
    unsigned long wide = slot;
    tuneU(wide, f, envKey, jsonKey);
    slot = (unsigned int)wide;
}

} // namespace

void loadConfig(Config& c) {
    // coop_config.json (next to the DLL) supplies the interactive-install
    // defaults; env vars still override every one of them (test harness).
    std::map<std::string, std::string> f = readConfigFile();
    // An unshadowed alias. Further down, a `double f` shadows the map inside the
    // tuning block, so a fileOr() written there silently resolves to the double
    // and fails to compile - which is how two knobs ended up env-only.
    const std::map<std::string, std::string>& cfgFile = f;

    std::string mode = envOr("KENSHICOOP_MODE", fileOr(f, "role", "host").c_str());
    c.isHost      = (mode != "join");
    c.ip          = envOr("KENSHICOOP_IP", fileOr(f, "ip", "127.0.0.1").c_str());
    c.port        = std::atoi(envOr("KENSHICOOP_PORT", fileOr(f, "port", "27800").c_str()).c_str());
    c.save        = envOr("KENSHICOOP_SAVE", "");
    c.testSeconds = std::atoi(envOr("KENSHICOOP_TEST_SECONDS", "0").c_str());

    std::string defLog = c.isHost ? "KenshiCoop_host.log" : "KenshiCoop_join.log";
    c.logPath  = envOr("KENSHICOOP_LOG", defLog.c_str());
    c.scenario = envOr("KENSHICOOP_SCENARIO", "");

    int d = std::atoi(envOr("KENSHICOOP_AUTOLOAD_DELAY_MS", "5000").c_str());
    c.autoLoadDelayMs = (d > 0) ? (unsigned long)d : 5000ul;

    c.setupScene = envOr("KENSHICOOP_SETUP", "");
    c.bakeSave   = envOr("KENSHICOOP_BAKESAVE", "");
    c.probeRecruit = envOr("KENSHICOOP_PROBE_RECRUIT", "") == "1";
    // AI-suspend is the DEFAULT quieting layer for the join's driven world NPCs
    // (review 2026-07-05). KENSHICOOP_AI_SUSPEND=0 is the escape hatch; the legacy
    // probe env still forces it on so old harness call sites keep working.
    c.aiSuspend = (envOr("KENSHICOOP_AI_SUSPEND", "1") != "0") ||
                  (envOr("KENSHICOOP_PROBE_AISUSPEND", "") == "1");
    c.noDetach  = envOr("KENSHICOOP_NO_DETACH", "") == "1";
    c.damageGuard = envOr("KENSHICOOP_DAMAGE_GUARD", "1") != "0";
    c.splitAuthority = envOr("KENSHICOOP_SPLIT_AUTHORITY",
                             fileOr(f, "splitAuthority", "0").c_str()) != "0";
    c.damageFloaters = envOr("KENSHICOOP_DAMAGE_FLOATERS",
                             fileOr(f, "damageFloaters", "1").c_str()) != "0";
    c.weatherSync    = envOr("KENSHICOOP_WEATHER_SYNC",
                             fileOr(f, "weatherSync", "1").c_str()) != "0";
    c.dialogueSync   = envOr("KENSHICOOP_DIALOGUE_SYNC",
                             fileOr(f, "dialogueSync", "1").c_str()) != "0";
    // Divergence-gated authority promoted to DEFAULT ON (step-4 A/B, 2026-07-05:
    // trusted-set engaged in 4/4 runs - grants 5-8, trusted ~5 of 12 driven - with
    // npc_track/pose gates at parity; the single red run was a host crash that
    // retry-passed). "0" is the escape hatch.
    c.gateAuthority = envOr("KENSHICOOP_GATE_AUTHORITY", "1") != "0";

    // Inventory sync (Phase 4a). Env semantics: "1" = force on, "0" = force off
    // (escape hatch), unset = ON for REAL sessions (scenario == "" - the 2026-07-07
    // remote session played with it off and equipment changes never crossed) and the
    // manual inventory setup scene. Scripted test scenarios that need it (the inv_*,
    // trade_*, world_*_drop, vendor/store/weapon_loot family) now force it ON via
    // their manifest DiagEnv (KENSHICOOP_INV_SYNC=1) instead of being named here.
    {
        std::string env = envOr("KENSHICOOP_INV_SYNC", "");
        bool auto_ = (c.scenario == "") || (c.setupScene == "inventory");
        c.invSync = (env == "1") || (env != "0" && auto_);
    }

    // Cross-owner transfer intents (protocol 37). Env semantics: "1" = force on,
    // "0" = force off (escape hatch), unset = ON whenever invSync is on - the drag
    // dupe/wipe/weapon-vanish is a real-session bug, so the fix defaults on with the
    // channel it repairs. The trade_probe diagnostic (which baselines the UNFIXED
    // signatures) forces it OFF via its manifest DiagEnv (KENSHICOOP_XFER_SYNC=0).
    {
        std::string env = envOr("KENSHICOOP_XFER_SYNC", "");
        c.xferSync = (env == "1") || (env != "0" && c.invSync);
    }

    // Cross-owner trade veto (OPT-IN). Blocks direct squad-to-squad drags and
    // forces ground drops. Superseded as the DEFAULT by allowing direct trade via
    // Protocol 37 (xferSync): the tryAddItem veto could not reliably classify every
    // engine drag path (the portrait-drag routes the item through the cursor, whose
    // owner is not a squad character - see the 2026-07-13 session), so a blocked
    // drag leaked as a phantom ground drop. Protocol 37 is post-hoc (diffs container
    // totals) and therefore path-agnostic, so REAL sessions now ALLOW + replicate.
    // Env semantics: "1" = force the veto on; unset/"0" = off. The dedicated
    // xfer_block scenario arms it via its manifest DiagEnv (KENSHICOOP_BLOCK_XFER=1).
    {
        std::string env = envOr("KENSHICOOP_BLOCK_XFER", "");
        c.blockXfer = (env == "1");
        if (c.blockXfer) c.xferSync = false; // veto supersedes replicate
    }

    // World-item sync (Phase W1/W2). Env semantics: "1" = force on, "0" = force off
    // (escape hatch), unset = ON for REAL sessions (scenario == "" - dropped gear was
    // invisible cross-client in the 2026-07-07 remote session with it off). The
    // scripted world scenarios that need it (world_item_*, world_*_drop, limb_loss)
    // force it ON via their manifest DiagEnv (KENSHICOOP_WORLD_SYNC=1).
    {
        std::string env = envOr("KENSHICOOP_WORLD_SYNC", "");
        bool auto_ = (c.scenario == ""); // free play / real co-op session
        c.worldSync = (env == "1") || (env != "0" && auto_);
    }

    // Medical sync (phase 2 of the player-combat/medical plan): DEFAULT ON -
    // owner-authoritative vitals for player-squad members + treatment forwarding.
    // Without it, spikes 21-23's truth holds: driven copies' vitals diverge
    // forever and cross-player first aid is lost. "0" is the A/B escape hatch.
    c.medSync = envOr("KENSHICOOP_MED_SYNC", "1") != "0";

    // Consensus game-speed sync: DEFAULT ON - requests min-arbitrated by the
    // host, combat caps fast-forward at 1x. "0" is the A/B escape hatch. The
    // speed_probe spike (which drives the quiet/loud writers directly) and
    // time_probe force it OFF via their manifest DiagEnv (KENSHICOOP_SPEED_SYNC=0).
    c.speedSync = envOr("KENSHICOOP_SPEED_SYNC", "1") != "0";
    c.speedCombatCap = envOr("KENSHICOOP_SPEED_COMBAT_CAP", "1") != "0";
    c.trackMove = envOr("KENSHICOOP_TRACK_MOVE", "0") == "1";

    // Character stats sync (protocol 17): DEFAULT ON - owner-authoritative
    // CharStats stream for player-squad members. Without it a driven copy
    // keeps save-load stats all session, and the peer's engine resolves REAL
    // fights with those stale numbers. "0" is the A/B escape hatch.
    c.statsSync = envOr("KENSHICOOP_STATS_SYNC", "1") != "0";

    // Crash tracer (vectored exception dump, core/CrashDump.h): DEFAULT OFF -
    // the handler is vectored at priority 1 and the engine throws routinely,
    // so the cost is paid on every exception, not only the fatal one. Env for
    // the harness, json ("crashTracer": true in coop_config.json) for players:
    // the game launches from Steam, so an env-only knob means editing launch
    // options mid-session - which is why two live crashes on 2026-08-10 went
    // untraced. Ask a player to flip the json key when taking a crash report.
    c.crashTracer = envOr("KENSHICOOP_CRASH_TRACER",
                          fileOr(f, "crashTracer", "0").c_str()) == "1";

    // Per-channel cadences and self-heal debounces. c.tuning starts at the
    // SyncTuning defaults, so each call below only replaces what was actually
    // supplied. Env name and JSON key are kept parallel on purpose.
    tuneU(c.tuning.moneyMinSendMs,    f, "KENSHICOOP_MONEY_MIN_SEND_MS",   "moneyMinSendMs");
    tuneU(c.tuning.moneyResendMs,     f, "KENSHICOOP_MONEY_RESEND_MS",     "moneyResendMs");
    tuneU(c.tuning.factionSampleMs,   f, "KENSHICOOP_FACTION_SAMPLE_MS",   "factionSampleMs");
    tuneU(c.tuning.factionResendMs,   f, "KENSHICOOP_FACTION_RESEND_MS",   "factionResendMs");
    tuneU(c.tuning.doorSampleMs,      f, "KENSHICOOP_DOOR_SAMPLE_MS",      "doorSampleMs");
    tuneU(c.tuning.doorResendMs,      f, "KENSHICOOP_DOOR_RESEND_MS",      "doorResendMs");
    tuneU(c.tuning.doorEchoHoldMs,    f, "KENSHICOOP_DOOR_ECHO_HOLD_MS",   "doorEchoHoldMs");
    tuneU(c.tuning.prodSampleMs,      f, "KENSHICOOP_PROD_SAMPLE_MS",      "prodSampleMs");
    tuneU(c.tuning.prodResendMs,      f, "KENSHICOOP_PROD_RESEND_MS",      "prodResendMs");
    tuneU(c.tuning.researchSampleMs,  f, "KENSHICOOP_RESEARCH_SAMPLE_MS",  "researchSampleMs");
    tuneU(c.tuning.researchResendMs,  f, "KENSHICOOP_RESEARCH_RESEND_MS",  "researchResendMs");
    tuneU(c.tuning.deedSampleMs,      f, "KENSHICOOP_DEED_SAMPLE_MS",      "deedSampleMs");
    tuneU(c.tuning.deedResendMs,      f, "KENSHICOOP_DEED_RESEND_MS",      "deedResendMs");
    tuneU(c.tuning.buildSampleMs,     f, "KENSHICOOP_BUILD_SAMPLE_MS",     "buildSampleMs");
    tuneU(c.tuning.buildResendMs,     f, "KENSHICOOP_BUILD_RESEND_MS",     "buildResendMs");
    tuneU(c.tuning.bdoorSampleMs,     f, "KENSHICOOP_BDOOR_SAMPLE_MS",     "bdoorSampleMs");
    tuneU(c.tuning.bdoorResendMs,     f, "KENSHICOOP_BDOOR_RESEND_MS",     "bdoorResendMs");
    tuneU(c.tuning.carryHealDebounceMs, f, "KENSHICOOP_CARRY_HEAL_DEBOUNCE_MS",
                                          "carryHealDebounceMs");
    tuneU(c.tuning.furnHealDebounceMs,  f, "KENSHICOOP_FURN_HEAL_DEBOUNCE_MS",
                                          "furnHealDebounceMs");
    tuneU(c.tuning.koReleaseDebounceMs, f, "KENSHICOOP_KO_RELEASE_DEBOUNCE_MS",
                                          "koReleaseDebounceMs");
    tuneU(c.tuning.invResendMs,       f, "KENSHICOOP_INV_RESEND_MS",       "invResendMs");
    tuneU(c.tuning.invResendBigMs,    f, "KENSHICOOP_INV_RESEND_BIG_MS",   "invResendBigMs");
    tuneUInt(c.tuning.invResendBigN,  f, "KENSHICOOP_INV_RESEND_BIG_N",    "invResendBigN");
    tuneUInt(c.tuning.midBandMax,     f, "KENSHICOOP_MID_BAND_MAX",        "midBandMax");
    tuneUInt(c.tuning.midSliceMax,    f, "KENSHICOOP_MID_SLICE_MAX",       "midSliceMax");
    c.carrySync = envOr("KENSHICOOP_CARRY_SYNC", "1") != "0";

    // Furniture occupancy sync (protocol 19, DEFAULT ON): reliable enter/exit
    // edges + self-healing BODY_IN_BED/BODY_IN_CAGE state, executed engine-
    // native (setBedMode/setPrisonMode) between each machine's local pair.
    // "0" is the A/B escape hatch.
    c.furnSync = envOr("KENSHICOOP_FURN_SYNC", "1") != "0";
    // Chained/pole prisoner sync (protocol 41, DEFAULT ON): rides the furniture
    // pipeline as kind=3 (Character::isChained -> setChainedMode). "0" disables
    // just the chain kind (beds/cages keep working).
    c.chainSync = envOr("KENSHICOOP_CHAIN_SYNC", "1") != "0";
    c.stealthSync = envOr("KENSHICOOP_STEALTH_SYNC", "1") != "0";
    c.proneSync   = envOr("KENSHICOOP_PRONE_SYNC", "1") != "0";
    c.moneySync   = envOr("KENSHICOOP_MONEY_SYNC", "1") != "0";
    c.spawnSync   = envOr("KENSHICOOP_SPAWN_SYNC", "1") != "0";
    c.recruitSync = envOr("KENSHICOOP_RECRUIT_SYNC", "1") != "0";
    c.factionSync = envOr("KENSHICOOP_FACTION_SYNC", "1") != "0";
    c.timeSync    = envOr("KENSHICOOP_TIME_SYNC", "1") != "0";
    c.doorSync    = envOr("KENSHICOOP_DOOR_SYNC", "1") != "0";
    c.buildSync   = envOr("KENSHICOOP_BUILD_SYNC", "1") != "0";
    c.bdoorSync   = envOr("KENSHICOOP_BDOOR_SYNC", "1") != "0";
    c.hungerSync  = envOr("KENSHICOOP_HUNGER_SYNC", "1") != "0";
    c.saveSync    = envOr("KENSHICOOP_SAVE_SYNC", "1") != "0";
    c.loadSync    = envOr("KENSHICOOP_LOAD_SYNC", "1") != "0";
    c.prodSync    = envOr("KENSHICOOP_PROD_SYNC", "1") != "0";
    c.researchSync = envOr("KENSHICOOP_RESEARCH_SYNC", "1") != "0";
    c.deedSync    = envOr("KENSHICOOP_DEED_SYNC", "1") != "0";
    c.storeSync   = envOr("KENSHICOOP_STORE_SYNC", "1") != "0";
    c.squadSync   = envOr("KENSHICOOP_SQUAD_SYNC", "1") != "0";
    c.latejoinSync = envOr("KENSHICOOP_LATEJOIN_SYNC", "1") != "0";
    // NOTE: every channel above DEFAULTS ON for real sessions; the diagnostic
    // "*_probe" scenarios (and time_probe/speed_sync) that need a channel OFF to
    // measure the unsynced baseline force it via their manifest DiagEnv (e.g.
    // shop_probe -> KENSHICOOP_MONEY_SYNC=0). This keeps scenario NAMES out of the
    // shipped plugin - the manifest (scripts/scenarios.psd1) is the single source
    // of truth for per-scenario channel A/B; see Contract.Tests's DiagEnv guard.

    // Transport: "udp" (default) keeps the stock ENet/UDP path (the whole local
    // harness runs on it); "steam" tunnels ENet over Steam P2P by SteamID (no
    // port forwarding / CGNAT-immune). steamPeer is the OTHER player's steamid64
    // (two-code exchange); steamPing arms the channel-1 reachability spike.
    c.transport = envOr("KENSHICOOP_TRANSPORT", fileOr(f, "transport", "udp").c_str());
    c.steamPeer = (unsigned long long)_strtoui64(
        envOr("KENSHICOOP_STEAM_PEER", fileOr(f, "steamPeer", "0").c_str()).c_str(), 0, 10);
    c.steamPing = (unsigned long long)_strtoui64(envOr("KENSHICOOP_STEAM_PING", "0").c_str(), 0, 10);

    // In-game panel session control: opt-in legacy auto-start. Default OFF so a
    // panel-driven (env-free) install defers the session to the Connect button;
    // the test harness overrides this in Plugin.cpp (scenario / test-seconds).
    // Accepts "1"/"true" from the file's JSON bool.
    {
        std::string ac = envOr("KENSHICOOP_AUTOCONNECT", fileOr(f, "autoConnect", "0").c_str());
        c.autoConnect = (ac == "1" || ac == "true");
    }

    // Protocol 36 movement-smoothness knobs. Defaults are the historical
    // constants; any positive env value overrides for live A/B tuning.
    {
        c.sendStamp = envOr("KENSHICOOP_SEND_STAMP", "1") != "0";
        int v;
        v = std::atoi(envOr("KENSHICOOP_INTERP_MIN_DELAY_MS", "0").c_str());
        c.interpMinDelayMs = (v > 0) ? (unsigned int)v : 50u;
        v = std::atoi(envOr("KENSHICOOP_INTERP_MAX_DELAY_MS", "0").c_str());
        c.interpMaxDelayMs = (v > 0) ? (unsigned int)v : 200u;
        v = std::atoi(envOr("KENSHICOOP_INTERP_MAX_EXTRAP_MS", "0").c_str());
        c.interpMaxExtrapMs = (v > 0) ? (unsigned int)v : 250u;
        v = std::atoi(envOr("KENSHICOOP_INTERP_MAX_CADENCE_DELAY_MS", "0").c_str());
        c.interpMaxCadenceDelayMs = (v > 0) ? (unsigned int)v : 1200u;
        v = std::atoi(envOr("KENSHICOOP_INTERP_STALE_MS", "0").c_str());
        c.interpStaleMs = (v > 0) ? (unsigned int)v : 2000u;
        double f;
        f = std::atof(envOr("KENSHICOOP_INTERP_SNAP_DIST", "0").c_str());
        c.interpSnapDist = (f > 0.0) ? (float)f : 50.0f;
        // The A/B control for the cadence-scaled buffer is
        // KENSHICOOP_INTERP_MAX_CADENCE_DELAY_MS=200, which pins the ceiling
        // back to interpMaxDelayMs for every tier. K alone cannot: the mid
        // band's ~500 ms cadence still clears 200 ms at any K above 0.4.
        f = std::atof(envOr("KENSHICOOP_INTERP_CADENCE_K", "0").c_str());
        c.interpCadenceK = (f > 0.0) ? (float)f : 2.0f;
        f = std::atof(envOr("KENSHICOOP_CATCHUP_K", "0").c_str());
        c.catchupK = (f > 0.0) ? (float)f : 2.0f;
        f = std::atof(envOr("KENSHICOOP_SNAP_DIST", "0").c_str());
        c.snapDist = (f > 0.0) ? (float)f : 8.0f;
        f = std::atof(envOr("KENSHICOOP_SNAP_SECONDS", "0").c_str());
        c.snapSeconds = (f > 0.0) ? (float)f : 0.75f;
        // Combat convergence bands (0 = keep the drive's ReplicatorUtil default).
        c.combatSoftDist    = (float)std::atof(envOr("KENSHICOOP_COMBAT_SOFT_DIST", "0").c_str());
        c.combatSnapDist    = (float)std::atof(envOr("KENSHICOOP_COMBAT_SNAP_DIST", "0").c_str());
        c.combatBigSnapDist = (float)std::atof(envOr("KENSHICOOP_COMBAT_BIG_SNAP_DIST", "0").c_str());
        c.combatSlideMax    = (float)std::atof(envOr("KENSHICOOP_COMBAT_SLIDE_MAX", "0").c_str());
        c.combatConvergeMs  = (unsigned int)std::atoi(envOr("KENSHICOOP_COMBAT_CONVERGE_MS", "0").c_str());
        // Census radius: "0" (explicit) disables; absent = 2000 u default.
        std::string cr = envOr("KENSHICOOP_CENSUS_RADIUS", "");
        c.censusRadius = cr.empty() ? 2000.0f : (float)std::atof(cr.c_str());
        if (c.censusRadius < 0.0f) c.censusRadius = 0.0f;
        // Census-mint radius: "0" (explicit) disables; absent = 600 u default.
        std::string mr = envOr("KENSHICOOP_SPAWN_MINT_RADIUS", "");
        c.spawnMintRadius = mr.empty() ? 600.0f : (float)std::atof(mr.c_str());
        if (c.spawnMintRadius < 0.0f) c.spawnMintRadius = 0.0f;
        // Census park distance: "0" (explicit) disables; absent = 120 u
        // default. Deliberately ABOVE town-schedule divergence (~50 u for a
        // bar NPC seated at a different stool per sim - run 185524 showed
        // parking those fights the seat AI every frame); a genuinely
        // divergent wanderer (the pack-hidden class) measures 500-900 u.
        // fileOr too, not env only. The game is launched from Steam, so an
        // env-only knob means editing Steam launch options mid-session - the
        // project's own rule for a new tunable ("a coop_config.json key, not
        // just an env var"), which these two missed. They matter more than most:
        // between them they disable the divergence PARK and the census FREEZE,
        // which are the two mechanisms most likely to need turning off from
        // inside a session that has started going wrong. Empty still means 120.
        std::string cp = envOr("KENSHICOOP_CENSUS_PARK",
                               fileOr(cfgFile, "censusPark", "").c_str());
        c.censusParkDist = cp.empty() ? 120.0f : (float)std::atof(cp.c_str());
        if (c.censusParkDist < 0.0f) c.censusParkDist = 0.0f;
        // Attention gate: "0" (explicit) disables - every body counts as
        // observed and reconciliation behaves exactly as it did before the
        // gate existed. Absent = 1000 u default.
        std::string ar = envOr("KENSHICOOP_ATTENTION_RADIUS", "");
        c.attentionRadius = ar.empty() ? 1000.0f : (float)std::atof(ar.c_str());
        if (c.attentionRadius < 0.0f) c.attentionRadius = 0.0f;
        // Presence authority (protocol 49). DEFAULT ON as of v0.47: it has run
        // split_far2 and run_apart green and a long manual session, and with it
        // off the join drives a whole town's population off the host's stream
        // instead of authoring the bodies it is standing next to. "0" restores
        // unconditional host authority.
        //
        // The scenario tier does NOT inherit this default. Set-CoopDiagEnv
        // (scripts/CoopHarness.psm1) pins KENSHICOOP_CELL_AUTH=0 for every
        // scenario that does not ask for it, which is what keeps the tier a
        // fail-open proof rather than a co-test of this flag.
        c.cellAuth = envOr("KENSHICOOP_CELL_AUTH", "1") != "0";
        // Census-band AI freeze: quiesce a diverging census-band body's local
        // AI so it can't flee/aggro the join's guards. DEFAULT ON; the A/B
        // escape hatch restores the position-park-only behavior.
        c.censusFreezeAi = envOr("KENSHICOOP_CENSUS_FREEZE_AI",
                                 fileOr(cfgFile, "censusFreezeAi", "1").c_str()) != "0";
        // Auth step 4 (docs/AUTHORITY-DESIGN.md): the assertion overlay's read
        // mode. Strings, because "0/1/2" in a player's config file is a riddle:
        //   off    - cell verdict only, bit-identical to v0.66
        //   shadow - cell verdict rules; divergence is counted ([auth] rollup)
        //   on     - an asserted owner overrides the cell verdict
        // The word-form doubles as the rollback lever step 6 pre-authorizes:
        // "authAssert": "off" in coop_config.json, no re-release needed.
        {
            std::string am = envOr("KENSHICOOP_AUTH_ASSERT",
                                   fileOr(cfgFile, "authAssert", "on").c_str());
            c.authAssert = (am == "on") ? 2 : (am == "off" || am == "0") ? 0 : 1;
        }
        // Ceiling on the arbitrated game speed. 0 keeps today's behaviour
        // (uncapped). Exists because the crash correlate is now 4 sightings
        // with 3 at 5x - all in Kenshi's own frame-end, i.e. the engine
        // presenting under quintuple sim load - and a pair who keep losing
        // sessions to it deserve a one-line way to fence it off:
        // "speedMax": 2 in coop_config.json.
        {
            std::string sm = envOr("KENSHICOOP_SPEED_MAX",
                                   fileOr(cfgFile, "speedMax", "0").c_str());
            c.speedMax = (float)std::atof(sm.c_str());
            if (c.speedMax < 0.0f) c.speedMax = 0.0f;
        }
        // Camera-anchored interest (protocol 43): fold the local camera +
        // peer camera hint into the interest anchors. DEFAULT ON; the A/B
        // escape hatch restores tab-leader-only anchors.
        c.camInterest = envOr("KENSHICOOP_CAM_INTEREST", "1") != "0";
        // Task-selection observation spike: OFF by default (diagnostic only).
        c.taskSelectSpike = envOr("KENSHICOOP_TASK_SPIKE", "0") != "0";
        // Jail put-to-work desync spike: OFF by default (diagnostic only).
        c.jailProbe = envOr("KENSHICOOP_JAIL_PROBE", "0") != "0";
        // Jail put-to-work observation spike (Phase A): OFF by default.
        c.jailObserve = envOr("KENSHICOOP_JAIL_OBSERVE", "0") != "0";
        // Starve hold: "0" (explicit) restores legacy release-on-stale;
        // absent = 10 s guard-hold default.
        std::string sh = envOr("KENSHICOOP_STARVE_HOLD_MS", "");
        int shv = sh.empty() ? 10000 : std::atoi(sh.c_str());
        c.starveHoldMs = (shv > 0) ? (unsigned int)shv : 0u;
    }

    int delay  = std::atoi(envOr("KENSHICOOP_NETSIM_DELAY_MS", "0").c_str());
    int jitter = std::atoi(envOr("KENSHICOOP_NETSIM_JITTER_MS", "0").c_str());
    int loss   = std::atoi(envOr("KENSHICOOP_NETSIM_LOSS_PCT", "0").c_str());
    c.netSimDelayMs  = (delay  > 0) ? (unsigned int)delay  : 0u;
    c.netSimJitterMs = (jitter > 0) ? (unsigned int)jitter : 0u;
    c.netSimLossPct  = (loss   > 0) ? (unsigned int)(loss > 100 ? 100 : loss) : 0u;

    c.fakeClockSkewMs = (long)std::atoi(envOr("KENSHICOOP_FAKE_CLOCK_SKEW_MS", "0").c_str());

    int armTimeout = std::atoi(envOr("KENSHICOOP_ARM_TIMEOUT_MS", "45000").c_str());
    c.scenarioArmTimeoutMs = (armTimeout > 0) ? (unsigned long)armTimeout : 0ul;

    // Ownership squad-tab ranks: parse a CSV of unsigned ints (e.g. "0", "1", "1,2").
    // KENSHICOOP_OWN_SQUAD is primary; KENSHICOOP_OWN_RANK is an accepted alias. Empty
    // env -> default (host owns tab {0} / join owns tab {1}).
    c.ownRanks.clear();
    {
        std::string ranks = envOr("KENSHICOOP_OWN_SQUAD", envOr("KENSHICOOP_OWN_RANK", "").c_str());
        c.ownRanksFromEnv = parseRankList(ranks, c.ownRanks);
        resolveOwnRanks(c.ownRanks, c.isHost, c.ownRanksFromEnv);
    }
}

std::string describeConfig(const Config& c) {
    // Compact "on/off" channel roster + the knobs most likely to explain a
    // divergence. Kept on one line so it is greppable in the log.
    std::string s = "KenshiCoop: effective cfg";
    s += " scenario='" + c.scenario + "'";
    if (!c.setupScene.empty()) s += " setup='" + c.setupScene + "'";
    // transportCfg, not transport: this line reports what the CONFIG asked
    // for, and the negotiated reality can differ (Steam demoted to UDP by a
    // missing peer id, a failed steamp2p init, or hooks failing on the net
    // thread). The negotiated answer lives in the [net] transport= line and
    // the panel's Connected suffix. On 2026-08-10 this field, read as fact,
    // produced a wrong "you switched to Steam" call - name intent as intent.
    s += " transportCfg=" + c.transport;
    struct Flag { const char* name; bool on; };
    const Flag flags[] = {
        { "inv",     c.invSync },      { "xfer",    c.xferSync },
        { "blockXfer", c.blockXfer },  { "world",   c.worldSync },
        { "med",     c.medSync },      { "speed",   c.speedSync },
        { "speedCombatCap", c.speedCombatCap },
        { "stats",   c.statsSync },    { "carry",   c.carrySync },
        { "crashTracer", c.crashTracer },
        { "furn",    c.furnSync },     { "chain",   c.chainSync },
        { "stealth", c.stealthSync },  { "prone",   c.proneSync },
        { "money",   c.moneySync },
        { "spawn",   c.spawnSync },    { "recruit", c.recruitSync },
        { "faction", c.factionSync },  { "time",    c.timeSync },
        { "door",    c.doorSync },     { "build",   c.buildSync },
        { "bdoor",   c.bdoorSync },    { "hunger",  c.hungerSync },
        { "save",    c.saveSync },     { "load",    c.loadSync },
        { "prod",    c.prodSync },     { "research",c.researchSync },
        { "deed",    c.deedSync },
        { "store",   c.storeSync },    { "squad",   c.squadSync },
        { "latejoin",c.latejoinSync }, { "aiSuspend", c.aiSuspend },
        { "gateAuth",c.gateAuthority },{ "camInterest", c.camInterest },
        { "censusFreezeAi", c.censusFreezeAi },
        { "splitAuth", c.splitAuthority },
        { "dmgFloat", c.damageFloaters },
        { "weather",  c.weatherSync },
        { "dialogue", c.dialogueSync },
    };
    s += " on=[";
    bool first = true;
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        if (!flags[i].on) continue;
        if (!first) s += ",";
        s += flags[i].name;
        first = false;
    }
    s += "] off=[";
    first = true;
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        if (flags[i].on) continue;
        if (!first) s += ",";
        s += flags[i].name;
        first = false;
    }
    s += "]";
    char b[192];
    _snprintf(b, sizeof(b) - 1,
              " censusR=%.0f mintR=%.0f park=%.0f snap=%.1f/%.2fs armMs=%lu"
              " attnR=%.0f cellAuth=%d authAssert=%s",
              c.censusRadius, c.spawnMintRadius, c.censusParkDist,
              c.snapDist, c.snapSeconds, c.scenarioArmTimeoutMs,
              c.attentionRadius, c.cellAuth ? 1 : 0,
              (c.authAssert == 2) ? "on" : (c.authAssert == 0) ? "off"
                                                               : "shadow");
    if (c.speedMax > 0.0f) {
        char sm[32];
        _snprintf(sm, sizeof(sm) - 1, " speedMax=%.1f", c.speedMax);
        sm[sizeof(sm) - 1] = '\0';
        s += sm;
    }
    b[sizeof(b) - 1] = '\0';
    s += b;
    // The tunables that change how much of the world actually reaches the peer, and
    // the debounces that decide whether a self-heal can undo a reliable event. Both
    // classes have produced player-visible bugs at their old values, so a session log
    // has to say what they were set to.
    char t[192];
    _snprintf(t, sizeof(t) - 1,
              " midBand=%u/%u healDeb=%lu/%lu doorHold=%lu koRel=%lu",
              c.tuning.midBandMax, c.tuning.midSliceMax,
              c.tuning.carryHealDebounceMs, c.tuning.furnHealDebounceMs,
              c.tuning.doorEchoHoldMs, c.tuning.koReleaseDebounceMs);
    t[sizeof(t) - 1] = '\0';
    s += t;
    return s;
}

void reloadPeerFromFile(Config& c) {
    // Re-read only the connection TARGET (friend code + UDP endpoint) from
    // coop_config.json, so editing the file then hitting Connect in the panel
    // takes effect without a game restart. Role/transport come from the panel
    // toggles at Connect time and are left untouched here.
    std::map<std::string, std::string> f = readConfigFile();
    std::map<std::string, std::string>::const_iterator it;
    it = f.find("steamPeer");
    if (it != f.end() && !it->second.empty())
        c.steamPeer = (unsigned long long)_strtoui64(it->second.c_str(), 0, 10);
    it = f.find("ip");
    if (it != f.end() && !it->second.empty()) c.ip = it->second;
    it = f.find("port");
    if (it != f.end() && !it->second.empty()) c.port = std::atoi(it->second.c_str());
}

} // namespace coop
