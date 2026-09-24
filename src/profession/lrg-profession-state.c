/* lrg-profession-state.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Per-character profession runtime state: known professions with skill and
 * trained cap, known recipes, skill-ups, crafting and gathering checks and
 * GVariant persistence.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "profession/lrg-profession-state.h"
#include "profession/lrg-profession-private.h"
#include "inventory/lrg-inventory.h"

#include <math.h>

/* Kind marker for professions whose definition has not been seen. */
#define KIND_UNRESOLVED (-1)

/* Default margin below the cap at which the next tier becomes trainable. */
#define DEFAULT_TRAIN_MARGIN (25)

/*
 * ProfessionEntry:
 * One known profession. @kind is an #LrgProfessionKind or KIND_UNRESOLVED.
 */
typedef struct
{
    guint skill;
    guint max_skill;
    gint  kind;
} ProfessionEntry;

struct _LrgProfessionState
{
    GObject     parent_instance;

    guint       max_primary;
    guint       train_margin;
    GHashTable *professions;  /* id (owned) -> ProfessionEntry (owned) */
    GHashTable *recipes;      /* recipe id (owned) -> profession id (owned) */
};

G_DEFINE_TYPE (LrgProfessionState, lrg_profession_state, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MAX_PRIMARY,
    PROP_TRAIN_MARGIN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject boilerplate
 * ========================================================================== */

static void
lrg_profession_state_finalize (GObject *object)
{
    LrgProfessionState *self = LRG_PROFESSION_STATE (object);

    g_clear_pointer (&self->professions, g_hash_table_unref);
    g_clear_pointer (&self->recipes, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_profession_state_parent_class)->finalize (object);
}

static void
lrg_profession_state_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgProfessionState *self = LRG_PROFESSION_STATE (object);

    switch (prop_id)
    {
    case PROP_MAX_PRIMARY:
        g_value_set_uint (value, self->max_primary);
        break;
    case PROP_TRAIN_MARGIN:
        g_value_set_uint (value, self->train_margin);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_profession_state_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgProfessionState *self = LRG_PROFESSION_STATE (object);
    guint v;

    switch (prop_id)
    {
    case PROP_MAX_PRIMARY:
        v = g_value_get_uint (value);
        if (self->max_primary != v)
        {
            self->max_primary = v;
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_TRAIN_MARGIN:
        v = g_value_get_uint (value);
        if (self->train_margin != v)
        {
            self->train_margin = v;
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_profession_state_class_init (LrgProfessionStateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_profession_state_finalize;
    object_class->get_property = lrg_profession_state_get_property;
    object_class->set_property = lrg_profession_state_set_property;

    /**
     * LrgProfessionState:max-primary:
     *
     * Number of primary professions a character may know at once.
     */
    properties[PROP_MAX_PRIMARY] =
        g_param_spec_uint ("max-primary", "Max Primary",
                           "Number of primary professions allowed",
                           0, LRG_PROFESSION_STATE_MAX_PROFESSIONS, 2,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                           G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
    /**
     * LrgProfessionState:train-margin:
     *
     * The next tier may be trained once skill >= current cap - margin.
     */
    properties[PROP_TRAIN_MARGIN] =
        g_param_spec_uint ("train-margin", "Train Margin",
                           "Skill margin below the cap for training the next tier",
                           0, LRG_PROFESSION_SKILL_LIMIT, DEFAULT_TRAIN_MARGIN,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_profession_state_init (LrgProfessionState *self)
{
    self->max_primary = 2;
    self->train_margin = DEFAULT_TRAIN_MARGIN;
    self->professions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    self->recipes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/* ==========================================================================
 * Internal helpers
 * ========================================================================== */

static ProfessionEntry *
lookup_entry (LrgProfessionState *self,
              const gchar        *profession_id)
{
    if (profession_id == NULL)
        return NULL;
    return g_hash_table_lookup (self->professions, profession_id);
}

static ProfessionEntry *
insert_entry (LrgProfessionState *self,
              const gchar        *profession_id,
              guint               skill,
              guint               max_skill,
              gint                kind)
{
    ProfessionEntry *entry;

    entry = g_new0 (ProfessionEntry, 1);
    entry->skill = skill;
    entry->max_skill = max_skill;
    entry->kind = kind;
    g_hash_table_replace (self->professions, g_strdup (profession_id), entry);
    return entry;
}

/*
 * count_primaries:
 * Counts primaries using @defs first, then the stored kind; unresolved kinds
 * count as primary so the limit fails closed.
 */
static guint
count_primaries (LrgProfessionState *self,
                 GHashTable         *defs)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    guint count = 0;

    g_hash_table_iter_init (&iter, self->professions);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        ProfessionEntry *entry = value;
        gint kind = entry->kind;
        gpointer def = NULL;

        if (defs != NULL)
            def = g_hash_table_lookup (defs, key);
        if (def != NULL && LRG_IS_PROFESSION_DEF (def))
            kind = (gint)lrg_profession_def_get_kind (LRG_PROFESSION_DEF (def));

        if (kind != (gint)LRG_PROFESSION_KIND_SECONDARY)
            count++;
    }
    return count;
}

/*
 * sorted_keys:
 * Collects the keys of @table (optionally filtered by value) into a new
 * container array sorted with g_strcmp0.
 */
static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

static GPtrArray *
sorted_keys (GHashTable  *table,
             const gchar *value_filter)
{
    GPtrArray *out;
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    out = g_ptr_array_sized_new (g_hash_table_size (table));
    g_hash_table_iter_init (&iter, table);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if (value_filter != NULL && g_strcmp0 ((const gchar *)value, value_filter) != 0)
            continue;
        g_ptr_array_add (out, key);
    }
    g_ptr_array_sort (out, compare_strings);
    return out;
}

/*
 * check_tool:
 * Requires at least one @tool via @count_func when both are set.
 */
static gboolean
check_tool (const gchar       *tool,
            LrgItemCountFunc   count_func,
            gpointer           user_data,
            GError           **error)
{
    if (tool == NULL || tool[0] == '\0' || count_func == NULL)
        return TRUE;
    if (count_func (tool, user_data) < 1)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Requires tool '%s'", tool);
        return FALSE;
    }
    return TRUE;
}

/* ==========================================================================
 * Construction and properties
 * ========================================================================== */

LrgProfessionState *
lrg_profession_state_new (guint max_primary)
{
    g_return_val_if_fail (max_primary <= LRG_PROFESSION_STATE_MAX_PROFESSIONS, NULL);

    return g_object_new (LRG_TYPE_PROFESSION_STATE, "max-primary", max_primary, NULL);
}

guint
lrg_profession_state_get_max_primary (LrgProfessionState *self)
{
    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), 0);
    return self->max_primary;
}

void
lrg_profession_state_set_max_primary (LrgProfessionState *self,
                                      guint               max_primary)
{
    g_return_if_fail (LRG_IS_PROFESSION_STATE (self));
    g_return_if_fail (max_primary <= LRG_PROFESSION_STATE_MAX_PROFESSIONS);
    g_object_set (self, "max-primary", max_primary, NULL);
}

guint
lrg_profession_state_get_train_margin (LrgProfessionState *self)
{
    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), 0);
    return self->train_margin;
}

void
lrg_profession_state_set_train_margin (LrgProfessionState *self,
                                       guint               margin)
{
    g_return_if_fail (LRG_IS_PROFESSION_STATE (self));
    g_return_if_fail (margin <= LRG_PROFESSION_SKILL_LIMIT);
    g_object_set (self, "train-margin", margin, NULL);
}

/* ==========================================================================
 * Professions
 * ========================================================================== */

gboolean
lrg_profession_state_learn (LrgProfessionState  *self,
                            LrgProfessionDef    *def,
                            guint                level,
                            GError             **error)
{
    const gchar *id;
    const LrgProfessionTier *first;
    LrgProfessionKind kind;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (def), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    id = lrg_profession_def_get_id (def);
    kind = lrg_profession_def_get_kind (def);

    /* 1. The definition itself must be usable */
    if (!_lrg_profession_id_valid (id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Invalid profession identifier");
        return FALSE;
    }
    first = lrg_profession_def_get_tier (def, 0);
    if (first == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Profession '%s' has no tiers", id);
        return FALSE;
    }

    /* 2. Already known */
    if (lookup_entry (self, id) != NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE,
                     "Profession '%s' is already known", id);
        return FALSE;
    }

    /* 3. Slot limits: primaries against max-primary, everything against the hard cap */
    if (kind == LRG_PROFESSION_KIND_PRIMARY &&
        count_primaries (self, NULL) >= self->max_primary)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Cannot learn more than %u primary professions", self->max_primary);
        return FALSE;
    }
    if (g_hash_table_size (self->professions) >= LRG_PROFESSION_STATE_MAX_PROFESSIONS)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Cannot know more than %u professions",
                     (guint)LRG_PROFESSION_STATE_MAX_PROFESSIONS);
        return FALSE;
    }

    /* 4. Character level for the apprentice tier */
    if (level < first->required_level)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Profession '%s' requires level %u", id, first->required_level);
        return FALSE;
    }

    insert_entry (self, id, 1, first->skill_cap, (gint)kind);
    return TRUE;
}

gboolean
lrg_profession_state_unlearn (LrgProfessionState *self,
                              const gchar        *profession_id)
{
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);

    if (profession_id == NULL || !g_hash_table_remove (self->professions, profession_id))
        return FALSE;

    /* Drop every recipe that belonged to the forgotten profession */
    g_hash_table_iter_init (&iter, self->recipes);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        if (g_strcmp0 ((const gchar *)value, profession_id) == 0)
            g_hash_table_iter_remove (&iter);
    }
    return TRUE;
}

gboolean
lrg_profession_state_knows (LrgProfessionState *self,
                            const gchar        *profession_id)
{
    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    return lookup_entry (self, profession_id) != NULL;
}

guint
lrg_profession_state_get_skill (LrgProfessionState *self,
                                const gchar        *profession_id)
{
    ProfessionEntry *entry;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), 0);
    entry = lookup_entry (self, profession_id);
    return entry != NULL ? entry->skill : 0;
}

guint
lrg_profession_state_get_max_skill (LrgProfessionState *self,
                                    const gchar        *profession_id)
{
    ProfessionEntry *entry;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), 0);
    entry = lookup_entry (self, profession_id);
    return entry != NULL ? entry->max_skill : 0;
}

guint
lrg_profession_state_get_primary_count (LrgProfessionState *self,
                                        GHashTable         *defs)
{
    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), 0);
    return count_primaries (self, defs);
}

GPtrArray *
lrg_profession_state_get_professions (LrgProfessionState *self)
{
    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), NULL);
    return sorted_keys (self->professions, NULL);
}

/* ==========================================================================
 * Training
 * ========================================================================== */

gboolean
lrg_profession_state_can_train (LrgProfessionState       *self,
                                LrgProfessionDef         *def,
                                guint                     level,
                                const LrgProfessionTier **out_tier,
                                GError                  **error)
{
    const gchar *id;
    ProfessionEntry *entry;
    const LrgProfessionTier *tier;
    gint next;
    guint threshold;

    if (out_tier != NULL)
        *out_tier = NULL;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (def), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    id = lrg_profession_def_get_id (def);

    /* 1. Must already know the profession */
    entry = lookup_entry (self, id);
    if (entry == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                     "Profession '%s' is not known", id != NULL ? id : "(null)");
        return FALSE;
    }

    /* 2. There must be a tier above the current cap */
    next = lrg_profession_def_get_next_tier (def, entry->max_skill);
    if (next < 0)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Profession '%s' is already at its highest tier", id);
        return FALSE;
    }
    tier = lrg_profession_def_get_tier (def, (guint)next);

    /* 3. Character level */
    if (level < tier->required_level)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Training %s requires level %u",
                     tier->name != NULL ? tier->name : "the next tier",
                     tier->required_level);
        return FALSE;
    }

    /* 4. Skill must be within the margin of the current cap (saturating) */
    threshold = entry->max_skill > self->train_margin
              ? entry->max_skill - self->train_margin : 0;
    if (entry->skill < threshold)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Training requires %s skill %u (have %u)", id, threshold, entry->skill);
        return FALSE;
    }

    if (out_tier != NULL)
        *out_tier = tier;
    return TRUE;
}

gboolean
lrg_profession_state_train (LrgProfessionState  *self,
                            LrgProfessionDef    *def,
                            guint                level,
                            GError             **error)
{
    const LrgProfessionTier *tier = NULL;
    ProfessionEntry *entry;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (def), FALSE);

    if (!lrg_profession_state_can_train (self, def, level, &tier, error))
        return FALSE;

    /* Only the cap moves; the character still has to level the skill */
    entry = lookup_entry (self, lrg_profession_def_get_id (def));
    if (entry == NULL || tier == NULL)
    {
        /* Unreachable: can_train succeeded, so both exist */
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_FAILED,
                     "Internal training inconsistency");
        return FALSE;
    }
    entry->max_skill = tier->skill_cap;

    /* Remember the kind if this entry was restored without definitions */
    if (entry->kind == KIND_UNRESOLVED)
        entry->kind = (gint)lrg_profession_def_get_kind (def);
    return TRUE;
}

/* ==========================================================================
 * Recipes
 * ========================================================================== */

gboolean
lrg_profession_state_learn_recipe (LrgProfessionState  *self,
                                   LrgRecipeDef        *recipe,
                                   GError             **error)
{
    const gchar *recipe_id;
    const gchar *profession_id;
    ProfessionEntry *entry;
    guint required;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (recipe), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    recipe_id = lrg_recipe_def_get_id (recipe);
    profession_id = lrg_recipe_def_get_profession_id (recipe);
    required = lrg_recipe_def_get_required_skill (recipe);

    /* 1. Identifiers */
    if (!_lrg_profession_id_valid (recipe_id) || !_lrg_profession_id_valid (profession_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Invalid recipe or profession identifier");
        return FALSE;
    }

    /* 2. Already known */
    if (g_hash_table_contains (self->recipes, recipe_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE,
                     "Recipe '%s' is already known", recipe_id);
        return FALSE;
    }

    /* 3. Profession and skill */
    entry = lookup_entry (self, profession_id);
    if (entry == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Recipe '%s' requires profession '%s'", recipe_id, profession_id);
        return FALSE;
    }
    if (entry->skill < required)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Recipe '%s' requires %s skill %u (have %u)",
                     recipe_id, profession_id, required, entry->skill);
        return FALSE;
    }

    /* 4. Capacity */
    if (g_hash_table_size (self->recipes) >= LRG_PROFESSION_STATE_MAX_RECIPES)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Cannot know more than %u recipes",
                     (guint)LRG_PROFESSION_STATE_MAX_RECIPES);
        return FALSE;
    }

    g_hash_table_replace (self->recipes, g_strdup (recipe_id), g_strdup (profession_id));
    return TRUE;
}

guint
lrg_profession_state_learn_starter_recipes (LrgProfessionState *self,
                                            GPtrArray          *recipes)
{
    guint learned = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), 0);
    g_return_val_if_fail (recipes != NULL, 0);

    for (i = 0; i < recipes->len; i++)
    {
        gpointer candidate = g_ptr_array_index (recipes, i);

        if (!LRG_IS_RECIPE_DEF (candidate))
            continue;
        if (lrg_recipe_def_get_source (LRG_RECIPE_DEF (candidate)) != LRG_RECIPE_SOURCE_STARTER)
            continue;
        /* Rejections (unknown profession, duplicate, skill) are expected here */
        if (lrg_profession_state_learn_recipe (self, LRG_RECIPE_DEF (candidate), NULL))
            learned++;
    }
    return learned;
}

gboolean
lrg_profession_state_knows_recipe (LrgProfessionState *self,
                                   const gchar        *recipe_id)
{
    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    if (recipe_id == NULL)
        return FALSE;
    return g_hash_table_contains (self->recipes, recipe_id);
}

GPtrArray *
lrg_profession_state_get_recipes (LrgProfessionState *self,
                                  const gchar        *profession_id)
{
    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), NULL);
    return sorted_keys (self->recipes, profession_id);
}

/* ==========================================================================
 * Skill-ups and admin
 * ========================================================================== */

guint
lrg_profession_state_try_skill_up (LrgProfessionState *self,
                                   const gchar        *profession_id,
                                   const LrgSkillBand *band,
                                   gdouble             roll)
{
    ProfessionEntry *entry;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), 0);
    g_return_val_if_fail (band != NULL, 0);

    /* Hostile or buggy rolls never grant anything */
    if (!isfinite (roll) || roll < 0.0 || roll >= 1.0)
        return 0;

    entry = lookup_entry (self, profession_id);
    if (entry == NULL || entry->skill >= entry->max_skill)
        return 0;

    /* Invalid bands report chance 0, so they fall through here */
    if (roll < lrg_skill_band_get_chance (band, entry->skill))
    {
        entry->skill++;
        return 1;
    }
    return 0;
}

void
lrg_profession_state_set_skill (LrgProfessionState *self,
                                const gchar        *profession_id,
                                guint               skill,
                                guint               max_skill)
{
    ProfessionEntry *entry;

    g_return_if_fail (LRG_IS_PROFESSION_STATE (self));
    g_return_if_fail (_lrg_profession_id_valid (profession_id));
    g_return_if_fail (max_skill >= 1 && max_skill <= LRG_PROFESSION_SKILL_LIMIT);

    entry = lookup_entry (self, profession_id);
    if (entry == NULL)
    {
        /* Refuse to grow past the hard cap even for admin writes */
        g_return_if_fail (g_hash_table_size (self->professions) <
                          LRG_PROFESSION_STATE_MAX_PROFESSIONS);
        entry = insert_entry (self, profession_id, 0, 0, KIND_UNRESOLVED);
    }
    entry->max_skill = max_skill;
    entry->skill = MIN (skill, max_skill);
}

/* ==========================================================================
 * Crafting and gathering checks
 * ========================================================================== */

gboolean
lrg_profession_state_can_craft (LrgProfessionState  *self,
                                LrgRecipeDef        *recipe,
                                guint                quantity,
                                LrgItemCountFunc     count_func,
                                gpointer             user_data,
                                GError             **error)
{
    const gchar *recipe_id;
    const gchar *profession_id;
    ProfessionEntry *entry;
    GPtrArray *reagents;
    guint required;
    guint i;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (recipe), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    recipe_id = lrg_recipe_def_get_id (recipe);
    profession_id = lrg_recipe_def_get_profession_id (recipe);
    required = lrg_recipe_def_get_required_skill (recipe);

    /* 1. Well-formed request */
    if (quantity == 0)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Craft quantity must be at least 1");
        return FALSE;
    }
    if (!_lrg_profession_id_valid (recipe_id) || !_lrg_profession_id_valid (profession_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Invalid recipe or profession identifier");
        return FALSE;
    }

    /* 2. Knowledge and skill */
    if (!g_hash_table_contains (self->recipes, recipe_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Recipe '%s' is not known", recipe_id);
        return FALSE;
    }
    entry = lookup_entry (self, profession_id);
    if (entry == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Recipe '%s' requires profession '%s'", recipe_id, profession_id);
        return FALSE;
    }
    if (entry->skill < required)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Recipe '%s' requires %s skill %u (have %u)",
                     recipe_id, profession_id, required, entry->skill);
        return FALSE;
    }

    /* 3. Items: nothing more to check without a counter */
    if (count_func == NULL)
        return TRUE;

    if (!check_tool (lrg_recipe_def_get_tool (recipe), count_func, user_data, error))
        return FALSE;

    /* 4. Every reagent count * quantity, in 64 bits so it cannot wrap */
    reagents = lrg_recipe_def_get_reagents (recipe);
    for (i = 0; i < reagents->len; i++)
    {
        const LrgRecipeItem *item = g_ptr_array_index (reagents, i);
        guint64 need = (guint64)item->count * (guint64)quantity;
        guint64 have = (guint64)count_func (item->item_id, user_data);

        if (have < need)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                         "Requires %" G_GUINT64_FORMAT " x %s (have %" G_GUINT64_FORMAT ")",
                         need, item->item_id, have);
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
lrg_profession_state_can_gather_with (LrgProfessionState  *self,
                                      LrgGatherNodeDef    *node,
                                      LrgItemCountFunc     count_func,
                                      gpointer             user_data,
                                      GError             **error)
{
    const gchar *profession_id;
    ProfessionEntry *entry;
    guint required;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (node), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    profession_id = lrg_gather_node_def_get_profession_id (node);
    required = lrg_gather_node_def_get_required_skill (node);

    if (!_lrg_profession_id_valid (profession_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Gather node has an invalid profession identifier");
        return FALSE;
    }

    entry = lookup_entry (self, profession_id);
    if (entry == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Requires profession '%s'", profession_id);
        return FALSE;
    }
    if (entry->skill < required)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Requires %s skill %u (have %u)", profession_id, required, entry->skill);
        return FALSE;
    }

    return check_tool (lrg_gather_node_def_get_required_tool (node),
                       count_func, user_data, error);
}

gboolean
lrg_profession_state_can_gather (LrgProfessionState  *self,
                                 LrgGatherNodeDef    *node,
                                 GError             **error)
{
    return lrg_profession_state_can_gather_with (self, node, NULL, NULL, error);
}

guint
lrg_profession_state_inventory_count (const gchar *item_id,
                                      gpointer     user_data)
{
    g_return_val_if_fail (LRG_IS_INVENTORY (user_data), 0);
    g_return_val_if_fail (item_id != NULL, 0);

    return lrg_inventory_count_item (LRG_INVENTORY (user_data), item_id);
}

/* ==========================================================================
 * Persistence
 * ========================================================================== */

GVariant *
lrg_profession_state_to_variant (LrgProfessionState *self)
{
    GVariantBuilder professions;
    GVariantBuilder recipes;
    g_autoptr(GPtrArray) ids = NULL;
    g_autoptr(GPtrArray) recipe_ids = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_PROFESSION_STATE (self), NULL);

    /* Professions sorted by id for deterministic output */
    ids = sorted_keys (self->professions, NULL);
    g_variant_builder_init (&professions, G_VARIANT_TYPE ("a(suu)"));
    for (i = 0; i < ids->len; i++)
    {
        const gchar *id = g_ptr_array_index (ids, i);
        ProfessionEntry *entry = g_hash_table_lookup (self->professions, id);

        g_variant_builder_add (&professions, "(suu)", id, entry->skill, entry->max_skill);
    }

    /* Recipes sorted by id */
    recipe_ids = sorted_keys (self->recipes, NULL);
    g_variant_builder_init (&recipes, G_VARIANT_TYPE ("as"));
    for (i = 0; i < recipe_ids->len; i++)
        g_variant_builder_add (&recipes, "s", (const gchar *)g_ptr_array_index (recipe_ids, i));

    return g_variant_ref_sink (g_variant_new ("(ua(suu)as)", self->max_primary,
                                              &professions, &recipes));
}

/*
 * restore_fail:
 * Sets @error with @code and a formatted message, then returns NULL.
 */
static LrgProfessionState *
restore_fail (GError     **error,
              gint         code,
              const gchar *format,
              ...) G_GNUC_PRINTF (3, 4);

static LrgProfessionState *
restore_fail (GError     **error,
              gint         code,
              const gchar *format,
              ...)
{
    va_list args;

    va_start (args, format);
    if (error != NULL)
        *error = g_error_new_valist (LRG_PROGRESSION_ERROR, code, format, args);
    va_end (args);
    return NULL;
}

LrgProfessionState *
lrg_profession_state_new_from_variant_full (GVariant    *variant,
                                            GHashTable  *professions,
                                            GHashTable  *recipes,
                                            GError     **error)
{
    g_autoptr(LrgProfessionState) self = NULL;
    g_autoptr(GVariant) prof_array = NULL;
    g_autoptr(GVariant) recipe_array = NULL;
    guint32 max_primary;
    gsize n_prof;
    gsize n_recipes;
    gsize i;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* 1. Shape: exact type and canonical encoding */
    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_PROFESSION_STATE_VARIANT_TYPE)))
        return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                             "Profession state has type '%s', expected '%s'",
                             g_variant_get_type_string (variant),
                             LRG_PROFESSION_STATE_VARIANT_TYPE);
    if (!g_variant_is_normal_form (variant))
        return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                             "Profession state is not in normal form");

    /* 2. Header and array bounds before touching any element */
    g_variant_get_child (variant, 0, "u", &max_primary);
    prof_array = g_variant_get_child_value (variant, 1);
    recipe_array = g_variant_get_child_value (variant, 2);
    n_prof = g_variant_n_children (prof_array);
    n_recipes = g_variant_n_children (recipe_array);

    if (max_primary > LRG_PROFESSION_STATE_MAX_PROFESSIONS)
        return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                             "max-primary %u exceeds %u", max_primary,
                             (guint)LRG_PROFESSION_STATE_MAX_PROFESSIONS);
    if (n_prof > LRG_PROFESSION_STATE_MAX_PROFESSIONS)
        return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                             "Too many professions (%" G_GSIZE_FORMAT ")", n_prof);
    if (n_recipes > LRG_PROFESSION_STATE_MAX_RECIPES)
        return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                             "Too many recipes (%" G_GSIZE_FORMAT ")", n_recipes);

    self = lrg_profession_state_new (max_primary);

    /* 3. Professions */
    for (i = 0; i < n_prof; i++)
    {
        const gchar *id;
        guint32 skill;
        guint32 max_skill;
        gint kind = KIND_UNRESOLVED;

        g_variant_get_child (prof_array, i, "(&suu)", &id, &skill, &max_skill);

        if (!_lrg_profession_id_valid (id))
            return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                 "Invalid profession identifier at index %" G_GSIZE_FORMAT, i);
        if (g_hash_table_contains (self->professions, id))
            return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                 "Duplicate profession '%s'", id);
        if (max_skill == 0 || max_skill > LRG_PROFESSION_SKILL_LIMIT)
            return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                 "Profession '%s' max skill %u out of range", id, max_skill);
        if (skill > max_skill)
            return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                 "Profession '%s' skill %u exceeds max %u", id, skill, max_skill);

        /* Optional definition checks */
        if (professions != NULL)
        {
            gpointer def = g_hash_table_lookup (professions, id);

            if (def == NULL || !LRG_IS_PROFESSION_DEF (def))
                return restore_fail (error, LRG_PROGRESSION_ERROR_NOT_FOUND,
                                     "Unknown profession '%s'", id);
            if (max_skill > lrg_profession_def_get_max_skill (LRG_PROFESSION_DEF (def)))
                return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                     "Profession '%s' max skill %u exceeds definition cap %u",
                                     id, max_skill,
                                     lrg_profession_def_get_max_skill (LRG_PROFESSION_DEF (def)));
            kind = (gint)lrg_profession_def_get_kind (LRG_PROFESSION_DEF (def));
        }

        insert_entry (self, id, skill, max_skill, kind);
    }

    /* With definitions the primary count is exact, so enforce the limit */
    if (professions != NULL && count_primaries (self, NULL) > max_primary)
        return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                             "Snapshot has %u primary professions but max-primary is %u",
                             count_primaries (self, NULL), max_primary);

    /* 4. Recipes: known definitions whose profession is in the snapshot */
    for (i = 0; i < n_recipes; i++)
    {
        const gchar *recipe_id;
        const gchar *profession_id;
        gpointer def = NULL;

        g_variant_get_child (recipe_array, i, "&s", &recipe_id);

        if (!_lrg_profession_id_valid (recipe_id))
            return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                 "Invalid recipe identifier at index %" G_GSIZE_FORMAT, i);
        if (g_hash_table_contains (self->recipes, recipe_id))
            return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                 "Duplicate recipe '%s'", recipe_id);

        if (recipes != NULL)
            def = g_hash_table_lookup (recipes, recipe_id);
        if (def == NULL || !LRG_IS_RECIPE_DEF (def))
            return restore_fail (error, LRG_PROGRESSION_ERROR_NOT_FOUND,
                                 "Unknown recipe '%s'", recipe_id);

        profession_id = lrg_recipe_def_get_profession_id (LRG_RECIPE_DEF (def));
        if (lookup_entry (self, profession_id) == NULL)
            return restore_fail (error, LRG_PROGRESSION_ERROR_INVALID,
                                 "Recipe '%s' belongs to profession '%s' which is not known",
                                 recipe_id, profession_id != NULL ? profession_id : "(null)");

        g_hash_table_replace (self->recipes, g_strdup (recipe_id), g_strdup (profession_id));
    }

    return g_steal_pointer (&self);
}

LrgProfessionState *
lrg_profession_state_new_from_variant (GVariant    *variant,
                                       GHashTable  *recipes,
                                       GError     **error)
{
    return lrg_profession_state_new_from_variant_full (variant, NULL, recipes, error);
}
