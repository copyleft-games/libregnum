/* lrg-camera.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract camera base class.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA (lrg_camera_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCamera, lrg_camera, LRG, CAMERA, GObject)

/**
 * LrgCameraClass:
 * @parent_class: Parent class
 * @begin: Begin rendering with this camera's transform
 * @end: End rendering with this camera's transform
 * @world_to_screen: Convert world coordinates to screen coordinates
 * @screen_to_world: Convert screen coordinates to world coordinates
 *
 * Abstract class for camera implementations.
 *
 * Subclasses must implement all virtual methods. Two implementations
 * are provided: #LrgCamera2D for 2D games and #LrgCamera3D for 3D games.
 */
struct _LrgCameraClass
{
	GObjectClass parent_class;

	/*< public >*/

	/**
	 * LrgCameraClass::begin:
	 * @self: an #LrgCamera
	 *
	 * Begin rendering with this camera's transform.
	 * All drawing calls after this will be transformed.
	 */
	void (*begin) (LrgCamera *self);

	/**
	 * LrgCameraClass::end:
	 * @self: an #LrgCamera
	 *
	 * End rendering with this camera's transform.
	 * Returns to screen-space rendering.
	 */
	void (*end) (LrgCamera *self);

	/**
	 * LrgCameraClass::world_to_screen:
	 * @self: an #LrgCamera
	 * @world: world position
	 * @out_screen: (out): screen position output
	 *
	 * Convert world coordinates to screen coordinates.
	 */
	void (*world_to_screen) (LrgCamera        *self,
	                         const GrlVector3 *world,
	                         GrlVector2       *out_screen);

	/**
	 * LrgCameraClass::screen_to_world:
	 * @self: an #LrgCamera
	 * @screen: screen position
	 * @out_world: (out): world position output
	 *
	 * Convert screen coordinates to world coordinates.
	 */
	void (*screen_to_world) (LrgCamera        *self,
	                         const GrlVector2 *screen,
	                         GrlVector3       *out_world);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_camera_begin:
 * @self: an #LrgCamera
 *
 * Begin rendering with this camera's transform.
 *
 * All drawing calls after this will use the camera's
 * view and projection matrices.  The camera's #LrgCamera:cull-mask
 * becomes the active render-layer mask
 * (lrg_render_layers_get_active_mask()) until lrg_camera_end().
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_begin (LrgCamera *self);

/**
 * lrg_camera_end:
 * @self: an #LrgCamera
 *
 * End rendering with this camera's transform.
 *
 * Returns to screen-space (2D) rendering and restores the render-layer
 * mask that was active before lrg_camera_begin().
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_end (LrgCamera *self);

/**
 * lrg_camera_world_to_screen:
 * @self: an #LrgCamera
 * @world: world position (uses x, y for 2D cameras)
 * @out_screen: (out): screen position output
 *
 * Convert world coordinates to screen coordinates.
 *
 * For 2D cameras, only the x and y components of @world are used.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_world_to_screen (LrgCamera        *self,
                                 const GrlVector3 *world,
                                 GrlVector2       *out_screen);

/**
 * lrg_camera_screen_to_world:
 * @self: an #LrgCamera
 * @screen: screen position
 * @out_world: (out): world position output
 *
 * Convert screen coordinates to world coordinates.
 *
 * For 2D cameras, the z component of @out_world will be 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_screen_to_world (LrgCamera        *self,
                                 const GrlVector2 *screen,
                                 GrlVector3       *out_world);

/* ==========================================================================
 * Render layers
 * ========================================================================== */

/**
 * LRG_RENDER_LAYERS_COUNT:
 *
 * Number of render layers.  Layers are indices 0 .. 31 so a #guint32
 * mask selects any set of them (bit N selects layer N).
 */
#define LRG_RENDER_LAYERS_COUNT (32)

/**
 * LRG_RENDER_LAYERS_MASK_ALL:
 *
 * A cull mask selecting every render layer (the default of every
 * camera, of the active mask and of the shadow caster mask).
 */
#define LRG_RENDER_LAYERS_MASK_ALL (0xFFFFFFFFu)

/**
 * LRG_RENDER_LAYERS_MASK_NONE:
 *
 * A cull mask selecting no render layer.
 */
#define LRG_RENDER_LAYERS_MASK_NONE (0x00000000u)

/**
 * LRG_RENDER_LAYERS_BIT:
 * @layer: a layer index 0 .. 31
 *
 * The mask bit of @layer.
 */
#define LRG_RENDER_LAYERS_BIT(layer) (((guint32)1u) << ((guint)(layer) & 31u))

/**
 * LRG_RENDER_LAYERS_DEFAULT:
 *
 * Layer 0, "default": where every drawable starts.
 */
#define LRG_RENDER_LAYERS_DEFAULT (0)

/**
 * LRG_RENDER_LAYERS_UI:
 *
 * Layer 1, "ui": world-space UI (name plates, 3D menus).
 */
#define LRG_RENDER_LAYERS_UI (1)

/**
 * LRG_RENDER_LAYERS_EDITOR_ONLY:
 *
 * Layer 2, "editor-only": gizmos, helpers and markers that a game
 * camera should leave out (clear this bit in its cull mask).
 */
#define LRG_RENDER_LAYERS_EDITOR_ONLY (2)

/**
 * LRG_RENDER_LAYERS_IGNORE_RAYCAST:
 *
 * Layer 3, "ignore-raycast": drawn normally, skipped by picking and
 * gameplay raycasts that honour layers.
 */
#define LRG_RENDER_LAYERS_IGNORE_RAYCAST (3)

/**
 * LRG_RENDER_LAYERS_TRANSPARENT_FX:
 *
 * Layer 4, "transparent-fx": particles and other blended effects.
 */
#define LRG_RENDER_LAYERS_TRANSPARENT_FX (4)

/**
 * LRG_RENDER_LAYERS_WATER:
 *
 * Layer 5, "water": water surfaces (left out of reflection cameras).
 */
#define LRG_RENDER_LAYERS_WATER (5)

/**
 * LRG_RENDER_LAYERS_SKY:
 *
 * Layer 6, "sky": sky domes and far backdrops.
 */
#define LRG_RENDER_LAYERS_SKY (6)

/**
 * lrg_render_layers_set_name:
 * @layer: a layer index 0 .. 31
 * @name: (nullable): the new name, or %NULL to restore the built-in
 *   name (layers 7 .. 31 have none)
 *
 * Names a render layer in the process-wide registry, so projects,
 * levels and the editor can refer to layers by name.  Names are
 * interned: a returned name stays valid for the life of the process.
 * Giving a name already used by another layer is allowed;
 * lrg_render_layers_lookup() then returns the lowest index.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_layers_set_name (guint        layer,
                                 const gchar *name);

/**
 * lrg_render_layers_get_name:
 * @layer: a layer index 0 .. 31
 *
 * Returns: (nullable) (transfer none): the layer's name (interned), or
 *   %NULL for an unnamed layer or an index out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_render_layers_get_name (guint layer);

/**
 * lrg_render_layers_lookup:
 * @name: a layer name
 *
 * Returns: the lowest layer index called @name, or -1 when no layer is
 */
LRG_AVAILABLE_IN_ALL
gint lrg_render_layers_lookup (const gchar *name);

/**
 * lrg_render_layers_mask_from_names:
 * @names: (array zero-terminated=1): layer names
 * @out_mask: (out): the mask selecting every named layer
 *
 * Builds a cull mask from names (e.g. a level's YAML list).
 *
 * Returns: %TRUE when every name was known; unknown names are left out
 *   of @out_mask
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_render_layers_mask_from_names (const gchar * const *names,
                                            guint32             *out_mask);

/**
 * lrg_render_layers_reset_names:
 *
 * Restores every built-in name and forgets every custom one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_layers_reset_names (void);

/**
 * lrg_render_layers_get_active_mask:
 *
 * The cull mask every layer-aware draw path consults: a drawable whose
 * layer bit is clear in it draws nothing.  lrg_camera_begin() sets it
 * to the camera's #LrgCamera:cull-mask (lrg_camera_end() restores it)
 * and lrg_shadow_renderer_begin_pass() narrows it to the caster mask.
 * Outside those it is %LRG_RENDER_LAYERS_MASK_ALL.
 *
 * Returns: the active cull mask
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_render_layers_get_active_mask (void);

/**
 * lrg_render_layers_set_active_mask:
 * @mask: the new active cull mask
 *
 * Sets the active cull mask directly, for render code that opens its
 * own 3D mode (raw BeginMode3D(), an editor viewport) instead of going
 * through lrg_camera_begin().  Restore the returned value afterwards.
 * Rendering is single-threaded: this is plain process state.
 *
 * Returns: the previous active mask
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_render_layers_set_active_mask (guint32 mask);

/**
 * lrg_render_layers_is_visible:
 * @layer: a layer index 0 .. 31
 *
 * Returns: %TRUE when @layer's bit is set in the active cull mask
 *   (%FALSE for an index out of range)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_render_layers_is_visible (guint layer);

/**
 * lrg_camera_set_cull_mask:
 * @self: an #LrgCamera
 * @mask: bit N selects render layer N
 *
 * Sets #LrgCamera:cull-mask: the layers this camera draws.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_set_cull_mask (LrgCamera *self,
                               guint32    mask);

/**
 * lrg_camera_get_cull_mask:
 * @self: an #LrgCamera
 *
 * Returns: the camera's cull mask (%LRG_RENDER_LAYERS_MASK_ALL by default)
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_camera_get_cull_mask (LrgCamera *self);

/**
 * lrg_camera_set_layer_visible:
 * @self: an #LrgCamera
 * @layer: a layer index 0 .. 31
 * @visible: whether the camera draws @layer
 *
 * Sets or clears one bit of #LrgCamera:cull-mask (per-viewport layer
 * toggles in the editor).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_set_layer_visible (LrgCamera *self,
                                   guint      layer,
                                   gboolean   visible);

/**
 * lrg_camera_get_layer_visible:
 * @self: an #LrgCamera
 * @layer: a layer index 0 .. 31
 *
 * Returns: whether the camera draws @layer (%FALSE out of range)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_get_layer_visible (LrgCamera *self,
                                       guint      layer);

G_END_DECLS
