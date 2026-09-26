#!/bin/sh
# Opens the dialog on a Broadway display, as gui-test.sh does, does the
# given steps in it and leaves a screenshot in tests/output/gui/<name>.png;
# then GIMP is stopped. For looking at the layout, or at a setting in the
# preview. Run tests/run.sh first.
#
#   tests/gui/look.sh [--photo file] <name> [step...]
#
# The steps are those of gimp-plugin-devtools/gui/cdp.mjs, with click,
# down, move and up at positions from the dialog's top left corner, e.g.
#
#   tests/gui/look.sh keep click:665,591 wait:2000 click:710,771 wait:2000
#   LQRT_SCENE=300x500 tests/gui/look.sh portrait
#
# LQRT_SCENE is the size of the generated scene (480x300 by default),
# LQRT_VIEW the size of the page (1400,1000).
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
photo=
if [ "$1" = --photo ]; then
    photo=$2
    shift 2
fi
[ -n "$1" ] || { sed -n '2,17s/^# \{0,1\}//p' "$0"; exit 1; }
name=$1
shift

. "$here/common.sh"

if [ -z "$at" ]; then
    echo "LQRT GUI FAIL the dialog did not open (log: $out/gimp.log)"
    exit 1
fi

steps=
for step in "$@"; do
    case $step in
        click:*|down:*|move:*|up:*)
            xy=${step#*:}
            step=${step%%:*}:$(p "${xy%,*}" "${xy#*,}");;
    esac
    steps="$steps $step"
done
# shellcheck disable=SC2086
$cdp $view $steps shot:"$out/$name.png" >/dev/null || exit 1
echo "$out/$name.png"
