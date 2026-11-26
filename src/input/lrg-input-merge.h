/* lrg-input-merge.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One input made of several: a gamepad and the touch controls' virtual
 * pad, a real pad and a VR controller's, read as one device.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-input.h"

G_BEGIN_DECLS

/**
 * SECTION:lrg-input-merge
 * @title: LrgInputMerge
 * @short_description: Several inputs read as one
 *
 * Code that reads one #LrgInput (a game's controller layer, say) can be
 * handed an #LrgInputMerge to read several at once, for example the
 * system's gamepads (#LrgInputGamepad) and the touch controls' virtual
 * gamepad (#LrgTouchControls:sink), so a phone game works the same with
 * a controller in hand or the thumbs on the glass.
 *
 * Every query asks each enabled part, in the order they were added:
 *
 * - keys, mouse buttons and gamepad buttons: pressed, down or released
 *   when any part says so;
 * - a gamepad is available when any part has it; its name and ids are
 *   the first part's that has it;
 * - a gamepad axis: the value with the largest magnitude (a stick at
 *   rest on one part never hides a stick pushed on another; a tie keeps
 *   the earlier part's);
 * - the mouse position: the first part with a mouse position other than
 *   (0, 0), else (0, 0); the mouse delta: the sum.
 *
 * lrg_input_poll() on the merge polls every enabled part; a part polled
 * elsewhere (by an #LrgInputManager too) is then polled twice per frame,
 * which input sources with frame edges (pressed/released) do not
 * tolerate: give a part either to the merge or to a manager.
 *
 * Since: 0.13.0
 */

#define LRG_TYPE_INPUT_MERGE (lrg_input_merge_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputMerge, lrg_input_merge, LRG, INPUT_MERGE, LrgInput)

/**
 * lrg_input_merge_new:
 *
 * Creates an empty merge (it reports nothing until a part is added).
 *
 * Returns: (transfer full): a new #LrgInputMerge
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputMerge * lrg_input_merge_new        (void);

/**
 * lrg_input_merge_add:
 * @self: an #LrgInputMerge
 * @part: (transfer none): an input to read
 *
 * Adds @part after the existing ones (a reference is taken).  Adding a
 * part twice, or the merge to itself, does nothing.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_input_merge_add        (LrgInputMerge *self,
                                            LrgInput      *part);

/**
 * lrg_input_merge_remove:
 * @self: an #LrgInputMerge
 * @part: an input added before
 *
 * Removes @part.
 *
 * Returns: %TRUE when @part was a part
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_input_merge_remove     (LrgInputMerge *self,
                                            LrgInput      *part);

/**
 * lrg_input_merge_get_n_parts:
 * @self: an #LrgInputMerge
 *
 * Returns: how many inputs the merge reads
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_input_merge_get_n_parts (LrgInputMerge *self);

/**
 * lrg_input_merge_get_part:
 * @self: an #LrgInputMerge
 * @index: 0 .. lrg_input_merge_get_n_parts() - 1
 *
 * Returns: (transfer none) (nullable): the @index-th part, or %NULL
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgInput *      lrg_input_merge_get_part   (LrgInputMerge *self,
                                            guint          index);

G_END_DECLS
