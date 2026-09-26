/* Liquid Rescale TNG: layers in and out
 *
 * Copyright 2026 David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __LAYER_IO_H__
#define __LAYER_IO_H__

#include <libgimp/gimp.h>

#include "carve.h"
#include "masks.h"

G_BEGIN_DECLS

/* the parasite that marks the mask layers this plug-in makes; its data is
 * "keep <tattoo>", "remove <tattoo>" or "rigid <tattoo>", the tattoo of
 * the layer they are for */
#define MASK_PARASITE "gimp-lqr-tng-mask"
/* the same, as Liquid Rescale Paint (this plug-in's first name) stored
 * it: still found, and renamed when the mask is stored again */
#define MASK_PARASITE_OLD "gimp-lqr-paint-mask"

/* the format a drawable is carved in: float, perceptual (the energy
 * follows what the eye sees), in the drawable's colour space */
const Babl * layer_io_format        (GimpDrawable       *drawable);

/* all of a drawable, in format, as floats; FALSE without memory */
gboolean     layer_io_read          (GimpDrawable       *drawable,
                                     const Babl         *format,
                                     CarveImage         *image);

/* all of a drawable scaled to width x height (for the preview) */
gboolean     layer_io_read_scaled   (GimpDrawable       *drawable,
                                     const Babl         *format,
                                     gint                width,
                                     gint                height,
                                     CarveImage         *image);

/* the part of layer over target, scaled to width x height (target's size
 * for no scaling), as R'G'B'A floats (transparent where layer does not
 * reach), and as a mask: alpha times the brightest colour channel, so a
 * transparent or black pixel is not painted. mask may be NULL. */
gboolean     layer_io_read_over     (GimpDrawable       *layer,
                                     GimpDrawable       *target,
                                     gint                width,
                                     gint                height,
                                     CarveImage         *rgba,
                                     CarveImage         *mask);

/* image (of the drawable's size) into the drawable, undoable */
void         layer_io_write         (GimpDrawable       *drawable,
                                     const Babl         *format,
                                     const CarveImage   *image);

/* the mask layer of this plug-in for target, or NULL */
GimpLayer  * layer_io_find_mask     (GimpImage          *image,
                                     GimpLayer          *target,
                                     MaskKind            kind);

/* marks layer as this plug-in's mask layer of kind for target (with the
 * parasite of this name, instead of the old one) */
void         layer_io_mark_mask     (GimpLayer          *layer,
                                     GimpLayer          *target,
                                     MaskKind            kind);

/* the painted mask values (target's size, 0 to 1) into this plug-in's
 * mask layer for target, made if there is none: green, red or blue, with
 * the values as alpha, above target and hidden. Removes the layer if values
 * is NULL. Returns the layer or NULL. */
GimpLayer  * layer_io_store_mask    (GimpImage          *image,
                                     GimpLayer          *target,
                                     MaskKind            kind,
                                     const gfloat       *values);

G_END_DECLS

#endif /* __LAYER_IO_H__ */
