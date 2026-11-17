#!/bin/sh
# universal: one OpenIndy3D.app for Apple silicon and Intel Macs (PORT-PLAN.md phase 9, P9.1).
#
#   tools/universal.sh [--data <data-root>] [--test] [-- <cmake options...>]
#
# CMake's Ninja generator cannot build Swift (the menus) for two architectures in one tree, so each architecture has a
# tree of its own, configured with the same options:
#   build-arm64/      CMAKE_OSX_ARCHITECTURES=arm64
#   build-x86_64/     CMAKE_OSX_ARCHITECTURES=x86_64
# and build-universal/OpenIndy3D.app is the arm64 app with its executable replaced by the two joined with lipo. Its
# signature is ad hoc; tools/package.sh build-universal signs it for distribution and makes the disk image.
#
# --test runs every test of both trees; the x86_64 tree's run under Rosetta 2. --data names the folder holding
# RESOURCE/ for the tests that need game data (J3D_TEST_DATA in the environment works too).
# Exit status is 0 only if both trees build (and, with --test, pass) and the app holds both architectures.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
data=${J3D_TEST_DATA:-}
test=0
while [ $# -gt 0 ]; do
    case $1 in
        --data) data=$2; shift 2 ;;
        --test) test=1; shift ;;
        --) shift; break ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "$0: unknown argument '$1'" >&2; exit 2 ;;
    esac
done

if [ "$(uname -m)" = arm64 ] && [ $test -eq 1 ] && ! /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
    echo "universal: the x86_64 tests need Rosetta 2 (softwareupdate --install-rosetta)" >&2
    exit 1
fi

for arch in arm64 x86_64; do
    dir=$root/build-$arch
    echo "== $arch: configure and build ($dir)"
    if [ -n "$data" ]; then
        cmake -S "$root" -B "$dir" -G Ninja -DJ3D_PLATFORM=apple -DCMAKE_OSX_ARCHITECTURES=$arch \
            "-DJ3D_TEST_DATA=$data" "$@" > /dev/null
    else
        cmake -S "$root" -B "$dir" -G Ninja -DJ3D_PLATFORM=apple -DCMAKE_OSX_ARCHITECTURES=$arch "$@" > /dev/null
    fi
    cmake --build "$dir"
    if [ $test -eq 1 ]; then
        echo "== $arch: test"
        (cd "$dir" && ctest --output-on-failure)
    fi
done

out=$root/build-universal
app=$out/OpenIndy3D.app
exe=Contents/MacOS/OpenIndy3D
rm -rf "$app"
mkdir -p "$out"
ditto "$root/build-arm64/OpenIndy3D.app" "$app"
lipo -create "$root/build-arm64/OpenIndy3D.app/$exe" "$root/build-x86_64/OpenIndy3D.app/$exe" -output "$app/$exe"
# The linker's ad-hoc signature covered one architecture's executable; sign the joined one the same way.
codesign --force --sign - "$app"
ln -sf "OpenIndy3D.app/$exe" "$out/jones3d"
[ -f "$root/build-arm64/OpenIndy3D.icns" ] && cp "$root/build-arm64/OpenIndy3D.icns" "$out/" # package.sh's icon check

archs=$(lipo -archs "$app/$exe")
case "$archs" in
    *arm64*x86_64*|*x86_64*arm64*) ;;
    *) echo "universal: $app holds '$archs', not both architectures" >&2; exit 1 ;;
esac
for arch in arm64 x86_64; do
    minos=$(vtool -arch $arch -show-build "$app/$exe" | awk '/minos/ { print $2; exit }')
    echo "universal: $arch, macOS $minos or later"
done
echo "universal: $app"
