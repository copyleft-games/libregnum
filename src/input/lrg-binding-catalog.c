/* lrg-binding-catalog.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Groups and actions are kept in arrays (their order is the screen's);
 * an id index finds actions.  Keys are stored as given and compared
 * normalized (trimmed, lower case).
 */

#include "config.h"

#include "lrg-binding-catalog.h"

#include <string.h>

typedef struct
{
    gchar *id;
    gchar *label;
} Group;

typedef struct
{
    gchar     *id;
    gchar     *label;
    gchar     *group;
    gchar     *owner;
    GPtrArray *keys;   /* gchar*, NULL-terminated for get_keys() */
} Action;

struct _LrgBindingCatalog
{
    GObject     parent_instance;

    GPtrArray  *groups;    /* Group* */
    GPtrArray  *actions;   /* Action* */
    GHashTable *index;     /* id -> Action* (borrowed) */
};

G_DEFINE_FINAL_TYPE (LrgBindingCatalog, lrg_binding_catalog, G_TYPE_OBJECT)

enum
{
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
group_free (gpointer data)
{
    Group *group = data;

    g_free (group->id);
    g_free (group->label);
    g_free (group);
}

static void
action_free (gpointer data)
{
    Action *action = data;

    g_free (action->id);
    g_free (action->label);
    g_free (action->group);
    g_free (action->owner);
    g_ptr_array_unref (action->keys);
    g_free (action);
}

static void
lrg_binding_catalog_finalize (GObject *object)
{
    LrgBindingCatalog *self = LRG_BINDING_CATALOG (object);

    g_clear_pointer (&self->index, g_hash_table_unref);
    g_clear_pointer (&self->actions, g_ptr_array_unref);
    g_clear_pointer (&self->groups, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_binding_catalog_parent_class)->finalize (object);
}

static void
lrg_binding_catalog_class_init (LrgBindingCatalogClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_binding_catalog_finalize;

    signals[SIGNAL_CHANGED] =
        g_signal_new ("changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_binding_catalog_init (LrgBindingCatalog *self)
{
    self->groups = g_ptr_array_new_with_free_func (group_free);
    self->actions = g_ptr_array_new_with_free_func (action_free);
    self->index = g_hash_table_new (g_str_hash, g_str_equal);
}

LrgBindingCatalog *
lrg_binding_catalog_new (void)
{
    return g_object_new (LRG_TYPE_BINDING_CATALOG, NULL);
}

gchar *
lrg_binding_catalog_normalize_key (const gchar *key)
{
    g_autofree gchar *copy = NULL;

    g_return_val_if_fail (key != NULL, NULL);

    copy = g_strstrip (g_strdup (key));
    return g_ascii_strdown (copy, -1);
}

static gboolean
key_equal (const gchar *a,
           const gchar *b)
{
    g_autofree gchar *na = lrg_binding_catalog_normalize_key (a);
    g_autofree gchar *nb = lrg_binding_catalog_normalize_key (b);

    return strcmp (na, nb) == 0;
}

/* the index of @key in @action's keys, -1 */
static gint
action_key_index (Action      *action,
                  const gchar *key)
{
    guint i;

    for (i = 0; i + 1 < action->keys->len; i++)
    {
        if (key_equal (g_ptr_array_index (action->keys, i), key))
            return (gint)i;
    }
    return -1;
}

static guint
action_key_count (Action *action)
{
    return action->keys->len - 1;   /* the NULL terminator */
}

/* replaces the keys: duplicates and empty names dropped, limited */
static void
action_set_keys (Action              *action,
                 const gchar * const *keys)
{
    guint i;

    g_ptr_array_set_size (action->keys, 0);
    g_ptr_array_add (action->keys, NULL);
    for (i = 0; keys != NULL && keys[i] != NULL; i++)
    {
        g_autofree gchar *trimmed = NULL;

        trimmed = g_strstrip (g_strdup (keys[i]));
        if (*trimmed == '\0' || action_key_index (action, trimmed) >= 0 ||
            action_key_count (action) >= LRG_BINDING_CATALOG_MAX_KEYS)
            continue;
        g_ptr_array_insert (action->keys, (gint)action_key_count (action), g_steal_pointer (&trimmed));
    }
}

static void
action_remove_key (Action *action,
                   guint   index)
{
    g_ptr_array_remove_index (action->keys, index);
}

static Group *
find_group (LrgBindingCatalog *self,
            const gchar       *id)
{
    guint i;

    for (i = 0; i < self->groups->len; i++)
    {
        Group *group = g_ptr_array_index (self->groups, i);

        if (strcmp (group->id, id) == 0)
            return group;
    }
    return NULL;
}

void
lrg_binding_catalog_add_group (LrgBindingCatalog *self,
                               const gchar       *id,
                               const gchar       *label)
{
    Group *group;

    g_return_if_fail (LRG_IS_BINDING_CATALOG (self));
    g_return_if_fail (id != NULL);

    group = find_group (self, id);
    if (group == NULL)
    {
        group = g_new0 (Group, 1);
        group->id = g_strdup (id);
        g_ptr_array_add (self->groups, group);
    }
    g_free (group->label);
    group->label = g_strdup (label != NULL ? label : id);
}

GStrv
lrg_binding_catalog_get_groups (LrgBindingCatalog *self)
{
    GPtrArray *out;
    guint i;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);

    out = g_ptr_array_new ();
    for (i = 0; i < self->groups->len; i++)
        g_ptr_array_add (out, g_strdup (((Group *)g_ptr_array_index (self->groups, i))->id));
    g_ptr_array_add (out, NULL);
    return (GStrv)g_ptr_array_free (out, FALSE);
}

const gchar *
lrg_binding_catalog_get_group_label (LrgBindingCatalog *self,
                                     const gchar       *id)
{
    Group *group;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    group = find_group (self, id);
    return group != NULL ? group->label : NULL;
}

gboolean
lrg_binding_catalog_add_action (LrgBindingCatalog   *self,
                                const gchar         *id,
                                const gchar         *label,
                                const gchar         *group,
                                const gchar         *owner,
                                const gchar * const *keys)
{
    Action *action;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), FALSE);
    g_return_val_if_fail (id != NULL && *id != '\0', FALSE);
    g_return_val_if_fail (group != NULL, FALSE);

    if (g_hash_table_contains (self->index, id))
        return FALSE;
    if (find_group (self, group) == NULL)
        lrg_binding_catalog_add_group (self, group, NULL);

    action = g_new0 (Action, 1);
    action->id = g_strdup (id);
    action->label = g_strdup (label != NULL ? label : id);
    action->group = g_strdup (group);
    action->owner = g_strdup (owner);
    action->keys = g_ptr_array_new_with_free_func (g_free);
    action_set_keys (action, keys);
    g_ptr_array_add (self->actions, action);
    g_hash_table_insert (self->index, action->id, action);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, NULL);
    return TRUE;
}

gboolean
lrg_binding_catalog_remove_action (LrgBindingCatalog *self,
                                   const gchar       *id)
{
    Action *action;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    action = g_hash_table_lookup (self->index, id);
    if (action == NULL)
        return FALSE;
    g_hash_table_remove (self->index, id);
    g_ptr_array_remove (self->actions, action);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, NULL);
    return TRUE;
}

guint
lrg_binding_catalog_remove_owner (LrgBindingCatalog *self,
                                  const gchar       *owner)
{
    guint removed;
    guint i;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), 0);
    g_return_val_if_fail (owner != NULL, 0);

    removed = 0;
    i = 0;
    while (i < self->actions->len)
    {
        Action *action = g_ptr_array_index (self->actions, i);

        if (g_strcmp0 (action->owner, owner) != 0)
        {
            i++;
            continue;
        }
        g_hash_table_remove (self->index, action->id);
        g_ptr_array_remove_index (self->actions, i);
        removed++;
    }
    if (removed > 0)
        g_signal_emit (self, signals[SIGNAL_CHANGED], 0, NULL);
    return removed;
}

gboolean
lrg_binding_catalog_has_action (LrgBindingCatalog *self,
                                const gchar       *id)
{
    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    return g_hash_table_contains (self->index, id);
}

GStrv
lrg_binding_catalog_get_actions (LrgBindingCatalog *self,
                                 const gchar       *group)
{
    GPtrArray *out;
    guint i;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);

    out = g_ptr_array_new ();
    for (i = 0; i < self->actions->len; i++)
    {
        Action *action = g_ptr_array_index (self->actions, i);

        if (group == NULL || strcmp (action->group, group) == 0)
            g_ptr_array_add (out, g_strdup (action->id));
    }
    g_ptr_array_add (out, NULL);
    return (GStrv)g_ptr_array_free (out, FALSE);
}

static Action *
lookup (LrgBindingCatalog *self,
        const gchar       *id)
{
    return id != NULL ? g_hash_table_lookup (self->index, id) : NULL;
}

const gchar *
lrg_binding_catalog_get_label (LrgBindingCatalog *self,
                               const gchar       *id)
{
    Action *action;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);
    action = lookup (self, id);
    return action != NULL ? action->label : NULL;
}

const gchar *
lrg_binding_catalog_get_group (LrgBindingCatalog *self,
                               const gchar       *id)
{
    Action *action;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);
    action = lookup (self, id);
    return action != NULL ? action->group : NULL;
}

const gchar *
lrg_binding_catalog_get_owner (LrgBindingCatalog *self,
                               const gchar       *id)
{
    Action *action;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);
    action = lookup (self, id);
    return action != NULL ? action->owner : NULL;
}

const gchar * const *
lrg_binding_catalog_get_keys (LrgBindingCatalog *self,
                              const gchar       *id)
{
    Action *action;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);
    action = lookup (self, id);
    return action != NULL ? (const gchar * const *)action->keys->pdata : NULL;
}

gboolean
lrg_binding_catalog_set_keys (LrgBindingCatalog   *self,
                              const gchar         *id,
                              const gchar * const *keys)
{
    Action *action;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), FALSE);
    action = lookup (self, id);
    if (action == NULL)
        return FALSE;
    action_set_keys (action, keys);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, action->id);
    return TRUE;
}

GStrv
lrg_binding_catalog_find_actions (LrgBindingCatalog *self,
                                  const gchar       *key,
                                  const gchar       *except)
{
    GPtrArray *out;
    guint i;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    out = g_ptr_array_new ();
    for (i = 0; i < self->actions->len; i++)
    {
        Action *action = g_ptr_array_index (self->actions, i);

        if (except != NULL && strcmp (action->id, except) == 0)
            continue;
        if (action_key_index (action, key) >= 0)
            g_ptr_array_add (out, g_strdup (action->id));
    }
    g_ptr_array_add (out, NULL);
    return (GStrv)g_ptr_array_free (out, FALSE);
}

gboolean
lrg_binding_catalog_bind (LrgBindingCatalog         *self,
                          const gchar               *id,
                          guint                      slot,
                          const gchar               *key,
                          LrgBindingConflictPolicy   policy,
                          GStrv                     *out_conflicts)
{
    g_auto(GStrv) conflicts = NULL;
    g_autofree gchar *old_key = NULL;
    g_autofree gchar *trimmed = NULL;
    Action *action;
    gint existing;
    guint i;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    if (out_conflicts != NULL)
        *out_conflicts = NULL;
    action = lookup (self, id);
    trimmed = g_strstrip (g_strdup (key));
    if (action == NULL || *trimmed == '\0')
        return FALSE;

    /* --- who else holds the key --- */
    conflicts = lrg_binding_catalog_find_actions (self, trimmed, id);
    if (out_conflicts != NULL)
        *out_conflicts = g_strdupv (conflicts);
    if (conflicts[0] != NULL && policy == LRG_BINDING_CONFLICT_REFUSE)
        return FALSE;

    /* --- this action: the key goes into the slot --- */
    existing = action_key_index (action, trimmed);
    if (existing >= 0 && (guint)existing != slot)
        action_remove_key (action, (guint)existing);
    if (existing >= 0 && (guint)existing == slot)
    {
        /* already there: only the others change */
    }
    else if (slot < action_key_count (action))
    {
        /* the slot's key moves to old_key (a swap hands it on) */
        old_key = g_ptr_array_index (action->keys, slot);
        g_ptr_array_index (action->keys, slot) = g_strdup (trimmed);
    }
    else if (action_key_count (action) < LRG_BINDING_CATALOG_MAX_KEYS)
    {
        g_ptr_array_insert (action->keys, (gint)action_key_count (action), g_strdup (trimmed));
    }
    else
    {
        /* full: the last key makes room */
        old_key = g_ptr_array_index (action->keys, action_key_count (action) - 1);
        g_ptr_array_index (action->keys, action_key_count (action) - 1) = g_strdup (trimmed);
    }

    /* --- the others --- */
    for (i = 0; policy != LRG_BINDING_CONFLICT_KEEP && conflicts[i] != NULL; i++)
    {
        Action *other;
        gint index;

        other = lookup (self, conflicts[i]);
        index = other != NULL ? action_key_index (other, trimmed) : -1;
        if (index < 0)
            continue;
        if (policy == LRG_BINDING_CONFLICT_SWAP && old_key != NULL &&
            action_key_index (other, old_key) < 0)
        {
            g_free (g_ptr_array_index (other->keys, index));
            g_ptr_array_index (other->keys, index) = g_strdup (old_key);
        }
        else
        {
            action_remove_key (other, (guint)index);
        }
        g_signal_emit (self, signals[SIGNAL_CHANGED], 0, other->id);
    }
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, action->id);
    return TRUE;
}

gboolean
lrg_binding_catalog_unbind (LrgBindingCatalog *self,
                            const gchar       *id,
                            const gchar       *key)
{
    Action *action;
    gint index;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), FALSE);
    action = lookup (self, id);
    if (action == NULL)
        return FALSE;
    if (key == NULL)
    {
        if (action_key_count (action) == 0)
            return FALSE;
        action_set_keys (action, NULL);
        g_signal_emit (self, signals[SIGNAL_CHANGED], 0, action->id);
        return TRUE;
    }
    index = action_key_index (action, key);
    if (index < 0)
        return FALSE;
    action_remove_key (action, (guint)index);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, action->id);
    return TRUE;
}

GStrv
lrg_binding_catalog_get_conflicted_keys (LrgBindingCatalog *self)
{
    g_autoptr(GHashTable) counts = NULL;
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    GPtrArray *out;
    guint i;

    g_return_val_if_fail (LRG_IS_BINDING_CATALOG (self), NULL);

    counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    for (i = 0; i < self->actions->len; i++)
    {
        Action *action = g_ptr_array_index (self->actions, i);
        guint k;

        for (k = 0; k < action_key_count (action); k++)
        {
            gchar *normal;
            guint n;

            normal = lrg_binding_catalog_normalize_key (g_ptr_array_index (action->keys, k));
            n = GPOINTER_TO_UINT (g_hash_table_lookup (counts, normal));
            g_hash_table_replace (counts, normal, GUINT_TO_POINTER (n + 1));
        }
    }
    out = g_ptr_array_new ();
    g_hash_table_iter_init (&iter, counts);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if (GPOINTER_TO_UINT (value) > 1)
            g_ptr_array_add (out, g_strdup (key));
    }
    g_ptr_array_sort_values (out, (GCompareFunc)g_strcmp0);
    g_ptr_array_add (out, NULL);
    return (GStrv)g_ptr_array_free (out, FALSE);
}
