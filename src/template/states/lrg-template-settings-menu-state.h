/* lrg-template-settings-menu-state.h - Settings menu state for game templates
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_SETTINGS_MENU_STATE_H
#define LRG_TEMPLATE_SETTINGS_MENU_STATE_H

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../gamestate/lrg-game-state.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_SETTINGS_MENU_STATE (lrg_template_settings_menu_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTemplateSettingsMenuState, lrg_template_settings_menu_state,
                          LRG, TEMPLATE_SETTINGS_MENU_STATE, LrgGameState)

/**
 * LrgTemplateSettingsMenuStateClass:
 * @parent_class: the parent class
 * @create_graphics_tab: Creates the graphics settings tab content
 * @create_audio_tab: Creates the audio settings tab content
 * @create_controls_tab: Creates the controls settings tab content
 * @create_custom_tab: Creates a custom tab content
 * @on_apply: Called when Apply is selected
 * @on_cancel: Called when Cancel is selected
 * @on_reset: Called when Reset to Defaults is selected
 *
 * The class structure for #LrgTemplateSettingsMenuState.
 */
struct _LrgTemplateSettingsMenuStateClass
{
    LrgGameStateClass parent_class;

    /*< public >*/

    /**
     * LrgTemplateSettingsMenuStateClass::create_graphics_tab:
     * @self: an #LrgTemplateSettingsMenuState
     *
     * Creates the content widget for the graphics settings tab.
     * Default implementation creates sliders/toggles for common graphics settings.
     *
     * Returns: (transfer full) (nullable): A widget, or %NULL to skip this tab
     */
    LrgWidget * (*create_graphics_tab) (LrgTemplateSettingsMenuState *self);

    /**
     * LrgTemplateSettingsMenuStateClass::create_audio_tab:
     * @self: an #LrgTemplateSettingsMenuState
     *
     * Creates the content widget for the audio settings tab.
     * Default implementation creates volume sliders.
     *
     * Returns: (transfer full) (nullable): A widget, or %NULL to skip this tab
     */
    LrgWidget * (*create_audio_tab)    (LrgTemplateSettingsMenuState *self);

    /**
     * LrgTemplateSettingsMenuStateClass::create_controls_tab:
     * @self: an #LrgTemplateSettingsMenuState
     *
     * Creates the content widget for the controls settings tab.
     * Default implementation creates a keybind display/editor.
     *
     * Returns: (transfer full) (nullable): A widget, or %NULL to skip this tab
     */
    LrgWidget * (*create_controls_tab) (LrgTemplateSettingsMenuState *self);

    /**
     * LrgTemplateSettingsMenuStateClass::create_custom_tab:
     * @self: an #LrgTemplateSettingsMenuState
     * @tab_name: the name of the custom tab to create
     *
     * Creates content for a custom tab added via add_custom_tab().
     *
     * Returns: (transfer full) (nullable): A widget
     */
    LrgWidget * (*create_custom_tab)   (LrgTemplateSettingsMenuState *self,
                                        const gchar                  *tab_name);

    /**
     * LrgTemplateSettingsMenuStateClass::on_apply:
     * @self: an #LrgTemplateSettingsMenuState
     *
     * Called when Apply button is activated.
     * Default implementation applies and saves settings.
     */
    void (*on_apply)  (LrgTemplateSettingsMenuState *self);

    /**
     * LrgTemplateSettingsMenuStateClass::on_cancel:
     * @self: an #LrgTemplateSettingsMenuState
     *
     * Called when Cancel button is activated.
     * Default implementation discards changes and pops the state.
     */
    void (*on_cancel) (LrgTemplateSettingsMenuState *self);

    /**
     * LrgTemplateSettingsMenuStateClass::on_reset:
     * @self: an #LrgTemplateSettingsMenuState
     *
     * Called when Reset to Defaults button is activated.
     * Default implementation resets settings to defaults.
     */
    void (*on_reset)  (LrgTemplateSettingsMenuState *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_new:
 *
 * Creates a new settings menu state.
 *
 * Returns: (transfer full): A new #LrgTemplateSettingsMenuState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateSettingsMenuState *
lrg_template_settings_menu_state_new (void);

/* ==========================================================================
 * Tab Visibility
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_show_graphics_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Graphics tab is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_settings_menu_state_get_show_graphics_tab (LrgTemplateSettingsMenuState *self);

/**
 * lrg_template_settings_menu_state_set_show_graphics_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Graphics tab
 *
 * Sets whether the Graphics tab is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_set_show_graphics_tab (LrgTemplateSettingsMenuState *self,
                                                        gboolean                      show);

/**
 * lrg_template_settings_menu_state_get_show_audio_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Audio tab is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_settings_menu_state_get_show_audio_tab (LrgTemplateSettingsMenuState *self);

/**
 * lrg_template_settings_menu_state_set_show_audio_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Audio tab
 *
 * Sets whether the Audio tab is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_set_show_audio_tab (LrgTemplateSettingsMenuState *self,
                                                     gboolean                      show);

/**
 * lrg_template_settings_menu_state_get_show_controls_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Controls tab is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_settings_menu_state_get_show_controls_tab (LrgTemplateSettingsMenuState *self);

/**
 * lrg_template_settings_menu_state_set_show_controls_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Controls tab
 *
 * Sets whether the Controls tab is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_set_show_controls_tab (LrgTemplateSettingsMenuState *self,
                                                        gboolean                      show);

/* ==========================================================================
 * Custom Tabs
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_add_custom_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @name: the internal name for the tab
 * @label: the display label for the tab
 *
 * Adds a custom tab. Override create_custom_tab() to provide content.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_add_custom_tab (LrgTemplateSettingsMenuState *self,
                                                 const gchar                  *name,
                                                 const gchar                  *label);

/**
 * lrg_template_settings_menu_state_remove_custom_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @name: the internal name for the tab
 *
 * Removes a custom tab.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_remove_custom_tab (LrgTemplateSettingsMenuState *self,
                                                    const gchar                  *name);

/* ==========================================================================
 * Active Tab
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_active_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets the index of the currently active tab.
 *
 * Returns: The active tab index
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_template_settings_menu_state_get_active_tab (LrgTemplateSettingsMenuState *self);

/**
 * lrg_template_settings_menu_state_set_active_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @index: the tab index to activate
 *
 * Sets which tab is currently active.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_set_active_tab (LrgTemplateSettingsMenuState *self,
                                                 guint                         index);

/* ==========================================================================
 * Button Visibility
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_show_reset_button:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Reset to Defaults button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_settings_menu_state_get_show_reset_button (LrgTemplateSettingsMenuState *self);

/**
 * lrg_template_settings_menu_state_set_show_reset_button:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Reset button
 *
 * Sets whether the Reset to Defaults button is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_set_show_reset_button (LrgTemplateSettingsMenuState *self,
                                                        gboolean                      show);

/* ==========================================================================
 * Confirmation
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_confirm_cancel:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether canceling with unsaved changes requires confirmation.
 *
 * Returns: %TRUE if confirmation is required
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_settings_menu_state_get_confirm_cancel (LrgTemplateSettingsMenuState *self);

/**
 * lrg_template_settings_menu_state_set_confirm_cancel:
 * @self: an #LrgTemplateSettingsMenuState
 * @confirm: whether to confirm before canceling
 *
 * Sets whether canceling with unsaved changes requires confirmation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_set_confirm_cancel (LrgTemplateSettingsMenuState *self,
                                                     gboolean                      confirm);

/**
 * lrg_template_settings_menu_state_get_confirm_reset:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether reset to defaults requires confirmation.
 *
 * Returns: %TRUE if confirmation is required
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_settings_menu_state_get_confirm_reset (LrgTemplateSettingsMenuState *self);

/**
 * lrg_template_settings_menu_state_set_confirm_reset:
 * @self: an #LrgTemplateSettingsMenuState
 * @confirm: whether to confirm before resetting
 *
 * Sets whether reset to defaults requires confirmation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_settings_menu_state_set_confirm_reset (LrgTemplateSettingsMenuState *self,
                                                    gboolean                      confirm);

/* ==========================================================================
 * Dirty State
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_has_unsaved_changes:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Checks if there are unsaved settings changes.
 *
 * Returns: %TRUE if there are unsaved changes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_settings_menu_state_has_unsaved_changes (LrgTemplateSettingsMenuState *self);

G_END_DECLS

#endif /* LRG_TEMPLATE_SETTINGS_MENU_STATE_H */
