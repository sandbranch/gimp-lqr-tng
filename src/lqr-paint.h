/* Liquid Rescale Paint: what the plug-in's files share
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

#ifndef __LQR_PAINT_H__
#define __LQR_PAINT_H__

#include <libgimp/gimp.h>

#include "carve.h"

G_BEGIN_DECLS

#define PLUG_IN_PROC   "plug-in-lqr-paint"
#define PLUG_IN_BINARY "gimp-lqr-paint"

/* what happens after carving to the new size: the ids of the "after"
 * argument */
typedef enum
{
  LQR_PAINT_AFTER_CROP,     /* the canvas fits the layer, when the layer
                             * covered all of it */
  LQR_PAINT_AFTER_KEEP,     /* the canvas keeps its size */
  LQR_PAINT_AFTER_RESTORE   /* carved back to the original size */
} LqrPaintAfter;

/* the carving settings of config, for a layer of the given size (a width
 * or height of 0 in config keeps the layer's) */
void lqr_paint_options_from_config (GimpProcedureConfig *config,
                                    CarveOptions        *options,
                                    gint                 layer_width,
                                    gint                 layer_height);

/* whether carving layer to its new size and cropping the image to it
 * would change the canvas: the layer covers all of the canvas */
gboolean lqr_paint_covers_canvas   (GimpImage           *image,
                                    GimpLayer           *layer);

/* whether mask can be a mask for layer: another layer (not a group) of
 * the same image */
gboolean lqr_paint_usable_mask     (GimpImage           *image,
                                    GimpLayer           *layer,
                                    GimpLayer           *mask);

G_END_DECLS

#endif /* __LQR_PAINT_H__ */
