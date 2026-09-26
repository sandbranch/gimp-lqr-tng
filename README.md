# Liquid Rescale Paint

A GIMP 3 plug-in for seam carving ("liquid rescale", content-aware
scaling) where you paint, right in its window, what to keep and what to
remove.

Layer > Liquid Rescale Paint... opens a window with the layer on the left
and the result, as it will be, on the right:

![The dialog, with the red post painted to remove](docs/dialog-remove.png)

- **Keep** (green): paint what must keep its shape, such as people and
  faces. The seams go around it.
- **Remove** (red): paint what should go. The seams go through it first.
  **Size to remove the red** makes the layer just enough smaller for all
  of the red to go, and **Restore the original size** carves back to the
  original size afterwards: the red stays gone.
- **Eraser**, or the right mouse button, takes paint away; **Undo** takes
  back a stroke.
- The result on the right is carved again a moment after every change.
- **Fine tune**: rigidity (straighter seams), mask strength, what counts as
  important (energy), the largest enlarging step, and which side goes
  first.

The painted masks are stored as hidden layers next to the layer (Keep
(Liquid Rescale Paint) and Remove (Liquid Rescale Paint)), and carved along
with it, so the next run starts from them. From scripts, any layer can be a
mask: its painted pixels (not transparent, not black) count.

Seam carving itself is done by [liblqr](https://github.com/carlobaldassi/liblqr)
(Carlo Baldassi), which the plug-in builds in. This is a new plug-in, not a
port: the ported GIMP 2 Liquid Rescale plug-in, with its own workflow of
mask layers painted on the canvas, is
[gimp-lqr-plugin](https://github.com/sandbranch/gimp-lqr-plugin).

Works on RGB and gray layers of every precision (8, 16, 32 bit, float),
with or without alpha, and carves a layer mask along.

## Building and installing

Needs meson, ninja, a C compiler and the GIMP 3 development files; liblqr
is used from the system or built in (`-Dbundled_liblqr=enabled` to always
build it in).

    meson setup build -Dplugindir=$HOME/.config/GIMP/3.2/plug-ins
    ninja -C build install

For the Flatpak version of GIMP, build inside it with
[gimp-plugin-devtools](https://github.com/sandbranch/gimp-plugin-devtools):

    gimp-build.sh . meson setup build -Dplugindir=\$GIMP_PLUGINDIR
    gimp-build.sh . ninja -C build install

Restart GIMP after installing.

## Tests

    tests/run.sh           unit tests, then the plug-in in a GIMP without a window
    tests/run.sh --asan    the same with AddressSanitizer and UBSan

The unit tests (`tests/unit`) check the seam carving and the masks without
GIMP; `tests/gimp-test.py` runs the plug-in on generated images of every
precision and type and checks the results pixel by pixel, in a throwaway
GIMP profile.

    tests/gui/gui-test.sh        the dialog as a user would use it
    tests/gui/start.sh [photo]   the dialog to try by hand

`gui-test.sh` opens the dialog on a Broadway display (GTK in a web page)
and drives it from a headless Chrome with
[gimp-plugin-devtools](https://github.com/sandbranch/gimp-plugin-devtools)'
`gui/cdp.mjs`: it paints a red post with Remove, presses Size to remove the
red and Rescale, and checks that the post is gone, the tree whole, and the
mask layer stored. Screenshots of each step are left in `tests/output/gui/`.

## License

GPL version 3 or later, see COPYING. liblqr is LGPL version 3 or later.
