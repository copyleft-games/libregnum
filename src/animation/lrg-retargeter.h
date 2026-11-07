/* lrg-retargeter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation retargeting: #LrgBoneMap pairs the bones of two skeletons
 * (explicit pairs plus name rules, stored as a .rbonemap YAML file) and
 * #LrgRetargeter moves poses and whole clips from a source skeleton onto
 * a target skeleton whose rest pose differs, aligning the two rest
 * poses in skeleton (world) space.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-skeleton.h"
#include "lrg-animation-clip.h"

G_BEGIN_DECLS

/**
 * LRG_BONE_MAP_FORMAT_VERSION:
 *
 * The "version" a .rbonemap file is written with (and the newest read).
 */
#define LRG_BONE_MAP_FORMAT_VERSION (1)

/**
 * LRG_RETARGET_ERROR:
 *
 * Error domain of #LrgBoneMap and #LrgRetargeter.
 */
#define LRG_RETARGET_ERROR (lrg_retarget_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_retarget_error_quark (void);

/**
 * LrgRetargetError:
 * @LRG_RETARGET_ERROR_IO: a file could not be read or written
 * @LRG_RETARGET_ERROR_PARSE: a .rbonemap is not valid YAML or has the
 *   wrong shape
 * @LRG_RETARGET_ERROR_VERSION: a .rbonemap is newer than this library
 * @LRG_RETARGET_ERROR_SKELETON: a skeleton's bone indices are not
 *   0 .. count - 1, or no bone could be mapped
 *
 * Errors of the retargeting module.
 */
typedef enum
{
	LRG_RETARGET_ERROR_IO,
	LRG_RETARGET_ERROR_PARSE,
	LRG_RETARGET_ERROR_VERSION,
	LRG_RETARGET_ERROR_SKELETON
} LrgRetargetError;

LRG_AVAILABLE_IN_ALL
GType lrg_retarget_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RETARGET_ERROR (lrg_retarget_error_get_type ())

/**
 * LrgRetargetTranslationMode:
 * @LRG_RETARGET_TRANSLATION_NONE: every target bone keeps its bind
 *   translation (rotations only; the character stays in place)
 * @LRG_RETARGET_TRANSLATION_ROOT: the root bone (see
 *   lrg_retargeter_set_root_bone()) follows the source root's movement
 *   away from its rest position, scaled by the translation scale; the
 *   others keep their bind translation. The usual choice.
 * @LRG_RETARGET_TRANSLATION_ALL: every mapped bone follows its source
 *   bone's movement away from rest (stretchy rigs, facial bones)
 *
 * Which target bones take translation from the source animation.
 */
typedef enum
{
	LRG_RETARGET_TRANSLATION_NONE,
	LRG_RETARGET_TRANSLATION_ROOT,
	LRG_RETARGET_TRANSLATION_ALL
} LrgRetargetTranslationMode;

LRG_AVAILABLE_IN_ALL
GType lrg_retarget_translation_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RETARGET_TRANSLATION_MODE (lrg_retarget_translation_mode_get_type ())

/* ------------------------------------------------------------------------ */
/* LrgBoneMap                                                               */
/* ------------------------------------------------------------------------ */

#define LRG_TYPE_BONE_MAP (lrg_bone_map_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBoneMap, lrg_bone_map, LRG, BONE_MAP, GObject)

/**
 * lrg_bone_map_new:
 *
 * Creates an empty bone map with the default name rules (ignore case,
 * normalise separators and sides, no prefixes stripped).
 *
 * Returns: (transfer full): a new #LrgBoneMap
 */
LRG_AVAILABLE_IN_ALL
LrgBoneMap *
lrg_bone_map_new (void);

/**
 * lrg_bone_map_new_from_file:
 * @path: (type filename): a .rbonemap file
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the map, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgBoneMap *
lrg_bone_map_new_from_file (const gchar  *path,
                            GError      **error);

/**
 * lrg_bone_map_new_from_data:
 * @data: .rbonemap YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the map, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgBoneMap *
lrg_bone_map_new_from_data (const gchar  *data,
                            gssize        length,
                            GError      **error);

/**
 * lrg_bone_map_to_data:
 * @self: an #LrgBoneMap
 * @error: return location for a #GError
 *
 * Serialises the map as .rbonemap YAML (explicit pairs sorted by source
 * name, so the output is stable for version control).
 *
 * Returns: (transfer full) (nullable): the YAML text, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_bone_map_to_data (LrgBoneMap  *self,
                      GError     **error);

/**
 * lrg_bone_map_save_to_file:
 * @self: an #LrgBoneMap
 * @path: (type filename): destination
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_bone_map_save_to_file (LrgBoneMap   *self,
                           const gchar  *path,
                           GError      **error);

/**
 * lrg_bone_map_set:
 * @self: an #LrgBoneMap
 * @source: a source bone name
 * @target: (nullable): the target bone it drives, or %NULL to remove the
 *   pair
 *
 * Sets an explicit pair (explicit pairs win over the name rules). A
 * target bone can be driven by one source bone only: an older pair
 * naming @target is removed. Emits #LrgBoneMap::changed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bone_map_set (LrgBoneMap  *self,
                  const gchar *source,
                  const gchar *target);

/**
 * lrg_bone_map_get_target:
 * @self: an #LrgBoneMap
 * @source: a source bone name
 *
 * Returns: (transfer none) (nullable): the explicit target of @source
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_bone_map_get_target (LrgBoneMap  *self,
                         const gchar *source);

/**
 * lrg_bone_map_get_source:
 * @self: an #LrgBoneMap
 * @target: a target bone name
 *
 * Returns: (transfer none) (nullable): the source explicitly mapped onto
 *   @target
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_bone_map_get_source (LrgBoneMap  *self,
                         const gchar *target);

/**
 * lrg_bone_map_get_sources:
 * @self: an #LrgBoneMap
 *
 * Returns: (transfer full) (array zero-terminated=1): the explicitly
 *   mapped source names, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_bone_map_get_sources (LrgBoneMap *self);

/**
 * lrg_bone_map_get_size:
 * @self: an #LrgBoneMap
 *
 * Returns: the number of explicit pairs
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_bone_map_get_size (LrgBoneMap *self);

/**
 * lrg_bone_map_clear:
 * @self: an #LrgBoneMap
 *
 * Removes every explicit pair (the rules stay).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bone_map_clear (LrgBoneMap *self);

/**
 * lrg_bone_map_add_strip_prefix:
 * @self: an #LrgBoneMap
 * @prefix: a prefix removed from bone names before they are compared
 *   ("mixamorig:", "Armature_", "DEF-")
 *
 * Adds a name rule. Emits #LrgBoneMap::changed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bone_map_add_strip_prefix (LrgBoneMap  *self,
                               const gchar *prefix);

/**
 * lrg_bone_map_get_strip_prefixes:
 * @self: an #LrgBoneMap
 *
 * Returns: (transfer none) (array zero-terminated=1): the prefixes
 */
LRG_AVAILABLE_IN_ALL
const gchar * const *
lrg_bone_map_get_strip_prefixes (LrgBoneMap *self);

/**
 * lrg_bone_map_set_ignore_case:
 * @self: an #LrgBoneMap
 * @ignore_case: compare names without case
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bone_map_set_ignore_case (LrgBoneMap *self,
                              gboolean    ignore_case);

/**
 * lrg_bone_map_get_ignore_case:
 * @self: an #LrgBoneMap
 *
 * Returns: whether names are compared without case
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_bone_map_get_ignore_case (LrgBoneMap *self);

/**
 * lrg_bone_map_set_normalize_separators:
 * @self: an #LrgBoneMap
 * @normalize: split names into words at '_', '-', '.', ':', '|', ' '
 *   and lower-to-upper case changes, so "upper_arm", "UpperArm" and
 *   "upper.arm" compare equal
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bone_map_set_normalize_separators (LrgBoneMap *self,
                                       gboolean    normalize);

/**
 * lrg_bone_map_get_normalize_separators:
 * @self: an #LrgBoneMap
 *
 * Returns: whether names are split into words
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_bone_map_get_normalize_separators (LrgBoneMap *self);

/**
 * lrg_bone_map_set_normalize_sides:
 * @self: an #LrgBoneMap
 * @normalize: treat the words "left"/"l" and "right"/"r" as a side
 *   marker wherever they are, so "LeftArm", "arm_l" and "Arm.L" compare
 *   equal (needs separator normalisation)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bone_map_set_normalize_sides (LrgBoneMap *self,
                                  gboolean    normalize);

/**
 * lrg_bone_map_get_normalize_sides:
 * @self: an #LrgBoneMap
 *
 * Returns: whether side words are normalised
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_bone_map_get_normalize_sides (LrgBoneMap *self);

/**
 * lrg_bone_map_normalize_name:
 * @self: an #LrgBoneMap
 * @name: a bone name
 *
 * The canonical form the name rules compare: prefixes stripped, words
 * joined with '_', side moved to a trailing ".l"/".r" ("mixamorig:LeftArm"
 * with that prefix becomes "arm.l").
 *
 * Returns: (transfer full): the canonical name
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_bone_map_normalize_name (LrgBoneMap  *self,
                             const gchar *name);

/**
 * lrg_bone_map_resolve:
 * @self: an #LrgBoneMap
 * @source: the source skeleton
 * @target: the target skeleton
 * @n_target: (out) (optional): number of entries (the target's bone
 *   count)
 *
 * Pairs the skeletons' bones: for each target bone index, the source bone
 * index driving it, or -1. An explicit pair whose target names this
 * bone wins; otherwise a source bone with the same canonical name
 * (lrg_bone_map_normalize_name()); each source bone drives at most one
 * target bone by rule.
 *
 * Returns: (transfer full) (array length=n_target): the source index per
 *   target bone index
 */
LRG_AVAILABLE_IN_ALL
gint *
lrg_bone_map_resolve (LrgBoneMap  *self,
                      LrgSkeleton *source,
                      LrgSkeleton *target,
                      guint       *n_target);

/**
 * lrg_bone_map_auto_fill:
 * @self: an #LrgBoneMap
 * @source: the source skeleton
 * @target: the target skeleton
 *
 * Turns the rule matches of lrg_bone_map_resolve() into explicit pairs
 * (an editor's "auto map" button, after which the user fixes the rest).
 *
 * Returns: the number of pairs added
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_bone_map_auto_fill (LrgBoneMap  *self,
                        LrgSkeleton *source,
                        LrgSkeleton *target);

/* ------------------------------------------------------------------------ */
/* LrgRetargeter                                                            */
/* ------------------------------------------------------------------------ */

#define LRG_TYPE_RETARGETER (lrg_retargeter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRetargeter, lrg_retargeter, LRG, RETARGETER, GObject)

/**
 * lrg_retargeter_new:
 * @source: the skeleton the animation was made for
 * @target: the skeleton to animate
 * @map: (nullable): the bone map, or %NULL for a map with only the
 *   default name rules
 *
 * Creates a retargeter. Both skeletons need bone indices 0 .. count - 1
 * (glTF-imported skeletons have them). Their bind poses are the rest
 * poses that get aligned: the retargeter carries each source bone's
 * rotation away from its rest orientation, measured in skeleton space,
 * onto the target bone's rest orientation, so the skeletons may have
 * different bone axes and rest angles, but should face the same way.
 *
 * Returns: (transfer full): a new #LrgRetargeter
 */
LRG_AVAILABLE_IN_ALL
LrgRetargeter *
lrg_retargeter_new (LrgSkeleton *source,
                    LrgSkeleton *target,
                    LrgBoneMap  *map);

/**
 * lrg_retargeter_get_source:
 * @self: an #LrgRetargeter
 *
 * Returns: (transfer none): the source skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *
lrg_retargeter_get_source (LrgRetargeter *self);

/**
 * lrg_retargeter_get_target:
 * @self: an #LrgRetargeter
 *
 * Returns: (transfer none): the target skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *
lrg_retargeter_get_target (LrgRetargeter *self);

/**
 * lrg_retargeter_get_bone_map:
 * @self: an #LrgRetargeter
 *
 * Returns: (transfer none): the bone map (edits take effect on the next
 *   retarget)
 */
LRG_AVAILABLE_IN_ALL
LrgBoneMap *
lrg_retargeter_get_bone_map (LrgRetargeter *self);

/**
 * lrg_retargeter_set_translation_mode:
 * @self: an #LrgRetargeter
 * @mode: which bones take translation
 */
LRG_AVAILABLE_IN_ALL
void
lrg_retargeter_set_translation_mode (LrgRetargeter              *self,
                                     LrgRetargetTranslationMode  mode);

/**
 * lrg_retargeter_get_translation_mode:
 * @self: an #LrgRetargeter
 *
 * Returns: the translation mode (default %LRG_RETARGET_TRANSLATION_ROOT)
 */
LRG_AVAILABLE_IN_ALL
LrgRetargetTranslationMode
lrg_retargeter_get_translation_mode (LrgRetargeter *self);

/**
 * lrg_retargeter_set_translation_scale:
 * @self: an #LrgRetargeter
 * @scale: factor applied to source movement, or 0 for automatic (the
 *   ratio of the root bones' rest heights, target over source)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_retargeter_set_translation_scale (LrgRetargeter *self,
                                      gfloat         scale);

/**
 * lrg_retargeter_get_translation_scale:
 * @self: an #LrgRetargeter
 *
 * Returns: the factor in use (the automatic one when set to 0)
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_retargeter_get_translation_scale (LrgRetargeter *self);

/**
 * lrg_retargeter_set_root_bone:
 * @self: an #LrgRetargeter
 * @target_bone: (nullable): the target bone that takes the source root's
 *   movement, or %NULL for the topmost mapped bone (the hips, typically)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_retargeter_set_root_bone (LrgRetargeter *self,
                              const gchar   *target_bone);

/**
 * lrg_retargeter_get_root_bone:
 * @self: an #LrgRetargeter
 *
 * Returns: (transfer none) (nullable): the root bone in use, %NULL when
 *   no bone is mapped
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_retargeter_get_root_bone (LrgRetargeter *self);

/**
 * lrg_retargeter_set_sample_rate:
 * @self: an #LrgRetargeter
 * @fps: keys per second written by lrg_retargeter_retarget_clip()
 *   (1 .. 240; default 30)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_retargeter_set_sample_rate (LrgRetargeter *self,
                                gfloat         fps);

/**
 * lrg_retargeter_get_sample_rate:
 * @self: an #LrgRetargeter
 *
 * Returns: the clip sample rate
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_retargeter_get_sample_rate (LrgRetargeter *self);

/**
 * lrg_retargeter_get_mapping:
 * @self: an #LrgRetargeter
 * @n_target: (out) (optional): number of entries
 *
 * The resolved pairing (lrg_bone_map_resolve()), refreshed when the map
 * or the settings change.
 *
 * Returns: (transfer none) (array length=n_target) (nullable): source
 *   bone index per target bone index (-1 unmapped), %NULL when a
 *   skeleton's indices are not 0 .. count - 1
 */
LRG_AVAILABLE_IN_ALL
const gint *
lrg_retargeter_get_mapping (LrgRetargeter *self,
                            guint         *n_target);

/**
 * lrg_retargeter_retarget_pose:
 * @self: an #LrgRetargeter
 * @source_local: (array length=n_source): the source's local poses by
 *   bone index
 * @n_source: the source's bone count
 * @out_target_local: (array length=n_target) (out caller-allocates): the
 *   target's local poses by bone index
 * @n_target: the target's bone count
 *
 * Retargets one pose. Mapped target bones get the source bone's
 * skeleton-space rotation change on top of their rest rotation; unmapped
 * bones keep their bind pose; translations follow the translation mode.
 *
 * Returns: %FALSE when the counts do not match the skeletons or nothing
 *   could be resolved
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_retargeter_retarget_pose (LrgRetargeter     *self,
                              const LrgBonePose *source_local,
                              guint              n_source,
                              LrgBonePose       *out_target_local,
                              guint              n_target);

/**
 * lrg_retargeter_retarget_skeleton:
 * @self: an #LrgRetargeter
 *
 * Live retargeting: reads the source skeleton's current local poses,
 * writes the target skeleton's local poses and recalculates its world
 * poses.
 *
 * Returns: %FALSE when nothing could be resolved
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_retargeter_retarget_skeleton (LrgRetargeter *self);

/**
 * lrg_retargeter_retarget_clip:
 * @self: an #LrgRetargeter
 * @clip: a clip made for the source skeleton (tracks named by source
 *   bones)
 * @error: return location for a #GError
 *
 * Bakes @clip onto the target: it is sampled at the sample rate (plus
 * its last instant), each sample retargeted, and written as linear
 * channels: rotation for every mapped target bone, translation for the
 * bones the translation mode moves. Base poses are the target bind
 * poses; morph weight channels, events, duration and loop mode are
 * copied.
 *
 * Errors: %LRG_RETARGET_ERROR_SKELETON when the skeletons cannot be
 * resolved or no bone is mapped.
 *
 * Returns: (transfer full) (nullable): the retargeted clip, or %NULL on
 *   error
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip *
lrg_retargeter_retarget_clip (LrgRetargeter     *self,
                              LrgAnimationClip  *clip,
                              GError           **error);

G_END_DECLS
