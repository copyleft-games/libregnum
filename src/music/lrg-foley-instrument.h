/* lrg-foley-instrument.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An instrument that plays a synthesized sound effect (#LrgFoleyPatch)
 * on every note: a few takes of the patch with different seeds alternate
 * round robin, pitched by key (or not), so a song can use zaps, impacts
 * and risers made in the Foley designer as notes on a track.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../audio/lrg-foley-patch.h"
#include "lrg-instrument.h"

G_BEGIN_DECLS

#define LRG_TYPE_FOLEY_INSTRUMENT (lrg_foley_instrument_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFoleyInstrument, lrg_foley_instrument, LRG, FOLEY_INSTRUMENT, LrgInstrument)

/**
 * lrg_foley_instrument_new:
 * @patch: (nullable): the sound to play (copied)
 *
 * The instrument renders #LrgFoleyInstrument:takes takes of the patch
 * (#LrgFoleyPatch:variation + 0, + 1, ...) at once; key #LrgFoleyInstrument:root-key
 * plays them unpitched and #LrgFoleyInstrument:pitch-track sets how far
 * other keys move the pitch.
 *
 * Returns: (transfer full): a new #LrgFoleyInstrument
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyInstrument * lrg_foley_instrument_new (LrgFoleyPatch *patch);

/**
 * lrg_foley_instrument_set_patch:
 * @self: an #LrgFoleyInstrument
 * @patch: (nullable): the sound to play (copied); %NULL silences
 * @error: (nullable): return location for an error
 *
 * Renders the takes of @patch.
 *
 * Returns: %TRUE when the patch rendered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foley_instrument_set_patch (LrgFoleyInstrument  *self,
                                         LrgFoleyPatch       *patch,
                                         GError             **error);

/**
 * lrg_foley_instrument_get_patch:
 * @self: an #LrgFoleyInstrument
 *
 * Returns: (transfer none) (nullable): the patch it plays
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyPatch * lrg_foley_instrument_get_patch (LrgFoleyInstrument *self);

G_END_DECLS
