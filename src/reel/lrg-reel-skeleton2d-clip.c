/* lrg-reel-skeleton2d-clip.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelSkeleton2DClip: poses an LrgSkeleton2D for the frame's time and
 * draws it on the CPU with lrg_skeleton2d_draw_to_canvas().
 */

#include "config.h"

#include <math.h>
#include <gio/gio.h>

#include "lrg-reel-skeleton2d-clip.h"
#include "lrg-reel-context.h"
#include "../graphics/lrg-image-canvas.h"

/*
 * Thread safety.  lrg_reel_renderer_render_parallel() renders different
 * frames of the same clip objects on several threads at once, so a clip
 * must keep no mutable render state on itself (docs/modules/reel/
 * rendering.org, "Parallel rendering").  A skeleton is nothing but
 * mutable render state: the local pose, the world matrices and palette,
 * the slot colours, attachments and draw order, and the deformed vertex
 * buffer of every attachment are all written while a frame is posed and
 * drawn.
 *
 * The two ways out are a private copy of the skeleton per render, or a
 * lock.  LrgSkeleton2D has no deep copy (its sprites, skins and clips
 * would all need one, and new channels such as blend-shape weights would
 * have to be remembered by it), and a copy per frame would also allocate
 * every mesh again.  So the pose-and-draw step runs under a lock.  The
 * lock is process-wide rather than per clip or per skeleton because
 * attachments are shared by reference: lrg_skeleton2d_set_attachment()
 * keeps the sprite, so two skeletons (and two clips) can deform the same
 * LrgSkinnedSprite.  It is recursive so a pose callback may render a
 * nested reel holding another skeleton clip on the same thread.
 *
 * What this costs: skeleton clips of different frames render one at a
 * time, while everything else in those frames (other clips, effects,
 * compositing, encoding) still runs in parallel.  Posing and drawing a
 * character is a small part of a frame next to clearing, compositing and
 * encoding it, and the output is byte-identical to a sequential render
 * because each frame resets the pose before posing it.
 */
static GRecMutex skeleton_render_lock;

struct _LrgReelSkeleton2DClip
{
    LrgReelClip                parent_instance;

    LrgSkeleton2D             *skeleton;
    LrgSkeleton2DClip         *animation;
    gchar                     *animation_name;
    gchar                     *file;

    gdouble                    speed;
    gboolean                   loop;
    gdouble                    x;
    gdouble                    y;
    gdouble                    scale;
    gboolean                   flip_x;

    LrgReelSkeleton2DPoseFunc  pose_func;
    gpointer                   pose_data;
    GDestroyNotify             pose_destroy;
};

G_DEFINE_FINAL_TYPE (LrgReelSkeleton2DClip, lrg_reel_skeleton2d_clip, LRG_TYPE_REEL_CLIP)

enum
{
    PROP_0,
    PROP_SKELETON,
    PROP_ANIMATION,
    PROP_ANIMATION_NAME,
    PROP_FILE,
    PROP_SPEED,
    PROP_LOOP,
    PROP_ORIGIN_X,
    PROP_ORIGIN_Y,
    PROP_SKELETON_SCALE,
    PROP_FLIP_X,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Render vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_skeleton2d_clip_render (LrgReelClip    *base,
                                 LrgReelContext *ctx,
                                 LrgImageCanvas *canvas)
{
    LrgReelSkeleton2DClip *self = LRG_REEL_SKELETON2D_CLIP (base);
    LrgSkeleton2D         *skel = self->skeleton;
    gdouble                seconds;
    gfloat                 s;

    if (skel == NULL)
        return;

    /* clip-relative time (the renderer pushed the clip's window), with
     * the motion-blur sub-frame phase */
    seconds = lrg_reel_context_get_seconds (ctx);
    s = (gfloat) self->scale;

    g_rec_mutex_lock (&skeleton_render_lock);

    /* 1. the frame's pose from scratch, so it depends only on @seconds
     *    and never on the frame rendered before it */
    lrg_skeleton2d_reset_pose (skel);
    if (self->animation != NULL)
        lrg_skeleton2d_clip_apply (self->animation, skel,
                                   (gfloat) lrg_reel_skeleton2d_clip_get_animation_time (self, seconds),
                                   1.0f);

    /* 2. procedural layers on top */
    if (self->pose_func != NULL)
        self->pose_func (self, skel, seconds, self->pose_data);

    /* 3. place it (as LrgSkeleton2DComponent does), compute the world
     *    transforms and draw; the canvas transform stays in effect, so
     *    the inherited clip transform composes with the placement */
    lrg_skeleton2d_set_root_transform (skel, (gfloat) self->x, (gfloat) self->y, 0.0f,
                                       self->flip_x ? -s : s, s);
    lrg_skeleton2d_update_world (skel);
    lrg_skeleton2d_draw_to_canvas (skel, canvas, NULL);

    g_rec_mutex_unlock (&skeleton_render_lock);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_skeleton2d_clip_dispose (GObject *object)
{
    LrgReelSkeleton2DClip *self = LRG_REEL_SKELETON2D_CLIP (object);

    g_clear_object (&self->skeleton);
    g_clear_object (&self->animation);
    lrg_reel_skeleton2d_clip_set_pose_func (self, NULL, NULL, NULL);

    G_OBJECT_CLASS (lrg_reel_skeleton2d_clip_parent_class)->dispose (object);
}

static void
lrg_reel_skeleton2d_clip_finalize (GObject *object)
{
    LrgReelSkeleton2DClip *self = LRG_REEL_SKELETON2D_CLIP (object);

    g_clear_pointer (&self->animation_name, g_free);
    g_clear_pointer (&self->file, g_free);

    G_OBJECT_CLASS (lrg_reel_skeleton2d_clip_parent_class)->finalize (object);
}

/*
 * Loads the `file` property (the YAML loader's way of naming a
 * skeleton).  A file that does not load leaves the clip without a
 * skeleton and warns: a property setter has no GError.
 */
static void
reel_skeleton2d_clip_set_file (LrgReelSkeleton2DClip *self,
                               const gchar           *file)
{
    g_autoptr(LrgSkeleton2D) skel = NULL;
    g_autoptr(GError)        error = NULL;

    if (g_strcmp0 (self->file, file) == 0)
        return;
    g_free (self->file);
    self->file = g_strdup (file);
    if (file != NULL && file[0] != '\0')
    {
        skel = lrg_skeleton2d_new_from_file (file, &error);
        if (skel == NULL)
            g_warning ("skeleton2d clip: %s", error->message);
    }
    lrg_reel_skeleton2d_clip_set_skeleton (self, skel);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILE]);
}

static void
lrg_reel_skeleton2d_clip_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    LrgReelSkeleton2DClip *self = LRG_REEL_SKELETON2D_CLIP (object);

    switch (prop_id)
    {
    case PROP_SKELETON:
        g_value_set_object (value, self->skeleton);
        break;
    case PROP_ANIMATION:
        g_value_set_object (value, self->animation);
        break;
    case PROP_ANIMATION_NAME:
        g_value_set_string (value, self->animation_name);
        break;
    case PROP_FILE:
        g_value_set_string (value, self->file);
        break;
    case PROP_SPEED:
        g_value_set_double (value, self->speed);
        break;
    case PROP_LOOP:
        g_value_set_boolean (value, self->loop);
        break;
    case PROP_ORIGIN_X:
        g_value_set_double (value, self->x);
        break;
    case PROP_ORIGIN_Y:
        g_value_set_double (value, self->y);
        break;
    case PROP_SKELETON_SCALE:
        g_value_set_double (value, self->scale);
        break;
    case PROP_FLIP_X:
        g_value_set_boolean (value, self->flip_x);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_skeleton2d_clip_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    LrgReelSkeleton2DClip *self = LRG_REEL_SKELETON2D_CLIP (object);

    switch (prop_id)
    {
    case PROP_SKELETON:
        lrg_reel_skeleton2d_clip_set_skeleton (self, g_value_get_object (value));
        break;
    case PROP_ANIMATION:
        lrg_reel_skeleton2d_clip_set_animation (self, g_value_get_object (value));
        break;
    case PROP_ANIMATION_NAME:
        lrg_reel_skeleton2d_clip_set_animation_name (self, g_value_get_string (value));
        break;
    case PROP_FILE:
        reel_skeleton2d_clip_set_file (self, g_value_get_string (value));
        break;
    case PROP_SPEED:
        lrg_reel_skeleton2d_clip_set_speed (self, g_value_get_double (value));
        break;
    case PROP_LOOP:
        lrg_reel_skeleton2d_clip_set_loop (self, g_value_get_boolean (value));
        break;
    case PROP_ORIGIN_X:
        lrg_reel_skeleton2d_clip_set_placement (self, g_value_get_double (value), self->y, self->scale);
        break;
    case PROP_ORIGIN_Y:
        lrg_reel_skeleton2d_clip_set_placement (self, self->x, g_value_get_double (value), self->scale);
        break;
    case PROP_SKELETON_SCALE:
        lrg_reel_skeleton2d_clip_set_placement (self, self->x, self->y, g_value_get_double (value));
        break;
    case PROP_FLIP_X:
        lrg_reel_skeleton2d_clip_set_flip_x (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_skeleton2d_clip_class_init (LrgReelSkeleton2DClipClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class = LRG_REEL_CLIP_CLASS (klass);

    object_class->dispose = lrg_reel_skeleton2d_clip_dispose;
    object_class->finalize = lrg_reel_skeleton2d_clip_finalize;
    object_class->get_property = lrg_reel_skeleton2d_clip_get_property;
    object_class->set_property = lrg_reel_skeleton2d_clip_set_property;

    clip_class->render = lrg_reel_skeleton2d_clip_render;

    /**
     * LrgReelSkeleton2DClip:skeleton:
     *
     * The skeleton drawn.
     *
     * Since: 0.14
     */
    properties[PROP_SKELETON] =
        g_param_spec_object ("skeleton", "Skeleton", "The skeleton drawn",
                             LRG_TYPE_SKELETON2D,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:animation:
     *
     * The animation played, or %NULL for the setup pose.
     *
     * Since: 0.14
     */
    properties[PROP_ANIMATION] =
        g_param_spec_object ("animation", "Animation", "The animation played",
                             LRG_TYPE_SKELETON2D_CLIP,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:animation-name:
     *
     * Name of the skeleton's clip to play (see
     * lrg_reel_skeleton2d_clip_set_animation_name()).
     *
     * Since: 0.14
     */
    properties[PROP_ANIMATION_NAME] =
        g_param_spec_string ("animation-name", "Animation name",
                             "Name of the skeleton clip to play", NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:file:
     *
     * A `.rskel2d` file to load the skeleton from (how reel YAML names
     * the skeleton).  Setting it replaces #LrgReelSkeleton2DClip:skeleton;
     * a file that does not load leaves the clip empty and logs a warning.
     *
     * Since: 0.14
     */
    properties[PROP_FILE] =
        g_param_spec_string ("file", "File", "A .rskel2d file to load the skeleton from", NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:speed:
     *
     * Playback speed: animation seconds per clip second.
     *
     * Since: 0.14
     */
    properties[PROP_SPEED] =
        g_param_spec_double ("speed", "Speed", "Animation seconds per clip second",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:loop:
     *
     * Whether the animation wraps (%TRUE) or holds its last pose.
     *
     * Since: 0.14
     */
    properties[PROP_LOOP] =
        g_param_spec_boolean ("loop", "Loop", "Whether the animation wraps", TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:origin-x:
     *
     * Canvas X of the skeleton origin in pixels.
     *
     * Since: 0.14
     */
    properties[PROP_ORIGIN_X] =
        g_param_spec_double ("origin-x", "Origin X", "Canvas X of the skeleton origin",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:origin-y:
     *
     * Canvas Y of the skeleton origin in pixels.
     *
     * Since: 0.14
     */
    properties[PROP_ORIGIN_Y] =
        g_param_spec_double ("origin-y", "Origin Y", "Canvas Y of the skeleton origin",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:skeleton-scale:
     *
     * Uniform scale of the skeleton.
     *
     * Since: 0.14
     */
    properties[PROP_SKELETON_SCALE] =
        g_param_spec_double ("skeleton-scale", "Skeleton scale", "Uniform scale of the skeleton",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelSkeleton2DClip:flip-x:
     *
     * Whether the skeleton is mirrored horizontally about its origin.
     *
     * Since: 0.14
     */
    properties[PROP_FLIP_X] =
        g_param_spec_boolean ("flip-x", "Flip X", "Mirror the skeleton horizontally", FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_skeleton2d_clip_init (LrgReelSkeleton2DClip *self)
{
    self->speed = 1.0;
    self->loop = TRUE;
    self->scale = 1.0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

LrgReelSkeleton2DClip *
lrg_reel_skeleton2d_clip_new (LrgSkeleton2D     *skeleton,
                              LrgSkeleton2DClip *animation)
{
    g_return_val_if_fail (skeleton == NULL || LRG_IS_SKELETON2D (skeleton), NULL);
    g_return_val_if_fail (animation == NULL || LRG_IS_SKELETON2D_CLIP (animation), NULL);

    return g_object_new (LRG_TYPE_REEL_SKELETON2D_CLIP,
                         "skeleton", skeleton,
                         "animation", animation,
                         NULL);
}

LrgReelSkeleton2DClip *
lrg_reel_skeleton2d_clip_new_from_file (const gchar  *path,
                                        const gchar  *animation,
                                        GError      **error)
{
    g_autoptr(LrgSkeleton2D) skel = NULL;
    LrgSkeleton2DClip       *anim = NULL;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    skel = lrg_skeleton2d_new_from_file (path, error);
    if (skel == NULL)
        return NULL;
    if (animation != NULL)
    {
        anim = lrg_skeleton2d_get_clip (skel, animation);
        if (anim == NULL)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                         "%s: no clip named '%s'", path, animation);
            return NULL;
        }
    }
    return lrg_reel_skeleton2d_clip_new (skel, anim);
}

LrgSkeleton2D *
lrg_reel_skeleton2d_clip_get_skeleton (LrgReelSkeleton2DClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self), NULL);

    return self->skeleton;
}

void
lrg_reel_skeleton2d_clip_set_skeleton (LrgReelSkeleton2DClip *self,
                                       LrgSkeleton2D         *skeleton)
{
    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));
    g_return_if_fail (skeleton == NULL || LRG_IS_SKELETON2D (skeleton));

    if (!g_set_object (&self->skeleton, skeleton))
        return;
    /* a remembered animation name is resolved against the new skeleton
     * here, never while rendering */
    if (self->animation_name != NULL)
        lrg_reel_skeleton2d_clip_set_animation (self,
            skeleton != NULL ? lrg_skeleton2d_get_clip (skeleton, self->animation_name) : NULL);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON]);
}

LrgSkeleton2DClip *
lrg_reel_skeleton2d_clip_get_animation (LrgReelSkeleton2DClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self), NULL);

    return self->animation;
}

void
lrg_reel_skeleton2d_clip_set_animation (LrgReelSkeleton2DClip *self,
                                        LrgSkeleton2DClip     *animation)
{
    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));
    g_return_if_fail (animation == NULL || LRG_IS_SKELETON2D_CLIP (animation));

    if (g_set_object (&self->animation, animation))
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION]);
}

gboolean
lrg_reel_skeleton2d_clip_set_animation_name (LrgReelSkeleton2DClip *self,
                                             const gchar           *name)
{
    LrgSkeleton2DClip *anim = NULL;

    g_return_val_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self), FALSE);

    if (name != NULL && name[0] == '\0')
        name = NULL;
    if (g_strcmp0 (self->animation_name, name) != 0)
    {
        g_free (self->animation_name);
        self->animation_name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_NAME]);
    }
    if (name == NULL)
        return TRUE;
    if (self->skeleton != NULL)
        anim = lrg_skeleton2d_get_clip (self->skeleton, name);
    lrg_reel_skeleton2d_clip_set_animation (self, anim);
    return anim != NULL;
}

gdouble
lrg_reel_skeleton2d_clip_get_speed (LrgReelSkeleton2DClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self), 1.0);

    return self->speed;
}

void
lrg_reel_skeleton2d_clip_set_speed (LrgReelSkeleton2DClip *self,
                                    gdouble                speed)
{
    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));

    if (self->speed == speed)
        return;
    self->speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED]);
}

gboolean
lrg_reel_skeleton2d_clip_get_loop (LrgReelSkeleton2DClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self), TRUE);

    return self->loop;
}

void
lrg_reel_skeleton2d_clip_set_loop (LrgReelSkeleton2DClip *self,
                                   gboolean               loop)
{
    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));

    loop = !!loop;
    if (self->loop == loop)
        return;
    self->loop = loop;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP]);
}

void
lrg_reel_skeleton2d_clip_set_placement (LrgReelSkeleton2DClip *self,
                                        gdouble                x,
                                        gdouble                y,
                                        gdouble                scale)
{
    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));

    g_object_freeze_notify (G_OBJECT (self));
    if (self->x != x)
    {
        self->x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIGIN_X]);
    }
    if (self->y != y)
    {
        self->y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIGIN_Y]);
    }
    if (self->scale != scale)
    {
        self->scale = scale;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON_SCALE]);
    }
    g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_reel_skeleton2d_clip_get_placement (LrgReelSkeleton2DClip *self,
                                        gdouble               *x,
                                        gdouble               *y,
                                        gdouble               *scale)
{
    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));

    if (x != NULL)
        *x = self->x;
    if (y != NULL)
        *y = self->y;
    if (scale != NULL)
        *scale = self->scale;
}

gboolean
lrg_reel_skeleton2d_clip_get_flip_x (LrgReelSkeleton2DClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self), FALSE);

    return self->flip_x;
}

void
lrg_reel_skeleton2d_clip_set_flip_x (LrgReelSkeleton2DClip *self,
                                     gboolean               flip_x)
{
    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));

    flip_x = !!flip_x;
    if (self->flip_x == flip_x)
        return;
    self->flip_x = flip_x;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLIP_X]);
}

void
lrg_reel_skeleton2d_clip_set_pose_func (LrgReelSkeleton2DClip     *self,
                                        LrgReelSkeleton2DPoseFunc  func,
                                        gpointer                   user_data,
                                        GDestroyNotify             destroy)
{
    GDestroyNotify old_destroy;
    gpointer       old_data;

    g_return_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self));

    /* swap first, notify after, so a destroy notifier that touches the
     * clip sees the new callback */
    old_destroy = self->pose_destroy;
    old_data = self->pose_data;
    self->pose_func = func;
    self->pose_data = user_data;
    self->pose_destroy = destroy;
    if (old_destroy != NULL)
        old_destroy (old_data);
}

gdouble
lrg_reel_skeleton2d_clip_get_animation_time (LrgReelSkeleton2DClip *self,
                                             gdouble                seconds)
{
    gdouble duration;
    gdouble t;

    g_return_val_if_fail (LRG_IS_REEL_SKELETON2D_CLIP (self), 0.0);

    if (self->animation == NULL)
        return 0.0;
    duration = lrg_skeleton2d_clip_get_duration (self->animation);
    t = seconds * self->speed;
    if (duration <= 0.0 || !isfinite (t))
        return 0.0;

    /* looping wraps into [0, duration) (backwards too); otherwise the
     * ends are held */
    if (self->loop)
    {
        t = fmod (t, duration);
        if (t < 0.0)
            t += duration;
        return t;
    }
    return CLAMP (t, 0.0, duration);
}
