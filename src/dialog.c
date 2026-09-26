/* Liquid Rescale Paint: the dialog
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
 * On the left, the layer to paint on, with the tools above it; on the
 * right, the result as it will be, carved in a thread on a copy of the
 * layer at the size shown (so it is quick, and close to the real result,
 * which is carved at full size), and the settings under it.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "carve.h"
#include "dialog.h"
#include "layer-io.h"
#include "lqr-paint.h"
#include "paint-view.h"
#include "plugin-intl.h"

/* the largest size the layer and the result are shown at */
#define VIEW_WIDTH  480
#define VIEW_HEIGHT 360

/* how long after a change the preview is carved again */
#define PREVIEW_DELAY 150

typedef struct
{
  GimpProcedureConfig *config;
  GimpImage           *image;
  GimpLayer           *layer;
  gint                 layer_width;
  gint                 layer_height;
  const Babl          *format;

  gdouble              view_scale;    /* displayed pixels per layer pixel */
  gdouble              carve_scale;   /* carved preview pixels per layer
                                       * pixel: view_scale, at most 1 */
  CarveImage           preview;       /* the layer at carve_scale */

  PaintView           *paint;
  GtkWidget           *undo_button;
  GtkWidget           *width_label;
  GtkWidget           *height_label;

  GtkWidget           *result_area;
  GtkWidget           *result_label;
  GtkWidget           *spinner;
  cairo_surface_t     *result;        /* the carved preview */

  guint                timeout;
  CarveJob            *job;           /* of the carving in progress */
  gboolean             running;
  gboolean             again;         /* carve again when it ends */
  gboolean             closing;
} Dialog;

typedef struct
{
  const CarveImage *image;
  CarveImage        keep;
  CarveImage        remove;
  CarveOptions      options;
  CarveJob         *job;
  CarveImage        result;
  gboolean          ok;
  GError           *error;
} PreviewTask;

static void schedule_preview (Dialog *d);

/* ---------------------------------------------------------------- views */

static cairo_surface_t *
surface_from (const CarveImage *image,
              const Babl       *format)
{
  cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                         image->width,
                                                         image->height);
  const Babl      *fish = babl_fish (format, babl_format ("cairo-ARGB32"));
  guchar          *data = cairo_image_surface_get_data (surface);
  gint             stride = cairo_image_surface_get_stride (surface);
  gint             y;

  for (y = 0; y < image->height; y++)
    babl_process (fish,
                  image->pixels + (gsize) y * image->width * image->channels,
                  data + (gsize) y * stride, image->width);
  cairo_surface_mark_dirty (surface);
  return surface;
}

/* the layer at the displayed size, for painting on */
static cairo_surface_t *
layer_surface (Dialog *d,
               gint    width,
               gint    height)
{
  cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                         width, height);
  GeglBuffer      *buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (d->layer));

  gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0, width, height),
                   (gdouble) width / d->layer_width,
                   babl_format ("cairo-ARGB32"),
                   cairo_image_surface_get_data (surface),
                   cairo_image_surface_get_stride (surface), GEGL_ABYSS_NONE);
  g_object_unref (buffer);
  cairo_surface_mark_dirty (surface);
  return surface;
}

static gboolean
on_result_draw (GtkWidget *widget,
                cairo_t   *cr,
                Dialog    *d)
{
  gint    w = gtk_widget_get_allocated_width (widget);
  gint    h = gtk_widget_get_allocated_height (widget);
  gdouble scale, rw, rh, bw, bh, x, y;
  gint    cx, cy;

  if (!d->result)
    return TRUE;

  /* the result at the scale of the layer on the left; the box around it
   * is the result, or, when the image keeps its size, at least the
   * layer's old size, with the empty strip showing; smaller to fit */
  scale = d->view_scale / d->carve_scale;
  rw = cairo_image_surface_get_width (d->result) * scale;
  rh = cairo_image_surface_get_height (d->result) * scale;
  bw = rw;
  bh = rh;
  if (gimp_procedure_config_get_choice_id (d->config, "after") ==
      LQR_PAINT_AFTER_KEEP)
    {
      bw = MAX (bw, d->preview.width * scale);
      bh = MAX (bh, d->preview.height * scale);
    }
  if (bw > w || bh > h)
    {
      gdouble fit = MIN (w / bw, h / bh);

      scale *= fit;
      rw *= fit;
      rh *= fit;
      bw *= fit;
      bh *= fit;
    }
  x = floor ((w - bw) / 2);
  y = floor ((h - bh) / 2);

  cairo_save (cr);
  cairo_rectangle (cr, x, y, ceil (bw), ceil (bh));
  cairo_clip (cr);
  cairo_set_source_rgb (cr, 0.6, 0.6, 0.6);
  cairo_paint (cr);
  cairo_set_source_rgb (cr, 0.4, 0.4, 0.4);
  for (cy = 0; cy < bh; cy += 8)
    for (cx = (cy / 8) % 2 * 8; cx < bw; cx += 16)
      cairo_rectangle (cr, x + cx, y + cy, 8, 8);
  cairo_fill (cr);
  /* the layer stays at the top left of the canvas */
  cairo_rectangle (cr, x, y, ceil (rw), ceil (rh));
  cairo_clip (cr);
  cairo_translate (cr, x, y);
  cairo_scale (cr, scale, scale);
  cairo_set_source_surface (cr, d->result, 0, 0);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
  cairo_paint (cr);
  cairo_restore (cr);
  return TRUE;
}

/* ---------------------------------------------------------------- preview */

static void
preview_task_free (PreviewTask *t)
{
  g_free (t->keep.pixels);
  g_free (t->remove.pixels);
  g_free (t->result.pixels);
  g_clear_error (&t->error);
  carve_job_free (t->job);
  g_free (t);
}

static void
preview_thread (GTask        *task,
                gpointer      source,
                PreviewTask  *t,
                GCancellable *cancellable)
{
  t->ok = carve (t->image,
                 t->keep.pixels ? &t->keep : NULL,
                 t->remove.pixels ? &t->remove : NULL,
                 &t->options, NULL, t->job, &t->result, NULL, NULL,
                 &t->error);
  g_task_return_boolean (task, TRUE);
}

static void
update_result_label (Dialog *d)
{
  CarveOptions options;
  gchar       *text;

  lqr_paint_options_from_config (d->config, &options, d->layer_width,
                                 d->layer_height);
  if (options.restore_size)
    text = g_strdup_printf (_("Result: %d \303\227 %d pixels (removed at %d "
                              "\303\227 %d, then carved back)"),
                            d->layer_width, d->layer_height,
                            options.width, options.height);
  else if ((options.width != d->layer_width ||
            options.height != d->layer_height) &&
           (gimp_procedure_config_get_choice_id (d->config, "after") !=
            LQR_PAINT_AFTER_CROP ||
            !lqr_paint_covers_canvas (d->image, d->layer)))
    text = g_strdup_printf (_("Result: %d \303\227 %d pixels (the image "
                              "stays %d \303\227 %d)"),
                            options.width, options.height,
                            gimp_image_get_width (d->image),
                            gimp_image_get_height (d->image));
  else
    text = g_strdup_printf (_("Result: %d \303\227 %d pixels"),
                            options.width, options.height);
  gtk_label_set_text (GTK_LABEL (d->result_label), text);
  g_free (text);
}

static void
preview_done (GObject      *source,
              GAsyncResult *res,
              gpointer      data)
{
  Dialog      *d = data;
  PreviewTask *t = g_task_get_task_data (G_TASK (res));

  d->running = FALSE;
  d->job = NULL;
  if (d->closing)
    return;

  if (t->ok && !d->again)
    {
      g_clear_pointer (&d->result, cairo_surface_destroy);
      d->result = surface_from (&t->result, d->format);
      gtk_widget_queue_draw (d->result_area);
    }
  else if (!t->ok && !d->again)
    {
      g_clear_pointer (&d->result, cairo_surface_destroy);
      if (g_error_matches (t->error, CARVE_ERROR, CARVE_ERROR_SIZE))
        gtk_label_set_text (GTK_LABEL (d->result_label),
                            _("This size cannot be carved: seam carving needs "
                              "at least 2 pixels across the side it carves."));
      else if (!g_error_matches (t->error, CARVE_ERROR, CARVE_ERROR_CANCELLED))
        gtk_label_set_text (GTK_LABEL (d->result_label),
                            _("The preview could not be made (not enough "
                              "memory?)."));
      gtk_widget_queue_draw (d->result_area);
    }
  gtk_spinner_stop (GTK_SPINNER (d->spinner));

  if (d->again)
    {
      d->again = FALSE;
      schedule_preview (d);
    }
}

static gfloat *
preview_mask (Dialog   *d,
              MaskKind  kind)
{
  const Masks *masks = paint_view_get_masks (d->paint);

  if (masks_empty (masks, kind))
    return NULL;
  return masks_resample (masks, kind, d->preview.width, d->preview.height);
}

static gint
preview_size (gint    target,
              gint    layer,
              gint    preview,
              gdouble scale)
{
  if (target == layer)
    return preview;
  return MAX (1, (gint) floor (target * scale + 0.5));
}

static gboolean
start_preview (gpointer data)
{
  Dialog      *d = data;
  PreviewTask *t;
  GTask       *task;

  d->timeout = 0;
  if (d->running)
    {
      d->again = TRUE;
      if (d->job)
        carve_job_cancel (d->job);
      return G_SOURCE_REMOVE;
    }

  update_result_label (d);

  t = g_new0 (PreviewTask, 1);
  t->image = &d->preview;
  lqr_paint_options_from_config (d->config, &t->options, d->layer_width,
                                 d->layer_height);
  t->options.width  = preview_size (t->options.width, d->layer_width,
                                    d->preview.width, d->carve_scale);
  t->options.height = preview_size (t->options.height, d->layer_height,
                                    d->preview.height, d->carve_scale);
  t->keep = (CarveImage) { d->preview.width, d->preview.height, 1,
                           preview_mask (d, MASK_KEEP) };
  t->remove = (CarveImage) { d->preview.width, d->preview.height, 1,
                             preview_mask (d, MASK_REMOVE) };
  t->job = carve_job_new ();
  d->job = t->job;
  d->running = TRUE;
  gtk_spinner_start (GTK_SPINNER (d->spinner));

  task = g_task_new (NULL, NULL, preview_done, d);
  g_task_set_task_data (task, t, (GDestroyNotify) preview_task_free);
  g_task_run_in_thread (task, (GTaskThreadFunc) preview_thread);
  g_object_unref (task);
  return G_SOURCE_REMOVE;
}

static void
schedule_preview (Dialog *d)
{
  if (d->closing)
    return;
  if (d->timeout)
    g_source_remove (d->timeout);
  d->timeout = g_timeout_add (PREVIEW_DELAY, start_preview, d);
}

/* ---------------------------------------------------------------- controls */

static void
on_masks_changed (PaintView *view,
                  gpointer   data)
{
  Dialog *d = data;

  gtk_widget_set_sensitive (d->undo_button, paint_view_can_undo (view));
  schedule_preview (d);
}

static void
on_config_changed (GObject    *config,
                   GParamSpec *pspec,
                   Dialog     *d)
{
  const gchar *name = g_param_spec_get_name (pspec);
  gint         width, height;

  if (strcmp (name, "brush-size") == 0)
    {
      gint size;

      g_object_get (config, "brush-size", &size, NULL);
      paint_view_set_brush (d->paint, size);
      return;
    }
  if (strcmp (name, "keep-layer") == 0 || strcmp (name, "remove-layer") == 0 ||
      strcmp (name, "carve-masks") == 0)
    return;

  /* 0 (as after Reset) is the layer's size */
  g_object_get (config, "width", &width, "height", &height, NULL);
  if (width == 0 || height == 0)
    {
      g_object_set (config,
                    "width", width ? width : d->layer_width,
                    "height", height ? height : d->layer_height,
                    NULL);
      return;
    }
  if (d->width_label)
    {
      gchar *text;

      text = g_strdup_printf ("%.0f %%", 100.0 * width / d->layer_width);
      gtk_label_set_text (GTK_LABEL (d->width_label), text);
      g_free (text);
      text = g_strdup_printf ("%.0f %%", 100.0 * height / d->layer_height);
      gtk_label_set_text (GTK_LABEL (d->height_label), text);
      g_free (text);
    }
  schedule_preview (d);
}

static void
on_tool (GtkToggleButton *button,
         Dialog          *d)
{
  if (gtk_toggle_button_get_active (button))
    paint_view_set_tool (d->paint,
                         GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
                                                             "tool")));
}

static void
on_undo (GtkButton *button,
         Dialog    *d)
{
  paint_view_undo (d->paint);
}

static void
on_clear (GtkButton *button,
          Dialog    *d)
{
  paint_view_clear (d->paint);
}

static void
on_show_masks (GtkToggleButton *button,
               Dialog          *d)
{
  paint_view_set_show_masks (d->paint, gtk_toggle_button_get_active (button));
}

/* the size that lets the seams take all of the red away: the most red
 * pixels in a row (or column), in the side where that is the smaller part */
static void
on_fit_remove (GtkButton *button,
               Dialog    *d)
{
  const Masks *masks = paint_view_get_masks (d->paint);
  CarveImage   remove = { masks->width, masks->height, 1, NULL };
  gint         across, down, amount_w, amount_h;

  if (masks_empty (masks, MASK_REMOVE))
    {
      g_message (_("Paint what to remove in red first."));
      return;
    }
  remove.pixels = masks_resample (masks, MASK_REMOVE, masks->width, masks->height);
  across = carve_removal_amount (&remove, TRUE);
  down   = carve_removal_amount (&remove, FALSE);
  g_free (remove.pixels);

  /* in layer pixels, with one to spare for the rounding */
  amount_w = (gint) ceil ((gdouble) across * d->layer_width / masks->width) + 1;
  amount_h = (gint) ceil ((gdouble) down * d->layer_height / masks->height) + 1;

  if ((gdouble) amount_w / d->layer_width <= (gdouble) amount_h / d->layer_height)
    g_object_set (d->config,
                  "width", MAX (1, d->layer_width - amount_w),
                  "height", d->layer_height, NULL);
  else
    g_object_set (d->config,
                  "width", d->layer_width,
                  "height", MAX (1, d->layer_height - amount_h), NULL);
}

static GtkWidget *
tool_button (GtkWidget   *group,
             const gchar *label,
             const gchar *tooltip,
             const gchar *style,
             PaintTool    tool,
             Dialog      *d)
{
  GtkWidget *button;

  button = group ? gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (group), label)
                 : gtk_radio_button_new_with_mnemonic (NULL, label);
  gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE);
  gtk_widget_set_tooltip_text (button, tooltip);
  if (style)
    gtk_style_context_add_class (gtk_widget_get_style_context (button), style);
  g_object_set_data (G_OBJECT (button), "tool", GINT_TO_POINTER (tool));
  g_signal_connect (button, "toggled", G_CALLBACK (on_tool), d);
  return button;
}

static void
add_style (void)
{
  GtkCssProvider *css = gtk_css_provider_new ();

  gtk_css_provider_load_from_data (css,
    "button.lqr-keep, button.lqr-remove {"
    "  background-image: none; color: #ffffff; font-weight: bold;"
    "  padding-left: 14px; padding-right: 14px; }"
    "button.lqr-keep { background-color: #2f9e44; }"
    "button.lqr-keep:hover { background-color: #37b24d; }"
    "button.lqr-keep:checked { background-color: #1e7a32;"
    "  box-shadow: inset 0 0 0 2px #ffffff; }"
    "button.lqr-remove { background-color: #d63a3a; }"
    "button.lqr-remove:hover { background-color: #e03131; }"
    "button.lqr-remove:checked { background-color: #a61e1e;"
    "  box-shadow: inset 0 0 0 2px #ffffff; }",
    -1, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (css),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (css);
}

static GtkWidget *
size_row (GtkGrid     *grid,
          gint         row,
          const gchar *label,
          GObject     *config,
          const gchar *property,
          gint         max,
          GtkWidget  **percent)
{
  GtkWidget *name = gtk_label_new_with_mnemonic (label);
  GtkWidget *spin = gimp_prop_spin_button_new (config, property, 1.0, 10.0, 0);
  GtkWidget *unit = gtk_label_new (_("pixels"));

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (spin), 1, max);
  gtk_label_set_mnemonic_widget (GTK_LABEL (name), spin);
  gtk_widget_set_halign (name, GTK_ALIGN_START);
  *percent = gtk_label_new (NULL);
  gtk_widget_set_halign (*percent, GTK_ALIGN_END);
  gtk_label_set_width_chars (GTK_LABEL (*percent), 6);
  gtk_grid_attach (grid, name, 0, row, 1, 1);
  gtk_grid_attach (grid, spin, 1, row, 1, 1);
  gtk_grid_attach (grid, unit, 2, row, 1, 1);
  gtk_grid_attach (grid, *percent, 3, row, 1, 1);
  return spin;
}

/* the masks to start from: those of config, or those this plug-in stored
 * for the layer; config then names them */
static Masks *
load_masks (Dialog *d)
{
  const gchar *properties[2] = { "keep-layer", "remove-layer" };
  Masks       *masks;
  gint         w, h, k;

  masks_work_size (d->layer_width, d->layer_height, &w, &h);
  masks = masks_new (w, h);
  for (k = 0; k < 2; k++)
    {
      GimpLayer *mask = NULL;
      CarveImage rgba = { 0 }, values = { 0 };

      g_object_get (d->config, properties[k], &mask, NULL);
      if (!lqr_paint_usable_mask (d->image, d->layer, mask))
        {
          g_clear_object (&mask);
          mask = layer_io_find_mask (d->image, d->layer, k);
          if (mask)
            g_object_ref (mask);
          g_object_set (d->config, properties[k], mask, NULL);
        }
      if (mask &&
          layer_io_read_over (GIMP_DRAWABLE (mask), GIMP_DRAWABLE (d->layer),
                              w, h, &rgba, &values))
        masks_set_from (masks, k, values.pixels, w, h);
      g_free (rgba.pixels);
      g_free (values.pixels);
      g_clear_object (&mask);
    }
  return masks;
}

static GtkWidget *
frame (const gchar *title,
       GtkWidget   *child)
{
  GtkWidget *f = gimp_frame_new (title);

  gtk_container_add (GTK_CONTAINER (f), child);
  return f;
}

gboolean
lqr_paint_dialog (GimpProcedure       *procedure,
                  GimpProcedureConfig *config,
                  GimpImage           *image,
                  GimpLayer           *layer,
                  Masks              **masks_out,
                  gboolean            *changed)
{
  Dialog     d = { 0 };
  GtkWidget *dialog, *content, *columns, *left, *right, *tools, *button;
  GtkWidget *keep, *box, *grid, *expander, *fine, *show, *label;
  gint       view_w, view_h, max;
  gboolean   run;

  d.config       = config;
  d.image        = image;
  d.layer        = layer;
  d.layer_width  = gimp_drawable_get_width (GIMP_DRAWABLE (layer));
  d.layer_height = gimp_drawable_get_height (GIMP_DRAWABLE (layer));
  d.format       = layer_io_format (GIMP_DRAWABLE (layer));

  /* shown to fit the view (small layers larger, up to 4 times), carved
   * at no more than the layer's size */
  d.view_scale  = MIN (MIN ((gdouble) VIEW_WIDTH / d.layer_width,
                            (gdouble) VIEW_HEIGHT / d.layer_height), 4.0);
  d.carve_scale = MIN (d.view_scale, 1.0);
  view_w = MAX (1, (gint) floor (d.layer_width * d.view_scale + 0.5));
  view_h = MAX (1, (gint) floor (d.layer_height * d.view_scale + 0.5));
  if (!layer_io_read_scaled (GIMP_DRAWABLE (layer), d.format,
                             MAX (1, (gint) floor (d.layer_width * d.carve_scale + 0.5)),
                             MAX (1, (gint) floor (d.layer_height * d.carve_scale + 0.5)),
                             &d.preview))
    {
      g_message (_("Not enough memory for this layer."));
      return FALSE;
    }

  /* the size is the layer's to start with: a size kept from the last run
   * was for another layer */
  g_object_set (config,
                "width", d.layer_width,
                "height", d.layer_height, NULL);

  add_style ();
  dialog = gimp_procedure_dialog_new (procedure, config,
                                      _("Liquid Rescale Paint"));
  gimp_procedure_dialog_set_ok_label (GIMP_PROCEDURE_DIALOG (dialog),
                                      _("_Rescale"));
  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

  columns = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (columns), 12);
  gtk_box_pack_start (GTK_BOX (content), columns, TRUE, TRUE, 0);

  /* left: the tools and the layer to paint on */
  left = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (columns), left, FALSE, FALSE, 0);

  tools = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start (GTK_BOX (left), tools, FALSE, FALSE, 0);
  keep = tool_button (NULL, _("_Keep"),
                      _("Paint in green what must keep its shape"),
                      "lqr-keep", PAINT_TOOL_KEEP, &d);
  gtk_box_pack_start (GTK_BOX (tools), keep, FALSE, FALSE, 0);
  button = tool_button (keep, _("Re_move"),
                        _("Paint in red what should go"),
                        "lqr-remove", PAINT_TOOL_REMOVE, &d);
  gtk_box_pack_start (GTK_BOX (tools), button, FALSE, FALSE, 0);
  button = tool_button (keep, _("_Eraser"),
                        _("Take paint away (also the right mouse button)"),
                        NULL, PAINT_TOOL_ERASE, &d);
  gtk_box_pack_start (GTK_BOX (tools), button, FALSE, FALSE, 0);

  button = gtk_button_new_from_icon_name ("edit-clear", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text (button, _("Clear all the paint"));
  g_signal_connect (button, "clicked", G_CALLBACK (on_clear), &d);
  gtk_box_pack_end (GTK_BOX (tools), button, FALSE, FALSE, 0);
  d.undo_button = gtk_button_new_from_icon_name ("edit-undo", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text (d.undo_button, _("Undo the last stroke"));
  gtk_widget_set_sensitive (d.undo_button, FALSE);
  g_signal_connect (d.undo_button, "clicked", G_CALLBACK (on_undo), &d);
  gtk_box_pack_end (GTK_BOX (tools), d.undo_button, FALSE, FALSE, 0);

  d.paint = paint_view_new (layer_surface (&d, view_w, view_h),
                            load_masks (&d), on_masks_changed, &d);
  {
    gint size;

    g_object_get (config, "brush-size", &size, NULL);
    paint_view_set_brush (d.paint, size);
  }
  gtk_box_pack_start (GTK_BOX (left),
                      frame (_("Paint what to keep and what to remove"),
                             paint_view_get_widget (d.paint)),
                      FALSE, FALSE, 0);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (left), box, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "brush-size",
                                                        GIMP_TYPE_SPIN_SCALE),
                      TRUE, TRUE, 0);
  show = gtk_check_button_new_with_mnemonic (_("Show the _paint"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show), TRUE);
  g_signal_connect (show, "toggled", G_CALLBACK (on_show_masks), &d);
  gtk_box_pack_start (GTK_BOX (box), show, FALSE, FALSE, 0);

  /* right: the result, and the settings */
  right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (columns), right, TRUE, TRUE, 0);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  d.result_area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (d.result_area, VIEW_WIDTH, VIEW_HEIGHT);
  g_signal_connect (d.result_area, "draw", G_CALLBACK (on_result_draw), &d);
  gtk_box_pack_start (GTK_BOX (box), d.result_area, FALSE, FALSE, 0);
  label = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  d.spinner = gtk_spinner_new ();
  gtk_box_pack_start (GTK_BOX (label), d.spinner, FALSE, FALSE, 0);
  d.result_label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (d.result_label), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (d.result_label), TRUE);
  gtk_box_pack_start (GTK_BOX (label), d.result_label, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (right), frame (_("Result (preview)"), box),
                      FALSE, FALSE, 0);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
  max = GIMP_MAX_IMAGE_SIZE;
  size_row (GTK_GRID (grid), 0, _("_Width:"), G_OBJECT (config), "width",
            MIN (max, d.layer_width * 10), &d.width_label);
  size_row (GTK_GRID (grid), 1, _("_Height:"), G_OBJECT (config), "height",
            MIN (max, d.layer_height * 10), &d.height_label);
  gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);
  button = gtk_button_new_with_mnemonic (_("_Size to remove the red"));
  gtk_widget_set_tooltip_text (button,
                               _("Make the layer just smaller enough for the "
                                 "seams to take all of the red away"));
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  g_signal_connect (button, "clicked", G_CALLBACK (on_fit_remove), &d);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "after",
                                                        GIMP_TYPE_INT_RADIO_FRAME),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (right), frame (_("Size"), box), FALSE, FALSE, 0);

  fine = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_container_set_border_width (GTK_CONTAINER (fine), 6);
  gtk_box_pack_start (GTK_BOX (fine),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "rigidity",
                                                        GIMP_TYPE_SPIN_SCALE),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (fine),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "mask-strength",
                                                        GIMP_TYPE_SPIN_SCALE),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (fine),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "energy", G_TYPE_NONE),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (fine),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "max-enlarge",
                                                        GIMP_TYPE_SPIN_SCALE),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (fine),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "order", G_TYPE_NONE),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (fine),
                      gimp_procedure_dialog_get_widget (GIMP_PROCEDURE_DIALOG (dialog),
                                                        "carve-masks",
                                                        GTK_TYPE_CHECK_BUTTON),
                      FALSE, FALSE, 0);
  expander = gtk_expander_new_with_mnemonic (_("_Fine tune"));
  gtk_container_add (GTK_CONTAINER (expander), fine);
  gtk_box_pack_start (GTK_BOX (right), expander, FALSE, FALSE, 0);

  g_signal_connect (config, "notify", G_CALLBACK (on_config_changed), &d);
  /* the percentages and the first preview */
  g_object_notify (G_OBJECT (config), "width");

  gtk_widget_show_all (dialog);
  run = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));

  /* the preview thread uses d: wait for it */
  d.closing = TRUE;
  g_signal_handlers_disconnect_by_data (config, &d);
  if (d.timeout)
    g_source_remove (d.timeout);
  if (d.job)
    carve_job_cancel (d.job);
  while (d.running)
    g_main_context_iteration (NULL, TRUE);

  *masks_out = paint_view_copy_masks (d.paint);
  *changed = paint_view_get_changed (d.paint);

  gtk_widget_destroy (dialog);
  paint_view_free (d.paint);
  g_clear_pointer (&d.result, cairo_surface_destroy);
  g_free (d.preview.pixels);
  return run;
}
