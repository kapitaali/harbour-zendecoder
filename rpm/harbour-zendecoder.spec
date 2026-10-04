Name:       harbour-zendecoder
Summary:    Barcode and QR code reader for Sailfish OS
Version: 0.2.0
Release:    1
Group:      Qt/Qt
# App code is GPL-3.0 (LICENSE); the vendored decoder adds libomniscan
# (3rdparty/omniscan/LICENSE, Apache-2.0) with its zxing-cpp backend
# (3rdparty/zxing-cpp/LICENSE, Apache-2.0) and libzueci
# (BSD-3-Clause, SPDX tag in each file).
License:    GPL-3.0-only AND Apache-2.0 AND BSD-3-Clause
URL:        https://github.com/kapitaali/harbour-zendecoder
Source0:    %{name}-%{version}.tar.bz2

# sailfishsilica + the QtMultimedia QML import behind the camera viewfinder.
# Deliberately no Requires for sailfish-components-pickers-qt5 or
# qt5-plugin-sqldriver-sqlite: the Harbour validator rejects both package
# names (allowed list does not contain them), and both are base-image
# packages, so they are always present anyway.
Requires:   sailfishsilica-qt5
Requires:   qt5-qtdeclarative-import-multimedia

BuildRequires: pkgconfig(sailfishapp) >= 1.0.2
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Gui)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5Sql)
BuildRequires: pkgconfig(Qt5Multimedia)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: desktop-file-utils

%description
A native barcode and QR code reader for Sailfish OS: live camera scanning,
gallery image import, manual entry, scan history with CSV/JSON export and
optional product lookup. Decoding runs on-device with the bundled zxing-cpp
library (1D and 2D symbologies, symbology names included), falling back to
the system zxing service for QR codes; product names come from Open Food
Facts only when enabled.

%prep
%setup -q -n %{name}-%{version}

%build
# Hand the git-derived version to the app so the About page shows the real
# thing instead of a copy that rots (APP_VERSION survives qmake untouched
# via the environment, see harbour-zendecoder.pro).
export APP_VERSION=%{version}
%qmake5 harbour-zendecoder.pro
%make_build

%install
rm -rf %{buildroot}
%qmake5_install
# The SDK invokes qmake with QMAKE_STRIP=: (no-op), and the build system's
# brp-strip only runs `strip -g`, which keeps .symtab — so the shipped binary
# still reads "not stripped" to file(1) and the Harbour validator. Full-strip
# it here instead.
%{__strip} %{buildroot}%{_bindir}/harbour-zendecoder

%files
%defattr(-,root,root,-)
%{_bindir}/harbour-zendecoder
%{_datadir}/harbour-zendecoder
%{_datadir}/applications/harbour-zendecoder.desktop
%{_datadir}/icons/hicolor/*/apps/harbour-zendecoder.png
