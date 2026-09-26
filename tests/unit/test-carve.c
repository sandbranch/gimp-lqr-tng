/* Unit tests of the seam carving (src/carve.c), without GIMP.
 *
 * Copyright 2026 David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The images are noise, so every pixel is different and it is exact to
 * check which ones a seam took: when carving only shrinks, each row of the
 * result is its row of the image with pixels taken out, in order. Objects
 * are flat gray (OBJECT in every colour channel), which noise never is
 * exactly; being flat, they have no energy, so seams go through them
 * unless a mask says otherwise. Alpha, where there is one, is 1.
 */

#include <math.h>
#include <string.h>

#include "carve.h"

#define OBJECT 0.5f

static gint
colour_channels (gint channels)
{
  return (channels == 2 || channels == 4) ? channels - 1 : channels;
}

/* an image of noise, channels per pixel; columns x0 to x1 - 1 (or rows,
 * if rows is TRUE) are a flat object */
static CarveImage
noise_image (gint     width,
             gint     height,
             gint     channels,
             gint     x0,
             gint     x1,
             gboolean rows,
             guint32  seed)
{
  CarveImage image = { width, height, channels, NULL };
  GRand     *rand = g_rand_new_with_seed (seed);
  gint       x, y, c;

  image.pixels = g_new (gfloat, (gsize) width * height * channels);
  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      {
        gfloat *p = image.pixels + ((gsize) y * width + x) * channels;
        gint    at = rows ? y : x;

        for (c = 0; c < channels; c++)
          p[c] = g_rand_double (rand);
        if (at >= x0 && at < x1)
          for (c = 0; c < colour_channels (channels); c++)
            p[c] = OBJECT;
        if (colour_channels (channels) < channels)
          p[channels - 1] = 1.0f;
      }
  g_rand_free (rand);
  return image;
}

/* a mask of the size of image, 1 over columns (or rows) a0 to a1 - 1 */
static CarveImage
band_mask (const CarveImage *image,
           gint              a0,
           gint              a1,
           gboolean          rows)
{
  CarveImage mask = { image->width, image->height, 1, NULL };
  gint       x, y;

  mask.pixels = g_new0 (gfloat, (gsize) image->width * image->height);
  for (y = 0; y < image->height; y++)
    for (x = 0; x < image->width; x++)
      {
        gint at = rows ? y : x;

        if (at >= a0 && at < a1)
          mask.pixels[(gsize) y * image->width + x] = 1.0f;
      }
  return mask;
}

/* marked (object) pixels in row y, or in column x */
static gint
marked_in_row (const CarveImage *image,
               gint              y)
{
  gint x, c, n = 0;

  for (x = 0; x < image->width; x++)
    {
      const gfloat *p = image->pixels +
                        ((gsize) y * image->width + x) * image->channels;
      gboolean      object = TRUE;

      for (c = 0; c < colour_channels (image->channels); c++)
        object = object && p[c] == OBJECT;
      n += object;
    }
  return n;
}

static gint
marked_total (const CarveImage *image)
{
  gint y, n = 0;

  for (y = 0; y < image->height; y++)
    n += marked_in_row (image, y);
  return n;
}

/* each row of result is its row of image with pixels taken out, in
 * order (true when only the width shrank) */
static gboolean
rows_are_subsequences (const CarveImage *image,
                       const CarveImage *result)
{
  gint y;

  if (result->height != image->height || result->channels != image->channels)
    return FALSE;
  for (y = 0; y < image->height; y++)
    {
      gint x = 0, rx;

      for (rx = 0; rx < result->width; rx++)
        {
          const gfloat *r = result->pixels +
                            ((gsize) y * result->width + rx) * result->channels;

          while (x < image->width &&
                 memcmp (image->pixels + ((gsize) y * image->width + x) *
                         image->channels, r,
                         image->channels * sizeof (gfloat)) != 0)
            x++;
          if (x == image->width)
            return FALSE;
          x++;
        }
    }
  return TRUE;
}

static void
free_image (CarveImage *image)
{
  g_clear_pointer (&image->pixels, g_free);
}

static void
test_same_size (void)
{
  CarveImage   image = noise_image (40, 30, 3, 0, 0, FALSE, 1);
  CarveImage   result;
  CarveOptions options;
  GError      *error = NULL;

  carve_options_init (&options, 40, 30);
  g_assert_true (carve (&image, NULL, NULL, &options, NULL, NULL,
                        &result, NULL, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpint (result.width, ==, 40);
  g_assert_cmpint (result.height, ==, 30);
  g_assert_cmpmem (result.pixels, 40 * 30 * 3 * sizeof (gfloat),
                   image.pixels, 40 * 30 * 3 * sizeof (gfloat));
  free_image (&result);
  free_image (&image);
}

static void
test_shrink_width (void)
{
  CarveImage   image = noise_image (80, 50, 3, 0, 0, FALSE, 2);
  CarveImage   result;
  CarveOptions options;

  carve_options_init (&options, 60, 50);
  g_assert_true (carve (&image, NULL, NULL, &options, NULL, NULL,
                        &result, NULL, NULL, NULL));
  g_assert_cmpint (result.width, ==, 60);
  g_assert_cmpint (result.height, ==, 50);
  g_assert_true (rows_are_subsequences (&image, &result));
  free_image (&result);
  free_image (&image);
}

/* the object under the keep mask survives, whole, in every row */
static void
test_keep (void)
{
  CarveImage   image = noise_image (100, 40, 4, 20, 30, FALSE, 3);
  CarveImage   keep = band_mask (&image, 20, 30, FALSE);
  CarveImage   result, keep_result;
  CarveOptions options;
  gint         y;

  carve_options_init (&options, 60, 40);
  g_assert_true (carve (&image, &keep, NULL, &options, NULL, NULL,
                        &result, &keep_result, NULL, NULL));
  g_assert_true (rows_are_subsequences (&image, &result));
  for (y = 0; y < result.height; y++)
    g_assert_cmpint (marked_in_row (&result, y), ==, 10);

  /* the mask was carved along: it still covers the object */
  g_assert_cmpint (keep_result.width, ==, 60);
  g_assert_cmpint (keep_result.channels, ==, 1);
  for (y = 0; y < keep_result.height; y++)
    {
      gint x, n = 0;

      for (x = 0; x < keep_result.width; x++)
        {
          gboolean in_mask = keep_result.pixels[(gsize) y * 60 + x] > 0.5f;
          gboolean in_object = result.pixels[((gsize) y * 60 + x) * 4] == OBJECT;

          g_assert_true (in_mask == in_object);
          n += in_mask;
        }
      g_assert_cmpint (n, ==, 10);
    }
  free_image (&keep_result);
  free_image (&result);
  free_image (&keep);
  free_image (&image);
}

/* without the mask, the same carving takes pixels of the object: the
 * keep test above shows the mask, not luck */
static void
test_keep_needed (void)
{
  CarveImage   image = noise_image (100, 40, 4, 20, 30, FALSE, 3);
  CarveImage   result;
  CarveOptions options;

  carve_options_init (&options, 60, 40);
  g_assert_true (carve (&image, NULL, NULL, &options, NULL, NULL,
                        &result, NULL, NULL, NULL));
  g_assert_cmpint (marked_total (&result), <, 10 * 40);
  free_image (&result);
  free_image (&image);
}

/* the object under the remove mask goes, all of it */
static void
test_remove (void)
{
  CarveImage   image = noise_image (100, 40, 4, 40, 50, FALSE, 4);
  CarveImage   remove = band_mask (&image, 40, 50, FALSE);
  CarveImage   result, remove_result;
  CarveOptions options;
  gint         x;

  carve_options_init (&options, 90, 40);
  g_assert_true (carve (&image, NULL, &remove, &options, NULL, NULL,
                        &result, NULL, &remove_result, NULL));
  g_assert_true (rows_are_subsequences (&image, &result));
  g_assert_cmpint (marked_total (&result), ==, 0);
  for (x = 0; x < remove_result.width * remove_result.height; x++)
    g_assert_cmpfloat (remove_result.pixels[x], <, 0.5f);
  free_image (&remove_result);
  free_image (&result);
  free_image (&remove);
  free_image (&image);
}

/* remove, and carve back to the original size: the object stays gone */
static void
test_restore_size (void)
{
  CarveImage   image = noise_image (100, 40, 4, 40, 50, FALSE, 5);
  CarveImage   keep = band_mask (&image, 10, 20, FALSE);
  CarveImage   remove = band_mask (&image, 40, 50, FALSE);
  CarveImage   result;
  CarveOptions options;
  gint         y;

  carve_options_init (&options, 100 - carve_removal_amount (&remove, TRUE), 40);
  g_assert_cmpint (options.width, ==, 90);
  options.restore_size = TRUE;
  g_assert_true (carve (&image, &keep, &remove, &options, NULL, NULL,
                        &result, NULL, NULL, NULL));
  g_assert_cmpint (result.width, ==, 100);
  g_assert_cmpint (result.height, ==, 40);
  g_assert_cmpint (marked_total (&result), ==, 0);

  /* the kept columns 10 to 19 are still there, unchanged: the seams
   * inserted when widening again go elsewhere */
  for (y = 0; y < 40; y++)
    {
      const gfloat *in = image.pixels + ((gsize) y * 100 + 10) * 4;
      gint          x;
      gboolean      found = FALSE;

      for (x = 0; x + 10 <= 100 && !found; x++)
        found = memcmp (result.pixels + ((gsize) y * 100 + x) * 4, in,
                        10 * 4 * sizeof (gfloat)) == 0;
      g_assert_true (found);
    }
  free_image (&result);
  free_image (&remove);
  free_image (&keep);
  free_image (&image);
}

static void
test_shrink_height (void)
{
  CarveImage   image = noise_image (40, 100, 4, 30, 40, TRUE, 6);
  CarveImage   remove = band_mask (&image, 30, 40, TRUE);
  CarveImage   result;
  CarveOptions options;

  carve_options_init (&options, 40, 90);
  g_assert_true (carve (&image, NULL, &remove, &options, NULL, NULL,
                        &result, NULL, NULL, NULL));
  g_assert_cmpint (result.width, ==, 40);
  g_assert_cmpint (result.height, ==, 90);
  g_assert_cmpint (marked_total (&result), ==, 0);
  free_image (&result);
  free_image (&remove);
  free_image (&image);
}

static void
test_both_sides_and_order (void)
{
  CarveImage   image = noise_image (60, 50, 3, 0, 0, FALSE, 7);
  CarveOrder   orders[] = { CARVE_ORDER_WIDTH_FIRST, CARVE_ORDER_HEIGHT_FIRST };
  gint         i;

  for (i = 0; i < 2; i++)
    {
      CarveImage   result;
      CarveOptions options;

      carve_options_init (&options, 45, 70);
      options.order = orders[i];
      g_assert_true (carve (&image, NULL, NULL, &options, NULL, NULL,
                            &result, NULL, NULL, NULL));
      g_assert_cmpint (result.width, ==, 45);
      g_assert_cmpint (result.height, ==, 70);
      free_image (&result);
    }
  free_image (&image);
}

/* enlarging more than one step allows */
static void
test_enlarge (void)
{
  CarveImage   image = noise_image (40, 30, 3, 0, 0, FALSE, 8);
  CarveImage   result;
  CarveOptions options;
  gsize        i;

  carve_options_init (&options, 100, 30);
  options.max_enlarge = 1.5;
  g_assert_true (carve (&image, NULL, NULL, &options, NULL, NULL,
                        &result, NULL, NULL, NULL));
  g_assert_cmpint (result.width, ==, 100);
  for (i = 0; i < (gsize) 100 * 30 * 3; i++)
    {
      g_assert_true (isfinite (result.pixels[i]));
      g_assert_cmpfloat (result.pixels[i], >=, 0.0f);
      g_assert_cmpfloat (result.pixels[i], <=, 1.0f);
    }
  free_image (&result);
  free_image (&image);
}

static void
test_channels (void)
{
  gint channels;

  for (channels = 1; channels <= 4; channels++)
    {
      CarveImage   image = noise_image (50, 20, channels, 5, 15, FALSE, 9);
      CarveImage   keep = band_mask (&image, 5, 15, FALSE);
      CarveImage   result;
      CarveOptions options;

      carve_options_init (&options, 30, 20);
      g_assert_true (carve (&image, &keep, NULL, &options, NULL, NULL,
                            &result, NULL, NULL, NULL));
      g_assert_cmpint (result.channels, ==, channels);
      g_assert_true (rows_are_subsequences (&image, &result));
      g_assert_cmpint (marked_total (&result), ==, 10 * 20);
      free_image (&result);
      free_image (&keep);
      free_image (&image);
    }
}

static void
test_removal_amount (void)
{
  CarveImage image = noise_image (30, 20, 1, 0, 0, FALSE, 10);
  CarveImage mask = band_mask (&image, 0, 0, FALSE);
  gint       x;

  g_assert_cmpint (carve_removal_amount (&mask, TRUE), ==, 0);
  /* a triangle: row y has y + 1 pixels */
  for (x = 0; x < 30 * 20; x++)
    mask.pixels[x] = (x % 30) <= (x / 30) ? 1.0f : 0.0f;
  g_assert_cmpint (carve_removal_amount (&mask, TRUE), ==, 20);
  /* column 0 has all 20 rows */
  g_assert_cmpint (carve_removal_amount (&mask, FALSE), ==, 20);
  /* half-painted pixels do not count */
  for (x = 0; x < 30 * 20; x++)
    mask.pixels[x] *= 0.4f;
  g_assert_cmpint (carve_removal_amount (&mask, TRUE), ==, 0);
  g_assert_cmpint (carve_removal_amount (NULL, TRUE), ==, 0);
  free_image (&mask);
  free_image (&image);
}

/* sizes liblqr cannot carve are refused, not crashed on */
static void
test_sizes (void)
{
  struct { gint w, h, nw, nh; gboolean ok; } cases[] = {
    { 2, 10, 1, 10, TRUE },    /* to 1 pixel wide */
    { 10, 2, 10, 1, TRUE },
    { 1, 10, 1, 7, FALSE },    /* a pass along 1 pixel */
    { 10, 1, 7, 1, FALSE },
    { 1, 1, 1, 1, TRUE },      /* nothing to do */
    { 1, 10, 1, 10, TRUE },
    { 10, 10, 1, 1, FALSE },   /* the second pass would start at 1 pixel */
    { 10, 10, 0, 10, FALSE },
    { 3, 3, 5, 5, TRUE },
  };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      CarveImage   image = noise_image (cases[i].w, cases[i].h, 3, 0, 0, FALSE, i);
      CarveImage   result;
      CarveOptions options;
      GError      *error = NULL;
      gboolean     ok;

      carve_options_init (&options, cases[i].nw, cases[i].nh);
      ok = carve (&image, NULL, NULL, &options, NULL, NULL, &result, NULL,
                  NULL, &error);
      g_assert_cmpint (ok, ==, cases[i].ok);
      if (ok)
        {
          g_assert_no_error (error);
          g_assert_cmpint (result.width, ==, cases[i].nw);
          g_assert_cmpint (result.height, ==, cases[i].nh);
          free_image (&result);
        }
      else
        {
          g_assert_error (error, CARVE_ERROR, CARVE_ERROR_SIZE);
          g_assert_null (result.pixels);
          g_clear_error (&error);
        }
      free_image (&image);
    }
}

static void
test_restore_size_supported (void)
{
  CarveImage   image = noise_image (10, 10, 3, 0, 0, FALSE, 11);
  CarveImage   result;
  CarveOptions options;
  GError      *error = NULL;

  /* 10 x 10 to 1 x 10 is fine, but back from 1 pixel wide is not */
  carve_options_init (&options, 1, 10);
  options.restore_size = TRUE;
  g_assert_false (carve (&image, NULL, NULL, &options, NULL, NULL, &result,
                         NULL, NULL, &error));
  g_assert_error (error, CARVE_ERROR, CARVE_ERROR_SIZE);
  g_clear_error (&error);
  free_image (&image);
}

static void
test_bad_values (void)
{
  CarveImage   image = noise_image (40, 30, 3, 0, 0, FALSE, 12);
  CarveImage   result;
  CarveOptions options;
  gsize        i;

  image.pixels[100] = NAN;
  image.pixels[200] = INFINITY;
  image.pixels[300] = -INFINITY;
  carve_options_init (&options, 25, 20);
  g_assert_true (carve (&image, NULL, NULL, &options, NULL, NULL,
                        &result, NULL, NULL, NULL));
  for (i = 0; i < (gsize) 25 * 20 * 3; i++)
    g_assert_true (isfinite (result.pixels[i]));
  free_image (&result);
  free_image (&image);
}

static void
test_mask_mismatch (void)
{
  CarveImage   image = noise_image (40, 30, 3, 0, 0, FALSE, 13);
  CarveImage   other = noise_image (30, 30, 3, 0, 0, FALSE, 13);
  CarveImage   mask = band_mask (&other, 0, 5, FALSE);
  CarveImage   result;
  CarveOptions options;
  GError      *error = NULL;

  carve_options_init (&options, 25, 30);
  g_assert_false (carve (&image, &mask, NULL, &options, NULL, NULL,
                         &result, NULL, NULL, &error));
  g_assert_error (error, CARVE_ERROR, CARVE_ERROR_FAILED);
  g_clear_error (&error);
  /* a progress handed over is freed on errors too (ASan checks) */
  g_assert_false (carve (&image, &mask, NULL, &options, lqr_progress_new (),
                         NULL, &result, NULL, NULL, &error));
  g_clear_error (&error);
  free_image (&mask);
  free_image (&other);
  free_image (&image);
}

static void
test_deterministic (void)
{
  CarveImage   image = noise_image (70, 40, 4, 10, 20, FALSE, 14);
  CarveImage   keep = band_mask (&image, 10, 20, FALSE);
  CarveImage   a, b;
  CarveOptions options;

  carve_options_init (&options, 50, 35);
  options.rigidity = 2.0;
  options.energy = LQR_EF_LUMA_GRAD_NORM;
  g_assert_true (carve (&image, &keep, NULL, &options, NULL, NULL, &a,
                        NULL, NULL, NULL));
  g_assert_true (carve (&image, &keep, NULL, &options, NULL, NULL, &b,
                        NULL, NULL, NULL));
  g_assert_cmpmem (a.pixels, (gsize) 50 * 35 * 4 * sizeof (gfloat),
                   b.pixels, (gsize) 50 * 35 * 4 * sizeof (gfloat));
  free_image (&b);
  free_image (&a);
  free_image (&keep);
  free_image (&image);
}

static void
test_cancel_before (void)
{
  CarveImage   image = noise_image (40, 30, 3, 0, 0, FALSE, 15);
  CarveImage   result;
  CarveOptions options;
  CarveJob    *job = carve_job_new ();
  GError      *error = NULL;

  carve_job_cancel (job);
  carve_options_init (&options, 20, 30);
  g_assert_false (carve (&image, NULL, NULL, &options, NULL, job, &result,
                         NULL, NULL, &error));
  g_assert_error (error, CARVE_ERROR, CARVE_ERROR_CANCELLED);
  g_clear_error (&error);
  carve_job_free (job);
  free_image (&image);
}

typedef struct
{
  CarveImage   image;
  CarveOptions options;
  CarveJob    *job;
  gboolean     ok;
  GError      *error;
} ThreadData;

static gpointer
carve_thread (gpointer user_data)
{
  ThreadData *d = user_data;
  CarveImage  result;

  d->ok = carve (&d->image, NULL, NULL, &d->options, NULL, d->job, &result,
                 NULL, NULL, &d->error);
  if (d->ok)
    free_image (&result);
  return NULL;
}

/* cancelling from another thread while carving: it stops, or it had
 * already finished; it never crashes */
static void
test_cancel_from_thread (void)
{
  ThreadData d = { 0 };
  GThread   *thread;

  d.image = noise_image (600, 400, 3, 0, 0, FALSE, 16);
  carve_options_init (&d.options, 200, 150);
  d.job = carve_job_new ();
  thread = g_thread_new ("carve", carve_thread, &d);
  g_usleep (20000);
  carve_job_cancel (d.job);
  g_thread_join (thread);
  if (!d.ok)
    g_assert_error (d.error, CARVE_ERROR, CARVE_ERROR_CANCELLED);
  g_clear_error (&d.error);
  carve_job_free (d.job);
  free_image (&d.image);
}

/* images carried along (a layer mask) take the same seams: a copy of
 * the image itself comes out as the result */
static void
test_extras (void)
{
  CarveImage   image = noise_image (60, 40, 3, 10, 20, FALSE, 17);
  CarveImage   remove = band_mask (&image, 10, 20, FALSE);
  CarveImage   extras[2], extra_results[2];
  CarveImage   result;
  CarveOptions options;
  CarveJob    *job;
  GError      *error = NULL;
  gsize        i;

  extras[0] = image;
  extras[1] = noise_image (60, 40, 1, 0, 0, FALSE, 18);
  carve_options_init (&options, 45, 30);
  g_assert_true (carve_full (&image, NULL, &remove, &options, NULL, NULL,
                             &result, NULL, NULL, extras, 2, extra_results,
                             &error));
  g_assert_no_error (error);
  g_assert_cmpint (extra_results[0].width, ==, 45);
  g_assert_cmpint (extra_results[0].height, ==, 30);
  g_assert_cmpint (extra_results[1].channels, ==, 1);
  g_assert_cmpmem (extra_results[0].pixels, (gsize) 45 * 30 * 3 * sizeof (gfloat),
                   result.pixels, (gsize) 45 * 30 * 3 * sizeof (gfloat));
  for (i = 0; i < 2; i++)
    free_image (&extra_results[i]);
  free_image (&result);

  /* a failure after the checks leaves no results behind */
  job = carve_job_new ();
  carve_job_cancel (job);
  g_assert_false (carve_full (&image, NULL, &remove, &options, NULL, job,
                              &result, NULL, NULL, extras, 2, extra_results,
                              &error));
  g_assert_error (error, CARVE_ERROR, CARVE_ERROR_CANCELLED);
  g_clear_error (&error);
  g_assert_null (result.pixels);
  g_assert_null (extra_results[0].pixels);
  g_assert_null (extra_results[1].pixels);
  carve_job_free (job);

  /* an extra of another size is refused */
  extras[1].width = 59;
  g_assert_false (carve_full (&image, NULL, NULL, &options, NULL, NULL,
                              &result, NULL, NULL, extras, 2, extra_results,
                              &error));
  g_assert_error (error, CARVE_ERROR, CARVE_ERROR_FAILED);
  g_clear_error (&error);
  extras[1].width = 60;

  free_image (&extras[1]);
  free_image (&remove);
  free_image (&image);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/carve/same-size", test_same_size);
  g_test_add_func ("/carve/shrink-width", test_shrink_width);
  g_test_add_func ("/carve/keep", test_keep);
  g_test_add_func ("/carve/keep-needed", test_keep_needed);
  g_test_add_func ("/carve/remove", test_remove);
  g_test_add_func ("/carve/restore-size", test_restore_size);
  g_test_add_func ("/carve/shrink-height", test_shrink_height);
  g_test_add_func ("/carve/both-sides-and-order", test_both_sides_and_order);
  g_test_add_func ("/carve/enlarge", test_enlarge);
  g_test_add_func ("/carve/channels", test_channels);
  g_test_add_func ("/carve/removal-amount", test_removal_amount);
  g_test_add_func ("/carve/sizes", test_sizes);
  g_test_add_func ("/carve/restore-size-supported", test_restore_size_supported);
  g_test_add_func ("/carve/bad-values", test_bad_values);
  g_test_add_func ("/carve/mask-mismatch", test_mask_mismatch);
  g_test_add_func ("/carve/deterministic", test_deterministic);
  g_test_add_func ("/carve/cancel-before", test_cancel_before);
  g_test_add_func ("/carve/cancel-from-thread", test_cancel_from_thread);
  g_test_add_func ("/carve/extras", test_extras);

  return g_test_run ();
}
