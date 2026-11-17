#!/bin/sh
# One command for the whole check (PORT-PLAN.md P7.1): configure, build and test every tree, then the layout audit.
# Run it before every commit.
#
#   tools/check.sh [--data <data-root>] [--quick] [--asserts] [tree ...]
#
# Trees, each in its own build directory, built with Ninja:
#   apple    build/          the game with the SDL3 and Metal backends
#   null     build-null/     the null backends only (no SDL3, no Swift)
#   asan     build-asan/     the apple tree under AddressSanitizer and UndefinedBehaviorSanitizer
#   asserts  build-asserts/  the apple tree with the engine's debug asserts compiled in (J3D_DEBUG_ASSERTS, P7.3);
#                            not part of the default set: name it, or pass --asserts to add it
# With no tree named, apple, null and asan are checked. --quick checks apple only.
#
# The data root (the folder holding RESOURCE/) comes from --data, else J3D_TEST_DATA in the environment, else the
# value already in the tree's CMake cache. Without one the tests that need game data are not registered and the
# script says so: that run does not count as the check.
#
# Exit status is 0 only if every tree configured, built with no warning, passed every test, and the layout audit
# reports no retail on-disk type that differs.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
data=${J3D_TEST_DATA:-}
trees=
quick=0
asserts=0

while [ $# -gt 0 ]; do
    case $1 in
        --data) data=$2; shift 2 ;;
        --quick) quick=1; shift ;;
        --asserts) asserts=1; shift ;;
        apple|null|asan|asserts) trees="$trees $1"; shift ;;
        -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "$0: unknown argument '$1'" >&2; exit 2 ;;
    esac
done
if [ -z "$trees" ]; then
    if [ $quick -eq 1 ]; then trees=apple; else trees="apple null asan"; fi
    if [ $asserts -eq 1 ]; then trees="$trees asserts"; fi
fi

failed=
note() { printf '\n== %s\n' "$*"; }

# check_tree <name> <build dir> <cmake options...>
check_tree()
{
    name=$1; dir=$root/$2; shift 2
    note "$name: configure ($dir)"
    if [ -n "$data" ]; then
        set -- "$@" "-DJ3D_TEST_DATA=$data"
    fi
    if ! cmake -S "$root" -B "$dir" -G Ninja "$@" > "$dir.configure.log" 2>&1; then
        tail -20 "$dir.configure.log"
        failed="$failed $name:configure"
        return
    fi
    rm -f "$dir.configure.log"
    if ! grep -q '^J3D_TEST_DATA:PATH=.' "$dir/CMakeCache.txt"; then
        echo "$name: no game data (pass --data): the tests that need it are not registered"
        failed="$failed $name:no-data"
    fi

    note "$name: build"
    log=$dir/check-build.log
    if ! cmake --build "$dir" > "$log" 2>&1; then
        grep -E 'error|FAILED' "$log" | head -40
        failed="$failed $name:build"
        return
    fi
    # -Werror stops compiler warnings; this catches what it does not (the linker's, Swift's notes).
    # The one known linker warning is Homebrew's SDL3 being built for a newer macOS than the deployment target (P8.3).
    warnings=$(grep -i 'warning' "$log" | grep -v 'libSDL3.*was built for newer' | grep -vc '^$')
    if [ "$warnings" -ne 0 ]; then
        grep -i 'warning' "$log" | grep -v 'libSDL3.*was built for newer' | head -20
        failed="$failed $name:warnings"
    fi

    note "$name: ctest"
    if ! ctest --test-dir "$dir" --output-on-failure -j 4 --timeout 600 > "$dir/check-ctest.log" 2>&1; then
        failed="$failed $name:ctest"
    fi
    grep -E 'tests passed|Not Run|\*\*\*Failed|\*\*\*Skipped|\*\*\*Timeout|Exception' "$dir/check-ctest.log"
}

for tree in $trees; do
    case $tree in
        apple)   check_tree apple build -DJ3D_PLATFORM=apple ;;
        null)    check_tree null build-null -DJ3D_PLATFORM=null ;;
        asan)    check_tree asan build-asan -DJ3D_PLATFORM=apple -DJ3D_SANITIZE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo ;;
        asserts) check_tree asserts build-asserts -DJ3D_PLATFORM=apple -DJ3D_DEBUG_ASSERTS=ON ;;
    esac
done

# The layout audit reads the compile commands of build/, so it needs the apple tree configured.
note "layout audit"
if [ -f "$root/build/CMakeCache.txt" ]; then
    if ! (cd "$root" && python3 tools/layout_audit.py); then
        failed="$failed layout-audit"
    fi
else
    echo "build/ is not configured: run the apple tree first"
    failed="$failed layout-audit"
fi

note "result"
if [ -n "$failed" ]; then
    echo "FAILED:$failed"
    exit 1
fi
echo "ok:$trees, layout audit"
