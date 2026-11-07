/* lrg-camera.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract camera base class.
 */

#include "config.h"
#include "lrg-camera.h"

/**
 * SECTION:lrg-camera
 * @title: LrgCamera
 * @short_description: Abstract camera base class
 *
 * #LrgCamera is an abstract base class for camera implementations.
 * It defines the interface that all cameras must implement.
 *
 * Two implementations are provided:
 * - #LrgCamera2D for 2D games with offset, target, zoom, and rotation
 * - #LrgCamera3D for 3D games with position, target, up vector, and projection
 *
 * ## Using Cameras
 *
 * Cameras are used with the renderer's begin_layer/end_layer methods,
 * or directly with begin/end:
 *
 * |[<!-- language="C" -->
 * lrg_camera_begin (camera);
 * // All drawing here uses camera transform
 * grl_draw_sphere (position, 1.0f, color);
 * lrg_camera_end (camera);
 * ]|
 *
 * ## Render layers
 *
 * Every camera has a #LrgCamera:cull-mask (all layers by default).
 * lrg_camera_begin() makes it the active mask that layer-aware draw
 * paths consult (lrg_render_layers_get_active_mask()), and
 * lrg_camera_end() restores the previous one.  Layer names live in a
 * process-wide registry (lrg_render_layers_set_name()).
 *
 * ## Custom Cameras
 *
 * You can create custom camera behaviors by subclassing #LrgCamera2D
 * or #LrgCamera3D. For example, a follow camera or orbit camera.
 */

/*
 * LrgCameraPrivate:
 * @cull_mask: the render layers this camera draws (#LrgCamera:cull-mask)
 * @saved_mask: the active mask lrg_camera_begin() replaced, restored by
 *   lrg_camera_end()
 * @begun: whether a begin is waiting for its end (so an unbalanced end
 *   does not restore a stale mask)
 */
typedef struct
{
	guint32  cull_mask;
	guint32  saved_mask;
	gboolean begun;
} LrgCameraPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgCamera, lrg_camera, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_CULL_MASK,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Render layer registry
 *
 * Names are interned strings (never freed), so get_name() can hand out
 * pointers without any lifetime contract.  The lock only guards the
 * table; the active mask is render-thread state and is not locked.
 * ========================================================================== */

static const gchar * const default_layer_names[LRG_RENDER_LAYERS_COUNT] = {
	"default", "ui", "editor-only", "ignore-raycast",
	"transparent-fx", "water", "sky", NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

G_LOCK_DEFINE_STATIC (layer_names);
static const gchar *layer_names[LRG_RENDER_LAYERS_COUNT];
static gboolean     layer_names_ready = FALSE;
static guint32      active_mask = LRG_RENDER_LAYERS_MASK_ALL;

/* ensure_names_locked: fill the table with the built-in names once
 * (caller holds the lock). */
static void
ensure_names_locked (void)
{
	guint i;

	if (layer_names_ready)
		return;
	for (i = 0; i < LRG_RENDER_LAYERS_COUNT; i++)
		layer_names[i] = default_layer_names[i] != NULL
			? g_intern_static_string (default_layer_names[i]) : NULL;
	layer_names_ready = TRUE;
}

void
lrg_render_layers_set_name (guint        layer,
                            const gchar *name)
{
	g_return_if_fail (layer < LRG_RENDER_LAYERS_COUNT);

	G_LOCK (layer_names);
	ensure_names_locked ();
	if (name == NULL || name[0] == '\0')
		layer_names[layer] = default_layer_names[layer] != NULL
			? g_intern_static_string (default_layer_names[layer]) : NULL;
	else
		layer_names[layer] = g_intern_string (name);
	G_UNLOCK (layer_names);
}

const gchar *
lrg_render_layers_get_name (guint layer)
{
	const gchar *name;

	if (layer >= LRG_RENDER_LAYERS_COUNT)
		return NULL;
	G_LOCK (layer_names);
	ensure_names_locked ();
	name = layer_names[layer];
	G_UNLOCK (layer_names);
	return name;
}

gint
lrg_render_layers_lookup (const gchar *name)
{
	const gchar *interned;
	gint         found = -1;
	guint        i;

	g_return_val_if_fail (name != NULL, -1);

	/* Interned names compare by pointer; a name never interned cannot
	 * be in the table. */
	interned = g_intern_string (name);
	G_LOCK (layer_names);
	ensure_names_locked ();
	for (i = 0; i < LRG_RENDER_LAYERS_COUNT && found < 0; i++)
		if (layer_names[i] == interned)
			found = (gint)i;
	G_UNLOCK (layer_names);
	return found;
}

gboolean
lrg_render_layers_mask_from_names (const gchar * const *names,
                                   guint32             *out_mask)
{
	gboolean all_known = TRUE;
	guint32  mask = 0;
	guint    i;

	g_return_val_if_fail (out_mask != NULL, FALSE);

	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		gint layer = lrg_render_layers_lookup (names[i]);

		if (layer < 0)
			all_known = FALSE;
		else
			mask |= LRG_RENDER_LAYERS_BIT (layer);
	}
	*out_mask = mask;
	return all_known;
}

void
lrg_render_layers_reset_names (void)
{
	G_LOCK (layer_names);
	layer_names_ready = FALSE;
	ensure_names_locked ();
	G_UNLOCK (layer_names);
}

guint32
lrg_render_layers_get_active_mask (void)
{
	return active_mask;
}

guint32
lrg_render_layers_set_active_mask (guint32 mask)
{
	guint32 previous = active_mask;

	active_mask = mask;
	return previous;
}

gboolean
lrg_render_layers_is_visible (guint layer)
{
	if (layer >= LRG_RENDER_LAYERS_COUNT)
		return FALSE;
	return (active_mask & LRG_RENDER_LAYERS_BIT (layer)) != 0;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_camera_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgCamera *self = LRG_CAMERA (object);

	switch (prop_id)
	{
	case PROP_CULL_MASK:
		g_value_set_uint (value, (guint)lrg_camera_get_cull_mask (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_camera_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgCamera *self = LRG_CAMERA (object);

	switch (prop_id)
	{
	case PROP_CULL_MASK:
		lrg_camera_set_cull_mask (self, (guint32)g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_camera_class_init (LrgCameraClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_camera_get_property;
	object_class->set_property = lrg_camera_set_property;

	/**
	 * LrgCamera:cull-mask:
	 *
	 * The render layers this camera draws: bit N selects layer N (see
	 * lrg_render_layers_get_name()).  lrg_camera_begin() makes it the
	 * active mask, so every layer-aware drawable (static batch chunks,
	 * billboard batches, terrain chunks, particle systems) drawn
	 * between begin and end skips layers outside it.  Defaults to
	 * every layer.
	 */
	properties[PROP_CULL_MASK] =
		g_param_spec_uint ("cull-mask", "Cull mask",
		                   "Render layers this camera draws",
		                   0, G_MAXUINT32, LRG_RENDER_LAYERS_MASK_ALL,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_camera_init (LrgCamera *self)
{
	LrgCameraPrivate *priv = lrg_camera_get_instance_private (self);

	priv->cull_mask = LRG_RENDER_LAYERS_MASK_ALL;
	priv->saved_mask = LRG_RENDER_LAYERS_MASK_ALL;
	priv->begun = FALSE;
}

void
lrg_camera_set_cull_mask (LrgCamera *self,
                          guint32    mask)
{
	LrgCameraPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA (self));

	priv = lrg_camera_get_instance_private (self);
	if (priv->cull_mask == mask)
		return;
	priv->cull_mask = mask;
	/* A change while the camera is begun applies at once. */
	if (priv->begun)
		active_mask = mask;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CULL_MASK]);
}

guint32
lrg_camera_get_cull_mask (LrgCamera *self)
{
	LrgCameraPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA (self), LRG_RENDER_LAYERS_MASK_ALL);

	priv = lrg_camera_get_instance_private (self);
	return priv->cull_mask;
}

void
lrg_camera_set_layer_visible (LrgCamera *self,
                              guint      layer,
                              gboolean   visible)
{
	guint32 mask;

	g_return_if_fail (LRG_IS_CAMERA (self));
	g_return_if_fail (layer < LRG_RENDER_LAYERS_COUNT);

	mask = lrg_camera_get_cull_mask (self);
	if (visible)
		mask |= LRG_RENDER_LAYERS_BIT (layer);
	else
		mask &= ~LRG_RENDER_LAYERS_BIT (layer);
	lrg_camera_set_cull_mask (self, mask);
}

gboolean
lrg_camera_get_layer_visible (LrgCamera *self,
                              guint      layer)
{
	g_return_val_if_fail (LRG_IS_CAMERA (self), FALSE);

	if (layer >= LRG_RENDER_LAYERS_COUNT)
		return FALSE;
	return (lrg_camera_get_cull_mask (self) & LRG_RENDER_LAYERS_BIT (layer)) != 0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
 * lrg_camera_begin:
 * @self: an #LrgCamera
 *
 * Begin rendering with this camera's transform.
 *
 * All drawing calls after this will use the camera's
 * view and projection matrices.
 */
void
lrg_camera_begin (LrgCamera *self)
{
	LrgCameraClass   *klass;
	LrgCameraPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA (self));

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->begin != NULL);

	/* The camera's cull mask becomes the active mask until end. */
	priv = lrg_camera_get_instance_private (self);
	if (!priv->begun)
		priv->saved_mask = active_mask;
	priv->begun = TRUE;
	active_mask = priv->cull_mask;

	klass->begin (self);
}

/*
 * lrg_camera_end:
 * @self: an #LrgCamera
 *
 * End rendering with this camera's transform.
 *
 * Returns to screen-space (2D) rendering.
 */
void
lrg_camera_end (LrgCamera *self)
{
	LrgCameraClass   *klass;
	LrgCameraPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA (self));

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->end != NULL);

	klass->end (self);

	/* Restore the mask that was active before begin. */
	priv = lrg_camera_get_instance_private (self);
	if (priv->begun)
		active_mask = priv->saved_mask;
	priv->begun = FALSE;
}

/*
 * lrg_camera_world_to_screen:
 * @self: an #LrgCamera
 * @world: world position (uses x, y for 2D cameras)
 * @out_screen: (out): screen position output
 *
 * Convert world coordinates to screen coordinates.
 *
 * For 2D cameras, only the x and y components of @world are used.
 */
void
lrg_camera_world_to_screen (LrgCamera        *self,
                            const GrlVector3 *world,
                            GrlVector2       *out_screen)
{
	LrgCameraClass *klass;

	g_return_if_fail (LRG_IS_CAMERA (self));
	g_return_if_fail (world != NULL);
	g_return_if_fail (out_screen != NULL);

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->world_to_screen != NULL);

	klass->world_to_screen (self, world, out_screen);
}

/*
 * lrg_camera_screen_to_world:
 * @self: an #LrgCamera
 * @screen: screen position
 * @out_world: (out): world position output
 *
 * Convert screen coordinates to world coordinates.
 *
 * For 2D cameras, the z component of @out_world will be 0.
 */
void
lrg_camera_screen_to_world (LrgCamera        *self,
                            const GrlVector2 *screen,
                            GrlVector3       *out_world)
{
	LrgCameraClass *klass;

	g_return_if_fail (LRG_IS_CAMERA (self));
	g_return_if_fail (screen != NULL);
	g_return_if_fail (out_world != NULL);

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->screen_to_world != NULL);

	klass->screen_to_world (self, screen, out_world);
}
