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

#include "config.h"

#include <math.h>
#include <string.h>

#include "layer-io.h"
#include "plugin-intl.h"

const Babl *
layer_io_format (GimpDrawable *drawable)
{
  const Babl *space = babl_format_get_space (gimp_drawable_get_format (drawable));
  gboolean    alpha = gimp_drawable_has_alpha (drawable);
  const gchar *name;

  if (gimp_drawable_is_gray (drawable))
    name = alpha ? "Y'A float" : "Y' float";
  else
    name = alpha ? "R'G'B'A float" : "R'G'B' float";
  return babl_format_with_space (name, space);
}

static gboolean
read_area (GimpDrawable        *drawable,
           const Babl          *format,
           const GeglRectangle *area,
           gdouble              scale,
           CarveImage          *image)
{
  GeglBuffer *buffer;

  image->width    = area->width;
  image->height   = area->height;
  image->channels = babl_format_get_n_components (format);
  image->pixels   = g_try_new (gfloat, (gsize) area->width * area->height *
                                       image->channels);
  if (!image->pixels)
    return FALSE;

  buffer = gimp_drawable_get_buffer (drawable);
  gegl_buffer_get (buffer, area, scale, format, image->pixels,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  g_object_unref (buffer);
  return TRUE;
}

gboolean
layer_io_read (GimpDrawable *drawable,
               const Babl   *format,
               CarveImage   *image)
{
  GeglRectangle area = { 0, 0, gimp_drawable_get_width (drawable),
                         gimp_drawable_get_height (drawable) };

  return read_area (drawable, format, &area, 1.0, image);
}

gboolean
layer_io_read_scaled (GimpDrawable *drawable,
                      const Babl   *format,
                      gint          width,
                      gint          height,
                      CarveImage   *image)
{
  GeglRectangle area = { 0, 0, width, height };
  gdouble       scale = (gdouble) width / gimp_drawable_get_width (drawable);

  return read_area (drawable, format, &area, scale, image);
}

gboolean
layer_io_read_over (GimpDrawable *layer,
                    GimpDrawable *target,
                    gint          width,
                    gint          height,
                    CarveImage   *rgba,
                    CarveImage   *mask)
{
  GeglRectangle area;
  gdouble       scale = (gdouble) width / gimp_drawable_get_width (target);
  gint          lx, ly, tx, ty;
  gsize         n, i;

  gimp_drawable_get_offsets (layer, &lx, &ly);
  gimp_drawable_get_offsets (target, &tx, &ty);
  /* in the scaled coordinates of layer */
  area.x      = (gint) floor ((tx - lx) * scale + 0.5);
  area.y      = (gint) floor ((ty - ly) * scale + 0.5);
  area.width  = width;
  area.height = height;

  if (!read_area (layer, babl_format ("R'G'B'A float"), &area, scale, rgba))
    return FALSE;
  if (!mask)
    return TRUE;

  n = (gsize) area.width * area.height;
  mask->width    = area.width;
  mask->height   = area.height;
  mask->channels = 1;
  mask->pixels   = g_try_new (gfloat, n);
  if (!mask->pixels)
    {
      g_clear_pointer (&rgba->pixels, g_free);
      return FALSE;
    }
  for (i = 0; i < n; i++)
    {
      const gfloat *p = rgba->pixels + i * 4;
      gfloat        bright = MAX (p[0], MAX (p[1], p[2]));

      mask->pixels[i] = CLAMP (p[3], 0.0f, 1.0f) * CLAMP (bright, 0.0f, 1.0f);
    }
  return TRUE;
}

void
layer_io_write (GimpDrawable     *drawable,
                const Babl       *format,
                const CarveImage *image)
{
  GeglBuffer *shadow = gimp_drawable_get_shadow_buffer (drawable);

  gegl_buffer_set (shadow, GEGL_RECTANGLE (0, 0, image->width, image->height),
                   0, format, image->pixels, GEGL_AUTO_ROWSTRIDE);
  g_object_unref (shadow);
  gimp_drawable_merge_shadow (drawable, TRUE);
  gimp_drawable_update (drawable, 0, 0, image->width, image->height);
}

static gchar *
parasite_text (GimpLayer *target,
               MaskKind   kind)
{
  return g_strdup_printf ("%s %u", kind == MASK_KEEP ? "keep" : "remove",
                          gimp_item_get_tattoo (GIMP_ITEM (target)));
}

static gboolean
has_parasite (GimpLayer   *layer,
              const gchar *name,
              const gchar *text)
{
  GimpParasite *parasite = gimp_item_get_parasite (GIMP_ITEM (layer), name);
  gboolean      found = FALSE;

  if (parasite)
    {
      guint32       size;
      gconstpointer data = gimp_parasite_get_data (parasite, &size);

      found = size == strlen (text) && memcmp (data, text, size) == 0;
      gimp_parasite_free (parasite);
    }
  return found;
}

/* the parasite of this name, or of the plug-in's first name */
static gboolean
is_mask_of (GimpLayer   *layer,
            const gchar *text)
{
  return has_parasite (layer, MASK_PARASITE, text) ||
         has_parasite (layer, MASK_PARASITE_OLD, text);
}

static GimpLayer *
find_in (GList       *layers,
         const gchar *text)
{
  GList *list;

  for (list = layers; list; list = list->next)
    {
      GimpLayer *layer = list->data;

      if (gimp_item_is_group (GIMP_ITEM (layer)))
        {
          GList     *children = gimp_item_list_children (GIMP_ITEM (layer));
          GimpLayer *found = find_in (children, text);

          g_list_free (children);
          if (found)
            return found;
        }
      else if (is_mask_of (layer, text))
        return layer;
    }
  return NULL;
}

static const gchar *
mask_name (MaskKind kind)
{
  return kind == MASK_KEEP ? _("Keep (Liquid Rescale TNG)")
                           : _("Remove (Liquid Rescale TNG)");
}

void
layer_io_mark_mask (GimpLayer *layer,
                    GimpLayer *target,
                    MaskKind   kind)
{
  gchar        *text = parasite_text (target, kind);
  GimpParasite *parasite;

  /* stored by Liquid Rescale Paint: now under this plug-in's names */
  if (has_parasite (layer, MASK_PARASITE_OLD, text))
    {
      gimp_item_detach_parasite (GIMP_ITEM (layer), MASK_PARASITE_OLD);
      gimp_item_set_name (GIMP_ITEM (layer), mask_name (kind));
    }
  parasite = gimp_parasite_new (MASK_PARASITE,
                                GIMP_PARASITE_PERSISTENT |
                                GIMP_PARASITE_UNDOABLE,
                                strlen (text), text);
  gimp_item_attach_parasite (GIMP_ITEM (layer), parasite);
  gimp_parasite_free (parasite);
  g_free (text);
}

GimpLayer *
layer_io_find_mask (GimpImage *image,
                    GimpLayer *target,
                    MaskKind   kind)
{
  GList     *layers = gimp_image_list_layers (image);
  gchar     *text = parasite_text (target, kind);
  GimpLayer *found = find_in (layers, text);

  g_free (text);
  g_list_free (layers);
  return found;
}

GimpLayer *
layer_io_store_mask (GimpImage    *image,
                     GimpLayer    *target,
                     MaskKind      kind,
                     const gfloat *values)
{
  GimpLayer  *layer = layer_io_find_mask (image, target, kind);
  gint        width = gimp_drawable_get_width (GIMP_DRAWABLE (target));
  gint        height = gimp_drawable_get_height (GIMP_DRAWABLE (target));
  gboolean    gray = gimp_image_get_base_type (image) == GIMP_GRAY;
  const Babl *format = babl_format (gray ? "Y'A float" : "R'G'B'A float");
  gint        channels = gray ? 2 : 4;
  CarveImage  pixels;
  gint        tx, ty;
  gsize       n = (gsize) width * height, i;

  if (!values)
    {
      if (layer)
        gimp_image_remove_layer (image, layer);
      return NULL;
    }

  gimp_drawable_get_offsets (GIMP_DRAWABLE (target), &tx, &ty);
  if (!layer)
    {
      GimpLayer *parent = GIMP_LAYER (gimp_item_get_parent (GIMP_ITEM (target)));

      layer = gimp_layer_new (image, mask_name (kind), width, height,
                              gray ? GIMP_GRAYA_IMAGE : GIMP_RGBA_IMAGE, 60.0,
                              GIMP_LAYER_MODE_NORMAL);
      gimp_image_insert_layer (image, layer, parent,
                               gimp_image_get_item_position (image,
                                                             GIMP_ITEM (target)));
      gimp_item_set_visible (GIMP_ITEM (layer), FALSE);
    }
  layer_io_mark_mask (layer, target, kind);
  if (gimp_drawable_get_width (GIMP_DRAWABLE (layer)) != width ||
           gimp_drawable_get_height (GIMP_DRAWABLE (layer)) != height)
    {
      gimp_layer_resize (layer, width, height, 0, 0);
    }
  gimp_layer_set_offsets (layer, tx, ty);

  pixels.width    = width;
  pixels.height   = height;
  pixels.channels = channels;
  pixels.pixels   = g_new (gfloat, n * channels);
  for (i = 0; i < n; i++)
    {
      gfloat *p = pixels.pixels + i * channels;
      gfloat  a = CLAMP (values[i], 0.0f, 1.0f);

      if (gray)
        {
          p[0] = 1.0f;
          p[1] = a;
        }
      else
        {
          /* green or red, which layer_io_read_over () reads back as a */
          p[0] = kind == MASK_REMOVE ? 1.0f : 0.0f;
          p[1] = kind == MASK_KEEP ? 1.0f : 0.0f;
          p[2] = 0.0f;
          p[3] = a;
        }
    }
  layer_io_write (GIMP_DRAWABLE (layer), format, &pixels);
  g_free (pixels.pixels);
  return layer;
}
