// Shim: SHADOWS the OgrePlatformInformation.h the vendored deps DO ship (the
// include order in every build path puts vc10_compat first). The shipped
// header's first act is `#include "OgrePrerequisites.h"` - the whole Ogre core
// web - and this empty stand-in is what keeps that web out of every TU that
// includes Building.h/NavMesh.h/ZoneManager.h. Empty suffices because its only
// consumers are OgreArrayConfig.h's OGRE_CPU checks, all inside the
// `#if OGRE_USE_SIMD == 1` branch the OgreConfig.h shim disables.
#ifndef KENSHICOOP_SHIM_OGRE_PLATFORM_INFORMATION_H
#define KENSHICOOP_SHIM_OGRE_PLATFORM_INFORMATION_H
#endif
