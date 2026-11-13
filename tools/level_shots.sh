#!/bin/sh
# Screenshots of every level by script (PORT-PLAN.md P2.22): starts each of the 17 levels headless in its own run of
# build/jones3d and writes the back buffer after the given passes of the game loop.
#
#   tools/level_shots.sh <data-root> <out-dir> [frames]
#
# frames is a comma-separated list, default "1,300": the first frame, and one ten seconds of game time in, past most
# opening fades. Files are <out-dir>/<NN>_<level>_f<frame>.bmp (24-bit BMP); each run's log is <out-dir>/<NN>.log.
# Nothing is written to the player's settings or save folders. Needs an apple build in build/ (screenshots of what the
# renderer drew; a null build's are black).
set -eu

if [ $# -lt 2 ]; then
    echo "usage: $0 <data-root> <out-dir> [frames]" >&2
    exit 2
fi

data=$1
out=$2
frames=${3:-1,300}
root=$(cd "$(dirname "$0")/.." && pwd)
game=$root/build/jones3d
last=${frames##*,}

if [ ! -x "$game" ]; then
    echo "$0: $game not built" >&2
    exit 1
fi

mkdir -p "$out"
out=$(cd "$out" && pwd)
levels="00_CYN 01_BAB 02_RIV 03_SHS 05_LAG 06_VOL 07_TEM 16_JEP 08_TEO 09_OLV 10_SEA 11_PYR 12_SOL 13_NUB 14_INF 15_AET 17_PRU"
status=0
n=1
for level in $levels; do
    name=$(printf '%02d_%s' "$n" "$level")
    # Start Mode 0 starts the level numbered on the command line (JonesMain_Startup); 1 to 17, in the game's order.
    if J3D_SETTINGS=/dev/null J3D_REG_Start_Mode=0 J3D_REG_Install_Path="$out/install/" J3D_LOGS="$out/logs/" \
        "$game" --data "$data" --headless --frames "$last" --screenshot-at "$frames" \
        --screenshot-file "$out/${name}_f{frame}.bmp" "$n" > "$out/$(printf '%02d' "$n").log" 2>&1; then
        echo "$name: ok"
    else
        echo "$name: exit $? (see $(printf '%02d' "$n").log)"
        status=1
    fi
    n=$((n + 1))
done
exit $status
