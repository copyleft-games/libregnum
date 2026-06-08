/* lrg-reel-context-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal compositor scratch pool carried on the render context.  Shared
 * between the renderer, the clip dispatcher, and sequences so that any clip can
 * be composited (transform / opacity / blend / effects) wherever it is drawn,
 * including deeply nested ones.  Not part of the public API.
 */

#pragma once

#include "lrg-reel-context.h"
#include <graylib.h>
#include "../graphics/lrg-image-canvas.h"

G_BEGIN_DECLS

/**
 * LrgReelScratch:
 * @layer: an off-screen #GrlLayer sized to the frame.
 * @canvas: an #LrgImageCanvas wrapping the layer's backing image.
 * @in_use: whether this entry is currently checked out.
 *
 * One reusable compositing scratch buffer.
 */
typedef struct _LrgReelScratch
{
    GrlLayer       *layer;
    LrgImageCanvas *canvas;
    gboolean        in_use;
} LrgReelScratch;

/*
 * Acquire a cleared (fully transparent), identity-transform scratch buffer
 * sized to the context's frame.  Returns %NULL only on allocation failure or if
 * the frame has no positive size.  Release with lrg_reel_context_release_scratch().
 */
LrgReelScratch *
lrg_reel_context_acquire_scratch (LrgReelContext *self);

void
lrg_reel_context_release_scratch (LrgReelContext *self,
                                  LrgReelScratch *scratch);

G_END_DECLS
