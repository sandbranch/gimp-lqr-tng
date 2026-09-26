#!/bin/sh
# Tests the dialog as a user would, on a Broadway display: opens it on the
# generated scene (tests/gui/start.sh), paints the red post with Remove,
# presses Size to remove the red and Rescale, and checks the result: the
# post is gone, the green tree is whole, the layer is narrower and the
# Remove mask layer was stored, hidden. Screenshots of each step are left
# in tests/output/gui/. Run tests/run.sh first (it builds and installs the
# plug-in into the test profile), and close GIMP.
#
# Needs a headless Chrome (google-chrome or chromium), node 22 and
# ../gimp-plugin-devtools (or GIMP_PLUGIN_DEVTOOLS) for gui/cdp.mjs.
# Prints PASS or FAIL for each check and exits non-zero if one fails.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
rm -rf "$here/../output/gui"
photo=
status=0
pass () { echo "LQRP GUI PASS $1"; }
fail () { echo "LQRP GUI FAIL $1"; status=1; }

. "$here/common.sh"

if [ -z "$at" ]; then
    fail "the dialog did not open (log: $out/gimp.log)"
    exit 1
fi
pass "the dialog opened"

# the positions are from the dialog's top left corner
$cdp $view shot:"$out/01-open.png" >/dev/null
# Remove, then a stroke down the post, then Size to remove the red
$cdp $view click:"$(p 166 100)" wait:300 \
     down:"$(p 403 272)" move:"$(p 403 300)" move:"$(p 403 330)" \
     move:"$(p 403 360)" move:"$(p 403 384)" up:"$(p 403 384)" wait:800 \
     click:"$(p 665 591)" wait:2500 shot:"$out/02-remove.png" >/dev/null
$cdp $view click:"$(p 1007 814)" >/dev/null

i=0
while [ ! -f "$out/result.png" ] && [ $i -lt 60 ]; do
    i=$((i + 1))
    sleep 2
done
if [ ! -f "$out/result.txt" ]; then
    fail "no result after Rescale (log: $out/gimp.log)"
    exit 1
fi

grep -q '^status success$' "$out/result.txt" && pass "Rescale succeeded" ||
  fail "status: $(head -1 "$out/result.txt")"
grep -q '^layer Remove (Liquid Rescale Paint) .* visible=False$' "$out/result.txt" &&
  pass "the Remove mask layer was stored, hidden" ||
  fail "no hidden Remove mask layer: $(cat "$out/result.txt")"

python3 - "$out" <<'EOF' || status=1
import sys
from PIL import Image

out = sys.argv[1]
im = Image.open(out + '/result.png').convert('RGB')
w, h = im.size
px = im.load()
red = sum(1 for y in range(h) for x in range(w)
          if px[x, y][0] > 150 and px[x, y][1] < 90 and px[x, y][2] < 90)
green = sum(1 for y in range(h) for x in range(w)
            if px[x, y][1] > 110 and px[x, y][0] < 60 and px[x, y][2] < 70)
failed = 0
def report(ok, what):
    global failed
    print('LQRP GUI %s %s' % ('PASS' if ok else 'FAIL', what))
    failed |= not ok
report(h == 300 and 440 <= w < 480, 'the layer is narrower: %d x %d' % (w, h))
report(red == 0, 'the red post is gone (%d red pixels)' % red)
# the tree's crown is 40 x 110 pixels of flat green
report(green == 40 * 110, 'the green tree is whole (%d of %d pixels)' % (green, 4400))
sys.exit(failed)
EOF

[ $status = 0 ] && echo "LQRP GUI all passed" || echo "LQRP GUI FAILED (screenshots in $out)"
exit $status
