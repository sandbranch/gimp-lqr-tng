/* Liquid Rescale TNG: the dialog
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

#ifndef __DIALOG_H__
#define __DIALOG_H__

#include <libgimp/gimp.h>

#include "masks.h"

G_BEGIN_DECLS

/* Shows the dialog for layer; TRUE if the user chose Rescale. Then config
 * holds the settings, *masks the painted masks (at the working size; free
 * with masks_free) and *changed whether they were painted on. */
gboolean lqr_tng_dialog (GimpProcedure       *procedure,
                         GimpProcedureConfig *config,
                         GimpImage           *image,
                         GimpLayer           *layer,
                         Masks              **masks,
                         gboolean            *changed);

G_END_DECLS

#endif /* __DIALOG_H__ */
