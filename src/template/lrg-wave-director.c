/* lrg-wave-director.c - Data-driven enemy wave/spawn sequencing
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-wave-director.h"
#include "../lrg-log.h"

#include <math.h>
#include <string.h>
#include <yaml-glib.h>

/**
 * SECTION:lrg-wave-director
 * @title: LrgWaveDirector
 * @short_description: Data-driven enemy wave/spawn sequencing
 *
 * #LrgWaveDirector sequences enemy waves without owning any entity
 * types. Waves are described as data (programmatically or via YAML)
 * and the director emits "spawn" signals with string enemy ids; the
 * game connects to the signal and instantiates whatever the id means
 * to it.
 *
 * The director is driven from the game loop via
 * lrg_wave_director_update(), which receives the frame delta plus two
 * externally-supplied values: a progress scalar (for
 * %LRG_WAVE_TRIGGER_PROGRESS triggers, e.g. shmup scroll position) and
 * the current live enemy count (for %LRG_WAVE_TRIGGER_CLEARED
 * triggers).
 *
 * ## Thread Safety
 *
 * LrgWaveDirector is NOT thread-safe. Drive it from the game thread.
 *
 * Since: 1.0
 */

/* ==========================================================================
 * Internal Data
 * ========================================================================== */

typedef struct
{
    gchar          *enemy_id;
    guint           count;
    LrgWavePattern  pattern;
    gfloat          x;
    gfloat          y;
    gfloat          radius;
    gfloat          interval;

    /* Runtime state */
    guint           spawned;
    gfloat          timer;
} WaveEntry;

typedef struct
{
    LrgWaveTriggerType  trigger;
    gfloat              trigger_value;
    GArray             *entries;    /* of WaveEntry */
} Wave;

struct _LrgWaveDirector
{
    GObject parent_instance;

    GPtrArray *waves;       /* of Wave*, owned */

    /* Bounds rectangle for edge patterns */
    gfloat bounds_min_x;
    gfloat bounds_min_y;
    gfloat bounds_max_x;
    gfloat bounds_max_y;

    /* Runtime state */
    gboolean started;
    gboolean finished;
    gboolean wave_active;   /* current wave's trigger fired, spawning */
    guint    current_wave;
    gfloat   wave_elapsed;  /* seconds since current wave became current */
};

enum
{
    PROP_0,
    PROP_WAVE_COUNT,
    PROP_CURRENT_WAVE,
    PROP_STARTED,
    PROP_FINISHED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_SPAWN,
    SIGNAL_WAVE_STARTED,
    SIGNAL_WAVE_COMPLETED,
    SIGNAL_FINISHED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgWaveDirector, lrg_wave_director, G_TYPE_OBJECT)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/*
 * Clear func for WaveEntry elements stored in a wave's entries array.
 */
static void
wave_entry_clear (gpointer data)
{
    WaveEntry *entry = data;

    g_clear_pointer (&entry->enemy_id, g_free);
}

/*
 * Allocates a new empty wave.
 */
static Wave *
wave_new (LrgWaveTriggerType trigger,
          gfloat             trigger_value)
{
    Wave *wave;

    wave = g_new0 (Wave, 1);
    wave->trigger = trigger;
    wave->trigger_value = trigger_value;
    wave->entries = g_array_new (FALSE, TRUE, sizeof (WaveEntry));
    g_array_set_clear_func (wave->entries, wave_entry_clear);

    return wave;
}

/*
 * Free func for Wave elements stored in the director's waves array.
 */
static void
wave_free (gpointer data)
{
    Wave *wave = data;

    g_clear_pointer (&wave->entries, g_array_unref);
    g_free (wave);
}

/*
 * Resets the runtime spawn state of every entry in a wave.
 */
static void
wave_reset_entries (Wave *wave)
{
    guint i;

    for (i = 0; i < wave->entries->len; i++)
    {
        WaveEntry *entry = &g_array_index (wave->entries, WaveEntry, i);

        entry->spawned = 0;
        entry->timer = 0.0f;
    }
}

/*
 * Checks whether every entry of a wave has dispatched all its spawns.
 */
static gboolean
wave_is_spawned_out (const Wave *wave)
{
    guint i;

    for (i = 0; i < wave->entries->len; i++)
    {
        const WaveEntry *entry = &g_array_index (wave->entries, WaveEntry, i);

        if (entry->spawned < entry->count)
            return FALSE;
    }

    return TRUE;
}

/*
 * Evaluates whether the current wave's trigger condition has fired.
 */
static gboolean
wave_trigger_fired (LrgWaveDirector *self,
                    const Wave      *wave,
                    gfloat           progress,
                    guint            active_count)
{
    switch (wave->trigger)
    {
    case LRG_WAVE_TRIGGER_TIME:
        return self->wave_elapsed >= wave->trigger_value;

    case LRG_WAVE_TRIGGER_PROGRESS:
        return progress >= wave->trigger_value;

    case LRG_WAVE_TRIGGER_CLEARED:
        return active_count == 0;

    default:
        g_warn_if_reached ();
        return FALSE;
    }
}

/*
 * Computes the spawn position for one spawn of an entry.
 * spawn_index is the zero-based index of this spawn within the entry
 * (used by the line pattern for even spacing).
 */
static void
wave_entry_spawn_position (LrgWaveDirector *self,
                           const WaveEntry *entry,
                           guint            spawn_index,
                           gfloat          *out_x,
                           gfloat          *out_y)
{
    LrgWavePattern pattern;

    pattern = entry->pattern;

    /* Resolve the random-edge pattern to a concrete edge first */
    if (pattern == LRG_WAVE_PATTERN_EDGE_RANDOM)
    {
        switch (g_random_int_range (0, 4))
        {
        case 0:
            pattern = LRG_WAVE_PATTERN_EDGE_TOP;
            break;
        case 1:
            pattern = LRG_WAVE_PATTERN_EDGE_BOTTOM;
            break;
        case 2:
            pattern = LRG_WAVE_PATTERN_EDGE_LEFT;
            break;
        default:
            pattern = LRG_WAVE_PATTERN_EDGE_RIGHT;
            break;
        }
    }

    switch (pattern)
    {
    case LRG_WAVE_PATTERN_POINT:
        *out_x = entry->x;
        *out_y = entry->y;
        break;

    case LRG_WAVE_PATTERN_EDGE_TOP:
        *out_x = (gfloat) g_random_double_range (self->bounds_min_x,
                                                 self->bounds_max_x);
        *out_y = self->bounds_min_y;
        break;

    case LRG_WAVE_PATTERN_EDGE_BOTTOM:
        *out_x = (gfloat) g_random_double_range (self->bounds_min_x,
                                                 self->bounds_max_x);
        *out_y = self->bounds_max_y;
        break;

    case LRG_WAVE_PATTERN_EDGE_LEFT:
        *out_x = self->bounds_min_x;
        *out_y = (gfloat) g_random_double_range (self->bounds_min_y,
                                                 self->bounds_max_y);
        break;

    case LRG_WAVE_PATTERN_EDGE_RIGHT:
        *out_x = self->bounds_max_x;
        *out_y = (gfloat) g_random_double_range (self->bounds_min_y,
                                                 self->bounds_max_y);
        break;

    case LRG_WAVE_PATTERN_RING:
        {
            gdouble angle;

            angle = g_random_double_range (0.0, 2.0 * G_PI);
            *out_x = entry->x + (gfloat) (cos (angle) * entry->radius);
            *out_y = entry->y + (gfloat) (sin (angle) * entry->radius);
        }
        break;

    case LRG_WAVE_PATTERN_LINE:
        {
            gfloat offset;

            /* Evenly spaced, centered on x: spacing = radius param */
            offset = ((gfloat) spawn_index -
                      ((gfloat) entry->count - 1.0f) / 2.0f) * entry->radius;
            *out_x = entry->x + offset;
            *out_y = entry->y;
        }
        break;

    case LRG_WAVE_PATTERN_EDGE_RANDOM:
    default:
        g_warn_if_reached ();
        *out_x = entry->x;
        *out_y = entry->y;
        break;
    }
}

/*
 * Dispatches one spawn of an entry and emits the "spawn" signal.
 */
static void
wave_entry_spawn_one (LrgWaveDirector *self,
                      WaveEntry       *entry)
{
    gfloat sx;
    gfloat sy;

    sx = 0.0f;
    sy = 0.0f;
    wave_entry_spawn_position (self, entry, entry->spawned, &sx, &sy);

    entry->spawned++;

    g_signal_emit (self, signals[SIGNAL_SPAWN], 0,
                   entry->enemy_id, sx, sy, self->current_wave);
}

/*
 * Activates the current wave: emits "wave-started" and dispatches the
 * immediate spawns (all spawns for interval 0 entries, the first spawn
 * for interval > 0 entries).
 */
static void
wave_activate (LrgWaveDirector *self,
               Wave            *wave)
{
    guint i;

    wave_reset_entries (wave);
    self->wave_active = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Wave %u started (%u entries)",
               self->current_wave, wave->entries->len);

    g_signal_emit (self, signals[SIGNAL_WAVE_STARTED], 0, self->current_wave);

    for (i = 0; i < wave->entries->len; i++)
    {
        WaveEntry *entry = &g_array_index (wave->entries, WaveEntry, i);

        if (entry->count == 0)
            continue;

        if (entry->interval <= 0.0f)
        {
            /* All at once */
            while (entry->spawned < entry->count)
                wave_entry_spawn_one (self, entry);
        }
        else
        {
            /* First spawn is immediate; the rest follow the interval */
            wave_entry_spawn_one (self, entry);
        }
    }
}

/*
 * Advances interval-based spawning of the active wave by delta seconds.
 */
static void
wave_update_spawning (LrgWaveDirector *self,
                      Wave            *wave,
                      gfloat           delta)
{
    guint i;

    for (i = 0; i < wave->entries->len; i++)
    {
        WaveEntry *entry = &g_array_index (wave->entries, WaveEntry, i);

        if (entry->spawned >= entry->count)
            continue;

        entry->timer += delta;

        while (entry->spawned < entry->count &&
               entry->timer >= entry->interval)
        {
            entry->timer -= entry->interval;
            wave_entry_spawn_one (self, entry);
        }
    }
}

/*
 * Completes the current wave: emits "wave-completed" and either moves
 * to the next wave or finishes the director.
 */
static void
wave_advance (LrgWaveDirector *self)
{
    guint index;

    index = self->current_wave;

    self->wave_active = FALSE;
    self->wave_elapsed = 0.0f;

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Wave %u completed", index);

    g_signal_emit (self, signals[SIGNAL_WAVE_COMPLETED], 0, index);

    if (index + 1 >= self->waves->len)
    {
        self->finished = TRUE;
        g_signal_emit (self, signals[SIGNAL_FINISHED], 0);
    }
    else
    {
        self->current_wave = index + 1;
    }
}

/* ==========================================================================
 * YAML Parsing Helpers
 * ========================================================================== */

/*
 * Parses a trigger type string ("time", "progress", "cleared").
 * Returns FALSE on an unknown type.
 */
static gboolean
parse_trigger_type (const gchar        *str,
                    LrgWaveTriggerType *out_trigger)
{
    if (g_strcmp0 (str, "time") == 0)
        *out_trigger = LRG_WAVE_TRIGGER_TIME;
    else if (g_strcmp0 (str, "progress") == 0)
        *out_trigger = LRG_WAVE_TRIGGER_PROGRESS;
    else if (g_strcmp0 (str, "cleared") == 0)
        *out_trigger = LRG_WAVE_TRIGGER_CLEARED;
    else
        return FALSE;

    return TRUE;
}

/*
 * Parses a spawn pattern string. Returns FALSE on an unknown pattern.
 */
static gboolean
parse_pattern (const gchar    *str,
               LrgWavePattern *out_pattern)
{
    if (g_strcmp0 (str, "point") == 0)
        *out_pattern = LRG_WAVE_PATTERN_POINT;
    else if (g_strcmp0 (str, "edge-top") == 0)
        *out_pattern = LRG_WAVE_PATTERN_EDGE_TOP;
    else if (g_strcmp0 (str, "edge-bottom") == 0)
        *out_pattern = LRG_WAVE_PATTERN_EDGE_BOTTOM;
    else if (g_strcmp0 (str, "edge-left") == 0)
        *out_pattern = LRG_WAVE_PATTERN_EDGE_LEFT;
    else if (g_strcmp0 (str, "edge-right") == 0)
        *out_pattern = LRG_WAVE_PATTERN_EDGE_RIGHT;
    else if (g_strcmp0 (str, "edge-random") == 0)
        *out_pattern = LRG_WAVE_PATTERN_EDGE_RANDOM;
    else if (g_strcmp0 (str, "ring") == 0)
        *out_pattern = LRG_WAVE_PATTERN_RING;
    else if (g_strcmp0 (str, "line") == 0)
        *out_pattern = LRG_WAVE_PATTERN_LINE;
    else
        return FALSE;

    return TRUE;
}

/*
 * Reads an optional float member from a mapping, with a default.
 */
static gfloat
mapping_get_float_or (YamlMapping *mapping,
                      const gchar *member,
                      gfloat       fallback)
{
    if (!yaml_mapping_has_member (mapping, member))
        return fallback;

    return (gfloat) yaml_mapping_get_double_member (mapping, member);
}

/*
 * Parses one wave mapping into a new Wave. Returns NULL and sets error
 * on malformed input.
 */
static Wave *
parse_wave (YamlMapping  *wave_map,
            guint         wave_number,
            GError      **error)
{
    YamlMapping *trigger_map;
    YamlSequence *entries_seq;
    const gchar *type_str;
    LrgWaveTriggerType trigger;
    gfloat trigger_value;
    Wave *wave;

    trigger_map = yaml_mapping_get_mapping_member (wave_map, "trigger");
    if (trigger_map == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Wave %u missing 'trigger' mapping", wave_number);
        return NULL;
    }

    type_str = yaml_mapping_get_string_member (trigger_map, "type");
    if (type_str == NULL || !parse_trigger_type (type_str, &trigger))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Wave %u has invalid trigger type '%s'",
                     wave_number, type_str != NULL ? type_str : "(none)");
        return NULL;
    }

    trigger_value = mapping_get_float_or (trigger_map, "value", 0.0f);

    wave = wave_new (trigger, trigger_value);

    entries_seq = yaml_mapping_get_sequence_member (wave_map, "entries");
    if (entries_seq != NULL)
    {
        guint i;
        guint n_entries;

        n_entries = yaml_sequence_get_length (entries_seq);
        for (i = 0; i < n_entries; i++)
        {
            YamlMapping *entry_map;
            const gchar *enemy_id;
            const gchar *pattern_str;
            LrgWavePattern pattern;
            WaveEntry entry;

            entry_map = yaml_sequence_get_mapping_element (entries_seq, i);
            if (entry_map == NULL)
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Wave %u entry %u is not a mapping",
                             wave_number, i);
                wave_free (wave);
                return NULL;
            }

            enemy_id = yaml_mapping_get_string_member (entry_map, "enemy");
            if (enemy_id == NULL)
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Wave %u entry %u missing 'enemy' field",
                             wave_number, i);
                wave_free (wave);
                return NULL;
            }

            pattern = LRG_WAVE_PATTERN_POINT;
            pattern_str = yaml_mapping_get_string_member (entry_map, "pattern");
            if (pattern_str != NULL && !parse_pattern (pattern_str, &pattern))
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Wave %u entry %u has unknown pattern '%s'",
                             wave_number, i, pattern_str);
                wave_free (wave);
                return NULL;
            }

            memset (&entry, 0, sizeof (entry));
            entry.enemy_id = g_strdup (enemy_id);
            entry.count = 1;
            if (yaml_mapping_has_member (entry_map, "count"))
                entry.count = (guint) yaml_mapping_get_int_member (entry_map, "count");
            entry.pattern = pattern;
            entry.x = mapping_get_float_or (entry_map, "x", 0.0f);
            entry.y = mapping_get_float_or (entry_map, "y", 0.0f);
            entry.radius = mapping_get_float_or (entry_map, "radius", 0.0f);
            entry.interval = mapping_get_float_or (entry_map, "interval", 0.0f);

            g_array_append_val (wave->entries, entry);
        }
    }

    return wave;
}

/*
 * Parses the root document of a wave definition and appends the parsed
 * waves to the director. On failure nothing is appended.
 */
static gboolean
load_from_parser (LrgWaveDirector  *self,
                  YamlParser       *parser,
                  GError          **error)
{
    YamlNode *root;
    YamlMapping *mapping;
    YamlSequence *waves_seq;
    g_autoptr(GPtrArray) parsed = NULL;
    guint i;
    guint n_waves;

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Empty wave definition document");
        return FALSE;
    }

    mapping = yaml_node_get_mapping (root);
    if (mapping == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Wave definition root must be a mapping");
        return FALSE;
    }

    waves_seq = yaml_mapping_get_sequence_member (mapping, "waves");
    if (waves_seq == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Wave definition missing 'waves' sequence");
        return FALSE;
    }

    /* Parse into a temporary list so failure appends nothing */
    parsed = g_ptr_array_new_with_free_func (wave_free);

    n_waves = yaml_sequence_get_length (waves_seq);
    for (i = 0; i < n_waves; i++)
    {
        YamlMapping *wave_map;
        Wave *wave;

        wave_map = yaml_sequence_get_mapping_element (waves_seq, i);
        if (wave_map == NULL)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Wave %u is not a mapping", i);
            return FALSE;
        }

        wave = parse_wave (wave_map, i, error);
        if (wave == NULL)
            return FALSE;

        g_ptr_array_add (parsed, wave);
    }

    /* Transfer parsed waves into the director */
    for (i = 0; i < parsed->len; i++)
        g_ptr_array_add (self->waves, g_ptr_array_index (parsed, i));

    g_ptr_array_set_free_func (parsed, NULL);

    return TRUE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_wave_director_finalize (GObject *object)
{
    LrgWaveDirector *self = LRG_WAVE_DIRECTOR (object);

    g_clear_pointer (&self->waves, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_wave_director_parent_class)->finalize (object);
}

static void
lrg_wave_director_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgWaveDirector *self = LRG_WAVE_DIRECTOR (object);

    switch (prop_id)
    {
    case PROP_WAVE_COUNT:
        g_value_set_uint (value, self->waves->len);
        break;

    case PROP_CURRENT_WAVE:
        g_value_set_uint (value, self->current_wave);
        break;

    case PROP_STARTED:
        g_value_set_boolean (value, self->started);
        break;

    case PROP_FINISHED:
        g_value_set_boolean (value, self->finished);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_wave_director_class_init (LrgWaveDirectorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_wave_director_finalize;
    object_class->get_property = lrg_wave_director_get_property;

    /**
     * LrgWaveDirector:wave-count:
     *
     * Number of waves configured on the director.
     *
     * Since: 1.0
     */
    properties[PROP_WAVE_COUNT] =
        g_param_spec_uint ("wave-count",
                           "Wave Count",
                           "Number of configured waves",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveDirector:current-wave:
     *
     * Index of the current wave.
     *
     * Since: 1.0
     */
    properties[PROP_CURRENT_WAVE] =
        g_param_spec_uint ("current-wave",
                           "Current Wave",
                           "Index of the current wave",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveDirector:started:
     *
     * Whether the director has been started.
     *
     * Since: 1.0
     */
    properties[PROP_STARTED] =
        g_param_spec_boolean ("started",
                              "Started",
                              "Whether the director has been started",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveDirector:finished:
     *
     * Whether all spawns of all waves have been dispatched.
     *
     * Since: 1.0
     */
    properties[PROP_FINISHED] =
        g_param_spec_boolean ("finished",
                              "Finished",
                              "Whether all waves have finished spawning",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    /**
     * LrgWaveDirector::spawn:
     * @self: the director
     * @enemy_id: string identifier of the enemy type to spawn
     * @x: spawn X position
     * @y: spawn Y position
     * @wave_index: index of the wave this spawn belongs to
     *
     * Emitted for each individual enemy spawn. The game connects to
     * this signal and instantiates its own entity for @enemy_id.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SPAWN] =
        g_signal_new ("spawn",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 4,
                      G_TYPE_STRING, G_TYPE_FLOAT, G_TYPE_FLOAT,
                      G_TYPE_UINT);

    /**
     * LrgWaveDirector::wave-started:
     * @self: the director
     * @index: index of the wave that started
     *
     * Emitted when a wave's trigger fires and its entries begin
     * spawning.
     *
     * Since: 1.0
     */
    signals[SIGNAL_WAVE_STARTED] =
        g_signal_new ("wave-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * LrgWaveDirector::wave-completed:
     * @self: the director
     * @index: index of the wave that completed
     *
     * Emitted when all entries of a wave have finished spawning.
     *
     * Since: 1.0
     */
    signals[SIGNAL_WAVE_COMPLETED] =
        g_signal_new ("wave-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * LrgWaveDirector::finished:
     * @self: the director
     *
     * Emitted when the last wave has finished spawning. This means all
     * spawns were dispatched, not that all enemies are dead.
     *
     * Since: 1.0
     */
    signals[SIGNAL_FINISHED] =
        g_signal_new ("finished",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_wave_director_init (LrgWaveDirector *self)
{
    self->waves = g_ptr_array_new_with_free_func (wave_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_wave_director_new:
 *
 * Creates a new, empty wave director.
 *
 * Returns: (transfer full): a new #LrgWaveDirector
 *
 * Since: 1.0
 */
LrgWaveDirector *
lrg_wave_director_new (void)
{
    return g_object_new (LRG_TYPE_WAVE_DIRECTOR, NULL);
}

/* ==========================================================================
 * Loading
 * ========================================================================== */

/**
 * lrg_wave_director_load_from_file:
 * @self: an #LrgWaveDirector
 * @path: path to a YAML wave definition file
 * @error: (nullable): return location for a #GError
 *
 * Loads wave definitions from a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_wave_director_load_from_file (LrgWaveDirector  *self,
                                  const gchar      *path,
                                  GError          **error)
{
    g_autoptr(YamlParser) parser = NULL;

    g_return_val_if_fail (LRG_IS_WAVE_DIRECTOR (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, error))
        return FALSE;

    return load_from_parser (self, parser, error);
}

/**
 * lrg_wave_director_load_from_data:
 * @self: an #LrgWaveDirector
 * @data: YAML wave definition data
 * @length: length of @data, or -1 if null-terminated
 * @error: (nullable): return location for a #GError
 *
 * Loads wave definitions from an in-memory YAML string.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_wave_director_load_from_data (LrgWaveDirector  *self,
                                  const gchar      *data,
                                  gssize            length,
                                  GError          **error)
{
    g_autoptr(YamlParser) parser = NULL;

    g_return_val_if_fail (LRG_IS_WAVE_DIRECTOR (self), FALSE);
    g_return_val_if_fail (data != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_data (parser, data, length, error))
        return FALSE;

    return load_from_parser (self, parser, error);
}

/* ==========================================================================
 * Programmatic Builders
 * ========================================================================== */

/**
 * lrg_wave_director_add_wave:
 * @self: an #LrgWaveDirector
 * @trigger: the trigger condition for this wave
 * @trigger_value: the trigger threshold
 *
 * Appends a new empty wave.
 *
 * Returns: the index of the new wave
 *
 * Since: 1.0
 */
guint
lrg_wave_director_add_wave (LrgWaveDirector    *self,
                            LrgWaveTriggerType  trigger,
                            gfloat              trigger_value)
{
    g_return_val_if_fail (LRG_IS_WAVE_DIRECTOR (self), 0);

    g_ptr_array_add (self->waves, wave_new (trigger, trigger_value));

    return self->waves->len - 1;
}

/**
 * lrg_wave_director_wave_add_entry:
 * @self: an #LrgWaveDirector
 * @wave_index: index of the wave to add the entry to
 * @enemy_id: string identifier for the enemy type to spawn
 * @count: number of enemies this entry spawns
 * @pattern: placement pattern for the spawns
 * @x: pattern X coordinate
 * @y: pattern Y coordinate
 * @radius: ring radius, or line spacing
 * @interval: seconds between successive spawns (0 = all at once)
 *
 * Adds a spawn entry to an existing wave.
 *
 * Since: 1.0
 */
void
lrg_wave_director_wave_add_entry (LrgWaveDirector *self,
                                  guint            wave_index,
                                  const gchar     *enemy_id,
                                  guint            count,
                                  LrgWavePattern   pattern,
                                  gfloat           x,
                                  gfloat           y,
                                  gfloat           radius,
                                  gfloat           interval)
{
    Wave *wave;
    WaveEntry entry;

    g_return_if_fail (LRG_IS_WAVE_DIRECTOR (self));
    g_return_if_fail (wave_index < self->waves->len);
    g_return_if_fail (enemy_id != NULL);

    wave = g_ptr_array_index (self->waves, wave_index);

    memset (&entry, 0, sizeof (entry));
    entry.enemy_id = g_strdup (enemy_id);
    entry.count = count;
    entry.pattern = pattern;
    entry.x = x;
    entry.y = y;
    entry.radius = radius;
    entry.interval = interval;

    g_array_append_val (wave->entries, entry);
}

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_wave_director_set_bounds:
 * @self: an #LrgWaveDirector
 * @min_x: left edge of the spawn bounds
 * @min_y: top edge of the spawn bounds
 * @max_x: right edge of the spawn bounds
 * @max_y: bottom edge of the spawn bounds
 *
 * Sets the bounds rectangle used by the edge spawn patterns.
 *
 * Since: 1.0
 */
void
lrg_wave_director_set_bounds (LrgWaveDirector *self,
                              gfloat           min_x,
                              gfloat           min_y,
                              gfloat           max_x,
                              gfloat           max_y)
{
    g_return_if_fail (LRG_IS_WAVE_DIRECTOR (self));
    g_return_if_fail (min_x <= max_x);
    g_return_if_fail (min_y <= max_y);

    self->bounds_min_x = min_x;
    self->bounds_min_y = min_y;
    self->bounds_max_x = max_x;
    self->bounds_max_y = max_y;
}

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

/**
 * lrg_wave_director_start:
 * @self: an #LrgWaveDirector
 *
 * Starts the director.
 *
 * Since: 1.0
 */
void
lrg_wave_director_start (LrgWaveDirector *self)
{
    g_return_if_fail (LRG_IS_WAVE_DIRECTOR (self));

    if (self->started)
        return;

    self->started = TRUE;
    self->finished = FALSE;
    self->wave_active = FALSE;
    self->current_wave = 0;
    self->wave_elapsed = 0.0f;

    /* A director with no waves has nothing to spawn */
    if (self->waves->len == 0)
    {
        self->finished = TRUE;
        g_signal_emit (self, signals[SIGNAL_FINISHED], 0);
    }
}

/**
 * lrg_wave_director_reset:
 * @self: an #LrgWaveDirector
 *
 * Returns the director to its pre-start state, keeping the waves.
 *
 * Since: 1.0
 */
void
lrg_wave_director_reset (LrgWaveDirector *self)
{
    guint i;

    g_return_if_fail (LRG_IS_WAVE_DIRECTOR (self));

    self->started = FALSE;
    self->finished = FALSE;
    self->wave_active = FALSE;
    self->current_wave = 0;
    self->wave_elapsed = 0.0f;

    for (i = 0; i < self->waves->len; i++)
        wave_reset_entries (g_ptr_array_index (self->waves, i));
}

/**
 * lrg_wave_director_update:
 * @self: an #LrgWaveDirector
 * @delta: time step in seconds
 * @progress: externally-supplied progress value
 * @active_count: externally-supplied count of live enemies
 *
 * Advances the director by one frame.
 *
 * Since: 1.0
 */
void
lrg_wave_director_update (LrgWaveDirector *self,
                          gfloat           delta,
                          gfloat           progress,
                          guint            active_count)
{
    Wave *wave;

    g_return_if_fail (LRG_IS_WAVE_DIRECTOR (self));

    if (!self->started || self->finished)
        return;

    wave = g_ptr_array_index (self->waves, self->current_wave);

    if (!self->wave_active)
    {
        self->wave_elapsed += delta;

        if (!wave_trigger_fired (self, wave, progress, active_count))
            return;

        wave_activate (self, wave);
    }
    else
    {
        wave_update_spawning (self, wave, delta);
    }

    if (wave_is_spawned_out (wave))
        wave_advance (self);
}

/**
 * lrg_wave_director_skip_to_wave:
 * @self: an #LrgWaveDirector
 * @index: index of the wave to make current
 *
 * Skips to the given wave, discarding pending spawns.
 *
 * Since: 1.0
 */
void
lrg_wave_director_skip_to_wave (LrgWaveDirector *self,
                                guint            index)
{
    g_return_if_fail (LRG_IS_WAVE_DIRECTOR (self));
    g_return_if_fail (index < self->waves->len);

    self->finished = FALSE;
    self->wave_active = FALSE;
    self->current_wave = index;
    self->wave_elapsed = 0.0f;

    wave_reset_entries (g_ptr_array_index (self->waves, index));
}

/* ==========================================================================
 * State Queries
 * ========================================================================== */

/**
 * lrg_wave_director_get_current_wave:
 * @self: an #LrgWaveDirector
 *
 * Gets the index of the current wave.
 *
 * Returns: the current wave index
 *
 * Since: 1.0
 */
guint
lrg_wave_director_get_current_wave (LrgWaveDirector *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DIRECTOR (self), 0);

    return self->current_wave;
}

/**
 * lrg_wave_director_get_wave_count:
 * @self: an #LrgWaveDirector
 *
 * Gets the number of configured waves.
 *
 * Returns: the wave count
 *
 * Since: 1.0
 */
guint
lrg_wave_director_get_wave_count (LrgWaveDirector *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DIRECTOR (self), 0);

    return self->waves->len;
}

/**
 * lrg_wave_director_is_started:
 * @self: an #LrgWaveDirector
 *
 * Checks whether the director has been started.
 *
 * Returns: %TRUE if started
 *
 * Since: 1.0
 */
gboolean
lrg_wave_director_is_started (LrgWaveDirector *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DIRECTOR (self), FALSE);

    return self->started;
}

/**
 * lrg_wave_director_is_finished:
 * @self: an #LrgWaveDirector
 *
 * Checks whether all spawns of all waves have been dispatched.
 *
 * Returns: %TRUE if finished
 *
 * Since: 1.0
 */
gboolean
lrg_wave_director_is_finished (LrgWaveDirector *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DIRECTOR (self), FALSE);

    return self->finished;
}
