/* lrg-reel-sequence.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-sequence.h"
#include "lrg-reel-context.h"

struct _LrgReelSequence
{
    LrgReelClip parent_instance;

    LrgReelSequenceMode mode;
    gint loop_frames;   /* LOOP: period */
    gint loop_times;    /* LOOP: iterations, <= 0 means infinite */
    gint frozen_frame;  /* FREEZE: locked frame */

    GPtrArray *children;  /* of LrgReelClip*, owns refs */
};

G_DEFINE_FINAL_TYPE (LrgReelSequence, lrg_reel_sequence, LRG_TYPE_REEL_CLIP)

static gboolean
reel_duration_is_infinite (gint duration)
{
    return duration < 0 || duration >= LRG_REEL_DURATION_INFINITE;
}

/*
 * Render one child: push its (from, duration) window, render it if the current
 * relative frame lands inside, then pop.  This is the single, uniform timing
 * primitive — the caller of any clip's render pushes that clip's window first.
 */
static void
reel_sequence_render_child (LrgReelClip    *child,
                            gint            from,
                            gint            duration,
                            LrgReelContext *ctx,
                            LrgImageCanvas *canvas)
{
    if (!lrg_reel_clip_get_visible (child))
        return;

    lrg_reel_context_push_offset (ctx, from, duration);
    if (lrg_reel_context_is_active (ctx))
        lrg_reel_clip_render (child, ctx, canvas);
    lrg_reel_context_pop_offset (ctx);
}

static void
reel_sequence_render_shift (LrgReelSequence *self,
                            LrgReelContext  *ctx,
                            LrgImageCanvas  *canvas)
{
    guint i;

    for (i = 0; i < self->children->len; i++)
    {
        LrgReelClip *child = g_ptr_array_index (self->children, i);

        reel_sequence_render_child (child,
                                    lrg_reel_clip_get_from_frame (child),
                                    lrg_reel_clip_get_duration_in_frames (child),
                                    ctx, canvas);
    }
}

static void
reel_sequence_render_series (LrgReelSequence *self,
                             LrgReelContext  *ctx,
                             LrgImageCanvas  *canvas)
{
    gint64 cursor = 0;
    guint i;

    for (i = 0; i < self->children->len; i++)
    {
        LrgReelClip *child = g_ptr_array_index (self->children, i);
        gint dur = lrg_reel_clip_get_duration_in_frames (child);
        gint from = (cursor >= G_MAXINT) ? G_MAXINT : (gint) cursor;

        /* In series mode a child occupies its own duration as a back-to-back
         * slot; its own from-frame is ignored. */
        reel_sequence_render_child (child, from, dur, ctx, canvas);

        if (reel_duration_is_infinite (dur))
            cursor = G_MAXINT; /* everything after an open-ended child is unreachable */
        else
            cursor += dur;

        if (cursor > G_MAXINT)
            cursor = G_MAXINT;
    }
}

static void
reel_sequence_render_loop (LrgReelSequence *self,
                           LrgReelContext  *ctx,
                           LrgImageCanvas  *canvas)
{
    gint r;
    gint period;
    gint local;
    guint i;

    period = (self->loop_frames > 0) ? self->loop_frames : 1;
    r = lrg_reel_context_get_frame (ctx);

    /* Floor-mod so a negative r (from an outer offset) still maps correctly. */
    local = ((r % period) + period) % period;

    /* Re-base the children to the start of the current iteration: after this
     * push, get_frame() returns 'local'. */
    lrg_reel_context_push_offset (ctx, r - local, period);
    for (i = 0; i < self->children->len; i++)
    {
        LrgReelClip *child = g_ptr_array_index (self->children, i);

        reel_sequence_render_child (child,
                                    lrg_reel_clip_get_from_frame (child),
                                    lrg_reel_clip_get_duration_in_frames (child),
                                    ctx, canvas);
    }
    lrg_reel_context_pop_offset (ctx);
}

static void
reel_sequence_render_freeze (LrgReelSequence *self,
                             LrgReelContext  *ctx,
                             LrgImageCanvas  *canvas)
{
    gint r;
    guint i;

    r = lrg_reel_context_get_frame (ctx);

    /* After this push, get_frame() always returns frozen_frame. */
    lrg_reel_context_push_offset (ctx, r - self->frozen_frame,
                                  LRG_REEL_DURATION_INFINITE);
    for (i = 0; i < self->children->len; i++)
    {
        LrgReelClip *child = g_ptr_array_index (self->children, i);

        reel_sequence_render_child (child,
                                    lrg_reel_clip_get_from_frame (child),
                                    lrg_reel_clip_get_duration_in_frames (child),
                                    ctx, canvas);
    }
    lrg_reel_context_pop_offset (ctx);
}

static void
lrg_reel_sequence_render (LrgReelClip    *clip,
                          LrgReelContext *ctx,
                          LrgImageCanvas *canvas)
{
    LrgReelSequence *self = LRG_REEL_SEQUENCE (clip);

    /* On entry the caller has already pushed this sequence's own
     * (from, duration) window, so get_frame() is sequence-relative. */
    switch (self->mode)
    {
    case LRG_REEL_SEQUENCE_MODE_SERIES:
        reel_sequence_render_series (self, ctx, canvas);
        break;
    case LRG_REEL_SEQUENCE_MODE_LOOP:
        reel_sequence_render_loop (self, ctx, canvas);
        break;
    case LRG_REEL_SEQUENCE_MODE_FREEZE:
        reel_sequence_render_freeze (self, ctx, canvas);
        break;
    case LRG_REEL_SEQUENCE_MODE_SHIFT:
    default:
        reel_sequence_render_shift (self, ctx, canvas);
        break;
    }
}

static void
lrg_reel_sequence_finalize (GObject *object)
{
    LrgReelSequence *self = LRG_REEL_SEQUENCE (object);

    g_clear_pointer (&self->children, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_reel_sequence_parent_class)->finalize (object);
}

static void
lrg_reel_sequence_class_init (LrgReelSequenceClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class = LRG_REEL_CLIP_CLASS (klass);

    object_class->finalize = lrg_reel_sequence_finalize;
    clip_class->render = lrg_reel_sequence_render;
}

static void
lrg_reel_sequence_init (LrgReelSequence *self)
{
    self->mode = LRG_REEL_SEQUENCE_MODE_SHIFT;
    self->loop_frames = 1;
    self->loop_times = 0;
    self->frozen_frame = 0;
    self->children = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgReelSequence *
lrg_reel_sequence_new (gint from,
                       gint duration_in_frames)
{
    LrgReelSequence *self;

    self = g_object_new (LRG_TYPE_REEL_SEQUENCE, NULL);
    self->mode = LRG_REEL_SEQUENCE_MODE_SHIFT;
    lrg_reel_clip_set_from_frame (LRG_REEL_CLIP (self), from);
    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (self), duration_in_frames);

    return self;
}

LrgReelSequence *
lrg_reel_sequence_new_series (void)
{
    LrgReelSequence *self;

    self = g_object_new (LRG_TYPE_REEL_SEQUENCE, NULL);
    self->mode = LRG_REEL_SEQUENCE_MODE_SERIES;
    lrg_reel_clip_set_from_frame (LRG_REEL_CLIP (self), 0);
    /* No children yet: zero-length window (inactive) until children are added. */
    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (self), 0);

    return self;
}

LrgReelSequence *
lrg_reel_sequence_new_loop (gint loop_frames,
                            gint times)
{
    LrgReelSequence *self;
    gint duration;

    g_return_val_if_fail (loop_frames > 0, NULL);

    self = g_object_new (LRG_TYPE_REEL_SEQUENCE, NULL);
    self->mode = LRG_REEL_SEQUENCE_MODE_LOOP;
    self->loop_frames = loop_frames;
    self->loop_times = times;

    if (times <= 0)
    {
        duration = LRG_REEL_DURATION_INFINITE;
    }
    else
    {
        gint64 total = (gint64) times * (gint64) loop_frames;
        duration = (total >= LRG_REEL_DURATION_INFINITE)
                       ? LRG_REEL_DURATION_INFINITE
                       : (gint) total;
    }

    lrg_reel_clip_set_from_frame (LRG_REEL_CLIP (self), 0);
    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (self), duration);

    return self;
}

LrgReelSequence *
lrg_reel_sequence_new_freeze (gint frozen_frame)
{
    LrgReelSequence *self;

    self = g_object_new (LRG_TYPE_REEL_SEQUENCE, NULL);
    self->mode = LRG_REEL_SEQUENCE_MODE_FREEZE;
    self->frozen_frame = frozen_frame;
    lrg_reel_clip_set_from_frame (LRG_REEL_CLIP (self), 0);
    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (self),
                                          LRG_REEL_DURATION_INFINITE);

    return self;
}

static void
reel_sequence_recompute_series_duration (LrgReelSequence *self)
{
    gint64 total = 0;
    gboolean infinite = FALSE;
    guint i;

    for (i = 0; i < self->children->len; i++)
    {
        LrgReelClip *child = g_ptr_array_index (self->children, i);
        gint dur = lrg_reel_clip_get_duration_in_frames (child);

        if (reel_duration_is_infinite (dur))
        {
            infinite = TRUE;
            break;
        }
        total += dur;
        if (total >= LRG_REEL_DURATION_INFINITE)
        {
            infinite = TRUE;
            break;
        }
    }

    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (self),
                                          infinite ? LRG_REEL_DURATION_INFINITE
                                                   : (gint) total);
}

void
lrg_reel_sequence_add_child (LrgReelSequence *self,
                             LrgReelClip     *child)
{
    g_return_if_fail (LRG_IS_REEL_SEQUENCE (self));
    g_return_if_fail (LRG_IS_REEL_CLIP (child));

    g_ptr_array_add (self->children, g_object_ref (child));

    if (self->mode == LRG_REEL_SEQUENCE_MODE_SERIES)
        reel_sequence_recompute_series_duration (self);
}

LrgReelSequenceMode
lrg_reel_sequence_get_mode (LrgReelSequence *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SEQUENCE (self), LRG_REEL_SEQUENCE_MODE_SHIFT);
    return self->mode;
}

guint
lrg_reel_sequence_get_n_children (LrgReelSequence *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SEQUENCE (self), 0);
    return self->children->len;
}
