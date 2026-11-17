#!/bin/sh
# determinism: a headless run is deterministic in every level (PORT-PLAN.md Phase 7 audit). Each of the 17 levels is
# started twice, headless for 600 frames; the two runs must print the same log and write byte-identical level-start
# autosaves. The first level was checked from phase 1 (boot); the others wrote saves that differed from run to run until
# phase 7, from uninitialised bytes in the thing-fade animations and the sound channels' environment flags.
#
#   tests/determinism.sh <jones3d> <data-root> <out-dir>
#
# Writes only under <out-dir>.
set -u

if [ $# -ne 3 ]; then
    echo "usage: $0 <jones3d> <data-root> <out-dir>" >&2
    exit 2
fi

game=$1
data=$2
out=$3
rm -rf "$out"
mkdir -p "$out"

status=0
n=1
while [ $n -le 17 ]; do
    for run in a b; do
        J3D_SETTINGS=/dev/null J3D_REG_Start_Mode=0 J3D_REG_Install_Path="$out/$n$run/" J3D_LOGS="$out/$n$run/logs/" \
            "$game" --data "$data" --headless --frames 600 "$n" > "$out/$n$run.log" 2>&1
        echo "exit $?" >> "$out/$n$run.log"
    done
    # The load times printed are the host's, not the game's
    sed 's/[0-9.]* seconds/N seconds/' "$out/${n}a.log" > "$out/${n}a.cmp"
    sed 's/[0-9.]* seconds/N seconds/' "$out/${n}b.log" > "$out/${n}b.cmp"
    if ! cmp -s "$out/${n}a.cmp" "$out/${n}b.cmp"; then
        echo "FAIL level $n: the two runs' logs differ"
        status=1
    fi
    saves=$(ls "$out/${n}a/SaveGames/"*.nds 2>/dev/null | wc -l)
    if [ "$saves" -ne 1 ]; then
        echo "FAIL level $n: $saves level-start autosaves"
        status=1
    else
        save=$(basename "$out/${n}a/SaveGames/"*.nds)
        if ! cmp -s "$out/${n}a/SaveGames/$save" "$out/${n}b/SaveGames/$save"; then
            echo "FAIL level $n: the two runs' $save differ"
            status=1
        fi
    fi
    if grep -qE 'ASSERT|ERROR: AddressSanitizer|runtime error:' "$out/${n}a.log" "$out/${n}b.log"; then
        echo "FAIL level $n: assertion or sanitizer report"
        status=1
    fi
    n=$((n + 1))
done
[ $status -eq 0 ] && echo "determinism: every level's two runs match"
exit $status
