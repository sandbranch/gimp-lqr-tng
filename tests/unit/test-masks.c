/* Unit tests of the painted masks (src/masks.c), without GIMP.
 *
 * Copyright 2026 David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <string.h>

#include "masks.h"

static gint
count (const guint8 *m,
       gsize         n)
{
  gsize i;
  gint  c = 0;

  for (i = 0; i < n; i++)
    c += m[i] != 0;
  return c;
}

static void
test_work_size (void)
{
  gint w, h;

  masks_work_size (6000, 4000, &w, &h);
  g_assert_cmpint (w, ==, 1024);
  g_assert_cmpint (h, ==, 683);
  masks_work_size (4000, 6000, &w, &h);
  g_assert_cmpint (w, ==, 683);
  g_assert_cmpint (h, ==, 1024);
  masks_work_size (800, 600, &w, &h);
  g_assert_cmpint (w, ==, 800);
  g_assert_cmpint (h, ==, 600);
  masks_work_size (100000, 3, &w, &h);
  g_assert_cmpint (w, ==, 1024);
  g_assert_cmpint (h, ==, 1);
}

static void
test_disc (void)
{
  Masks *m = masks_new (100, 80);
  gint   area[4] = { 0, 0, 0, 0 };
  gint   n;

  masks_paint_disc (m, MASK_KEEP, FALSE, 50, 40, 10, area);
  n = count (m->keep, 100 * 80);
  /* about pi r^2 */
  g_assert_cmpint (n, >, 290);
  g_assert_cmpint (n, <, 340);
  g_assert_cmpint (m->keep[40 * 100 + 50], ==, 255);
  g_assert_cmpint (count (m->remove, 100 * 80), ==, 0);
  g_assert_cmpint (area[0], <=, 40);
  g_assert_cmpint (area[1], <=, 30);
  g_assert_cmpint (area[0] + area[2], >=, 60);
  g_assert_cmpint (area[1] + area[3], >=, 50);
  g_assert_false (masks_empty (m, MASK_KEEP));
  g_assert_true (masks_empty (m, MASK_REMOVE));

  /* painting "remove" over it takes those pixels from "keep" */
  masks_paint_disc (m, MASK_REMOVE, FALSE, 50, 40, 5, NULL);
  g_assert_cmpint (m->remove[40 * 100 + 50], ==, 255);
  g_assert_cmpint (m->keep[40 * 100 + 50], ==, 0);
  g_assert_cmpint (count (m->keep, 100 * 80) + count (m->remove, 100 * 80), ==, n);

  /* the eraser takes both */
  masks_paint_disc (m, MASK_KEEP, TRUE, 50, 40, 20, NULL);
  g_assert_true (masks_empty (m, MASK_KEEP));
  g_assert_true (masks_empty (m, MASK_REMOVE));
  masks_free (m);
}

/* discs partly or wholly outside, tiny and huge: no writes outside */
static void
test_disc_edges (void)
{
  Masks *m = masks_new (20, 10);
  gint   area[4] = { 0, 0, 0, 0 };

  masks_paint_disc (m, MASK_KEEP, FALSE, -30, -30, 5, area);
  g_assert_true (masks_empty (m, MASK_KEEP));
  g_assert_cmpint (area[2], ==, 0);
  masks_paint_disc (m, MASK_KEEP, FALSE, 0, 0, 3, area);
  g_assert_cmpint (m->keep[0], ==, 255);
  g_assert_cmpint (area[0], ==, 0);
  g_assert_cmpint (area[1], ==, 0);
  masks_paint_disc (m, MASK_KEEP, FALSE, 5.2, 5.7, 0.1, NULL);
  g_assert_cmpint (m->keep[5 * 20 + 5], ==, 255);
  masks_paint_disc (m, MASK_REMOVE, FALSE, 10, 5, 1e6, area);
  g_assert_cmpint (count (m->remove, 200), ==, 200);
  g_assert_cmpint (area[2], ==, 20);
  g_assert_cmpint (area[3], ==, 10);
  masks_free (m);
}

static void
test_line (void)
{
  Masks *m = masks_new (200, 50);
  gint   x;

  masks_paint_line (m, MASK_REMOVE, FALSE, 10, 25, 190, 25, 2, NULL);
  for (x = 10; x < 190; x++)
    g_assert_cmpint (m->remove[25 * 200 + x], ==, 255);
  g_assert_cmpint (m->remove[25 * 200 + 5], ==, 0);
  /* a line of length 0 is a disc */
  masks_paint_line (m, MASK_KEEP, FALSE, 100, 10, 100, 10, 3, NULL);
  g_assert_cmpint (m->keep[10 * 200 + 100], ==, 255);
  masks_free (m);
}

static void
test_resample (void)
{
  Masks  *m = masks_new (4, 2);
  gfloat *r;
  gint    i;

  /* left half painted */
  for (i = 0; i < 8; i++)
    m->keep[i] = (i % 4) < 2 ? 255 : 0;

  r = masks_resample (m, MASK_KEEP, 4, 2);
  for (i = 0; i < 8; i++)
    g_assert_cmpfloat (r[i], ==, (i % 4) < 2 ? 1.0f : 0.0f);
  g_free (r);

  /* shrinking averages */
  r = masks_resample (m, MASK_KEEP, 1, 1);
  g_assert_cmpfloat_with_epsilon (r[0], 0.5f, 1e-6);
  g_free (r);
  r = masks_resample (m, MASK_KEEP, 2, 1);
  g_assert_cmpfloat_with_epsilon (r[0], 1.0f, 1e-6);
  g_assert_cmpfloat_with_epsilon (r[1], 0.0f, 1e-6);
  g_free (r);
  /* 4 to 3: the middle target pixel covers 1/3 painted, 1/3 not... */
  r = masks_resample (m, MASK_KEEP, 3, 2);
  g_assert_cmpfloat_with_epsilon (r[0], 1.0f, 1e-6);
  g_assert_cmpfloat_with_epsilon (r[1], 0.5f, 1e-6);
  g_assert_cmpfloat_with_epsilon (r[2], 0.0f, 1e-6);
  g_free (r);

  /* growing: bilinear, within 0 to 1, a step stays a step at the ends */
  r = masks_resample (m, MASK_KEEP, 40, 20);
  for (i = 0; i < 40 * 20; i++)
    {
      g_assert_cmpfloat (r[i], >=, 0.0f);
      g_assert_cmpfloat (r[i], <=, 1.0f);
    }
  g_assert_cmpfloat (r[0], ==, 1.0f);
  g_assert_cmpfloat (r[39], ==, 0.0f);
  g_free (r);
  masks_free (m);

  /* from a single pixel */
  m = masks_new (1, 1);
  m->remove[0] = 255;
  r = masks_resample (m, MASK_REMOVE, 7, 5);
  for (i = 0; i < 35; i++)
    g_assert_cmpfloat (r[i], ==, 1.0f);
  g_free (r);
  masks_free (m);
}

static void
test_set_from (void)
{
  Masks  *m = masks_new (30, 20), *n = masks_new (30, 20);
  gfloat *values;
  gsize   i;

  masks_paint_disc (m, MASK_KEEP, FALSE, 12, 9, 6, NULL);
  values = masks_resample (m, MASK_KEEP, 30, 20);
  masks_set_from (n, MASK_KEEP, values, 30, 20);
  g_assert_cmpmem (n->keep, 600, m->keep, 600);
  g_free (values);

  /* from a larger mask: a big painted square survives shrinking */
  values = g_new0 (gfloat, 300 * 200);
  for (i = 0; i < 300 * 200; i++)
    values[i] = ((i % 300) >= 100 && (i % 300) < 200) ? 1.0f : 0.0f;
  masks_set_from (n, MASK_REMOVE, values, 300, 200);
  g_assert_cmpint (n->remove[10 * 30 + 15], ==, 255);
  g_assert_cmpint (n->remove[10 * 30 + 2], ==, 0);
  g_free (values);
  masks_free (n);
  masks_free (m);
}

static void
test_undo (void)
{
  Masks     *m = masks_new (60, 40), *before;
  MasksUndo *undo;
  gint       area[4] = { 0, 0, 0, 0 };

  masks_paint_disc (m, MASK_KEEP, FALSE, 20, 20, 8, NULL);
  before = masks_copy (m);
  masks_paint_line (m, MASK_REMOVE, FALSE, 10, 10, 50, 30, 4, area);
  g_assert_true (memcmp (m->keep, before->keep, 2400) != 0 ||
                 memcmp (m->remove, before->remove, 2400) != 0);
  undo = masks_undo_new (before, area);
  masks_undo_apply (m, undo);
  g_assert_cmpmem (m->keep, 2400, before->keep, 2400);
  g_assert_cmpmem (m->remove, 2400, before->remove, 2400);
  masks_undo_free (undo);
  masks_free (before);
  masks_free (m);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/masks/work-size", test_work_size);
  g_test_add_func ("/masks/disc", test_disc);
  g_test_add_func ("/masks/disc-edges", test_disc_edges);
  g_test_add_func ("/masks/line", test_line);
  g_test_add_func ("/masks/resample", test_resample);
  g_test_add_func ("/masks/set-from", test_set_from);
  g_test_add_func ("/masks/undo", test_undo);

  return g_test_run ();
}
