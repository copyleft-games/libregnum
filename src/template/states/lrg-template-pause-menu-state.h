/* lrg-template-pause-menu-state.h - Pause menu state for game templates
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_PAUSE_MENU_STATE_H
#define LRG_TEMPLATE_PAUSE_MENU_STATE_H

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../gamestate/lrg-game-state.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_PAUSE_MENU_STATE (lrg_template_pause_menu_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTemplatePauseMenuState, lrg_template_pause_menu_state,
                          LRG, TEMPLATE_PAUSE_MENU_STATE, LrgGameState)

/**
 * LrgTemplatePauseMenuStateClass:
 * @parent_class: the parent class
 * @on_resume: Called when Resume is selected
 * @on_settings: Called when Settings is selected
 * @on_main_menu: Called when Main Menu is selected
 * @on_exit: Called when Exit is selected
 *
 * The class structure for #LrgTemplatePauseMenuState.
 */
struct _LrgTemplatePauseMenuStateClass
{
    LrgGameStateClass parent_class;

    /*< public >*/

    /**
     * LrgTemplatePauseMenuStateClass::on_resume:
     * @self: an #LrgTemplatePauseMenuState
     *
     * Called when Resume button is activated.
     * Default implementation emits the ::resume signal.
     */
    void (*on_resume)    (LrgTemplatePauseMenuState *self);

    /**
     * LrgTemplatePauseMenuStateClass::on_settings:
     * @self: an #LrgTemplatePauseMenuState
     *
     * Called when Settings button is activated.
     * Default implementation emits the ::settings signal.
     */
    void (*on_settings)  (LrgTemplatePauseMenuState *self);

    /**
     * LrgTemplatePauseMenuStateClass::on_main_menu:
     * @self: an #LrgTemplatePauseMenuState
     *
     * Called when Main Menu button is activated.
     * Default implementation emits the ::main-menu signal.
     */
    void (*on_main_menu) (LrgTemplatePauseMenuState *self);

    /**
     * LrgTemplatePauseMenuStateClass::on_exit:
     * @self: an #LrgTemplatePauseMenuState
     *
     * Called when Exit button is activated.
     * Default implementation emits the ::exit-game signal.
     */
    void (*on_exit)      (LrgTemplatePauseMenuState *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_template_pause_menu_state_new:
 *
 * Creates a new pause menu state with default settings.
 *
 * Returns: (transfer full): A new #LrgTemplatePauseMenuState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplatePauseMenuState *
lrg_template_pause_menu_state_new (void);

/* ==========================================================================
 * Audio Ducking
 * ========================================================================== */

/**
 * lrg_template_pause_menu_state_get_duck_audio:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets whether audio is ducked when the pause menu is shown.
 *
 * Returns: %TRUE if audio is ducked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_pause_menu_state_get_duck_audio (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_duck_audio:
 * @self: an #LrgTemplatePauseMenuState
 * @duck: whether to duck audio
 *
 * Sets whether audio is ducked when the pause menu is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_duck_audio (LrgTemplatePauseMenuState *self,
                                              gboolean                   duck);

/**
 * lrg_template_pause_menu_state_get_duck_factor:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets the audio duck factor (volume multiplier).
 *
 * Returns: The duck factor (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_template_pause_menu_state_get_duck_factor (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_duck_factor:
 * @self: an #LrgTemplatePauseMenuState
 * @factor: the volume multiplier (0.0 to 1.0)
 *
 * Sets the audio duck factor. 0.2 means audio plays at 20% volume.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_duck_factor (LrgTemplatePauseMenuState *self,
                                               gfloat                     factor);

/* ==========================================================================
 * Overlay Appearance
 * ========================================================================== */

/**
 * lrg_template_pause_menu_state_get_overlay_color:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets the overlay color drawn behind the pause menu.
 *
 * Returns: (transfer none): The overlay color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_template_pause_menu_state_get_overlay_color (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_overlay_color:
 * @self: an #LrgTemplatePauseMenuState
 * @color: the overlay color
 *
 * Sets the overlay color. The alpha channel controls transparency.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_overlay_color (LrgTemplatePauseMenuState *self,
                                                 const GrlColor            *color);

/* ==========================================================================
 * Confirmation Dialogs
 * ========================================================================== */

/**
 * lrg_template_pause_menu_state_get_confirm_main_menu:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets whether returning to main menu requires confirmation.
 *
 * Returns: %TRUE if confirmation is required
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_pause_menu_state_get_confirm_main_menu (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_confirm_main_menu:
 * @self: an #LrgTemplatePauseMenuState
 * @confirm: whether to confirm
 *
 * Sets whether returning to main menu requires confirmation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_confirm_main_menu (LrgTemplatePauseMenuState *self,
                                                     gboolean                   confirm);

/**
 * lrg_template_pause_menu_state_get_confirm_exit:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets whether exiting requires confirmation.
 *
 * Returns: %TRUE if confirmation is required
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_pause_menu_state_get_confirm_exit (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_confirm_exit:
 * @self: an #LrgTemplatePauseMenuState
 * @confirm: whether to confirm
 *
 * Sets whether exiting requires confirmation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_confirm_exit (LrgTemplatePauseMenuState *self,
                                                gboolean                   confirm);

/* ==========================================================================
 * Button Visibility
 * ========================================================================== */

/**
 * lrg_template_pause_menu_state_get_show_settings:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets whether the Settings button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_pause_menu_state_get_show_settings (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_show_settings:
 * @self: an #LrgTemplatePauseMenuState
 * @show: whether to show Settings button
 *
 * Sets whether the Settings button is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_show_settings (LrgTemplatePauseMenuState *self,
                                                 gboolean                   show);

/**
 * lrg_template_pause_menu_state_get_show_main_menu:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets whether the Main Menu button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_pause_menu_state_get_show_main_menu (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_show_main_menu:
 * @self: an #LrgTemplatePauseMenuState
 * @show: whether to show Main Menu button
 *
 * Sets whether the Main Menu button is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_show_main_menu (LrgTemplatePauseMenuState *self,
                                                  gboolean                   show);

/**
 * lrg_template_pause_menu_state_get_show_exit:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets whether the Exit button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_pause_menu_state_get_show_exit (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_show_exit:
 * @self: an #LrgTemplatePauseMenuState
 * @show: whether to show Exit button
 *
 * Sets whether the Exit button is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_show_exit (LrgTemplatePauseMenuState *self,
                                             gboolean                   show);

/* ==========================================================================
 * Navigation
 * ========================================================================== */

/**
 * lrg_template_pause_menu_state_get_selected_index:
 * @self: an #LrgTemplatePauseMenuState
 *
 * Gets the currently selected menu item index.
 *
 * Returns: The selected index
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_template_pause_menu_state_get_selected_index (LrgTemplatePauseMenuState *self);

/**
 * lrg_template_pause_menu_state_set_selected_index:
 * @self: an #LrgTemplatePauseMenuState
 * @index: the menu item index to select
 *
 * Sets the selected menu item index.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_pause_menu_state_set_selected_index (LrgTemplatePauseMenuState *self,
                                                  gint                       index);

G_END_DECLS

#endif /* LRG_TEMPLATE_PAUSE_MENU_STATE_H */
