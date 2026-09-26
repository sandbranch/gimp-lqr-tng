# Runs inside GIMP (tests/run.sh), without a window: calls
# plug-in-lqr-paint non-interactively on generated images and checks the
# results numerically. Prints "LQRP PASS <case>" or "LQRP FAIL <case>:
# <why>" for each case, and "LQRP failures: <n>" at the end.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The images are noise, so each pixel is different: when carving only
# shrinks the width, each row of the result is its row of the layer with
# pixels taken out, in order, and this is checked exactly (8 and 16 bit)
# or nearly (float). Objects are flat colour, which has no energy, so the
# seams go through them unless a mask says otherwise.
import os
import random
import re
import struct
import sys
import traceback

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl

PDB = Gimp.get_pdb()
PROC = PDB.lookup_procedure('plug-in-lqr-paint')
FMT = "R'G'B'A float"

U8 = Gimp.Precision.U8_NON_LINEAR
U16 = Gimp.Precision.U16_NON_LINEAR
FLOAT = Gimp.Precision.FLOAT_LINEAR
FLOAT_NL = Gimp.Precision.FLOAT_NON_LINEAR

OBJECT = (0.0, 0.0, 1.0)     # a flat blue object

cases = []
failures = []


def case(func):
    cases.append(func)
    return func


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


# ---------------------------------------------------------------- helpers

def new_image(w, h, base=Gimp.ImageBaseType.RGB, precision=U8, alpha=False,
              pixel=None, name='photo'):
    """An image with one layer of w x h, filled by pixel(x, y) -> (r, g, b, a)."""
    image = Gimp.Image.new_with_precision(w, h, base, precision)
    if base == Gimp.ImageBaseType.RGB:
        itype = Gimp.ImageType.RGBA_IMAGE if alpha else Gimp.ImageType.RGB_IMAGE
    else:
        itype = Gimp.ImageType.GRAYA_IMAGE if alpha else Gimp.ImageType.GRAY_IMAGE
    layer = Gimp.Layer.new(image, name, w, h, itype, 100, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    if pixel:
        put(layer, pixel)
    return image, layer


def new_layer(image, name, w, h, x=0, y=0, pixel=None):
    """An RGBA layer at (x, y), transparent unless pixel(x, y) is given."""
    layer = Gimp.Layer.new(image, name, w, h, Gimp.ImageType.RGBA_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    layer.set_offsets(x, y)
    layer.fill(Gimp.FillType.TRANSPARENT)
    if pixel:
        put(layer, pixel)
    return layer


def put(drawable, pixel):
    w, h = drawable.get_width(), drawable.get_height()
    data = bytearray()
    for y in range(h):
        for x in range(w):
            data += struct.pack('ffff', *pixel(x, y))
    buf = drawable.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, w, h), FMT, bytes(data))
    buf.flush()
    drawable.update(0, 0, w, h)


def get(drawable, fmt=FMT, channels=4, ctype='f'):
    """The pixels as rows of tuples."""
    w, h = drawable.get_width(), drawable.get_height()
    buf = drawable.get_buffer()
    raw = buf.get(Gegl.Rectangle.new(0, 0, w, h), 1.0, fmt, Gegl.AbyssPolicy.NONE)
    vals = struct.unpack('%d%s' % (w * h * channels, ctype), raw)
    return [[vals[(y * w + x) * channels:(y * w + x) * channels + channels]
             for x in range(w)] for y in range(h)]


def native_format(drawable):
    """The name of the drawable's own format, for exact comparisons."""
    precision = drawable.get_image().get_precision()
    ctype = {U8: 'u8', U16: 'u16', FLOAT: 'float', FLOAT_NL: 'float'}[precision]
    linear = precision == FLOAT
    if drawable.is_gray():
        model = 'Y' if linear else "Y'"
    else:
        model = 'RGB' if linear else "R'G'B'"
    if drawable.has_alpha():
        model += 'A'
    return '%s %s' % (model, ctype)


def get_native(drawable):
    """Pixels in the drawable's own format (exact comparisons)."""
    fmt = native_format(drawable)
    w, h = drawable.get_width(), drawable.get_height()
    raw = drawable.get_buffer().get(Gegl.Rectangle.new(0, 0, w, h), 1.0, fmt,
                                    Gegl.AbyssPolicy.NONE)
    bpp = len(raw) // (w * h)
    return [[raw[(y * w + x) * bpp:(y * w + x + 1) * bpp] for x in range(w)]
            for y in range(h)]


def noise(seed, object_columns=(), object_rows=(), alpha=1.0):
    rnd = random.Random(seed)
    table = {}

    def pixel(x, y):
        if x in object_columns or y in object_rows:
            return OBJECT + (alpha,)
        if (x, y) not in table:
            table[(x, y)] = (rnd.random(), rnd.random(), rnd.random(), alpha)
        return table[(x, y)]
    return pixel


def band(columns=(), rows=(), value=(1.0, 1.0, 1.0, 1.0)):
    """A mask: painted (white) over the columns or rows, transparent elsewhere."""
    return lambda x, y: value if (x in columns or y in rows) else (0, 0, 0, 0)


def run(image, drawables, **args):
    """Runs plug-in-lqr-paint non-interactively; returns the PDB status."""
    config = PROC.create_config()
    config.set_property('run-mode', args.pop('run_mode', Gimp.RunMode.NONINTERACTIVE))
    config.set_property('image', image)
    config.set_core_object_array('drawables', drawables)
    for key, value in args.items():
        config.set_property(key.replace('_', '-'), value)
    return PROC.run(config).index(0)


def ok(status):
    check(status == Gimp.PDBStatusType.SUCCESS, 'status %s' % status.value_nick)


def refused(status):
    check(status == Gimp.PDBStatusType.CALLING_ERROR or
          status == Gimp.PDBStatusType.EXECUTION_ERROR,
          'expected an error, got %s' % status.value_nick)


def size(item):
    return item.get_width(), item.get_height()


def is_object(p, tol=0.004):
    return all(abs(a - b) < tol for a, b in zip(p[:3], OBJECT))


def object_count(layer):
    return sum(1 for row in get(layer) for p in row if is_object(p))


def rows_are_subsequences(before, after, exact=True, tol=1e-4):
    """Each row of after is its row of before with pixels taken out."""
    if len(before) != len(after):
        return 'height %d, was %d' % (len(after), len(before))
    same = (lambda a, b: a == b) if exact else \
           (lambda a, b: all(abs(p - q) <= tol for p, q in zip(a, b)))
    for y, (row, out) in enumerate(zip(before, after)):
        x = 0
        for p in out:
            while x < len(row) and not same(row[x], p):
                x += 1
            if x == len(row):
                return 'row %d is not the old row with pixels taken out' % y
            x += 1
    return None


# ------------------------------------------------------------- the basics

@case
def registered():
    check(PROC is not None, 'plug-in-lqr-paint is not registered')
    names = [a.get_name() for a in PROC.get_arguments()]
    for name in ['width', 'height', 'keep-layer', 'remove-layer', 'after',
                 'rigidity', 'mask-strength', 'energy', 'max-enlarge', 'order',
                 'carve-masks']:
        check(name in names, 'no argument %s' % name)
    for name in ['restore-size', 'resize-canvas']:
        check(name not in names, 'the old argument %s is still there' % name)
    check(PROC.get_menu_label() and 'Paint' in PROC.get_menu_label(),
          'menu label %r' % PROC.get_menu_label())


@case
def same_size_changes_nothing():
    image, layer = new_image(50, 30, pixel=noise(1))
    before = get_native(layer)
    ok(run(image, [layer]))
    check(size(layer) == (50, 30), 'size %dx%d' % size(layer))
    check(get_native(layer) == before, 'pixels changed')
    image.delete()


def shrink_case(name, precision, base, alpha):
    def test():
        image, layer = new_image(60, 30, base, precision, alpha, noise(2))
        before = get_native(layer)
        ok(run(image, [layer], width=42))
        check(size(layer) == (42, 30), 'size %dx%d' % size(layer))
        check(size(image) == (42, 30), 'canvas %dx%d' % size(image))
        check(layer.has_alpha() == alpha, 'alpha channel changed')
        check(image.get_precision() == precision, 'precision changed')
        problem = rows_are_subsequences(before, get_native(layer))
        check(problem is None, problem)
        image.delete()
    test.__name__ = 'shrink_' + name
    return case(test)


for _name, _prec, _base, _alpha in [
        ('u8_rgb', U8, Gimp.ImageBaseType.RGB, False),
        ('u8_rgba', U8, Gimp.ImageBaseType.RGB, True),
        ('u8_gray', U8, Gimp.ImageBaseType.GRAY, False),
        ('u8_graya', U8, Gimp.ImageBaseType.GRAY, True),
        ('u16_rgb', U16, Gimp.ImageBaseType.RGB, False),
        ('u16_graya', U16, Gimp.ImageBaseType.GRAY, True),
        ('float_nonlinear_rgba', FLOAT_NL, Gimp.ImageBaseType.RGB, True)]:
    shrink_case(_name, _prec, _base, _alpha)


@case
def shrink_float_linear():
    # carved in perceptual float: linear float values come back within
    # float rounding
    image, layer = new_image(60, 30, Gimp.ImageBaseType.RGB, FLOAT, True, noise(3))
    before = get(layer)
    ok(run(image, [layer], width=45))
    check(size(layer) == (45, 30), 'size %dx%d' % size(layer))
    problem = rows_are_subsequences(before, get(layer), exact=False, tol=1e-5)
    check(problem is None, problem)
    image.delete()


@case
def float_values_outside_0_1_kept():
    pix = lambda x, y: (2.5 if x == 10 else 0.5, -0.25 if y == 5 else 0.5, 0.5, 1.0)
    image, layer = new_image(40, 20, Gimp.ImageBaseType.RGB, FLOAT_NL, True, pix)
    ok(run(image, [layer], width=30))
    vals = [c for row in get(layer) for p in row for c in p]
    check(max(vals) > 2.0 and min(vals) < -0.1, 'values clamped')
    image.delete()


@case
def shrink_height():
    image, layer = new_image(30, 60, pixel=noise(4))
    ok(run(image, [layer], height=40))
    check(size(layer) == (30, 40), 'size %dx%d' % size(layer))
    image.delete()


@case
def enlarge_both():
    image, layer = new_image(40, 30, pixel=noise(5))
    ok(run(image, [layer], width=100, height=45, max_enlarge=1.3))
    check(size(layer) == (100, 45), 'size %dx%d' % size(layer))
    check(size(image) == (100, 45), 'canvas %dx%d' % size(image))
    image.delete()


@case
def order_height_first():
    image, layer = new_image(40, 30, pixel=noise(6))
    ok(run(image, [layer], width=30, height=20, order='height-first'))
    check(size(layer) == (30, 20), 'size %dx%d' % size(layer))
    image.delete()


@case
def every_energy():
    for energy in ['grad-xabs', 'grad-sumabs', 'grad-norm', 'luma-grad-xabs',
                   'luma-grad-sumabs', 'luma-grad-norm', 'none']:
        image, layer = new_image(30, 20, pixel=noise(7))
        ok(run(image, [layer], width=22, energy=energy))
        check(size(layer) == (22, 20), '%s: size %dx%d' % ((energy,) + size(layer)))
        image.delete()


@case
def rigidity_has_an_effect():
    results = []
    for rigidity in (0.0, 10.0):
        image, layer = new_image(60, 40, pixel=noise(8))
        ok(run(image, [layer], width=40, rigidity=rigidity))
        results.append(get_native(layer))
        image.delete()
    check(results[0] != results[1], 'rigidity changed nothing')


# ------------------------------------------------------------- the masks

OBJ = range(20, 30)   # the object's columns


@case
def object_goes_without_a_mask():
    # the control for the keep cases: flat, the object is carved away
    image, layer = new_image(100, 30, pixel=noise(9, object_columns=OBJ))
    ok(run(image, [layer], width=60))
    check(object_count(layer) < 10 * 30, 'the object survived without a mask')
    image.delete()


@case
def keep_mask_layer():
    image, layer = new_image(100, 30, pixel=noise(9, object_columns=OBJ))
    keep = new_layer(image, 'keep', 100, 30, pixel=band(columns=OBJ))
    before = get_native(layer)
    ok(run(image, [layer], width=60, keep_layer=keep))
    check(object_count(layer) == 10 * 30,
          'object pixels %d of %d' % (object_count(layer), 300))
    problem = rows_are_subsequences(before, get_native(layer))
    check(problem is None, problem)
    # carved along: the mask still covers the object, at the layer's size
    check(size(keep) == (60, 30), 'mask size %dx%d' % size(keep))
    painted = [(x, y) for y, row in enumerate(get(keep)) for x, p in enumerate(row)
               if p[3] > 0.5]
    lpix = get(layer)
    check(len(painted) == 300 and all(is_object(lpix[y][x]) for x, y in painted),
          'the carved mask does not cover the object')
    image.delete()


@case
def keep_mask_offset_and_smaller():
    # a mask layer of another size and place: only its part over the layer
    image, layer = new_image(100, 30, pixel=noise(10, object_columns=OBJ))
    keep = new_layer(image, 'keep', 20, 30, x=15, pixel=band(columns=range(5, 15)))
    ok(run(image, [layer], width=60, keep_layer=keep))
    check(object_count(layer) == 300, 'object pixels %d' % object_count(layer))
    image.delete()


@case
def keep_mask_not_carved():
    image, layer = new_image(100, 30, pixel=noise(9, object_columns=OBJ))
    keep = new_layer(image, 'keep', 100, 30, pixel=band(columns=OBJ))
    ok(run(image, [layer], width=60, keep_layer=keep, carve_masks=False))
    check(size(keep) == (100, 30), 'the mask was carved: %dx%d' % size(keep))
    image.delete()


@case
def black_or_transparent_is_not_painted():
    image, layer = new_image(100, 30, pixel=noise(9, object_columns=OBJ))
    keep = new_layer(image, 'keep', 100, 30,
                     pixel=band(columns=OBJ, value=(0, 0, 0, 1)))
    ok(run(image, [layer], width=60, keep_layer=keep))
    check(object_count(layer) < 300, 'a black mask kept the object')
    image.delete()


@case
def remove_mask_layer():
    image, layer = new_image(100, 30, pixel=noise(11, object_columns=range(40, 50)))
    remove = new_layer(image, 'remove', 100, 30, pixel=band(columns=range(40, 50)))
    ok(run(image, [layer], width=90, remove_layer=remove))
    check(object_count(layer) == 0, '%d object pixels left' % object_count(layer))
    image.delete()


@case
def remove_and_restore():
    image, layer = new_image(100, 30, pixel=noise(12, object_columns=range(40, 50)))
    remove = new_layer(image, 'remove', 100, 30, pixel=band(columns=range(40, 50)))
    ok(run(image, [layer], width=90, remove_layer=remove, after='restore'))
    check(size(layer) == (100, 30), 'size %dx%d' % size(layer))
    check(size(image) == (100, 30), 'canvas %dx%d' % size(image))
    check(object_count(layer) == 0, '%d object pixels left' % object_count(layer))
    image.delete()


@case
def keep_and_remove_together():
    obj = range(20, 30)
    other = range(60, 68)
    pix = noise(13, object_columns=list(obj) + list(other))
    image, layer = new_image(100, 30, pixel=pix)
    keep = new_layer(image, 'keep', 100, 30, pixel=band(columns=obj))
    remove = new_layer(image, 'remove', 100, 30, pixel=band(columns=other))
    ok(run(image, [layer], width=70, keep_layer=keep, remove_layer=remove))
    # everything flat left is the kept object: all of it, and none of the other
    check(object_count(layer) == 10 * 30, 'object pixels %d' % object_count(layer))
    image.delete()


@case
def mask_strength_zero_ignores_masks():
    image, layer = new_image(100, 30, pixel=noise(9, object_columns=OBJ))
    keep = new_layer(image, 'keep', 100, 30, pixel=band(columns=OBJ))
    ok(run(image, [layer], width=60, keep_layer=keep, mask_strength=0.0))
    check(object_count(layer) < 300, 'the mask worked at strength 0')
    image.delete()


# ------------------------------------------------- layers, canvas and more

@case
def layer_mask_carved_along():
    image, layer = new_image(80, 30, pixel=noise(14, object_columns=range(30, 40)))
    mask = layer.create_mask(Gimp.AddMaskType.WHITE)
    layer.add_mask(mask)
    # the mask marks the object's columns, which must stay aligned
    put(mask, lambda x, y: (1, 1, 1, 1) if 30 <= x < 40 else (0, 0, 0, 1))
    ok(run(image, [layer], width=60))
    mask = layer.get_mask()
    check(size(mask) == (60, 30), 'mask size %dx%d' % size(mask))
    lpix, mpix = get(layer), get(mask)
    for y in range(30):
        for x in range(60):
            if is_object(lpix[y][x]) != (mpix[y][x][0] > 0.5):
                raise Fail('mask and layer apart at %d,%d' % (x, y))
    image.delete()


def not_covering_image():
    """A 200 x 100 image with a background, and a noise layer of 60 x 40 at
    (30, 20) over it."""
    image = Gimp.Image.new(200, 100, Gimp.ImageBaseType.RGB)
    bg = Gimp.Layer.new(image, 'bg', 200, 100, Gimp.ImageType.RGB_IMAGE, 100,
                        Gimp.LayerMode.NORMAL)
    image.insert_layer(bg, None, 0)
    layer = new_layer(image, 'photo', 60, 40, x=30, y=20, pixel=noise(15))
    return image, layer


@case
def layer_not_covering_the_canvas():
    # never resizes the canvas, whatever comes after
    for after in ['crop', 'keep']:
        image, layer = not_covering_image()
        ok(run(image, [layer], width=40, after=after))
        check(size(image) == (200, 100), '%s: canvas %dx%d' % ((after,) + size(image)))
        check(size(layer) == (40, 40), '%s: size %dx%d' % ((after,) + size(layer)))
        check(layer.get_offsets()[1:] == (30, 20),
              '%s: offsets %s' % (after, layer.get_offsets()[1:]))
        image.delete()
    image, layer = not_covering_image()
    ok(run(image, [layer], width=40, after='restore'))
    check(size(image) == (200, 100), 'restore: canvas %dx%d' % size(image))
    check(size(layer) == (60, 40), 'restore: size %dx%d' % size(layer))
    image.delete()


@case
def after_crop_is_the_default():
    check(PROC.create_config().get_property('after') == 'crop',
          'default %r' % PROC.create_config().get_property('after'))
    image, layer = new_image(60, 40, pixel=noise(16))
    ok(run(image, [layer], width=40, after='crop'))
    check(size(layer) == (40, 40), 'size %dx%d' % size(layer))
    check(size(image) == (40, 40), 'canvas %dx%d' % size(image))
    image.delete()


@case
def after_keep_leaves_the_canvas():
    image, layer = new_image(60, 40, pixel=noise(16))
    ok(run(image, [layer], width=40, after='keep'))
    check(size(layer) == (40, 40), 'size %dx%d' % size(layer))
    check(size(image) == (60, 40), 'canvas %dx%d' % size(image))
    check(layer.get_offsets()[1:] == (0, 0), 'offsets %s' % (layer.get_offsets()[1:],))
    image.delete()


@case
def after_keep_when_enlarging():
    image, layer = new_image(40, 30, pixel=noise(18))
    ok(run(image, [layer], width=60, after='keep'))
    check(size(layer) == (60, 30), 'size %dx%d' % size(layer))
    check(size(image) == (40, 30), 'canvas %dx%d' % size(image))
    image.delete()


@case
def after_restore_without_a_mask():
    image, layer = new_image(60, 40, pixel=noise(16))
    ok(run(image, [layer], width=40, height=30, after='restore'))
    check(size(layer) == (60, 40), 'size %dx%d' % size(layer))
    check(size(image) == (60, 40), 'canvas %dx%d' % size(image))
    image.delete()


@case
def selection_kept_and_ignored():
    image, layer = new_image(60, 40, pixel=noise(17))
    image.select_rectangle(Gimp.ChannelOps.REPLACE, 5, 5, 10, 10)
    ok(run(image, [layer], width=40))
    # all of the layer was carved, not only the selected part
    check(size(layer) == (40, 40), 'size %dx%d' % size(layer))
    bounds = Gimp.Selection.bounds(image)
    non_empty, x1, y1, x2, y2 = bounds[-5:]
    check(non_empty and (x1, y1, x2, y2) == (5, 5, 15, 15),
          'selection %s' % (bounds,))
    check(len(image.get_channels()) == 0, 'a saved selection channel was left')
    image.delete()


# ------------------------------------------------------------- refusals

@case
def refuses_indexed():
    image, layer = new_image(40, 30, pixel=noise(19))
    image.convert_indexed(Gimp.ConvertDitherType.NONE, Gimp.ConvertPaletteType.GENERATE,
                          16, False, False, '')
    layer = image.get_layers()[0]
    refused(run(image, [layer], width=30))
    check(size(layer) == (40, 30), 'changed')
    image.delete()


@case
def refuses_group():
    image, layer = new_image(40, 30, pixel=noise(20))
    group = Gimp.GroupLayer.new(image, 'group')
    image.insert_layer(group, None, 0)
    refused(run(image, [group], width=30))
    image.delete()


@case
def refuses_two_or_no_drawables():
    image, layer = new_image(40, 30, pixel=noise(21))
    other = new_layer(image, 'other', 40, 30)
    refused(run(image, [layer, other], width=30))
    refused(run(image, [], width=30))
    check(size(layer) == (40, 30), 'changed')
    image.delete()


@case
def refuses_channel_and_layer_mask():
    image, layer = new_image(40, 30, pixel=noise(22))
    channel = Gimp.Channel.new(image, 'c', 40, 30, 100, Gegl.Color.new('black'))
    image.insert_channel(channel, None, 0)
    refused(run(image, [channel], width=30))
    mask = layer.create_mask(Gimp.AddMaskType.WHITE)
    layer.add_mask(mask)
    refused(run(image, [layer.get_mask()], width=30))
    image.delete()


@case
def refuses_locked_pixels():
    image, layer = new_image(40, 30, pixel=noise(23))
    layer.set_lock_content(True)
    refused(run(image, [layer], width=30))
    check(size(layer) == (40, 30), 'changed')
    image.delete()


@case
def refuses_masks_that_do_not_fit():
    image, layer = new_image(40, 30, pixel=noise(24))
    other_image, other = new_image(40, 30, pixel=noise(25))
    refused(run(image, [layer], width=30, keep_layer=other))
    refused(run(image, [layer], width=30, keep_layer=layer))
    keep = new_layer(image, 'keep', 40, 30)
    refused(run(image, [layer], width=30, keep_layer=keep, remove_layer=keep))
    check(size(layer) == (40, 30), 'changed')
    other_image.delete()
    image.delete()


@case
def refuses_sizes_liblqr_cannot_carve():
    image, layer = new_image(1, 30, pixel=noise(26))
    refused(run(image, [layer], height=20))
    check(size(layer) == (1, 30), 'changed')
    image.delete()


@case
def tiny_sizes():
    for (w, h, nw, nh) in [(2, 20, 1, 20), (20, 2, 20, 1), (1, 1, 1, 1),
                           (3, 3, 6, 6), (2, 2, 3, 3)]:
        image, layer = new_image(w, h, pixel=noise(27))
        ok(run(image, [layer], width=nw, height=nh))
        check(size(layer) == (nw, nh), '%dx%d to %dx%d gave %dx%d'
              % ((w, h, nw, nh) + size(layer)))
        image.delete()


@case
def argument_ranges():
    def spec(name):
        for a in PROC.get_arguments():
            if a.get_name() == name:
                return a
    check(spec('width').minimum == 0, 'width minimum')
    check(spec('rigidity').maximum >= 10, 'rigidity maximum')
    check(abs(spec('max-enlarge').minimum - 1.05) < 1e-9, 'max-enlarge minimum')
    check(spec('mask-strength').default_value == 1000.0, 'mask-strength default')


# ------------------------------------------------------------------- run

only = re.compile(os.environ.get('LQRP_ONLY') or '.')
for func in cases:
    name = func.__name__.replace('_', '-')
    if not only.search(name):
        continue
    try:
        func()
        print('LQRP PASS', name)
    except Fail as e:
        failures.append(name)
        print('LQRP FAIL %s: %s' % (name, e))
    except Exception as e:
        failures.append(name)
        print('LQRP FAIL %s: %s: %s' % (name, type(e).__name__, e))
        traceback.print_exc(file=sys.stdout)
    sys.stdout.flush()

print('LQRP failures:', len(failures))
