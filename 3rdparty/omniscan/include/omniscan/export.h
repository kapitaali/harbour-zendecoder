#pragma once
// Symbol export control for shared builds (M9, found by the Sailfish RPM
// build). The library compiles with -fvisibility=hidden, so every symbol a
// consumer may link against must be marked OMNISCAN_API. Without this the
// shared lib exports only the C ABI and C++ consumers fail to link.
//
//   OMNISCAN_STATIC      defined by the build system for static builds
//                        (nothing to export; nothing is annotated).
//   OMNISCAN_BUILDING_CAPI defined by the build system only while compiling
//                        the library itself (dllexport on Windows).
//
// C-compatible (no C++ syntax) so omniscan_c.h can reuse it.

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(OMNISCAN_STATIC)
#    define OMNISCAN_API
#  elif defined(OMNISCAN_BUILDING_CAPI)
#    define OMNISCAN_API __declspec(dllexport)
#  else
#    define OMNISCAN_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define OMNISCAN_API __attribute__((visibility("default")))
#else
#  define OMNISCAN_API
#endif
