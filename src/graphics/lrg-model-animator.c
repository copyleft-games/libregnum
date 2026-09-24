/* lrg-model-animator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared-model skeletal animation. Playback state lives in per-actor
 * LrgModelAnimState values; the animator turns a state into a pose key,
 * poses the shared raylib model only when the key changes, and draws a
 * subset of the model's meshes.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-model-animator.h"

/* ------------------------------------------------------------------------ */
/* LrgModelAnimState                                                        */
/* ------------------------------------------------------------------------ */

G_DEFINE_BOXED_TYPE (LrgModelAnimState, lrg_model_anim_state,
                     lrg_model_anim_state_copy,
                     lrg_model_anim_state_free)

LrgModelAnimState *
lrg_model_anim_state_new (void)
{
    LrgModelAnimState *state;

    state = g_new (LrgModelAnimState, 1);
    lrg_model_anim_state_reset (state);
    return state;
}

LrgModelAnimState *
lrg_model_anim_state_copy (const LrgModelAnimState *self)
{
    LrgModelAnimState *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new (LrgModelAnimState, 1);
    *copy = *self;
    return copy;
}

void
lrg_model_anim_state_free (LrgModelAnimState *self)
{
    g_free (self);
}

void
lrg_model_anim_state_reset (LrgModelAnimState *self)
{
    g_return_if_fail (self != NULL);

    self->clip = -1;
    self->time = 0.0;
    self->rate = 1.0;
    self->loop = TRUE;
    self->previous_clip = -1;
    self->previous_time = 0.0;
    self->previous_loop = TRUE;
    self->fade = 0.0;
    self->fade_duration = 0.0;
}

gdouble
lrg_model_anim_state_get_blend (const LrgModelAnimState *self)
{
    g_return_val_if_fail (self != NULL, 1.0);

    if (self->previous_clip < 0 || !(self->fade_duration > 0.0))
        return 1.0;
    return CLAMP (self->fade / self->fade_duration, 0.0, 1.0);
}

/* ------------------------------------------------------------------------ */
/* Animator                                                                 */
/* ------------------------------------------------------------------------ */

/*
 * PoseKey:
 *
 * Everything that determines the model's pose for one apply(). When
 * previous_clip is -1 the pose is a single clip frame and blend is
 * LRG_MODEL_ANIMATOR_BLEND_LEVELS.
 */
typedef struct
{
    gint  clip;
    gint  frame;
    gint  previous_clip;
    gint  previous_frame;
    guint blend;
} PoseKey;

typedef struct
{
    gchar             *name;
    gint               frame_count;
    GrlModelAnimation *animation;   /* NULL when headless */
} Clip;

struct _LrgModelAnimator
{
    GObject     parent_instance;

    GrlModel   *model;      /* nullable (headless) */
    GArray     *clips;      /* Clip */
    GHashTable *aliases;    /* alias -> target name */

    gboolean    has_key;
    PoseKey     last_key;
};

G_DEFINE_TYPE (LrgModelAnimator, lrg_model_animator, G_TYPE_OBJECT)

static void
clip_clear (gpointer data)
{
    Clip *clip = data;

    g_clear_pointer (&clip->name, g_free);
    g_clear_object (&clip->animation);
}

static void
lrg_model_animator_finalize (GObject *object)
{
    LrgModelAnimator *self = LRG_MODEL_ANIMATOR (object);

    g_clear_pointer (&self->clips, g_array_unref);
    g_clear_pointer (&self->aliases, g_hash_table_unref);
    g_clear_object (&self->model);

    G_OBJECT_CLASS (lrg_model_animator_parent_class)->finalize (object);
}

static void
lrg_model_animator_class_init (LrgModelAnimatorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_model_animator_finalize;
}

static void
lrg_model_animator_init (LrgModelAnimator *self)
{
    self->clips = g_array_new (FALSE, TRUE, sizeof (Clip));
    g_array_set_clear_func (self->clips, clip_clear);
    self->aliases = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static Clip *
clip_at (LrgModelAnimator *self,
         gint              clip)
{
    if (clip < 0 || (guint)clip >= self->clips->len)
        return NULL;
    return &g_array_index (self->clips, Clip, clip);
}

LrgModelAnimator *
lrg_model_animator_new (GrlModel  *model,
                        GPtrArray *clips)
{
    LrgModelAnimator *self;
    guint             i;

    g_return_val_if_fail (GRL_IS_MODEL (model), NULL);
    g_return_val_if_fail (clips != NULL, NULL);

    self = g_object_new (LRG_TYPE_MODEL_ANIMATOR, NULL);
    self->model = g_object_ref (model);
    for (i = 0; i < clips->len; i++)
    {
        GrlModelAnimation *animation = g_ptr_array_index (clips, i);
        Clip               clip;

        g_return_val_if_fail (GRL_IS_MODEL_ANIMATION (animation), self);
        clip.name = g_strdup (grl_model_animation_get_name (animation));
        clip.frame_count = grl_model_animation_get_frame_count (animation);
        clip.animation = g_object_ref (animation);
        g_array_append_val (self->clips, clip);
    }
    return self;
}

LrgModelAnimator *
lrg_model_animator_new_headless (const gchar *const *names,
                                 const gint         *frame_counts,
                                 guint               n_clips)
{
    LrgModelAnimator *self;
    guint             i;

    g_return_val_if_fail (n_clips == 0 || (names != NULL && frame_counts != NULL), NULL);

    self = g_object_new (LRG_TYPE_MODEL_ANIMATOR, NULL);
    for (i = 0; i < n_clips; i++)
    {
        Clip clip;

        clip.name = g_strdup (names[i] != NULL ? names[i] : "");
        clip.frame_count = MAX (0, frame_counts[i]);
        clip.animation = NULL;
        g_array_append_val (self->clips, clip);
    }
    return self;
}

GrlModel *
lrg_model_animator_get_model (LrgModelAnimator *self)
{
    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), NULL);

    return self->model;
}

guint
lrg_model_animator_get_clip_count (LrgModelAnimator *self)
{
    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), 0);

    return self->clips->len;
}

const gchar *
lrg_model_animator_get_clip_name (LrgModelAnimator *self,
                                  gint              clip)
{
    Clip *entry;

    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), NULL);

    entry = clip_at (self, clip);
    return entry != NULL ? entry->name : NULL;
}

gint
lrg_model_animator_get_clip_frame_count (LrgModelAnimator *self,
                                         gint              clip)
{
    Clip *entry;

    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), 0);

    entry = clip_at (self, clip);
    return entry != NULL ? entry->frame_count : 0;
}

gdouble
lrg_model_animator_get_clip_duration (LrgModelAnimator *self,
                                      gint              clip)
{
    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), 0.0);

    return lrg_model_animator_get_clip_frame_count (self, clip) / LRG_MODEL_ANIMATOR_FPS;
}

gboolean
lrg_model_animator_clip_is_valid (LrgModelAnimator *self,
                                  gint              clip)
{
    Clip *entry;

    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), FALSE);

    entry = clip_at (self, clip);
    if (entry == NULL || entry->frame_count < 1)
        return FALSE;
    if (self->model == NULL)
        return TRUE;
    return entry->animation != NULL && grl_model_animation_is_valid (entry->animation, self->model);
}

gboolean
lrg_model_animator_set_clip_names_from_info (LrgModelAnimator *self,
                                             LrgGltfInfo      *info)
{
    guint i;

    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), FALSE);
    g_return_val_if_fail (LRG_IS_GLTF_INFO (info), FALSE);

    /* Validate everything first so a mismatch changes nothing. */
    if (lrg_gltf_info_get_animation_count (info) != self->clips->len)
        return FALSE;
    for (i = 0; i < self->clips->len; i++)
        if (!g_str_has_prefix (lrg_gltf_info_get_animation_name (info, i),
                               g_array_index (self->clips, Clip, i).name))
            return FALSE;

    for (i = 0; i < self->clips->len; i++)
    {
        Clip *clip = &g_array_index (self->clips, Clip, i);

        g_free (clip->name);
        clip->name = g_strdup (lrg_gltf_info_get_animation_name (info, i));
    }
    return TRUE;
}

void
lrg_model_animator_set_alias (LrgModelAnimator *self,
                              const gchar      *alias,
                              const gchar      *target)
{
    g_return_if_fail (LRG_IS_MODEL_ANIMATOR (self));
    g_return_if_fail (alias != NULL);

    if (target == NULL)
        g_hash_table_remove (self->aliases, alias);
    else
        g_hash_table_insert (self->aliases, g_strdup (alias), g_strdup (target));
}

void
lrg_model_animator_clear_aliases (LrgModelAnimator *self)
{
    g_return_if_fail (LRG_IS_MODEL_ANIMATOR (self));

    g_hash_table_remove_all (self->aliases);
}

static gint
find_exact (LrgModelAnimator *self,
            const gchar      *name)
{
    guint i;

    for (i = 0; i < self->clips->len; i++)
        if (strcmp (g_array_index (self->clips, Clip, i).name, name) == 0)
            return (gint)i;
    return -1;
}

/* find_suffix:
 * Case-insensitive suffix match at a word boundary. Candidates rank by
 * what precedes the suffix: nothing (a case-insensitive whole-name match),
 * then '|' (an exporter prefix such as "Armature|"), then any other
 * non-alphanumeric character. Within a rank the shortest name wins, then
 * the lowest index. */
static gint
find_suffix (LrgModelAnimator *self,
             const gchar      *name)
{
    gsize length = strlen (name);
    gsize best_length = G_MAXSIZE;
    guint best_rank = G_MAXUINT;
    gint  best = -1;
    guint i;

    if (length == 0)
        return -1;
    for (i = 0; i < self->clips->len; i++)
    {
        const gchar *clip = g_array_index (self->clips, Clip, i).name;
        gsize        clip_length = strlen (clip);
        const gchar *tail;
        guint        rank;

        if (clip_length < length)
            continue;
        tail = clip + clip_length - length;
        if (g_ascii_strcasecmp (tail, name) != 0)
            continue;
        if (tail > clip && g_ascii_isalnum (tail[-1]))
            continue;
        rank = tail == clip ? 0 : tail[-1] == '|' ? 1 : 2;
        if (rank < best_rank || (rank == best_rank && clip_length < best_length))
        {
            best = (gint)i;
            best_rank = rank;
            best_length = clip_length;
        }
    }
    return best;
}

gint
lrg_model_animator_find_clip (LrgModelAnimator *self,
                              const gchar      *name)
{
    const gchar *target;
    gint         clip;

    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), -1);
    g_return_val_if_fail (name != NULL, -1);

    clip = find_exact (self, name);
    if (clip >= 0)
        return clip;

    target = g_hash_table_lookup (self->aliases, name);
    if (target != NULL)
    {
        clip = find_exact (self, target);
        if (clip >= 0)
            return clip;
        clip = find_suffix (self, target);
        if (clip >= 0)
            return clip;
    }
    return find_suffix (self, name);
}

gboolean
lrg_model_animator_play (LrgModelAnimator  *self,
                         LrgModelAnimState *state,
                         gint               clip,
                         gboolean           loop,
                         gdouble            fade_seconds)
{
    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), FALSE);
    g_return_val_if_fail (state != NULL, FALSE);

    if (clip < -1 || clip >= (gint)self->clips->len)
        return FALSE;

    /* Same clip: keep playing, only the loop mode may change. */
    if (clip == state->clip)
    {
        state->loop = loop;
        return TRUE;
    }

    if (clip >= 0 && isfinite (fade_seconds) && fade_seconds > 0.0 &&
        lrg_model_animator_clip_is_valid (self, state->clip))
    {
        state->previous_clip = state->clip;
        state->previous_time = state->time;
        state->previous_loop = state->loop;
        state->fade = 0.0;
        state->fade_duration = fade_seconds;
    }
    else
    {
        state->previous_clip = -1;
        state->previous_time = 0.0;
        state->fade = 0.0;
        state->fade_duration = 0.0;
    }

    state->clip = clip;
    state->loop = loop;
    state->time = 0.0;
    return TRUE;
}

void
lrg_model_animator_restart (LrgModelAnimator  *self,
                            LrgModelAnimState *state)
{
    g_return_if_fail (LRG_IS_MODEL_ANIMATOR (self));
    g_return_if_fail (state != NULL);

    state->time = 0.0;
    state->previous_clip = -1;
    state->previous_time = 0.0;
    state->fade = 0.0;
    state->fade_duration = 0.0;
}

/* advance_time:
 * Moves one clip's clock: loops wrap into [0, duration), one-shots clamp
 * to [0, last frame time]. */
static gdouble
advance_time (LrgModelAnimator *self,
              gint              clip,
              gdouble           time,
              gdouble           delta,
              gboolean          loop)
{
    gint    frames = lrg_model_animator_get_clip_frame_count (self, clip);
    gdouble duration;
    gdouble next;

    if (frames < 1)
        return 0.0;
    next = time + delta;
    if (!isfinite (next))
        return time;

    if (loop)
    {
        duration = frames / LRG_MODEL_ANIMATOR_FPS;
        next = fmod (next, duration);
        if (next < 0.0)
            next += duration;
        /* fmod of a value just below a multiple can round up to duration. */
        if (next >= duration)
            next = 0.0;
        return next;
    }
    return CLAMP (next, 0.0, (frames - 1) / LRG_MODEL_ANIMATOR_FPS);
}

void
lrg_model_animator_advance (LrgModelAnimator  *self,
                            LrgModelAnimState *state,
                            gdouble            dt)
{
    gdouble delta;

    g_return_if_fail (LRG_IS_MODEL_ANIMATOR (self));
    g_return_if_fail (state != NULL);

    if (!isfinite (dt) || dt < 0.0)
        return;

    delta = isfinite (state->rate) ? dt * state->rate : 0.0;
    if (state->clip >= 0)
        state->time = advance_time (self, state->clip, state->time, delta, state->loop);

    if (state->previous_clip >= 0)
    {
        state->previous_time = advance_time (self, state->previous_clip, state->previous_time,
                                             delta, state->previous_loop);
        state->fade += dt;
        if (!(state->fade < state->fade_duration))
        {
            state->previous_clip = -1;
            state->previous_time = 0.0;
            state->fade = 0.0;
            state->fade_duration = 0.0;
        }
    }
}

/* Tolerance, in frames, when flooring time * 60: accumulated or wrapped
 * times such as fmod (0.6, 0.5) = 0.0999... must still land on frame 6. */
#define FRAME_EPSILON (1e-6)

/* frame_for:
 * floor(time * 60 + epsilon) wrapped for loops, clamped for one-shots. */
static gint
frame_for (LrgModelAnimator *self,
           gint              clip,
           gdouble           time,
           gboolean          loop)
{
    gint    frames = lrg_model_animator_get_clip_frame_count (self, clip);
    gdouble raw;
    gint64  frame;

    if (frames < 1 || !isfinite (time))
        return -1;
    raw = floor (time * LRG_MODEL_ANIMATOR_FPS + FRAME_EPSILON);
    if (raw < 0.0)
        raw = loop ? raw : 0.0;
    if (raw > (gdouble)G_MAXINT32)
        raw = (gdouble)G_MAXINT32;
    if (raw < (gdouble)G_MININT32)
        raw = (gdouble)G_MININT32;
    frame = (gint64)raw;

    if (loop)
    {
        frame %= frames;
        if (frame < 0)
            frame += frames;
        return (gint)frame;
    }
    return (gint)MIN (frame, (gint64)frames - 1);
}

gint
lrg_model_animator_frame_of (LrgModelAnimator        *self,
                             const LrgModelAnimState *state)
{
    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), -1);
    g_return_val_if_fail (state != NULL, -1);

    return frame_for (self, state->clip, state->time, state->loop);
}

gint
lrg_model_animator_previous_frame_of (LrgModelAnimator        *self,
                                      const LrgModelAnimState *state)
{
    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), -1);
    g_return_val_if_fail (state != NULL, -1);

    if (state->previous_clip < 0)
        return -1;
    return frame_for (self, state->previous_clip, state->previous_time, state->previous_loop);
}

gboolean
lrg_model_animator_is_finished (LrgModelAnimator        *self,
                                const LrgModelAnimState *state)
{
    gint frames;

    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), FALSE);
    g_return_val_if_fail (state != NULL, FALSE);

    frames = lrg_model_animator_get_clip_frame_count (self, state->clip);
    if (state->loop || frames < 1)
        return FALSE;
    if (state->rate < 0.0)
        return !(state->time > 0.0);
    return !(state->time < (frames - 1) / LRG_MODEL_ANIMATOR_FPS);
}

/* compute_key:
 * Builds the pose key for @state. Returns FALSE when there is nothing
 * valid to pose. A crossfade whose weight quantizes to the full level, or
 * whose previous clip is unusable, degrades to the plain current pose. */
static gboolean
compute_key (LrgModelAnimator        *self,
             const LrgModelAnimState *state,
             PoseKey                 *key)
{
    gdouble weight;

    memset (key, 0, sizeof *key);
    if (!lrg_model_animator_clip_is_valid (self, state->clip))
        return FALSE;

    key->clip = state->clip;
    key->frame = frame_for (self, state->clip, state->time, state->loop);
    /* A non-finite time has no frame; raylib would read keyframe -1. */
    if (key->frame < 0)
        return FALSE;
    key->previous_clip = -1;
    key->previous_frame = -1;
    key->blend = LRG_MODEL_ANIMATOR_BLEND_LEVELS;

    if (state->previous_clip >= 0 &&
        lrg_model_animator_clip_is_valid (self, state->previous_clip))
    {
        weight = lrg_model_anim_state_get_blend (state);
        key->blend = (guint)floor (weight * LRG_MODEL_ANIMATOR_BLEND_LEVELS + 0.5);
        if (key->blend < LRG_MODEL_ANIMATOR_BLEND_LEVELS)
        {
            key->previous_clip = state->previous_clip;
            key->previous_frame = frame_for (self, state->previous_clip,
                                             state->previous_time, state->previous_loop);
            /* Unusable previous time: drop the fade, pose the current clip. */
            if (key->previous_frame < 0)
            {
                key->previous_clip = -1;
                key->previous_frame = -1;
                key->blend = LRG_MODEL_ANIMATOR_BLEND_LEVELS;
            }
        }
        else
        {
            key->blend = LRG_MODEL_ANIMATOR_BLEND_LEVELS;
        }
    }
    return TRUE;
}

gboolean
lrg_model_animator_apply (LrgModelAnimator        *self,
                          const LrgModelAnimState *state)
{
    PoseKey key;

    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), FALSE);
    g_return_val_if_fail (state != NULL, FALSE);

    if (!compute_key (self, state, &key))
        return FALSE;
    if (self->has_key && memcmp (&key, &self->last_key, sizeof key) == 0)
        return FALSE;

    if (self->model != NULL)
    {
        Clip *current = clip_at (self, key.clip);
        Clip *previous = clip_at (self, key.previous_clip);

        /* compute_key() only yields valid clips; keep the compiler sure. */
        if (current == NULL)
            return FALSE;
        if (previous != NULL)
        {

            /* blend 0 = previous clip, 1 = current clip. */
            grl_model_animation_blend (self->model,
                                       previous->animation, (gfloat)key.previous_frame,
                                       current->animation, (gfloat)key.frame,
                                       (gfloat)key.blend / (gfloat)LRG_MODEL_ANIMATOR_BLEND_LEVELS);
        }
        else
        {
            grl_model_animation_update (current->animation, self->model, key.frame);
        }
    }

    self->last_key = key;
    self->has_key = TRUE;
    return TRUE;
}

void
lrg_model_animator_invalidate (LrgModelAnimator *self)
{
    g_return_if_fail (LRG_IS_MODEL_ANIMATOR (self));

    self->has_key = FALSE;
}

GBytes *
lrg_model_animator_build_node_mask (LrgModelAnimator   *self,
                                    LrgGltfInfo        *info,
                                    const gchar *const *hidden_nodes)
{
    g_return_val_if_fail (LRG_IS_MODEL_ANIMATOR (self), NULL);
    g_return_val_if_fail (LRG_IS_GLTF_INFO (info), NULL);

    if (self->model != NULL &&
        (gint)lrg_gltf_info_get_mesh_count (info) != grl_model_get_mesh_count (self->model))
        return NULL;
    return lrg_gltf_info_build_node_mask (info, hidden_nodes);
}

/* ------------------------------------------------------------------------ */
/* Matrix helpers (raymath.h is not gnu89-clean)                            */
/* ------------------------------------------------------------------------ */

/* matrix_to_array / matrix_from_array:
 * a[k] holds raylib's field mK, i.e. column-major storage. */
static void
matrix_to_array (const Matrix *m,
                 gfloat        a[16])
{
    a[0] = m->m0;   a[1] = m->m1;   a[2] = m->m2;   a[3] = m->m3;
    a[4] = m->m4;   a[5] = m->m5;   a[6] = m->m6;   a[7] = m->m7;
    a[8] = m->m8;   a[9] = m->m9;   a[10] = m->m10; a[11] = m->m11;
    a[12] = m->m12; a[13] = m->m13; a[14] = m->m14; a[15] = m->m15;
}

static Matrix
matrix_from_array (const gfloat a[16])
{
    Matrix m;

    m.m0 = a[0];   m.m1 = a[1];   m.m2 = a[2];   m.m3 = a[3];
    m.m4 = a[4];   m.m5 = a[5];   m.m6 = a[6];   m.m7 = a[7];
    m.m8 = a[8];   m.m9 = a[9];   m.m10 = a[10]; m.m11 = a[11];
    m.m12 = a[12]; m.m13 = a[13]; m.m14 = a[14]; m.m15 = a[15];
    return m;
}

/* matrix_multiply:
 * Same result as raymath MatrixMultiply(left, right): apply @left first,
 * then @right. */
static Matrix
matrix_multiply (Matrix left,
                 Matrix right)
{
    gfloat l[16], r[16], out[16];
    gint   c, row, k;

    matrix_to_array (&left, l);
    matrix_to_array (&right, r);
    for (c = 0; c < 4; c++)
        for (row = 0; row < 4; row++)
        {
            gfloat sum = 0.0f;

            for (k = 0; k < 4; k++)
                sum += l[c * 4 + k] * r[k * 4 + row];
            out[c * 4 + row] = sum;
        }
    return matrix_from_array (out);
}

/* matrix_scale_yaw_translate:
 * Scale, then rotate about +Y by @yaw radians, then translate - the
 * order raylib's DrawModelEx() uses. */
static Matrix
matrix_scale_yaw_translate (gfloat x,
                            gfloat y,
                            gfloat z,
                            gfloat yaw,
                            gfloat scale)
{
    gfloat a[16];
    gfloat c = cosf (yaw);
    gfloat s = sinf (yaw);

    memset (a, 0, sizeof a);
    /* Rotation about Y (raymath MatrixRotateY) times the uniform scale. */
    a[0] = c * scale;
    a[2] = -s * scale;
    a[5] = scale;
    a[8] = s * scale;
    a[10] = c * scale;
    a[12] = x;
    a[13] = y;
    a[14] = z;
    a[15] = 1.0f;
    return matrix_from_array (a);
}

/* draw_meshes:
 * DrawModelEx() restricted to the meshes the mask keeps. */
static void
draw_meshes (LrgModelAnimator *self,
             GBytes           *mask,
             Matrix            transform,
             Color             tint)
{
    Model        *model;
    const guint8 *bytes = NULL;
    gsize         n_bytes = 0;
    Matrix        world;
    gint          i;

    if (self->model == NULL || !grl_model_is_valid (self->model))
        return;
    model = grl_model_get_handle (self->model);
    if (mask != NULL)
        bytes = g_bytes_get_data (mask, &n_bytes);

    world = matrix_multiply (model->transform, transform);
    for (i = 0; i < model->meshCount; i++)
    {
        Material material;
        Color    diffuse;

        if (bytes != NULL && (gsize)i < n_bytes && bytes[i] == 0)
            continue;

        material = model->materials[model->meshMaterial[i]];
        diffuse = material.maps[MATERIAL_MAP_DIFFUSE].color;
        material.maps[MATERIAL_MAP_DIFFUSE].color = (Color) {
            (unsigned char)(((gint)diffuse.r * (gint)tint.r) / 255),
            (unsigned char)(((gint)diffuse.g * (gint)tint.g) / 255),
            (unsigned char)(((gint)diffuse.b * (gint)tint.b) / 255),
            (unsigned char)(((gint)diffuse.a * (gint)tint.a) / 255)
        };

        /* GPU skinning: bone matrices go to the shader before the draw. */
        if (material.shader.locs != NULL &&
            material.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS] != -1 &&
            model->boneMatrices != NULL)
        {
            rlEnableShader (material.shader.id);
            rlSetUniformMatrices (material.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS],
                                  model->boneMatrices, model->skeleton.boneCount);
        }

        DrawMesh (model->meshes[i], material, world);

        /* maps is shared with the model: restore the untinted colour. */
        material.maps[MATERIAL_MAP_DIFFUSE].color = diffuse;
    }
}

/* GrlMatrix mirrors raylib's Matrix field for field. */
G_STATIC_ASSERT (sizeof (GrlMatrix) == sizeof (Matrix));

static Color
tint_or_white (const GrlColor *tint)
{
    if (tint == NULL)
        return (Color) { 255, 255, 255, 255 };
    return (Color) { tint->r, tint->g, tint->b, tint->a };
}

void
lrg_model_animator_draw_masked (LrgModelAnimator *self,
                                GBytes           *mask,
                                const GrlMatrix  *transform,
                                const GrlColor   *tint)
{
    Matrix matrix;

    g_return_if_fail (LRG_IS_MODEL_ANIMATOR (self));
    g_return_if_fail (transform != NULL);

    memcpy (&matrix, transform, sizeof matrix);
    draw_meshes (self, mask, matrix, tint_or_white (tint));
}

void
lrg_model_animator_draw_masked_ex (LrgModelAnimator *self,
                                   GBytes           *mask,
                                   gfloat            x,
                                   gfloat            y,
                                   gfloat            z,
                                   gfloat            yaw,
                                   gfloat            scale,
                                   const GrlColor   *tint)
{
    Matrix transform;

    g_return_if_fail (LRG_IS_MODEL_ANIMATOR (self));

    transform = matrix_scale_yaw_translate (x, y, z, yaw, scale);
    draw_meshes (self, mask, transform, tint_or_white (tint));
}
