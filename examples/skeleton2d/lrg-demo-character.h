/* lrg-demo-character.h - a cartoon head-and-shoulders character built in code
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared by the skeleton2d examples (skeleton2d-talking-head,
 * skeleton2d-shape-sheet) and tests/test-skeleton2d-character.c.  The
 * character implements every shape of the standard shape sheet
 * (lrg-shape-sheet.h) on five mesh slots, with textures painted into
 * GrlImages at build time: no image files, no window.
 */

#pragma once

#include <libregnum.h>

G_BEGIN_DECLS

/* slot names, back to front */
#define LRG_DEMO_SLOT_BODY  "body"
#define LRG_DEMO_SLOT_HEAD  "head"
#define LRG_DEMO_SLOT_EYES  "eyes"
#define LRG_DEMO_SLOT_BROWS "brows"
#define LRG_DEMO_SLOT_MOUTH "mouth"

/* bone names */
#define LRG_DEMO_BONE_ROOT  "root"
#define LRG_DEMO_BONE_BODY  "body"
#define LRG_DEMO_BONE_HEAD  "head"

/* the character's extent in setup space (y down, origin between the
 * feet at the bottom edge of the shirt) */
#define LRG_DEMO_CHARACTER_TOP    (-450.0f)
#define LRG_DEMO_CHARACTER_HALF_W (160.0f)

/* the centre of the face in setup space (the head bone pivots at the
 * neck, below it) */
#define LRG_DEMO_FACE_X (0.0f)
#define LRG_DEMO_FACE_Y (-300.0f)

/**
 * LrgDemoRegion:
 * @LRG_DEMO_REGION_MOUTH: the mouth with room for its widest shapes
 * @LRG_DEMO_REGION_EYE_LEFT: the character's left eye (screen right)
 * @LRG_DEMO_REGION_EYE_RIGHT: the character's right eye (screen left)
 * @LRG_DEMO_REGION_FACE: the whole head
 *
 * Feature boxes, for tests and framing.
 */
typedef enum
{
	LRG_DEMO_REGION_MOUTH,
	LRG_DEMO_REGION_EYE_LEFT,
	LRG_DEMO_REGION_EYE_RIGHT,
	LRG_DEMO_REGION_FACE
} LrgDemoRegion;

/**
 * lrg_demo_character_new:
 *
 * Builds the character: bones root, body and head; slots body, head,
 * eyes, brows and mouth (meshes with generated CPU source images); and
 * every shape of the standard shape sheet (mouth visemes on the mouth,
 * blinks, gaze, squint and wide on the eyes, brow poses on the brows,
 * moods on eyes, brows and mouth, squash and stretch on every slot,
 * breathe on the body).
 *
 * Returns: (transfer full): the character
 */
LrgSkeleton2D *lrg_demo_character_new (void);

/**
 * lrg_demo_character_set_shape:
 * @skel: the character (or any skeleton)
 * @shape: a shape name
 * @weight: the weight, 0 .. 1
 *
 * Sets @shape on every slot whose attachment implements it, so one call
 * drives a mood that spans eyes, brows and mouth.
 *
 * Returns: the number of slots set
 */
guint lrg_demo_character_set_shape (LrgSkeleton2D *skel,
                                    const gchar   *shape,
                                    gfloat         weight);

/**
 * lrg_demo_character_key_shape:
 * @skel: the character (or any skeleton)
 * @clip: the clip to key
 * @shape: a shape name
 * @time: key time in seconds
 * @weight: key weight, 0 .. 1
 *
 * Adds a linear @shape key at @time on every slot whose attachment
 * implements @shape.
 *
 * Returns: the number of keys added
 */
guint lrg_demo_character_key_shape (LrgSkeleton2D     *skel,
                                    LrgSkeleton2DClip *clip,
                                    const gchar       *shape,
                                    gfloat             time,
                                    gfloat             weight);

/**
 * lrg_demo_character_get_region:
 * @region: which feature
 * @x: (out): left edge in setup space
 * @y: (out): top edge in setup space
 * @width: (out): width
 * @height: (out): height
 *
 * The setup-space box of a feature with the head unturned.  A frame
 * drawn with the skeleton's root at (ox, oy) and scale s shows it at
 * (ox + s * x, oy + s * y), s * width by s * height.
 */
void lrg_demo_character_get_region (LrgDemoRegion  region,
                                    gfloat        *x,
                                    gfloat        *y,
                                    gfloat        *width,
                                    gfloat        *height);

G_END_DECLS
