/* lrg-providers.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Content provider interfaces implementation.
 */

#include "config.h"
#include "lrg-providers.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

#include <string.h>

/* ==========================================================================
 * LrgConsoleCommand Boxed Type
 * ========================================================================== */

struct _LrgConsoleCommand
{
    gchar                 *name;
    gchar                 *description;
    LrgConsoleCommandFunc  callback;
    gpointer               user_data;
    GDestroyNotify         destroy;
};

#pragma GCC visibility push(default)
G_DEFINE_BOXED_TYPE (LrgConsoleCommand, lrg_console_command,
                     lrg_console_command_copy, lrg_console_command_free)
#pragma GCC visibility pop

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
LrgConsoleCommand *
lrg_console_command_new (const gchar            *name,
                         const gchar            *description,
                         LrgConsoleCommandFunc   callback,
                         gpointer                user_data,
                         GDestroyNotify          destroy)
{
    LrgConsoleCommand *cmd;

    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    cmd = g_new0 (LrgConsoleCommand, 1);
    cmd->name = g_strdup (name);
    cmd->description = g_strdup (description);
    cmd->callback = callback;
    cmd->user_data = user_data;
    cmd->destroy = destroy;

    return cmd;
}

/**
 * lrg_console_command_copy:
 * @self: a #LrgConsoleCommand
 *
 * Copies a console command.
 * Note: The callback and user_data are shared (not deep copied).
 *
 * Returns: (transfer full): a copy of @self
 */
LrgConsoleCommand *
lrg_console_command_copy (const LrgConsoleCommand *self)
{
    LrgConsoleCommand *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgConsoleCommand, 1);
    copy->name = g_strdup (self->name);
    copy->description = g_strdup (self->description);
    copy->callback = self->callback;
    copy->user_data = self->user_data;
    /* Don't copy destroy since we don't own the user_data in a copy */
    copy->destroy = NULL;

    return copy;
}

/**
 * lrg_console_command_free:
 * @self: a #LrgConsoleCommand
 *
 * Frees a console command.
 */
void
lrg_console_command_free (LrgConsoleCommand *self)
{
    if (self == NULL)
        return;

    if (self->destroy != NULL && self->user_data != NULL)
        self->destroy (self->user_data);

    g_free (self->name);
    g_free (self->description);
    g_free (self);
}

const gchar *
lrg_console_command_get_name (const LrgConsoleCommand *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->name;
}

const gchar *
lrg_console_command_get_description (const LrgConsoleCommand *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->description;
}

LrgConsoleCommandFunc
lrg_console_command_get_callback (const LrgConsoleCommand *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->callback;
}

gpointer
lrg_console_command_get_user_data (const LrgConsoleCommand *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->user_data;
}

/* ==========================================================================
 * LrgEntityProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgEntityProvider, lrg_entity_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_entity_provider_default_init (LrgEntityProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_entity_provider_get_entity_types:
 * @self: an #LrgEntityProvider
 *
 * Gets entity types provided by this mod.
 *
 * Returns: (transfer container) (element-type GType): list of GTypes
 */
GList *
lrg_entity_provider_get_entity_types (LrgEntityProvider *self)
{
    LrgEntityProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_ENTITY_PROVIDER (self), NULL);

    iface = LRG_ENTITY_PROVIDER_GET_IFACE (self);

    if (iface->get_entity_types == NULL)
        return NULL;

    return iface->get_entity_types (self);
}

/* ==========================================================================
 * LrgItemProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgItemProvider, lrg_item_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_item_provider_default_init (LrgItemProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_item_provider_get_item_defs:
 * @self: an #LrgItemProvider
 *
 * Gets item definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgItemDef): list of #LrgItemDef
 */
GList *
lrg_item_provider_get_item_defs (LrgItemProvider *self)
{
    LrgItemProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_ITEM_PROVIDER (self), NULL);

    iface = LRG_ITEM_PROVIDER_GET_IFACE (self);

    if (iface->get_item_defs == NULL)
        return NULL;

    return iface->get_item_defs (self);
}

/* ==========================================================================
 * LrgSceneProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgSceneProvider, lrg_scene_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_scene_provider_default_init (LrgSceneProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_scene_provider_get_scenes:
 * @self: an #LrgSceneProvider
 *
 * Gets scenes provided by this mod.
 *
 * Returns: (transfer container) (element-type GObject): list of GrlScene
 */
GList *
lrg_scene_provider_get_scenes (LrgSceneProvider *self)
{
    LrgSceneProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_SCENE_PROVIDER (self), NULL);

    iface = LRG_SCENE_PROVIDER_GET_IFACE (self);

    if (iface->get_scenes == NULL)
        return NULL;

    return iface->get_scenes (self);
}

/* ==========================================================================
 * LrgDialogProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgDialogProvider, lrg_dialog_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_dialog_provider_default_init (LrgDialogProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_dialog_provider_get_dialog_trees:
 * @self: an #LrgDialogProvider
 *
 * Gets dialog trees provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgDialogTree): list of #LrgDialogTree
 */
GList *
lrg_dialog_provider_get_dialog_trees (LrgDialogProvider *self)
{
    LrgDialogProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_DIALOG_PROVIDER (self), NULL);

    iface = LRG_DIALOG_PROVIDER_GET_IFACE (self);

    if (iface->get_dialog_trees == NULL)
        return NULL;

    return iface->get_dialog_trees (self);
}

/* ==========================================================================
 * LrgQuestProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgQuestProvider, lrg_quest_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_quest_provider_default_init (LrgQuestProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_quest_provider_get_quest_defs:
 * @self: an #LrgQuestProvider
 *
 * Gets quest definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgQuestDef): list of #LrgQuestDef
 */
GList *
lrg_quest_provider_get_quest_defs (LrgQuestProvider *self)
{
    LrgQuestProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_QUEST_PROVIDER (self), NULL);

    iface = LRG_QUEST_PROVIDER_GET_IFACE (self);

    if (iface->get_quest_defs == NULL)
        return NULL;

    return iface->get_quest_defs (self);
}

/* ==========================================================================
 * LrgAIProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgAIProvider, lrg_ai_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_ai_provider_default_init (LrgAIProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_ai_provider_get_bt_node_types:
 * @self: an #LrgAIProvider
 *
 * Gets behavior tree node types provided by this mod.
 *
 * Returns: (transfer container) (element-type GType): list of GTypes
 */
GList *
lrg_ai_provider_get_bt_node_types (LrgAIProvider *self)
{
    LrgAIProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_AI_PROVIDER (self), NULL);

    iface = LRG_AI_PROVIDER_GET_IFACE (self);

    if (iface->get_bt_node_types == NULL)
        return NULL;

    return iface->get_bt_node_types (self);
}

/* ==========================================================================
 * LrgCommandProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgCommandProvider, lrg_command_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_command_provider_default_init (LrgCommandProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_command_provider_get_commands:
 * @self: an #LrgCommandProvider
 *
 * Gets console commands provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgConsoleCommand): list of #LrgConsoleCommand
 */
GList *
lrg_command_provider_get_commands (LrgCommandProvider *self)
{
    LrgCommandProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_COMMAND_PROVIDER (self), NULL);

    iface = LRG_COMMAND_PROVIDER_GET_IFACE (self);

    if (iface->get_commands == NULL)
        return NULL;

    return iface->get_commands (self);
}

/* ==========================================================================
 * LrgLocaleProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgLocaleProvider, lrg_locale_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_locale_provider_default_init (LrgLocaleProviderInterface *iface)
{
    /* Default implementation */
}

/**
 * lrg_locale_provider_get_locales:
 * @self: an #LrgLocaleProvider
 *
 * Gets locales provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgLocale): list of #LrgLocale
 */
GList *
lrg_locale_provider_get_locales (LrgLocaleProvider *self)
{
    LrgLocaleProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_LOCALE_PROVIDER (self), NULL);

    iface = LRG_LOCALE_PROVIDER_GET_IFACE (self);

    if (iface->get_locales == NULL)
        return NULL;

    return iface->get_locales (self);
}

/* ==========================================================================
 * LrgCardProvider Interface
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgCardProvider, lrg_card_provider, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_card_provider_default_init (LrgCardProviderInterface *iface)
{
    /* Default implementation - all methods return NULL by default */
}

/**
 * lrg_card_provider_get_card_defs:
 * @self: an #LrgCardProvider
 *
 * Gets card definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgCardDef) (nullable): list of #LrgCardDef
 */
GList *
lrg_card_provider_get_card_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_card_defs == NULL)
        return NULL;

    return iface->get_card_defs (self);
}

/**
 * lrg_card_provider_get_deck_defs:
 * @self: an #LrgCardProvider
 *
 * Gets deck definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgDeckDef) (nullable): list of #LrgDeckDef
 */
GList *
lrg_card_provider_get_deck_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_deck_defs == NULL)
        return NULL;

    return iface->get_deck_defs (self);
}

/**
 * lrg_card_provider_get_relic_defs:
 * @self: an #LrgCardProvider
 *
 * Gets relic definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgRelicDef) (nullable): list of #LrgRelicDef
 */
GList *
lrg_card_provider_get_relic_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_relic_defs == NULL)
        return NULL;

    return iface->get_relic_defs (self);
}

/**
 * lrg_card_provider_get_potion_defs:
 * @self: an #LrgCardProvider
 *
 * Gets potion definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgPotionDef) (nullable): list of #LrgPotionDef
 */
GList *
lrg_card_provider_get_potion_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_potion_defs == NULL)
        return NULL;

    return iface->get_potion_defs (self);
}

/**
 * lrg_card_provider_get_enemy_defs:
 * @self: an #LrgCardProvider
 *
 * Gets enemy definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgEnemyDef) (nullable): list of #LrgEnemyDef
 */
GList *
lrg_card_provider_get_enemy_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_enemy_defs == NULL)
        return NULL;

    return iface->get_enemy_defs (self);
}

/**
 * lrg_card_provider_get_event_defs:
 * @self: an #LrgCardProvider
 *
 * Gets random event definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgEventDef) (nullable): list of #LrgEventDef
 */
GList *
lrg_card_provider_get_event_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_event_defs == NULL)
        return NULL;

    return iface->get_event_defs (self);
}

/**
 * lrg_card_provider_get_joker_defs:
 * @self: an #LrgCardProvider
 *
 * Gets joker definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgJokerDef) (nullable): list of #LrgJokerDef
 */
GList *
lrg_card_provider_get_joker_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_joker_defs == NULL)
        return NULL;

    return iface->get_joker_defs (self);
}

/**
 * lrg_card_provider_get_effect_executors:
 * @self: an #LrgCardProvider
 *
 * Gets effect executor GTypes provided by this mod.
 *
 * Returns: (transfer container) (element-type GType) (nullable): list of GTypes
 */
GList *
lrg_card_provider_get_effect_executors (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_effect_executors == NULL)
        return NULL;

    return iface->get_effect_executors (self);
}

/**
 * lrg_card_provider_get_status_effect_defs:
 * @self: an #LrgCardProvider
 *
 * Gets status effect definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef) (nullable): list of #LrgStatusEffectDef
 */
GList *
lrg_card_provider_get_status_effect_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_status_effect_defs == NULL)
        return NULL;

    return iface->get_status_effect_defs (self);
}

/**
 * lrg_card_provider_get_keyword_defs:
 * @self: an #LrgCardProvider
 *
 * Gets keyword definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgCardKeywordDef) (nullable): list of #LrgCardKeywordDef
 */
GList *
lrg_card_provider_get_keyword_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_keyword_defs == NULL)
        return NULL;

    return iface->get_keyword_defs (self);
}

/**
 * lrg_card_provider_get_character_defs:
 * @self: an #LrgCardProvider
 *
 * Gets character definitions provided by this mod.
 *
 * Returns: (transfer container) (element-type LrgCharacterDef) (nullable): list of #LrgCharacterDef
 */
GList *
lrg_card_provider_get_character_defs (LrgCardProvider *self)
{
    LrgCardProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_PROVIDER (self), NULL);

    iface = LRG_CARD_PROVIDER_GET_IFACE (self);

    if (iface->get_character_defs == NULL)
        return NULL;

    return iface->get_character_defs (self);
}
