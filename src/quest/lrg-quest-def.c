/* lrg-quest-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-log.h"

typedef struct
{
    gchar      *id;
    gchar      *name;
    gchar      *description;
    gchar      *giver_npc;
    GPtrArray  *stages;           /* LrgQuestObjective, objective 0 of each stage (borrowed) */
    GPtrArray  *stage_objectives; /* GPtrArray per stage, owning every LrgQuestObjective */
    GPtrArray  *prerequisites;    /* string quest IDs */
    GPtrArray  *exclusives;       /* string quest IDs */
    guint       min_level;
    LrgQuestRepeat repeat;
    gchar      *category;
    gchar      *chain_id;
    gchar      *zone;
    gint        reward_gold;
    gint        reward_xp;
    GHashTable *reward_items;   /* string -> GINT_TO_POINTER(count) */
} LrgQuestDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgQuestDef, lrg_quest_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_GIVER_NPC,
    PROP_REWARD_GOLD,
    PROP_REWARD_XP,
    PROP_MIN_LEVEL,
    PROP_REPEAT,
    PROP_CATEGORY,
    PROP_CHAIN_ID,
    PROP_ZONE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean lrg_quest_def_real_check_prerequisites (LrgQuestDef *self,
                                                        gpointer     player);
static void     lrg_quest_def_real_grant_rewards       (LrgQuestDef *self,
                                                        gpointer     player);

static void
lrg_quest_def_finalize (GObject *object)
{
    LrgQuestDef        *self = LRG_QUEST_DEF (object);
    LrgQuestDefPrivate *priv = lrg_quest_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->giver_npc, g_free);
    g_clear_pointer (&priv->category, g_free);
    g_clear_pointer (&priv->chain_id, g_free);
    g_clear_pointer (&priv->zone, g_free);
    /* stages only borrows objective 0 of each stage; drop it first. */
    g_clear_pointer (&priv->stages, g_ptr_array_unref);
    g_clear_pointer (&priv->stage_objectives, g_ptr_array_unref);
    g_clear_pointer (&priv->prerequisites, g_ptr_array_unref);
    g_clear_pointer (&priv->exclusives, g_ptr_array_unref);
    g_clear_pointer (&priv->reward_items, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_quest_def_parent_class)->finalize (object);
}

static void
lrg_quest_def_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgQuestDef        *self = LRG_QUEST_DEF (object);
    LrgQuestDefPrivate *priv = lrg_quest_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
    case PROP_GIVER_NPC:
        g_value_set_string (value, priv->giver_npc);
        break;
    case PROP_REWARD_GOLD:
        g_value_set_int (value, priv->reward_gold);
        break;
    case PROP_REWARD_XP:
        g_value_set_int (value, priv->reward_xp);
        break;
    case PROP_MIN_LEVEL:
        g_value_set_uint (value, priv->min_level);
        break;
    case PROP_REPEAT:
        g_value_set_enum (value, priv->repeat);
        break;
    case PROP_CATEGORY:
        g_value_set_string (value, priv->category);
        break;
    case PROP_CHAIN_ID:
        g_value_set_string (value, priv->chain_id);
        break;
    case PROP_ZONE:
        g_value_set_string (value, priv->zone);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_quest_def_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgQuestDef        *self = LRG_QUEST_DEF (object);
    LrgQuestDefPrivate *priv = lrg_quest_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_free (priv->description);
        priv->description = g_value_dup_string (value);
        break;
    case PROP_GIVER_NPC:
        g_free (priv->giver_npc);
        priv->giver_npc = g_value_dup_string (value);
        break;
    case PROP_REWARD_GOLD:
        priv->reward_gold = g_value_get_int (value);
        break;
    case PROP_REWARD_XP:
        priv->reward_xp = g_value_get_int (value);
        break;
    case PROP_MIN_LEVEL:
        if (priv->min_level != g_value_get_uint (value))
        {
            priv->min_level = g_value_get_uint (value);
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_REPEAT:
        if (priv->repeat != (LrgQuestRepeat)g_value_get_enum (value))
        {
            priv->repeat = (LrgQuestRepeat)g_value_get_enum (value);
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_CATEGORY:
        if (g_strcmp0 (priv->category, g_value_get_string (value)) != 0)
        {
            g_free (priv->category);
            priv->category = g_value_dup_string (value);
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_CHAIN_ID:
        if (g_strcmp0 (priv->chain_id, g_value_get_string (value)) != 0)
        {
            g_free (priv->chain_id);
            priv->chain_id = g_value_dup_string (value);
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_ZONE:
        if (g_strcmp0 (priv->zone, g_value_get_string (value)) != 0)
        {
            g_free (priv->zone);
            priv->zone = g_value_dup_string (value);
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_quest_def_class_init (LrgQuestDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_quest_def_finalize;
    object_class->get_property = lrg_quest_def_get_property;
    object_class->set_property = lrg_quest_def_set_property;

    klass->check_prerequisites = lrg_quest_def_real_check_prerequisites;
    klass->grant_rewards = lrg_quest_def_real_grant_rewards;

    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Quest identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Quest name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Quest description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GIVER_NPC] =
        g_param_spec_string ("giver-npc", "Giver NPC", "Quest giver NPC ID",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_REWARD_GOLD] =
        g_param_spec_int ("reward-gold", "Reward Gold", "Gold reward amount",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_REWARD_XP] =
        g_param_spec_int ("reward-xp", "Reward XP", "Experience reward amount",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestDef:min-level:
     *
     * Minimum character level required by lrg_quest_log_can_start().
     */
    properties[PROP_MIN_LEVEL] =
        g_param_spec_uint ("min-level", "Minimum Level", "Minimum level to start the quest",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestDef:repeat:
     *
     * How often the quest may be completed again.
     */
    properties[PROP_REPEAT] =
        g_param_spec_enum ("repeat", "Repeat", "Repeat cadence",
                           LRG_TYPE_QUEST_REPEAT, LRG_QUEST_REPEAT_NONE,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestDef:category:
     *
     * Game-defined category such as "story", "daily" or "class".
     */
    properties[PROP_CATEGORY] =
        g_param_spec_string ("category", "Category", "Quest category",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestDef:chain-id:
     *
     * Identifier of the #LrgQuestChain the quest belongs to.
     */
    properties[PROP_CHAIN_ID] =
        g_param_spec_string ("chain-id", "Chain ID", "Owning quest chain",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestDef:zone:
     *
     * Zone the quest is offered in.
     */
    properties[PROP_ZONE] =
        g_param_spec_string ("zone", "Zone", "Zone the quest is offered in",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
objective_free_wrapper (gpointer data)
{
    lrg_quest_objective_free ((LrgQuestObjective *)data);
}

static void
lrg_quest_def_init (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv = lrg_quest_def_get_instance_private (self);

    priv->stages = g_ptr_array_new ();
    priv->stage_objectives = g_ptr_array_new_with_free_func ((GDestroyNotify)g_ptr_array_unref);
    priv->prerequisites = g_ptr_array_new_with_free_func (g_free);
    priv->exclusives = g_ptr_array_new_with_free_func (g_free);
    priv->repeat = LRG_QUEST_REPEAT_NONE;
    priv->reward_items = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, NULL);
}

static gboolean
lrg_quest_def_real_check_prerequisites (LrgQuestDef *self,
                                        gpointer     player)
{
    LrgQuestDefPrivate *priv = lrg_quest_def_get_instance_private (self);
    LrgQuestLog        *log;
    guint               i;

    /* Historical behaviour for anything that is not a quest log. */
    if (player == NULL || !LRG_IS_QUEST_LOG (player))
        return (priv->prerequisites->len == 0);

    log = LRG_QUEST_LOG (player);

    /* Every prerequisite must have been completed at least once. */
    for (i = 0; i < priv->prerequisites->len; i++)
    {
        const gchar *id = g_ptr_array_index (priv->prerequisites, i);
        if (!lrg_quest_log_is_quest_completed (log, id))
            return FALSE;
    }

    /* No mutually exclusive quest may be active or completed. */
    for (i = 0; i < priv->exclusives->len; i++)
    {
        const gchar *id = g_ptr_array_index (priv->exclusives, i);
        if (lrg_quest_log_is_quest_active (log, id) ||
            lrg_quest_log_is_quest_completed (log, id))
            return FALSE;
    }

    return TRUE;
}

static void
lrg_quest_def_real_grant_rewards (LrgQuestDef *self,
                                  gpointer     player)
{
    /* Default: no-op, subclasses implement actual reward granting */
    (void)self;
    (void)player;
}

LrgQuestDef *
lrg_quest_def_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);
    return g_object_new (LRG_TYPE_QUEST_DEF, "id", id, NULL);
}

const gchar *
lrg_quest_def_get_id (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_quest_def_get_name (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->name;
}

void
lrg_quest_def_set_name (LrgQuestDef *self,
                        const gchar *name)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "name", name, NULL);
}

const gchar *
lrg_quest_def_get_description (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->description;
}

void
lrg_quest_def_set_description (LrgQuestDef *self,
                               const gchar *description)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "description", description, NULL);
}

const gchar *
lrg_quest_def_get_giver_npc (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->giver_npc;
}

void
lrg_quest_def_set_giver_npc (LrgQuestDef *self,
                             const gchar *npc_id)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "giver-npc", npc_id, NULL);
}

void
lrg_quest_def_add_stage (LrgQuestDef       *self,
                         LrgQuestObjective *objective)
{
    LrgQuestDefPrivate *priv;
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_return_if_fail (objective != NULL);
    priv = lrg_quest_def_get_instance_private (self);

    /* The per-stage array owns the objective; stages borrows objective 0. */
    {
        GPtrArray *objectives = g_ptr_array_new_with_free_func (objective_free_wrapper);
        g_ptr_array_add (objectives, objective);
        g_ptr_array_add (priv->stage_objectives, objectives);
    }
    g_ptr_array_add (priv->stages, objective);
}

GPtrArray *
lrg_quest_def_get_stages (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->stages;
}

guint
lrg_quest_def_get_stage_count (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), 0);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->stages->len;
}

LrgQuestObjective *
lrg_quest_def_get_stage (LrgQuestDef *self,
                         guint        index)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    if (index >= priv->stages->len)
        return NULL;
    return g_ptr_array_index (priv->stages, index);
}

void
lrg_quest_def_add_prerequisite (LrgQuestDef *self,
                                const gchar *quest_id)
{
    LrgQuestDefPrivate *priv;
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_return_if_fail (quest_id != NULL);
    priv = lrg_quest_def_get_instance_private (self);
    g_ptr_array_add (priv->prerequisites, g_strdup (quest_id));
}

GPtrArray *
lrg_quest_def_get_prerequisites (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->prerequisites;
}

void
lrg_quest_def_set_reward_gold (LrgQuestDef *self,
                               gint         gold)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "reward-gold", gold, NULL);
}

gint
lrg_quest_def_get_reward_gold (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), 0);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->reward_gold;
}

void
lrg_quest_def_set_reward_xp (LrgQuestDef *self,
                             gint         xp)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "reward-xp", xp, NULL);
}

gint
lrg_quest_def_get_reward_xp (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), 0);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->reward_xp;
}

void
lrg_quest_def_add_reward_item (LrgQuestDef *self,
                               const gchar *item_id,
                               guint        count)
{
    LrgQuestDefPrivate *priv;
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_return_if_fail (item_id != NULL);
    priv = lrg_quest_def_get_instance_private (self);
    g_hash_table_replace (priv->reward_items, g_strdup (item_id),
                          GINT_TO_POINTER ((gint)count));
}

GHashTable *
lrg_quest_def_get_reward_items (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->reward_items;
}

gboolean
lrg_quest_def_check_prerequisites (LrgQuestDef *self,
                                   gpointer     player)
{
    LrgQuestDefClass *klass;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), FALSE);
    klass = LRG_QUEST_DEF_GET_CLASS (self);
    if (klass->check_prerequisites)
        return klass->check_prerequisites (self, player);
    return TRUE;
}

void
lrg_quest_def_grant_rewards (LrgQuestDef *self,
                             gpointer     player)
{
    LrgQuestDefClass *klass;
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    klass = LRG_QUEST_DEF_GET_CLASS (self);
    if (klass->grant_rewards)
        klass->grant_rewards (self, player);
}

guint
lrg_quest_def_get_min_level (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), 0);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->min_level;
}

void
lrg_quest_def_set_min_level (LrgQuestDef *self,
                             guint        min_level)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "min-level", min_level, NULL);
}

LrgQuestRepeat
lrg_quest_def_get_repeat (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), LRG_QUEST_REPEAT_NONE);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->repeat;
}

void
lrg_quest_def_set_repeat (LrgQuestDef    *self,
                          LrgQuestRepeat  repeat)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_return_if_fail (repeat == LRG_QUEST_REPEAT_NONE ||
                      repeat == LRG_QUEST_REPEAT_DAILY ||
                      repeat == LRG_QUEST_REPEAT_WEEKLY);
    g_object_set (self, "repeat", repeat, NULL);
}

const gchar *
lrg_quest_def_get_category (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->category;
}

void
lrg_quest_def_set_category (LrgQuestDef *self,
                            const gchar *category)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "category", category, NULL);
}

const gchar *
lrg_quest_def_get_chain_id (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->chain_id;
}

void
lrg_quest_def_set_chain_id (LrgQuestDef *self,
                            const gchar *chain_id)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "chain-id", chain_id, NULL);
}

const gchar *
lrg_quest_def_get_zone (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->zone;
}

void
lrg_quest_def_set_zone (LrgQuestDef *self,
                        const gchar *zone)
{
    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_object_set (self, "zone", zone, NULL);
}

void
lrg_quest_def_add_exclusive (LrgQuestDef *self,
                             const gchar *quest_id)
{
    LrgQuestDefPrivate *priv;
    guint               i;

    g_return_if_fail (LRG_IS_QUEST_DEF (self));
    g_return_if_fail (quest_id != NULL && quest_id[0] != '\0');
    priv = lrg_quest_def_get_instance_private (self);

    for (i = 0; i < priv->exclusives->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (priv->exclusives, i), quest_id) == 0)
            return;
    }
    g_ptr_array_add (priv->exclusives, g_strdup (quest_id));
}

GPtrArray *
lrg_quest_def_get_exclusives (LrgQuestDef *self)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    return priv->exclusives;
}

gboolean
lrg_quest_def_add_stage_objective (LrgQuestDef       *self,
                                   guint              stage,
                                   LrgQuestObjective *objective)
{
    LrgQuestDefPrivate *priv;
    GPtrArray          *objectives;
    const gchar        *new_id;
    guint               i;

    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), FALSE);
    g_return_val_if_fail (objective != NULL, FALSE);
    priv = lrg_quest_def_get_instance_private (self);

    /* Ownership is always taken, so every rejection frees the objective. */
    if (stage >= priv->stage_objectives->len)
    {
        lrg_quest_objective_free (objective);
        return FALSE;
    }

    objectives = g_ptr_array_index (priv->stage_objectives, stage);
    if (objectives->len >= LRG_QUEST_MAX_STAGE_OBJECTIVES)
    {
        lrg_quest_objective_free (objective);
        return FALSE;
    }

    new_id = lrg_quest_objective_get_id (objective);
    for (i = 0; i < objectives->len; i++)
    {
        LrgQuestObjective *existing = g_ptr_array_index (objectives, i);
        if (g_strcmp0 (lrg_quest_objective_get_id (existing), new_id) == 0)
        {
            lrg_quest_objective_free (objective);
            return FALSE;
        }
    }

    g_ptr_array_add (objectives, objective);
    return TRUE;
}

GPtrArray *
lrg_quest_def_get_stage_objectives (LrgQuestDef *self,
                                    guint        stage)
{
    LrgQuestDefPrivate *priv;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), NULL);
    priv = lrg_quest_def_get_instance_private (self);
    if (stage >= priv->stage_objectives->len)
        return NULL;
    return g_ptr_array_index (priv->stage_objectives, stage);
}

guint
lrg_quest_def_get_stage_objective_count (LrgQuestDef *self,
                                         guint        stage)
{
    GPtrArray *objectives;
    g_return_val_if_fail (LRG_IS_QUEST_DEF (self), 0);
    objectives = lrg_quest_def_get_stage_objectives (self, stage);
    return objectives != NULL ? objectives->len : 0;
}
