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
tests=$(dirname "$here")
src=$(dirname "$tests")
out=$tests/output/gui
devtools=${GIMP_PLUGIN_DEVTOOLS:-$src/../gimp-plugin-devtools}
cdp="node $devtools/gui/cdp.mjs"

chrome=$(command -v google-chrome || command -v chromium || command -v chromium-browser)
[ -n "$chrome" ] || { echo "LQRP GUI SKIP: no Chrome or Chromium"; exit 0; }

rm -rf "$out"
mkdir -p "$out"
CDP_PORT=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
export CDP_PORT

instances () { flatpak ps --columns=instance,application 2>/dev/null |
               awk '$2 == "org.gimp.GIMP" { print $1 }' | sort; }
before=$(instances)
chrome_pid=
cleanup () {
    [ -n "$chrome_pid" ] && kill "$chrome_pid" 2>/dev/null
    # GIMP quits by itself after Rescale; after a failure the Flatpak
    # instance this test started is stopped, not one that was running
    for instance in $(instances); do
        echo "$before" | grep -qx "$instance" || flatpak kill "$instance" 2>/dev/null
    done
}
trap cleanup EXIT

"$here/start.sh" >"$out/gimp.log" 2>&1 &
"$chrome" --headless=new --remote-debugging-port="$CDP_PORT" \
  --user-data-dir="$out/chrome" --password-store=basic about:blank \
  >/dev/null 2>&1 &
chrome_pid=$!

status=0
pass () { echo "LQRP GUI PASS $1"; }
fail () { echo "LQRP GUI FAIL $1"; status=1; }

# the dialog: a window of about 1100 x 840 on the page (Broadway draws each
# window as a canvas; GIMP's own window is as large but off the page); the
# positions below are from its top left corner
find_dialog () {
    $cdp size:1400,900 nav:http://127.0.0.1:8085/ wait:4000 \
      "eval:(() => { const c = [...document.querySelectorAll('canvas')]
        .map(e => e.getBoundingClientRect())
        .find(r => r.left >= 0 && r.top >= 0 && r.width > 1000 &&
                   r.width < 1300 && r.height > 700);
        return c ? Math.round(c.left) + ',' + Math.round(c.top) : '' })()" 2>/dev/null
}
at=
i=0
while [ $i -lt 30 ]; do
    at=$(find_dialog)
    [ -n "$at" ] && break
    i=$((i + 1))
    sleep 3
done
if [ -z "$at" ]; then
    fail "the dialog did not open (log: $out/gimp.log)"
    exit 1
fi
pass "the dialog opened"
x0=${at%,*}
y0=${at#*,}
p () { echo "$(( x0 + $1 )),$(( y0 + $2 ))"; }

$cdp shot:"$out/01-open.png" >/dev/null
# Remove, then a stroke down the post, then Size to remove the red
$cdp click:"$(p 166 100)" wait:300 \
     down:"$(p 403 272)" move:"$(p 403 300)" move:"$(p 403 330)" \
     move:"$(p 403 360)" move:"$(p 403 384)" up:"$(p 403 384)" wait:800 \
     click:"$(p 665 611)" wait:2500 shot:"$out/02-remove.png" >/dev/null
$cdp click:"$(p 1007 766)" >/dev/null

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
