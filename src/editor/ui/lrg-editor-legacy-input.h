/* lrg-editor-legacy-input.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Translation of the 0.8.0 immediate-mode input frame (#LrgGuiInput) into
 * an #LrgCanvasInput, for code that still drives the editor through the
 * deprecated lrg_editor_app_frame().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../gui/lrg-gui-context.h"
#include "../../ui/lrg-canvas-input.h"

G_BEGIN_DECLS

/**
 * lrg_editor_gui_key_to_grl:
 * @key: an #LrgGuiKey
 *
 * Maps a key of the 0.8.0 immediate-mode GUI to the graylib key the
 * retained UI uses.
 *
 * Returns: the #GrlKey, or %GRL_KEY_NULL for %LRG_GUI_KEY_NONE and
 *   unknown values
 */
LRG_AVAILABLE_IN_ALL
GrlKey lrg_editor_gui_key_to_grl (LrgGuiKey key);

/**
 * lrg_editor_gui_input_to_canvas:
 * @input: this frame's immediate-mode input
 * @previous: (nullable): the previous frame's input, or %NULL for the
 *   first frame (nothing was held)
 * @out: (out caller-allocates): the canvas input frame to fill
 *
 * Builds one #LrgCanvasInput from an #LrgGuiInput. The immediate-mode
 * frame only carries button and key states, so presses and releases are
 * found by comparing with @previous: a button down now and not before
 * was pressed, one down before and not now was released, and a key held
 * in @previous->key_down that is no longer held becomes a key release.
 * Key presses (with their repeat flag), typed characters, the pointer,
 * the wheel, the modifiers and the time step are copied.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gui_input_to_canvas (const LrgGuiInput *input,
                                     const LrgGuiInput *previous,
                                     LrgCanvasInput    *out);

G_END_DECLS
