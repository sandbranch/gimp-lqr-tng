# Runs inside GIMP on a Broadway display (tests/gui/start.sh): opens a
# photo (LQRT_PHOTO) or a generated scene (of the size LQRT_SCENE), and
# the Liquid Rescale TNG dialog. After Rescale it saves the result and the mask layers in
# LQRT_OUT, writes the result's size to LQRT_OUT/result.txt, and quits.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import struct

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

out = os.environ['LQRT_OUT']
photo = os.environ.get('LQRT_PHOTO')

if photo:
    image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(photo))
    layer = image.get_layers()[0]
else:
    # a sky, a ground, a green tree to keep and a red post to remove, laid
    # out on 480 x 300 and stretched to LQRT_SCENE (e.g. 300x500)
    W, H = (int(v) for v in os.environ.get('LQRT_SCENE', '480x300').split('x'))
    image = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, 'scene', W, H, Gimp.ImageType.RGB_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    data = bytearray()
    for Y in range(H):
        for X in range(W):
            x, y = X * 480 // W, Y * 300 // H
            if 110 <= x < 150 and 90 <= y < 240:
                p = (0.1, 0.55, 0.15) if y < 200 else (0.35, 0.2, 0.1)
            elif 330 <= x < 345 and 120 <= y < 240:
                p = (0.8, 0.15, 0.1)
            elif y < 200:
                p = (0.45 + 0.2 * y / 200, 0.65 + 0.15 * y / 200, 0.95)
            else:
                p = (0.4 + 0.1 * ((x // 7 + y // 5) % 2), 0.55, 0.25)
            data += struct.pack('fff', *p)
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, W, H), "R'G'B' float", bytes(data))
    buf.flush()

Gimp.Display.new(image)
proc = Gimp.get_pdb().lookup_procedure('plug-in-lqr-tng')
config = proc.create_config()
config.set_property('run-mode', Gimp.RunMode.INTERACTIVE)
config.set_property('image', image)
config.set_core_object_array('drawables', [layer])
result = proc.run(config)
status = result.index(0)

with open(os.path.join(out, 'result.txt'), 'w') as f:
    f.write('status %s\n' % status.value_nick)
    f.write('size %d %d\n' % (layer.get_width(), layer.get_height()))
    f.write('canvas %d %d\n' % (image.get_width(), image.get_height()))
    for l in image.get_layers():
        f.write('layer %s %d %d visible=%s\n' % (l.get_name(), l.get_width(),
                                                 l.get_height(), l.get_visible()))

if status == Gimp.PDBStatusType.SUCCESS:
    copy = image.duplicate()
    for l in copy.get_layers():
        l.set_visible(l.get_name() == layer.get_name())
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, copy,
                   Gio.File.new_for_path(os.path.join(out, 'result.png')), None)
    copy.delete()
quit = Gimp.get_pdb().lookup_procedure('gimp-quit')
quit_config = quit.create_config()
quit_config.set_property('force', True)
quit.run(quit_config)
