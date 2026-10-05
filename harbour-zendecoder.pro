# harbour-zendecoder.pro

TARGET = harbour-zendecoder
CONFIG += sailfishapp

# The vendored zxing-cpp (3rdparty/) needs C++20 (std::numbers, ranges,
# starts_with) — and GCC 13 in the build engine has it. An explicit flag,
# not CONFIG += c++11: the last -std on the command line wins, and qmake's
# c++11 feature would otherwise squeeze gnu++0x in ahead of this one.
QMAKE_CXXFLAGS += -std=gnu++20

# Seed builds ship as full Pro (see src/trialmanager.h). Remove the define
# to enforce the 14-day trial + Ko-fi license key.
DEFINES += PRO_SEED_BUILD

# Build identifier for the About page (lets testers tell a stale binary
# from a fresh one when the RPM version hasn't moved). Prefers the git
# hash, falls back to a UTC timestamp outside a repo.
BUILD_ID = $$system(git -C $$PWD rev-parse --short HEAD 2>/dev/null)
isEmpty(BUILD_ID): BUILD_ID = $$system(date -u +%Y%m%d-%H%M%S)
DEFINES += BUILD_ID=\\\"$$BUILD_ID\\\"

# Version shown on the About page: the RPM build exports APP_VERSION (the
# sfdk/git-tag version) in %build, manual qmake runs may pass it as an
# argument, and anything else is a hand-built binary.
isEmpty(APP_VERSION): APP_VERSION = $$getenv(APP_VERSION)
isEmpty(APP_VERSION): APP_VERSION = dev
DEFINES += APP_VERSION=\\\"$$APP_VERSION\\\"

# make cannot tell that -DAPP_VERSION/-DBUILD_ID changed (compiler flags are
# invisible to it), so an incremental build would keep the previous values
# baked into the binary. Bump main()'s timestamp so it recompiles with the
# current defines; it is a single translation unit, so this costs a second
# at most.
_version_touch = $$system(touch $$PWD/src/harbour-zendecoder.cpp)

# Ko-fi shop URL for Pro. Single constant so a Harbour-targeted build can
# ship with it emptied (shows "Pro coming soon") while Chum builds link out.
# Override at build time: qmake KOFI_URL=https://ko-fi.com/...
isEmpty(KOFI_URL): KOFI_URL = https://ko-fi.com/kapitaali
DEFINES += KOFI_URL=\\\"$$KOFI_URL\\\"

# No Qt5Concurrent: the static decode runs on its own worker thread
# (src/staticdecoder.*) instead of QThreadPool/QtConcurrent, whose
# qfutureinterface.h does not survive this toolchain's C++20 mode next to
# Qt 5.6 headers.
QT += core gui qml quick sql multimedia dbus

# libomniscan v0.2.0 (Apache-2.0) with its vendored zxing-cpp v2.3.0
# backend (3rdparty/omniscan + 3rdparty/zxing-cpp) — compiled into the
# binary, single RPM, no extra dependency. See 3rdparty/omniscan/omniscan.pri
# and 3rdparty/zxing-cpp/zxing-cpp.pri for the file lists.
include(3rdparty/omniscan/omniscan.pri)
include(3rdparty/zxing-cpp/zxing-cpp.pri)

SOURCES += \
    src/harbour-zendecoder.cpp \
    src/decoder.cpp \
    src/staticdecoder.cpp \
    src/history.cpp \
    src/settings.cpp \
    src/trialmanager.cpp \
    src/productlookup.cpp

HEADERS += \
    src/decoder.h \
    src/formatgroups.h \
    src/staticdecoder.h \
    src/history.h \
    src/settings.h \
    src/trialmanager.h \
    src/productlookup.h

# sailfishapp.prf installs the whole qml/ tree; this list exists so the IDE
# and qmake know about the files (and so OTHER_FILES below is complete).
QML_FILES = \
    qml/harbour-zendecoder.qml \
    qml/cover/CoverPage.qml \
    qml/components/Toast.qml \
    qml/pages/ScannerPage.qml \
    qml/pages/ResultPage.qml \
    qml/pages/HistoryPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/AboutPage.qml

OTHER_FILES += \
    harbour-zendecoder.desktop \
    rpm/harbour-zendecoder.spec \
    PRIVACY.md \
    qml/img/harbour-zendecoder.png \
    $$QML_FILES

# Names only, not paths: sailfishapp.prf expands each entry to
# icons/<size>/<TARGET>.png and installs it into icons/hicolor/<size>/apps.
SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172 256x256

# Privacy policy ships with the package (the store listing references it;
# the repo copy at github.com/kapitaali/harbour-zendecoder is the URL
# version of the same file).
privacy.files = PRIVACY.md
privacy.path = /usr/share/harbour-zendecoder
INSTALLS += privacy
