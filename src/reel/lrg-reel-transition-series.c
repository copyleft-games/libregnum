/* lrg-reel-transition-series.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-transition-series.h"
#include "lrg-reel-context.h"
#include "../graphics/lrg-image-canvas.h"

/* --------------------------------------------------------------------------
 * Internal item list
 *
 * Items alternate: SEGMENT, [TRANSITION, SEGMENT]*.
 * -------------------------------------------------------------------------- */

typedef enum
{
    ITEM_SEGMENT,
    ITEM_TRANSITION
} ItemKind;

typedef struct
{
    ItemKind kind;

    /* SEGMENT fields */
    LrgReelClip *clip;       /* owned ref */
    gint         duration;   /* frames */

    /* TRANSITION fields */
    LrgReelTransition *transition; /* owned ref */
    gint               overlap;    /* frames; 0 = hard cut */
} SeriesItem;

static SeriesItem *
series_item_new_segment (LrgReelClip *clip,
                         gint         duration)
{
    SeriesItem *item;

    item = g_slice_new0 (SeriesItem);
    item->kind     = ITEM_SEGMENT;
    item->clip     = g_object_ref (clip);
    item->duration = duration;

    return item;
}

static SeriesItem *
series_item_new_transition (LrgReelTransition *transition,
                            gint               overlap)
{
    SeriesItem *item;

    item = g_slice_new0 (SeriesItem);
    item->kind       = ITEM_TRANSITION;
    item->transition = g_object_ref (transition);
    item->overlap    = overlap;

    return item;
}

static void
series_item_free (gpointer data)
{
    SeriesItem *item = (SeriesItem *) data;

    if (item->kind == ITEM_SEGMENT)
        g_clear_object (&item->clip);
    else
        g_clear_object (&item->transition);

    g_slice_free (SeriesItem, item);
}

/* --------------------------------------------------------------------------
 * Instance struct
 * -------------------------------------------------------------------------- */

struct _LrgReelTransitionSeries
{
    LrgReelClip parent_instance;

    /*
     * Ordered list of SeriesItem*.  Valid layout:
     *   SEGMENT [TRANSITION SEGMENT]*
     *
     * A trailing TRANSITION item means add_transition() was called but the
     * following add() has not yet been called (pending transition).
     */
    GPtrArray *items; /* of SeriesItem*, freed via series_item_free */

    /*
     * Reusable scratch canvases for rendering outgoing and incoming segments
     * during a transition overlap window.  Allocated lazily and reallocated
     * when frame dimensions change.
     */
    LrgImageCanvas *tmp_out;
    LrgImageCanvas *tmp_in;
    gint            tmp_w;
    gint            tmp_h;
};

G_DEFINE_FINAL_TYPE (LrgReelTransitionSeries, lrg_reel_transition_series,
                     LRG_TYPE_REEL_CLIP)

/* --------------------------------------------------------------------------
 * Timeline helpers
 * -------------------------------------------------------------------------- */

/*
 * Recompute the total frame count from the item list.
 *
 * Algorithm (per the spec):
 *
 *   pos = 0
 *   for each SEGMENT k (in order):
 *       start[k] = pos
 *       let trans = TRANSITION immediately after segment k (if any)
 *       pos += duration[k] - (trans->overlap if trans exists, else 0)
 *   total = start[last] + duration[last]
 *
 * We track pos and the last segment's duration to reconstruct total at the
 * end without storing per-segment start arrays.
 */
static guint
series_compute_total (LrgReelTransitionSeries *self)
{
    gint64 pos = 0;
    gint64 last_seg_end = 0; /* pos + duration of the last segment visited */
    guint i;

    for (i = 0; i < self->items->len; i++)
    {
        SeriesItem *item;
        gint        overlap;

        item = (SeriesItem *) g_ptr_array_index (self->items, i);
        if (item->kind != ITEM_SEGMENT)
            continue;

        last_seg_end = pos + (gint64) item->duration;

        /* Extract the overlap of the TRANSITION item immediately after, if any. */
        overlap = 0;
        if (i + 1 < self->items->len)
        {
            SeriesItem *nxt = (SeriesItem *) g_ptr_array_index (self->items, i + 1);
            if (nxt->kind == ITEM_TRANSITION)
                overlap = nxt->overlap;
        }
        pos += (gint64) item->duration - (gint64) overlap;
    }

    /* total = start[last] + duration[last] = last_seg_end */
    if (last_seg_end < 0)
        last_seg_end = 0;

    return (guint) last_seg_end;
}

static void
series_sync_duration (LrgReelTransitionSeries *self)
{
    guint total;

    total = series_compute_total (self);
    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (self), (gint) total);
}

/* --------------------------------------------------------------------------
 * Scratch canvas management
 * -------------------------------------------------------------------------- */

static void
series_ensure_scratch (LrgReelTransitionSeries *self,
                       gint                     w,
                       gint                     h)
{
    if (self->tmp_out != NULL && self->tmp_w == w && self->tmp_h == h)
        return;

    g_clear_object (&self->tmp_out);
    g_clear_object (&self->tmp_in);

    self->tmp_out = lrg_image_canvas_new (w, h, NULL);
    self->tmp_in  = lrg_image_canvas_new (w, h, NULL);
    self->tmp_w   = w;
    self->tmp_h   = h;
}

/* --------------------------------------------------------------------------
 * Render helpers
 * -------------------------------------------------------------------------- */

/*
 * Render @seg_item's clip into @target, giving the child a frame offset by
 * @seg_start from the current context frame.
 */
static void
series_render_segment_to (SeriesItem     *seg_item,
                          gint            seg_start,
                          gint            seg_dur,
                          LrgReelContext *ctx,
                          LrgImageCanvas *target)
{
    lrg_reel_context_push_offset (ctx, seg_start, seg_dur);
    lrg_reel_clip_render (seg_item->clip, ctx, target);
    lrg_reel_context_pop_offset (ctx);
}

/* --------------------------------------------------------------------------
 * Render vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_transition_series_render (LrgReelClip    *clip,
                                   LrgReelContext *ctx,
                                   LrgImageCanvas *canvas)
{
    LrgReelTransitionSeries *self = LRG_REEL_TRANSITION_SERIES (clip);
    gint f;
    gint w;
    gint h;
    gint64 pos;
    guint i;

    /* Active segment tracking. */
    gboolean        found;
    SeriesItem     *active_item;   /* the segment that owns frame f */
    gint            active_start;  /* series-relative start of active segment */

    /* Transition state (only valid when a transition follows active_item). */
    LrgReelTransition *trans_after;
    gint               overlap_start;
    gint               overlap_end;
    SeriesItem        *next_seg_item;
    gint               next_seg_start;

    if (self->items->len == 0)
        return;

    f = lrg_reel_context_get_frame (ctx);
    w = lrg_reel_context_get_width (ctx);
    h = lrg_reel_context_get_height (ctx);

    pos            = 0;
    found          = FALSE;
    active_item    = NULL;
    active_start   = 0;
    trans_after    = NULL;
    overlap_start  = 0;
    overlap_end    = 0;
    next_seg_item  = NULL;
    next_seg_start = 0;

    for (i = 0; i < self->items->len; i++)
    {
        SeriesItem *item;
        gint        seg_end;
        SeriesItem *follow_trans;
        SeriesItem *follow_seg;
        gint        overlap;
        SeriesItem *nxt;
        SeriesItem *nxt2;

        nxt  = NULL;
        nxt2 = NULL;
        item = (SeriesItem *) g_ptr_array_index (self->items, i);
        if (item->kind != ITEM_SEGMENT)
            continue;

        seg_end = (gint) pos + item->duration;

        if (f >= (gint) pos && f < seg_end)
        {
            found        = TRUE;
            active_item  = item;
            active_start = (gint) pos;

            /* Look for a TRANSITION item immediately after this segment. */
            follow_trans = NULL;
            follow_seg   = NULL;

            if (i + 1 < self->items->len)
            {
                nxt = (SeriesItem *) g_ptr_array_index (self->items, i + 1);
                if (nxt->kind == ITEM_TRANSITION)
                {
                    follow_trans = nxt;
                    if (i + 2 < self->items->len)
                    {
                        nxt2 = (SeriesItem *) g_ptr_array_index (self->items, i + 2);
                        if (nxt2->kind == ITEM_SEGMENT)
                            follow_seg = nxt2;
                    }
                }
            }

            if (follow_trans != NULL && follow_trans->overlap > 0 &&
                follow_seg != NULL)
            {
                overlap = follow_trans->overlap;

                /*
                 * The overlap window is the last @overlap frames of the active
                 * segment, which coincides with the first @overlap frames of
                 * the incoming segment:
                 *
                 *   overlap_end   = active_start + active_item->duration
                 *   overlap_start = overlap_end - overlap
                 *   next_seg_start = overlap_start
                 */
                overlap_end    = active_start + active_item->duration;
                overlap_start  = overlap_end - overlap;
                next_seg_start = overlap_start;
                trans_after    = follow_trans->transition;
                next_seg_item  = follow_seg;
            }

            break;
        }

        /* Advance cursor: subtract the transition overlap (if any) so the next
         * segment's start is pulled earlier. */
        overlap = 0;
        if (i + 1 < self->items->len)
        {
            nxt = (SeriesItem *) g_ptr_array_index (self->items, i + 1);
            if (nxt->kind == ITEM_TRANSITION)
                overlap = nxt->overlap;
        }
        pos += (gint64) item->duration - (gint64) overlap;
    }

    if (!found)
    {
        /*
         * @f is before the first segment or past the last.  Per spec, do
         * nothing (caller is responsible for clamping if desired).
         */
        return;
    }

    /* Is @f inside the transition overlap window? */
    if (trans_after != NULL && f >= overlap_start && f < overlap_end)
    {
        gint    overlap_len;
        gdouble progress;
        GrlColor transparent;
        GrlImage *out_img;
        GrlImage *in_img;

        overlap_len = overlap_end - overlap_start;

        if (overlap_len <= 0)
        {
            /* Degenerate zero-length overlap: hard cut to incoming segment. */
            series_render_segment_to (next_seg_item,
                                      next_seg_start,
                                      next_seg_item->duration,
                                      ctx, canvas);
            return;
        }

        progress = (gdouble)(f - overlap_start) / (gdouble) overlap_len;
        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;

        series_ensure_scratch (self, w, h);

        /* Clear both scratch buffers to fully transparent. */
        transparent.r = 0;
        transparent.g = 0;
        transparent.b = 0;
        transparent.a = 0;
        lrg_image_canvas_clear (self->tmp_out, &transparent);
        lrg_image_canvas_clear (self->tmp_in,  &transparent);

        /* Render outgoing segment into tmp_out. */
        series_render_segment_to (active_item,
                                  active_start,
                                  active_item->duration,
                                  ctx, self->tmp_out);

        /* Render incoming segment into tmp_in. */
        series_render_segment_to (next_seg_item,
                                  next_seg_start,
                                  next_seg_item->duration,
                                  ctx, self->tmp_in);

        out_img = lrg_image_canvas_get_image (self->tmp_out);
        in_img  = lrg_image_canvas_get_image (self->tmp_in);

        lrg_reel_transition_composite (trans_after, canvas,
                                       out_img, in_img, progress);
        return;
    }

    /* Solo case: render the active segment directly onto @canvas. */
    series_render_segment_to (active_item,
                              active_start,
                              active_item->duration,
                              ctx, canvas);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_transition_series_finalize (GObject *object)
{
    LrgReelTransitionSeries *self = LRG_REEL_TRANSITION_SERIES (object);

    g_clear_pointer (&self->items, g_ptr_array_unref);
    g_clear_object (&self->tmp_out);
    g_clear_object (&self->tmp_in);

    G_OBJECT_CLASS (lrg_reel_transition_series_parent_class)->finalize (object);
}

static void
lrg_reel_transition_series_class_init (LrgReelTransitionSeriesClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class   = LRG_REEL_CLIP_CLASS (klass);

    object_class->finalize = lrg_reel_transition_series_finalize;
    clip_class->render     = lrg_reel_transition_series_render;
}

static void
lrg_reel_transition_series_init (LrgReelTransitionSeries *self)
{
    self->items   = g_ptr_array_new_with_free_func (series_item_free);
    self->tmp_out = NULL;
    self->tmp_in  = NULL;
    self->tmp_w   = 0;
    self->tmp_h   = 0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_transition_series_new:
 *
 * Creates a new, empty #LrgReelTransitionSeries.
 *
 * Returns: (transfer full): a new #LrgReelTransitionSeries
 *
 * Since: 1.0
 */
LrgReelTransitionSeries *
lrg_reel_transition_series_new (void)
{
    return g_object_new (LRG_TYPE_REEL_TRANSITION_SERIES, NULL);
}

/**
 * lrg_reel_transition_series_add:
 * @self: an #LrgReelTransitionSeries
 * @clip: (transfer none): the #LrgReelClip to append as the next segment.
 * @duration_in_frames: how many frames this segment occupies (> 0).
 *
 * Appends @clip as a new segment.  If a pending transition was registered by a
 * preceding lrg_reel_transition_series_add_transition() call, it is now
 * associated with the previous segment and this new segment as their crossfade.
 *
 * Since: 1.0
 */
void
lrg_reel_transition_series_add (LrgReelTransitionSeries *self,
                                LrgReelClip             *clip,
                                gint                     duration_in_frames)
{
    SeriesItem *seg;

    g_return_if_fail (LRG_IS_REEL_TRANSITION_SERIES (self));
    g_return_if_fail (LRG_IS_REEL_CLIP (clip));
    g_return_if_fail (duration_in_frames > 0);

    seg = series_item_new_segment (clip, duration_in_frames);
    g_ptr_array_add (self->items, seg);

    series_sync_duration (self);
}

/**
 * lrg_reel_transition_series_add_transition:
 * @self: an #LrgReelTransitionSeries
 * @transition: (transfer none): the #LrgReelTransition to interpose.
 * @duration_in_frames: overlap length in frames (>= 0).
 *
 * Registers a transition between the previously appended segment and the next
 * one.  Must be called after at least one add() and before the next add().
 *
 * Since: 1.0
 */
void
lrg_reel_transition_series_add_transition (LrgReelTransitionSeries *self,
                                           LrgReelTransition       *transition,
                                           gint                     duration_in_frames)
{
    SeriesItem *trans_item;

    g_return_if_fail (LRG_IS_REEL_TRANSITION_SERIES (self));
    g_return_if_fail (LRG_IS_REEL_TRANSITION (transition));
    g_return_if_fail (duration_in_frames >= 0);

    trans_item = series_item_new_transition (transition, duration_in_frames);
    g_ptr_array_add (self->items, trans_item);

    /* The series duration does not change until the next add() call. */
}

/**
 * lrg_reel_transition_series_get_total_frames:
 * @self: an #LrgReelTransitionSeries
 *
 * Returns the total duration of the series in frames, accounting for all
 * segment durations and transition overlaps.  The inherited clip duration is
 * kept in sync with this value.
 *
 * Returns: total frame count (0 for an empty series)
 *
 * Since: 1.0
 */
guint
lrg_reel_transition_series_get_total_frames (LrgReelTransitionSeries *self)
{
    guint total;

    g_return_val_if_fail (LRG_IS_REEL_TRANSITION_SERIES (self), 0);

    total = series_compute_total (self);

    /* Keep the inherited clip duration in sync (idempotent if unchanged). */
    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (self), (gint) total);

    return total;
}
