/* lrg-template-error-state.h - Error recovery state for game templates
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_ERROR_STATE_H
#define LRG_TEMPLATE_ERROR_STATE_H

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../gamestate/lrg-game-state.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_ERROR_STATE (lrg_template_error_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTemplateErrorState, lrg_template_error_state,
                          LRG, TEMPLATE_ERROR_STATE, LrgGameState)

/**
 * LrgTemplateErrorStateClass:
 * @parent_class: the parent class
 * @on_retry: Called when Retry is selected
 * @on_main_menu: Called when Main Menu is selected
 * @on_exit: Called when Exit is selected
 *
 * The class structure for #LrgTemplateErrorState.
 */
struct _LrgTemplateErrorStateClass
{
    LrgGameStateClass parent_class;

    /*< public >*/

    /**
     * LrgTemplateErrorStateClass::on_retry:
     * @self: an #LrgTemplateErrorState
     *
     * Called when Retry button is activated.
     * Default implementation emits the ::retry signal.
     */
    void (*on_retry)     (LrgTemplateErrorState *self);

    /**
     * LrgTemplateErrorStateClass::on_main_menu:
     * @self: an #LrgTemplateErrorState
     *
     * Called when Main Menu button is activated.
     * Default implementation emits the ::main-menu signal.
     */
    void (*on_main_menu) (LrgTemplateErrorState *self);

    /**
     * LrgTemplateErrorStateClass::on_exit:
     * @self: an #LrgTemplateErrorState
     *
     * Called when Exit button is activated.
     * Default implementation emits the ::exit-game signal.
     */
    void (*on_exit)      (LrgTemplateErrorState *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_template_error_state_new:
 *
 * Creates a new error state.
 *
 * Returns: (transfer full): A new #LrgTemplateErrorState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateErrorState *
lrg_template_error_state_new (void);

/**
 * lrg_template_error_state_new_with_error:
 * @error: the error to display
 *
 * Creates a new error state displaying the given error.
 *
 * Returns: (transfer full): A new #LrgTemplateErrorState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateErrorState *
lrg_template_error_state_new_with_error (GError *error);

/* ==========================================================================
 * Error Information
 * ========================================================================== */

/**
 * lrg_template_error_state_get_error_message:
 * @self: an #LrgTemplateErrorState
 *
 * Gets the error message.
 *
 * Returns: (transfer none) (nullable): The error message
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_error_state_get_error_message (LrgTemplateErrorState *self);

/**
 * lrg_template_error_state_set_error_message:
 * @self: an #LrgTemplateErrorState
 * @message: (nullable): the error message
 *
 * Sets the error message to display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_error_state_set_error_message (LrgTemplateErrorState *self,
                                            const gchar           *message);

/**
 * lrg_template_error_state_set_error:
 * @self: an #LrgTemplateErrorState
 * @error: (nullable): the GError to display
 *
 * Sets the error to display from a GError.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_error_state_set_error (LrgTemplateErrorState *self,
                                    GError                *error);

/**
 * lrg_template_error_state_get_title:
 * @self: an #LrgTemplateErrorState
 *
 * Gets the title text.
 *
 * Returns: (transfer none): The title
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_error_state_get_title (LrgTemplateErrorState *self);

/**
 * lrg_template_error_state_set_title:
 * @self: an #LrgTemplateErrorState
 * @title: the title text
 *
 * Sets the title text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_error_state_set_title (LrgTemplateErrorState *self,
                                    const gchar           *title);

/* ==========================================================================
 * Button Visibility
 * ========================================================================== */

/**
 * lrg_template_error_state_get_allow_retry:
 * @self: an #LrgTemplateErrorState
 *
 * Gets whether the Retry button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_error_state_get_allow_retry (LrgTemplateErrorState *self);

/**
 * lrg_template_error_state_set_allow_retry:
 * @self: an #LrgTemplateErrorState
 * @allow: whether to show Retry button
 *
 * Sets whether the Retry button is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_error_state_set_allow_retry (LrgTemplateErrorState *self,
                                          gboolean               allow);

/**
 * lrg_template_error_state_get_show_main_menu:
 * @self: an #LrgTemplateErrorState
 *
 * Gets whether the Main Menu button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_error_state_get_show_main_menu (LrgTemplateErrorState *self);

/**
 * lrg_template_error_state_set_show_main_menu:
 * @self: an #LrgTemplateErrorState
 * @show: whether to show Main Menu button
 *
 * Sets whether the Main Menu button is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_error_state_set_show_main_menu (LrgTemplateErrorState *self,
                                             gboolean               show);

/**
 * lrg_template_error_state_get_show_exit:
 * @self: an #LrgTemplateErrorState
 *
 * Gets whether the Exit button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_error_state_get_show_exit (LrgTemplateErrorState *self);

/**
 * lrg_template_error_state_set_show_exit:
 * @self: an #LrgTemplateErrorState
 * @show: whether to show Exit button
 *
 * Sets whether the Exit button is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_error_state_set_show_exit (LrgTemplateErrorState *self,
                                        gboolean               show);

G_END_DECLS

#endif /* LRG_TEMPLATE_ERROR_STATE_H */
