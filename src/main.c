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

#include "config.h"

#include <string.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "carve.h"
#include "dialog.h"
#include "layer-io.h"
#include "lqr-tng.h"
#include "masks.h"
#include "plugin-intl.h"

/* the seam colours of the Liquid Rescale plug-in: (1, 1, 0) and
 * (0.2, 0, 0) */
#define SEAMS_START "#ffff00"
#define SEAMS_END   "#330000"

typedef struct _LqrTng      LqrTng;
typedef struct _LqrTngClass LqrTngClass;

struct _LqrTng
{
  GimpPlugIn parent_instance;
};

struct _LqrTngClass
{
  GimpPlugInClass parent_class;
};

#define LQR_TNG_TYPE (lqr_tng_get_type ())
GType lqr_tng_get_type (void) G_GNUC_CONST;

static GList          * lqr_tng_query_procedures (GimpPlugIn           *plug_in);
static GimpProcedure  * lqr_tng_create_procedure (GimpPlugIn           *plug_in,
                                                  const gchar          *name);
static GimpValueArray * lqr_tng_run              (GimpProcedure        *procedure,
                                                  GimpRunMode           run_mode,
                                                  GimpImage            *image,
                                                  GimpDrawable        **drawables,
                                                  GimpProcedureConfig  *config,
                                                  gpointer              run_data);

G_DEFINE_TYPE (LqrTng, lqr_tng, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (LQR_TNG_TYPE)

static void
lqr_tng_class_init (LqrTngClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  /* the default set_i18n: the domain is the plug-in's name, in the
   * locale folder next to it */
  plug_in_class->query_procedures = lqr_tng_query_procedures;
  plug_in_class->create_procedure = lqr_tng_create_procedure;
}

static void
lqr_tng_init (LqrTng *lqr_tng)
{
}

static GList *
lqr_tng_query_procedures (GimpPlugIn *plug_in)
{
  return g_list_append (NULL, g_strdup (PLUG_IN_PROC));
}

static GimpProcedure *
lqr_tng_create_procedure (GimpPlugIn  *plug_in,
                          const gchar *name)
{
  GimpProcedure *procedure;

  if (strcmp (name, PLUG_IN_PROC) != 0)
    return NULL;
  /* for the default colours */
  gegl_init (NULL, NULL);

  procedure = gimp_image_procedure_new (plug_in, name, GIMP_PDB_PROC_TYPE_PLUGIN,
                                        lqr_tng_run, NULL, NULL);

  gimp_procedure_set_image_types (procedure, "RGB*, GRAY*");
  gimp_procedure_set_sensitivity_mask (procedure,
                                       GIMP_PROCEDURE_SENSITIVE_DRAWABLE);
  gimp_procedure_set_menu_label (procedure, _("Liquid Rescale _TNG..."));
  gimp_procedure_add_menu_path (procedure, "<Image>/Layer");
  gimp_procedure_set_documentation (procedure,
    _("Rescale a layer by seam carving, with the parts to keep and to "
      "remove painted in the dialog"),
    _("Changes the size of a layer by taking away (or adding) the paths of "
      "pixels that matter least, so that the important parts keep their "
      "shape. Paint what to keep in green and what to remove in red. The "
      "masks are stored as hidden layers, so that the next run starts from "
      "them; any layer can serve as a mask when called from a script: "
      "its painted (not transparent, not black) pixels count. "
      "Liquid Rescale TNG follows the Liquid Rescale plug-in by Carlo "
      "Baldassi, whose library liblqr does the seam carving."),
    PLUG_IN_PROC);
  gimp_procedure_set_attribution (procedure,
                                  "David, after Liquid Rescale by Carlo Baldassi",
                                  "David", "2026");

  gimp_procedure_add_int_argument (procedure, "width", _("_Width"),
                                   _("The new width; 0 keeps the width"),
                                   0, GIMP_MAX_IMAGE_SIZE, 0,
                                   G_PARAM_READWRITE);
  gimp_procedure_add_int_argument (procedure, "height", _("_Height"),
                                   _("The new height; 0 keeps the height"),
                                   0, GIMP_MAX_IMAGE_SIZE, 0,
                                   G_PARAM_READWRITE);
  gimp_procedure_add_layer_argument (procedure, "keep-layer", _("Keep mask"),
                                     _("A layer whose painted pixels are kept"),
                                     TRUE, G_PARAM_READWRITE);
  gimp_procedure_add_layer_argument (procedure, "remove-layer", _("Remove mask"),
                                     _("A layer whose painted pixels are removed"),
                                     TRUE, G_PARAM_READWRITE);
  gimp_procedure_add_layer_argument (procedure, "rigidity-layer",
                                     _("Straight mask"),
                                     _("A layer whose painted pixels stay "
                                       "straight: the paths of pixels bend "
                                       "less there"),
                                     TRUE, G_PARAM_READWRITE);
  gimp_procedure_add_choice_argument (procedure, "after", _("After _carving"),
                                      _("What happens after carving to the "
                                        "new size"),
                                      gimp_choice_new_with_values (
                                        "crop", LQR_TNG_AFTER_CROP,
                                        _("Crop the image to the result"),
                                        _("Fit the canvas to the layer's new "
                                          "size, when the layer covered all "
                                          "of the canvas"),
                                        "keep", LQR_TNG_AFTER_KEEP,
                                        _("Keep the image size (leave an "
                                          "empty strip)"),
                                        _("The canvas keeps its size: where "
                                          "the layer shrank, it is empty"),
                                        "restore", LQR_TNG_AFTER_RESTORE,
                                        _("Carve back to the original size"),
                                        _("Then carve back to the original "
                                          "size: the parts painted to remove "
                                          "stay removed"),
                                        "scale", LQR_TNG_AFTER_SCALE,
                                        _("Scale back to the original size"),
                                        _("Then scale the result back to the "
                                          "original size, by ordinary "
                                          "scaling"),
                                        "scale-width", LQR_TNG_AFTER_SCALE_WIDTH,
                                        _("Scale back the width only"),
                                        _("Then scale the result, keeping its "
                                          "proportions, to the original "
                                          "width"),
                                        "scale-height", LQR_TNG_AFTER_SCALE_HEIGHT,
                                        _("Scale back the height only"),
                                        _("Then scale the result, keeping its "
                                          "proportions, to the original "
                                          "height"),
                                        NULL),
                                      "crop", G_PARAM_READWRITE);
  gimp_procedure_add_choice_argument (procedure, "output", _("O_utput"),
                                      _("Where the result goes"),
                                      gimp_choice_new_with_values (
                                        "layer", LQR_TNG_OUTPUT_LAYER,
                                        _("Change the layer"), NULL,
                                        "new-layer", LQR_TNG_OUTPUT_NEW_LAYER,
                                        _("A new layer above it"),
                                        _("The layer stays as it is"),
                                        "new-image", LQR_TNG_OUTPUT_NEW_IMAGE,
                                        _("A new image"),
                                        _("The image stays as it is"),
                                        NULL),
                                      "layer", G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "rigidity", _("Ri_gidity"),
                                      _("How much the paths of pixels avoid "
                                        "bending: higher keeps straight lines "
                                        "straighter"),
                                      0.0, 20.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "mask-strength",
                                      _("Mask s_trength"),
                                      _("How strongly the painted masks steer "
                                        "the paths of pixels"),
                                      0.0, 10000.0, 1000.0, G_PARAM_READWRITE);
  gimp_procedure_add_choice_argument (procedure, "energy", _("E_nergy"),
                                      _("What counts as important: where the "
                                        "image changes"),
                                      gimp_choice_new_with_values (
                                        "grad-xabs", LQR_EF_GRAD_XABS,
                                        _("Colour, across (fastest)"), NULL,
                                        "grad-sumabs", LQR_EF_GRAD_SUMABS,
                                        _("Colour, across and along"), NULL,
                                        "grad-norm", LQR_EF_GRAD_NORM,
                                        _("Colour, gradient size"), NULL,
                                        "luma-grad-xabs", LQR_EF_LUMA_GRAD_XABS,
                                        _("Brightness, across"), NULL,
                                        "luma-grad-sumabs", LQR_EF_LUMA_GRAD_SUMABS,
                                        _("Brightness, across and along"), NULL,
                                        "luma-grad-norm", LQR_EF_LUMA_GRAD_NORM,
                                        _("Brightness, gradient size"), NULL,
                                        "none", LQR_EF_NULL,
                                        _("Nothing (masks only)"), NULL,
                                        NULL),
                                      "grad-xabs", G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "max-enlarge",
                                      _("_Largest enlarging step"),
                                      _("When enlarging, at most this factor "
                                        "at a time: smaller steps repeat the "
                                        "same paths of pixels less"),
                                      1.05, 2.0, 1.5, G_PARAM_READWRITE);
  gimp_procedure_add_choice_argument (procedure, "order", _("_Order"),
                                      _("Which side is carved first when "
                                        "both change"),
                                      gimp_choice_new_with_values (
                                        "width-first", CARVE_ORDER_WIDTH_FIRST,
                                        _("Width first"), NULL,
                                        "height-first", CARVE_ORDER_HEIGHT_FIRST,
                                        _("Height first"), NULL,
                                        NULL),
                                      "width-first", G_PARAM_READWRITE);
  gimp_procedure_add_boolean_argument (procedure, "carve-masks",
                                       _("C_arve the masks along"),
                                       _("Carve the mask layers with the same "
                                         "paths of pixels, so that they still "
                                         "fit the layer for another run"),
                                       TRUE, G_PARAM_READWRITE);
  gimp_procedure_add_boolean_argument (procedure, "output-seams",
                                       _("_Draw the seams"),
                                       _("Also draw the paths of pixels taken "
                                         "away (or added) on a new layer, one "
                                         "for each pass, from the first "
                                         "colour to the last"),
                                       FALSE, G_PARAM_READWRITE);
  {
    GeglColor *start = gegl_color_new (SEAMS_START);
    GeglColor *end = gegl_color_new (SEAMS_END);

    gimp_procedure_add_color_argument (procedure, "seams-color-start",
                                       _("First seams"),
                                       _("The colour of the first paths of "
                                         "pixels"),
                                       FALSE, start, G_PARAM_READWRITE);
    gimp_procedure_add_color_argument (procedure, "seams-color-end",
                                       _("Last seams"),
                                       _("The colour of the last paths of "
                                         "pixels"),
                                       FALSE, end, G_PARAM_READWRITE);
    g_object_unref (start);
    g_object_unref (end);
  }

  gimp_procedure_add_layer_return_value (procedure, "result-layer",
                                         _("Result layer"),
                                         _("The carved layer: the layer, the "
                                           "new layer or the layer of the new "
                                           "image"),
                                         FALSE, G_PARAM_READWRITE);
  gimp_procedure_add_image_return_value (procedure, "result-image",
                                         _("Result image"),
                                         _("The image of the result layer"),
                                         FALSE, G_PARAM_READWRITE);

  gimp_procedure_add_int_aux_argument (procedure, "brush-size", _("_Brush"),
                                       _("The size of the brush, in pixels of "
                                         "the preview"),
                                       1, 200, 24, G_PARAM_READWRITE);

  return procedure;
}

static GimpValueArray *
fail (GimpProcedure     *procedure,
      GimpPDBStatusType  status,
      const gchar       *message)
{
  GError *error = g_error_new_literal (GIMP_PLUG_IN_ERROR, 0, message);

  return gimp_procedure_new_return_values (procedure, status, error);
}

/* why the plug-in cannot carve this drawable, or NULL */
static const gchar *
unsupported (GimpImage    *image,
             GimpDrawable *drawable)
{
  if (!GIMP_IS_LAYER (drawable) || GIMP_IS_LAYER_MASK (drawable))
    return _("Liquid Rescale TNG works on a layer, not on a channel or a mask.");
  if (gimp_item_is_group (GIMP_ITEM (drawable)))
    return _("Liquid Rescale TNG cannot carve a layer group.");
  if (gimp_image_get_base_type (image) == GIMP_INDEXED ||
      gimp_drawable_is_indexed (drawable))
    return _("Liquid Rescale TNG cannot carve an indexed image; convert "
             "it to RGB first.");
  if (gimp_layer_is_floating_sel (GIMP_LAYER (drawable)))
    return _("Anchor the floating selection first.");
  if (gimp_item_get_lock_content (GIMP_ITEM (drawable)))
    return _("The layer's pixels are locked.");
  return NULL;
}

/* liblqr's progress, into GIMP's */
static LqrRetVal
progress_init (const gchar *message)
{
  gimp_progress_init (message);
  return LQR_OK;
}

static LqrRetVal
progress_update (gdouble fraction)
{
  gimp_progress_update (fraction);
  return LQR_OK;
}

static LqrRetVal
progress_end (const gchar *message)
{
  gimp_progress_update (1.0);
  return LQR_OK;
}

static LqrProgress *
new_progress (void)
{
  LqrProgress *progress = lqr_progress_new ();

  if (!progress)
    return NULL;
  lqr_progress_set_init (progress, progress_init);
  lqr_progress_set_update (progress, progress_update);
  lqr_progress_set_end (progress, progress_end);
  lqr_progress_set_init_width_message (progress, _("Liquid Rescale: width"));
  lqr_progress_set_init_height_message (progress, _("Liquid Rescale: height"));
  return progress;
}

void
lqr_tng_options_from_config (GimpProcedureConfig *config,
                             CarveOptions        *options,
                             gint                 layer_width,
                             gint                 layer_height)
{
  gint width, height;

  g_object_get (config,
                "width", &width,
                "height", &height,
                "rigidity", &options->rigidity,
                "mask-strength", &options->strength,
                "max-enlarge", &options->max_enlarge,
                NULL);
  options->width  = width > 0 ? width : layer_width;
  options->height = height > 0 ? height : layer_height;
  options->energy = gimp_procedure_config_get_choice_id (config, "energy");
  options->order  = gimp_procedure_config_get_choice_id (config, "order");
  options->restore_size = (gimp_procedure_config_get_choice_id (config, "after")
                           == LQR_TNG_AFTER_RESTORE);
}

gboolean
lqr_tng_covers_canvas (GimpImage *image,
                       GimpLayer *layer)
{
  gint x, y;

  gimp_drawable_get_offsets (GIMP_DRAWABLE (layer), &x, &y);
  return x == 0 && y == 0 &&
         gimp_drawable_get_width (GIMP_DRAWABLE (layer)) ==
         gimp_image_get_width (image) &&
         gimp_drawable_get_height (GIMP_DRAWABLE (layer)) ==
         gimp_image_get_height (image);
}

/* a mask layer argument: in this image, and not the layer itself */
gboolean
lqr_tng_usable_mask (GimpImage *image,
                     GimpLayer *layer,
                     GimpLayer *mask)
{
  return mask && mask != layer &&
         gimp_item_get_image (GIMP_ITEM (mask)) == image &&
         !gimp_item_is_group (GIMP_ITEM (mask));
}

void
lqr_tng_final_size (GimpProcedureConfig *config,
                    gint                 layer_width,
                    gint                 layer_height,
                    gint                 width,
                    gint                 height,
                    gint                *final_width,
                    gint                *final_height)
{
  /* as the Liquid Rescale plug-in scales back */
  switch (gimp_procedure_config_get_choice_id (config, "after"))
    {
    case LQR_TNG_AFTER_RESTORE:
    case LQR_TNG_AFTER_SCALE:
      *final_width  = layer_width;
      *final_height = layer_height;
      break;
    case LQR_TNG_AFTER_SCALE_WIDTH:
      *final_width  = layer_width;
      *final_height = MAX (1, (gint) ((gdouble) height * layer_width / width));
      break;
    case LQR_TNG_AFTER_SCALE_HEIGHT:
      *final_width  = MAX (1, (gint) ((gdouble) width * layer_height / height));
      *final_height = layer_height;
      break;
    default:
      *final_width  = width;
      *final_height = height;
      break;
    }
}

static const gchar *mask_properties[MASK_N_KINDS] =
{
  "keep-layer", "remove-layer", "rigidity-layer"
};

/* the masks of config, checked */
static gboolean
get_masks (GimpImage            *image,
           GimpLayer            *layer,
           GimpProcedureConfig  *config,
           GimpLayer            *masks[MASK_N_KINDS],
           GError              **error)
{
  const gchar *names[MASK_N_KINDS] = { N_("keep"), N_("remove"), N_("straight") };
  gint         k, j;

  for (k = 0; k < MASK_N_KINDS; k++)
    g_object_get (config, mask_properties[k], &masks[k], NULL);

  for (k = 0; k < MASK_N_KINDS; k++)
    {
      if (masks[k] && !lqr_tng_usable_mask (image, layer, masks[k]))
        {
          g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                       _("The %s mask must be another layer of the same image."),
                       _(names[k]));
          return FALSE;
        }
      for (j = 0; j < k; j++)
        if (masks[k] && masks[k] == masks[j])
          {
            g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                                 _("The masks must be different layers."));
            return FALSE;
          }
    }
  return TRUE;
}

/* a new image for the result: of layer's size, type, precision and colour
 * profile, with a copy of layer at the top left */
static GimpLayer *
new_image_for (GimpImage *image,
               GimpLayer *layer)
{
  GimpImage        *copy;
  GimpLayer        *target;
  GimpColorProfile *profile;
  gdouble           xres, yres;

  copy = gimp_image_new_with_precision (gimp_drawable_get_width (GIMP_DRAWABLE (layer)),
                                        gimp_drawable_get_height (GIMP_DRAWABLE (layer)),
                                        gimp_image_get_base_type (image),
                                        gimp_image_get_precision (image));
  gimp_image_undo_disable (copy);
  profile = gimp_image_get_color_profile (image);
  if (profile)
    {
      gimp_image_set_color_profile (copy, profile);
      g_object_unref (profile);
    }
  gimp_image_get_resolution (image, &xres, &yres);
  gimp_image_set_resolution (copy, xres, yres);

  target = gimp_layer_new_from_drawable (GIMP_DRAWABLE (layer), copy);
  gimp_image_insert_layer (copy, target, NULL, 0);
  /* the copy is "<name> copy" */
  gimp_item_set_name (GIMP_ITEM (target), gimp_item_get_name (GIMP_ITEM (layer)));
  gimp_layer_set_offsets (target, 0, 0);
  gimp_item_set_visible (GIMP_ITEM (target), TRUE);
  return target;
}

/* a copy of the mask layer mask of kind for target, in target's image
 * above it, which becomes target's stored mask of that kind */
static GimpLayer *
copy_mask (GimpLayer *mask,
           GimpLayer *target,
           MaskKind   kind)
{
  GimpImage *image = gimp_item_get_image (GIMP_ITEM (target));
  GimpLayer *copy = gimp_layer_new_from_drawable (GIMP_DRAWABLE (mask), image);

  gimp_image_insert_layer (image, copy,
                           GIMP_LAYER (gimp_item_get_parent (GIMP_ITEM (target))),
                           gimp_image_get_item_position (image, GIMP_ITEM (target)));
  gimp_item_set_name (GIMP_ITEM (copy), gimp_item_get_name (GIMP_ITEM (mask)));
  layer_io_mark_mask (copy, target, kind);
  return copy;
}

/* the seam maps as layers above target, from the first colour to the
 * last, as the Liquid Rescale plug-in draws them */
static void
draw_seams (GimpLayer           *target,
            GimpProcedureConfig *config,
            const gchar         *name,
            const CarveResult   *result,
            gint                 x,
            gint                 y)
{
  GimpImage *image = gimp_item_get_image (GIMP_ITEM (target));
  gboolean   gray = gimp_image_get_base_type (image) == GIMP_GRAY;
  GeglColor *colours[2] = { NULL, NULL };
  gdouble    start[4], end[4];
  gint       i;

  g_object_get (config,
                "seams-color-start", &colours[0],
                "seams-color-end", &colours[1],
                NULL);
  gegl_color_get_pixel (colours[0], babl_format ("R'G'B'A double"), start);
  gegl_color_get_pixel (colours[1], babl_format ("R'G'B'A double"), end);
  g_object_unref (colours[0]);
  g_object_unref (colours[1]);

  for (i = 0; i < result->n_seams; i++)
    {
      const CarveImage *map = &result->seams[i].map;
      CarveImage        pixels = { map->width, map->height, 4, NULL };
      GimpLayer        *layer;
      gchar            *layer_name;
      gsize             n = (gsize) map->width * map->height, j;

      layer_name = g_strdup_printf (result->seams[i].height ?
                                    _("%s seams (height)") :
                                    _("%s seams (width)"), name);
      /* a gray image has gray seams */
      layer = gimp_layer_new (image, layer_name, map->width, map->height,
                              gray ? GIMP_GRAYA_IMAGE : GIMP_RGBA_IMAGE, 100.0,
                              GIMP_LAYER_MODE_NORMAL);
      g_free (layer_name);
      gimp_image_insert_layer (image, layer,
                               GIMP_LAYER (gimp_item_get_parent (GIMP_ITEM (target))),
                               gimp_image_get_item_position (image,
                                                             GIMP_ITEM (target)));
      gimp_layer_set_offsets (layer, x, y);

      pixels.pixels = g_new0 (gfloat, n * 4);
      for (j = 0; j < n; j++)
        {
          gfloat  v = map->pixels[j];
          gfloat *p = pixels.pixels + j * 4;
          gint    c;

          if (v <= 0.0f)
            continue;
          for (c = 0; c < 3; c++)
            p[c] = v * start[c] + (1.0 - v) * end[c];
          p[3] = 0.5 * (1.0 + v);
        }
      layer_io_write (GIMP_DRAWABLE (layer), babl_format ("R'G'B'A float"),
                      &pixels);
      g_free (pixels.pixels);
    }
}

/* scales layer to width x height, keeping its top left corner */
static void
scale_in_place (GimpLayer *layer,
                gint       width,
                gint       height)
{
  gint x, y;

  if (gimp_drawable_get_width (GIMP_DRAWABLE (layer)) == width &&
      gimp_drawable_get_height (GIMP_DRAWABLE (layer)) == height)
    return;
  gimp_drawable_get_offsets (GIMP_DRAWABLE (layer), &x, &y);
  gimp_layer_scale (layer, width, height, TRUE);
  gimp_layer_set_offsets (layer, x, y);
}

static GimpPDBStatusType
lqr_tng_apply (GimpImage            *image,
               GimpLayer            *layer,
               GimpProcedureConfig  *config,
               GimpRunMode           run_mode,
               GimpLayer           **result_layer,
               GimpImage           **result_image,
               GError              **error)
{
  GimpDrawable  *drawable = GIMP_DRAWABLE (layer);
  GimpLayer     *masks[MASK_N_KINDS] = { NULL, NULL, NULL };
  GimpLayer     *carved_masks[MASK_N_KINDS] = { NULL, NULL, NULL };
  GimpLayerMask *layer_mask = gimp_layer_get_mask (layer);
  GimpLayerMask *target_mask;
  GimpLayer     *target;
  GimpImage     *target_image;
  CarveImage     pixels = { 0 }, layer_mask_pixels = { 0 };
  CarveImage     mask_rgba[MASK_N_KINDS] = { { 0 } };
  CarveImage     mask_values[MASK_N_KINDS] = { { 0 } };
  CarveImage     extras[MASK_N_KINDS + 1];
  gint           extra_of[MASK_N_KINDS + 1];
  CarveInput     input = { NULL, NULL, NULL, FALSE, extras, 0 };
  CarveResult    result;
  CarveOptions   options;
  gboolean       carve_masks, seams, covers;
  LqrTngAfter    after;
  LqrTngOutput   output;
  GimpChannel   *selection = NULL;
  const Babl    *format = layer_io_format (drawable);
  const Babl    *mask_format = babl_format ("Y float");
  gint           width = gimp_drawable_get_width (drawable);
  gint           height = gimp_drawable_get_height (drawable);
  gint           final_width, final_height;
  gint           x, y, i, k;
  gboolean       ok = FALSE;

  g_object_get (config,
                "carve-masks", &carve_masks,
                "output-seams", &seams,
                NULL);
  after  = gimp_procedure_config_get_choice_id (config, "after");
  output = gimp_procedure_config_get_choice_id (config, "output");
  lqr_tng_options_from_config (config, &options, width, height);
  options.seams = seams;

  if (!get_masks (image, layer, config, masks, error))
    {
      for (k = 0; k < MASK_N_KINDS; k++)
        g_clear_object (&masks[k]);
      return GIMP_PDB_CALLING_ERROR;
    }

  memset (&result, 0, sizeof (result));
  gimp_drawable_get_offsets (drawable, &x, &y);
  covers = lqr_tng_covers_canvas (image, layer);

  ok = layer_io_read (drawable, format, &pixels);
  for (k = 0; k < MASK_N_KINDS && ok; k++)
    if (masks[k])
      ok = layer_io_read_over (GIMP_DRAWABLE (masks[k]), drawable,
                               width, height, &mask_rgba[k], &mask_values[k]);
  if (ok && layer_mask)
    ok = layer_io_read (GIMP_DRAWABLE (layer_mask), mask_format,
                        &layer_mask_pixels);
  if (!ok)
    {
      g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                           _("Not enough memory for this layer."));
      goto out;
    }

  input.keep   = masks[MASK_KEEP] ? &mask_values[MASK_KEEP] : NULL;
  input.remove = masks[MASK_REMOVE] ? &mask_values[MASK_REMOVE] : NULL;
  input.rigid  = masks[MASK_RIGID] ? &mask_values[MASK_RIGID] : NULL;
  /* carried along the same seams: the mask layers, and the layer mask */
  for (k = 0; k < MASK_N_KINDS; k++)
    if (masks[k] && carve_masks)
      {
        extra_of[input.n_extras] = k;
        extras[input.n_extras++] = mask_rgba[k];
      }
  if (layer_mask)
    {
      extra_of[input.n_extras] = -1;
      extras[input.n_extras++] = layer_mask_pixels;
    }

  ok = carve_full (&pixels, &input, &options, new_progress (), NULL,
                   &result, error);
  gimp_progress_end ();
  if (!ok)
    goto out;
  lqr_tng_final_size (config, width, height, options.width, options.height,
                      &final_width, &final_height);

  /* the selection would limit writing the result; it is put back after */
  if (output != LQR_TNG_OUTPUT_NEW_IMAGE && !gimp_selection_is_empty (image))
    {
      selection = gimp_selection_save (image);
      gimp_selection_none (image);
    }

  /* where the result goes, with its layer mask */
  switch (output)
    {
    case LQR_TNG_OUTPUT_NEW_LAYER:
      target = gimp_layer_copy (layer);
      gimp_image_insert_layer (image, target,
                               GIMP_LAYER (gimp_item_get_parent (GIMP_ITEM (layer))),
                               gimp_image_get_item_position (image,
                                                             GIMP_ITEM (layer)));
      {
        gchar *name = g_strdup_printf (_("%s (carved)"),
                                       gimp_item_get_name (GIMP_ITEM (layer)));

        gimp_item_set_name (GIMP_ITEM (target), name);
        g_free (name);
      }
      gimp_item_set_visible (GIMP_ITEM (target), TRUE);
      break;
    case LQR_TNG_OUTPUT_NEW_IMAGE:
      target = new_image_for (image, layer);
      x = y = 0;
      break;
    default:
      target = layer;
      break;
    }
  target_image = gimp_item_get_image (GIMP_ITEM (target));
  target_mask = gimp_layer_get_mask (target);

  if (result.image.width != width || result.image.height != height)
    gimp_layer_resize (target, result.image.width, result.image.height, 0, 0);
  layer_io_write (GIMP_DRAWABLE (target), format, &result.image);

  for (i = 0; i < result.n_extras; i++)
    {
      if (extra_of[i] < 0)
        {
          if (target_mask)
            layer_io_write (GIMP_DRAWABLE (target_mask), mask_format,
                            &result.extras[i]);
          continue;
        }
      /* the carved part of the mask layer, which now covers the layer;
       * for a new layer or image, a copy of it becomes the result's mask
       * and the mask stays with the layer, which is unchanged */
      k = extra_of[i];
      carved_masks[k] = output == LQR_TNG_OUTPUT_LAYER ?
                        masks[k] : copy_mask (masks[k], target, k);
      gimp_layer_resize (carved_masks[k], result.image.width,
                         result.image.height, 0, 0);
      gimp_layer_set_offsets (carved_masks[k], x, y);
      layer_io_write (GIMP_DRAWABLE (carved_masks[k]),
                      babl_format ("R'G'B'A float"), &result.extras[i]);
    }

  if (seams)
    draw_seams (target, config, gimp_item_get_name (GIMP_ITEM (layer)),
                &result, x, y);

  /* ordinary scaling back, of the layer and its masks */
  if (final_width != result.image.width || final_height != result.image.height)
    {
      scale_in_place (target, final_width, final_height);
      for (k = 0; k < MASK_N_KINDS; k++)
        if (carved_masks[k])
          scale_in_place (carved_masks[k], final_width, final_height);
    }

  /* the canvas: a new image fits the result, but keeps the layer's old
   * size with "keep"; the image fits the result when the layer covered it,
   * except with "keep" */
  if (output == LQR_TNG_OUTPUT_NEW_IMAGE)
    {
      if (after != LQR_TNG_AFTER_KEEP)
        gimp_image_resize (target_image, final_width, final_height, 0, 0);
    }
  else if (after != LQR_TNG_AFTER_KEEP && covers &&
           (final_width != gimp_image_get_width (image) ||
            final_height != gimp_image_get_height (image)))
    {
      gimp_image_resize (image, final_width, final_height, 0, 0);
    }

  if (selection)
    {
      gimp_image_select_item (image, GIMP_CHANNEL_OP_REPLACE,
                              GIMP_ITEM (selection));
      gimp_image_remove_channel (image, selection);
    }

  if (output == LQR_TNG_OUTPUT_NEW_IMAGE)
    {
      gimp_image_undo_enable (target_image);
      gimp_image_clean_all (target_image);
      if (run_mode != GIMP_RUN_NONINTERACTIVE)
        gimp_display_new (target_image);
    }
  *result_layer = target;
  *result_image = target_image;

out:
  g_free (pixels.pixels);
  g_free (layer_mask_pixels.pixels);
  carve_result_clear (&result);
  for (k = 0; k < MASK_N_KINDS; k++)
    {
      g_free (mask_rgba[k].pixels);
      g_free (mask_values[k].pixels);
      g_clear_object (&masks[k]);
    }
  if (!ok)
    return g_error_matches (*error, CARVE_ERROR, CARVE_ERROR_CANCELLED) ?
           GIMP_PDB_CANCEL : GIMP_PDB_EXECUTION_ERROR;
  return GIMP_PDB_SUCCESS;
}

void
lqr_tng_find_masks (GimpImage           *image,
                    GimpLayer           *layer,
                    GimpProcedureConfig *config)
{
  gint k;

  for (k = 0; k < MASK_N_KINDS; k++)
    {
      GimpLayer *mask = NULL;

      g_object_get (config, mask_properties[k], &mask, NULL);
      if (!lqr_tng_usable_mask (image, layer, mask))
        g_object_set (config, mask_properties[k],
                      layer_io_find_mask (image, layer, k), NULL);
      g_clear_object (&mask);
    }
}

/* the masks painted in the dialog into the mask layers of this plug-in,
 * which become the masks of config */
static void
store_masks (GimpImage           *image,
             GimpLayer           *layer,
             GimpProcedureConfig *config,
             const Masks         *masks)
{
  gint width = gimp_drawable_get_width (GIMP_DRAWABLE (layer));
  gint height = gimp_drawable_get_height (GIMP_DRAWABLE (layer));
  gint k;

  for (k = 0; k < MASK_N_KINDS; k++)
    {
      gfloat *values = NULL;

      if (!masks_empty (masks, k))
        values = masks_resample (masks, k, width, height);
      g_object_set (config, mask_properties[k],
                    layer_io_store_mask (image, layer, k, values), NULL);
      g_free (values);
    }
}

/* GIMP 3.2 hands over the colour arguments without their defaults, as
 * transparent black, to this plug-in (and so its dialog) and to scripts
 * of other plug-ins: a seam colour has no alpha, so that is the default */
static void
default_colours (GimpProcedureConfig *config)
{
  const gchar *properties[2] = { "seams-color-start", "seams-color-end" };
  const gchar *defaults[2] = { SEAMS_START, SEAMS_END };
  gint         i;

  for (i = 0; i < 2; i++)
    {
      GeglColor *colour = NULL;
      gdouble    rgba[4] = { 0.0, 0.0, 0.0, 0.0 };

      g_object_get (config, properties[i], &colour, NULL);
      if (colour)
        gegl_color_get_pixel (colour, babl_format ("R'G'B'A double"), rgba);
      g_clear_object (&colour);
      if (rgba[3] <= 0.0)
        {
          colour = gegl_color_new (defaults[i]);
          g_object_set (config, properties[i], colour, NULL);
          g_object_unref (colour);
        }
    }
}

static GimpValueArray *
lqr_tng_run (GimpProcedure        *procedure,
             GimpRunMode           run_mode,
             GimpImage            *image,
             GimpDrawable        **drawables,
             GimpProcedureConfig  *config,
             gpointer              run_data)
{
  GimpDrawable      *drawable;
  const gchar       *why;
  GimpPDBStatusType  status;
  GError            *error = NULL;
  Masks             *masks = NULL;
  gboolean           changed = FALSE;
  GimpLayer         *result_layer = NULL;
  GimpImage         *result_image = NULL;
  GimpValueArray    *values;

  gegl_init (NULL, NULL);

  if (gimp_core_object_array_get_length ((GObject **) drawables) != 1)
    return fail (procedure, GIMP_PDB_CALLING_ERROR,
                 _("Liquid Rescale TNG works on one layer at a time."));
  drawable = drawables[0];
  why = unsupported (image, drawable);
  if (why)
    return fail (procedure, GIMP_PDB_CALLING_ERROR, why);

  default_colours (config);
  if (run_mode == GIMP_RUN_INTERACTIVE)
    {
      gimp_ui_init (PLUG_IN_BINARY);
      if (!lqr_tng_dialog (procedure, config, image, GIMP_LAYER (drawable),
                           &masks, &changed))
        return gimp_procedure_new_return_values (procedure, GIMP_PDB_CANCEL,
                                                 NULL);
    }

  /* Repeat: the masks stored for this layer, unless those of the last
   * run fit it */
  if (run_mode == GIMP_RUN_WITH_LAST_VALS)
    lqr_tng_find_masks (image, GIMP_LAYER (drawable), config);

  /* the painted masks become layers, and the carving uses them: one step
   * to undo */
  gimp_image_undo_group_start (image);
  if (changed)
    store_masks (image, GIMP_LAYER (drawable), config, masks);
  masks_free (masks);
  status = lqr_tng_apply (image, GIMP_LAYER (drawable), config, run_mode,
                          &result_layer, &result_image, &error);
  gimp_image_undo_group_end (image);
  if (status != GIMP_PDB_SUCCESS)
    return gimp_procedure_new_return_values (procedure, status, error);

  if (run_mode != GIMP_RUN_NONINTERACTIVE)
    gimp_displays_flush ();
  values = gimp_procedure_new_return_values (procedure, GIMP_PDB_SUCCESS, NULL);
  GIMP_VALUES_SET_LAYER (values, 1, result_layer);
  GIMP_VALUES_SET_IMAGE (values, 2, result_image);
  return values;
}
