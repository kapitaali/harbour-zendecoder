#pragma once
/// @file
/// OmniScan version macros.
///
/// Keep in sync with CMake PROJECT_VERSION — enforced by
/// `test_capi` ("version matches CMake project"), which compares these
/// macros against OMNISCAN_PROJECT_VERSION injected by the build system,
/// and by the release-consistency step in `scripts/run_tests.sh`
/// (CMakeLists / version.h / RPM spec / changelog heading).
/// Proven to fail when desynced: set VERSION_MINOR back by one and the
/// check turns red (PROGRESS "version sync guard").
#define OMNISCAN_VERSION_MAJOR 0
#define OMNISCAN_VERSION_MINOR 5
#define OMNISCAN_VERSION_PATCH 0
#define OMNISCAN_VERSION_STRING "0.5.0"
