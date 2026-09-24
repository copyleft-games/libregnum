/* lrg-quest-chain.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ordered storyline of quests. The chain only stores quest IDs; progress
 * is always derived from a caller-supplied #LrgQuestLog so it can never
 * disagree with the authoritative log.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "quest/lrg-quest-chain.h"

struct _LrgQuestChain
{
    GObject    parent_instance;

    gchar     *id;
    gchar     *name;
    gchar     *storyline;
    gchar     *description;
    GPtrArray *quest_ids;  /* owned strings, chain order */
};

G_DEFINE_FINAL_TYPE (LrgQuestChain, lrg_quest_chain, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_STORYLINE,
    PROP_DESCRIPTION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * replace_string:
 * @slot: string field
 * @value: (nullable): new value
 *
 * Returns: whether the stored value changed
 */
static gboolean
replace_string (gchar       **slot,
                const gchar  *value)
{
    if (g_strcmp0 (*slot, value) == 0)
        return FALSE;
    g_free (*slot);
    *slot = g_strdup (value);
    return TRUE;
}

static void
lrg_quest_chain_finalize (GObject *object)
{
    LrgQuestChain *self = LRG_QUEST_CHAIN (object);

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->storyline, g_free);
    g_clear_pointer (&self->description, g_free);
    g_clear_pointer (&self->quest_ids, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_quest_chain_parent_class)->finalize (object);
}

static void
lrg_quest_chain_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgQuestChain *self = LRG_QUEST_CHAIN (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_STORYLINE:
        g_value_set_string (value, self->storyline);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_quest_chain_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgQuestChain *self = LRG_QUEST_CHAIN (object);

    switch (prop_id)
    {
    case PROP_ID:
        /* Construct-only: set exactly once. */
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        if (replace_string (&self->name, g_value_get_string (value)))
            g_object_notify_by_pspec (object, pspec);
        break;
    case PROP_STORYLINE:
        if (replace_string (&self->storyline, g_value_get_string (value)))
            g_object_notify_by_pspec (object, pspec);
        break;
    case PROP_DESCRIPTION:
        if (replace_string (&self->description, g_value_get_string (value)))
            g_object_notify_by_pspec (object, pspec);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_quest_chain_class_init (LrgQuestChainClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_quest_chain_finalize;
    object_class->get_property = lrg_quest_chain_get_property;
    object_class->set_property = lrg_quest_chain_set_property;

    /**
     * LrgQuestChain:id:
     *
     * Unique chain identifier.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Chain identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestChain:name:
     *
     * Display name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestChain:storyline:
     *
     * Storyline the chain belongs to, for grouping in a UI.
     */
    properties[PROP_STORYLINE] =
        g_param_spec_string ("storyline", "Storyline", "Storyline name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestChain:description:
     *
     * Description of the chain.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Chain description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_quest_chain_init (LrgQuestChain *self)
{
    self->quest_ids = g_ptr_array_new_with_free_func (g_free);
}

LrgQuestChain *
lrg_quest_chain_new (const gchar *id,
                     const gchar *name)
{
    g_return_val_if_fail (id != NULL && id[0] != '\0', NULL);

    return g_object_new (LRG_TYPE_QUEST_CHAIN,
                         "id", id,
                         "name", name,
                         NULL);
}

const gchar *
lrg_quest_chain_get_id (LrgQuestChain *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), NULL);
    return self->id;
}

const gchar *
lrg_quest_chain_get_name (LrgQuestChain *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), NULL);
    return self->name;
}

void
lrg_quest_chain_set_name (LrgQuestChain *self,
                          const gchar   *name)
{
    g_return_if_fail (LRG_IS_QUEST_CHAIN (self));
    g_object_set (self, "name", name, NULL);
}

const gchar *
lrg_quest_chain_get_storyline (LrgQuestChain *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), NULL);
    return self->storyline;
}

void
lrg_quest_chain_set_storyline (LrgQuestChain *self,
                               const gchar   *storyline)
{
    g_return_if_fail (LRG_IS_QUEST_CHAIN (self));
    g_object_set (self, "storyline", storyline, NULL);
}

const gchar *
lrg_quest_chain_get_description (LrgQuestChain *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), NULL);
    return self->description;
}

void
lrg_quest_chain_set_description (LrgQuestChain *self,
                                 const gchar   *description)
{
    g_return_if_fail (LRG_IS_QUEST_CHAIN (self));
    g_object_set (self, "description", description, NULL);
}

gboolean
lrg_quest_chain_add_quest (LrgQuestChain *self,
                           const gchar   *quest_id)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), FALSE);
    g_return_val_if_fail (quest_id != NULL && quest_id[0] != '\0', FALSE);

    if (lrg_quest_chain_index_of (self, quest_id) >= 0)
        return FALSE;

    g_ptr_array_add (self->quest_ids, g_strdup (quest_id));
    return TRUE;
}

GPtrArray *
lrg_quest_chain_get_quest_ids (LrgQuestChain *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), NULL);
    return self->quest_ids;
}

guint
lrg_quest_chain_get_length (LrgQuestChain *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), 0);
    return self->quest_ids->len;
}

gint
lrg_quest_chain_index_of (LrgQuestChain *self,
                          const gchar   *quest_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), -1);
    g_return_val_if_fail (quest_id != NULL, -1);

    for (i = 0; i < self->quest_ids->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (self->quest_ids, i), quest_id) == 0)
            return (gint)i;
    }
    return -1;
}

const gchar *
lrg_quest_chain_get_next (LrgQuestChain *self,
                          LrgQuestLog   *log)
{
    guint i;

    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), NULL);
    g_return_val_if_fail (LRG_IS_QUEST_LOG (log), NULL);

    for (i = 0; i < self->quest_ids->len; i++)
    {
        const gchar *quest_id = g_ptr_array_index (self->quest_ids, i);
        if (!lrg_quest_log_is_quest_completed (log, quest_id))
            return quest_id;
    }
    return NULL;
}

guint
lrg_quest_chain_get_progress (LrgQuestChain *self,
                              LrgQuestLog   *log)
{
    guint count;
    guint i;

    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), 0);
    g_return_val_if_fail (LRG_IS_QUEST_LOG (log), 0);

    count = 0;
    for (i = 0; i < self->quest_ids->len; i++)
    {
        if (lrg_quest_log_is_quest_completed (log, g_ptr_array_index (self->quest_ids, i)))
            count++;
    }
    return count;
}

gboolean
lrg_quest_chain_is_complete (LrgQuestChain *self,
                             LrgQuestLog   *log)
{
    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), FALSE);
    g_return_val_if_fail (LRG_IS_QUEST_LOG (log), FALSE);

    return lrg_quest_chain_get_next (self, log) == NULL;
}

/*
 * has_prerequisite:
 * @def: definition
 * @quest_id: prerequisite candidate
 *
 * Returns: whether @def already lists @quest_id as a prerequisite
 */
static gboolean
has_prerequisite (LrgQuestDef *def,
                  const gchar *quest_id)
{
    GPtrArray *prereqs;
    guint      i;

    prereqs = lrg_quest_def_get_prerequisites (def);
    for (i = 0; i < prereqs->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (prereqs, i), quest_id) == 0)
            return TRUE;
    }
    return FALSE;
}

gboolean
lrg_quest_chain_link_prerequisites (LrgQuestChain *self,
                                    GHashTable    *defs,
                                    GError       **error)
{
    guint i;

    g_return_val_if_fail (LRG_IS_QUEST_CHAIN (self), FALSE);
    g_return_val_if_fail (defs != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Resolve every definition before touching any, so failure is atomic. */
    for (i = 0; i < self->quest_ids->len; i++)
    {
        const gchar *quest_id = g_ptr_array_index (self->quest_ids, i);
        gpointer     def = g_hash_table_lookup (defs, quest_id);

        if (def == NULL || !LRG_IS_QUEST_DEF (def))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                         "Quest chain '%s' references unknown quest '%s'",
                         self->id, quest_id);
            return FALSE;
        }
    }

    /* Link each quest to its predecessor. */
    for (i = 1; i < self->quest_ids->len; i++)
    {
        const gchar *previous = g_ptr_array_index (self->quest_ids, i - 1);
        LrgQuestDef *def = LRG_QUEST_DEF (g_hash_table_lookup (defs,
                                          g_ptr_array_index (self->quest_ids, i)));

        if (!has_prerequisite (def, previous))
            lrg_quest_def_add_prerequisite (def, previous);
    }

    return TRUE;
}
