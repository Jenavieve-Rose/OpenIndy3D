#!/bin/sh
# A headless session in every level (PORT-PLAN.md P7.3, P7.9): starts each of the 17 levels in its own run of a game
# binary, runs it for a number of frames on the virtual clock, and reports the exit status, any engine assert or
# sanitizer report, and the run's wall time and peak memory.
#
#   tools/level_sessions.sh <jones3d> <data-root> <out-dir> [frames]
#
# <jones3d> is any build's game binary: build-asserts/jones3d-null for the engine's asserts (P7.3), build-asan/jones3d-null
# under the sanitizers, build/jones3d-null for timings. frames defaults to 9000 (five minutes of game time). Each
# run's log is <out-dir>/<NN>_<level>.log; nothing is written to the player's settings or save folders.
# With no input Indy can die (Meroë's hyenas, King Sol's Mines, the Aetherium): the Game Over dialog then has nobody
# to answer it, is dismissed, and the game exits with status 1. That run counts, reported as "died (no input)".
# Exit status is 0 only if no level asserted, raised a sanitizer report, or ended any other way than its last frame
# or that game over.
set -u

if [ $# -lt 3 ]; then
    echo "usage: $0 <jones3d> <data-root> <out-dir> [frames]" >&2
    exit 2
fi

game=$1
data=$2
out=$3
frames=${4:-9000}

mkdir -p "$out"
out=$(cd "$out" && pwd)
levels="00_CYN 01_BAB 02_RIV 03_SHS 05_LAG 06_VOL 07_TEM 16_JEP 08_TEO 09_OLV 10_SEA 11_PYR 12_SOL 13_NUB 14_INF 15_AET 17_PRU"
status=0
n=1
for level in $levels; do
    name=$(printf '%02d_%s' "$n" "$level")
    log="$out/$name.log"
    # Start Mode 0 starts the level numbered on the command line (JonesMain_Startup); 1 to 17, in the game's order.
    # /usr/bin/time -l reports the peak resident size; its output goes to the end of the log.
    J3D_SETTINGS=/dev/null J3D_REG_Start_Mode=0 J3D_REG_Install_Path="$out/install/$name/" J3D_LOGS="$out/logs/$name/" \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ASAN_OPTIONS=detect_leaks=0 \
        /usr/bin/time -l "$game" --data "$data" --headless --frames "$frames" "$n" > "$log" 2>&1
    result=$?
    secs=$(awk '/ real /{print $1}' "$log")
    peak=$(awk '/maximum resident set size/{printf "%d", $1 / 1048576}' "$log")
    if grep -qE 'ASSERT|ERROR: AddressSanitizer|runtime error:' "$log"; then
        echo "$name: FAIL (see $log)"
        grep -m 3 -E 'ASSERT|ERROR: AddressSanitizer|runtime error:' "$log"
        status=1
    elif [ $result -eq 1 ] && grep -q 'dialog "Game Over" dismissed' "$log"; then
        echo "$name: ok, died (no input) in ${secs}s, peak ${peak} MB"
    elif [ $result -ne 0 ]; then
        echo "$name: FAIL exit $result (see $log)"
        grep -m 3 -E 'ASSERT|ERROR: AddressSanitizer|runtime error:' "$log"
        status=1
    else
        echo "$name: ok, $frames frames in ${secs}s, peak ${peak} MB"
    fi
    n=$((n + 1))
done
exit $status
