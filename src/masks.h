/* Liquid Rescale TNG: the painted masks
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
 *
 * The keep, remove and rigid masks are painted at a working size (at most
 * MASK_WORK_SIZE pixels on the long side, so that a large brush stays
 * fast on a large photo), one byte per pixel, and resampled to the size
 * they are used at: the preview, or the layer for the result.
 */

#ifndef __MASKS_H__
#define __MASKS_H__

#include <glib.h>

G_BEGIN_DECLS

#define MASK_WORK_SIZE 1024

typedef enum
{
  MASK_KEEP,
  MASK_REMOVE,
  MASK_RIGID,       /* where the seams bend less: straight lines */
  MASK_N_KINDS
} MaskKind;

typedef struct
{
  gint    width;
  gint    height;
  guint8 *keep;     /* 0 to 255 */
  guint8 *remove;
  guint8 *rigid;
} Masks;

/* the working size for a layer of width x height */
void     masks_work_size   (gint          width,
                            gint          height,
                            gint         *work_width,
                            gint         *work_height);

Masks  * masks_new         (gint          width,
                            gint          height);
Masks  * masks_copy        (const Masks  *masks);
void     masks_free        (Masks        *masks);
gboolean masks_empty       (const Masks  *masks,
                            MaskKind      kind);
void     masks_clear       (Masks        *masks);

/* paints (value 255) or erases (0) a disc of radius r at (x, y), in
 * working pixels. Keep and remove exclude each other: painting one
 * erases the other there. Rigid goes with keep (a straight line in
 * something kept), not with remove (what goes needs no straight lines,
 * and rigid seams would follow it less well): painting rigid erases
 * remove there, painting remove erases rigid. The eraser takes all three.
 * Grows *area (x, y, width, height; width 0 for none yet) to
 * cover what changed. */
void     masks_paint_disc  (Masks        *masks,
                            MaskKind      kind,
                            gboolean      erase,
                            gdouble       x,
                            gdouble       y,
                            gdouble       r,
                            gint          area[4]);

/* the same along the line from (x0, y0) to (x1, y1), with discs close
 * enough for a solid stroke */
void     masks_paint_line  (Masks        *masks,
                            MaskKind      kind,
                            gboolean      erase,
                            gdouble       x0,
                            gdouble       y0,
                            gdouble       x1,
                            gdouble       y1,
                            gdouble       r,
                            gint          area[4]);

/* one mask resampled to width x height as floats 0 to 1: averaged when
 * shrinking, bilinear when growing. Free with g_free. */
gfloat * masks_resample    (const Masks  *masks,
                            MaskKind      kind,
                            gint          width,
                            gint          height);

/* a float mask (0 to 1) of any size into one of the masks, resampled
 * the same way */
void     masks_set_from    (Masks        *masks,
                            MaskKind      kind,
                            const gfloat *values,
                            gint          width,
                            gint          height);

/* the mask of kind */
guint8 * masks_get         (const Masks  *masks,
                            MaskKind      kind);

/* an undo step: the area of all the masks as it was */
typedef struct
{
  gint    area[4];
  guint8 *masks[MASK_N_KINDS];
} MasksUndo;

MasksUndo * masks_undo_new     (const Masks *before,
                                const gint   area[4]);
void        masks_undo_apply   (Masks       *masks,
                                const MasksUndo *undo);
void        masks_undo_free    (MasksUndo   *undo);

G_END_DECLS

#endif /* __MASKS_H__ */
