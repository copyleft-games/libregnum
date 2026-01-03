/* lrg-template-main-menu-state.h - Main menu state for game templates
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_MAIN_MENU_STATE_H
#define LRG_TEMPLATE_MAIN_MENU_STATE_H

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../gamestate/lrg-game-state.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_MAIN_MENU_STATE (lrg_template_main_menu_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTemplateMainMenuState, lrg_template_main_menu_state,
                          LRG, TEMPLATE_MAIN_MENU_STATE, LrgGameState)

/**
 * LrgTemplateMainMenuStateClass:
 * @parent_class: the parent class
 * @create_menu_items: Creates the menu buttons. Override to customize menu items.
 * @on_new_game: Called when New Game is selected
 * @on_continue: Called when Continue is selected
 * @on_settings: Called when Settings is selected
 * @on_exit: Called when Exit is selected
 * @on_custom_item: Called when a custom menu item is selected
 *
 * The class structure for #LrgTemplateMainMenuState.
 *
 * Subclasses can override the virtual methods to customize behavior.
 */
struct _LrgTemplateMainMenuStateClass
{
    LrgGameStateClass parent_class;

    /*< public >*/

    /**
     * LrgTemplateMainMenuStateClass::create_menu_items:
     * @self: an #LrgTemplateMainMenuState
     *
     * Creates the menu buttons. Override to customize menu layout.
     * The default implementation creates New Game, Continue, Settings, and Exit.
     */
    void (*create_menu_items) (LrgTemplateMainMenuState *self);

    /**
     * LrgTemplateMainMenuStateClass::on_new_game:
     * @self: an #LrgTemplateMainMenuState
     *
     * Called when New Game button is activated.
     * Default implementation emits the ::new-game signal.
     */
    void (*on_new_game)       (LrgTemplateMainMenuState *self);

    /**
     * LrgTemplateMainMenuStateClass::on_continue:
     * @self: an #LrgTemplateMainMenuState
     *
     * Called when Continue button is activated.
     * Default implementation emits the ::continue-game signal.
     */
    void (*on_continue)       (LrgTemplateMainMenuState *self);

    /**
     * LrgTemplateMainMenuStateClass::on_settings:
     * @self: an #LrgTemplateMainMenuState
     *
     * Called when Settings button is activated.
     * Default implementation emits the ::settings signal.
     */
    void (*on_settings)       (LrgTemplateMainMenuState *self);

    /**
     * LrgTemplateMainMenuStateClass::on_exit:
     * @self: an #LrgTemplateMainMenuState
     *
     * Called when Exit button is activated.
     * Default implementation emits the ::exit-game signal.
     */
    void (*on_exit)           (LrgTemplateMainMenuState *self);

    /**
     * LrgTemplateMainMenuStateClass::on_custom_item:
     * @self: an #LrgTemplateMainMenuState
     * @item_id: the custom item identifier
     *
     * Called when a custom menu item is activated.
     * Default implementation emits the ::custom-item signal.
     */
    void (*on_custom_item)    (LrgTemplateMainMenuState *self,
                               const gchar              *item_id);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_new:
 *
 * Creates a new main menu state with default settings.
 *
 * Returns: (transfer full): A new #LrgTemplateMainMenuState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateMainMenuState *
lrg_template_main_menu_state_new (void);

/**
 * lrg_template_main_menu_state_new_with_title:
 * @title: the game title to display
 *
 * Creates a new main menu state with the specified title.
 *
 * Returns: (transfer full): A new #LrgTemplateMainMenuState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateMainMenuState *
lrg_template_main_menu_state_new_with_title (const gchar *title);

/* ==========================================================================
 * Title
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_get_title:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the game title displayed in the menu.
 *
 * Returns: (transfer none) (nullable): The title, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_main_menu_state_get_title (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_title:
 * @self: an #LrgTemplateMainMenuState
 * @title: (nullable): the title to display
 *
 * Sets the game title displayed in the menu.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_title (LrgTemplateMainMenuState *self,
                                        const gchar              *title);

/**
 * lrg_template_main_menu_state_get_title_font_size:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the title font size.
 *
 * Returns: The font size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_template_main_menu_state_get_title_font_size (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_title_font_size:
 * @self: an #LrgTemplateMainMenuState
 * @size: the font size in pixels
 *
 * Sets the title font size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_title_font_size (LrgTemplateMainMenuState *self,
                                                  gfloat                    size);

/* ==========================================================================
 * Background
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_get_background_color:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the background color.
 *
 * Returns: (transfer none): The background color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_template_main_menu_state_get_background_color (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_background_color:
 * @self: an #LrgTemplateMainMenuState
 * @color: the background color
 *
 * Sets the background color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_background_color (LrgTemplateMainMenuState *self,
                                                   const GrlColor           *color);

/**
 * lrg_template_main_menu_state_get_background_texture:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the background texture.
 *
 * Returns: (transfer none) (nullable): The texture, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_template_main_menu_state_get_background_texture (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_background_texture:
 * @self: an #LrgTemplateMainMenuState
 * @texture: (nullable): the background texture
 *
 * Sets the background texture. If set, this is drawn instead of the
 * background color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_background_texture (LrgTemplateMainMenuState *self,
                                                     GrlTexture               *texture);

/* ==========================================================================
 * Continue Button Visibility
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_get_show_continue:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets whether the Continue button is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_main_menu_state_get_show_continue (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_show_continue:
 * @self: an #LrgTemplateMainMenuState
 * @show: whether to show Continue button
 *
 * Sets whether the Continue button is shown. Typically you'd hide this
 * when there's no save game available.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_show_continue (LrgTemplateMainMenuState *self,
                                                gboolean                  show);

/* ==========================================================================
 * Custom Menu Items
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_add_custom_item:
 * @self: an #LrgTemplateMainMenuState
 * @item_id: unique identifier for the item
 * @label: the display text
 * @position: position in the menu (-1 for end, before Exit)
 *
 * Adds a custom menu item. When activated, the ::custom-item signal
 * is emitted with the item_id.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_add_custom_item (LrgTemplateMainMenuState *self,
                                              const gchar              *item_id,
                                              const gchar              *label,
                                              gint                      position);

/**
 * lrg_template_main_menu_state_remove_custom_item:
 * @self: an #LrgTemplateMainMenuState
 * @item_id: the item identifier
 *
 * Removes a custom menu item.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_remove_custom_item (LrgTemplateMainMenuState *self,
                                                 const gchar              *item_id);

/* ==========================================================================
 * Navigation
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_get_selected_index:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the currently selected menu item index.
 *
 * Returns: The selected index
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_template_main_menu_state_get_selected_index (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_selected_index:
 * @self: an #LrgTemplateMainMenuState
 * @index: the menu item index to select
 *
 * Sets the selected menu item index.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_selected_index (LrgTemplateMainMenuState *self,
                                                 gint                      index);

/**
 * lrg_template_main_menu_state_get_menu_item_count:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the total number of visible menu items.
 *
 * Returns: The number of menu items
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_template_main_menu_state_get_menu_item_count (LrgTemplateMainMenuState *self);

/* ==========================================================================
 * Menu Spacing and Layout
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_get_button_spacing:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the spacing between menu buttons.
 *
 * Returns: The spacing in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_template_main_menu_state_get_button_spacing (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_button_spacing:
 * @self: an #LrgTemplateMainMenuState
 * @spacing: the spacing in pixels
 *
 * Sets the spacing between menu buttons.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_button_spacing (LrgTemplateMainMenuState *self,
                                                 gfloat                    spacing);

/**
 * lrg_template_main_menu_state_get_button_width:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the width of menu buttons.
 *
 * Returns: The width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_template_main_menu_state_get_button_width (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_button_width:
 * @self: an #LrgTemplateMainMenuState
 * @width: the width in pixels
 *
 * Sets the width of menu buttons.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_button_width (LrgTemplateMainMenuState *self,
                                               gfloat                    width);

/**
 * lrg_template_main_menu_state_get_button_height:
 * @self: an #LrgTemplateMainMenuState
 *
 * Gets the height of menu buttons.
 *
 * Returns: The height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_template_main_menu_state_get_button_height (LrgTemplateMainMenuState *self);

/**
 * lrg_template_main_menu_state_set_button_height:
 * @self: an #LrgTemplateMainMenuState
 * @height: the height in pixels
 *
 * Sets the height of menu buttons.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_set_button_height (LrgTemplateMainMenuState *self,
                                                gfloat                    height);

/* ==========================================================================
 * Rebuild Menu
 * ========================================================================== */

/**
 * lrg_template_main_menu_state_rebuild_menu:
 * @self: an #LrgTemplateMainMenuState
 *
 * Rebuilds the menu UI. Call this after making changes to custom items
 * or visibility settings if the state has already been entered.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_main_menu_state_rebuild_menu (LrgTemplateMainMenuState *self);

G_END_DECLS

#endif /* LRG_TEMPLATE_MAIN_MENU_STATE_H */
