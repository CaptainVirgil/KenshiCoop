// CoopLog implementation. See CoopLog.h for rationale.
//
// VS2010 (v100) compatible: Win32 CRITICAL_SECTION + GetLocalTime, plain stdio.

#define _CRT_SECURE_NO_WARNINGS 1

#include "CoopLog.h"
#include <string>

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace coop {
namespace {

FILE*            g_fp   = 0;
CRITICAL_SECTION g_cs;
bool             g_init = false;
char             g_tag[16] = { 0 };
char             g_curPath[512] = { 0 };  // what logRetag renames FROM
volatile long    g_fakeSkewMs = 0;

void writeLine(const char* level, const char* msg) {
    if (!g_init) return;
    EnterCriticalSection(&g_cs);
    if (g_fp) {
        // Derive the stamp from wallClockMs() (real clock + injected skew) so
        // log timestamps and the wire time-sync share one clock.
        unsigned long ms = wallClockMs();
        unsigned long hh = (ms / 3600000ul) % 24ul;
        unsigned long mm = (ms / 60000ul) % 60ul;
        unsigned long ss = (ms / 1000ul) % 60ul;
        unsigned long mmm = ms % 1000ul;
        std::fprintf(g_fp, "[%02lu:%02lu:%02lu.%03lu] [%s] %s: %s\n",
                     hh, mm, ss, mmm,
                     g_tag, level, msg ? msg : "");
        std::fflush(g_fp);
    }
    LeaveCriticalSection(&g_cs);
}

} // namespace

unsigned long wallClockMs() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    long ms = (long)((((unsigned long)st.wHour * 60ul + st.wMinute) * 60ul + st.wSecond) * 1000ul
                     + st.wMilliseconds);
    ms += g_fakeSkewMs;
    // Wrap into [0, 24h) so a skew across midnight still formats sanely.
    const long DAY = 24l * 3600l * 1000l;
    ms %= DAY;
    if (ms < 0) ms += DAY;
    return (unsigned long)ms;
}

void logSetFakeSkewMs(long skewMs) { g_fakeSkewMs = skewMs; }

unsigned long monoUs() {
    // QueryPerformanceCounter, reduced to microseconds against a first-call
    // origin so the value stays small and the 32-bit wrap is ~71 minutes of
    // uptime rather than an arbitrary boundary. Only ever used for deltas
    // within a frame, where unsigned subtraction makes the wrap harmless.
    static LARGE_INTEGER freq;
    static LARGE_INTEGER origin;
    static int inited = 0;
    if (!inited) {
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
            inited = -1;               // no QPC: report 0 and let callers no-op
        } else {
            QueryPerformanceCounter(&origin);
            inited = 1;
        }
    }
    if (inited != 1) return 0ul;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // Scale before dividing by frequency, but in 64-bit so a multi-hour uptime
    // cannot overflow the intermediate.
    __int64 ticks = now.QuadPart - origin.QuadPart;
    return (unsigned long)((ticks * 1000000i64) / freq.QuadPart);
}

void logInit(const char* path, const char* modeTag) {
    if (g_init) return;
    InitializeCriticalSection(&g_cs);
    g_init = true;

    if (modeTag) {
        size_t i = 0;
        for (; modeTag[i] && i < sizeof(g_tag) - 1; ++i) g_tag[i] = modeTag[i];
        g_tag[i] = '\0';
    }

    if (path && path[0]) {
        // Keep ONE previous run. The log was truncated on every plugin load, so the
        // usual sequence after a crash - relaunch Kenshi to look at what happened -
        // destroyed the evidence before anyone could read it, and the crash is
        // exactly the run whose log matters most.
        {
            std::string prev(path);
            prev += ".prev";
            std::remove(prev.c_str());
            std::rename(path, prev.c_str()); // fails harmlessly on the first run
        }
        g_fp = std::fopen(path, "w");
        size_t pi = 0;
        for (; path[pi] && pi < sizeof(g_curPath) - 1; ++pi) g_curPath[pi] = path[pi];
        g_curPath[pi] = '\0';
    }
    writeLine("INFO", "log opened");
}

bool logRetag(const char* newPath, const char* newTag) {
    if (!g_init || !newPath || !newPath[0]) return false;
    EnterCriticalSection(&g_cs);
    bool moved = false;
    if (g_curPath[0] && std::strcmp(g_curPath, newPath) != 0) {
        if (g_fp) { std::fflush(g_fp); std::fclose(g_fp); g_fp = 0; }
        // Take the .prev slot with us, so the "keep one previous run" rule still
        // holds under the new name rather than leaving an orphan beside the old.
        {
            std::string oldPrev(g_curPath); oldPrev += ".prev";
            std::string newPrev(newPath);   newPrev += ".prev";
            std::remove(newPrev.c_str());
            std::rename(oldPrev.c_str(), newPrev.c_str()); // harmless if absent
        }
        std::remove(newPath);                       // rename() will not overwrite
        moved = (std::rename(g_curPath, newPath) == 0);
        // Append, never "w": the run so far is the point. If the rename failed the
        // file is still at the old path and that is where we keep writing - losing
        // the rest of a session to a naming nicety would be the worse bug.
        const char* open_at = moved ? newPath : g_curPath;
        g_fp = std::fopen(open_at, "a");
        if (!g_fp && moved) {                       // renamed but unopenable
            g_fp = std::fopen(g_curPath, "a");
            moved = false;
        }
        if (moved) {
            size_t i = 0;
            for (; newPath[i] && i < sizeof(g_curPath) - 1; ++i) g_curPath[i] = newPath[i];
            g_curPath[i] = '\0';
        }
    }
    if (newTag) {
        size_t i = 0;
        for (; newTag[i] && i < sizeof(g_tag) - 1; ++i) g_tag[i] = newTag[i];
        g_tag[i] = '\0';
    }
    LeaveCriticalSection(&g_cs);
    return moved;
}

void logLine(const char* msg)    { writeLine("INFO",  msg); }
void logErrLine(const char* msg) { writeLine("ERROR", msg); }

// ---- Main-thread liveness watchdog (see CoopLog.h) --------------------------
// Deliberately lock-free: the beat is on the game's hot path and must not
// contend with the net thread's logging critical section. Reads on the other
// side are diagnostic - a torn or one-tick-stale value changes a number in a
// log line, never a decision - so aligned 32-bit volatile writes are enough,
// and are what this toolset offers anyway (<atomic> does not ship with VC10).
// g_beatPhase is a literal pointer by contract, so publishing it is one store.
namespace {
volatile unsigned long g_beatMs    = 0;
volatile unsigned long g_beatCount = 0;
const char* volatile   g_beatPhase = "pre-hook";
}

void mainThreadBeat(const char* phase) {
    if (phase) g_beatPhase = phase;
    ++g_beatCount;
    // Stamped LAST: a reader that sees a fresh timestamp has necessarily seen
    // the phase and count that go with it.
    g_beatMs = wallClockMs();
}

unsigned long mainThreadStalledMs() {
    const unsigned long beat = g_beatMs;
    if (beat == 0) return 0;              // never beaten - not a stall
    const unsigned long now = wallClockMs();
    return (now > beat) ? (now - beat) : 0;
}

const char* mainThreadPhase() {
    const char* p = g_beatPhase;
    return p ? p : "?";
}

unsigned long mainThreadBeats() { return g_beatCount; }

void logClose() {
    if (!g_init) return;
    EnterCriticalSection(&g_cs);
    if (g_fp) {
        std::fflush(g_fp);
        std::fclose(g_fp);
        g_fp = 0;
    }
    LeaveCriticalSection(&g_cs);
}

} // namespace coop
