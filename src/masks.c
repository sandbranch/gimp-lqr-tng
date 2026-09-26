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
 */

#include "masks.h"

#include <math.h>
#include <string.h>

void
masks_work_size (gint  width,
                 gint  height,
                 gint *work_width,
                 gint *work_height)
{
  gint longest = MAX (width, height);

  if (longest <= MASK_WORK_SIZE)
    {
      *work_width  = width;
      *work_height = height;
      return;
    }
  *work_width  = MAX (1, (gint) floor ((gdouble) width * MASK_WORK_SIZE / longest + 0.5));
  *work_height = MAX (1, (gint) floor ((gdouble) height * MASK_WORK_SIZE / longest + 0.5));
}

Masks *
masks_new (gint width,
           gint height)
{
  Masks *masks = g_new (Masks, 1);

  masks->width  = width;
  masks->height = height;
  masks->keep   = g_new0 (guint8, (gsize) width * height);
  masks->remove = g_new0 (guint8, (gsize) width * height);
  masks->rigid  = g_new0 (guint8, (gsize) width * height);
  return masks;
}

Masks *
masks_copy (const Masks *masks)
{
  Masks *copy = g_new (Masks, 1);
  gsize  n = (gsize) masks->width * masks->height;

  *copy = *masks;
  copy->keep   = g_memdup2 (masks->keep, n);
  copy->remove = g_memdup2 (masks->remove, n);
  copy->rigid  = g_memdup2 (masks->rigid, n);
  return copy;
}

void
masks_free (Masks *masks)
{
  if (!masks)
    return;
  g_free (masks->keep);
  g_free (masks->remove);
  g_free (masks->rigid);
  g_free (masks);
}

guint8 *
masks_get (const Masks *masks,
           MaskKind     kind)
{
  switch (kind)
    {
    case MASK_KEEP:
      return masks->keep;
    case MASK_REMOVE:
      return masks->remove;
    default:
      return masks->rigid;
    }
}

gboolean
masks_empty (const Masks *masks,
             MaskKind     kind)
{
  const guint8 *m = masks_get (masks, kind);
  gsize         n = (gsize) masks->width * masks->height, i;

  for (i = 0; i < n; i++)
    if (m[i])
      return FALSE;
  return TRUE;
}

void
masks_clear (Masks *masks)
{
  gsize n = (gsize) masks->width * masks->height;

  memset (masks->keep, 0, n);
  memset (masks->remove, 0, n);
  memset (masks->rigid, 0, n);
}

static void
grow_area (gint area[4],
           gint x0,
           gint y0,
           gint x1,
           gint y1)
{
  if (x1 <= x0 || y1 <= y0)
    return;
  if (area[2] <= 0 || area[3] <= 0)
    {
      area[0] = x0;
      area[1] = y0;
      area[2] = x1 - x0;
      area[3] = y1 - y0;
      return;
    }
  x1 = MAX (x1, area[0] + area[2]);
  y1 = MAX (y1, area[1] + area[3]);
  area[0] = MIN (area[0], x0);
  area[1] = MIN (area[1], y0);
  area[2] = x1 - area[0];
  area[3] = y1 - area[1];
}

void
masks_paint_disc (Masks    *masks,
                  MaskKind  kind,
                  gboolean  erase,
                  gdouble   x,
                  gdouble   y,
                  gdouble   r,
                  gint      area[4])
{
  gint    x0, y0, x1, y1, px, py;
  gdouble r2;

  /* a disc of radius below half a pixel still paints the pixel under it */
  r  = MAX (r, 0.5);
  r2 = r * r;
  x0 = MAX (0, (gint) floor (x - r));
  y0 = MAX (0, (gint) floor (y - r));
  x1 = MIN (masks->width, (gint) ceil (x + r) + 1);
  y1 = MIN (masks->height, (gint) ceil (y + r) + 1);

  for (py = y0; py < y1; py++)
    for (px = x0; px < x1; px++)
      {
        /* the distance from the pixel's centre */
        gdouble dx = px + 0.5 - x, dy = py + 0.5 - y;
        gsize   at = (gsize) py * masks->width + px;

        if (dx * dx + dy * dy > r2)
          continue;
        if (erase)
          {
            masks->keep[at]   = 0;
            masks->remove[at] = 0;
            masks->rigid[at]  = 0;
            continue;
          }
        switch (kind)
          {
          case MASK_KEEP:
            masks->keep[at]   = 255;
            masks->remove[at] = 0;
            break;
          case MASK_REMOVE:
            masks->remove[at] = 255;
            masks->keep[at]   = 0;
            masks->rigid[at]  = 0;
            break;
          default:
            masks->rigid[at]  = 255;
            masks->remove[at] = 0;
            break;
          }
      }
  if (area)
    grow_area (area, x0, y0, x1, y1);
}

void
masks_paint_line (Masks    *masks,
                  MaskKind  kind,
                  gboolean  erase,
                  gdouble   x0,
                  gdouble   y0,
                  gdouble   x1,
                  gdouble   y1,
                  gdouble   r,
                  gint      area[4])
{
  gdouble length = hypot (x1 - x0, y1 - y0);
  /* discs a quarter of the radius apart, at least every half pixel */
  gdouble spacing = MAX (0.5, r / 4.0);
  gint    steps = (gint) ceil (length / spacing), i;

  for (i = 0; i <= steps; i++)
    {
      gdouble t = steps > 0 ? (gdouble) i / steps : 0.0;

      masks_paint_disc (masks, kind, erase, x0 + t * (x1 - x0),
                        y0 + t * (y1 - y0), r, area);
    }
}

/* n values (stride apart) to m values: over each target pixel the
 * average of what it covers when shrinking, bilinear when growing */
static void
resample_1d (const gfloat *src,
             gint          n,
             gsize         src_stride,
             gfloat       *dst,
             gint          m,
             gsize         dst_stride)
{
  gint i;

  if (n == m)
    {
      for (i = 0; i < m; i++)
        dst[i * dst_stride] = src[i * src_stride];
      return;
    }
  if (n > m)
    {
      gdouble scale = (gdouble) n / m;

      for (i = 0; i < m; i++)
        {
          gdouble a = i * scale, b = (i + 1) * scale, sum = 0.0;
          gint    j;

          for (j = (gint) floor (a); j < n && j < b; j++)
            {
              gdouble lo = MAX (a, j), hi = MIN (b, j + 1.0);

              sum += src[j * src_stride] * (hi - lo);
            }
          dst[i * dst_stride] = sum / scale;
        }
      return;
    }
  for (i = 0; i < m; i++)
    {
      gdouble s = (i + 0.5) * n / m - 0.5;
      gint    j;
      gdouble f;

      s = CLAMP (s, 0.0, n - 1.0);
      j = MIN ((gint) floor (s), n - 2);
      j = MAX (j, 0);
      f = n > 1 ? s - j : 0.0;
      dst[i * dst_stride] = n > 1 ? (1.0 - f) * src[j * src_stride] +
                                    f * src[(j + 1) * src_stride]
                                  : src[0];
    }
}

static gfloat *
resample (const gfloat *src,
          gint          sw,
          gint          sh,
          gint          dw,
          gint          dh)
{
  gfloat *rows = g_new (gfloat, (gsize) dw * sh);
  gfloat *dst = g_new (gfloat, (gsize) dw * dh);
  gint    i;

  for (i = 0; i < sh; i++)
    resample_1d (src + (gsize) i * sw, sw, 1, rows + (gsize) i * dw, dw, 1);
  for (i = 0; i < dw; i++)
    resample_1d (rows + i, sh, dw, dst + i, dh, dw);
  g_free (rows);
  return dst;
}

gfloat *
masks_resample (const Masks *masks,
                MaskKind     kind,
                gint         width,
                gint         height)
{
  const guint8 *m = masks_get (masks, kind);
  gsize         n = (gsize) masks->width * masks->height, i;
  gfloat       *values = g_new (gfloat, n);
  gfloat       *out;

  for (i = 0; i < n; i++)
    values[i] = m[i] / 255.0f;
  out = resample (values, masks->width, masks->height, width, height);
  g_free (values);
  return out;
}

void
masks_set_from (Masks        *masks,
                MaskKind      kind,
                const gfloat *values,
                gint          width,
                gint          height)
{
  guint8 *m = masks_get (masks, kind);
  gfloat *work = resample (values, width, height, masks->width, masks->height);
  gsize   n = (gsize) masks->width * masks->height, i;

  for (i = 0; i < n; i++)
    m[i] = (guint8) (CLAMP (work[i], 0.0f, 1.0f) * 255.0f + 0.5f);
  g_free (work);
}

MasksUndo *
masks_undo_new (const Masks *before,
                const gint   area[4])
{
  MasksUndo *undo = g_new0 (MasksUndo, 1);
  gint       y, k;

  memcpy (undo->area, area, sizeof (undo->area));
  for (k = 0; k < MASK_N_KINDS; k++)
    {
      const guint8 *m = masks_get (before, k);

      undo->masks[k] = g_new (guint8, (gsize) area[2] * area[3]);
      for (y = 0; y < area[3]; y++)
        memcpy (undo->masks[k] + (gsize) y * area[2],
                m + (gsize) (area[1] + y) * before->width + area[0], area[2]);
    }
  return undo;
}

void
masks_undo_apply (Masks           *masks,
                  const MasksUndo *undo)
{
  gint y, k;

  for (k = 0; k < MASK_N_KINDS; k++)
    {
      guint8 *m = masks_get (masks, k);

      for (y = 0; y < undo->area[3]; y++)
        memcpy (m + (gsize) (undo->area[1] + y) * masks->width + undo->area[0],
                undo->masks[k] + (gsize) y * undo->area[2], undo->area[2]);
    }
}

void
masks_undo_free (MasksUndo *undo)
{
  gint k;

  if (!undo)
    return;
  for (k = 0; k < MASK_N_KINDS; k++)
    g_free (undo->masks[k]);
  g_free (undo);
}
