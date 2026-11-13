#!/bin/sh
# input: the Phase 3 input path, end to end (PORT-PLAN.md P3.2 to P3.10).
#
#   tests/input.sh <jones3d> <data-root> <out-dir>
#
# Each run starts level 2 (Babylon) headless, feeds scripted input once the opening has handed Indy to the player
# (frame 600 on; host_record.h describes the script), quicksaves with F5 at frame 800 and stops at 850. The quicksaves
# are the evidence:
#
#   keyboard   holding the forward key changes the quicksave (stdControl's keyboard reaches sithControl)
#   replay     a run recorded with --record and played back with --replay writes the same quicksave, byte for byte,
#              and recording does not change the run
#   pad        with a scripted dual-stick controller and no key set configured, the port's default key set is used:
#              button A jumps exactly as the keyboard's jump key does
#   keyset     with the retail "Microsoft SideWinder game pad" key set (KEYSETS/*.kfg through the data root), the pad's
#              button 7 sidesteps exactly as the keyboard's A key does
#   mouse      with mouse control on, mouse motion turns Indy
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

# run <name> <script text> [extra arguments...]: one headless run; its quicksave is $out/<name>/SaveGames/QUICKSAVE.nds
run() {
    name=$1
    script=$2
    shift 2
    printf '%s\n800 vkey F5\n' "$script" > "$out/$name.txt"
    if ! env J3D_SETTINGS=/dev/null J3D_REG_Start_Mode=0 J3D_REG_Install_Path="$out/$name/" J3D_LOGS="$out/$name/logs/" \
        "$game" --data "$data" --headless --input-script "$out/$name.txt" "$@" 2 > "$out/$name.log" 2>&1; then
        echo "FAIL $name: exit status (see $out/$name.log)"
        status=1
    elif grep -q ASSERT "$out/$name.log"; then
        echo "FAIL $name: assertion (see $out/$name.log)"
        status=1
    elif [ ! -f "$out/$name/SaveGames/QUICKSAVE.nds" ]; then
        echo "FAIL $name: no quicksave"
        status=1
    fi
}

save() {
    echo "$out/$1/SaveGames/QUICKSAVE.nds"
}

# expect same|differ <a> <b> <what>
expect() {
    if cmp -s "$(save "$2")" "$(save "$3")"; then result=same; else result=differ; fi
    if [ "$result" = "$1" ]; then
        echo "ok   $4"
    else
        echo "FAIL $4 ($2 and $3 $result)"
        status=1
    fi
}

run idle   ''                                    --frames 850
run walk   "$(printf '600 key UP down\n625 key UP up')"  --frames 850 --record "$out/walk.rec"
run replay ''                                    --replay "$out/walk.rec"
run plain  "$(printf '600 key UP down\n625 key UP up')"  --frames 850
expect differ idle walk   "keyboard: holding forward moves Indy"
expect same   walk replay "replay: the replayed run's quicksave is the recorded run's"
expect same   walk plain  "replay: recording does not change the run"

run padjump "$(printf '600 pad 1 down\n606 pad 1 up')" --frames 850
run keyjump "$(printf '600 key X down\n606 key X up')" --frames 850
expect differ idle padjump    "pad: button A acts"
expect same   padjump keyjump "pad: the port's default key set makes A jump, as the jump key does"

J3D_REG_Configuration="Microsoft SideWinder game pad"
export J3D_REG_Configuration
run kfgpad "$(printf '600 pad 7 down\n630 pad 7 up')" --frames 850
run kfgkey "$(printf '600 key A down\n630 key A up')" --frames 850
unset J3D_REG_Configuration
expect differ idle kfgkey   "keyset: the A key sidesteps"
expect same   kfgpad kfgkey "keyset: the retail key set's BUTTON 7 sidesteps, as A does"

J3D_REG_Mouse_Control=1
export J3D_REG_Mouse_Control
mouse=$(i=600; while [ $i -lt 640 ]; do echo "$i mouse 40 0"; i=$((i + 1)); done)
run mouse     "$mouse" --frames 850
run mouseidle ''       --frames 850
unset J3D_REG_Mouse_Control
expect differ mouseidle mouse "mouse: motion turns Indy"

exit $status
