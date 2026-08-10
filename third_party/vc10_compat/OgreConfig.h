// Shim: SHADOWS the OgreConfig.h the vendored deps DO ship - every build path
// (build_plugin.cmd, build_plugin_direct.ps1, vcenv.sh) lists vc10_compat
// before KenshiLib/Include/ogre. The shipped file pulls OgreBuildSettings.h,
// whose CMake-generated `#define OGRE_USE_SIMD 1` would put OgreArrayConfig.h
// on its SSE2 branch (ArrayReal = __m128, ARRAY_PACKED_REALS 4). OGRE_USE_SIMD 0
// keeps it scalar.
// NOTE: Ogre::Aabb's 24-byte two-Vector3 POD layout (Building::AABB at
// 0x27C..0x294) does NOT depend on these macros - the vendored
// Math/Simple/OgreAabb.h proxy has its SIMD redirect commented out and includes
// C/OgreAabb.h unconditionally. Delete this shim and that layout survives, but
// the SSE2 ArrayMath types come back into every TU that includes Building.h /
// NavMesh.h / ZoneManager.h. Keep the shim, and keep vc10_compat ahead of
// Include/ogre in every build path's INCLUDE order.
#ifndef KENSHICOOP_SHIM_OGRE_CONFIG_H
#define KENSHICOOP_SHIM_OGRE_CONFIG_H

#define OGRE_DOUBLE_PRECISION 0
#define OGRE_USE_SIMD 0

#endif
