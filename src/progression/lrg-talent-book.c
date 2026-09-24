/* lrg-talent-book.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dual/multi specialisation: an ordered set of talent loadouts for one
 * class, a number of unlocked slots, the active slot and a respec counter.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-talent-book.h"

#include <string.h>

struct _LrgTalentBook
{
    GObject    parent_instance;

    gchar     *class_id;
    guint      capacity;
    guint      active;
    guint      respec_count;
    guint      first_level;
    guint      points_per_level;

    GPtrArray *loadouts; /* owns LrgTalentLoadout*, length == unlocked */
};

G_DEFINE_TYPE (LrgTalentBook, lrg_talent_book, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CLASS_ID,
    PROP_CAPACITY,
    PROP_UNLOCKED,
    PROP_ACTIVE_INDEX,
    PROP_RESPEC_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

/*
 * book_id_is_valid:
 *
 * Checks that @id is non-NULL, non-empty, at most LRG_TALENT_ID_MAX_LENGTH
 * bytes and valid UTF-8.
 */
static gboolean
book_id_is_valid (const gchar *id)
{
    gsize len;

    if (id == NULL)
        return FALSE;

    len = strlen (id);
    if (len == 0 || len > LRG_TALENT_ID_MAX_LENGTH)
        return FALSE;

    return g_utf8_validate (id, (gssize)len, NULL);
}

/*
 * book_new_loadout:
 *
 * Creates an empty loadout of the book's class with the book's point rules.
 */
static LrgTalentLoadout *
book_new_loadout (LrgTalentBook *self)
{
    LrgTalentLoadout *loadout;

    loadout = lrg_talent_loadout_new (self->class_id);
    lrg_talent_loadout_set_first_level (loadout, self->first_level);
    lrg_talent_loadout_set_points_per_level (loadout, self->points_per_level);
    return loadout;
}

/* ========================================================================= */
/* GObject                                                                   */
/* ========================================================================= */

static void
lrg_talent_book_constructed (GObject *object)
{
    LrgTalentBook *self = LRG_TALENT_BOOK (object);

    G_OBJECT_CLASS (lrg_talent_book_parent_class)->constructed (object);

    /* Every book starts with one unlocked, empty, active loadout */
    if (self->class_id == NULL)
        self->class_id = g_strdup ("");
    g_ptr_array_add (self->loadouts, book_new_loadout (self));
}

static void
lrg_talent_book_finalize (GObject *object)
{
    LrgTalentBook *self = LRG_TALENT_BOOK (object);

    g_clear_pointer (&self->loadouts, g_ptr_array_unref);
    g_free (self->class_id);

    G_OBJECT_CLASS (lrg_talent_book_parent_class)->finalize (object);
}

static void
lrg_talent_book_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgTalentBook *self = LRG_TALENT_BOOK (object);

    switch (prop_id)
    {
    case PROP_CLASS_ID:
        g_value_set_string (value, self->class_id);
        break;
    case PROP_CAPACITY:
        g_value_set_uint (value, self->capacity);
        break;
    case PROP_UNLOCKED:
        g_value_set_uint (value, self->loadouts->len);
        break;
    case PROP_ACTIVE_INDEX:
        g_value_set_uint (value, self->active);
        break;
    case PROP_RESPEC_COUNT:
        g_value_set_uint (value, self->respec_count);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_talent_book_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgTalentBook *self = LRG_TALENT_BOOK (object);

    switch (prop_id)
    {
    case PROP_CLASS_ID:
        g_free (self->class_id);
        self->class_id = g_value_dup_string (value);
        break;
    case PROP_CAPACITY:
        self->capacity = g_value_get_uint (value);
        break;
    case PROP_RESPEC_COUNT:
        lrg_talent_book_set_respec_count (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_talent_book_class_init (LrgTalentBookClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = lrg_talent_book_constructed;
    object_class->finalize = lrg_talent_book_finalize;
    object_class->get_property = lrg_talent_book_get_property;
    object_class->set_property = lrg_talent_book_set_property;

    /**
     * LrgTalentBook:class-id:
     *
     * Class of every loadout in the book.
     */
    properties[PROP_CLASS_ID] =
        g_param_spec_string ("class-id", "Class ID", "Owning class id", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentBook:capacity:
     *
     * Maximum number of loadouts.
     */
    properties[PROP_CAPACITY] =
        g_param_spec_uint ("capacity", "Capacity", "Maximum loadouts",
                           1, LRG_TALENT_BOOK_MAX_CAPACITY,
                           LRG_TALENT_BOOK_DEFAULT_CAPACITY,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentBook:unlocked:
     *
     * Number of unlocked loadouts.
     */
    properties[PROP_UNLOCKED] =
        g_param_spec_uint ("unlocked", "Unlocked", "Unlocked loadouts",
                           1, LRG_TALENT_BOOK_MAX_CAPACITY, 1,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentBook:active-index:
     *
     * Index of the active loadout.
     */
    properties[PROP_ACTIVE_INDEX] =
        g_param_spec_uint ("active-index", "Active Index", "Active loadout",
                           0, LRG_TALENT_BOOK_MAX_CAPACITY - 1, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentBook:respec-count:
     *
     * Number of recorded respecs.
     */
    properties[PROP_RESPEC_COUNT] =
        g_param_spec_uint ("respec-count", "Respec Count", "Recorded respecs",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_talent_book_init (LrgTalentBook *self)
{
    self->capacity = LRG_TALENT_BOOK_DEFAULT_CAPACITY;
    self->first_level = LRG_TALENT_LOADOUT_DEFAULT_FIRST_LEVEL;
    self->points_per_level = 1;
    self->loadouts = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

LrgTalentBook *
lrg_talent_book_new (const gchar *class_id,
                     guint        capacity)
{
    g_return_val_if_fail (class_id != NULL, NULL);
    g_return_val_if_fail (capacity >= 1 && capacity <= LRG_TALENT_BOOK_MAX_CAPACITY, NULL);

    return g_object_new (LRG_TYPE_TALENT_BOOK,
                         "class-id", class_id,
                         "capacity", capacity,
                         NULL);
}

const gchar *
lrg_talent_book_get_class_id (LrgTalentBook *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), NULL);
    return self->class_id;
}

guint
lrg_talent_book_get_capacity (LrgTalentBook *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), 0);
    return self->capacity;
}

guint
lrg_talent_book_get_unlocked (LrgTalentBook *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), 0);
    return self->loadouts->len;
}

gboolean
lrg_talent_book_unlock (LrgTalentBook  *self,
                        GError        **error)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->loadouts->len >= self->capacity)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "All %u talent loadouts are already unlocked", self->capacity);
        return FALSE;
    }

    g_ptr_array_add (self->loadouts, book_new_loadout (self));
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNLOCKED]);
    return TRUE;
}

guint
lrg_talent_book_get_active_index (LrgTalentBook *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), 0);
    return self->active;
}

gboolean
lrg_talent_book_set_active (LrgTalentBook  *self,
                            guint           index,
                            GError        **error)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (index >= self->capacity)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent loadout %u is outside capacity %u", index, self->capacity);
        return FALSE;
    }
    if (index >= self->loadouts->len)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Talent loadout %u is locked", index);
        return FALSE;
    }

    if (self->active != index)
    {
        self->active = index;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_INDEX]);
    }
    return TRUE;
}

LrgTalentLoadout *
lrg_talent_book_get_loadout (LrgTalentBook *self,
                             guint          index)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), NULL);

    if (index >= self->loadouts->len)
        return NULL;

    return g_ptr_array_index (self->loadouts, index);
}

LrgTalentLoadout *
lrg_talent_book_get_active (LrgTalentBook *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), NULL);
    return g_ptr_array_index (self->loadouts, self->active);
}

guint
lrg_talent_book_get_respec_count (LrgTalentBook *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), 0);
    return self->respec_count;
}

void
lrg_talent_book_record_respec (LrgTalentBook *self)
{
    g_return_if_fail (LRG_IS_TALENT_BOOK (self));

    if (self->respec_count < G_MAXUINT)
        lrg_talent_book_set_respec_count (self, self->respec_count + 1);
}

void
lrg_talent_book_set_respec_count (LrgTalentBook *self,
                                  guint          count)
{
    g_return_if_fail (LRG_IS_TALENT_BOOK (self));

    if (self->respec_count == count)
        return;

    self->respec_count = count;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESPEC_COUNT]);
}

void
lrg_talent_book_set_point_rules (LrgTalentBook *self,
                                 guint          first_level,
                                 guint          points_per_level)
{
    guint i;

    g_return_if_fail (LRG_IS_TALENT_BOOK (self));
    g_return_if_fail (first_level >= 1);
    g_return_if_fail (points_per_level <= 100);

    self->first_level = first_level;
    self->points_per_level = points_per_level;

    for (i = 0; i < self->loadouts->len; i++)
    {
        LrgTalentLoadout *loadout = g_ptr_array_index (self->loadouts, i);

        lrg_talent_loadout_set_first_level (loadout, first_level);
        lrg_talent_loadout_set_points_per_level (loadout, points_per_level);
    }
}

gboolean
lrg_talent_book_validate (LrgTalentBook  *self,
                          GHashTable     *trees,
                          guint           level,
                          GError        **error)
{
    guint i;

    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), FALSE);
    g_return_val_if_fail (trees != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    for (i = 0; i < self->loadouts->len; i++)
    {
        if (!lrg_talent_loadout_validate (g_ptr_array_index (self->loadouts, i),
                                          trees, level, error))
        {
            g_prefix_error (error, "Talent loadout %u: ", i);
            return FALSE;
        }
    }

    return TRUE;
}

GVariant *
lrg_talent_book_to_variant (LrgTalentBook *self)
{
    GVariantBuilder loadouts;
    GVariant *variant;
    guint i;

    g_return_val_if_fail (LRG_IS_TALENT_BOOK (self), NULL);

    g_variant_builder_init (&loadouts, G_VARIANT_TYPE ("av"));
    for (i = 0; i < self->loadouts->len; i++)
    {
        g_autoptr(GVariant) inner = NULL;

        inner = lrg_talent_loadout_to_variant (g_ptr_array_index (self->loadouts, i));
        g_variant_builder_add (&loadouts, "v", inner);
    }

    variant = g_variant_new ("(suuu@av)",
                             self->class_id,
                             (guint32)self->loadouts->len,
                             (guint32)self->active,
                             (guint32)self->respec_count,
                             g_variant_builder_end (&loadouts));
    return g_variant_ref_sink (variant);
}

LrgTalentBook *
lrg_talent_book_new_from_variant (GVariant  *variant,
                                  GError   **error)
{
    guint32 unlocked;
    guint capacity;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Derive the capacity from the snapshot; the full variant validates */
    capacity = LRG_TALENT_BOOK_DEFAULT_CAPACITY;
    if (g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_TALENT_BOOK_VARIANT_TYPE)))
    {
        g_autoptr(GVariant) child = g_variant_get_child_value (variant, 1);

        unlocked = g_variant_get_uint32 (child);
        if (unlocked > capacity && unlocked <= LRG_TALENT_BOOK_MAX_CAPACITY)
            capacity = unlocked;
    }

    return lrg_talent_book_new_from_variant_full (variant, capacity, error);
}

LrgTalentBook *
lrg_talent_book_new_from_variant_full (GVariant  *variant,
                                       guint      capacity,
                                       GError   **error)
{
    g_autoptr(LrgTalentBook) self = NULL;
    g_autoptr(GVariant) loadouts = NULL;
    const gchar *class_id;
    guint32 unlocked;
    guint32 active;
    guint32 respec_count;
    gsize n_loadouts;
    gsize i;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (capacity >= 1 && capacity <= LRG_TALENT_BOOK_MAX_CAPACITY, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Shape checks */
    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_TALENT_BOOK_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent book variant has type '%s', expected '%s'",
                     g_variant_get_type_string (variant),
                     LRG_TALENT_BOOK_VARIANT_TYPE);
        return NULL;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "Talent book variant is not in normal form");
        return NULL;
    }

    g_variant_get (variant, "(&suuu@av)", &class_id, &unlocked, &active,
                   &respec_count, &loadouts);

    /* Header fields */
    if (!book_id_is_valid (class_id))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "Talent book has an invalid class id");
        return NULL;
    }
    if (unlocked < 1 || unlocked > capacity)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent book unlocked count %u outside 1..%u", unlocked, capacity);
        return NULL;
    }
    if (active >= unlocked)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent book active index %u not below unlocked %u",
                     active, unlocked);
        return NULL;
    }
    n_loadouts = g_variant_n_children (loadouts);
    if (n_loadouts != unlocked)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent book has %" G_GSIZE_FORMAT " loadouts but %u unlocked",
                     n_loadouts, unlocked);
        return NULL;
    }

    self = lrg_talent_book_new (class_id, capacity);
    g_ptr_array_set_size (self->loadouts, 0);

    /* Each loadout is validated by the loadout parser and must match the class */
    for (i = 0; i < n_loadouts; i++)
    {
        g_autoptr(GVariant) boxed = g_variant_get_child_value (loadouts, i);
        g_autoptr(GVariant) inner = g_variant_get_variant (boxed);
        LrgTalentLoadout *loadout;

        loadout = lrg_talent_loadout_new_from_variant (inner, error);
        if (loadout == NULL)
        {
            g_prefix_error (error, "Talent book loadout %" G_GSIZE_FORMAT ": ", i);
            return NULL;
        }
        g_ptr_array_add (self->loadouts, loadout);

        if (g_strcmp0 (lrg_talent_loadout_get_class_id (loadout), class_id) != 0)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Talent book loadout %" G_GSIZE_FORMAT
                         " has class '%s', expected '%s'",
                         i, lrg_talent_loadout_get_class_id (loadout), class_id);
            return NULL;
        }
    }

    self->active = active;
    self->respec_count = respec_count;

    return g_steal_pointer (&self);
}
