# harbour-zendecoder.pro

TARGET = harbour-zendecoder
CONFIG += sailfishapp c++11

# Seed builds ship as full Pro (see src/trialmanager.h). Remove the define
# to enforce the 14-day trial + Ko-fi license key.
DEFINES += PRO_SEED_BUILD

# Version shown on the About page: the RPM build exports APP_VERSION (the
# sfdk/git-tag version) in %build, manual qmake runs may pass it as an
# argument, and anything else is a hand-built binary.
isEmpty(APP_VERSION): APP_VERSION = $$getenv(APP_VERSION)
isEmpty(APP_VERSION): APP_VERSION = dev
DEFINES += APP_VERSION=\\\"$$APP_VERSION\\\"

# make cannot tell that -DAPP_VERSION changed (compiler flags are invisible
# to it), so an incremental build would keep the previous version baked into
# the binary. Bump main()'s timestamp so it recompiles with the current
# define; it is a single translation unit, so this costs a second at most.
_version_touch = $$system(touch $$PWD/src/harbour-zendecoder.cpp)

# Ko-fi shop URL for Pro. Single constant so a Harbour-targeted build can
# ship with it emptied (shows "Pro coming soon") while Chum builds link out.
# Override at build time: qmake KOFI_URL=https://ko-fi.com/...
isEmpty(KOFI_URL): KOFI_URL = https://ko-fi.com/kapitaali
DEFINES += KOFI_URL=\\\"$$KOFI_URL\\\"

QT += core gui qml quick sql multimedia dbus

SOURCES += \
    src/harbour-zendecoder.cpp \
    src/decoder.cpp \
    src/history.cpp \
    src/settings.cpp \
    src/trialmanager.cpp \
    src/productlookup.cpp

HEADERS += \
    src/decoder.h \
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
    qml/img/harbour-zendecoder.png \
    $$QML_FILES

# Names only, not paths: sailfishapp.prf expands each entry to
# icons/<size>/<TARGET>.png and installs it into icons/hicolor/<size>/apps.
SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172 256x256
