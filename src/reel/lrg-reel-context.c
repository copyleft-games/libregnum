/* lrg-reel-context.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-context.h"
#include "lrg-reel-context-private.h"

typedef struct
{
    gint cum_offset;  /* cumulative offset at this level */
    gint duration;    /* window length, or LRG_REEL_DURATION_INFINITE */
} LrgReelStackEntry;

struct _LrgReelContext
{
    GObject parent_instance;

    gint    absolute_frame;
    gdouble subframe;        /* [0,1) fractional offset, for motion blur */
    gdouble fps;
    gint    width;
    gint    height;
    gint    duration_in_frames;

    GArray    *stack;         /* of LrgReelStackEntry */
    GPtrArray *scratch_pool;  /* of LrgReelScratch*, lazily created */
};

static void
reel_scratch_free (gpointer data)
{
    LrgReelScratch *s = data;

    g_clear_object (&s->canvas);
    g_clear_pointer (&s->layer, grl_layer_unref);
    g_free (s);
}

G_DEFINE_FINAL_TYPE (LrgReelContext, lrg_reel_context, G_TYPE_OBJECT)

/* A duration is "infinite" if it is the sentinel or negative (open-ended). */
static gboolean
reel_duration_is_infinite (gint duration)
{
    return duration < 0 || duration >= LRG_REEL_DURATION_INFINITE;
}

static void
lrg_reel_context_finalize (GObject *object)
{
    LrgReelContext *self = LRG_REEL_CONTEXT (object);

    g_clear_pointer (&self->stack, g_array_unref);
    g_clear_pointer (&self->scratch_pool, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_reel_context_parent_class)->finalize (object);
}

static void
lrg_reel_context_class_init (LrgReelContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_reel_context_finalize;
}

static void
lrg_reel_context_init (LrgReelContext *self)
{
    self->stack = g_array_new (FALSE, FALSE, sizeof (LrgReelStackEntry));
}

LrgReelContext *
lrg_reel_context_new (gint    absolute_frame,
                      gdouble fps,
                      gint    width,
                      gint    height,
                      gint    duration_in_frames)
{
    LrgReelContext *self;

    self = g_object_new (LRG_TYPE_REEL_CONTEXT, NULL);
    self->absolute_frame = absolute_frame;
    self->fps = (fps > 0.0) ? fps : 60.0;
    self->width = width;
    self->height = height;
    self->duration_in_frames = duration_in_frames;

    return self;
}

/* Sum of cumulative offset currently on top of the stack (0 if empty). */
static gint
reel_context_top_offset (LrgReelContext *self)
{
    if (self->stack->len == 0)
        return 0;

    return g_array_index (self->stack, LrgReelStackEntry,
                          self->stack->len - 1).cum_offset;
}

gint
lrg_reel_context_get_frame (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0);

    return self->absolute_frame - reel_context_top_offset (self);
}

gint
lrg_reel_context_get_absolute_frame (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0);

    return self->absolute_frame;
}

void
lrg_reel_context_set_absolute_frame (LrgReelContext *self,
                                     gint            absolute_frame)
{
    g_return_if_fail (LRG_IS_REEL_CONTEXT (self));

    self->absolute_frame = absolute_frame;
    if (self->stack->len > 0)
        g_array_set_size (self->stack, 0);
}

gdouble
lrg_reel_context_get_fps (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0.0);

    return self->fps;
}

gdouble
lrg_reel_context_get_seconds (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0.0);

    return ((gdouble) lrg_reel_context_get_frame (self) + self->subframe) / self->fps;
}

/**
 * lrg_reel_context_set_subframe:
 * @self: an #LrgReelContext
 * @subframe: a fractional in-frame offset in [0,1).
 *
 * Sets the sub-frame phase used by lrg_reel_context_get_seconds() — the renderer
 * sweeps this across a frame's exposure window to accumulate motion blur.
 * Time-based animations (driven by get_seconds) follow it; integer-frame
 * lookups (get_frame) do not.
 *
 * Since: 1.0
 */
void
lrg_reel_context_set_subframe (LrgReelContext *self,
                               gdouble         subframe)
{
    g_return_if_fail (LRG_IS_REEL_CONTEXT (self));

    self->subframe = subframe;
}

/**
 * lrg_reel_context_get_frame_exact:
 * @self: an #LrgReelContext
 *
 * Returns: the clip-relative frame plus the current sub-frame phase
 *
 * Since: 1.0
 */
gdouble
lrg_reel_context_get_frame_exact (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0.0);

    return (gdouble) lrg_reel_context_get_frame (self) + self->subframe;
}

gint
lrg_reel_context_get_width (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0);

    return self->width;
}

gint
lrg_reel_context_get_height (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0);

    return self->height;
}

gint
lrg_reel_context_get_duration_in_frames (LrgReelContext *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), 0);

    return self->duration_in_frames;
}

void
lrg_reel_context_push_offset (LrgReelContext *self,
                              gint            from,
                              gint            duration_in_frames)
{
    LrgReelStackEntry entry;

    g_return_if_fail (LRG_IS_REEL_CONTEXT (self));

    entry.cum_offset = reel_context_top_offset (self) + from;
    entry.duration = duration_in_frames;
    g_array_append_val (self->stack, entry);
}

void
lrg_reel_context_pop_offset (LrgReelContext *self)
{
    g_return_if_fail (LRG_IS_REEL_CONTEXT (self));

    if (self->stack->len == 0)
    {
        g_warning ("lrg_reel_context_pop_offset: stack underflow");
        return;
    }

    g_array_set_size (self->stack, self->stack->len - 1);
}

gboolean
lrg_reel_context_is_active (LrgReelContext *self)
{
    LrgReelStackEntry entry;
    gint relative;

    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), FALSE);

    if (self->stack->len == 0)
        return TRUE;

    entry = g_array_index (self->stack, LrgReelStackEntry, self->stack->len - 1);
    relative = self->absolute_frame - entry.cum_offset;

    if (relative < 0)
        return FALSE;

    if (reel_duration_is_infinite (entry.duration))
        return TRUE;

    return relative < entry.duration;
}

gboolean
lrg_reel_context_test_window (LrgReelContext *self,
                              gint            from,
                              gint            duration_in_frames)
{
    gint relative;

    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), FALSE);

    relative = lrg_reel_context_get_frame (self) - from;

    if (relative < 0)
        return FALSE;

    if (reel_duration_is_infinite (duration_in_frames))
        return TRUE;

    return relative < duration_in_frames;
}

/* ==========================================================================
 * Internal compositor scratch pool (see lrg-reel-context-private.h)
 * ========================================================================== */

LrgReelScratch *
lrg_reel_context_acquire_scratch (LrgReelContext *self)
{
    GrlColor        transparent = { 0, 0, 0, 0 };
    LrgReelScratch *scratch;
    guint           i;

    g_return_val_if_fail (LRG_IS_REEL_CONTEXT (self), NULL);

    if (self->width <= 0 || self->height <= 0)
        return NULL;

    if (self->scratch_pool == NULL)
        self->scratch_pool = g_ptr_array_new_with_free_func (reel_scratch_free);

    scratch = NULL;
    for (i = 0; i < self->scratch_pool->len; i++)
    {
        LrgReelScratch *s = g_ptr_array_index (self->scratch_pool, i);

        if (!s->in_use)
        {
            scratch = s;
            break;
        }
    }

    if (scratch == NULL)
    {
        scratch = g_new0 (LrgReelScratch, 1);
        scratch->layer = grl_layer_new (self->width, self->height);
        scratch->canvas =
            lrg_image_canvas_new_for_image (grl_layer_get_image (scratch->layer));
        g_ptr_array_add (self->scratch_pool, scratch);
    }

    scratch->in_use = TRUE;
    lrg_image_canvas_clear (scratch->canvas, &transparent);
    lrg_image_canvas_reset_transform (scratch->canvas);

    return scratch;
}

void
lrg_reel_context_release_scratch (LrgReelContext *self,
                                  LrgReelScratch *scratch)
{
    g_return_if_fail (LRG_IS_REEL_CONTEXT (self));

    if (scratch != NULL)
        scratch->in_use = FALSE;
}
