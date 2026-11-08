/* lrg-physics-layers.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsLayers - up to 32 named collision (object) layers and a
 * symmetric collide matrix. A body carries one layer index plus a mask
 * of the layers it accepts; two bodies may collide when the matrix
 * lets their layers collide and each mask holds the other's layer bit.
 * The matrix is project data: it lives in the physics: block of
 * project.ryaml (see docs/modules/physics/physics-layers.org).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_PHYSICS_LAYERS_MAX:
 *
 * Number of layer slots (indices 0 to 31).
 */
#define LRG_PHYSICS_LAYERS_MAX (32u)

/**
 * LRG_PHYSICS_LAYERS_ALL:
 *
 * A mask with every layer bit set.
 */
#define LRG_PHYSICS_LAYERS_ALL (0xFFFFFFFFu)

/**
 * LRG_PHYSICS_LAYER_BIT:
 * @index: a layer index below %LRG_PHYSICS_LAYERS_MAX
 *
 * The mask bit of one layer.
 */
#define LRG_PHYSICS_LAYER_BIT(index) ((guint32) (1u << (guint) (index)))

/**
 * LrgPhysicsLayer:
 * @LRG_PHYSICS_LAYER_DEFAULT: layer 0, "default"
 * @LRG_PHYSICS_LAYER_STATIC: layer 1, "static" (level geometry)
 * @LRG_PHYSICS_LAYER_DYNAMIC: layer 2, "dynamic" (props, rigid bodies)
 * @LRG_PHYSICS_LAYER_KINEMATIC: layer 3, "kinematic" (moving platforms)
 * @LRG_PHYSICS_LAYER_CHARACTER: layer 4, "character" (controllers)
 * @LRG_PHYSICS_LAYER_TRIGGER: layer 5, "trigger" (sensors)
 * @LRG_PHYSICS_LAYER_DEBRIS: layer 6, "debris" (fracture chunks, gibs)
 * @LRG_PHYSICS_LAYER_RAGDOLL: layer 7, "ragdoll"
 * @LRG_PHYSICS_LAYER_PROJECTILE: layer 8, "projectile"
 *
 * The layers lrg_physics_layers_reset_default() names. Projects are free
 * to rename or reuse any slot; these are only the defaults.
 */
typedef enum
{
	LRG_PHYSICS_LAYER_DEFAULT    = 0,
	LRG_PHYSICS_LAYER_STATIC     = 1,
	LRG_PHYSICS_LAYER_DYNAMIC    = 2,
	LRG_PHYSICS_LAYER_KINEMATIC  = 3,
	LRG_PHYSICS_LAYER_CHARACTER  = 4,
	LRG_PHYSICS_LAYER_TRIGGER    = 5,
	LRG_PHYSICS_LAYER_DEBRIS     = 6,
	LRG_PHYSICS_LAYER_RAGDOLL    = 7,
	LRG_PHYSICS_LAYER_PROJECTILE = 8
} LrgPhysicsLayer;

LRG_AVAILABLE_IN_ALL
GType lrg_physics_layer_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PHYSICS_LAYER (lrg_physics_layer_get_type ())

/**
 * LRG_PHYSICS_LAYERS_ERROR:
 *
 * Error domain for #LrgPhysicsLayers. Errors carry an
 * #LrgPhysicsLayersError code.
 */
#define LRG_PHYSICS_LAYERS_ERROR (lrg_physics_layers_error_quark ())

/**
 * LrgPhysicsLayersError:
 * @LRG_PHYSICS_LAYERS_ERROR_PARSE: the YAML text does not parse
 * @LRG_PHYSICS_LAYERS_ERROR_FORMAT: the physics block has a wrong shape
 *   (e.g. layers is not a sequence, a pair is not two entries)
 * @LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER: a name names no layer, or an
 *   index is out of range
 * @LRG_PHYSICS_LAYERS_ERROR_DUPLICATE: two layers share a name
 * @LRG_PHYSICS_LAYERS_ERROR_FULL: all 32 slots are named
 *
 * Error codes for the %LRG_PHYSICS_LAYERS_ERROR domain.
 */
typedef enum
{
	LRG_PHYSICS_LAYERS_ERROR_PARSE = 1,
	LRG_PHYSICS_LAYERS_ERROR_FORMAT = 2,
	LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER = 3,
	LRG_PHYSICS_LAYERS_ERROR_DUPLICATE = 4,
	LRG_PHYSICS_LAYERS_ERROR_FULL = 5
} LrgPhysicsLayersError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_physics_layers_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_physics_layers_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PHYSICS_LAYERS_ERROR (lrg_physics_layers_error_get_type ())

#define LRG_TYPE_PHYSICS_LAYERS (lrg_physics_layers_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhysicsLayers, lrg_physics_layers, LRG, PHYSICS_LAYERS, GObject)

/**
 * lrg_physics_layers_new:
 *
 * Creates a table with no named layers where every layer collides with
 * every layer (the matrix is all ones). Call
 * lrg_physics_layers_reset_default() for the default layer set.
 *
 * Returns: (transfer full): a new #LrgPhysicsLayers
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsLayers * lrg_physics_layers_new (void);

/**
 * lrg_physics_layers_new_default:
 *
 * Creates a table holding the default layers and matrix, see
 * lrg_physics_layers_reset_default().
 *
 * Returns: (transfer full): a new #LrgPhysicsLayers
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsLayers * lrg_physics_layers_new_default (void);

/**
 * lrg_physics_layers_copy:
 * @self: an #LrgPhysicsLayers
 *
 * Returns: (transfer full): a new table with the same names and matrix
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsLayers * lrg_physics_layers_copy (LrgPhysicsLayers *self);

/**
 * lrg_physics_layers_assign:
 * @self: an #LrgPhysicsLayers
 * @other: the table to copy from
 *
 * Replaces the names and matrix of @self with those of @other and emits
 * #LrgPhysicsLayers::changed once if anything differed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_layers_assign (LrgPhysicsLayers *self,
                                LrgPhysicsLayers *other);

/**
 * lrg_physics_layers_equal:
 * @self: an #LrgPhysicsLayers
 * @other: another #LrgPhysicsLayers
 *
 * Returns: %TRUE if both tables have the same names and matrix
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_layers_equal (LrgPhysicsLayers *self,
                                   LrgPhysicsLayers *other);

/**
 * lrg_physics_layers_reset:
 * @self: an #LrgPhysicsLayers
 *
 * Clears every name and sets the matrix to all ones (the state of
 * lrg_physics_layers_new()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_layers_reset (LrgPhysicsLayers *self);

/**
 * lrg_physics_layers_reset_default:
 * @self: an #LrgPhysicsLayers
 *
 * Names layers 0 to 8 after #LrgPhysicsLayer and sets the default
 * matrix: everything collides except static-static, static-kinematic,
 * kinematic-kinematic, trigger-trigger, trigger-static, debris-debris,
 * debris-character, debris-projectile and debris-trigger. Unnamed slots
 * 9 to 31 collide with everything.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_layers_reset_default (LrgPhysicsLayers *self);

/**
 * lrg_physics_layers_set_name:
 * @self: an #LrgPhysicsLayers
 * @index: a layer index below %LRG_PHYSICS_LAYERS_MAX
 * @name: (nullable): the name, or %NULL / "" to unname the slot
 * @error: return location for a #GError
 *
 * Names a slot. Names are unique (a name already used by another slot
 * fails with %LRG_PHYSICS_LAYERS_ERROR_DUPLICATE).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_layers_set_name (LrgPhysicsLayers  *self,
                                      guint              index,
                                      const gchar       *name,
                                      GError           **error);

/**
 * lrg_physics_layers_get_name:
 * @self: an #LrgPhysicsLayers
 * @index: a layer index
 *
 * Returns: (nullable): the name of the slot, or %NULL when unnamed or
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_physics_layers_get_name (LrgPhysicsLayers *self,
                                           guint             index);

/**
 * lrg_physics_layers_add:
 * @self: an #LrgPhysicsLayers
 * @name: a new, unique, non-empty name
 * @error: return location for a #GError
 *
 * Names the lowest unnamed slot. Its matrix row is left as it is (all
 * ones unless it was edited).
 *
 * Returns: the slot index, or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_physics_layers_add (LrgPhysicsLayers  *self,
                             const gchar       *name,
                             GError           **error);

/**
 * lrg_physics_layers_find:
 * @self: an #LrgPhysicsLayers
 * @name: a layer name
 *
 * Returns: the slot index named @name, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_physics_layers_find (LrgPhysicsLayers *self,
                              const gchar      *name);

/**
 * lrg_physics_layers_get_count:
 * @self: an #LrgPhysicsLayers
 *
 * Returns: one past the highest named slot (0 when nothing is named);
 *   editors show this many rows
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_layers_get_count (LrgPhysicsLayers *self);

/**
 * lrg_physics_layers_set_collides:
 * @self: an #LrgPhysicsLayers
 * @a: a layer index
 * @b: a layer index
 * @collides: whether the two layers collide
 *
 * Sets one cell and its mirror (the matrix stays symmetric).
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_layers_set_collides (LrgPhysicsLayers *self,
                                      guint             a,
                                      guint             b,
                                      gboolean          collides);

/**
 * lrg_physics_layers_get_collides:
 * @self: an #LrgPhysicsLayers
 * @a: a layer index
 * @b: a layer index
 *
 * Returns: %TRUE if layers @a and @b collide (%FALSE out of range)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_layers_get_collides (LrgPhysicsLayers *self,
                                          guint             a,
                                          guint             b);

/**
 * lrg_physics_layers_get_collide_mask:
 * @self: an #LrgPhysicsLayers
 * @layer: a layer index
 *
 * Returns: the matrix row of @layer: bit i is set when @layer collides
 *   with layer i (0 out of range)
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_physics_layers_get_collide_mask (LrgPhysicsLayers *self,
                                             guint             layer);

/**
 * lrg_physics_layers_set_collide_mask:
 * @self: an #LrgPhysicsLayers
 * @layer: a layer index
 * @mask: the new row
 *
 * Sets a whole row and the matching column, keeping symmetry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_layers_set_collide_mask (LrgPhysicsLayers *self,
                                          guint             layer,
                                          guint32           mask);

/**
 * lrg_physics_layers_should_collide:
 * @self: (nullable): an #LrgPhysicsLayers, or %NULL for "all collide"
 * @layer_a: layer of the first body
 * @mask_a: mask of the first body
 * @layer_b: layer of the second body
 * @mask_b: mask of the second body
 *
 * The full pair rule: the matrix lets the layers collide, @mask_a holds
 * the bit of @layer_b and @mask_b holds the bit of @layer_a.
 *
 * Returns: %TRUE if the two bodies may collide
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_layers_should_collide (LrgPhysicsLayers *self,
                                            guint             layer_a,
                                            guint32           mask_a,
                                            guint             layer_b,
                                            guint32           mask_b);

/**
 * lrg_physics_layers_mask_from_names:
 * @self: an #LrgPhysicsLayers
 * @names: (array zero-terminated=1): layer names (or decimal indices)
 * @out_mask: (out): the mask
 * @error: return location for a #GError
 *
 * Builds a body mask from names, e.g. { "static", "dynamic", NULL }.
 * The single name "all" gives %LRG_PHYSICS_LAYERS_ALL.
 *
 * Returns: %TRUE on success, %FALSE if a name is unknown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_layers_mask_from_names (LrgPhysicsLayers    *self,
                                             const gchar * const *names,
                                             guint32             *out_mask,
                                             GError             **error);

/**
 * lrg_physics_layers_mask_to_names:
 * @self: an #LrgPhysicsLayers
 * @mask: a body mask
 *
 * Lists the bits of @mask in index order, as names (or decimal indices
 * for unnamed slots). %LRG_PHYSICS_LAYERS_ALL gives { "all" }.
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_physics_layers_mask_to_names (LrgPhysicsLayers *self,
                                        guint32           mask);

/**
 * lrg_physics_layers_resolve:
 * @self: an #LrgPhysicsLayers
 * @name_or_index: a layer name or a decimal index
 * @error: return location for a #GError
 *
 * Resolves a layer reference as found in .rlevel and project files.
 *
 * Returns: the layer index, or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_physics_layers_resolve (LrgPhysicsLayers  *self,
                                 const gchar       *name_or_index,
                                 GError           **error);

/**
 * lrg_physics_layers_load_from_data:
 * @self: an #LrgPhysicsLayers
 * @data: YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @error: return location for a #GError
 *
 * Loads the layers from YAML text. @data may be a whole project.ryaml
 * (the root physics: block is used; a physics: inside project: is
 * accepted too) or a document whose root is the block itself. A file
 * without a physics: block leaves the table at
 * lrg_physics_layers_new() state (no names, all collide) and succeeds.
 * Unknown keys inside the block are ignored. On error @self is left
 * unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_layers_load_from_data (LrgPhysicsLayers  *self,
                                            const gchar       *data,
                                            gssize             length,
                                            GError           **error);

/**
 * lrg_physics_layers_load_from_file:
 * @self: an #LrgPhysicsLayers
 * @path: a YAML file, usually a project.ryaml
 * @error: return location for a #GError
 *
 * Reads @path and calls lrg_physics_layers_load_from_data().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_layers_load_from_file (LrgPhysicsLayers  *self,
                                            const gchar       *path,
                                            GError           **error);

/**
 * lrg_physics_layers_to_data:
 * @self: an #LrgPhysicsLayers
 * @error: return location for a #GError
 *
 * Writes a YAML document whose root mapping holds one physics: block
 * (layers and no_collide), the same text project.ryaml carries.
 *
 * Returns: (transfer full) (nullable): the YAML text, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_physics_layers_to_data (LrgPhysicsLayers  *self,
                                    GError           **error);

/* ==========================================================================
 * LrgPhysicsSettings: the world settings of the physics: block
 * ========================================================================== */

/**
 * LRG_PHYSICS_SETTINGS_MAX_ITERATIONS:
 *
 * The largest #LrgPhysicsSettings.iterations value (solver passes per
 * substep).
 */
#define LRG_PHYSICS_SETTINGS_MAX_ITERATIONS (32u)

/**
 * LRG_PHYSICS_SETTINGS_MAX_SUBSTEPS:
 *
 * The largest #LrgPhysicsSettings.max_substeps value (the same cap as
 * %LRG_DYNAMICS_MAX_SUBSTEPS).
 */
#define LRG_PHYSICS_SETTINGS_MAX_SUBSTEPS (8u)

/**
 * LrgPhysicsSettings:
 * @gravity: world acceleration in m/s² (Y up: a negative Y component);
 *   default { 0, -9.81, 0 }
 * @fixed_dt: the fixed substep in seconds, in (0, 0.1]; default 1/60
 * @max_substeps: substeps one step may run, 1 to
 *   %LRG_PHYSICS_SETTINGS_MAX_SUBSTEPS; default 4
 * @iterations: solver velocity passes per substep, 1 to
 *   %LRG_PHYSICS_SETTINGS_MAX_ITERATIONS; default 10
 * @sleep_threshold: a body whose |v|²+|ω|² stays below this is quiet;
 *   zero or positive, default 9e-4
 * @sleep_substeps: quiet substeps before an island sleeps, at least 1;
 *   default 30
 *
 * The world settings a project keeps in the `physics:` block of
 * `project.ryaml`, next to the layer table (#LrgPhysicsLayers):
 * gravity, the fixed substep and its cap, solver iterations and the
 * sleep threshold. A plain struct (boxed for bindings): fill it with
 * lrg_physics_settings_init_default(), edit the fields, check them with
 * lrg_physics_settings_validate() and hand it to
 * lrg_dynamics_world_apply_settings().
 *
 * The block is read with lrg_physics_settings_load_from_data() /
 * lrg_physics_settings_load_from_file() and written back with
 * lrg_physics_settings_write_data() / lrg_physics_settings_save_to_file(),
 * which replace only these keys (and optionally the layer table) and
 * keep every other key of the file. lrg_world3d_ensure_physics() and
 * lrg_level_instantiate_3d() apply a project's settings automatically,
 * see docs/modules/physics/physics-layers.org.
 */
typedef struct _LrgPhysicsSettings LrgPhysicsSettings;

struct _LrgPhysicsSettings
{
	gfloat gravity[3];
	gfloat fixed_dt;
	guint  max_substeps;
	guint  iterations;
	gfloat sleep_threshold;
	guint  sleep_substeps;

	/*< private >*/
	gpointer reserved[4];
};

#define LRG_TYPE_PHYSICS_SETTINGS (lrg_physics_settings_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_physics_settings_get_type (void) G_GNUC_CONST;

/**
 * lrg_physics_settings_new:
 *
 * Creates settings holding the defaults (see
 * lrg_physics_settings_init_default()).
 *
 * Returns: (transfer full): new settings; free with
 *   lrg_physics_settings_free()
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsSettings * lrg_physics_settings_new (void);

/**
 * lrg_physics_settings_copy:
 * @self: settings
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsSettings * lrg_physics_settings_copy (const LrgPhysicsSettings *self);

/**
 * lrg_physics_settings_free:
 * @self: (nullable): settings from lrg_physics_settings_new() or
 *   lrg_physics_settings_copy()
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_settings_free (LrgPhysicsSettings *self);

/**
 * lrg_physics_settings_init_default:
 * @self: settings to fill (may live on the stack)
 *
 * Sets every field to its default: gravity { 0, -9.81, 0 }, fixed_dt
 * 1/60, max_substeps 4, iterations 10, sleep_threshold 9e-4,
 * sleep_substeps 30. These are the values an #LrgDynamicsWorld starts
 * with, except gravity, which a bare world leaves at zero.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_settings_init_default (LrgPhysicsSettings *self);

/**
 * lrg_physics_settings_equal:
 * @a: settings
 * @b: settings
 *
 * Returns: %TRUE when every field of @a equals the one of @b
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_settings_equal (const LrgPhysicsSettings *a,
                                     const LrgPhysicsSettings *b);

/**
 * lrg_physics_settings_validate:
 * @self: settings
 * @error: return location for a #GError
 *
 * Checks every field against its range (see #LrgPhysicsSettings).
 * Fails with %LRG_PHYSICS_LAYERS_ERROR_FORMAT naming the first bad
 * field.
 *
 * Returns: %TRUE when the settings are usable
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_settings_validate (const LrgPhysicsSettings  *self,
                                        GError                   **error);

/**
 * lrg_physics_settings_load_from_data:
 * @self: settings
 * @data: YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @error: return location for a #GError
 *
 * Loads the world settings from YAML text: a whole project.ryaml (the
 * root `physics:` block, or one nested in `project:`) or a document
 * whose root is the block itself. Missing keys (or a missing block)
 * take their defaults; unknown keys are ignored. Errors are in the
 * %LRG_PHYSICS_LAYERS_ERROR domain: %LRG_PHYSICS_LAYERS_ERROR_PARSE for
 * text that is not YAML, %LRG_PHYSICS_LAYERS_ERROR_FORMAT for a value
 * of the wrong shape or out of range. On error @self is unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_settings_load_from_data (LrgPhysicsSettings  *self,
                                              const gchar         *data,
                                              gssize               length,
                                              GError             **error);

/**
 * lrg_physics_settings_load_from_file:
 * @self: settings
 * @path: a YAML file, usually a project.ryaml
 * @error: return location for a #GError
 *
 * Reads @path and calls lrg_physics_settings_load_from_data().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_settings_load_from_file (LrgPhysicsSettings  *self,
                                              const gchar         *path,
                                              GError             **error);

/**
 * lrg_physics_settings_write_data:
 * @self: settings
 * @layers: (nullable): a layer table to write too, or %NULL to leave the
 *   block's `layers` and `no_collide` as they are
 * @data: (nullable): the current file text (a project.ryaml), or %NULL
 *   for an empty document
 * @length: length of @data, or -1 when NUL-terminated
 * @error: return location for a #GError
 *
 * Returns @data with the world settings (and @layers, when given)
 * written into its `physics:` block. Every other key of the document
 * and of the block is kept; a block nested in `project:` is updated in
 * place, and a document without a block gets one at the root.
 *
 * Returns: (transfer full) (nullable): the new YAML text, or %NULL on
 *   error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_physics_settings_write_data (const LrgPhysicsSettings  *self,
                                         LrgPhysicsLayers          *layers,
                                         const gchar               *data,
                                         gssize                     length,
                                         GError                   **error);

/**
 * lrg_physics_settings_save_to_file:
 * @self: settings
 * @layers: (nullable): a layer table to write too, or %NULL
 * @path: a YAML file, usually a project.ryaml; it need not exist
 * @error: return location for a #GError
 *
 * Reads @path (when it exists), calls lrg_physics_settings_write_data()
 * and writes the result back, keeping every other key of the file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_settings_save_to_file (const LrgPhysicsSettings  *self,
                                            LrgPhysicsLayers          *layers,
                                            const gchar               *path,
                                            GError                   **error);

/**
 * lrg_physics_settings_find_project:
 * @start: (type filename): a file or directory inside a project
 *
 * Walks up from @start (the directory of @start when it is a file) to
 * the first directory holding a `project.ryaml`, the same search the
 * level serializer does for project settings.
 *
 * Returns: (transfer full) (nullable) (type filename): the path of that
 *   `project.ryaml`, or %NULL when there is none
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_physics_settings_find_project (const gchar *start);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPhysicsSettings, lrg_physics_settings_free)

#ifndef __GI_SCANNER__
#ifdef LIBREGNUM_COMPILATION
/*
 * In-tree helpers for LrgProject and other loaders (not installed API:
 * they expose yaml-glib types, which the public GIR does not include).
 */
#include <yaml-glib.h>

/*
 * lrg_physics_layers_load_yaml:
 * @node: the physics: block (a mapping); NULL resets to new() state
 *
 * Loads layers and no_collide from the block. Unknown keys are ignored;
 * on error @self is unchanged.
 */
G_GNUC_INTERNAL
gboolean lrg_physics_layers_load_yaml (LrgPhysicsLayers  *self,
                                       YamlNode          *node,
                                       GError           **error);

/*
 * lrg_physics_layers_write_yaml:
 * @map: the physics: mapping to update
 *
 * Sets (replaces) the layers and no_collide members of @map and leaves
 * every other member as it is, so other physics settings survive.
 */
G_GNUC_INTERNAL
void lrg_physics_layers_write_yaml (LrgPhysicsLayers *self,
                                    YamlMapping      *map);

/*
 * lrg_physics_layers_find_block:
 * @root: a parsed project.ryaml root
 *
 * Returns: (transfer none) (nullable): the physics: mapping node (root
 * level first, then inside project:), or NULL.
 */
G_GNUC_INTERNAL
YamlNode * lrg_physics_layers_find_block (YamlNode *root);

/*
 * lrg_physics_settings_load_yaml:
 * @node: the physics: block (a mapping); NULL gives the defaults
 *
 * Loads the world settings from the block. Missing keys take their
 * defaults, unknown keys are ignored; on error @self is unchanged.
 */
G_GNUC_INTERNAL
gboolean lrg_physics_settings_load_yaml (LrgPhysicsSettings  *self,
                                         YamlNode            *node,
                                         GError             **error);

/*
 * lrg_physics_settings_write_yaml:
 * @map: the physics: mapping to update
 *
 * Sets (replaces) gravity, fixed_dt, max_substeps, iterations,
 * sleep_threshold and sleep_substeps and keeps every other member.
 */
G_GNUC_INTERNAL
void lrg_physics_settings_write_yaml (const LrgPhysicsSettings *self,
                                      YamlMapping              *map);
#endif
#endif

G_END_DECLS
