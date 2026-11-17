#!/bin/sh
# crash: the crash handler (PORT-PLAN.md P8.11). A headless game is sent SIGSEGV mid-level; it must still die of the
# signal (so macOS writes its crash report), leave Crash.log with the signal, a backtrace and the engine's last output,
# and the next run must say where that log is, once.
#
#   tests/crash.sh <jones3d> <data-root> <out-dir>
#
# Skipped (77) in sanitizer builds, where the sanitizers' handlers report crashes instead.
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
export J3D_SETTINGS="$out/settings.ini" J3D_REG_Install_Path="$out/user/" J3D_LOGS="$out/logs/" J3D_REG_Start_Mode=0

"$game" --data "$data" --headless --frames 100000 > "$out/run1.log" 2>&1 &
pid=$!
# Wait for the level to be running (its first lines are logged), then crash it
n=0
while [ $n -lt 60 ] && ! grep -q "Switch to idle camera\|seconds to load 00_cyn" "$out/run1.log" 2>/dev/null; do
    sleep 1
    n=$((n + 1))
done
kill -SEGV $pid
wait $pid
status=$?

if [ ! -f "$out/logs/Crash.log" ] && grep -q "AddressSanitizer\|UndefinedBehaviorSanitizer" "$out/run1.log"; then
    echo "crash: sanitizer build, skipped"
    exit 77
fi

fail=0
[ $status -eq 139 ] || { echo "FAIL: exit status $status, not 139 (SIGSEGV)"; fail=1; }
grep -q "^Signal: SIGSEGV" "$out/logs/Crash.log" 2>/dev/null || { echo "FAIL: no Crash.log naming the signal"; fail=1; }
grep -q "^Backtrace:" "$out/logs/Crash.log" 2>/dev/null || { echo "FAIL: no backtrace"; fail=1; }
grep -q "seconds to load" "$out/logs/Crash.log" 2>/dev/null || { echo "FAIL: no engine output in Crash.log"; fail=1; }

"$game" --data "$data" --headless --frames 10 > "$out/run2.log" 2>&1
grep -q "the last run crashed; its log is .*Crash.log" "$out/run2.log" || { echo "FAIL: the next run did not say where the log is"; fail=1; }
"$game" --data "$data" --headless --frames 10 > "$out/run3.log" 2>&1
grep -q "the last run crashed" "$out/run3.log" && { echo "FAIL: the crash was reported twice"; fail=1; }

[ $fail -eq 0 ] && echo "crash: the log is written and reported once"
exit $fail
