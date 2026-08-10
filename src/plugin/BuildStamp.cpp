// See BuildStamp.h. This TU is compiled UNCONDITIONALLY by both build scripts
// (exempted by name from their incremental skip) so the stamp below is the
// link's, not a stale compile's. Keep it tiny: its whole job is to be cheap
// enough that rebuilding it every run costs nothing.

#include "BuildStamp.h"

#ifdef KENSHICOOP_HAVE_DEPS_PIN
#include "DepsPin.h"
#endif

const char* kcBuildStamp() {
#ifdef KENSHICOOP_BUILD_STAMP
    return KENSHICOOP_BUILD_STAMP;
#else
    return "unstamped";
#endif
}
