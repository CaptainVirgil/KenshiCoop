// BuildStamp - the one honest answer to "which build is this?".
//
// The startup banner used to print __DATE__/__TIME__, which is stamped when
// the TU containing it was last COMPILED, not when the DLL was linked. Both
// build scripts are incremental, so the stamp froze across builds that changed
// the DLL - on 2026-08-10 it mislabelled three consecutive releases with one
// time and produced two wrong "which build is he running?" calls in a live
// debugging session.
//
// kcBuildStamp() lives in BuildStamp.cpp, which BOTH build scripts compile
// unconditionally on every run (it is exempted by name from the incremental
// skip), reading KENSHICOOP_BUILD_STAMP out of the generated DepsPin.h that
// is likewise rewritten on every run. The sha256 of the DLL remains the only
// cryptographic identity; this is the human-readable one.

#ifndef KENSHICOOP_BUILDSTAMP_H
#define KENSHICOOP_BUILDSTAMP_H

// "YYYY-MM-DD HH:MM:SS <repo-describe>" of the build that linked this DLL,
// or "unstamped" when built outside the scripts (bare MSBuild).
const char* kcBuildStamp();

#endif // KENSHICOOP_BUILDSTAMP_H
