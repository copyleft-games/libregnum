/* lrg-particle-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The `.rparticles` particle effect format and its runtime:
 *
 * - LrgParticleAsset: an effect document: settings, seed and a list of
 *   emitters (YAML load/save, byte-stable, unknown keys kept).
 * - LrgParticleAssetEmitter: one emitter: a module stack and a
 *   renderer block (LrgParticleRenderSettings).
 * - The built-in module kinds (spawn-rate, burst, shape, velocity, ...,
 *   sub-emitter): LrgParticleModule subclasses created by name.
 * - LrgParticleEffect: a playing instance of an asset: one
 *   LrgParticleSystem per emitter, its renderer, sub-emitter wiring,
 *   transform, seeking and live reload for the editor.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../core/lrg-asset-manager.h"
#include "../core/lrg-color-gradient.h"
#include "lrg-particle-system.h"
#include "lrg-particle-billboard-renderer.h"
#include "lrg-particle-ribbon-renderer.h"
#include "lrg-particle-mesh-renderer.h"

G_BEGIN_DECLS

/**
 * LRG_PARTICLE_ASSET_FORMAT_VERSION:
 *
 * The `.rparticles` version this build writes (and the newest it reads).
 */
#define LRG_PARTICLE_ASSET_FORMAT_VERSION (1)

/* ==========================================================================
 * Errors and enums
 * ========================================================================== */

/**
 * LrgParticleAssetError:
 * @LRG_PARTICLE_ASSET_ERROR_PARSE: not YAML, or a value of the wrong shape
 * @LRG_PARTICLE_ASSET_ERROR_VALUE: a value out of range or not allowed
 * @LRG_PARTICLE_ASSET_ERROR_UNKNOWN_MODULE: a module `type:` no kind is
 *   registered under
 * @LRG_PARTICLE_ASSET_ERROR_VERSION: the file is newer than this build
 * @LRG_PARTICLE_ASSET_ERROR_REFERENCE: a sub-emitter names a missing
 *   emitter, or emitter names repeat
 * @LRG_PARTICLE_ASSET_ERROR_IO: reading or writing the file failed
 *
 * Errors of the `.rparticles` loader, writer and validation.  Messages
 * read "source: /key/path: what is wrong".
 */
typedef enum
{
	LRG_PARTICLE_ASSET_ERROR_PARSE,
	LRG_PARTICLE_ASSET_ERROR_VALUE,
	LRG_PARTICLE_ASSET_ERROR_UNKNOWN_MODULE,
	LRG_PARTICLE_ASSET_ERROR_VERSION,
	LRG_PARTICLE_ASSET_ERROR_REFERENCE,
	LRG_PARTICLE_ASSET_ERROR_IO
} LrgParticleAssetError;

/**
 * lrg_particle_asset_error_quark:
 *
 * Returns: the error domain of #LrgParticleAssetError
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_particle_asset_error_quark (void);
#define LRG_PARTICLE_ASSET_ERROR (lrg_particle_asset_error_quark ())

/**
 * LrgParticleShape:
 * @LRG_PARTICLE_SHAPE_POINT: the origin; direction straight along +Y
 * @LRG_PARTICLE_SHAPE_SPHERE: a ball of the radius; direction outward
 * @LRG_PARTICLE_SHAPE_HEMISPHERE: the upper (+Y) half ball; outward
 * @LRG_PARTICLE_SHAPE_CONE: a disc of the radius at the base of a cone
 *   opening by the angle around +Y; direction along the cone's side
 * @LRG_PARTICLE_SHAPE_BOX: a box of the size; direction +Y
 * @LRG_PARTICLE_SHAPE_CIRCLE: a disc of the radius in the XZ plane;
 *   direction outward in the plane
 * @LRG_PARTICLE_SHAPE_MESH: the surface of a mesh (area weighted);
 *   direction along the triangle normal
 *
 * Where the shape module places new particles (before the system's
 * rotation and the module's offset).
 */
typedef enum
{
	LRG_PARTICLE_SHAPE_POINT,
	LRG_PARTICLE_SHAPE_SPHERE,
	LRG_PARTICLE_SHAPE_HEMISPHERE,
	LRG_PARTICLE_SHAPE_CONE,
	LRG_PARTICLE_SHAPE_BOX,
	LRG_PARTICLE_SHAPE_CIRCLE,
	LRG_PARTICLE_SHAPE_MESH
} LrgParticleShape;

/**
 * lrg_particle_shape_get_type:
 *
 * Returns: the #GType of #LrgParticleShape
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_SHAPE (lrg_particle_shape_get_type ())

/**
 * LrgParticleCollisionMode:
 * @LRG_PARTICLE_COLLISION_PLANE: against the infinite plane
 *   dot (normal, p) = offset (world space)
 * @LRG_PARTICLE_COLLISION_WORLD: against the world through the system's
 *   raycast function (lrg_particle_system_set_raycast_func())
 *
 * What the collision module tests particles against.
 */
typedef enum
{
	LRG_PARTICLE_COLLISION_PLANE,
	LRG_PARTICLE_COLLISION_WORLD
} LrgParticleCollisionMode;

/**
 * lrg_particle_collision_mode_get_type:
 *
 * Returns: the #GType of #LrgParticleCollisionMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_collision_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_COLLISION_MODE (lrg_particle_collision_mode_get_type ())

/**
 * LrgParticleRendererKind:
 * @LRG_PARTICLE_RENDERER_NONE: not drawn (a sub-emitter's hidden carrier,
 *   or simulation only)
 * @LRG_PARTICLE_RENDERER_BILLBOARD: camera-facing quads
 *   (#LrgParticleBillboardRenderer)
 * @LRG_PARTICLE_RENDERER_RIBBON: trails or a connecting ribbon
 *   (#LrgParticleRibbonRenderer)
 * @LRG_PARTICLE_RENDERER_MESH: instanced meshes (#LrgParticleMeshRenderer)
 *
 * How an emitter's particles are drawn.
 */
typedef enum
{
	LRG_PARTICLE_RENDERER_NONE,
	LRG_PARTICLE_RENDERER_BILLBOARD,
	LRG_PARTICLE_RENDERER_RIBBON,
	LRG_PARTICLE_RENDERER_MESH
} LrgParticleRendererKind;

/**
 * lrg_particle_renderer_kind_get_type:
 *
 * Returns: the #GType of #LrgParticleRendererKind
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_renderer_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_RENDERER_KIND (lrg_particle_renderer_kind_get_type ())

/* ==========================================================================
 * Module kinds
 * ========================================================================== */

/**
 * lrg_particle_module_new:
 * @kind: a registered module kind: "spawn-rate", "burst", "shape",
 *   "velocity", "lifetime", "size", "color", "rotation",
 *   "size-over-life", "color-over-life", "alpha-over-life",
 *   "rotation-over-life", "gravity", "wind", "drag", "turbulence",
 *   "vortex", "attractor", "collision", "sub-emitter", or one added with
 *   lrg_particle_module_register_kind()
 *
 * Creates a module with default parameters.  Set them as GObject
 * properties (g_object_set()); the property names are the `.rparticles`
 * keys (see docs/modules/particles/particle-asset.org).
 *
 * Returns: (transfer full) (nullable): the module, or %NULL for an
 *   unknown kind
 */
LRG_AVAILABLE_IN_ALL
LrgParticleModule * lrg_particle_module_new (const gchar *kind);

/**
 * lrg_particle_module_register_kind:
 * @kind: the `type:` name in files
 * @type: an instantiatable #LrgParticleModule subclass
 *
 * Registers a custom module kind so files can name it and
 * lrg_particle_module_new() can create it.  Its properties are read and
 * written like the built-ins' (numbers, booleans, strings, enums by
 * nick, #GrlVector2 ranges, #GrlVector3, #GrlColor, #LrgGradientStops,
 * #LrgKeyframeCurve).  Re-registering a name replaces it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_module_register_kind (const gchar *kind,
                                        GType        type);

/**
 * lrg_particle_module_lookup_kind:
 * @kind: a module kind name
 *
 * Returns: the registered type, or %G_TYPE_INVALID
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_module_lookup_kind (const gchar *kind);

/**
 * lrg_particle_module_list_kinds:
 *
 * Returns: (transfer full): every registered kind name, built-ins first
 *   in stage order, then custom kinds in registration order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_particle_module_list_kinds (void);

/**
 * lrg_particle_module_get_kind:
 * @self: an #LrgParticleModule
 *
 * Returns: (nullable): the kind name @self's type is registered under
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_particle_module_get_kind (LrgParticleModule *self);

/**
 * lrg_particle_module_get_gpu_capable:
 * @self: an #LrgParticleModule
 *
 * Whether the module's behaviour is a pure function of the particle and
 * the clock, so a GPU simulation can run it: every built-in except
 * sub-emitter, collision in world mode and shape in mesh mode; %FALSE
 * for custom kinds.
 *
 * Returns: %TRUE when a GPU path may run the module
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_module_get_gpu_capable (LrgParticleModule *self);

/**
 * lrg_particle_module_set_surface:
 * @self: a "shape" module
 * @positions: (array length=n_floats): vertex positions, x y z each,
 *   in the effect's local space
 * @n_floats: number of floats in @positions (3 per vertex)
 * @indices: (array length=n_indices) (nullable): triangle indices, 3 per
 *   triangle; %NULL for a plain triangle list
 * @n_indices: number of indices
 *
 * Gives a shape module the triangles of %LRG_PARTICLE_SHAPE_MESH (the
 * effect fills them from the module's `mesh` asset when it has an asset
 * manager and a GPU context; tests and tools pass them directly).
 * Triangles are sampled by area.
 *
 * Returns: %TRUE if @self is a shape module and at least one triangle has
 *   area
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_module_set_surface (LrgParticleModule *self,
                                          const gfloat      *positions,
                                          guint              n_floats,
                                          const guint       *indices,
                                          guint              n_indices);

/* ==========================================================================
 * LrgParticleRenderSettings
 * ========================================================================== */

#define LRG_TYPE_PARTICLE_RENDER_SETTINGS (lrg_particle_render_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleRenderSettings, lrg_particle_render_settings,
                      LRG, PARTICLE_RENDER_SETTINGS, GObject)

/**
 * lrg_particle_render_settings_new:
 *
 * Creates billboard settings with alpha blending on layer 0.  The
 * settings are the emitter's `renderer:` block: every key is a property
 * of the same name (kind, blend, layer, texture, columns, rows, fps;
 * ribbon-mode, width, width-curve, color-gradient, uv-mode,
 * uv-tile-length, max-points, min-distance, trail-lifetime for ribbons;
 * mesh, primitive, mesh-scale, align, lit for meshes).
 *
 * Returns: (transfer full): new settings
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRenderSettings * lrg_particle_render_settings_new (void);

/**
 * lrg_particle_render_settings_get_kind:
 * @self: render settings
 *
 * Returns: the renderer kind
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRendererKind lrg_particle_render_settings_get_kind (LrgParticleRenderSettings *self);

/**
 * lrg_particle_render_settings_set_kind:
 * @self: render settings
 * @kind: the renderer kind
 *
 * Sets #LrgParticleRenderSettings:kind.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_render_settings_set_kind (LrgParticleRenderSettings *self,
                                           LrgParticleRendererKind    kind);

/**
 * lrg_particle_render_settings_get_blend:
 * @self: render settings
 *
 * Returns: the blend mode
 */
LRG_AVAILABLE_IN_ALL
LrgParticleBlendMode lrg_particle_render_settings_get_blend (LrgParticleRenderSettings *self);

/**
 * lrg_particle_render_settings_set_blend:
 * @self: render settings
 * @blend: the blend mode
 *
 * Sets #LrgParticleRenderSettings:blend.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_render_settings_set_blend (LrgParticleRenderSettings *self,
                                            LrgParticleBlendMode       blend);

/**
 * lrg_particle_render_settings_get_texture:
 * @self: render settings
 *
 * Returns: (nullable): the texture asset reference
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_particle_render_settings_get_texture (LrgParticleRenderSettings *self);

/**
 * lrg_particle_render_settings_set_texture:
 * @self: render settings
 * @texture: (nullable): a texture asset reference (a path the asset
 *   manager resolves, or `guid:`), %NULL for the built-in soft dot
 *
 * Sets #LrgParticleRenderSettings:texture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_render_settings_set_texture (LrgParticleRenderSettings *self,
                                              const gchar               *texture);

/* ==========================================================================
 * LrgParticleAssetEmitter
 * ========================================================================== */

#define LRG_TYPE_PARTICLE_ASSET_EMITTER (lrg_particle_asset_emitter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleAssetEmitter, lrg_particle_asset_emitter,
                      LRG, PARTICLE_ASSET_EMITTER, GObject)

/**
 * lrg_particle_asset_emitter_new:
 * @name: the emitter's name (unique in its asset; sub-emitters refer to it)
 *
 * Creates an emitter with an empty module stack, 1000 particles and
 * billboard render settings.
 *
 * Returns: (transfer full): a new emitter
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAssetEmitter * lrg_particle_asset_emitter_new (const gchar *name);

/**
 * lrg_particle_asset_emitter_get_name:
 * @self: an emitter
 *
 * Returns: the name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_particle_asset_emitter_get_name (LrgParticleAssetEmitter *self);

/**
 * lrg_particle_asset_emitter_set_name:
 * @self: an emitter
 * @name: a non-empty name
 *
 * Sets #LrgParticleAssetEmitter:name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_emitter_set_name (LrgParticleAssetEmitter *self,
                                          const gchar             *name);

/**
 * lrg_particle_asset_emitter_get_max_particles:
 * @self: an emitter
 *
 * Returns: the particle capacity
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_asset_emitter_get_max_particles (LrgParticleAssetEmitter *self);

/**
 * lrg_particle_asset_emitter_set_max_particles:
 * @self: an emitter
 * @max_particles: capacity (1 .. 1000000)
 *
 * Sets #LrgParticleAssetEmitter:max-particles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_emitter_set_max_particles (LrgParticleAssetEmitter *self,
                                                   guint                    max_particles);

/**
 * lrg_particle_asset_emitter_get_enabled:
 * @self: an emitter
 *
 * Returns: whether the emitter is simulated and drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_emitter_get_enabled (LrgParticleAssetEmitter *self);

/**
 * lrg_particle_asset_emitter_set_enabled:
 * @self: an emitter
 * @enabled: whether the emitter is simulated and drawn
 *
 * Sets #LrgParticleAssetEmitter:enabled (the editor's solo/mute).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_emitter_set_enabled (LrgParticleAssetEmitter *self,
                                             gboolean                 enabled);

/**
 * lrg_particle_asset_emitter_get_start_delay:
 * @self: an emitter
 *
 * Returns: seconds after play before the emitter emits
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_asset_emitter_get_start_delay (LrgParticleAssetEmitter *self);

/**
 * lrg_particle_asset_emitter_set_start_delay:
 * @self: an emitter
 * @delay: seconds (>= 0)
 *
 * Sets #LrgParticleAssetEmitter:start-delay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_emitter_set_start_delay (LrgParticleAssetEmitter *self,
                                                 gfloat                   delay);

/**
 * lrg_particle_asset_emitter_get_renderer:
 * @self: an emitter
 *
 * Returns: (transfer none): the `renderer:` block
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRenderSettings * lrg_particle_asset_emitter_get_renderer (LrgParticleAssetEmitter *self);

/**
 * lrg_particle_asset_emitter_get_n_modules:
 * @self: an emitter
 *
 * Returns: the number of modules in the stack
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_asset_emitter_get_n_modules (LrgParticleAssetEmitter *self);

/**
 * lrg_particle_asset_emitter_get_module:
 * @self: an emitter
 * @index: a stack index
 *
 * Returns: (transfer none) (nullable): the module, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgParticleModule * lrg_particle_asset_emitter_get_module (LrgParticleAssetEmitter *self,
                                                           guint                    index);

/**
 * lrg_particle_asset_emitter_find_module:
 * @self: an emitter
 * @kind: a module kind name
 *
 * Returns: (transfer none) (nullable): the first module of @kind
 */
LRG_AVAILABLE_IN_ALL
LrgParticleModule * lrg_particle_asset_emitter_find_module (LrgParticleAssetEmitter *self,
                                                            const gchar             *kind);

/**
 * lrg_particle_asset_emitter_insert_module:
 * @self: an emitter
 * @module: (transfer none): a module
 * @index: position, or -1 to append
 *
 * Inserts a module into the stack (a reference is kept).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_emitter_insert_module (LrgParticleAssetEmitter *self,
                                               LrgParticleModule       *module,
                                               gint                     index);

/**
 * lrg_particle_asset_emitter_add_module:
 * @self: an emitter
 * @kind: a module kind name
 *
 * Creates a module of @kind with default parameters and appends it.
 *
 * Returns: (transfer none) (nullable): the new module, or %NULL for an
 *   unknown kind
 */
LRG_AVAILABLE_IN_ALL
LrgParticleModule * lrg_particle_asset_emitter_add_module (LrgParticleAssetEmitter *self,
                                                           const gchar             *kind);

/**
 * lrg_particle_asset_emitter_remove_module:
 * @self: an emitter
 * @index: a stack index
 *
 * Returns: %TRUE if a module was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_emitter_remove_module (LrgParticleAssetEmitter *self,
                                                   guint                    index);

/**
 * lrg_particle_asset_emitter_move_module:
 * @self: an emitter
 * @from: current index
 * @to: new index
 *
 * Reorders the stack (the editor's drag and drop).
 *
 * Returns: %TRUE if both indices are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_emitter_move_module (LrgParticleAssetEmitter *self,
                                                 guint                    from,
                                                 guint                    to);

/* ==========================================================================
 * LrgParticleAsset
 * ========================================================================== */

#define LRG_TYPE_PARTICLE_ASSET (lrg_particle_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleAsset, lrg_particle_asset, LRG, PARTICLE_ASSET, GObject)

/**
 * lrg_particle_asset_new:
 *
 * Creates an empty effect: seed 0, duration 0 (endless), looping, world
 * space, no emitters.
 *
 * Returns: (transfer full): a new #LrgParticleAsset
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAsset * lrg_particle_asset_new (void);

/**
 * lrg_particle_asset_new_from_file:
 * @path: a `.rparticles` file
 * @error: (nullable): return location for a #LRG_PARTICLE_ASSET_ERROR
 *
 * Loads and validates an effect.
 *
 * Returns: (transfer full) (nullable): the effect, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAsset * lrg_particle_asset_new_from_file (const gchar  *path,
                                                     GError      **error);

/**
 * lrg_particle_asset_new_from_data:
 * @data: `.rparticles` text
 * @length: length of @data, or -1 when nul-terminated
 * @source: (nullable): a name for messages ("fire.rparticles")
 * @error: (nullable): return location for a #LRG_PARTICLE_ASSET_ERROR
 *
 * Parses and validates an effect from text (the editor's validation
 * path: no temporary files).
 *
 * Returns: (transfer full) (nullable): the effect, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAsset * lrg_particle_asset_new_from_data (const gchar  *data,
                                                     gssize        length,
                                                     const gchar  *source,
                                                     GError      **error);

/**
 * lrg_particle_asset_save_to_data:
 * @self: an #LrgParticleAsset
 * @length: (out) (optional): length of the text
 * @error: (nullable): return location for an error
 *
 * Writes the effect.  Values equal to their defaults are left out,
 * unknown keys of the loaded document are written back after the known
 * keys of the same mapping, and loading the text and saving again gives
 * the same bytes.
 *
 * Returns: (transfer full) (nullable): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_particle_asset_save_to_data (LrgParticleAsset  *self,
                                         gsize             *length,
                                         GError           **error);

/**
 * lrg_particle_asset_save_to_file:
 * @self: an #LrgParticleAsset
 * @path: destination (written atomically)
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_save_to_file (LrgParticleAsset  *self,
                                          const gchar       *path,
                                          GError           **error);

/**
 * lrg_particle_asset_validate:
 * @self: an #LrgParticleAsset
 * @error: (nullable): return location for a #LRG_PARTICLE_ASSET_ERROR
 *
 * Checks what single values cannot: emitter names are non-empty and
 * unique, and every sub-emitter names another emitter of the asset.
 * Loading validates; call it after editing in code.
 *
 * Returns: %TRUE when the effect is consistent
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_validate (LrgParticleAsset  *self,
                                      GError           **error);

/**
 * lrg_particle_asset_get_unknown_keys:
 * @self: an #LrgParticleAsset
 *
 * Returns: (transfer full): the key paths ("/emitters/0/modules/1/colour")
 *   the last load kept without understanding them, in document order;
 *   the editor shows them as warnings (they are usually typos)
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_particle_asset_get_unknown_keys (LrgParticleAsset *self);

/**
 * lrg_particle_asset_get_name:
 * @self: an #LrgParticleAsset
 *
 * Returns: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_particle_asset_get_name (LrgParticleAsset *self);

/**
 * lrg_particle_asset_set_name:
 * @self: an #LrgParticleAsset
 * @name: (nullable): a display name
 *
 * Sets #LrgParticleAsset:name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_set_name (LrgParticleAsset *self,
                                  const gchar      *name);

/**
 * lrg_particle_asset_get_seed:
 * @self: an #LrgParticleAsset
 *
 * Returns: the effect seed
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_particle_asset_get_seed (LrgParticleAsset *self);

/**
 * lrg_particle_asset_set_seed:
 * @self: an #LrgParticleAsset
 * @seed: the effect seed; emitter i's system is seeded with a hash of
 *   (@seed, i)
 *
 * Sets #LrgParticleAsset:seed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_set_seed (LrgParticleAsset *self,
                                  guint64           seed);

/**
 * lrg_particle_asset_get_duration:
 * @self: an #LrgParticleAsset
 *
 * Returns: seconds per emission cycle (0: endless)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_asset_get_duration (LrgParticleAsset *self);

/**
 * lrg_particle_asset_set_duration:
 * @self: an #LrgParticleAsset
 * @duration: seconds per emission cycle (0: endless)
 *
 * Sets #LrgParticleAsset:duration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_set_duration (LrgParticleAsset *self,
                                      gfloat            duration);

/**
 * lrg_particle_asset_get_loop:
 * @self: an #LrgParticleAsset
 *
 * Returns: whether cycles repeat
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_get_loop (LrgParticleAsset *self);

/**
 * lrg_particle_asset_set_loop:
 * @self: an #LrgParticleAsset
 * @loop: whether cycles repeat
 *
 * Sets #LrgParticleAsset:loop.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_set_loop (LrgParticleAsset *self,
                                  gboolean          loop);

/**
 * lrg_particle_asset_get_n_emitters:
 * @self: an #LrgParticleAsset
 *
 * Returns: the number of emitters
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_asset_get_n_emitters (LrgParticleAsset *self);

/**
 * lrg_particle_asset_get_emitter:
 * @self: an #LrgParticleAsset
 * @index: an emitter index
 *
 * Returns: (transfer none) (nullable): the emitter
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAssetEmitter * lrg_particle_asset_get_emitter (LrgParticleAsset *self,
                                                          guint             index);

/**
 * lrg_particle_asset_find_emitter:
 * @self: an #LrgParticleAsset
 * @name: an emitter name
 *
 * Returns: (transfer none) (nullable): the emitter called @name
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAssetEmitter * lrg_particle_asset_find_emitter (LrgParticleAsset *self,
                                                           const gchar      *name);

/**
 * lrg_particle_asset_add_emitter:
 * @self: an #LrgParticleAsset
 * @emitter: (transfer none): an emitter
 *
 * Appends an emitter (a reference is kept).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_asset_add_emitter (LrgParticleAsset        *self,
                                     LrgParticleAssetEmitter *emitter);

/**
 * lrg_particle_asset_remove_emitter:
 * @self: an #LrgParticleAsset
 * @index: an emitter index
 *
 * Returns: %TRUE if an emitter was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_remove_emitter (LrgParticleAsset *self,
                                            guint             index);

/**
 * lrg_particle_asset_move_emitter:
 * @self: an #LrgParticleAsset
 * @from: current index
 * @to: new index
 *
 * Reorders emitters (draw order follows emitter order).
 *
 * Returns: %TRUE if both indices are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_asset_move_emitter (LrgParticleAsset *self,
                                          guint             from,
                                          guint             to);

/* ==========================================================================
 * LrgParticleEffect
 * ========================================================================== */

#define LRG_TYPE_PARTICLE_EFFECT (lrg_particle_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleEffect, lrg_particle_effect, LRG, PARTICLE_EFFECT, GObject)

/**
 * lrg_particle_effect_new:
 * @asset: the effect to play
 *
 * Builds a playing instance: one #LrgParticleSystem per emitter (named
 * after it, seeded from the asset seed and its index, with copies of its
 * modules) and the emitter's renderer.  Several effects may play one
 * asset.  The effect is stopped; call lrg_particle_effect_play().
 *
 * Returns: (transfer full): a new effect
 */
LRG_AVAILABLE_IN_ALL
LrgParticleEffect * lrg_particle_effect_new (LrgParticleAsset *asset);

/**
 * lrg_particle_asset_instantiate:
 * @self: an #LrgParticleAsset
 *
 * The same as lrg_particle_effect_new() (@self).
 *
 * Returns: (transfer full): a new effect playing @self
 */
LRG_AVAILABLE_IN_ALL
LrgParticleEffect * lrg_particle_asset_instantiate (LrgParticleAsset *self);

/**
 * lrg_particle_effect_get_asset:
 * @self: an effect
 *
 * Returns: (transfer none): the asset it plays
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAsset * lrg_particle_effect_get_asset (LrgParticleEffect *self);

/**
 * lrg_particle_effect_get_n_systems:
 * @self: an effect
 *
 * Returns: the number of systems (one per emitter)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_effect_get_n_systems (LrgParticleEffect *self);

/**
 * lrg_particle_effect_get_system:
 * @self: an effect
 * @index: an emitter index
 *
 * Returns: (transfer none) (nullable): emitter @index's system
 */
LRG_AVAILABLE_IN_ALL
LrgParticleSystem * lrg_particle_effect_get_system (LrgParticleEffect *self,
                                                    guint              index);

/**
 * lrg_particle_effect_find_system:
 * @self: an effect
 * @name: an emitter name
 *
 * Returns: (transfer none) (nullable): the system of the emitter called
 *   @name
 */
LRG_AVAILABLE_IN_ALL
LrgParticleSystem * lrg_particle_effect_find_system (LrgParticleEffect *self,
                                                     const gchar       *name);

/**
 * lrg_particle_effect_get_renderer:
 * @self: an effect
 * @index: an emitter index
 *
 * Returns: (transfer none) (nullable): emitter @index's renderer: an
 *   #LrgParticleBillboardRenderer, #LrgParticleRibbonRenderer or
 *   #LrgParticleMeshRenderer, or %NULL for %LRG_PARTICLE_RENDERER_NONE
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_particle_effect_get_renderer (LrgParticleEffect *self,
                                            guint              index);

/**
 * lrg_particle_effect_play:
 * @self: an effect
 *
 * Plays (after a stop: from the start, prewarmed when the asset asks
 * for it; after a pause: resumes).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_play (LrgParticleEffect *self);

/**
 * lrg_particle_effect_pause:
 * @self: an effect
 *
 * Freezes the simulation (drawing still works).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_pause (LrgParticleEffect *self);

/**
 * lrg_particle_effect_stop:
 * @self: an effect
 *
 * Stops and clears every particle and trail.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_stop (LrgParticleEffect *self);

/**
 * lrg_particle_effect_restart:
 * @self: an effect
 *
 * Stop and play: the same particles again for the same steps.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_restart (LrgParticleEffect *self);

/**
 * lrg_particle_effect_is_playing:
 * @self: an effect
 *
 * Returns: whether the effect is playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_effect_is_playing (LrgParticleEffect *self);

/**
 * lrg_particle_effect_is_alive:
 * @self: an effect
 *
 * Returns: whether any system still emits or has live particles (or a
 *   trail is still fading)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_effect_is_alive (LrgParticleEffect *self);

/**
 * lrg_particle_effect_update:
 * @self: an effect
 * @delta_time: seconds since the last update
 *
 * Steps every enabled system in emitter order, then the trail renderers.
 * Emits #LrgParticleEffect::finished once when a non-looping effect has
 * nothing left alive.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_update (LrgParticleEffect *self,
                                 gfloat             delta_time);

/**
 * lrg_particle_effect_simulate:
 * @self: an effect
 * @seconds: time to advance
 * @step: fixed step in seconds (> 0; 1/60 is typical)
 *
 * Advances by @seconds in fixed steps (the last step shorter), so the
 * result does not depend on the frame rate.  Plays the effect if it was
 * stopped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_simulate (LrgParticleEffect *self,
                                   gfloat             seconds,
                                   gfloat             step);

/**
 * lrg_particle_effect_seek:
 * @self: an effect
 * @time: the time to show, in seconds from the start
 * @step: fixed step (> 0)
 *
 * Restarts and simulates up to @time: the editor's timeline scrubbing.
 * Deterministic: the same @time and @step always give the same state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_seek (LrgParticleEffect *self,
                               gfloat             time,
                               gfloat             step);

/**
 * lrg_particle_effect_get_time:
 * @self: an effect
 *
 * Returns: seconds simulated since the last start
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_effect_get_time (LrgParticleEffect *self);

/**
 * lrg_particle_effect_get_seed:
 * @self: an effect
 *
 * Returns: the seed in use (the asset's, or the override)
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_particle_effect_get_seed (LrgParticleEffect *self);

/**
 * lrg_particle_effect_set_seed:
 * @self: an effect
 * @seed: a seed overriding the asset's (instances of one asset that
 *   should differ)
 *
 * Reseeds every system; takes effect at the next start.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_set_seed (LrgParticleEffect *self,
                                   guint64            seed);

/**
 * lrg_particle_effect_set_position:
 * @self: an effect
 * @x: X
 * @y: Y
 * @z: Z
 *
 * Moves every system (emission origin; live particles too in local
 * space).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_set_position (LrgParticleEffect *self,
                                       gfloat             x,
                                       gfloat             y,
                                       gfloat             z);

/**
 * lrg_particle_effect_get_position:
 * @self: an effect
 * @x: (out) (optional): X
 * @y: (out) (optional): Y
 * @z: (out) (optional): Z
 *
 * Reads the effect position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_get_position (LrgParticleEffect *self,
                                       gfloat            *x,
                                       gfloat            *y,
                                       gfloat            *z);

/**
 * lrg_particle_effect_set_rotation:
 * @self: an effect
 * @rotation: (array fixed-size=4): a quaternion (x, y, z, w)
 *
 * Orients every system's shapes and directions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_set_rotation (LrgParticleEffect *self,
                                       const gfloat      *rotation);

/**
 * lrg_particle_effect_set_asset_manager:
 * @self: an effect
 * @manager: (nullable): resolves texture and mesh references
 *
 * Without a manager, textured renderers draw the built-in soft dot and
 * mesh renderers their primitive.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_set_asset_manager (LrgParticleEffect *self,
                                            LrgAssetManager   *manager);

/**
 * lrg_particle_effect_set_raycast_func:
 * @self: an effect
 * @func: (nullable) (scope notified) (closure user_data): world raycast
 *   for collision modules in world mode
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Sets the raycast function of every system.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_set_raycast_func (LrgParticleEffect     *self,
                                           LrgParticleRaycastFunc func,
                                           gpointer               user_data,
                                           GDestroyNotify         destroy);

/**
 * lrg_particle_effect_reload:
 * @self: an effect
 *
 * Re-reads the asset (the editor's live preview after an edit).  When
 * the emitters are the same (names, order, capacities), each system gets
 * fresh copies of its modules and renderer settings and keeps its live
 * particles, clock and trails; otherwise the systems are rebuilt and the
 * effect restarts if it was playing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_reload (LrgParticleEffect *self);

/**
 * lrg_particle_effect_get_auto_reload:
 * @self: an effect
 *
 * Returns: whether asset edits reload the effect by themselves
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_effect_get_auto_reload (LrgParticleEffect *self);

/**
 * lrg_particle_effect_set_auto_reload:
 * @self: an effect
 * @auto_reload: %TRUE to call lrg_particle_effect_reload() on every
 *   #LrgParticleAsset::changed
 *
 * Sets #LrgParticleEffect:auto-reload (the editor preview sets it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_set_auto_reload (LrgParticleEffect *self,
                                          gboolean           auto_reload);

/**
 * lrg_particle_effect_get_active_count:
 * @self: an effect
 *
 * Returns: live particles over all systems
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_effect_get_active_count (LrgParticleEffect *self);

/**
 * lrg_particle_effect_get_bounds:
 * @self: an effect
 * @out_min: (out caller-allocates) (array fixed-size=3): smallest corner
 * @out_max: (out caller-allocates) (array fixed-size=3): largest corner
 *
 * The box around every live particle of every system.
 *
 * Returns: %FALSE when nothing is alive (the box is the position)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_effect_get_bounds (LrgParticleEffect *self,
                                         gfloat            *out_min,
                                         gfloat            *out_max);

/**
 * lrg_particle_effect_draw:
 * @self: an effect
 * @eye: (array fixed-size=3): the camera position
 * @up: (array fixed-size=3): the camera's up direction
 *
 * Draws every enabled system with its renderer, in emitter order.  Call
 * it inside a 3D mode after the opaque geometry.  Textures and meshes
 * are resolved through the asset manager on the first draw.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_draw (LrgParticleEffect *self,
                               const gfloat      *eye,
                               const gfloat      *up);

/**
 * lrg_particle_effect_draw_emitter:
 * @self: an effect
 * @index: an emitter index
 * @eye: (array fixed-size=3): the camera position
 * @up: (array fixed-size=3): the camera's up direction
 *
 * Draws one emitter's system (nothing when it is disabled or has no
 * renderer).  The particle component draws each emitter as its own
 * transparent render item so emitters sort against each other and the
 * scene.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_draw_emitter (LrgParticleEffect *self,
                                       guint              index,
                                       const gfloat      *eye,
                                       const gfloat      *up);

/**
 * lrg_particle_effect_get_emitter_bounds:
 * @self: an effect
 * @index: an emitter index
 * @out_min: (out caller-allocates) (array fixed-size=3): smallest corner
 * @out_max: (out caller-allocates) (array fixed-size=3): largest corner
 *
 * The box around one emitter's live particles, grown by its ribbon
 * width (trail points included) when it draws ribbons.
 *
 * Returns: %FALSE when the emitter has nothing to draw
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_effect_get_emitter_bounds (LrgParticleEffect *self,
                                                 guint              index,
                                                 gfloat            *out_min,
                                                 gfloat            *out_max);

/**
 * lrg_particle_effect_draw_camera:
 * @self: an effect
 * @camera: the camera of the current 3D mode
 *
 * lrg_particle_effect_draw() with @camera's position and up vector.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_draw_camera (LrgParticleEffect *self,
                                      GrlCamera3D       *camera);

/**
 * lrg_particle_effect_release_gpu:
 * @self: an effect
 *
 * Releases the renderers' GPU objects and the resolved textures while
 * the context exists.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_effect_release_gpu (LrgParticleEffect *self);

G_END_DECLS
