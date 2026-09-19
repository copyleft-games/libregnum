/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-simulation.h"
#include "lrg-mmo-service-private.h"
#include <math.h>

typedef struct
{
    guint64 id;
    gchar *account;
    gchar *zone;
    gdouble p[3], radius, speed, range;
    guint health, faction, damage;
    guint64 moved, attacked, sequence, cooldown;
} Character;
typedef struct
{
    gchar *zone;
    gdouble min[3], max[3];
} Obstacle;
struct _LrgMmoSimulation
{
    GObject parent_instance;
    GHashTable *characters;
    GPtrArray *obstacles;
    guint capacity;
    guint64 tick;
};
G_DEFINE_TYPE (LrgMmoSimulation, lrg_mmo_simulation, G_TYPE_OBJECT)
static void
character_free (gpointer data)
{
    Character *c = data;
    g_free (c->account);
    g_free (c->zone);
    g_free (c);
}
static void
obstacle_free (gpointer data)
{
    Obstacle *o = data;
    g_free (o->zone);
    g_free (o);
}
static void
lrg_mmo_simulation_finalize (GObject *object)
{
    LrgMmoSimulation *self = LRG_MMO_SIMULATION (object);
    g_hash_table_unref (self->characters);
    g_ptr_array_unref (self->obstacles);
    G_OBJECT_CLASS (lrg_mmo_simulation_parent_class)->finalize (object);
}
static void
lrg_mmo_simulation_class_init (LrgMmoSimulationClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_simulation_finalize;
}
static void
lrg_mmo_simulation_init (LrgMmoSimulation *self)
{
    self->characters = g_hash_table_new_full (g_int64_hash, g_int64_equal, NULL, character_free);
    self->obstacles = g_ptr_array_new_with_free_func (obstacle_free);
}
LrgMmoSimulation *
lrg_mmo_simulation_new (guint capacity)
{
    LrgMmoSimulation *self;
    g_return_val_if_fail (capacity > 0, NULL);
    self = g_object_new (LRG_TYPE_MMO_SIMULATION, NULL);
    self->capacity = capacity;
    return self;
}
static gboolean
point_valid (const gdouble *p)
{
    guint i;
    for (i = 0; i < 3; i++)
        if (!isfinite (p[i]) || fabs (p[i]) > 1e9)
            return FALSE;
    return TRUE;
}
static gboolean
blocked (LrgMmoSimulation *self, const gchar *zone, const gdouble *from, const gdouble *to, gdouble radius)
{
    guint n;
    for (n = 0; n < self->obstacles->len; n++)
    {
        Obstacle *o = g_ptr_array_index (self->obstacles, n);
        gdouble near = 0, far = 1;
        guint i;
        if (!g_str_equal (zone, o->zone))
            continue;
        for (i = 0; i < 3; i++)
        {
            gdouble d = to[i] - from[i];
            gdouble low = o->min[i] - radius, high = o->max[i] + radius;
            if (d == 0)
            {
                if (from[i] < low || from[i] > high)
                    break;
            }
            else
            {
                gdouble a = (low - from[i]) / d, b = (high - from[i]) / d;
                near = MAX (near, MIN (a, b));
                far = MIN (far, MAX (a, b));
                if (near > far)
                    break;
            }
        }
        if (i == 3)
            return TRUE;
    }
    return FALSE;
}

gboolean
lrg_mmo_simulation_spawn (LrgMmoSimulation *self, guint64 id, const gchar *account,
                          const gchar *zone, gdouble x, gdouble y, gdouble z,
                          gdouble radius, guint health, guint faction, gdouble speed,
                          guint damage, gdouble range, guint cooldown, GError **error)
{
    Character *c;
    gdouble p[] = { x, y, z };
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), FALSE);
    if (id == 0 || !_lrg_mmo_id_valid (account) || !_lrg_mmo_id_valid (zone) || !point_valid (p) ||
        !isfinite (radius) || radius < 0 || radius > 1000 || health == 0 || health > 1000000000 ||
        !isfinite (speed) || speed <= 0 || speed > 1e6 || damage == 0 || damage > 1000000000 ||
        !isfinite (range) || range < 0 || range > 1000 || cooldown == 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid character configuration");
    if (g_hash_table_contains (self->characters, &id))
        return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Character already exists");
    if (g_hash_table_size (self->characters) >= self->capacity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Character capacity reached");
    if (blocked (self, zone, p, p, radius))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Spawn intersects solid content");
    c = g_new0 (Character, 1);
    c->id = id;
    c->account = g_strdup (account);
    c->zone = g_strdup (zone);
    memcpy (c->p, p, sizeof p);
    c->radius = radius;
    c->health = health;
    c->faction = faction;
    c->speed = speed;
    c->damage = damage;
    c->range = range;
    c->cooldown = cooldown;
    c->moved = self->tick;
    c->attacked = self->tick;
    g_hash_table_insert (self->characters, &c->id, c);
    return TRUE;
}

gboolean
lrg_mmo_simulation_add_obstacle (LrgMmoSimulation *self, const gchar *zone,
                                 gdouble min_x, gdouble min_y, gdouble min_z,
                                 gdouble max_x, gdouble max_y, gdouble max_z, GError **error)
{
    gdouble low[] = { min_x, min_y, min_z }, high[] = { max_x, max_y, max_z };
    Obstacle *o;
    GHashTableIter iter;
    gpointer value;
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), FALSE);
    if (!_lrg_mmo_id_valid (zone) || !point_valid (low) || !point_valid (high))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid obstacle");
    for (i = 0; i < 3; i++)
        if (low[i] > high[i])
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Inverted obstacle");
    if (self->obstacles->len >= 4096)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Obstacle capacity reached");
    o = g_new0 (Obstacle, 1);
    o->zone = g_strdup (zone);
    memcpy (o->min, low, sizeof low);
    memcpy (o->max, high, sizeof high);
    g_ptr_array_add (self->obstacles, o);
    g_hash_table_iter_init (&iter, self->characters);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Character *c = value;
        if (blocked (self, c->zone, c->p, c->p, c->radius))
        {
            g_ptr_array_remove_index (self->obstacles, self->obstacles->len - 1);
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Obstacle intersects character");
        }
    }
    return TRUE;
}

gboolean
lrg_mmo_simulation_advance (LrgMmoSimulation *self, guint ticks, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), FALSE);
    if (ticks == 0 || ticks > 20 || self->tick > G_MAXUINT64 - ticks)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid server tick advance");
    self->tick += ticks;
    return TRUE;
}
static Character *
authorize (LrgMmoSimulation *self, const gchar *account, guint64 id, guint64 sequence, GError **error)
{
    Character *c = g_hash_table_lookup (self->characters, &id);
    if (c == NULL || g_strcmp0 (c->account, account) != 0 || c->health == 0 ||
        sequence == 0 || sequence <= c->sequence)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Character command unauthorized or stale");
        return NULL;
    }
    return c;
}

gboolean
lrg_mmo_simulation_move (LrgMmoSimulation *self, const gchar *account, guint64 id,
                         guint64 sequence, gdouble x, gdouble y, gdouble z, GError **error)
{
    Character *c;
    gdouble p[] = { x, y, z };
    gdouble distance, budget;
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), FALSE);
    c = authorize (self, account, id, sequence, error);
    if (c == NULL)
        return FALSE;
    if (!point_valid (p))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid movement destination");
    distance = hypot (hypot (x - c->p[0], y - c->p[1]), z - c->p[2]);
    budget = MIN (self->tick - c->moved, 5) * 0.05 * c->speed;
    if (distance > budget || blocked (self, c->zone, c->p, p, c->radius))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Movement exceeds budget or intersects solid content");
    memcpy (c->p, p, sizeof p);
    c->moved = self->tick;
    c->sequence = sequence;
    return TRUE;
}

gboolean
lrg_mmo_simulation_attack (LrgMmoSimulation *self, const gchar *account,
                           guint64 id, guint64 sequence, guint64 target, GError **error)
{
    Character *c, *victim;
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), FALSE);
    c = authorize (self, account, id, sequence, error);
    if (c == NULL)
        return FALSE;
    victim = g_hash_table_lookup (self->characters, &target);
    if (victim == NULL || victim->health == 0 || victim->faction == c->faction ||
        !g_str_equal (victim->zone, c->zone) || self->tick - c->attacked < c->cooldown ||
        hypot (hypot (victim->p[0] - c->p[0], victim->p[1] - c->p[1]), victim->p[2] - c->p[2]) > c->range ||
        blocked (self, c->zone, c->p, victim->p, 0))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Target, range, sight line or cooldown invalid");
    victim->health -= MIN (victim->health, c->damage);
    c->attacked = self->tick;
    c->sequence = sequence;
    return TRUE;
}

GVariant *
lrg_mmo_simulation_lookup (LrgMmoSimulation *self, guint64 id)
{
    Character *c;
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), NULL);
    c = g_hash_table_lookup (self->characters, &id);
    return c == NULL ? NULL : g_variant_ref_sink (g_variant_new ("(dddut)",
                                     c->p[0], c->p[1], c->p[2], c->health, c->sequence));
}

GVariant *
lrg_mmo_simulation_snapshot (LrgMmoSimulation *self)
{
    GVariantBuilder builder;
    GHashTableIter iter;
    gpointer value;
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(tssdddduududtttt)"));
    g_hash_table_iter_init (&iter, self->characters);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Character *c = value;
        g_variant_builder_add (&builder, "(tssdddduududtttt)", c->id, c->account, c->zone,
             c->p[0], c->p[1], c->p[2], c->radius, c->health, c->faction, c->speed, c->damage,
             c->range, c->moved, c->attacked, c->sequence, c->cooldown);
    }
    return g_variant_ref_sink (g_variant_new ("(t@a(tssdddduududtttt))", self->tick,
                                              g_variant_builder_end (&builder)));
}

gboolean
lrg_mmo_simulation_restore (LrgMmoSimulation *self, GVariant *snapshot, GError **error)
{
    g_autoptr(LrgMmoSimulation) staged = NULL;
    g_autoptr(GVariant) rows = NULL;
    GVariantIter iter;
    const gchar *account, *zone;
    guint64 tick, id, moved, attacked, sequence, cooldown;
    guint health, faction, damage;
    gdouble x, y, z, radius, speed, range;
    g_return_val_if_fail (LRG_IS_MMO_SIMULATION (self), FALSE);
    if (snapshot == NULL || !g_variant_is_of_type (snapshot, G_VARIANT_TYPE ("(ta(tssdddduududtttt))")) ||
        g_variant_get_size (snapshot) > 1024 * 1024 || !g_variant_is_normal_form (snapshot))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid simulation snapshot");
    g_variant_get (snapshot, "(t@a(tssdddduududtttt))", &tick, &rows);
    if (g_variant_n_children (rows) > self->capacity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Snapshot exceeds character capacity");
    staged = lrg_mmo_simulation_new (self->capacity);
    staged->tick = tick;
    g_ptr_array_unref (staged->obstacles);
    staged->obstacles = g_ptr_array_ref (self->obstacles);
    g_variant_iter_init (&iter, rows);
    while (g_variant_iter_next (&iter, "(t&s&sdddduududtttt)", &id, &account, &zone,
                               &x, &y, &z, &radius, &health, &faction, &speed, &damage,
                               &range, &moved, &attacked, &sequence, &cooldown))
    {
        Character *c;
        if (moved > tick || attacked > tick || cooldown == 0 || cooldown > G_MAXUINT)
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Future snapshot clock");
        if (!lrg_mmo_simulation_spawn (staged, id, account, zone, x, y, z, radius,
                                       MAX (health, 1), faction, speed, damage, range, cooldown, error))
            return FALSE;
        c = g_hash_table_lookup (staged->characters, &id);
        c->health = health;
        c->moved = moved;
        c->attacked = attacked;
        c->sequence = sequence;
    }
    g_hash_table_unref (self->characters);
    self->characters = g_steal_pointer (&staged->characters);
    staged->characters = g_hash_table_new (g_int64_hash, g_int64_equal);
    self->tick = tick;
    return TRUE;
}
