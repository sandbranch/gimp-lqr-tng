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

#ifndef __PAINT_VIEW_H__
#define __PAINT_VIEW_H__

#include <gtk/gtk.h>

#include "masks.h"

G_BEGIN_DECLS

typedef enum
{
  PAINT_TOOL_KEEP,
  PAINT_TOOL_REMOVE,
  PAINT_TOOL_ERASE
} PaintTool;

typedef struct _PaintView PaintView;

typedef void (* PaintViewChanged) (PaintView *view,
                                   gpointer   data);

/* a view of image (a cairo image surface of the displayed size, taken
 * over) with masks painted over it; the view owns masks. changed is
 * called when a stroke ends, on undo and on clear. */
PaintView * paint_view_new            (cairo_surface_t  *image,
                                       Masks            *masks,
                                       PaintViewChanged  changed,
                                       gpointer          data);
void        paint_view_free           (PaintView        *view);

GtkWidget * paint_view_get_widget     (PaintView        *view);
const Masks * paint_view_get_masks    (PaintView        *view);
Masks     * paint_view_copy_masks     (PaintView        *view);
gboolean    paint_view_get_changed    (PaintView        *view);

void        paint_view_set_tool       (PaintView        *view,
                                       PaintTool         tool);
void        paint_view_set_brush      (PaintView        *view,
                                       gdouble           diameter);
void        paint_view_set_show_masks (PaintView        *view,
                                       gboolean          show);

gboolean    paint_view_can_undo       (PaintView        *view);
void        paint_view_undo           (PaintView        *view);
void        paint_view_clear          (PaintView        *view);

G_END_DECLS

#endif /* __PAINT_VIEW_H__ */
