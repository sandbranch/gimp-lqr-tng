/* Liquid Rescale TNG: seam carving with painted keep and remove masks
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

#include "carve.h"

#include <math.h>
#include <string.h>

struct _CarveJob
{
  GMutex     lock;
  LqrCarver *carver;     /* while carving */
  gboolean   cancelled;
};

G_DEFINE_QUARK (carve-error-quark, carve_error)

void
carve_options_init (CarveOptions *options,
                    gint          width,
                    gint          height)
{
  options->width        = width;
  options->height       = height;
  options->rigidity     = 0.0;
  options->energy       = LQR_EF_GRAD_XABS;
  options->max_enlarge  = 1.5;
  options->order        = CARVE_ORDER_WIDTH_FIRST;
  options->strength     = 1000.0;
  options->restore_size = FALSE;
  options->seams        = FALSE;
}

CarveJob *
carve_job_new (void)
{
  CarveJob *job = g_new0 (CarveJob, 1);

  g_mutex_init (&job->lock);
  return job;
}

void
carve_job_cancel (CarveJob *job)
{
  g_mutex_lock (&job->lock);
  job->cancelled = TRUE;
  if (job->carver)
    lqr_carver_cancel (job->carver);
  g_mutex_unlock (&job->lock);
}

void
carve_job_free (CarveJob *job)
{
  if (!job)
    return;
  g_mutex_clear (&job->lock);
  g_free (job);
}

static void
job_set_carver (CarveJob  *job,
                LqrCarver *carver)
{
  if (!job)
    return;
  g_mutex_lock (&job->lock);
  job->carver = carver;
  g_mutex_unlock (&job->lock);
}

static gboolean
job_cancelled (CarveJob *job)
{
  gboolean cancelled;

  if (!job)
    return FALSE;
  g_mutex_lock (&job->lock);
  cancelled = job->cancelled;
  g_mutex_unlock (&job->lock);
  return cancelled;
}

/* one pass changes one side of the current image; liblqr reads past its
 * buffers when a pass starts on an image of 1 pixel in either direction */
static gboolean
passes_supported (gint       width,
                  gint       height,
                  gint       new_width,
                  gint       new_height,
                  CarveOrder order)
{
  if (order == CARVE_ORDER_WIDTH_FIRST)
    return (new_width == width || (width > 1 && height > 1)) &&
           (new_height == height || (new_width > 1 && height > 1));

  return (new_height == height || (width > 1 && height > 1)) &&
         (new_width == width || (width > 1 && new_height > 1));
}

gboolean
carve_size_supported (gint       width,
                      gint       height,
                      gint       new_width,
                      gint       new_height,
                      CarveOrder order)
{
  return width > 0 && height > 0 && new_width > 0 && new_height > 0 &&
         passes_supported (width, height, new_width, new_height, order);
}

/* the order to carve in: the asked one, or the other one where only that
 * works; FALSE if neither does */
static gboolean
choose_order (const CarveImage   *image,
              const CarveOptions *options,
              CarveOrder         *order)
{
  CarveOrder orders[2];
  gint       i;

  orders[0] = options->order;
  orders[1] = options->order == CARVE_ORDER_WIDTH_FIRST ?
              CARVE_ORDER_HEIGHT_FIRST : CARVE_ORDER_WIDTH_FIRST;

  for (i = 0; i < 2; i++)
    {
      if (!carve_size_supported (image->width, image->height,
                                 options->width, options->height, orders[i]))
        continue;
      /* carving back runs in the same order */
      if (options->restore_size &&
          !carve_size_supported (options->width, options->height,
                                 image->width, image->height, orders[i]))
        continue;
      *order = orders[i];
      return TRUE;
    }
  return FALSE;
}

static gboolean
size_matches (const CarveImage *other,
              const CarveImage *image,
              gint              channels)
{
  return other == NULL ||
         (other->width == image->width && other->height == image->height &&
          (channels == 0 ? (other->channels >= 1 && other->channels <= 4)
                         : other->channels == channels) &&
          other->pixels != NULL);
}

/* a copy for liblqr, which takes it over; values that are not finite
 * count as 0, as they would turn the energy and the seams into nonsense */
static gfloat *
copy_pixels (const CarveImage *image)
{
  gsize   n = (gsize) image->width * image->height * image->channels;
  gfloat *copy = g_try_new (gfloat, n);
  gsize   i;

  if (!copy)
    return NULL;
  for (i = 0; i < n; i++)
    copy[i] = isfinite (image->pixels[i]) ? image->pixels[i] : 0.0f;
  return copy;
}

static gboolean
set_error (GError   **error,
           LqrRetVal  ret)
{
  switch (ret)
    {
    case LQR_OK:
      return TRUE;
    case LQR_NOMEM:
      g_set_error_literal (error, CARVE_ERROR, CARVE_ERROR_MEMORY,
                           "Not enough memory for seam carving");
      break;
    case LQR_USRCANCEL:
      g_set_error_literal (error, CARVE_ERROR, CARVE_ERROR_CANCELLED,
                           "Seam carving was cancelled");
      break;
    default:
      g_set_error_literal (error, CARVE_ERROR, CARVE_ERROR_FAILED,
                           "Seam carving failed");
      break;
    }
  return FALSE;
}

/* the (current) image of a carver into a new buffer */
static gboolean
read_carver (LqrCarver  *carver,
             CarveImage *out)
{
  gint  x, y;
  void *rgb;

  out->width    = lqr_carver_get_width (carver);
  out->height   = lqr_carver_get_height (carver);
  out->channels = lqr_carver_get_channels (carver);
  out->pixels   = g_try_new (gfloat, (gsize) out->width * out->height *
                                     out->channels);
  if (!out->pixels)
    return FALSE;

  lqr_carver_scan_reset (carver);
  while (lqr_carver_scan_ext (carver, &x, &y, &rgb))
    memcpy (out->pixels + ((gsize) y * out->width + x) * out->channels,
            rgb, out->channels * sizeof (gfloat));
  return TRUE;
}

/* the bias of the masks: + strength where to keep, - where to remove.
 * lqr_carver_bias_add () adds bias_factor * value / 2. */
static LqrRetVal
add_bias (LqrCarver        *carver,
          const CarveImage *keep,
          const CarveImage *remove,
          gdouble           strength,
          gint              width,
          gint              height)
{
  gsize     n = (gsize) width * height, i;
  gdouble  *bias;
  LqrRetVal ret;

  if ((!keep && !remove) || strength <= 0.0)
    return LQR_OK;

  bias = g_try_new0 (gdouble, n);
  if (!bias)
    return LQR_NOMEM;
  for (i = 0; i < n; i++)
    {
      if (keep)
        bias[i] += CLAMP (keep->pixels[i], 0.0f, 1.0f) * strength;
      if (remove)
        bias[i] -= CLAMP (remove->pixels[i], 0.0f, 1.0f) * strength;
    }
  ret = lqr_carver_bias_add (carver, bias, 2);
  g_free (bias);
  return ret;
}

/* an image as an attached carver, which the seams of carver carve too */
static LqrCarver *
attach_image (LqrCarver        *carver,
              const CarveImage *other,
              LqrRetVal        *ret)
{
  gfloat    *copy = copy_pixels (other);
  LqrCarver *aux;

  if (!copy)
    {
      *ret = LQR_NOMEM;
      return NULL;
    }
  aux = lqr_carver_new_ext (copy, other->width, other->height,
                            other->channels, LQR_COLDEPTH_32F);
  if (!aux)
    {
      g_free (copy);
      *ret = LQR_NOMEM;
      return NULL;
    }
  *ret = lqr_carver_attach (carver, aux);
  if (*ret != LQR_OK)
    {
      lqr_carver_destroy (aux);
      return NULL;
    }
  return aux;
}

/* the rigidity mask: see CARVE_STRAIGHT_RIGIDITY; the rigidity of the
 * carver is that of a fully painted pixel */
static gdouble
straight_rigidity (const CarveOptions *options)
{
  return 3.0 * MAX (MAX (options->rigidity, 0.0), CARVE_STRAIGHT_RIGIDITY);
}

static LqrRetVal
add_rigidity (LqrCarver          *carver,
              const CarveImage   *rigid,
              const CarveOptions *options)
{
  gsize     n = (gsize) rigid->width * rigid->height, i;
  gdouble   base = MAX (options->rigidity, 0.0);
  gdouble   full = straight_rigidity (options);
  gdouble  *values;
  LqrRetVal ret;

  values = g_try_new (gdouble, n);
  if (!values)
    return LQR_NOMEM;
  for (i = 0; i < n; i++)
    values[i] = (base + CLAMP (rigid->pixels[i], 0.0f, 1.0f) * (full - base)) /
                full;
  ret = lqr_carver_rigmask_add (carver, values);
  g_free (values);
  return ret;
}

/* liblqr's seam maps, one per pass */
static gboolean
read_seams (LqrCarver   *carver,
            CarveResult *result)
{
  LqrVMapList *list;
  gint         n = 0;

  for (list = lqr_vmap_list_start (carver); list; list = lqr_vmap_list_next (list))
    n++;
  if (n == 0)
    return TRUE;
  result->seams = g_try_new0 (CarveSeams, n);
  if (!result->seams)
    return FALSE;

  for (list = lqr_vmap_list_start (carver); list; list = lqr_vmap_list_next (list))
    {
      LqrVMap    *vmap = lqr_vmap_list_current (list);
      CarveSeams *seams = &result->seams[result->n_seams];
      gint       *data = lqr_vmap_get_data (vmap);
      gint        depth = lqr_vmap_get_depth (vmap);
      gsize       size, i;

      seams->height       = lqr_vmap_get_orientation (vmap) != 0;
      seams->map.width    = lqr_vmap_get_width (vmap);
      seams->map.height   = lqr_vmap_get_height (vmap);
      seams->map.channels = 1;
      size = (gsize) seams->map.width * seams->map.height;
      seams->map.pixels = g_try_new (gfloat, size);
      if (!seams->map.pixels)
        return FALSE;
      result->n_seams++;
      for (i = 0; i < size; i++)
        seams->map.pixels[i] = data[i] == 0 ? 0.0f :
                               (gfloat) (depth + 1 - data[i]) / (depth + 1);
    }
  return TRUE;
}

void
carve_result_clear (CarveResult *result)
{
  gint i;

  g_clear_pointer (&result->image.pixels, g_free);
  g_clear_pointer (&result->keep.pixels, g_free);
  g_clear_pointer (&result->remove.pixels, g_free);
  g_clear_pointer (&result->rigid.pixels, g_free);
  for (i = 0; i < result->n_extras; i++)
    g_free (result->extras[i].pixels);
  g_clear_pointer (&result->extras, g_free);
  result->n_extras = 0;
  for (i = 0; i < result->n_seams; i++)
    g_free (result->seams[i].map.pixels);
  g_clear_pointer (&result->seams, g_free);
  result->n_seams = 0;
}

gboolean
carve (const CarveImage   *image,
       const CarveImage   *keep,
       const CarveImage   *remove,
       const CarveOptions *options,
       LqrProgress        *progress,
       CarveJob           *job,
       CarveImage         *result,
       CarveImage         *keep_result,
       CarveImage         *remove_result,
       GError            **error)
{
  CarveInput  input = { keep, remove, NULL,
                        keep_result != NULL || remove_result != NULL,
                        NULL, 0 };
  CarveResult out;

  if (!carve_full (image, &input, options, progress, job, &out, error))
    {
      memset (result, 0, sizeof (CarveImage));
      if (keep_result)
        memset (keep_result, 0, sizeof (CarveImage));
      if (remove_result)
        memset (remove_result, 0, sizeof (CarveImage));
      return FALSE;
    }
  *result = out.image;
  out.image.pixels = NULL;
  if (keep_result)
    {
      *keep_result = out.keep;
      out.keep.pixels = NULL;
    }
  if (remove_result)
    {
      *remove_result = out.remove;
      out.remove.pixels = NULL;
    }
  carve_result_clear (&out);
  return TRUE;
}

gboolean
carve_full (const CarveImage   *image,
            const CarveInput   *input,
            const CarveOptions *options,
            LqrProgress        *progress,
            CarveJob           *job,
            CarveResult        *result,
            GError            **error)
{
  LqrCarver  *carver = NULL, *keep_carver = NULL, *remove_carver = NULL;
  LqrCarver  *rigid_carver = NULL;
  LqrCarver **extra_carvers = NULL;
  const CarveImage *keep, *remove, *rigid;
  CarveOrder  order;
  gfloat     *pixels;
  LqrRetVal   ret = LQR_OK;
  gboolean    ok = FALSE;
  gint        n_extras, i;

  g_return_val_if_fail (image != NULL && image->pixels != NULL, FALSE);
  g_return_val_if_fail (image->channels >= 1 && image->channels <= 4, FALSE);
  g_return_val_if_fail (input != NULL && options != NULL && result != NULL,
                        FALSE);
  g_return_val_if_fail (input->n_extras == 0 || input->extras, FALSE);

  keep     = input->keep;
  remove   = input->remove;
  rigid    = input->rigid;
  n_extras = input->n_extras;
  memset (result, 0, sizeof (CarveResult));

  ok = size_matches (keep, image, 1) && size_matches (remove, image, 1) &&
       size_matches (rigid, image, 1);
  for (i = 0; i < n_extras; i++)
    ok = ok && size_matches (&input->extras[i], image, 0);
  if (!ok)
    {
      g_set_error_literal (error, CARVE_ERROR, CARVE_ERROR_FAILED,
                           "The masks must have the size of the image");
      if (progress)
        g_free (progress);
      return FALSE;
    }
  ok = FALSE;
  if (!choose_order (image, options, &order))
    {
      g_set_error (error, CARVE_ERROR, CARVE_ERROR_SIZE,
                   "Cannot carve %d x %d to %d x %d: seam carving needs "
                   "at least 2 pixels across the side it carves",
                   image->width, image->height,
                   options->width, options->height);
      if (progress)
        g_free (progress);
      return FALSE;
    }

  pixels = copy_pixels (image);
  if (pixels)
    carver = lqr_carver_new_ext (pixels, image->width, image->height,
                                 image->channels, LQR_COLDEPTH_32F);
  if (!carver)
    {
      g_free (pixels);
      if (progress)
        g_free (progress);
      return set_error (error, LQR_NOMEM);
    }
  if (progress)
    lqr_carver_set_progress (carver, progress);

  ret = lqr_carver_init (carver, 1,
                         (gfloat) (rigid ? straight_rigidity (options)
                                         : MAX (options->rigidity, 0.0)));
  if (ret == LQR_OK)
    ret = add_bias (carver, keep, remove, options->strength,
                    image->width, image->height);
  if (ret == LQR_OK && rigid)
    ret = add_rigidity (carver, rigid, options);
  /* the masks follow the seams: for the result, and the kept parts for
   * carving back to the original size */
  if (ret == LQR_OK && keep && (input->masks_along || options->restore_size))
    keep_carver = attach_image (carver, keep, &ret);
  if (ret == LQR_OK && remove && input->masks_along)
    remove_carver = attach_image (carver, remove, &ret);
  if (ret == LQR_OK && rigid && input->masks_along)
    rigid_carver = attach_image (carver, rigid, &ret);
  if (n_extras > 0)
    extra_carvers = g_new0 (LqrCarver *, n_extras);
  for (i = 0; i < n_extras && ret == LQR_OK; i++)
    extra_carvers[i] = attach_image (carver, &input->extras[i], &ret);
  if (ret != LQR_OK)
    goto out;

  lqr_carver_set_energy_function_builtin (carver, options->energy);
  lqr_carver_set_resize_order (carver, order == CARVE_ORDER_WIDTH_FIRST ?
                                       LQR_RES_ORDER_HOR : LQR_RES_ORDER_VERT);
  lqr_carver_set_side_switch_frequency (carver, 2);
  if (options->seams)
    lqr_carver_set_dump_vmaps (carver);
  ret = lqr_carver_set_enl_step (carver, (gfloat) CLAMP (options->max_enlarge,
                                                         1.05, 2.0));
  if (ret != LQR_OK)
    goto out;

  job_set_carver (job, carver);
  if (job_cancelled (job))
    ret = LQR_USRCANCEL;
  else
    ret = lqr_carver_resize (carver, options->width, options->height);

  if (ret == LQR_OK && options->restore_size &&
      (options->width != image->width || options->height != image->height))
    {
      /* start again from the carved image, steered by the carved "keep"
       * mask only: what was painted "remove" is gone. The rigidity mask
       * stays with the carver. */
      ret = lqr_carver_flatten (carver);
      if (ret == LQR_OK)
        lqr_carver_bias_clear (carver);
      if (ret == LQR_OK && keep_carver)
        {
          CarveImage carved_keep;

          if (!read_carver (keep_carver, &carved_keep))
            ret = LQR_NOMEM;
          else
            {
              ret = add_bias (carver, &carved_keep, NULL, options->strength,
                              carved_keep.width, carved_keep.height);
              g_free (carved_keep.pixels);
            }
        }
      if (ret == LQR_OK && job_cancelled (job))
        ret = LQR_USRCANCEL;
      if (ret == LQR_OK)
        ret = lqr_carver_resize (carver, image->width, image->height);
    }
  job_set_carver (job, NULL);
  if (ret != LQR_OK)
    goto out;

  ret = LQR_NOMEM;
  if (!read_carver (carver, &result->image))
    goto out;
  if (input->masks_along &&
      ((keep_carver && !read_carver (keep_carver, &result->keep)) ||
       (remove_carver && !read_carver (remove_carver, &result->remove)) ||
       (rigid_carver && !read_carver (rigid_carver, &result->rigid))))
    goto out;
  if (n_extras > 0)
    {
      result->extras = g_try_new0 (CarveImage, n_extras);
      if (!result->extras)
        goto out;
      result->n_extras = n_extras;
      for (i = 0; i < n_extras; i++)
        if (!read_carver (extra_carvers[i], &result->extras[i]))
          goto out;
    }
  if (options->seams && !read_seams (carver, result))
    goto out;
  ret = LQR_OK;
  ok = TRUE;

out:
  job_set_carver (job, NULL);
  /* destroys the attached carvers too */
  lqr_carver_destroy (carver);
  g_free (extra_carvers);
  if (!ok)
    {
      carve_result_clear (result);
      set_error (error, ret == LQR_OK ? LQR_ERROR : ret);
    }
  return ok;
}

gint
carve_removal_amount (const CarveImage *remove,
                      gboolean          width)
{
  gint most = 0, i, j;
  gint lines, along;

  if (!remove || !remove->pixels)
    return 0;

  lines = width ? remove->height : remove->width;
  along = width ? remove->width : remove->height;

  for (i = 0; i < lines; i++)
    {
      gint count = 0;

      for (j = 0; j < along; j++)
        {
          gsize at = width ? (gsize) i * remove->width + j
                           : (gsize) j * remove->width + i;

          if (remove->pixels[at] >= 0.5f)
            count++;
        }
      most = MAX (most, count);
    }
  return most;
}
