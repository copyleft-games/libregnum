/* lrg-skeleton2d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2D: 2D skeletal animation.  A skeleton is a hierarchy of 2D
 * bones (translation, rotation in degrees, non-uniform scale, length),
 * slots (draw-order entries bound to a bone that show one attachment at
 * a time, tinted by a colour), skins (named sets of attachments, each an
 * #LrgSkinnedSprite mesh) and clips (#LrgSkeleton2DClip: bone TRS,
 * slot attachment and colour keys, draw-order keys and events).  It
 * loads from and saves to the `.rskel2d` YAML format.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../graphics/lrg-skinned-sprite.h"

G_BEGIN_DECLS

/**
 * LRG_SKELETON2D_FORMAT_VERSION:
 *
 * The `.rskel2d` format version this library reads and writes (the value
 * of the root `rskel2d:` key).
 */
#define LRG_SKELETON2D_FORMAT_VERSION (1)

/**
 * LRG_SKELETON2D_DEFAULT_SKIN:
 *
 * Name of the skin every skeleton has.  Attachments not found in the
 * active skin are looked up here.
 */
#define LRG_SKELETON2D_DEFAULT_SKIN "default"

/* ------------------------------------------------------------------ */
/* Enumerations                                                        */
/* ------------------------------------------------------------------ */

/**
 * LRG_SKELETON2D_ERROR:
 *
 * Error domain of #LrgSkeleton2D.  Errors are #LrgSkeleton2DError
 * values.
 */
#define LRG_SKELETON2D_ERROR (lrg_skeleton2d_error_quark ())

/**
 * lrg_skeleton2d_error_quark:
 *
 * The error quark of #LRG_SKELETON2D_ERROR.
 *
 * Returns: the quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_skeleton2d_error_quark (void);

/**
 * LrgSkeleton2DError:
 * @LRG_SKELETON2D_ERROR_PARSE: the document is not YAML or a value has
 *   the wrong type or shape
 * @LRG_SKELETON2D_ERROR_INVALID: the document is well formed but wrong:
 *   an unknown key, a missing required key, a reference to an unknown
 *   bone or slot, a duplicate name, an index out of range
 * @LRG_SKELETON2D_ERROR_VERSION: the `rskel2d:` version is missing or
 *   not supported
 * @LRG_SKELETON2D_ERROR_IO: a file could not be read or written
 *
 * Errors reported by #LrgSkeleton2D.
 */
typedef enum
{
	LRG_SKELETON2D_ERROR_PARSE,
	LRG_SKELETON2D_ERROR_INVALID,
	LRG_SKELETON2D_ERROR_VERSION,
	LRG_SKELETON2D_ERROR_IO
} LrgSkeleton2DError;

LRG_AVAILABLE_IN_ALL
GType lrg_skeleton2d_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SKELETON2D_ERROR (lrg_skeleton2d_error_get_type ())

/**
 * LrgSkeleton2DCurve:
 * @LRG_SKELETON2D_CURVE_LINEAR: interpolate linearly to the next key
 * @LRG_SKELETON2D_CURVE_STEPPED: hold this key's value until the next key
 * @LRG_SKELETON2D_CURVE_BEZIER: ease to the next key along a cubic
 *   Bézier timing curve given by two control points (like CSS
 *   `cubic-bezier(x1, y1, x2, y2)`)
 *
 * How a key interpolates toward the following key.
 */
typedef enum
{
	LRG_SKELETON2D_CURVE_LINEAR,
	LRG_SKELETON2D_CURVE_STEPPED,
	LRG_SKELETON2D_CURVE_BEZIER
} LrgSkeleton2DCurve;

LRG_AVAILABLE_IN_ALL
GType lrg_skeleton2d_curve_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SKELETON2D_CURVE (lrg_skeleton2d_curve_get_type ())

/**
 * LrgSkeleton2DChannel:
 * @LRG_SKELETON2D_CHANNEL_TRANSLATE: bone translation offset from the
 *   setup pose (two values: x, y)
 * @LRG_SKELETON2D_CHANNEL_ROTATE: bone rotation offset from the setup
 *   pose in degrees (one value)
 * @LRG_SKELETON2D_CHANNEL_SCALE: bone scale factor multiplied into the
 *   setup scale (two values: x, y)
 * @LRG_SKELETON2D_CHANNEL_COLOR: slot colour, absolute (four values:
 *   r, g, b, a in 0 .. 1)
 * @LRG_SKELETON2D_CHANNEL_ATTACHMENT: slot attachment name (stepped; no
 *   values)
 *
 * The animatable channels of a clip.  The first three target bones, the
 * last two target slots.
 */
typedef enum
{
	LRG_SKELETON2D_CHANNEL_TRANSLATE,
	LRG_SKELETON2D_CHANNEL_ROTATE,
	LRG_SKELETON2D_CHANNEL_SCALE,
	LRG_SKELETON2D_CHANNEL_COLOR,
	LRG_SKELETON2D_CHANNEL_ATTACHMENT
} LrgSkeleton2DChannel;

LRG_AVAILABLE_IN_ALL
GType lrg_skeleton2d_channel_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SKELETON2D_CHANNEL (lrg_skeleton2d_channel_get_type ())

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

#define LRG_TYPE_SKELETON2D (lrg_skeleton2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkeleton2D, lrg_skeleton2d, LRG, SKELETON2D, GObject)

#define LRG_TYPE_SKELETON2D_CLIP (lrg_skeleton2d_clip_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkeleton2DClip, lrg_skeleton2d_clip, LRG, SKELETON2D_CLIP, GObject)

/* ------------------------------------------------------------------ */
/* LrgSkeleton2DClip                                                   */
/* ------------------------------------------------------------------ */

/**
 * lrg_skeleton2d_clip_new:
 * @name: the clip name (unique within a skeleton)
 *
 * Creates an empty clip of duration 0.  Keys refer to bones and slots by
 * name, so one clip can play on any skeleton that has those names.
 *
 * Returns: (transfer full): a new #LrgSkeleton2DClip
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DClip * lrg_skeleton2d_clip_new (const gchar *name);

/**
 * lrg_skeleton2d_clip_get_name:
 * @self: an #LrgSkeleton2DClip
 *
 * Returns: the clip name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_clip_get_name (LrgSkeleton2DClip *self);

/**
 * lrg_skeleton2d_clip_set_name:
 * @self: an #LrgSkeleton2DClip
 * @name: the new name (not empty)
 *
 * Renames the clip.  Rename clips before adding them to a skeleton (the
 * skeleton looks them up by name).
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_set_name (LrgSkeleton2DClip *self,
                                   const gchar       *name);

/**
 * lrg_skeleton2d_clip_get_duration:
 * @self: an #LrgSkeleton2DClip
 *
 * Returns: the length in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_skeleton2d_clip_get_duration (LrgSkeleton2DClip *self);

/**
 * lrg_skeleton2d_clip_set_duration:
 * @self: an #LrgSkeleton2DClip
 * @duration: length in seconds (0 or more)
 *
 * Sets the length.  Adding a key later than the duration extends it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_set_duration (LrgSkeleton2DClip *self,
                                       gfloat             duration);

/**
 * lrg_skeleton2d_clip_get_loop:
 * @self: an #LrgSkeleton2DClip
 *
 * Returns: whether the clip loops by default
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_clip_get_loop (LrgSkeleton2DClip *self);

/**
 * lrg_skeleton2d_clip_set_loop:
 * @self: an #LrgSkeleton2DClip
 * @loop: whether the clip loops by default
 *
 * Sets whether players loop the clip when they are not told otherwise.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_set_loop (LrgSkeleton2DClip *self,
                                   gboolean           loop);

/**
 * lrg_skeleton2d_clip_add_translate_key:
 * @self: an #LrgSkeleton2DClip
 * @bone: bone name
 * @time: key time in seconds (0 or more)
 * @x: translation offset X from the setup pose
 * @y: translation offset Y from the setup pose
 * @curve: interpolation toward the next key
 *
 * Adds (or, at an existing key time, replaces) a translation key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_add_translate_key (LrgSkeleton2DClip  *self,
                                            const gchar        *bone,
                                            gfloat              time,
                                            gfloat              x,
                                            gfloat              y,
                                            LrgSkeleton2DCurve  curve);

/**
 * lrg_skeleton2d_clip_add_rotate_key:
 * @self: an #LrgSkeleton2DClip
 * @bone: bone name
 * @time: key time in seconds (0 or more)
 * @degrees: rotation offset from the setup pose (positive turns
 *   clockwise on a y-down screen); values beyond ±180 are kept, so a key
 *   of 360 is a full turn from the previous key at 0
 * @curve: interpolation toward the next key
 *
 * Adds (or replaces) a rotation key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_add_rotate_key (LrgSkeleton2DClip  *self,
                                         const gchar        *bone,
                                         gfloat              time,
                                         gfloat              degrees,
                                         LrgSkeleton2DCurve  curve);

/**
 * lrg_skeleton2d_clip_add_scale_key:
 * @self: an #LrgSkeleton2DClip
 * @bone: bone name
 * @time: key time in seconds (0 or more)
 * @x: scale factor X multiplied into the setup scale
 * @y: scale factor Y multiplied into the setup scale
 * @curve: interpolation toward the next key
 *
 * Adds (or replaces) a scale key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_add_scale_key (LrgSkeleton2DClip  *self,
                                        const gchar        *bone,
                                        gfloat              time,
                                        gfloat              x,
                                        gfloat              y,
                                        LrgSkeleton2DCurve  curve);

/**
 * lrg_skeleton2d_clip_add_color_key:
 * @self: an #LrgSkeleton2DClip
 * @slot: slot name
 * @time: key time in seconds (0 or more)
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @a: alpha, 0 .. 1
 * @curve: interpolation toward the next key
 *
 * Adds (or replaces) a slot colour key.  Colours are absolute (not
 * relative to the setup colour).
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_add_color_key (LrgSkeleton2DClip  *self,
                                        const gchar        *slot,
                                        gfloat              time,
                                        gfloat              r,
                                        gfloat              g,
                                        gfloat              b,
                                        gfloat              a,
                                        LrgSkeleton2DCurve  curve);

/**
 * lrg_skeleton2d_clip_add_attachment_key:
 * @self: an #LrgSkeleton2DClip
 * @slot: slot name
 * @time: key time in seconds (0 or more)
 * @attachment: (nullable): attachment name to show from @time on, or
 *   %NULL to hide the slot
 *
 * Adds (or replaces) an attachment swap key.  Attachment keys are always
 * stepped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_add_attachment_key (LrgSkeleton2DClip *self,
                                             const gchar       *slot,
                                             gfloat             time,
                                             const gchar       *attachment);

/**
 * lrg_skeleton2d_clip_add_draw_order_key:
 * @self: an #LrgSkeleton2DClip
 * @time: key time in seconds (0 or more)
 * @slots: (array zero-terminated=1) (nullable): every slot name of the
 *   skeleton, back to front, or %NULL to return to the setup order
 *
 * Adds (or replaces) a draw-order key (stepped).  A key naming slots the
 * skeleton does not have, or not naming all of them, is ignored when
 * applied.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_add_draw_order_key (LrgSkeleton2DClip   *self,
                                             gfloat               time,
                                             const gchar * const *slots);

/**
 * lrg_skeleton2d_clip_add_event_key:
 * @self: an #LrgSkeleton2DClip
 * @time: event time in seconds (0 or more)
 * @name: event name
 *
 * Adds an event (a footstep, a sound cue).  Several events may share a
 * time.  #LrgSkeleton2DComponent emits #LrgSkeleton2DComponent::event
 * when playback crosses it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_add_event_key (LrgSkeleton2DClip *self,
                                        gfloat             time,
                                        const gchar       *name);

/**
 * lrg_skeleton2d_clip_set_key_bezier:
 * @self: an #LrgSkeleton2DClip
 * @target: bone name (translate, rotate, scale) or slot name (colour)
 * @channel: the channel (not %LRG_SKELETON2D_CHANNEL_ATTACHMENT)
 * @index: key index in time order
 * @cx1: first control point X (0 .. 1, the fraction of the key span)
 * @cy1: first control point Y (the fraction of the value change)
 * @cx2: second control point X (0 .. 1)
 * @cy2: second control point Y
 *
 * Makes the key ease toward the next one along a cubic Bézier timing
 * curve (its curve becomes %LRG_SKELETON2D_CURVE_BEZIER).
 *
 * Returns: %TRUE if the key exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_clip_set_key_bezier (LrgSkeleton2DClip    *self,
                                             const gchar          *target,
                                             LrgSkeleton2DChannel  channel,
                                             guint                 index,
                                             gfloat                cx1,
                                             gfloat                cy1,
                                             gfloat                cx2,
                                             gfloat                cy2);

/**
 * lrg_skeleton2d_clip_get_key_count:
 * @self: an #LrgSkeleton2DClip
 * @target: bone or slot name
 * @channel: the channel
 *
 * Returns: the number of keys of that timeline (0 if there is none)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_clip_get_key_count (LrgSkeleton2DClip    *self,
                                         const gchar          *target,
                                         LrgSkeleton2DChannel  channel);

/**
 * lrg_skeleton2d_clip_get_key:
 * @self: an #LrgSkeleton2DClip
 * @target: bone or slot name
 * @channel: the channel
 * @index: key index in time order
 * @time: (out) (optional): the key time
 * @values: (out caller-allocates) (array fixed-size=4) (optional): the
 *   key values (1, 2 or 4 used depending on @channel; unused are 0)
 * @curve: (out) (optional): the key's curve
 *
 * Reads a key of a numeric channel (for attachment keys use
 * lrg_skeleton2d_clip_get_attachment_key()).
 *
 * Returns: %TRUE if the key exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_clip_get_key (LrgSkeleton2DClip    *self,
                                      const gchar          *target,
                                      LrgSkeleton2DChannel  channel,
                                      guint                 index,
                                      gfloat               *time,
                                      gfloat               *values,
                                      LrgSkeleton2DCurve   *curve);

/**
 * lrg_skeleton2d_clip_get_attachment_key:
 * @self: an #LrgSkeleton2DClip
 * @slot: slot name
 * @index: key index in time order
 * @time: (out) (optional): the key time
 * @attachment: (out) (optional) (nullable) (transfer none): the
 *   attachment name, %NULL for "hidden"
 *
 * Reads an attachment key.
 *
 * Returns: %TRUE if the key exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_clip_get_attachment_key (LrgSkeleton2DClip  *self,
                                                 const gchar        *slot,
                                                 guint               index,
                                                 gfloat             *time,
                                                 const gchar       **attachment);

/**
 * lrg_skeleton2d_clip_remove_key:
 * @self: an #LrgSkeleton2DClip
 * @target: bone or slot name
 * @channel: the channel
 * @index: key index in time order
 *
 * Removes one key (the dopesheet's delete).  The duration is kept.
 *
 * Returns: %TRUE if the key existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_clip_remove_key (LrgSkeleton2DClip    *self,
                                         const gchar          *target,
                                         LrgSkeleton2DChannel  channel,
                                         guint                 index);

/**
 * lrg_skeleton2d_clip_sample:
 * @self: an #LrgSkeleton2DClip
 * @target: bone or slot name
 * @channel: a numeric channel
 * @time: time in seconds (clamped to the clip)
 * @values: (out caller-allocates) (array fixed-size=4): the sampled
 *   values (offsets/factors for bone channels, as stored)
 *
 * Samples one timeline.
 *
 * Returns: %TRUE if the timeline exists and has keys
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_clip_sample (LrgSkeleton2DClip    *self,
                                     const gchar          *target,
                                     LrgSkeleton2DChannel  channel,
                                     gfloat                time,
                                     gfloat               *values);

/**
 * lrg_skeleton2d_clip_get_targets:
 * @self: an #LrgSkeleton2DClip
 * @slots: %FALSE for the animated bones, %TRUE for the animated slots
 *
 * The names of the bones (or slots) the clip has timelines for, in the
 * order they were first keyed (the dopesheet's rows).
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_skeleton2d_clip_get_targets (LrgSkeleton2DClip *self,
                                          gboolean           slots);

/**
 * lrg_skeleton2d_clip_get_draw_order_key_count:
 * @self: an #LrgSkeleton2DClip
 *
 * Returns: the number of draw-order keys
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_clip_get_draw_order_key_count (LrgSkeleton2DClip *self);

/**
 * lrg_skeleton2d_clip_get_draw_order_key:
 * @self: an #LrgSkeleton2DClip
 * @index: key index in time order
 * @time: (out) (optional): the key time
 *
 * Reads a draw-order key.
 *
 * Returns: (transfer none) (array zero-terminated=1) (nullable): the
 *   slot names back to front, or %NULL for the setup order (or an
 *   invalid @index)
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_skeleton2d_clip_get_draw_order_key (LrgSkeleton2DClip *self,
                                                              guint              index,
                                                              gfloat            *time);

/**
 * lrg_skeleton2d_clip_get_event_count:
 * @self: an #LrgSkeleton2DClip
 *
 * Returns: the number of events
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_clip_get_event_count (LrgSkeleton2DClip *self);

/**
 * lrg_skeleton2d_clip_get_event:
 * @self: an #LrgSkeleton2DClip
 * @index: event index in time order
 * @time: (out) (optional): the event time
 *
 * Reads an event.
 *
 * Returns: (nullable): the event name, or %NULL for an invalid @index
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_clip_get_event (LrgSkeleton2DClip *self,
                                             guint              index,
                                             gfloat            *time);

/**
 * lrg_skeleton2d_clip_apply:
 * @self: an #LrgSkeleton2DClip
 * @skeleton: the skeleton to pose
 * @time: time in seconds (clamped to the clip; wrap it yourself to
 *   loop)
 * @alpha: mix weight, 0 .. 1
 *
 * Poses @skeleton's current (local) pose from the clip.  Each keyed bone
 * channel moves from the current value toward the clip value by @alpha
 * (rotation along the shorter arc; @alpha 1 sets the value exactly);
 * slot colours mix the same way; attachment and draw-order keys apply
 * when @alpha is at least 0.5.  Channels the clip does not key are left
 * alone, so reset with lrg_skeleton2d_reset_pose() first, then apply
 * clips in order: apply A at 1 and B at w to crossfade from A to B.
 * Call lrg_skeleton2d_update_world() afterwards.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_clip_apply (LrgSkeleton2DClip *self,
                                LrgSkeleton2D     *skeleton,
                                gfloat             time,
                                gfloat             alpha);

/* ------------------------------------------------------------------ */
/* LrgSkeleton2D: construction and files                               */
/* ------------------------------------------------------------------ */

/**
 * lrg_skeleton2d_new:
 *
 * Creates an empty skeleton with only the default skin.
 *
 * Returns: (transfer full): a new #LrgSkeleton2D
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2D * lrg_skeleton2d_new (void);

/**
 * lrg_skeleton2d_new_from_file:
 * @path: a `.rskel2d` file
 * @error: return location for a #GError
 *
 * Loads a skeleton.  The file's directory becomes
 * #LrgSkeleton2D:base-dir, which lrg_skeleton2d_load_textures() resolves
 * image paths against.  Loading is strict: every problem is an error
 * whose message names the file and the path of the offending node
 * (`figure.rskel2d: /bones/3/parent: unknown bone "hip"`).  No texture
 * is loaded (no window is needed).
 *
 * Returns: (transfer full) (nullable): the skeleton, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2D * lrg_skeleton2d_new_from_file (const gchar  *path,
                                              GError      **error);

/**
 * lrg_skeleton2d_new_from_data:
 * @data: `.rskel2d` text
 * @length: length of @data, or -1 if it is NUL-terminated
 * @error: return location for a #GError
 *
 * Loads a skeleton from text (the editor validates documents this way).
 * Messages name the source `<data>`.
 *
 * Returns: (transfer full) (nullable): the skeleton, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2D * lrg_skeleton2d_new_from_data (const gchar  *data,
                                              gssize        length,
                                              GError      **error);

/**
 * lrg_skeleton2d_save_to_data:
 * @self: an #LrgSkeleton2D
 * @length: (out) (optional): length of the text
 *
 * Writes the skeleton (setup pose, slots, skins, clips) as `.rskel2d`
 * text.  Output is deterministic: loading it and saving again gives the
 * same bytes.  Values equal to their defaults are left out.
 *
 * Returns: (transfer full): the document text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_skeleton2d_save_to_data (LrgSkeleton2D *self,
                                     gsize         *length);

/**
 * lrg_skeleton2d_save_to_file:
 * @self: an #LrgSkeleton2D
 * @path: destination path
 * @error: return location for a #GError
 *
 * Writes lrg_skeleton2d_save_to_data() to @path atomically.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_save_to_file (LrgSkeleton2D  *self,
                                      const gchar    *path,
                                      GError        **error);

/**
 * lrg_skeleton2d_load_textures:
 * @self: an #LrgSkeleton2D
 * @error: return location for a #GError
 *
 * Loads the image of every attachment that has one and no texture yet,
 * relative to #LrgSkeleton2D:base-dir.  Attachments sharing an image
 * share one #GrlTexture.  Needs an open window.
 *
 * Returns: %TRUE if every image loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_load_textures (LrgSkeleton2D  *self,
                                       GError        **error);

/**
 * lrg_skeleton2d_get_name:
 * @self: an #LrgSkeleton2D
 *
 * Returns: (nullable): the skeleton's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_name (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_set_name:
 * @self: an #LrgSkeleton2D
 * @name: (nullable): the new name
 *
 * Sets the skeleton's name (informational; stored in the file).
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_name (LrgSkeleton2D *self,
                              const gchar   *name);

/**
 * lrg_skeleton2d_get_base_dir:
 * @self: an #LrgSkeleton2D
 *
 * Returns: (nullable): the directory image paths are resolved against
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_base_dir (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_set_base_dir:
 * @self: an #LrgSkeleton2D
 * @base_dir: (nullable): a directory
 *
 * Sets the directory image paths are resolved against.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_base_dir (LrgSkeleton2D *self,
                                  const gchar   *base_dir);

/* ------------------------------------------------------------------ */
/* Bones                                                               */
/* ------------------------------------------------------------------ */

/**
 * lrg_skeleton2d_add_bone:
 * @self: an #LrgSkeleton2D
 * @name: a unique bone name
 * @parent: index of an existing bone, or -1 for a root bone
 * @x: setup translation X relative to the parent
 * @y: setup translation Y relative to the parent
 * @rotation: setup rotation in degrees relative to the parent
 *   (clockwise on a y-down screen)
 * @scale_x: setup scale X
 * @scale_y: setup scale Y
 * @length: bone length along its local +X axis (drawing, picking and
 *   automatic weights; 0 for a point bone)
 *
 * Appends a bone.  Parents always precede their children, so world
 * transforms are computed in one pass in index order.  The current pose
 * of the new bone is its setup pose.
 *
 * Returns: the new bone index, or -1 if @name is empty or taken or
 *   @parent does not exist
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skeleton2d_add_bone (LrgSkeleton2D *self,
                              const gchar   *name,
                              gint           parent,
                              gfloat         x,
                              gfloat         y,
                              gfloat         rotation,
                              gfloat         scale_x,
                              gfloat         scale_y,
                              gfloat         length);

/**
 * lrg_skeleton2d_get_bone_count:
 * @self: an #LrgSkeleton2D
 *
 * Returns: the number of bones
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_get_bone_count (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_find_bone:
 * @self: an #LrgSkeleton2D
 * @name: a bone name
 *
 * Returns: the bone index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skeleton2d_find_bone (LrgSkeleton2D *self,
                               const gchar   *name);

/**
 * lrg_skeleton2d_get_bone_name:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 *
 * Returns: (nullable): the bone name, or %NULL for an invalid index
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_bone_name (LrgSkeleton2D *self,
                                            gint           bone);

/**
 * lrg_skeleton2d_get_bone_parent:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 *
 * Returns: the parent index, or -1 for a root bone or an invalid index
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skeleton2d_get_bone_parent (LrgSkeleton2D *self,
                                     gint           bone);

/**
 * lrg_skeleton2d_get_bone_length:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 *
 * Returns: the bone length (0 for an invalid index)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_skeleton2d_get_bone_length (LrgSkeleton2D *self,
                                       gint           bone);

/**
 * lrg_skeleton2d_set_bone_length:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @length: the new length (0 or more)
 *
 * Sets the bone length.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_bone_length (LrgSkeleton2D *self,
                                     gint           bone,
                                     gfloat         length);

/**
 * lrg_skeleton2d_get_bone_setup:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @x: (out) (optional): translation X
 * @y: (out) (optional): translation Y
 * @rotation: (out) (optional): rotation in degrees
 * @scale_x: (out) (optional): scale X
 * @scale_y: (out) (optional): scale Y
 *
 * Reads the setup (rest) pose of a bone, relative to its parent.
 *
 * Returns: %TRUE if @bone is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_get_bone_setup (LrgSkeleton2D *self,
                                        gint           bone,
                                        gfloat        *x,
                                        gfloat        *y,
                                        gfloat        *rotation,
                                        gfloat        *scale_x,
                                        gfloat        *scale_y);

/**
 * lrg_skeleton2d_set_bone_setup:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @x: translation X
 * @y: translation Y
 * @rotation: rotation in degrees
 * @scale_x: scale X
 * @scale_y: scale Y
 *
 * Changes the setup pose of a bone (the rigging tool's bone move).  The
 * bind matrices are recomputed from the new setup pose, so mesh vertices
 * (which live in setup space) stay where they are drawn in the setup
 * pose; the current pose is not changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_bone_setup (LrgSkeleton2D *self,
                                    gint           bone,
                                    gfloat         x,
                                    gfloat         y,
                                    gfloat         rotation,
                                    gfloat         scale_x,
                                    gfloat         scale_y);

/**
 * lrg_skeleton2d_get_bone_local:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @x: (out) (optional): translation X
 * @y: (out) (optional): translation Y
 * @rotation: (out) (optional): rotation in degrees
 * @scale_x: (out) (optional): scale X
 * @scale_y: (out) (optional): scale Y
 *
 * Reads the current (animated) pose of a bone, relative to its parent.
 *
 * Returns: %TRUE if @bone is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_get_bone_local (LrgSkeleton2D *self,
                                        gint           bone,
                                        gfloat        *x,
                                        gfloat        *y,
                                        gfloat        *rotation,
                                        gfloat        *scale_x,
                                        gfloat        *scale_y);

/**
 * lrg_skeleton2d_set_bone_local:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @x: translation X
 * @y: translation Y
 * @rotation: rotation in degrees
 * @scale_x: scale X
 * @scale_y: scale Y
 *
 * Sets the current pose of a bone (procedural animation, IK, the pose
 * tool).  Call lrg_skeleton2d_update_world() afterwards.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_bone_local (LrgSkeleton2D *self,
                                    gint           bone,
                                    gfloat         x,
                                    gfloat         y,
                                    gfloat         rotation,
                                    gfloat         scale_x,
                                    gfloat         scale_y);

/**
 * lrg_skeleton2d_set_root_transform:
 * @self: an #LrgSkeleton2D
 * @x: position X of the skeleton origin
 * @y: position Y of the skeleton origin
 * @rotation: rotation in degrees
 * @scale_x: scale X (negative mirrors)
 * @scale_y: scale Y (negative mirrors)
 *
 * Places the whole skeleton (what root bones are relative to).  The
 * default is the identity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_root_transform (LrgSkeleton2D *self,
                                        gfloat         x,
                                        gfloat         y,
                                        gfloat         rotation,
                                        gfloat         scale_x,
                                        gfloat         scale_y);

/**
 * lrg_skeleton2d_update_world:
 * @self: an #LrgSkeleton2D
 *
 * Computes every bone's world matrix from the current pose and the root
 * transform, and the skinning palette.  Call after posing, before
 * reading world transforms or drawing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_update_world (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_get_bone_world_matrix:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @matrix: (out caller-allocates) (array fixed-size=6): the world matrix
 *   `(a, b, c, d, tx, ty)`: `x' = a*x + c*y + tx`, `y' = b*x + d*y + ty`
 *
 * Reads a bone's world matrix as of the last
 * lrg_skeleton2d_update_world().
 *
 * Returns: %TRUE if @bone is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_get_bone_world_matrix (LrgSkeleton2D *self,
                                               gint           bone,
                                               gfloat        *matrix);

/**
 * lrg_skeleton2d_get_bone_world_position:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @x: (out) (optional): world X of the bone origin
 * @y: (out) (optional): world Y of the bone origin
 *
 * Reads where a bone starts, as of the last update.
 *
 * Returns: %TRUE if @bone is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_get_bone_world_position (LrgSkeleton2D *self,
                                                 gint           bone,
                                                 gfloat        *x,
                                                 gfloat        *y);

/**
 * lrg_skeleton2d_get_bone_world_tip:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 * @x: (out) (optional): world X of the bone tip
 * @y: (out) (optional): world Y of the bone tip
 *
 * Reads where a bone ends (its origin plus its length along local +X),
 * as of the last update.
 *
 * Returns: %TRUE if @bone is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_get_bone_world_tip (LrgSkeleton2D *self,
                                            gint           bone,
                                            gfloat        *x,
                                            gfloat        *y);

/**
 * lrg_skeleton2d_get_bone_world_rotation:
 * @self: an #LrgSkeleton2D
 * @bone: a bone index
 *
 * Returns: the world rotation of a bone's +X axis in degrees (-180 ..
 *   180), as of the last update
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_skeleton2d_get_bone_world_rotation (LrgSkeleton2D *self,
                                               gint           bone);

/**
 * lrg_skeleton2d_get_palette:
 * @self: an #LrgSkeleton2D
 * @n_floats: (out) (optional): length of the result (6 per bone)
 *
 * The skinning palette of the last update: world(bone) *
 * inverse_bind(bone) per bone, where the bind matrices are the setup
 * pose's world matrices without the root transform.  Feed it to
 * lrg_skinned_sprite_deform().
 *
 * Returns: (transfer none) (array length=n_floats): the palette
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_skeleton2d_get_palette (LrgSkeleton2D *self,
                                           guint         *n_floats);

/**
 * lrg_skeleton2d_pick_bone:
 * @self: an #LrgSkeleton2D
 * @x: world X
 * @y: world Y
 * @radius: pick tolerance in world units
 *
 * Finds the bone whose world segment (origin to tip) passes closest to
 * (@x, @y) within @radius (the rigging tool's click).  Later bones win
 * ties, so children over their parents.
 *
 * Returns: the bone index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skeleton2d_pick_bone (LrgSkeleton2D *self,
                               gfloat         x,
                               gfloat         y,
                               gfloat         radius);

/* ------------------------------------------------------------------ */
/* Slots and draw order                                                */
/* ------------------------------------------------------------------ */

/**
 * lrg_skeleton2d_add_slot:
 * @self: an #LrgSkeleton2D
 * @name: a unique slot name
 * @bone: the bone the slot follows
 * @attachment: (nullable): setup attachment name, or %NULL for an empty
 *   slot
 *
 * Appends a slot at the front of the setup draw order (slots draw back
 * to front in the order they were added).  Its colour starts white.
 *
 * Returns: the new slot index, or -1 if @name is empty or taken or
 *   @bone does not exist
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skeleton2d_add_slot (LrgSkeleton2D *self,
                              const gchar   *name,
                              gint           bone,
                              const gchar   *attachment);

/**
 * lrg_skeleton2d_get_slot_count:
 * @self: an #LrgSkeleton2D
 *
 * Returns: the number of slots
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_get_slot_count (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_find_slot:
 * @self: an #LrgSkeleton2D
 * @name: a slot name
 *
 * Returns: the slot index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skeleton2d_find_slot (LrgSkeleton2D *self,
                               const gchar   *name);

/**
 * lrg_skeleton2d_get_slot_name:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 *
 * Returns: (nullable): the slot name, or %NULL for an invalid index
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_slot_name (LrgSkeleton2D *self,
                                            gint           slot);

/**
 * lrg_skeleton2d_get_slot_bone:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 *
 * Returns: the slot's bone, or -1 for an invalid index
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skeleton2d_get_slot_bone (LrgSkeleton2D *self,
                                   gint           slot);

/**
 * lrg_skeleton2d_get_slot_attachment:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 *
 * Returns: (nullable): the attachment the slot currently shows
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_slot_attachment (LrgSkeleton2D *self,
                                                  gint           slot);

/**
 * lrg_skeleton2d_set_slot_attachment:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 * @attachment: (nullable): attachment name, or %NULL to hide the slot
 *
 * Changes what the slot currently shows (until the next reset or clip
 * key).  A name with no attachment in the active or default skin draws
 * nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_slot_attachment (LrgSkeleton2D *self,
                                         gint           slot,
                                         const gchar   *attachment);

/**
 * lrg_skeleton2d_get_slot_setup_attachment:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 *
 * Returns: (nullable): the attachment shown in the setup pose
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_slot_setup_attachment (LrgSkeleton2D *self,
                                                        gint           slot);

/**
 * lrg_skeleton2d_set_slot_setup_attachment:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 * @attachment: (nullable): attachment name, or %NULL
 *
 * Changes the attachment shown in the setup pose (and the current one).
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_slot_setup_attachment (LrgSkeleton2D *self,
                                               gint           slot,
                                               const gchar   *attachment);

/**
 * lrg_skeleton2d_get_slot_color:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 * @rgba: (out caller-allocates) (array fixed-size=4): the current colour
 *   (0 .. 1 each)
 *
 * Reads the slot's current colour.
 *
 * Returns: %TRUE if @slot is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_get_slot_color (LrgSkeleton2D *self,
                                        gint           slot,
                                        gfloat        *rgba);

/**
 * lrg_skeleton2d_set_slot_color:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 * @r: red
 * @g: green
 * @b: blue
 * @a: alpha
 *
 * Sets the slot's current colour (multiplied into its attachment).
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_slot_color (LrgSkeleton2D *self,
                                    gint           slot,
                                    gfloat         r,
                                    gfloat         g,
                                    gfloat         b,
                                    gfloat         a);

/**
 * lrg_skeleton2d_set_slot_setup_color:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 * @r: red
 * @g: green
 * @b: blue
 * @a: alpha
 *
 * Sets the slot's setup colour (and the current one).
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_set_slot_setup_color (LrgSkeleton2D *self,
                                          gint           slot,
                                          gfloat         r,
                                          gfloat         g,
                                          gfloat         b,
                                          gfloat         a);

/**
 * lrg_skeleton2d_get_draw_order:
 * @self: an #LrgSkeleton2D
 * @n_slots: (out) (optional): number of entries
 *
 * The current draw order, back to front.
 *
 * Returns: (transfer none) (array length=n_slots): slot indices
 */
LRG_AVAILABLE_IN_ALL
const gint * lrg_skeleton2d_get_draw_order (LrgSkeleton2D *self,
                                            guint         *n_slots);

/**
 * lrg_skeleton2d_set_draw_order:
 * @self: an #LrgSkeleton2D
 * @order: (array length=n_slots): every slot index once, back to front
 * @n_slots: length of @order (must equal the slot count)
 *
 * Sets the current draw order.
 *
 * Returns: %TRUE if @order is a permutation of the slots
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_set_draw_order (LrgSkeleton2D *self,
                                        const gint    *order,
                                        guint          n_slots);

/**
 * lrg_skeleton2d_reset_pose:
 * @self: an #LrgSkeleton2D
 *
 * Returns every bone to its setup pose and every slot to its setup
 * attachment and colour, and the draw order to the setup order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_reset_pose (LrgSkeleton2D *self);

/* ------------------------------------------------------------------ */
/* Skins and attachments                                               */
/* ------------------------------------------------------------------ */

/**
 * lrg_skeleton2d_add_skin:
 * @self: an #LrgSkeleton2D
 * @name: skin name
 *
 * Adds an empty skin (does nothing if it exists).
 *
 * Returns: %TRUE if the skin exists afterwards
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_add_skin (LrgSkeleton2D *self,
                                  const gchar   *name);

/**
 * lrg_skeleton2d_get_skin_count:
 * @self: an #LrgSkeleton2D
 *
 * Returns: the number of skins (at least 1: the default skin)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_get_skin_count (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_get_skin_name:
 * @self: an #LrgSkeleton2D
 * @index: a skin index (0 is the default skin)
 *
 * Returns: (nullable): the skin name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_skin_name (LrgSkeleton2D *self,
                                            guint          index);

/**
 * lrg_skeleton2d_set_skin:
 * @self: an #LrgSkeleton2D
 * @name: (nullable): the skin to show, or %NULL for the default skin
 *
 * Selects the active skin.  Attachments are looked up in the active skin
 * first, then in the default skin.
 *
 * Returns: %TRUE if the skin exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_set_skin (LrgSkeleton2D *self,
                                  const gchar   *name);

/**
 * lrg_skeleton2d_get_skin:
 * @self: an #LrgSkeleton2D
 *
 * Returns: the active skin's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_get_skin (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_set_attachment:
 * @self: an #LrgSkeleton2D
 * @skin: (nullable): skin name (created if missing), or %NULL for the
 *   default skin
 * @slot: a slot index
 * @name: attachment name, unique per skin and slot
 * @sprite: (nullable): the mesh, or %NULL to remove the attachment
 *
 * Adds, replaces or removes an attachment.  The skeleton keeps a
 * reference to @sprite.  The mesh's weights are skeleton bone indices;
 * vertices without weights follow the slot's bone.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_set_attachment (LrgSkeleton2D    *self,
                                        const gchar      *skin,
                                        gint              slot,
                                        const gchar      *name,
                                        LrgSkinnedSprite *sprite);

/**
 * lrg_skeleton2d_get_skin_attachment:
 * @self: an #LrgSkeleton2D
 * @skin: (nullable): skin name, or %NULL for the default skin
 * @slot: a slot index
 * @name: attachment name
 *
 * Looks an attachment up in one skin only.
 *
 * Returns: (transfer none) (nullable): the mesh
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedSprite * lrg_skeleton2d_get_skin_attachment (LrgSkeleton2D *self,
                                                       const gchar   *skin,
                                                       gint           slot,
                                                       const gchar   *name);

/**
 * lrg_skeleton2d_get_attachment:
 * @self: an #LrgSkeleton2D
 * @slot: a slot index
 * @name: (nullable): attachment name, or %NULL for what the slot
 *   currently shows
 *
 * Looks an attachment up in the active skin, then in the default skin.
 *
 * Returns: (transfer none) (nullable): the mesh
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedSprite * lrg_skeleton2d_get_attachment (LrgSkeleton2D *self,
                                                  gint           slot,
                                                  const gchar   *name);

/**
 * lrg_skeleton2d_list_attachments:
 * @self: an #LrgSkeleton2D
 * @skin: (nullable): skin name, or %NULL for the default skin
 * @slot: a slot index
 *
 * The attachment names of one slot in one skin, in insertion order (the
 * rigging tool's attachment list).
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_skeleton2d_list_attachments (LrgSkeleton2D *self,
                                          const gchar   *skin,
                                          gint           slot);

/**
 * lrg_skeleton2d_compute_auto_weights:
 * @self: an #LrgSkeleton2D
 * @sprite: the mesh to weight (vertices in setup space)
 * @bones: (array length=n_bones) (nullable): the bones that may
 *   influence it, or %NULL for every bone
 * @n_bones: length of @bones
 * @falloff: distance exponent (see
 *   lrg_skinned_sprite_compute_auto_weights())
 * @max_influences: influences kept per vertex (1 .. 4)
 * @smooth_iterations: neighbour-averaging passes (0 = none)
 *
 * Weights @sprite automatically to the setup-pose segments of @bones
 * (origin to tip).  This is the engine half of the rigging tool's
 * "automatic weights" button.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_compute_auto_weights (LrgSkeleton2D    *self,
                                              LrgSkinnedSprite *sprite,
                                              const gint       *bones,
                                              guint             n_bones,
                                              gfloat            falloff,
                                              guint             max_influences,
                                              guint             smooth_iterations);

/* ------------------------------------------------------------------ */
/* Clips                                                               */
/* ------------------------------------------------------------------ */

/**
 * lrg_skeleton2d_add_clip:
 * @self: an #LrgSkeleton2D
 * @clip: a clip
 *
 * Adds a clip, replacing one of the same name (in its place).  The
 * skeleton keeps a reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_add_clip (LrgSkeleton2D     *self,
                              LrgSkeleton2DClip *clip);

/**
 * lrg_skeleton2d_remove_clip:
 * @self: an #LrgSkeleton2D
 * @name: a clip name
 *
 * Removes a clip.
 *
 * Returns: %TRUE if it existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_remove_clip (LrgSkeleton2D *self,
                                     const gchar   *name);

/**
 * lrg_skeleton2d_get_clip:
 * @self: an #LrgSkeleton2D
 * @name: a clip name
 *
 * Returns: (transfer none) (nullable): the clip
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DClip * lrg_skeleton2d_get_clip (LrgSkeleton2D *self,
                                             const gchar   *name);

/**
 * lrg_skeleton2d_get_clip_count:
 * @self: an #LrgSkeleton2D
 *
 * Returns: the number of clips
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_get_clip_count (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_get_clip_at:
 * @self: an #LrgSkeleton2D
 * @index: a clip index (insertion order)
 *
 * Returns: (transfer none) (nullable): the clip
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DClip * lrg_skeleton2d_get_clip_at (LrgSkeleton2D *self,
                                                guint          index);

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/**
 * lrg_skeleton2d_deform:
 * @self: an #LrgSkeleton2D
 *
 * Skins every attachment the slots currently show with the palette of
 * the last lrg_skeleton2d_update_world() (no drawing; no window
 * needed).  lrg_skeleton2d_draw() does this itself.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_deform (LrgSkeleton2D *self);

/**
 * lrg_skeleton2d_draw:
 * @self: an #LrgSkeleton2D
 * @tint: (nullable): colour multiplied into every slot, or %NULL for
 *   white
 *
 * Deforms and draws the attachments the slots show, in the current draw
 * order, each tinted by its slot colour times @tint.  Draws in the
 * current 2D mode; needs a window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_draw (LrgSkeleton2D  *self,
                          const GrlColor *tint);

/**
 * lrg_skeleton2d_draw_bones:
 * @self: an #LrgSkeleton2D
 * @color: (nullable): line colour, or %NULL for yellow
 *
 * Draws every bone as a line from its origin to its tip with a dot at
 * the origin (the rigging tool's bone overlay).  Needs a window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_draw_bones (LrgSkeleton2D  *self,
                                const GrlColor *color);

G_END_DECLS
