/* Liquid Rescale Paint: seam carving with painted keep and remove masks
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
 * The seam carving itself, on plain float buffers, with liblqr. Nothing
 * here knows GIMP or GTK, so the unit tests can run it on their own, and
 * the dialog can run it in a thread for the live preview.
 */

#ifndef __CARVE_H__
#define __CARVE_H__

#include <glib.h>
#include <lqr.h>

G_BEGIN_DECLS

#define CARVE_ERROR (carve_error_quark ())

typedef enum
{
  CARVE_ERROR_SIZE,       /* a size liblqr cannot carve to */
  CARVE_ERROR_MEMORY,
  CARVE_ERROR_CANCELLED,
  CARVE_ERROR_FAILED
} CarveError;

/* an image or a mask: width x height pixels of channels floats, row by
 * row; 1 channel is gray (or a mask), 2 gray with alpha, 3 RGB, 4 RGBA */
typedef struct
{
  gint    width;
  gint    height;
  gint    channels;
  gfloat *pixels;
} CarveImage;

typedef enum
{
  CARVE_ORDER_WIDTH_FIRST,
  CARVE_ORDER_HEIGHT_FIRST
} CarveOrder;

typedef struct
{
  gint       width;         /* the size to carve to */
  gint       height;
  gdouble    rigidity;      /* 0 and up: how much seams avoid bending */
  gint       energy;        /* an LqrEnergyFuncBuiltinType */
  gdouble    max_enlarge;   /* the largest step when enlarging, as a
                             * factor, e.g. 1.5 */
  CarveOrder order;
  gdouble    strength;      /* how strongly the masks steer the seams;
                             * liblqr's bias of a fully painted pixel */
  gboolean   restore_size;  /* then carve back to the original size: the
                             * painted "remove" parts stay removed */
} CarveOptions;

/* a carving that another thread can cancel; one per carve () call at a
 * time */
typedef struct _CarveJob CarveJob;

GQuark     carve_error_quark      (void);

void       carve_options_init     (CarveOptions     *options,
                                   gint              width,
                                   gint              height);

CarveJob * carve_job_new          (void);
void       carve_job_cancel       (CarveJob         *job);
void       carve_job_free         (CarveJob         *job);

/* Carves image to options->width x options->height. keep and remove are
 * masks of the size of image (0 to 1 per pixel, either may be NULL).
 * On success, result holds the carved image (free result->pixels with
 * g_free), and if keep_result or remove_result are not NULL, the masks
 * carved along the same seams. progress, if not NULL, is taken over by
 * the carver. job, if not NULL, makes the carving cancellable. */
gboolean   carve                  (const CarveImage   *image,
                                   const CarveImage   *keep,
                                   const CarveImage   *remove,
                                   const CarveOptions *options,
                                   LqrProgress        *progress,
                                   CarveJob           *job,
                                   CarveImage         *result,
                                   CarveImage         *keep_result,
                                   CarveImage         *remove_result,
                                   GError            **error);

/* The same, also carving n_extras more images (such as a layer mask, of
 * any number of channels, of the size of image) along the same seams, into
 * extra_results. */
gboolean   carve_full             (const CarveImage   *image,
                                   const CarveImage   *keep,
                                   const CarveImage   *remove,
                                   const CarveOptions *options,
                                   LqrProgress        *progress,
                                   CarveJob           *job,
                                   CarveImage         *result,
                                   CarveImage         *keep_result,
                                   CarveImage         *remove_result,
                                   const CarveImage   *extras,
                                   gint                n_extras,
                                   CarveImage         *extra_results,
                                   GError            **error);

/* How many pixels the width (or the height) must shrink by so that seams
 * can take every painted "remove" pixel away: the most remove pixels in
 * any one row (or column). */
gint       carve_removal_amount   (const CarveImage   *remove,
                                   gboolean            width);

/* whether liblqr can carve width x height to new_width x new_height in
 * the given order; it cannot start a pass on an image that is 1 pixel
 * wide or high (it reads past its buffers) */
gboolean   carve_size_supported   (gint               width,
                                   gint               height,
                                   gint               new_width,
                                   gint               new_height,
                                   CarveOrder         order);

G_END_DECLS

#endif /* __CARVE_H__ */
