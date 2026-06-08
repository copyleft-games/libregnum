/* lrg-reel.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel.h"
#include "lrg-reel-clip.h"

struct _LrgReel
{
    GObject parent_instance;

    gchar      *id;
    gint        width;
    gint        height;
    gdouble     fps;
    gint        duration_in_frames;

    GPtrArray  *clips;  /* of LrgReelClip*, owns refs */
    GHashTable *props;  /* gchar* -> GValue* */
};

G_DEFINE_FINAL_TYPE (LrgReel, lrg_reel, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FPS,
    PROP_DURATION_IN_FRAMES,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
reel_value_free (gpointer value)
{
    GValue *v = value;

    g_value_unset (v);
    g_free (v);
}

static void
lrg_reel_finalize (GObject *object)
{
    LrgReel *self = LRG_REEL (object);

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->clips, g_ptr_array_unref);
    g_clear_pointer (&self->props, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_reel_parent_class)->finalize (object);
}

static void
lrg_reel_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgReel *self = LRG_REEL (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_WIDTH:
        g_value_set_int (value, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int (value, self->height);
        break;
    case PROP_FPS:
        g_value_set_double (value, self->fps);
        break;
    case PROP_DURATION_IN_FRAMES:
        g_value_set_int (value, self->duration_in_frames);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgReel *self = LRG_REEL (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_WIDTH:
        self->width = g_value_get_int (value);
        break;
    case PROP_HEIGHT:
        self->height = g_value_get_int (value);
        break;
    case PROP_FPS:
        self->fps = g_value_get_double (value);
        break;
    case PROP_DURATION_IN_FRAMES:
        self->duration_in_frames = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_class_init (LrgReelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_reel_finalize;
    object_class->get_property = lrg_reel_get_property;
    object_class->set_property = lrg_reel_set_property;

    properties[PROP_ID] =
        g_param_spec_string ("id", "Id", "Composition identifier", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_WIDTH] =
        g_param_spec_int ("width", "Width", "Width in pixels",
                          1, G_MAXINT, 1920,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_HEIGHT] =
        g_param_spec_int ("height", "Height", "Height in pixels",
                          1, G_MAXINT, 1080,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_FPS] =
        g_param_spec_double ("fps", "FPS", "Frames per second",
                             0.0001, G_MAXDOUBLE, 30.0,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_DURATION_IN_FRAMES] =
        g_param_spec_int ("duration-in-frames", "Duration In Frames",
                          "Total length in frames",
                          1, G_MAXINT, 1,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_init (LrgReel *self)
{
    self->width = 1920;
    self->height = 1080;
    self->fps = 30.0;
    self->duration_in_frames = 1;
    self->clips = g_ptr_array_new_with_free_func (g_object_unref);
    self->props = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, reel_value_free);
}

LrgReel *
lrg_reel_new (const gchar *id,
              gint         width,
              gint         height,
              gdouble      fps,
              gint         duration_in_frames)
{
    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);
    g_return_val_if_fail (fps > 0.0, NULL);
    g_return_val_if_fail (duration_in_frames > 0, NULL);

    return g_object_new (LRG_TYPE_REEL,
                         "id", id,
                         "width", width,
                         "height", height,
                         "fps", fps,
                         "duration-in-frames", duration_in_frames,
                         NULL);
}

const gchar *
lrg_reel_get_id (LrgReel *self)
{
    g_return_val_if_fail (LRG_IS_REEL (self), NULL);
    return self->id;
}

gint
lrg_reel_get_width (LrgReel *self)
{
    g_return_val_if_fail (LRG_IS_REEL (self), 0);
    return self->width;
}

gint
lrg_reel_get_height (LrgReel *self)
{
    g_return_val_if_fail (LRG_IS_REEL (self), 0);
    return self->height;
}

gdouble
lrg_reel_get_fps (LrgReel *self)
{
    g_return_val_if_fail (LRG_IS_REEL (self), 0.0);
    return self->fps;
}

gint
lrg_reel_get_duration_in_frames (LrgReel *self)
{
    g_return_val_if_fail (LRG_IS_REEL (self), 0);
    return self->duration_in_frames;
}

gdouble
lrg_reel_frames_to_seconds (LrgReel *self,
                            gint     frame)
{
    g_return_val_if_fail (LRG_IS_REEL (self), 0.0);
    return (gdouble) frame / self->fps;
}

gint
lrg_reel_seconds_to_frames (LrgReel *self,
                            gdouble  seconds)
{
    g_return_val_if_fail (LRG_IS_REEL (self), 0);
    return (gint) (seconds * self->fps + 0.5);
}

void
lrg_reel_add_clip (LrgReel     *self,
                   LrgReelClip *clip)
{
    g_return_if_fail (LRG_IS_REEL (self));
    g_return_if_fail (LRG_IS_REEL_CLIP (clip));

    g_ptr_array_add (self->clips, g_object_ref (clip));
}

void
lrg_reel_insert_clip (LrgReel     *self,
                      LrgReelClip *clip,
                      guint        index)
{
    g_return_if_fail (LRG_IS_REEL (self));
    g_return_if_fail (LRG_IS_REEL_CLIP (clip));

    if (index > self->clips->len)
        index = self->clips->len;

    g_ptr_array_insert (self->clips, (gint) index, g_object_ref (clip));
}

gboolean
lrg_reel_remove_clip (LrgReel     *self,
                      LrgReelClip *clip)
{
    g_return_val_if_fail (LRG_IS_REEL (self), FALSE);
    g_return_val_if_fail (LRG_IS_REEL_CLIP (clip), FALSE);

    return g_ptr_array_remove (self->clips, clip);
}

guint
lrg_reel_get_n_clips (LrgReel *self)
{
    g_return_val_if_fail (LRG_IS_REEL (self), 0);
    return self->clips->len;
}

LrgReelClip *
lrg_reel_get_clip (LrgReel *self,
                   guint    index)
{
    g_return_val_if_fail (LRG_IS_REEL (self), NULL);

    if (index >= self->clips->len)
        return NULL;

    return g_ptr_array_index (self->clips, index);
}

GPtrArray *
lrg_reel_get_clips (LrgReel *self)
{
    g_return_val_if_fail (LRG_IS_REEL (self), NULL);
    return self->clips;
}

/* ==========================================================================
 * Default properties
 * ========================================================================== */

static void
reel_set_value (LrgReel      *self,
                const gchar  *key,
                const GValue *value)
{
    GValue *stored;

    stored = g_new0 (GValue, 1);
    g_value_init (stored, G_VALUE_TYPE (value));
    g_value_copy (value, stored);
    g_hash_table_insert (self->props, g_strdup (key), stored);
}

gboolean
lrg_reel_has_prop (LrgReel     *self,
                   const gchar *key)
{
    g_return_val_if_fail (LRG_IS_REEL (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (self->props, key);
}

void
lrg_reel_set_prop_double (LrgReel     *self,
                          const gchar *key,
                          gdouble      value)
{
    GValue v = G_VALUE_INIT;

    g_return_if_fail (LRG_IS_REEL (self));
    g_return_if_fail (key != NULL);

    g_value_init (&v, G_TYPE_DOUBLE);
    g_value_set_double (&v, value);
    reel_set_value (self, key, &v);
    g_value_unset (&v);
}

gdouble
lrg_reel_get_prop_double (LrgReel     *self,
                          const gchar *key,
                          gdouble      fallback)
{
    GValue *v;

    g_return_val_if_fail (LRG_IS_REEL (self), fallback);
    g_return_val_if_fail (key != NULL, fallback);

    v = g_hash_table_lookup (self->props, key);
    if (v == NULL || !G_VALUE_HOLDS_DOUBLE (v))
        return fallback;

    return g_value_get_double (v);
}

void
lrg_reel_set_prop_int (LrgReel     *self,
                       const gchar *key,
                       gint         value)
{
    GValue v = G_VALUE_INIT;

    g_return_if_fail (LRG_IS_REEL (self));
    g_return_if_fail (key != NULL);

    g_value_init (&v, G_TYPE_INT);
    g_value_set_int (&v, value);
    reel_set_value (self, key, &v);
    g_value_unset (&v);
}

gint
lrg_reel_get_prop_int (LrgReel     *self,
                       const gchar *key,
                       gint         fallback)
{
    GValue *v;

    g_return_val_if_fail (LRG_IS_REEL (self), fallback);
    g_return_val_if_fail (key != NULL, fallback);

    v = g_hash_table_lookup (self->props, key);
    if (v == NULL || !G_VALUE_HOLDS_INT (v))
        return fallback;

    return g_value_get_int (v);
}

void
lrg_reel_set_prop_boolean (LrgReel     *self,
                           const gchar *key,
                           gboolean     value)
{
    GValue v = G_VALUE_INIT;

    g_return_if_fail (LRG_IS_REEL (self));
    g_return_if_fail (key != NULL);

    g_value_init (&v, G_TYPE_BOOLEAN);
    g_value_set_boolean (&v, value);
    reel_set_value (self, key, &v);
    g_value_unset (&v);
}

gboolean
lrg_reel_get_prop_boolean (LrgReel     *self,
                           const gchar *key,
                           gboolean     fallback)
{
    GValue *v;

    g_return_val_if_fail (LRG_IS_REEL (self), fallback);
    g_return_val_if_fail (key != NULL, fallback);

    v = g_hash_table_lookup (self->props, key);
    if (v == NULL || !G_VALUE_HOLDS_BOOLEAN (v))
        return fallback;

    return g_value_get_boolean (v);
}

void
lrg_reel_set_prop_string (LrgReel     *self,
                          const gchar *key,
                          const gchar *value)
{
    GValue v = G_VALUE_INIT;

    g_return_if_fail (LRG_IS_REEL (self));
    g_return_if_fail (key != NULL);

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    reel_set_value (self, key, &v);
    g_value_unset (&v);
}

const gchar *
lrg_reel_get_prop_string (LrgReel     *self,
                          const gchar *key)
{
    GValue *v;

    g_return_val_if_fail (LRG_IS_REEL (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    v = g_hash_table_lookup (self->props, key);
    if (v == NULL || !G_VALUE_HOLDS_STRING (v))
        return NULL;

    return g_value_get_string (v);
}
