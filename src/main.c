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
  gimp_procedure_add_choice_argument (procedure, "after", _("After carving"),
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
                                        NULL),
                                      "crop", G_PARAM_READWRITE);
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

static GimpPDBStatusType
lqr_tng_apply (GimpImage            *image,
               GimpLayer            *layer,
               GimpProcedureConfig  *config,
               GError              **error)
{
  GimpDrawable  *drawable = GIMP_DRAWABLE (layer);
  GimpLayer     *masks[2] = { NULL, NULL };
  GimpLayerMask *layer_mask = gimp_layer_get_mask (layer);
  CarveImage     pixels = { 0 }, result = { 0 };
  CarveImage     mask_rgba[2] = { { 0 } }, mask_values[2] = { { 0 } };
  CarveImage     extras[3], extra_results[3];
  gint           extra_of[3];
  gint           n_extras = 0, i, k;
  CarveOptions   options;
  gboolean       carve_masks, crop;
  GimpChannel   *selection = NULL;
  const Babl    *format = layer_io_format (drawable);
  const Babl    *mask_format = NULL;
  gint           width = gimp_drawable_get_width (drawable);
  gint           height = gimp_drawable_get_height (drawable);
  gint           x, y;
  gboolean       ok;

  g_object_get (config,
                "keep-layer", &masks[MASK_KEEP],
                "remove-layer", &masks[MASK_REMOVE],
                "carve-masks", &carve_masks,
                NULL);
  lqr_tng_options_from_config (config, &options, width, height);

  for (k = 0; k < 2; k++)
    if (masks[k] && !lqr_tng_usable_mask (image, layer, masks[k]))
      {
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     _("The %s mask must be another layer of the same image."),
                     k == MASK_KEEP ? _("keep") : _("remove"));
        g_clear_object (&masks[0]);
        g_clear_object (&masks[1]);
        return GIMP_PDB_CALLING_ERROR;
      }
  if (masks[0] && masks[0] == masks[1])
    {
      g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                           _("The keep and remove masks must be different layers."));
      g_clear_object (&masks[0]);
      g_clear_object (&masks[1]);
      return GIMP_PDB_CALLING_ERROR;
    }

  gimp_drawable_get_offsets (drawable, &x, &y);
  crop = gimp_procedure_config_get_choice_id (config, "after") ==
         LQR_TNG_AFTER_CROP &&
         lqr_tng_covers_canvas (image, layer);

  ok = layer_io_read (drawable, format, &pixels);
  for (k = 0; k < 2 && ok; k++)
    if (masks[k])
      ok = layer_io_read_over (GIMP_DRAWABLE (masks[k]), drawable,
                               width, height, &mask_rgba[k], &mask_values[k]);
  if (!ok)
    {
      g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                           _("Not enough memory for this layer."));
      goto out;
    }

  /* carried along the same seams: the mask layers, and the layer mask */
  for (k = 0; k < 2; k++)
    if (masks[k] && carve_masks)
      {
        extra_of[n_extras] = k;
        extras[n_extras++] = mask_rgba[k];
      }
  if (layer_mask)
    {
      mask_format = babl_format ("Y float");
      ok = layer_io_read (GIMP_DRAWABLE (layer_mask), mask_format,
                          &extras[n_extras]);
      if (!ok)
        {
          g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0,
                               _("Not enough memory for this layer."));
          goto out;
        }
      extra_of[n_extras++] = -1;
    }

  ok = carve_full (&pixels,
                   masks[MASK_KEEP] ? &mask_values[MASK_KEEP] : NULL,
                   masks[MASK_REMOVE] ? &mask_values[MASK_REMOVE] : NULL,
                   &options, new_progress (), NULL, &result, NULL, NULL,
                   extras, n_extras, extra_results, error);
  gimp_progress_end ();
  if (!ok)
    goto out;

  /* the selection would limit writing the result; it is put back after */
  if (!gimp_selection_is_empty (image))
    {
      selection = gimp_selection_save (image);
      gimp_selection_none (image);
    }

  if (result.width != width || result.height != height)
    gimp_layer_resize (layer, result.width, result.height, 0, 0);
  layer_io_write (drawable, format, &result);

  for (i = 0; i < n_extras; i++)
    {
      if (extra_of[i] < 0)
        {
          layer_io_write (GIMP_DRAWABLE (layer_mask), mask_format,
                          &extra_results[i]);
          continue;
        }
      /* the carved part of the mask layer, which now covers the layer */
      k = extra_of[i];
      gimp_layer_resize (masks[k], result.width, result.height, 0, 0);
      gimp_layer_set_offsets (masks[k], x, y);
      layer_io_write (GIMP_DRAWABLE (masks[k]), babl_format ("R'G'B'A float"),
                      &extra_results[i]);
    }

  if (crop)
    gimp_image_resize (image, result.width, result.height, 0, 0);

  if (selection)
    {
      gimp_image_select_item (image, GIMP_CHANNEL_OP_REPLACE,
                              GIMP_ITEM (selection));
      gimp_image_remove_channel (image, selection);
    }

out:
  g_free (pixels.pixels);
  g_free (result.pixels);
  for (k = 0; k < 2; k++)
    {
      g_free (mask_rgba[k].pixels);
      g_free (mask_values[k].pixels);
      g_clear_object (&masks[k]);
    }
  for (i = 0; i < n_extras; i++)
    {
      if (extra_of[i] < 0)
        g_free (extras[i].pixels);
      if (ok)
        g_free (extra_results[i].pixels);
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
  const gchar *properties[2] = { "keep-layer", "remove-layer" };
  gint         k;

  for (k = 0; k < 2; k++)
    {
      GimpLayer *mask = NULL;

      g_object_get (config, properties[k], &mask, NULL);
      if (!lqr_tng_usable_mask (image, layer, mask))
        g_object_set (config, properties[k],
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
  gint       width = gimp_drawable_get_width (GIMP_DRAWABLE (layer));
  gint       height = gimp_drawable_get_height (GIMP_DRAWABLE (layer));
  GimpLayer *stored[2];
  gint       k;

  for (k = 0; k < 2; k++)
    {
      gfloat *values = NULL;

      if (!masks_empty (masks, k))
        values = masks_resample (masks, k, width, height);
      stored[k] = layer_io_store_mask (image, layer, k, values);
      g_free (values);
    }
  g_object_set (config,
                "keep-layer", stored[MASK_KEEP],
                "remove-layer", stored[MASK_REMOVE],
                NULL);
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

  gegl_init (NULL, NULL);

  if (gimp_core_object_array_get_length ((GObject **) drawables) != 1)
    return fail (procedure, GIMP_PDB_CALLING_ERROR,
                 _("Liquid Rescale TNG works on one layer at a time."));
  drawable = drawables[0];
  why = unsupported (image, drawable);
  if (why)
    return fail (procedure, GIMP_PDB_CALLING_ERROR, why);

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
  status = lqr_tng_apply (image, GIMP_LAYER (drawable), config, &error);
  gimp_image_undo_group_end (image);
  if (status != GIMP_PDB_SUCCESS)
    return gimp_procedure_new_return_values (procedure, status, error);

  if (run_mode != GIMP_RUN_NONINTERACTIVE)
    gimp_displays_flush ();
  return gimp_procedure_new_return_values (procedure, GIMP_PDB_SUCCESS, NULL);
}
