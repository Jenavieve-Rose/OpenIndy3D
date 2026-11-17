#!/bin/sh
# launcher: the launcher's two ways into the game (PORT-PLAN.md P6.30, P6.37), each answered by J3D_MENU_TEST_LAUNCH
# instead of a click, in windowed runs of the real game:
#
#   newgame   Start New Game: "Start Mode" 0 is saved, the first level loads and writes its start save, which also
#             becomes "Last Save Game"
#   resume    Resume Play with that settings file: "Start Mode" 1 is saved and the last save is restored, with no load
#             menu (the game window is not shown yet when the game asks for the save)
#   options   the Options window opens, its Controls tab's button opens the game's control options over it, all close
#             (J3D_MENU_AUTOCANCEL) and the launcher then quits, exit status 0
#             After a crash (a Crash.pending left in the log folder) the crash notice comes first (P8.11).
#   nodata    with no game data to find, the game asks for its folder (P8.4); answered Quit, it ends, exit status 0
#
#   tests/launcher.sh <jones3d> <data-root> <out-dir>
#
# Exits 77 (skipped) when there is no window server, as the present test does.
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

if ! /usr/bin/pgrep -q WindowServer 2>/dev/null; then
    echo "launcher: no window server, skipped"
    exit 77
fi

status=0
run() {
    name=$1
    shift
    env J3D_SETTINGS="$out/settings.ini" J3D_REG_Install_Path="$out/user/" J3D_LOGS="$out/logs/" J3D_NO_AUDIO=1 J3D_REG_Debug_Mode=1 \
        "$@" > "$out/$name.log" 2>&1
}

run newgame J3D_MENU_TEST_LAUNCH=newgame "$game" --data "$data" --frames 300 || { echo "FAIL newgame: exit status"; status=1; }
grep -q '^Start Mode=i:0$' "$out/settings.ini" || { echo "FAIL newgame: Start Mode 0 not saved"; status=1; }
grep -q '^Last Save Game=s:.*start_01_CANYON.nds$' "$out/settings.ini" || { echo "FAIL newgame: no start save"; status=1; }

run resume J3D_MENU_TEST_LAUNCH=resume "$game" --data "$data" --frames 100 || { echo "FAIL resume: exit status"; status=1; }
grep -q '^Start Mode=i:1$' "$out/settings.ini" || { echo "FAIL resume: Start Mode 1 not saved"; status=1; }
grep -q 'RESTORE: returning SUCCESS' "$out/resume.log" || { echo "FAIL resume: the save was not restored"; status=1; }

# The previous run "crashed": the crash notice comes first (P8.11), answered by J3D_MENU_AUTOCANCEL
mkdir -p "$out/logs" && touch "$out/logs/Crash.pending"
run options J3D_MENU_TEST_OPTIONS=1 J3D_MENU_TEST_PRESS=3:3 J3D_MENU_AUTOCANCEL=1 "$game" --data "$data" || { echo "FAIL options: exit status"; status=1; }

grep -q 'the last run crashed' "$out/options.log" || { echo "FAIL options: the crash notice was not shown"; status=1; }
[ ! -f "$out/logs/Crash.pending" ] || { echo "FAIL options: the crash notice was not cleared"; status=1; }

# nodata: with nothing to find (an empty home folder) and no --data, the game asks for its folder; Quit ends it, status 0
mkdir -p "$out/home"
run nodata HOME="$out/home" J3D_SETTINGS="$out/nodata.ini" J3D_MENU_AUTOCANCEL=1 "$game" || { echo "FAIL nodata: exit status"; status=1; }
grep -q '^Game data: none chosen' "$out/nodata.log" || { echo "FAIL nodata: the data folder was not asked for"; status=1; }

if grep -q ASSERT "$out"/*.log; then
    echo "FAIL: assertion (see $out)"
    status=1
fi
[ $status -eq 0 ] && echo "launcher: new game, resume, options and the data question pass"
exit $status
