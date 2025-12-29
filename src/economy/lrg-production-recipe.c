/* lrg-production-recipe.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-production-recipe.h"
#include "../lrg-log.h"

#include <math.h>

/* Input entry */
typedef struct
{
    LrgResource *resource;
    gdouble      amount;
} InputEntry;

/* Output entry with chance */
typedef struct
{
    LrgResource *resource;
    gdouble      amount;
    gdouble      chance;
} OutputEntry;

static void
input_entry_free (gpointer data)
{
    InputEntry *entry = (InputEntry *)data;
    g_clear_object (&entry->resource);
    g_free (entry);
}

static void
output_entry_free (gpointer data)
{
    OutputEntry *entry = (OutputEntry *)data;
    g_clear_object (&entry->resource);
    g_free (entry);
}

struct _LrgProductionRecipe
{
    GObject      parent_instance;

    gchar       *id;
    gchar       *name;
    gchar       *description;
    gdouble      production_time;
    gboolean     enabled;

    GHashTable  *inputs;   /* gchar* (id) -> InputEntry* */
    GHashTable  *outputs;  /* gchar* (id) -> OutputEntry* */
};

G_DEFINE_TYPE (LrgProductionRecipe, lrg_production_recipe, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_PRODUCTION_TIME,
    PROP_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_production_recipe_finalize (GObject *object)
{
    LrgProductionRecipe *self = LRG_PRODUCTION_RECIPE (object);

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->description, g_free);
    g_clear_pointer (&self->inputs, g_hash_table_unref);
    g_clear_pointer (&self->outputs, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_production_recipe_parent_class)->finalize (object);
}

static void
lrg_production_recipe_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgProductionRecipe *self = LRG_PRODUCTION_RECIPE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;
    case PROP_PRODUCTION_TIME:
        g_value_set_double (value, self->production_time);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, self->enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_production_recipe_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgProductionRecipe *self = LRG_PRODUCTION_RECIPE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        lrg_production_recipe_set_name (self, g_value_get_string (value));
        break;
    case PROP_DESCRIPTION:
        lrg_production_recipe_set_description (self, g_value_get_string (value));
        break;
    case PROP_PRODUCTION_TIME:
        lrg_production_recipe_set_production_time (self, g_value_get_double (value));
        break;
    case PROP_ENABLED:
        lrg_production_recipe_set_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_production_recipe_class_init (LrgProductionRecipeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_production_recipe_finalize;
    object_class->get_property = lrg_production_recipe_get_property;
    object_class->set_property = lrg_production_recipe_set_property;

    /**
     * LrgProductionRecipe:id:
     *
     * Unique identifier for this recipe.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgProductionRecipe:name:
     *
     * Display name for this recipe.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgProductionRecipe:description:
     *
     * Description text for this recipe.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Recipe description",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgProductionRecipe:production-time:
     *
     * Time required to complete this recipe in seconds.
     */
    properties[PROP_PRODUCTION_TIME] =
        g_param_spec_double ("production-time",
                             "Production Time",
                             "Time to complete in seconds",
                             0.0, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgProductionRecipe:enabled:
     *
     * Whether this recipe is currently enabled.
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether recipe is enabled",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_production_recipe_init (LrgProductionRecipe *self)
{
    self->production_time = 1.0;
    self->enabled = TRUE;

    self->inputs = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           input_entry_free);

    self->outputs = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            output_entry_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgProductionRecipe *
lrg_production_recipe_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_PRODUCTION_RECIPE,
                         "id", id,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

const gchar *
lrg_production_recipe_get_id (LrgProductionRecipe *self)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), NULL);
    return self->id;
}

const gchar *
lrg_production_recipe_get_name (LrgProductionRecipe *self)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), NULL);
    return self->name;
}

void
lrg_production_recipe_set_name (LrgProductionRecipe *self,
                                const gchar         *name)
{
    g_return_if_fail (LRG_IS_PRODUCTION_RECIPE (self));

    if (g_strcmp0 (self->name, name) != 0)
    {
        g_free (self->name);
        self->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

const gchar *
lrg_production_recipe_get_description (LrgProductionRecipe *self)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), NULL);
    return self->description;
}

void
lrg_production_recipe_set_description (LrgProductionRecipe *self,
                                       const gchar         *description)
{
    g_return_if_fail (LRG_IS_PRODUCTION_RECIPE (self));

    if (g_strcmp0 (self->description, description) != 0)
    {
        g_free (self->description);
        self->description = g_strdup (description);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
    }
}

gdouble
lrg_production_recipe_get_production_time (LrgProductionRecipe *self)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), 1.0);
    return self->production_time;
}

void
lrg_production_recipe_set_production_time (LrgProductionRecipe *self,
                                           gdouble              time)
{
    g_return_if_fail (LRG_IS_PRODUCTION_RECIPE (self));
    g_return_if_fail (time >= 0.0);

    if (self->production_time != time)
    {
        self->production_time = time;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRODUCTION_TIME]);
    }
}

gboolean
lrg_production_recipe_get_enabled (LrgProductionRecipe *self)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), FALSE);
    return self->enabled;
}

void
lrg_production_recipe_set_enabled (LrgProductionRecipe *self,
                                   gboolean             enabled)
{
    g_return_if_fail (LRG_IS_PRODUCTION_RECIPE (self));

    if (self->enabled != enabled)
    {
        self->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

/* ==========================================================================
 * Inputs
 * ========================================================================== */

void
lrg_production_recipe_add_input (LrgProductionRecipe *self,
                                 LrgResource         *resource,
                                 gdouble              amount)
{
    InputEntry *entry;
    const gchar *id;

    g_return_if_fail (LRG_IS_PRODUCTION_RECIPE (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (amount > 0.0);

    entry = g_new0 (InputEntry, 1);
    entry->resource = g_object_ref (resource);
    entry->amount = amount;

    id = lrg_resource_get_id (resource);
    g_hash_table_insert (self->inputs, g_strdup (id), entry);
}

gboolean
lrg_production_recipe_remove_input (LrgProductionRecipe *self,
                                    LrgResource         *resource)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    id = lrg_resource_get_id (resource);
    return g_hash_table_remove (self->inputs, id);
}

gdouble
lrg_production_recipe_get_input_amount (LrgProductionRecipe *self,
                                        LrgResource         *resource)
{
    InputEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->inputs, id);

    if (entry != NULL)
        return entry->amount;

    return 0.0;
}

GList *
lrg_production_recipe_get_inputs (LrgProductionRecipe *self)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), NULL);

    g_hash_table_iter_init (&iter, self->inputs);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        InputEntry *entry = (InputEntry *)value;
        result = g_list_prepend (result, entry->resource);
    }

    return result;
}

guint
lrg_production_recipe_get_input_count (LrgProductionRecipe *self)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), 0);
    return g_hash_table_size (self->inputs);
}

/* ==========================================================================
 * Outputs
 * ========================================================================== */

void
lrg_production_recipe_add_output (LrgProductionRecipe *self,
                                  LrgResource         *resource,
                                  gdouble              amount,
                                  gdouble              chance)
{
    OutputEntry *entry;
    const gchar *id;

    g_return_if_fail (LRG_IS_PRODUCTION_RECIPE (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (amount > 0.0);
    g_return_if_fail (chance >= 0.0 && chance <= 1.0);

    entry = g_new0 (OutputEntry, 1);
    entry->resource = g_object_ref (resource);
    entry->amount = amount;
    entry->chance = chance;

    id = lrg_resource_get_id (resource);
    g_hash_table_insert (self->outputs, g_strdup (id), entry);
}

gboolean
lrg_production_recipe_remove_output (LrgProductionRecipe *self,
                                     LrgResource         *resource)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    id = lrg_resource_get_id (resource);
    return g_hash_table_remove (self->outputs, id);
}

gdouble
lrg_production_recipe_get_output_amount (LrgProductionRecipe *self,
                                         LrgResource         *resource)
{
    OutputEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->outputs, id);

    if (entry != NULL)
        return entry->amount;

    return 0.0;
}

gdouble
lrg_production_recipe_get_output_chance (LrgProductionRecipe *self,
                                         LrgResource         *resource)
{
    OutputEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->outputs, id);

    if (entry != NULL)
        return entry->chance;

    return 0.0;
}

GList *
lrg_production_recipe_get_outputs (LrgProductionRecipe *self)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), NULL);

    g_hash_table_iter_init (&iter, self->outputs);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        OutputEntry *entry = (OutputEntry *)value;
        result = g_list_prepend (result, entry->resource);
    }

    return result;
}

guint
lrg_production_recipe_get_output_count (LrgProductionRecipe *self)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), 0);
    return g_hash_table_size (self->outputs);
}

/* ==========================================================================
 * Production
 * ========================================================================== */

gboolean
lrg_production_recipe_can_produce (LrgProductionRecipe *self,
                                   LrgResourcePool     *pool)
{
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (pool), FALSE);

    if (!self->enabled)
        return FALSE;

    /* Check all inputs */
    g_hash_table_iter_init (&iter, self->inputs);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        InputEntry *entry = (InputEntry *)value;
        if (!lrg_resource_pool_has (pool, entry->resource, entry->amount))
            return FALSE;
    }

    return TRUE;
}

guint
lrg_production_recipe_can_produce_count (LrgProductionRecipe *self,
                                         LrgResourcePool     *pool)
{
    GHashTableIter iter;
    gpointer value;
    guint min_count = G_MAXUINT;

    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), 0);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (pool), 0);

    if (!self->enabled)
        return 0;

    if (g_hash_table_size (self->inputs) == 0)
        return G_MAXUINT;  /* No inputs = unlimited production */

    /* Find limiting resource */
    g_hash_table_iter_init (&iter, self->inputs);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        InputEntry *entry = (InputEntry *)value;
        gdouble available;
        guint count;

        available = lrg_resource_pool_get (pool, entry->resource);
        count = (guint)floor (available / entry->amount);

        if (count < min_count)
            min_count = count;
    }

    return min_count;
}

static gboolean
produce_internal (LrgProductionRecipe *self,
                  LrgResourcePool     *source,
                  LrgResourcePool     *destination,
                  gboolean             guaranteed)
{
    GHashTableIter iter;
    gpointer value;

    /* Check if we can produce */
    if (!lrg_production_recipe_can_produce (self, source))
        return FALSE;

    /* Consume inputs */
    g_hash_table_iter_init (&iter, self->inputs);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        InputEntry *entry = (InputEntry *)value;
        lrg_resource_pool_remove (source, entry->resource, entry->amount);
    }

    /* Produce outputs */
    g_hash_table_iter_init (&iter, self->outputs);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        OutputEntry *entry = (OutputEntry *)value;
        gboolean should_produce;

        if (guaranteed || entry->chance >= 1.0)
        {
            should_produce = TRUE;
        }
        else
        {
            /* Roll for chance */
            gdouble roll = g_random_double ();
            should_produce = (roll <= entry->chance);
        }

        if (should_produce)
        {
            lrg_resource_pool_add (destination, entry->resource, entry->amount);
        }
    }

    return TRUE;
}

gboolean
lrg_production_recipe_produce (LrgProductionRecipe *self,
                               LrgResourcePool     *pool)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (pool), FALSE);

    return produce_internal (self, pool, pool, FALSE);
}

gboolean
lrg_production_recipe_produce_guaranteed (LrgProductionRecipe *self,
                                          LrgResourcePool     *pool)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (pool), FALSE);

    return produce_internal (self, pool, pool, TRUE);
}

gboolean
lrg_production_recipe_produce_to_pool (LrgProductionRecipe *self,
                                       LrgResourcePool     *source,
                                       LrgResourcePool     *destination)
{
    g_return_val_if_fail (LRG_IS_PRODUCTION_RECIPE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (source), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (destination), FALSE);

    return produce_internal (self, source, destination, FALSE);
}
