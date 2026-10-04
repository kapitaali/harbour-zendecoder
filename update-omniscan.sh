#!/bin/sh
# Re-vendor libomniscan (it moves fast) and rebuild the app against it.
#
#   ./update-omniscan.sh [--lib DIR] [--ref REV] [--target T] [--no-build]
#
# Copies the lib subset (same paths as the initial vendoring) at the given
# ref (default: HEAD) into 3rdparty/omniscan/, stamps the commit into
# omniscan.pri, verifies the copy is pristine and that omniscan.pri covers
# every vendored .cpp, warns if the lib's pinned zxing tag drifted from
# our vendored 3rdparty/zxing-cpp, then rebuilds (default: aarch64).
# Exits non-zero on any mismatch — fix omniscan.pri by hand, never patch
# the vendored files (lib changes belong in the lib repo).
set -e

LIB="$HOME/Jolla/libomniscan"
REF="HEAD"
TARGET="SailfishOS-5.1.0.11-aarch64"
BUILD=1

while [ $# -gt 0 ]; do
    case "$1" in
        --lib) LIB="$2"; shift 2;;
        --ref) REF="$2"; shift 2;;
        --target) TARGET="$2"; shift 2;;
        --no-build) BUILD=0; shift;;
        *) echo "usage: $0 [--lib DIR] [--ref REV] [--target T] [--no-build]" >&2; exit 2;;
    esac
done

cd "$(dirname "$0")"
[ -d "$LIB/.git" ] || { echo "not a git checkout: $LIB" >&2; exit 1; }

HASH=$(git -C "$LIB" rev-parse --short "$REF")
echo "== libomniscan $REF = $HASH ($LIB)"
if [ -n "$(git -C "$LIB" status --short | head -1)" ]; then
    echo "-- warning: lib tree is dirty; archiving committed state at $REF" >&2
fi

# Fresh export: drop the vendored dirs (NOT omniscan.pri — that is ours)
# so deleted/renamed lib files cannot linger.
rm -rf 3rdparty/omniscan/include 3rdparty/omniscan/src
git -C "$LIB" archive "$REF" include/omniscan src/core src/backend_zxing \
    src/formats src/payload LICENSE | tar -x -C 3rdparty/omniscan/

# Byte-level pristine check: re-extract to temp and diff -r.
rm -rf /tmp/omniscan-vendor-check
mkdir -p /tmp/omniscan-vendor-check
git -C "$LIB" archive "$REF" include/omniscan src/core src/backend_zxing \
    src/formats src/payload LICENSE | tar -x -C /tmp/omniscan-vendor-check
if ! diff -r /tmp/omniscan-vendor-check/include 3rdparty/omniscan/include \
    || ! diff -r /tmp/omniscan-vendor-check/src 3rdparty/omniscan/src \
    || ! cmp -s /tmp/omniscan-vendor-check/LICENSE 3rdparty/omniscan/LICENSE; then
    echo "pristine check FAILED" >&2; exit 1
fi
rm -rf /tmp/omniscan-vendor-check
echo "-- vendored copy is pristine at $HASH"

# Stamp the commit into omniscan.pri.
sed -i "s#\(libomniscan commit \)[0-9a-f]*#\1$HASH#" 3rdparty/omniscan/omniscan.pri
grep -q "libomniscan commit $HASH" 3rdparty/omniscan/omniscan.pri \
    || { echo "pri stamp FAILED" >&2; exit 1; }

# Coverage check: every vendored .cpp must be in omniscan.pri SOURCES.
(cd 3rdparty/omniscan/src && find . -name "*.cpp" | sed 's|^\./|src/|' | sort) \
    > /tmp/omniscan-vendored.txt
grep -oE '\$\$OMNISCAN_ROOT/[A-Za-z0-9_/]+\.cpp' 3rdparty/omniscan/omniscan.pri \
    | sed 's/\$\$OMNISCAN_ROOT\///' | sort > /tmp/omniscan-prilist.txt
if ! diff /tmp/omniscan-prilist.txt /tmp/omniscan-vendored.txt; then
    echo "omniscan.pri SOURCES does not cover the vendored tree — update it by hand" >&2
    exit 1
fi
echo "-- omniscan.pri covers all vendored sources"

# zxing drift check: the adapter expects the lib's pinned backend version.
PINNED=$(grep -m1 -A2 "FetchContent_Declare(zxing_cpp" "$LIB/CMakeLists.txt" \
    | grep -oE "v[0-9]+\.[0-9]+\.[0-9]+")
VENDORED=$(grep -m1 "ZXING_VERSION_STR" 3rdparty/zxing-cpp/include/Version.h \
    | grep -oE "[0-9]+\.[0-9]+\.[0-9]+")
echo "-- lib pins zxing $PINNED; vendored is $VENDORED"
[ "$PINNED" = "v$VENDORED" ] \
    || echo "-- WARNING: zxing drift! re-vendor 3rdparty/zxing-cpp at $PINNED" >&2

if [ "$BUILD" = 1 ]; then
    echo "== building $TARGET"
    rm -f Makefile .qmake.stash *.o moc_*.cpp harbour-zendecoder
    sfdk -c "target=$TARGET" build
fi
echo OK
