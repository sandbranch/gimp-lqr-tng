#!/bin/sh
# Opens the Liquid Rescale TNG dialog in the Flatpak GIMP on a Broadway
# display (http://127.0.0.1:8085/), to use or test it with
# gimp-plugin-devtools/gui/cdp.mjs (see gui-test.sh). The plug-in is the
# one built by tests/run.sh, in its throwaway profile. When the dialog is
# closed with Rescale, the result and the mask layers are saved in
# tests/output/gui/ and GIMP quits.
#
#   tests/gui/start.sh [photo]     a photo, or a generated scene
#   LQRT_SCENE=300x500 tests/gui/start.sh    the scene at another size
#
# broadwayd stops when GIMP quits, also when GIMP fails. GIMP loads no
# fonts (--no-fonts): on Broadway it often hung at start while loading
# them, after "corrupted double-linked list", also without this plug-in.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
src=$(dirname "$tests")
out=$tests/output/gui
mkdir -p "$out"
photo=${1:+$(cd "$(dirname "$1")" && pwd)/$(basename "$1")}
flatpak run --filesystem="$src" ${photo:+--filesystem="$(dirname "$photo")":ro} \
  --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=:5 \
  --env=GIMP3_DIRECTORY="$tests/output/profile" \
  --env=LQRT_PHOTO="$photo" --env=LQRT_OUT="$out" \
  --env=LQRT_SCENE="${LQRT_SCENE:-480x300}" \
  --command=sh org.gimp.GIMP -c \
  "broadwayd --port 8085 :5 & bw=\$!; trap 'kill \$bw' EXIT; sleep 2; \
   gimp-3.2 --no-splash --no-fonts \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/open-dialog.py').read())\""
