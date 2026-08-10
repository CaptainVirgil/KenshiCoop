// CoopLog - tiny thread-safe file logger for KenshiCoop.
//
// Why a separate logger when KenshiLib already has DebugLog/ErrorLog?
//   * Those route into the engine's kenshi.log, which is overwritten each
//     launch and interleaved with heavy engine spam - awkward for an automated
//     test runner to evaluate.
//   * KenshiLib's UI/log helpers are NOT safe to call off the main thread,
//     whereas this logger guards a FILE* with a CRITICAL_SECTION so the net
//     thread can write too.
//
// Output is a dedicated, per-line-flushed file (so it survives a hard kill),
// with a timestamp + mode tag (HOST/JOIN) on every line. The high-level
// coopLog()/coopErr() wrappers in KenshiCoop.cpp call BOTH this and the
// KenshiLib helpers, so events still appear in kenshi.log as before.

#ifndef KENSHICOOP_COOPLOG_H
#define KENSHICOOP_COOPLOG_H

namespace coop {

// Open the log file at 'path' (truncating any previous run) and remember a
// short mode tag (e.g. "HOST"/"JOIN"). Safe to call once at plugin load.
void logInit(const char* path, const char* modeTag);

// The wall clock every timestamp in this plugin derives from: milliseconds
// since local midnight PLUS the injected fake skew (see logSetFakeSkewMs).
// Both the log-line "[HH:MM:SS.mmm]" stamps AND the wire time-sync packets
// (NetLink CLOCKSYNC) read THIS function, so an injected skew shifts them
// together - which is exactly what the clock-skew validation relies on: the
// join's log stamps drift by +S while its estimated host-offset reads -S, and
// the oracles' offset correction must recover alignment. Thread-safe.
unsigned long wallClockMs();

// Inject a fake wall-clock skew (ms, may be negative). Set once at startup from
// KENSHICOOP_FAKE_CLOCK_SKEW_MS (join only) BEFORE logInit. 0 = real clock.
void logSetFakeSkewMs(long skewMs);

// Monotonic MICROSECOND counter, for measuring work INSIDE one frame.
//
// Deliberately separate from wallClockMs() and never expressed in terms of it.
// unsigned long is 32-bit under MSVC x64, so a microsecond counter wraps about
// every 71 minutes - harmless for sub-frame deltas under unsigned subtraction,
// and fatal for the ms deltas this codebase compares over minutes (30 s target
// age-out, 60 s inventory forget, 60 s keepalive prune). Microseconds because
// the thing being measured is a budget of one or two MILLIseconds; GetTickCount
// (~15 ms granularity, the only clock Plugin.cpp had) cannot see it at all.
// Thread-safe.
unsigned long monoUs();

// Append one INFO/ERROR line (timestamped, tagged) and flush. Thread-safe.
void logLine(const char* msg);
void logErrLine(const char* msg);

// ---- Main-thread liveness watchdog -----------------------------------------
//
// A main-thread HANG and a crash are indistinguishable in this log: in both, the
// lines simply stop. That cost a real diagnosis on 2026-08-10 - the host's log
// ended mid-session and was read as a crash, when in fact the main thread froze
// and the ENet thread went on logging `[net] bandwidth` alone for 28 more
// seconds (out collapsed 5.4 -> 0.3 KB/s while in held at ~5, because nothing
// was draining the inbound queue).
//
// That asymmetry is the tool: the network thread keeps running when the main
// thread does not, so it can witness and NAME the stall. The main thread stamps
// a heartbeat and a phase label; the net thread's existing 5 s tick reports any
// gap. `phase` must be a string LITERAL - it is stored by pointer and read from
// another thread, so it has to outlive the call.
void mainThreadBeat(const char* phase);
// 0 when the main thread has never beaten (pre-hook), else ms since it last did.
unsigned long mainThreadStalledMs();
// The phase label in force at the last beat - i.e. what it was doing when it
// stopped. Never null.
const char* mainThreadPhase();
// Monotonic beat count, so a stall report can distinguish "frozen" from "slow".
unsigned long mainThreadBeats();

// Re-point the log at a new path and mode tag, mid-session.
//
// The role a player actually plays is chosen in the F2 panel at Connect time,
// but the log file is opened at plugin load from the CONFIGURED role - so a
// player whose config said "host" and who then pressed JOIN wrote their whole
// session into KenshiCoop_host.log. That trap was documented twice (CLAUDE.md,
// PLAYING.md) instead of fixed, and it misleads everyone who reads a log,
// including the person who wrote the warning.
//
// Flushes and closes, renames the file on disk (so the run so far is not lost),
// and reopens in APPEND mode. Every failure path falls back to reopening the
// ORIGINAL path in append mode: a log that ends up with the wrong name is a
// nuisance, a session that silently stops logging is not. Returns true when the
// file now lives at newPath. No-op if the path is unchanged.
bool logRetag(const char* newPath, const char* newTag);

// Flush and close the file. Called right before ExitProcess on test self-exit.
void logClose();

} // namespace coop

#endif // KENSHICOOP_COOPLOG_H
