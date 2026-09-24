/* lrg-aura-set.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ordered collection of auras with stacking, periodic ticks, expiry,
 * dispels and validated persistence.
 *
 * Reentrancy: signal handlers may apply or remove auras. Auras detached
 * while any emission is running are parked in a graveyard and freed only
 * when the outermost emission finishes, so the pointer a handler received
 * (and the snapshot lrg_aura_set_tick() iterates) never dangles.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-aura-set.h"

#include <math.h>

struct _LrgAuraSet
{
    GObject    parent_instance;

    guint      capacity;
    GPtrArray *auras;       /* LrgAura*, application order, owned */
    GPtrArray *graveyard;   /* LrgAura* detached during emission, owned */
    guint      emit_depth;
};

G_DEFINE_TYPE (LrgAuraSet, lrg_aura_set, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CAPACITY,
    N_PROPS
};

enum
{
    SIGNAL_AURA_TICKED,
    SIGNAL_AURA_EXPIRED,
    SIGNAL_AURA_REMOVED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void
aura_free_wrapper (gpointer data)
{
    lrg_aura_free ((LrgAura *) data);
}

/* --------------------------------------------------------------------------
 * Emission bookkeeping
 * -------------------------------------------------------------------------- */

static void
emit_begin (LrgAuraSet *self)
{
    self->emit_depth++;
}

static void
emit_end (LrgAuraSet *self)
{
    self->emit_depth--;
    if (self->emit_depth == 0 && self->graveyard->len > 0)
        g_ptr_array_set_size (self->graveyard, 0);
}

/* Frees a detached aura now, or later when an emission is running. */
static void
release (LrgAuraSet *self,
         LrgAura    *aura)
{
    if (self->emit_depth > 0)
        g_ptr_array_add (self->graveyard, aura);
    else
        lrg_aura_free (aura);
}

static gboolean
is_live (LrgAuraSet *self,
         LrgAura    *aura)
{
    return g_ptr_array_find (self->auras, aura, NULL);
}

/* Takes an aura out of the array without freeing it. */
static void
detach (LrgAuraSet *self,
        LrgAura    *aura)
{
    guint index;

    if (g_ptr_array_find (self->auras, aura, &index))
        g_ptr_array_steal_index (self->auras, index);
}

/* Detaches, emits aura-removed, then releases one aura. */
static void
remove_with_signal (LrgAuraSet *self,
                    LrgAura    *aura)
{
    detach (self, aura);
    emit_begin (self);
    g_signal_emit (self, signals[SIGNAL_AURA_REMOVED], 0, aura);
    release (self, aura);
    emit_end (self);
}

static gint
find_index (LrgAuraSet  *self,
            const gchar *id,
            guint        source)
{
    guint i;

    for (i = 0; i < self->auras->len; i++)
    {
        LrgAura *aura = g_ptr_array_index (self->auras, i);

        if (aura->source == source && g_strcmp0 (aura->id, id) == 0)
            return (gint) i;
    }
    return -1;
}

/* --------------------------------------------------------------------------
 * GObject
 * -------------------------------------------------------------------------- */

static void
lrg_aura_set_finalize (GObject *object)
{
    LrgAuraSet *self = LRG_AURA_SET (object);

    g_clear_pointer (&self->auras, g_ptr_array_unref);
    g_clear_pointer (&self->graveyard, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_aura_set_parent_class)->finalize (object);
}

static void
lrg_aura_set_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgAuraSet *self = LRG_AURA_SET (object);

    switch (prop_id)
    {
    case PROP_CAPACITY:
        g_value_set_uint (value, self->capacity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_aura_set_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgAuraSet *self = LRG_AURA_SET (object);

    switch (prop_id)
    {
    case PROP_CAPACITY:
        lrg_aura_set_set_capacity (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_aura_set_class_init (LrgAuraSetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_aura_set_finalize;
    object_class->get_property = lrg_aura_set_get_property;
    object_class->set_property = lrg_aura_set_set_property;

    /**
     * LrgAuraSet:capacity:
     *
     * Most simultaneous auras.
     */
    properties[PROP_CAPACITY] =
        g_param_spec_uint ("capacity", NULL, "Most simultaneous auras",
                           1, LRG_AURA_SET_MAX_CAPACITY, LRG_AURA_SET_DEFAULT_CAPACITY,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                           G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAuraSet::aura-ticked:
     * @self: the set
     * @aura: the aura whose period elapsed
     *
     * Emitted once per elapsed period of a periodic aura.
     */
    signals[SIGNAL_AURA_TICKED] =
        g_signal_new ("aura-ticked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1,
                      LRG_TYPE_AURA | G_SIGNAL_TYPE_STATIC_SCOPE);

    /**
     * LrgAuraSet::aura-expired:
     * @self: the set
     * @aura: the aura that ran out; still in the set during emission
     *
     * Emitted before a timed aura is removed because its duration ended.
     */
    signals[SIGNAL_AURA_EXPIRED] =
        g_signal_new ("aura-expired", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1,
                      LRG_TYPE_AURA | G_SIGNAL_TYPE_STATIC_SCOPE);

    /**
     * LrgAuraSet::aura-removed:
     * @self: the set
     * @aura: the aura, already detached from the set
     *
     * Emitted for explicit removal, dispel and clear (not for expiry).
     */
    signals[SIGNAL_AURA_REMOVED] =
        g_signal_new ("aura-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1,
                      LRG_TYPE_AURA | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
lrg_aura_set_init (LrgAuraSet *self)
{
    self->capacity = LRG_AURA_SET_DEFAULT_CAPACITY;
    self->auras = g_ptr_array_new_with_free_func (aura_free_wrapper);
    self->graveyard = g_ptr_array_new_with_free_func (aura_free_wrapper);
    self->emit_depth = 0;
}

LrgAuraSet *
lrg_aura_set_new (guint capacity)
{
    g_return_val_if_fail (capacity >= 1 && capacity <= LRG_AURA_SET_MAX_CAPACITY, NULL);

    return g_object_new (LRG_TYPE_AURA_SET, "capacity", capacity, NULL);
}

guint
lrg_aura_set_get_capacity (LrgAuraSet *self)
{
    g_return_val_if_fail (LRG_IS_AURA_SET (self), 0);

    return self->capacity;
}

void
lrg_aura_set_set_capacity (LrgAuraSet *self,
                           guint       capacity)
{
    g_return_if_fail (LRG_IS_AURA_SET (self));
    g_return_if_fail (capacity >= 1 && capacity <= LRG_AURA_SET_MAX_CAPACITY);
    g_return_if_fail (capacity >= self->auras->len);

    if (self->capacity == capacity)
        return;
    self->capacity = capacity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAPACITY]);
}

/* --------------------------------------------------------------------------
 * Apply / remove
 * -------------------------------------------------------------------------- */

LrgAuraApplyResult
lrg_aura_set_apply (LrgAuraSet    *self,
                    const LrgAura *aura)
{
    g_autoptr(LrgAura) incoming = NULL;
    LrgAura *existing;
    gint index;
    guint stacks;

    g_return_val_if_fail (LRG_IS_AURA_SET (self), LRG_AURA_APPLY_RESULT_REJECTED);
    g_return_val_if_fail (aura != NULL, LRG_AURA_APPLY_RESULT_REJECTED);

    /* normalise timers and validate before any change */
    incoming = lrg_aura_copy (aura);
    incoming->remaining = incoming->duration;
    incoming->tick_left = incoming->period;
    if (!lrg_aura_is_valid (incoming))
        return LRG_AURA_APPLY_RESULT_REJECTED;

    index = find_index (self, incoming->id, incoming->source);
    if (index < 0)
    {
        if (self->auras->len >= self->capacity)
            return LRG_AURA_APPLY_RESULT_REJECTED;
        g_ptr_array_add (self->auras, g_steal_pointer (&incoming));
        return LRG_AURA_APPLY_RESULT_ADDED;
    }

    /* refresh or stack an existing aura in place */
    existing = g_ptr_array_index (self->auras, (guint) index);
    stacks = MIN (existing->stacks + 1, incoming->max_stacks);

    existing->kind = incoming->kind;
    g_free (existing->dispel);
    existing->dispel = g_steal_pointer (&incoming->dispel);
    g_free (existing->stat);
    existing->stat = g_steal_pointer (&incoming->stat);
    existing->magnitude = incoming->magnitude;
    existing->duration = incoming->duration;
    existing->remaining = incoming->duration;
    existing->max_stacks = incoming->max_stacks;
    if (incoming->period <= 0.0)
        existing->tick_left = 0.0;
    else if (existing->period <= 0.0 || existing->tick_left > incoming->period ||
             !(existing->tick_left > 0.0))
        existing->tick_left = incoming->period;
    existing->period = incoming->period;

    if (stacks > existing->stacks)
    {
        existing->stacks = stacks;
        return LRG_AURA_APPLY_RESULT_STACKED;
    }
    existing->stacks = stacks;
    return LRG_AURA_APPLY_RESULT_REFRESHED;
}

gboolean
lrg_aura_set_remove (LrgAuraSet  *self,
                     const gchar *id,
                     guint        source)
{
    gint index;

    g_return_val_if_fail (LRG_IS_AURA_SET (self), FALSE);

    if (id == NULL)
        return FALSE;
    index = find_index (self, id, source);
    if (index < 0)
        return FALSE;
    remove_with_signal (self, g_ptr_array_index (self->auras, (guint) index));
    return TRUE;
}

/*
 * remove_matching:
 *
 * Collects matching auras first (oldest first, up to @max_count) and then
 * removes each one that is still live, so handlers cannot upset iteration.
 */
static guint
remove_matching (LrgAuraSet *self,
                 gboolean  (*match) (const LrgAura *aura, gconstpointer data),
                 gconstpointer data,
                 guint       max_count)
{
    g_autoptr(GPtrArray) victims = NULL;
    guint removed;
    guint i;

    victims = g_ptr_array_new ();
    for (i = 0; i < self->auras->len && victims->len < max_count; i++)
    {
        LrgAura *aura = g_ptr_array_index (self->auras, i);

        if (match (aura, data))
            g_ptr_array_add (victims, aura);
    }

    removed = 0;
    emit_begin (self);
    for (i = 0; i < victims->len; i++)
    {
        LrgAura *aura = g_ptr_array_index (victims, i);

        if (!is_live (self, aura))
            continue;
        remove_with_signal (self, aura);
        removed++;
    }
    emit_end (self);
    return removed;
}

static gboolean
match_id (const LrgAura *aura,
          gconstpointer  data)
{
    return g_strcmp0 (aura->id, data) == 0;
}

guint
lrg_aura_set_remove_all_by_id (LrgAuraSet  *self,
                               const gchar *id)
{
    g_return_val_if_fail (LRG_IS_AURA_SET (self), 0);

    if (id == NULL)
        return 0;
    return remove_matching (self, match_id, id, G_MAXUINT);
}

typedef struct
{
    LrgAuraKind  kind;
    const gchar *dispel;
} DispelMatch;

static gboolean
match_dispel (const LrgAura *aura,
              gconstpointer  data)
{
    const DispelMatch *m = data;

    if (aura->kind != m->kind || aura->dispel == NULL)
        return FALSE;
    return m->dispel == NULL || g_strcmp0 (aura->dispel, m->dispel) == 0;
}

guint
lrg_aura_set_dispel (LrgAuraSet  *self,
                     LrgAuraKind  kind,
                     const gchar *dispel,
                     guint        max_count)
{
    DispelMatch m;

    g_return_val_if_fail (LRG_IS_AURA_SET (self), 0);

    if (max_count == 0)
        return 0;
    m.kind = kind;
    m.dispel = dispel;
    return remove_matching (self, match_dispel, &m, max_count);
}

/* --------------------------------------------------------------------------
 * Queries
 * -------------------------------------------------------------------------- */

LrgAura *
lrg_aura_set_get (LrgAuraSet  *self,
                  const gchar *id,
                  guint        source)
{
    gint index;

    g_return_val_if_fail (LRG_IS_AURA_SET (self), NULL);

    if (id == NULL)
        return NULL;
    index = find_index (self, id, source);
    return index < 0 ? NULL : g_ptr_array_index (self->auras, (guint) index);
}

gboolean
lrg_aura_set_has (LrgAuraSet  *self,
                  const gchar *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_AURA_SET (self), FALSE);

    if (id == NULL)
        return FALSE;
    for (i = 0; i < self->auras->len; i++)
    {
        LrgAura *aura = g_ptr_array_index (self->auras, i);

        if (g_strcmp0 (aura->id, id) == 0)
            return TRUE;
    }
    return FALSE;
}

GPtrArray *
lrg_aura_set_get_all (LrgAuraSet *self)
{
    g_return_val_if_fail (LRG_IS_AURA_SET (self), NULL);

    return self->auras;
}

guint
lrg_aura_set_get_count (LrgAuraSet *self)
{
    g_return_val_if_fail (LRG_IS_AURA_SET (self), 0);

    return self->auras->len;
}

gdouble
lrg_aura_set_sum_stat (LrgAuraSet  *self,
                       const gchar *stat)
{
    gdouble sum;
    guint i;

    g_return_val_if_fail (LRG_IS_AURA_SET (self), 0.0);

    if (stat == NULL)
        return 0.0;
    sum = 0.0;
    for (i = 0; i < self->auras->len; i++)
    {
        LrgAura *aura = g_ptr_array_index (self->auras, i);

        if (g_strcmp0 (aura->stat, stat) == 0)
            sum += lrg_aura_get_total (aura);
    }
    return sum;
}

/* --------------------------------------------------------------------------
 * Time
 * -------------------------------------------------------------------------- */

void
lrg_aura_set_tick (LrgAuraSet *self,
                   gdouble     delta)
{
    g_autoptr(GPtrArray) snapshot = NULL;
    guint i;

    g_return_if_fail (LRG_IS_AURA_SET (self));

    if (!isfinite (delta) || !(delta > 0.0))
        return;

    /* iterate a snapshot: handlers may add or remove auras */
    snapshot = g_ptr_array_sized_new (self->auras->len);
    for (i = 0; i < self->auras->len; i++)
        g_ptr_array_add (snapshot, g_ptr_array_index (self->auras, i));

    emit_begin (self);
    for (i = 0; i < snapshot->len; i++)
    {
        LrgAura *aura = g_ptr_array_index (snapshot, i);
        gdouble effective;

        if (!is_live (self, aura))
            continue;

        /* periodic ticks only happen within the aura's remaining lifetime */
        effective = (aura->duration > 0.0) ? MIN (delta, aura->remaining) : delta;
        if (aura->period > 0.0)
        {
            guint n;
            guint k;

            aura->tick_left -= effective;
            n = 0;
            while (aura->tick_left <= 0.0 && n < LRG_AURA_SET_MAX_TICKS_PER_CALL)
            {
                aura->tick_left += aura->period;
                n++;
            }
            /* excess periods beyond the bound are dropped */
            if (aura->tick_left <= 0.0)
                aura->tick_left = aura->period;

            for (k = 0; k < n && is_live (self, aura); k++)
                g_signal_emit (self, signals[SIGNAL_AURA_TICKED], 0, aura);
        }

        if (!is_live (self, aura) || aura->duration <= 0.0)
            continue;

        /* expiry: signal while still in the set, then drop silently */
        aura->remaining -= delta;
        if (aura->remaining <= 0.0)
        {
            aura->remaining = 0.0;
            g_signal_emit (self, signals[SIGNAL_AURA_EXPIRED], 0, aura);
            if (is_live (self, aura))
            {
                detach (self, aura);
                release (self, aura);
            }
        }
    }
    emit_end (self);
}

void
lrg_aura_set_clear (LrgAuraSet *self)
{
    g_autoptr(GPtrArray) old = NULL;
    guint i;

    g_return_if_fail (LRG_IS_AURA_SET (self));

    /* swap in an empty array, then announce each removal in order */
    old = self->auras;
    self->auras = g_ptr_array_new_with_free_func (aura_free_wrapper);

    emit_begin (self);
    for (i = 0; i < old->len; i++)
        g_signal_emit (self, signals[SIGNAL_AURA_REMOVED], 0, g_ptr_array_index (old, i));
    /* the old array's free func releases everything; park it while nested */
    while (old->len > 0)
        release (self, g_ptr_array_steal_index (old, 0));
    emit_end (self);
}

/* --------------------------------------------------------------------------
 * Persistence
 * -------------------------------------------------------------------------- */

GVariant *
lrg_aura_set_to_variant (LrgAuraSet *self)
{
    GVariantBuilder builder;
    guint i;

    g_return_val_if_fail (LRG_IS_AURA_SET (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suussddddduu)"));
    for (i = 0; i < self->auras->len; i++)
    {
        LrgAura *a = g_ptr_array_index (self->auras, i);

        g_variant_builder_add (&builder, "(suussddddduu)",
                               a->id, a->source, (guint32) a->kind,
                               a->dispel != NULL ? a->dispel : "",
                               a->stat != NULL ? a->stat : "",
                               a->magnitude, a->duration, a->remaining,
                               a->period, a->tick_left,
                               a->stacks, a->max_stacks);
    }
    return g_variant_ref_sink (g_variant_new ("(ua(suussddddduu))", self->capacity, &builder));
}

LrgAuraSet *
lrg_aura_set_new_from_variant (GVariant  *variant,
                               GError   **error)
{
    g_autoptr(LrgAuraSet) set = NULL;
    g_autoptr(GVariant) auras = NULL;
    guint32 capacity;
    gsize n;
    gsize i;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_AURA_SET_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "aura variant has type '%s', expected '%s'",
                     g_variant_get_type_string (variant), LRG_AURA_SET_VARIANT_TYPE);
        return NULL;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "aura variant is not in normal form");
        return NULL;
    }

    g_variant_get_child (variant, 0, "u", &capacity);
    auras = g_variant_get_child_value (variant, 1);
    n = g_variant_n_children (auras);

    if (capacity < 1 || capacity > LRG_AURA_SET_MAX_CAPACITY)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "aura variant capacity %u is outside 1..%u",
                     capacity, LRG_AURA_SET_MAX_CAPACITY);
        return NULL;
    }
    if (n > capacity)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "aura variant holds %" G_GSIZE_FORMAT " auras, capacity %u",
                     n, capacity);
        return NULL;
    }

    set = lrg_aura_set_new (capacity);
    for (i = 0; i < n; i++)
    {
        g_autoptr(LrgAura) aura = NULL;
        const gchar *id;
        const gchar *dispel;
        const gchar *stat;
        guint32 source;
        guint32 kind;
        guint32 stacks;
        guint32 max_stacks;
        gdouble magnitude;
        gdouble duration;
        gdouble remaining;
        gdouble period;
        gdouble tick_left;

        g_variant_get_child (auras, i, "(&suu&s&sddddduu)",
                             &id, &source, &kind, &dispel, &stat,
                             &magnitude, &duration, &remaining, &period, &tick_left,
                             &stacks, &max_stacks);

        /* the enum range is checked before the value is stored as a kind */
        if (kind != LRG_AURA_KIND_BUFF && kind != LRG_AURA_KIND_DEBUFF)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "aura variant entry %" G_GSIZE_FORMAT " has unknown kind %u", i, kind);
            return NULL;
        }

        aura = g_new0 (LrgAura, 1);
        aura->id = g_strdup (id);
        aura->source = source;
        aura->kind = (LrgAuraKind) kind;
        aura->dispel = (*dispel != '\0') ? g_strdup (dispel) : NULL;
        aura->stat = (*stat != '\0') ? g_strdup (stat) : NULL;
        aura->magnitude = magnitude;
        aura->duration = duration;
        aura->remaining = remaining;
        aura->period = period;
        aura->tick_left = tick_left;
        aura->stacks = stacks;
        aura->max_stacks = max_stacks;

        if (!lrg_aura_is_valid (aura))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "aura variant entry %" G_GSIZE_FORMAT " is out of range", i);
            return NULL;
        }
        if (find_index (set, aura->id, aura->source) >= 0)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "aura variant repeats aura '%s' from source %u", aura->id, aura->source);
            return NULL;
        }
        g_ptr_array_add (set->auras, g_steal_pointer (&aura));
    }

    return g_steal_pointer (&set);
}
