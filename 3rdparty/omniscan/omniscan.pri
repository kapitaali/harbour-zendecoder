# libomniscan v0.2.0 — vendored static, backend ON (single decode stack).
# Source: ~/Jolla/libomniscan commit b52ab7a (Apache-2.0, see LICENSE),
# pristine subset: include/omniscan/**, src/{core,backend_zxing,formats,
# payload}/**. Excluded: capi/ + io/ (app uses the C++ API and Qt image
# loading), tests/, tools/, bindings/, docs/, cmake/, third_party/.
# Verify with: git -C <lib> archive b52ab7a <same paths> | tar -t | diff.
# Regenerate on bump; OMNISCAN_WITH_ZXING selects the zxing adapter's real
# backend (needs 3rdparty/zxing-cpp on the include path, see below).

OMNISCAN_ROOT = $$PWD

INCLUDEPATH += $$OMNISCAN_ROOT/include

# Backend selection: real zxing-cpp adapter (Grade-1 formats). Without it
# the Grade-1 bits report BackendNotAvailable and only the native Tier-2
# decoders answer.
DEFINES += OMNISCAN_WITH_ZXING

SOURCES += \
    $$OMNISCAN_ROOT/src/core/binarizer.cpp \
    $$OMNISCAN_ROOT/src/core/decode.cpp \
    $$OMNISCAN_ROOT/src/core/despeckle.cpp \
    $$OMNISCAN_ROOT/src/core/geometry.cpp \
    $$OMNISCAN_ROOT/src/core/image.cpp \
    $$OMNISCAN_ROOT/src/core/locator.cpp \
    $$OMNISCAN_ROOT/src/core/result.cpp \
    $$OMNISCAN_ROOT/src/core/symbology.cpp \
    $$OMNISCAN_ROOT/src/backend_zxing/zxing_adapter.cpp \
    $$OMNISCAN_ROOT/src/formats/linear/itf.cpp \
    $$OMNISCAN_ROOT/src/formats/linear/linear_scan.cpp \
    $$OMNISCAN_ROOT/src/formats/linear/msi.cpp \
    $$OMNISCAN_ROOT/src/formats/linear/native_linear.cpp \
    $$OMNISCAN_ROOT/src/formats/linear/pharmacode.cpp \
    $$OMNISCAN_ROOT/src/formats/linear/plessey.cpp \
    $$OMNISCAN_ROOT/src/formats/linear/telepen.cpp \
    $$OMNISCAN_ROOT/src/formats/postal/auspost.cpp \
    $$OMNISCAN_ROOT/src/formats/postal/deutsche_post.cpp \
    $$OMNISCAN_ROOT/src/formats/postal/fourstate.cpp \
    $$OMNISCAN_ROOT/src/formats/postal/imb.cpp \
    $$OMNISCAN_ROOT/src/formats/postal/japan_post.cpp \
    $$OMNISCAN_ROOT/src/formats/postal/native_postal.cpp \
    $$OMNISCAN_ROOT/src/formats/postal/rm4scc.cpp \
    $$OMNISCAN_ROOT/src/payload/gs1_ai.cpp \
    $$OMNISCAN_ROOT/src/payload/mecard.cpp \
    $$OMNISCAN_ROOT/src/payload/otpauth.cpp \
    $$OMNISCAN_ROOT/src/payload/sniff.cpp \
    $$OMNISCAN_ROOT/src/payload/swissqr.cpp \
    $$OMNISCAN_ROOT/src/payload/vcard.cpp \
    $$OMNISCAN_ROOT/src/payload/vin.cpp \
    $$OMNISCAN_ROOT/src/payload/wifi.cpp
