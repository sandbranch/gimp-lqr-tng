#!/bin/sh
# Tests Liquid Rescale TNG: the unit tests of the seam carving and the
# masks (meson test, no GIMP), then the plug-in inside the Flatpak GIMP
# without a window (tests/gimp-test.py). GIMP runs with a throwaway
# profile in tests/output (GIMP3_DIRECTORY), where the plug-in is
# installed: your own GIMP profile and plug-ins are not used or changed.
# The build and GIMP run isolated from your folders (tests/isolate.sh,
# with gimp-devtools/gimp-run.sh): HOME and the XDG folders inside
# the Flatpak point into tests/output/gimp-home, so nothing lands in
# ~/.var/app/org.gimp.GIMP either. Before and after, it lists your
# folders of GIMP and the other apps (gimp-devtools/snapshot.sh)
# and fails if anything there changed.
#
#   tests/run.sh           build, unit tests, GIMP tests
#   tests/run.sh --asan    the same with AddressSanitizer and UBSan
#   LQRT_ONLY=mask tests/run.sh   only the GIMP cases whose names match
#
# Prints PASS or FAIL for each case and exits non-zero if any case fails,
# or if the plug-in printed warnings, criticals or sanitizer reports.
# Needs ../gimp-devtools (or GIMP_PLUGIN_DEVTOOLS) for the build.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
src=$(dirname "$here")
out=$here/output
devtools=${GIMP_PLUGIN_DEVTOOLS:-$src/../gimp-devtools}
GIMP_RUN_HOME=$out/gimp-home
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
mkdir -p "$out"
snapshot_take "$out/snapshot-before.txt"

build=$out/build
profile=$out/profile
setup_args=
run_args=
test_env=
if [ "$1" = --asan ]; then
    build=$out/build-asan
    profile=$out/profile-asan
    setup_args="-Db_sanitize=address,undefined -Db_lundef=false"
    # the sanitizer runtimes are in the SDK, which --devel runs GIMP with;
    # leak reports only for the unit tests: libgimp keeps objects of its
    # own until the plug-in exits
    run_args="--devel --env=ASAN_OPTIONS=log_path=$out/sanitizer/asan:detect_leaks=0 \
      --env=UBSAN_OPTIONS=log_path=$out/sanitizer/ubsan:print_stacktrace=1"
    test_env="ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
fi
mkdir -p "$out"
rm -rf "$out/sanitizer"
mkdir -p "$out/sanitizer"
plugindir=$profile/plug-ins
log=$out/test.log
status=0

if [ ! -d "$build" ]; then
    "$devtools/gimp-build.sh" "$src" "meson setup '$build' $setup_args \
      -Dplugindir='$plugindir'" >"$out/setup.log" 2>&1 ||
      { cat "$out/setup.log"; exit 1; }
fi
"$devtools/gimp-build.sh" "$src" "ninja -C '$build' install" \
  >"$out/build.log" 2>&1 || { cat "$out/build.log"; exit 1; }

echo "== unit tests"
if "$devtools/gimp-build.sh" "$src" "$test_env meson test -C '$build' --print-errorlogs" \
     >"$out/unit.log" 2>&1; then
    grep -E "^ *[0-9]+/[0-9]+ " "$out/unit.log" | sed 's/^ */PASS /; s/  *OK .*//'
else
    status=1
    cat "$out/unit.log"
    echo "LQRT FAIL: unit tests"
fi

echo "== GIMP"
# shellcheck disable=SC2086
gimp_run --timeout=1800 --flatpak $run_args --filesystem="$src" --env=GIMP3_DIRECTORY="$profile" \
  --env=LQRT_ONLY="$LQRT_ONLY" -- \
  gimp-console-3.2 --no-interface --no-data --no-fonts --batch-interpreter python-fu-eval \
  -b "exec(open('$here/gimp-test.py').read())" --quit >"$log" 2>&1

grep -E "^LQRT|Traceback|^  File|Error" "$log"

grep -q "^LQRT failures: 0$" "$log" || status=1
# messages of the plug-in (its process is named after it), and GIMP
# closing undo groups that the plug-in left open
if grep -E "gimp-lqr-tng.*(WARNING|CRITICAL)|inconsistent state" "$log"; then
    echo "LQRT FAIL: warnings from the plug-in, see $log"
    status=1
fi
for report in "$out"/sanitizer/*; do
    [ -e "$report" ] || continue
    echo "LQRT FAIL: sanitizer report $report"
    head -30 "$report"
    status=1
done
snapshot_check "$out/snapshot-before.txt" "LQRT " || status=1
[ $status = 0 ] && echo "LQRT all passed" || echo "LQRT FAILED (log: $log)"
exit $status
