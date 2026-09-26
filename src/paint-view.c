/* Liquid Rescale TNG: the view to paint the masks on
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

#include "paint-view.h"

#include <math.h>
#include <string.h>

/* undo steps kept */
#define MAX_UNDO 50

/* the colours over the image, premultiplied ARGB with this alpha */
#define OVERLAY_ALPHA 120

struct _PaintView
{
  GtkWidget        *area;
  cairo_surface_t  *image;      /* the displayed size */
  gint              width;
  gint              height;
  Masks            *masks;      /* the working size */
  cairo_surface_t  *overlay;    /* the masks in colour, working size */

  PaintTool         tool;
  gdouble           brush;      /* diameter, displayed pixels */
  gboolean          show_masks;

  gboolean          painting;
  PaintTool         stroke_tool;
  gdouble           last_x;     /* working pixels */
  gdouble           last_y;
  gint              stroke_area[4];
  Masks            *before;     /* the masks when the stroke began */
  GList            *undo;       /* of MasksUndo, newest first */
  gint              n_undo;

  gboolean          pointer_in;
  gdouble           pointer_x;  /* widget pixels */
  gdouble           pointer_y;

  gboolean          changed;
  PaintViewChanged  changed_func;
  gpointer          changed_data;
};

static inline guint32
overlay_pixel (guint8 keep,
               guint8 remove)
{
  /* green (40, 200, 60) and red (230, 40, 40), premultiplied */
  if (keep)
    {
      guint a = OVERLAY_ALPHA * keep / 255;

      return (a << 24) | ((40 * a / 255) << 16) | ((200 * a / 255) << 8) |
             (60 * a / 255);
    }
  if (remove)
    {
      guint a = OVERLAY_ALPHA * remove / 255;

      return (a << 24) | ((230 * a / 255) << 16) | ((40 * a / 255) << 8) |
             (40 * a / 255);
    }
  return 0;
}

static void
update_overlay (PaintView *view,
                const gint area[4])
{
  guchar *data;
  gint    stride, x, y;

  if (area[2] <= 0 || area[3] <= 0)
    return;
  cairo_surface_flush (view->overlay);
  data   = cairo_image_surface_get_data (view->overlay);
  stride = cairo_image_surface_get_stride (view->overlay);
  for (y = area[1]; y < area[1] + area[3]; y++)
    {
      guint32 *row = (guint32 *) (data + (gsize) y * stride);

      for (x = area[0]; x < area[0] + area[2]; x++)
        {
          gsize at = (gsize) y * view->masks->width + x;

          row[x] = overlay_pixel (view->masks->keep[at], view->masks->remove[at]);
        }
    }
  cairo_surface_mark_dirty_rectangle (view->overlay, area[0], area[1],
                                      area[2], area[3]);
}

static void
update_overlay_all (PaintView *view)
{
  gint all[4] = { 0, 0, view->masks->width, view->masks->height };

  update_overlay (view, all);
}

/* where the image starts in the widget: centred */
static void
origin (PaintView *view,
        gdouble   *x,
        gdouble   *y)
{
  *x = floor ((gtk_widget_get_allocated_width (view->area) - view->width) / 2.0);
  *y = floor ((gtk_widget_get_allocated_height (view->area) - view->height) / 2.0);
}

static void
to_work (PaintView *view,
         gdouble    wx,
         gdouble    wy,
         gdouble   *x,
         gdouble   *y)
{
  gdouble ox, oy;

  origin (view, &ox, &oy);
  *x = (wx - ox) * view->masks->width / view->width;
  *y = (wy - oy) * view->masks->height / view->height;
}

static gdouble
work_radius (PaintView *view)
{
  return view->brush / 2.0 * view->masks->width / view->width;
}

static void
notify_changed (PaintView *view)
{
  view->changed = TRUE;
  if (view->changed_func)
    view->changed_func (view, view->changed_data);
}

static void
paint_to (PaintView *view,
          gdouble    x,
          gdouble    y)
{
  gint area[4] = { 0, 0, 0, 0 };

  masks_paint_line (view->masks,
                    view->stroke_tool == PAINT_TOOL_REMOVE ? MASK_REMOVE : MASK_KEEP,
                    view->stroke_tool == PAINT_TOOL_ERASE,
                    view->last_x, view->last_y, x, y, work_radius (view), area);
  view->last_x = x;
  view->last_y = y;
  update_overlay (view, area);
  if (area[2] > 0 && area[3] > 0)
    {
      gint *s = view->stroke_area;

      if (s[2] <= 0 || s[3] <= 0)
        memcpy (s, area, sizeof (area));
      else
        {
          gint x1 = MAX (s[0] + s[2], area[0] + area[2]);
          gint y1 = MAX (s[1] + s[3], area[1] + area[3]);

          s[0] = MIN (s[0], area[0]);
          s[1] = MIN (s[1], area[1]);
          s[2] = x1 - s[0];
          s[3] = y1 - s[1];
        }
    }
}

static void
push_undo (PaintView *view,
           MasksUndo *undo)
{
  view->undo = g_list_prepend (view->undo, undo);
  if (++view->n_undo > MAX_UNDO)
    {
      GList *last = g_list_last (view->undo);

      masks_undo_free (last->data);
      view->undo = g_list_delete_link (view->undo, last);
      view->n_undo--;
    }
}

static void
end_stroke (PaintView *view)
{
  if (!view->painting)
    return;
  view->painting = FALSE;
  if (view->stroke_area[2] > 0 && view->stroke_area[3] > 0)
    {
      push_undo (view, masks_undo_new (view->before, view->stroke_area));
      notify_changed (view);
    }
  g_clear_pointer (&view->before, masks_free);
}

static gboolean
on_press (GtkWidget      *widget,
          GdkEventButton *event,
          PaintView      *view)
{
  if (event->type != GDK_BUTTON_PRESS || (event->button != 1 && event->button != 3))
    return FALSE;
  end_stroke (view);
  view->painting = TRUE;
  /* the right button erases */
  view->stroke_tool = event->button == 3 ? PAINT_TOOL_ERASE : view->tool;
  view->before = masks_copy (view->masks);
  memset (view->stroke_area, 0, sizeof (view->stroke_area));
  to_work (view, event->x, event->y, &view->last_x, &view->last_y);
  paint_to (view, view->last_x, view->last_y);
  gtk_widget_queue_draw (widget);
  return TRUE;
}

static gboolean
on_motion (GtkWidget      *widget,
           GdkEventMotion *event,
           PaintView      *view)
{
  view->pointer_in = TRUE;
  view->pointer_x = event->x;
  view->pointer_y = event->y;
  if (view->painting)
    {
      gdouble x, y;

      to_work (view, event->x, event->y, &x, &y);
      paint_to (view, x, y);
    }
  gtk_widget_queue_draw (widget);
  return TRUE;
}

static gboolean
on_release (GtkWidget      *widget,
            GdkEventButton *event,
            PaintView      *view)
{
  if (event->button != 1 && event->button != 3)
    return FALSE;
  end_stroke (view);
  gtk_widget_queue_draw (widget);
  return TRUE;
}

static gboolean
on_crossing (GtkWidget        *widget,
             GdkEventCrossing *event,
             PaintView        *view)
{
  view->pointer_in = event->type == GDK_ENTER_NOTIFY;
  gtk_widget_queue_draw (widget);
  return FALSE;
}

static gboolean
on_draw (GtkWidget *widget,
         cairo_t   *cr,
         PaintView *view)
{
  gdouble ox, oy;
  gint    x, y;

  origin (view, &ox, &oy);
  cairo_save (cr);
  cairo_translate (cr, ox, oy);

  /* checks under transparent parts */
  cairo_rectangle (cr, 0, 0, view->width, view->height);
  cairo_clip (cr);
  cairo_set_source_rgb (cr, 0.6, 0.6, 0.6);
  cairo_paint (cr);
  cairo_set_source_rgb (cr, 0.4, 0.4, 0.4);
  for (y = 0; y < view->height; y += 8)
    for (x = (y / 8) % 2 * 8; x < view->width; x += 16)
      cairo_rectangle (cr, x, y, 8, 8);
  cairo_fill (cr);

  cairo_set_source_surface (cr, view->image, 0, 0);
  cairo_paint (cr);

  if (view->show_masks)
    {
      cairo_save (cr);
      cairo_scale (cr, (gdouble) view->width / view->masks->width,
                   (gdouble) view->height / view->masks->height);
      cairo_set_source_surface (cr, view->overlay, 0, 0);
      cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  cairo_restore (cr);

  /* the brush */
  if (view->pointer_in)
    {
      cairo_set_line_width (cr, 1.0);
      cairo_arc (cr, view->pointer_x, view->pointer_y, view->brush / 2.0, 0, 2 * G_PI);
      cairo_set_source_rgba (cr, 0, 0, 0, 0.8);
      cairo_stroke_preserve (cr);
      cairo_set_dash (cr, (gdouble[]) { 3.0 }, 1, 0);
      cairo_set_source_rgba (cr, 1, 1, 1, 0.9);
      cairo_stroke (cr);
    }
  return TRUE;
}

PaintView *
paint_view_new (cairo_surface_t  *image,
                Masks            *masks,
                PaintViewChanged  changed,
                gpointer          data)
{
  PaintView *view = g_new0 (PaintView, 1);

  view->image  = image;
  view->width  = cairo_image_surface_get_width (image);
  view->height = cairo_image_surface_get_height (image);
  view->masks  = masks;
  view->overlay = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                              masks->width, masks->height);
  update_overlay_all (view);

  view->tool         = PAINT_TOOL_KEEP;
  view->brush        = 24.0;
  view->show_masks   = TRUE;
  view->changed_func = changed;
  view->changed_data = data;

  view->area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (view->area, view->width, view->height);
  gtk_widget_add_events (view->area,
                         GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                         GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
                         GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (view->area, "draw", G_CALLBACK (on_draw), view);
  g_signal_connect (view->area, "button-press-event", G_CALLBACK (on_press), view);
  g_signal_connect (view->area, "motion-notify-event", G_CALLBACK (on_motion), view);
  g_signal_connect (view->area, "button-release-event", G_CALLBACK (on_release), view);
  g_signal_connect (view->area, "enter-notify-event", G_CALLBACK (on_crossing), view);
  g_signal_connect (view->area, "leave-notify-event", G_CALLBACK (on_crossing), view);
  /* the widget may go first (with the dialog) */
  g_object_add_weak_pointer (G_OBJECT (view->area), (gpointer *) &view->area);
  return view;
}

void
paint_view_free (PaintView *view)
{
  if (!view)
    return;
  if (view->area)
    {
      g_signal_handlers_disconnect_by_data (view->area, view);
      g_object_remove_weak_pointer (G_OBJECT (view->area), (gpointer *) &view->area);
    }
  g_list_free_full (view->undo, (GDestroyNotify) masks_undo_free);
  masks_free (view->before);
  masks_free (view->masks);
  cairo_surface_destroy (view->overlay);
  cairo_surface_destroy (view->image);
  g_free (view);
}

GtkWidget *
paint_view_get_widget (PaintView *view)
{
  return view->area;
}

const Masks *
paint_view_get_masks (PaintView *view)
{
  return view->masks;
}

Masks *
paint_view_copy_masks (PaintView *view)
{
  return masks_copy (view->masks);
}

gboolean
paint_view_get_changed (PaintView *view)
{
  return view->changed;
}

void
paint_view_set_tool (PaintView *view,
                     PaintTool  tool)
{
  view->tool = tool;
}

void
paint_view_set_brush (PaintView *view,
                      gdouble    diameter)
{
  view->brush = MAX (1.0, diameter);
  if (view->area)
    gtk_widget_queue_draw (view->area);
}

void
paint_view_set_show_masks (PaintView *view,
                           gboolean   show)
{
  view->show_masks = show;
  if (view->area)
    gtk_widget_queue_draw (view->area);
}

gboolean
paint_view_can_undo (PaintView *view)
{
  return view->undo != NULL;
}

void
paint_view_undo (PaintView *view)
{
  MasksUndo *undo;

  end_stroke (view);
  if (!view->undo)
    return;
  undo = view->undo->data;
  view->undo = g_list_delete_link (view->undo, view->undo);
  view->n_undo--;
  masks_undo_apply (view->masks, undo);
  update_overlay (view, undo->area);
  masks_undo_free (undo);
  if (view->area)
    gtk_widget_queue_draw (view->area);
  notify_changed (view);
}

void
paint_view_clear (PaintView *view)
{
  gint all[4] = { 0, 0, view->masks->width, view->masks->height };

  end_stroke (view);
  if (masks_empty (view->masks, MASK_KEEP) && masks_empty (view->masks, MASK_REMOVE))
    return;
  push_undo (view, masks_undo_new (view->masks, all));
  masks_clear (view->masks);
  update_overlay_all (view);
  if (view->area)
    gtk_widget_queue_draw (view->area);
  notify_changed (view);
}
