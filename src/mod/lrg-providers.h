/* lrg-providers.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Content provider interfaces for mods.
 *
 * These interfaces allow mods to provide various types of content
 * that will be registered with the engine subsystems.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Console Command (Boxed Type)
 * ========================================================================== */

#define LRG_TYPE_CONSOLE_COMMAND (lrg_console_command_get_type ())

/**
 * LrgConsoleCommand:
 *
 * A console command definition that can be registered with the debug console.
 *
 * Since: 1.0
 */
typedef struct _LrgConsoleCommand LrgConsoleCommand;

/**
 * LrgConsoleCommandFunc:
 * @console: the debug console
 * @argc: argument count
 * @argv: (array length=argc): argument values
 * @user_data: (nullable): user data
 *
 * Callback for console commands.
 *
 * Returns: (transfer full) (nullable): command output
 */
typedef gchar * (*LrgConsoleCommandFunc) (LrgDebugConsole  *console,
                                          guint             argc,
                                          const gchar     **argv,
                                          gpointer          user_data);

LRG_AVAILABLE_IN_ALL
GType              lrg_console_command_get_type    (void) G_GNUC_CONST;

/**
 * lrg_console_command_new:
 * @name: command name
 * @description: (nullable): command description
 * @callback: command callback function
 * @user_data: (nullable): user data for callback
 * @destroy: (nullable): destroy function for user_data
 *
 * Creates a new console command.
 *
 * Returns: (transfer full): a new #LrgConsoleCommand
 */
LRG_AVAILABLE_IN_ALL
LrgConsoleCommand *lrg_console_command_new         (const gchar            *name,
                                                    const gchar            *description,
                                                    LrgConsoleCommandFunc   callback,
                                                    gpointer                user_data,
                                                    GDestroyNotify          destroy);

LRG_AVAILABLE_IN_ALL
LrgConsoleCommand *lrg_console_command_copy        (const LrgConsoleCommand *self);

LRG_AVAILABLE_IN_ALL
void               lrg_console_command_free        (LrgConsoleCommand *self);

/**
 * lrg_console_command_get_name:
 * @self: a #LrgConsoleCommand
 *
 * Gets the command name.
 *
 * Returns: (transfer none): the command name
 */
LRG_AVAILABLE_IN_ALL
const gchar       *lrg_console_command_get_name    (const LrgConsoleCommand *self);

/**
 * lrg_console_command_get_description:
 * @self: a #LrgConsoleCommand
 *
 * Gets the command description.
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar       *lrg_console_command_get_description (const LrgConsoleCommand *self);

/**
 * lrg_console_command_get_callback:
 * @self: a #LrgConsoleCommand
 *
 * Gets the command callback function.
 *
 * Returns: the callback function
 */
LRG_AVAILABLE_IN_ALL
LrgConsoleCommandFunc lrg_console_command_get_callback (const LrgConsoleCommand *self);

/**
 * lrg_console_command_get_user_data:
 * @self: a #LrgConsoleCommand
 *
 * Gets the user data.
 *
 * Returns: (transfer none) (nullable): the user data
 */
LRG_AVAILABLE_IN_ALL
gpointer           lrg_console_command_get_user_data (const LrgConsoleCommand *self);

/* ==========================================================================
 * LrgEntityProvider Interface
 * ========================================================================== */

#define LRG_TYPE_ENTITY_PROVIDER (lrg_entity_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgEntityProvider, lrg_entity_provider, LRG, ENTITY_PROVIDER, GObject)

/**
 * LrgEntityProviderInterface:
 * @g_iface: parent interface
 * @get_entity_types: returns GTypes of entity classes
 *
 * Interface for providing entity types.
 */
struct _LrgEntityProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgEntityProviderInterface::get_entity_types:
     * @self: the provider
     *
     * Gets the entity GTypes provided by this mod.
     *
     * Returns: (transfer container) (element-type GType): list of GTypes
     */
    GList * (*get_entity_types) (LrgEntityProvider *self);
};

/**
 * lrg_entity_provider_get_entity_types:
 * @self: an #LrgEntityProvider
 *
 * Gets entity types provided by this mod.
 *
 * Returns: (transfer container) (element-type GType): list of GTypes
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_entity_provider_get_entity_types (LrgEntityProvider *self);

/* ==========================================================================
 * LrgItemProvider Interface
 * ========================================================================== */

#define LRG_TYPE_ITEM_PROVIDER (lrg_item_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgItemProvider, lrg_item_provider, LRG, ITEM_PROVIDER, GObject)

/**
 * LrgItemProviderInterface:
 * @g_iface: parent interface
 * @get_item_defs: returns item definitions
 *
 * Interface for providing item definitions.
 */
struct _LrgItemProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgItemProviderInterface::get_item_defs:
     * @self: the provider
     *
     * Gets the item definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgItemDef): list of item defs
     */
    GList * (*get_item_defs) (LrgItemProvider *self);
};

/**
 * lrg_item_provider_get_item_defs:
 * @self: an #LrgItemProvider
 *
 * Gets item definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgItemDef): list of #LrgItemDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_item_provider_get_item_defs (LrgItemProvider *self);

/* ==========================================================================
 * LrgSceneProvider Interface
 * ========================================================================== */

#define LRG_TYPE_SCENE_PROVIDER (lrg_scene_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgSceneProvider, lrg_scene_provider, LRG, SCENE_PROVIDER, GObject)

/**
 * LrgSceneProviderInterface:
 * @g_iface: parent interface
 * @get_scenes: returns scenes (graylib GrlScene)
 *
 * Interface for providing scenes.
 */
struct _LrgSceneProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgSceneProviderInterface::get_scenes:
     * @self: the provider
     *
     * Gets the scenes provided by this mod.
     * Each element should be a GrlScene from graylib.
     *
     * Returns: (transfer container) (element-type GObject): list of scenes
     */
    GList * (*get_scenes) (LrgSceneProvider *self);
};

/**
 * lrg_scene_provider_get_scenes:
 * @self: an #LrgSceneProvider
 *
 * Gets scenes provided by this mod.
 *
 * Returns: (transfer container) (element-type GObject): list of GrlScene
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_scene_provider_get_scenes (LrgSceneProvider *self);

/* ==========================================================================
 * LrgDialogProvider Interface
 * ========================================================================== */

#define LRG_TYPE_DIALOG_PROVIDER (lrg_dialog_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgDialogProvider, lrg_dialog_provider, LRG, DIALOG_PROVIDER, GObject)

/**
 * LrgDialogProviderInterface:
 * @g_iface: parent interface
 * @get_dialog_trees: returns dialog trees
 *
 * Interface for providing dialog trees.
 */
struct _LrgDialogProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgDialogProviderInterface::get_dialog_trees:
     * @self: the provider
     *
     * Gets the dialog trees provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgDialogTree): list of trees
     */
    GList * (*get_dialog_trees) (LrgDialogProvider *self);
};

/**
 * lrg_dialog_provider_get_dialog_trees:
 * @self: an #LrgDialogProvider
 *
 * Gets dialog trees provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgDialogTree): list of #LrgDialogTree
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_dialog_provider_get_dialog_trees (LrgDialogProvider *self);

/* ==========================================================================
 * LrgQuestProvider Interface
 * ========================================================================== */

#define LRG_TYPE_QUEST_PROVIDER (lrg_quest_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgQuestProvider, lrg_quest_provider, LRG, QUEST_PROVIDER, GObject)

/**
 * LrgQuestProviderInterface:
 * @g_iface: parent interface
 * @get_quest_defs: returns quest definitions
 *
 * Interface for providing quest definitions.
 */
struct _LrgQuestProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgQuestProviderInterface::get_quest_defs:
     * @self: the provider
     *
     * Gets the quest definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgQuestDef): list of quests
     */
    GList * (*get_quest_defs) (LrgQuestProvider *self);
};

/**
 * lrg_quest_provider_get_quest_defs:
 * @self: an #LrgQuestProvider
 *
 * Gets quest definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgQuestDef): list of #LrgQuestDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_quest_provider_get_quest_defs (LrgQuestProvider *self);

/* ==========================================================================
 * LrgAIProvider Interface
 * ========================================================================== */

#define LRG_TYPE_AI_PROVIDER (lrg_ai_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgAIProvider, lrg_ai_provider, LRG, AI_PROVIDER, GObject)

/**
 * LrgAIProviderInterface:
 * @g_iface: parent interface
 * @get_bt_node_types: returns behavior tree node GTypes
 *
 * Interface for providing AI behavior tree nodes.
 */
struct _LrgAIProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgAIProviderInterface::get_bt_node_types:
     * @self: the provider
     *
     * Gets the behavior tree node GTypes provided by this mod.
     *
     * Returns: (transfer container) (element-type GType): list of GTypes
     */
    GList * (*get_bt_node_types) (LrgAIProvider *self);
};

/**
 * lrg_ai_provider_get_bt_node_types:
 * @self: an #LrgAIProvider
 *
 * Gets behavior tree node types provided by this mod.
 *
 * Returns: (transfer container) (element-type GType): list of GTypes
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_ai_provider_get_bt_node_types (LrgAIProvider *self);

/* ==========================================================================
 * LrgCommandProvider Interface
 * ========================================================================== */

#define LRG_TYPE_COMMAND_PROVIDER (lrg_command_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgCommandProvider, lrg_command_provider, LRG, COMMAND_PROVIDER, GObject)

/**
 * LrgCommandProviderInterface:
 * @g_iface: parent interface
 * @get_commands: returns console commands
 *
 * Interface for providing debug console commands.
 */
struct _LrgCommandProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgCommandProviderInterface::get_commands:
     * @self: the provider
     *
     * Gets the console commands provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgConsoleCommand): list of commands
     */
    GList * (*get_commands) (LrgCommandProvider *self);
};

/**
 * lrg_command_provider_get_commands:
 * @self: an #LrgCommandProvider
 *
 * Gets console commands provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgConsoleCommand): list of #LrgConsoleCommand
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_command_provider_get_commands (LrgCommandProvider *self);

/* ==========================================================================
 * LrgLocaleProvider Interface
 * ========================================================================== */

#define LRG_TYPE_LOCALE_PROVIDER (lrg_locale_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgLocaleProvider, lrg_locale_provider, LRG, LOCALE_PROVIDER, GObject)

/**
 * LrgLocaleProviderInterface:
 * @g_iface: parent interface
 * @get_locales: returns locales
 *
 * Interface for providing localization data.
 */
struct _LrgLocaleProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgLocaleProviderInterface::get_locales:
     * @self: the provider
     *
     * Gets the locales provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgLocale): list of locales
     */
    GList * (*get_locales) (LrgLocaleProvider *self);
};

/**
 * lrg_locale_provider_get_locales:
 * @self: an #LrgLocaleProvider
 *
 * Gets locales provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgLocale): list of #LrgLocale
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_locale_provider_get_locales (LrgLocaleProvider *self);

/* ==========================================================================
 * LrgCardProvider Interface
 * ========================================================================== */

#define LRG_TYPE_CARD_PROVIDER (lrg_card_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgCardProvider, lrg_card_provider, LRG, CARD_PROVIDER, GObject)

/**
 * LrgCardProviderInterface:
 * @g_iface: parent interface
 * @get_card_defs: returns card definitions
 * @get_deck_defs: returns deck definitions
 * @get_relic_defs: returns relic definitions
 * @get_potion_defs: returns potion definitions
 * @get_enemy_defs: returns enemy definitions
 * @get_event_defs: returns random event definitions
 * @get_joker_defs: returns joker definitions
 * @get_effect_executors: returns effect executor GTypes
 * @get_status_effect_defs: returns status effect definitions
 * @get_keyword_defs: returns keyword definitions
 * @get_character_defs: returns character definitions
 *
 * Interface for providing deckbuilder content.
 *
 * This comprehensive interface allows mods to provide all types of
 * deckbuilder content including cards, relics, enemies, and more.
 *
 * Since: 1.0
 */
struct _LrgCardProviderInterface
{
    GTypeInterface g_iface;

    /**
     * LrgCardProviderInterface::get_card_defs:
     * @self: the provider
     *
     * Gets the card definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgCardDef) (nullable): list of card defs
     */
    GList * (*get_card_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_deck_defs:
     * @self: the provider
     *
     * Gets the deck definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgDeckDef) (nullable): list of deck defs
     */
    GList * (*get_deck_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_relic_defs:
     * @self: the provider
     *
     * Gets the relic definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgRelicDef) (nullable): list of relic defs
     */
    GList * (*get_relic_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_potion_defs:
     * @self: the provider
     *
     * Gets the potion definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgPotionDef) (nullable): list of potion defs
     */
    GList * (*get_potion_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_enemy_defs:
     * @self: the provider
     *
     * Gets the enemy definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgEnemyDef) (nullable): list of enemy defs
     */
    GList * (*get_enemy_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_event_defs:
     * @self: the provider
     *
     * Gets the random event definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgEventDef) (nullable): list of event defs
     */
    GList * (*get_event_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_joker_defs:
     * @self: the provider
     *
     * Gets the joker definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgJokerDef) (nullable): list of joker defs
     */
    GList * (*get_joker_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_effect_executors:
     * @self: the provider
     *
     * Gets the effect executor GTypes provided by this mod.
     *
     * Returns: (transfer container) (element-type GType) (nullable): list of GTypes
     */
    GList * (*get_effect_executors) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_status_effect_defs:
     * @self: the provider
     *
     * Gets the status effect definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgStatusEffectDef) (nullable): list of status defs
     */
    GList * (*get_status_effect_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_keyword_defs:
     * @self: the provider
     *
     * Gets the keyword definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgCardKeywordDef) (nullable): list of keyword defs
     */
    GList * (*get_keyword_defs) (LrgCardProvider *self);

    /**
     * LrgCardProviderInterface::get_character_defs:
     * @self: the provider
     *
     * Gets the character definitions provided by this mod.
     *
     * Returns: (transfer container) (element-type LrgCharacterDef) (nullable): list of character defs
     */
    GList * (*get_character_defs) (LrgCardProvider *self);
};

/**
 * lrg_card_provider_get_card_defs:
 * @self: an #LrgCardProvider
 *
 * Gets card definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgCardDef) (nullable): list of #LrgCardDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_card_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_deck_defs:
 * @self: an #LrgCardProvider
 *
 * Gets deck definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgDeckDef) (nullable): list of #LrgDeckDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_deck_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_relic_defs:
 * @self: an #LrgCardProvider
 *
 * Gets relic definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgRelicDef) (nullable): list of #LrgRelicDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_relic_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_potion_defs:
 * @self: an #LrgCardProvider
 *
 * Gets potion definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgPotionDef) (nullable): list of #LrgPotionDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_potion_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_enemy_defs:
 * @self: an #LrgCardProvider
 *
 * Gets enemy definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgEnemyDef) (nullable): list of #LrgEnemyDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_enemy_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_event_defs:
 * @self: an #LrgCardProvider
 *
 * Gets random event definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgEventDef) (nullable): list of #LrgEventDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_event_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_joker_defs:
 * @self: an #LrgCardProvider
 *
 * Gets joker definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgJokerDef) (nullable): list of #LrgJokerDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_joker_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_effect_executors:
 * @self: an #LrgCardProvider
 *
 * Gets effect executor GTypes provided by this mod.
 *
 * Returns: (transfer container) (element-type GType) (nullable): list of GTypes
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_effect_executors (LrgCardProvider *self);

/**
 * lrg_card_provider_get_status_effect_defs:
 * @self: an #LrgCardProvider
 *
 * Gets status effect definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef) (nullable): list of #LrgStatusEffectDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_status_effect_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_keyword_defs:
 * @self: an #LrgCardProvider
 *
 * Gets keyword definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgCardKeywordDef) (nullable): list of #LrgCardKeywordDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_keyword_defs (LrgCardProvider *self);

/**
 * lrg_card_provider_get_character_defs:
 * @self: an #LrgCardProvider
 *
 * Gets character definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgCharacterDef) (nullable): list of #LrgCharacterDef
 */
LRG_AVAILABLE_IN_ALL
GList *lrg_card_provider_get_character_defs (LrgCardProvider *self);

G_END_DECLS
