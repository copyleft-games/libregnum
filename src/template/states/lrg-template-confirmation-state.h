/* lrg-template-confirmation-state.h - Generic confirmation dialog state
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_CONFIRMATION_STATE_H
#define LRG_TEMPLATE_CONFIRMATION_STATE_H

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../gamestate/lrg-game-state.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_CONFIRMATION_STATE (lrg_template_confirmation_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTemplateConfirmationState, lrg_template_confirmation_state,
                          LRG, TEMPLATE_CONFIRMATION_STATE, LrgGameState)

/**
 * LrgTemplateConfirmationStateClass:
 * @parent_class: the parent class
 * @on_confirm: Called when the confirm action is selected
 * @on_cancel: Called when the cancel action is selected
 *
 * The class structure for #LrgTemplateConfirmationState.
 */
struct _LrgTemplateConfirmationStateClass
{
    LrgGameStateClass parent_class;

    /*< public >*/

    /**
     * LrgTemplateConfirmationStateClass::on_confirm:
     * @self: an #LrgTemplateConfirmationState
     *
     * Called when the confirm button is activated.
     * Default implementation emits the ::confirmed signal.
     */
    void (*on_confirm) (LrgTemplateConfirmationState *self);

    /**
     * LrgTemplateConfirmationStateClass::on_cancel:
     * @self: an #LrgTemplateConfirmationState
     *
     * Called when the cancel button is activated.
     * Default implementation emits the ::cancelled signal.
     */
    void (*on_cancel)  (LrgTemplateConfirmationState *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_new:
 *
 * Creates a new confirmation state.
 *
 * Returns: (transfer full): A new #LrgTemplateConfirmationState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateConfirmationState *
lrg_template_confirmation_state_new (void);

/**
 * lrg_template_confirmation_state_new_with_message:
 * @title: the title text
 * @message: the message text
 *
 * Creates a new confirmation state with the given title and message.
 *
 * Returns: (transfer full): A new #LrgTemplateConfirmationState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateConfirmationState *
lrg_template_confirmation_state_new_with_message (const gchar *title,
                                                   const gchar *message);

/* ==========================================================================
 * Text Content
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_title:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the title text.
 *
 * Returns: (transfer none): The title
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_confirmation_state_get_title (LrgTemplateConfirmationState *self);

/**
 * lrg_template_confirmation_state_set_title:
 * @self: an #LrgTemplateConfirmationState
 * @title: the title text
 *
 * Sets the title text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_confirmation_state_set_title (LrgTemplateConfirmationState *self,
                                            const gchar                  *title);

/**
 * lrg_template_confirmation_state_get_message:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the message text.
 *
 * Returns: (transfer none): The message
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_confirmation_state_get_message (LrgTemplateConfirmationState *self);

/**
 * lrg_template_confirmation_state_set_message:
 * @self: an #LrgTemplateConfirmationState
 * @message: the message text
 *
 * Sets the message text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_confirmation_state_set_message (LrgTemplateConfirmationState *self,
                                              const gchar                  *message);

/* ==========================================================================
 * Button Labels
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_confirm_label:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the confirm button label.
 *
 * Returns: (transfer none): The label
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_confirmation_state_get_confirm_label (LrgTemplateConfirmationState *self);

/**
 * lrg_template_confirmation_state_set_confirm_label:
 * @self: an #LrgTemplateConfirmationState
 * @label: the button label
 *
 * Sets the confirm button label.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_confirmation_state_set_confirm_label (LrgTemplateConfirmationState *self,
                                                    const gchar                  *label);

/**
 * lrg_template_confirmation_state_get_cancel_label:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the cancel button label.
 *
 * Returns: (transfer none): The label
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_confirmation_state_get_cancel_label (LrgTemplateConfirmationState *self);

/**
 * lrg_template_confirmation_state_set_cancel_label:
 * @self: an #LrgTemplateConfirmationState
 * @label: the button label
 *
 * Sets the cancel button label.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_confirmation_state_set_cancel_label (LrgTemplateConfirmationState *self,
                                                   const gchar                  *label);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_overlay_color:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the overlay color drawn behind the dialog.
 *
 * Returns: (transfer none): The overlay color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_template_confirmation_state_get_overlay_color (LrgTemplateConfirmationState *self);

/**
 * lrg_template_confirmation_state_set_overlay_color:
 * @self: an #LrgTemplateConfirmationState
 * @color: the overlay color
 *
 * Sets the overlay color. The alpha channel controls transparency.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_confirmation_state_set_overlay_color (LrgTemplateConfirmationState *self,
                                                    const GrlColor               *color);

/**
 * lrg_template_confirmation_state_get_dialog_color:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the dialog background color.
 *
 * Returns: (transfer none): The dialog color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_template_confirmation_state_get_dialog_color (LrgTemplateConfirmationState *self);

/**
 * lrg_template_confirmation_state_set_dialog_color:
 * @self: an #LrgTemplateConfirmationState
 * @color: the dialog background color
 *
 * Sets the dialog background color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_confirmation_state_set_dialog_color (LrgTemplateConfirmationState *self,
                                                   const GrlColor               *color);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_default_selection:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets which button is selected by default (0 = confirm, 1 = cancel).
 *
 * Returns: The default selection index
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_template_confirmation_state_get_default_selection (LrgTemplateConfirmationState *self);

/**
 * lrg_template_confirmation_state_set_default_selection:
 * @self: an #LrgTemplateConfirmationState
 * @selection: the default selection (0 = confirm, 1 = cancel)
 *
 * Sets which button is selected by default. Setting to 1 (cancel)
 * is recommended for destructive actions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_confirmation_state_set_default_selection (LrgTemplateConfirmationState *self,
                                                        gint                          selection);

G_END_DECLS

#endif /* LRG_TEMPLATE_CONFIRMATION_STATE_H */
