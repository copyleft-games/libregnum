/* lrg-billboard-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBillboardComponent: submits one camera-facing (or fixed) textured
 * quad per frame.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-billboard-component.h"
#include "../lrg-entity3d.h"
#include "../../core/lrg-asset-manager.h"

GType
lrg_billboard_facing_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BILLBOARD_FACING_SPHERICAL, "LRG_BILLBOARD_FACING_SPHERICAL", "spherical" },
			{ LRG_BILLBOARD_FACING_AXIS_Y, "LRG_BILLBOARD_FACING_AXIS_Y", "axis-y" },
			{ LRG_BILLBOARD_FACING_FIXED, "LRG_BILLBOARD_FACING_FIXED", "fixed" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgBillboardFacing"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

struct _LrgBillboardComponent
{
	LrgComponent        parent_instance;

	GrlTexture         *texture;           /* explicit or resolved; owned ref */
	gboolean            texture_explicit;
	gboolean            load_failed;
	gchar              *texture_path;
	LrgAssetManager    *asset_manager;
	gfloat              width;
	gfloat              height;
	gfloat              pixels_per_unit;
	gboolean            preserve_aspect;
	LrgBillboardFacing  facing;
	gfloat              color[4];
	gboolean            cutout;
	gboolean            cast_shadows;
	gint                layer;
};

G_DEFINE_FINAL_TYPE (LrgBillboardComponent, lrg_billboard_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_TEXTURE,
	PROP_TEXTURE_PATH,
	PROP_ASSET_MANAGER,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_PIXELS_PER_UNIT,
	PROP_PRESERVE_ASPECT,
	PROP_FACING,
	PROP_COLOR_R,
	PROP_COLOR_G,
	PROP_COLOR_B,
	PROP_COLOR_A,
	PROP_CUTOUT,
	PROP_CAST_SHADOWS,
	PROP_LAYER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Loads the texture path through the asset manager once. */
static void
resolve_texture (LrgBillboardComponent *self)
{
	g_autoptr(GError) error = NULL;
	GrlTexture       *texture;

	if (self->texture != NULL || self->texture_path == NULL || self->asset_manager == NULL ||
	    self->load_failed)
		return;
	texture = lrg_asset_manager_load_texture (self->asset_manager, self->texture_path, &error);
	if (texture == NULL)
	{
		g_debug ("LrgBillboardComponent: cannot load '%s': %s", self->texture_path,
		         error != NULL ? error->message : "unknown error");
		self->load_failed = TRUE;
		return;
	}
	self->texture = (GrlTexture *) g_object_ref (texture);
}

/* Drops a resolved (not explicit) texture so the path resolves again. */
static void
drop_resolved_texture (LrgBillboardComponent *self)
{
	if (!self->texture_explicit)
		g_clear_object (&self->texture);
	self->load_failed = FALSE;
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static gboolean
lrg_billboard_component_get_bounds (LrgComponent     *component,
                                    LrgBoundingBox3D *out_local)
{
	LrgBillboardComponent *self = LRG_BILLBOARD_COMPONENT (component);
	gfloat                 w, h, r;

	lrg_billboard_component_get_effective_size (self, &w, &h);
	if (self->facing == LRG_BILLBOARD_FACING_FIXED)
	{
		/* The quad in the local XY plane */
		out_local->min.x = -w * 0.5f;
		out_local->min.y = -h * 0.5f;
		out_local->min.z = 0.0f;
		out_local->max.x = w * 0.5f;
		out_local->max.y = h * 0.5f;
		out_local->max.z = 0.0f;
		return TRUE;
	}

	/* A turning quad sweeps a box of its half diagonal */
	r = 0.5f * sqrtf (w * w + h * h);
	out_local->min.x = out_local->min.y = out_local->min.z = -r;
	out_local->max.x = out_local->max.y = out_local->max.z = r;
	return TRUE;
}

static void
lrg_billboard_component_collect_render (LrgComponent  *component,
                                        LrgRenderList *list)
{
	LrgBillboardComponent *self = LRG_BILLBOARD_COMPONENT (component);
	LrgEntity3D           *entity = lrg_component_get_entity (component);
	LrgRenderItem          item;
	LrgBoundingBox3D       local;
	gfloat                 w, h;
	guint                  flags;

	if (entity == NULL)
		return;

	resolve_texture (self);
	lrg_billboard_component_get_effective_size (self, &w, &h);
	lrg_render_item_init_for_entity (&item, entity, component);
	memset (item.params, 0, sizeof item.params);
	item.texture = self->texture;
	memcpy (item.color, self->color, sizeof item.color);

	if (self->facing == LRG_BILLBOARD_FACING_FIXED)
	{
		/*
		 * A plane (XZ, normal +Y) turned +90 degrees about local X so it
		 * lies in XY facing +Z, as the editor draws SPRITE nodes:
		 * world' = world * Rx(90).  Column-major: the new Y column is the
		 * old Z column and the new Z column is minus the old Y column.
		 */
		gfloat col_y[3];

		memcpy (col_y, &item.world_matrix[4], sizeof col_y);
		memcpy (&item.world_matrix[4], &item.world_matrix[8], sizeof col_y);
		item.world_matrix[8] = -col_y[0];
		item.world_matrix[9] = -col_y[1];
		item.world_matrix[10] = -col_y[2];

		item.kind = LRG_RENDER_ITEM_PRIMITIVE;
		item.primitive = LRG_PRIMITIVE_PLANE;
		item.params[0] = w;
		item.params[1] = h;
	}
	else
	{
		item.kind = LRG_RENDER_ITEM_BILLBOARD;
		item.params[0] = w;
		item.params[1] = h;
		item.params[2] = (gfloat) ((self->facing == LRG_BILLBOARD_FACING_AXIS_Y)
		                           ? LRG_BILLBOARD_MODE_AXIS_Y : LRG_BILLBOARD_MODE_SPHERICAL);
	}

	/* Sprites are alpha tested (or blended when translucent), two sided */
	flags = LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED | LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
	if (self->color[3] < 1.0f)
		flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
	else if (self->cutout)
		flags |= LRG_RENDER_ITEM_FLAG_CUTOUT;
	if (self->cast_shadows)
		flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
	item.flags = (LrgRenderItemFlags) flags;
	if (self->layer >= 0)
		item.layer = (guint) self->layer;
	item.sort_key = ((guint64) item.kind << 56) |
	                ((guint64) (guintptr) self->texture & G_GUINT64_CONSTANT (0x00ffffffffffffff));

	/* Bounds from the entity matrix (the fixed quad's rotation is inside) */
	lrg_billboard_component_get_bounds (component, &local);
	if (self->facing == LRG_BILLBOARD_FACING_FIXED)
	{
		LrgRenderItem probe = item;

		memcpy (probe.world_matrix, lrg_entity3d_peek_world_matrix (entity), sizeof probe.world_matrix);
		lrg_render_item_set_local_bounds (&probe, &local);
		item.bounds = probe.bounds;
	}
	else
		lrg_render_item_set_local_bounds (&item, &local);

	if (lrg_render_list_accepts (list, &item))
		lrg_render_list_add (list, &item);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_billboard_component_dispose (GObject *object)
{
	LrgBillboardComponent *self = LRG_BILLBOARD_COMPONENT (object);

	g_clear_object (&self->texture);
	g_clear_object (&self->asset_manager);

	G_OBJECT_CLASS (lrg_billboard_component_parent_class)->dispose (object);
}

static void
lrg_billboard_component_finalize (GObject *object)
{
	LrgBillboardComponent *self = LRG_BILLBOARD_COMPONENT (object);

	g_free (self->texture_path);

	G_OBJECT_CLASS (lrg_billboard_component_parent_class)->finalize (object);
}

/* Stores a float and notifies when it changed. */
static void
set_float (LrgBillboardComponent *self,
           gfloat                *slot,
           gfloat                 value,
           guint                  prop)
{
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* Stores a boolean and notifies when it changed. */
static void
set_bool (LrgBillboardComponent *self,
          gboolean              *slot,
          gboolean               value,
          guint                  prop)
{
	value = !!value;
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_billboard_component_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
	LrgBillboardComponent *self = LRG_BILLBOARD_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_TEXTURE:         g_value_set_object (value, self->texture); break;
	case PROP_TEXTURE_PATH:    g_value_set_string (value, self->texture_path); break;
	case PROP_ASSET_MANAGER:   g_value_set_object (value, self->asset_manager); break;
	case PROP_WIDTH:           g_value_set_float (value, self->width); break;
	case PROP_HEIGHT:          g_value_set_float (value, self->height); break;
	case PROP_PIXELS_PER_UNIT: g_value_set_float (value, self->pixels_per_unit); break;
	case PROP_PRESERVE_ASPECT: g_value_set_boolean (value, self->preserve_aspect); break;
	case PROP_FACING:          g_value_set_enum (value, (gint) self->facing); break;
	case PROP_COLOR_R:         g_value_set_float (value, self->color[0]); break;
	case PROP_COLOR_G:         g_value_set_float (value, self->color[1]); break;
	case PROP_COLOR_B:         g_value_set_float (value, self->color[2]); break;
	case PROP_COLOR_A:         g_value_set_float (value, self->color[3]); break;
	case PROP_CUTOUT:          g_value_set_boolean (value, self->cutout); break;
	case PROP_CAST_SHADOWS:    g_value_set_boolean (value, self->cast_shadows); break;
	case PROP_LAYER:           g_value_set_int (value, self->layer); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_billboard_component_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
	LrgBillboardComponent *self = LRG_BILLBOARD_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_TEXTURE:
		lrg_billboard_component_set_texture (self, g_value_get_object (value));
		break;
	case PROP_TEXTURE_PATH:
		lrg_billboard_component_set_texture_path (self, g_value_get_string (value));
		break;
	case PROP_ASSET_MANAGER:
		lrg_billboard_component_set_asset_manager (self, g_value_get_object (value));
		break;
	case PROP_WIDTH:  set_float (self, &self->width, g_value_get_float (value), prop_id); break;
	case PROP_HEIGHT: set_float (self, &self->height, g_value_get_float (value), prop_id); break;
	case PROP_PIXELS_PER_UNIT:
		set_float (self, &self->pixels_per_unit, g_value_get_float (value), prop_id);
		break;
	case PROP_PRESERVE_ASPECT:
		set_bool (self, &self->preserve_aspect, g_value_get_boolean (value), prop_id);
		break;
	case PROP_FACING:
		lrg_billboard_component_set_facing (self, (LrgBillboardFacing) g_value_get_enum (value));
		break;
	case PROP_COLOR_R: set_float (self, &self->color[0], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_G: set_float (self, &self->color[1], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_B: set_float (self, &self->color[2], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_A: set_float (self, &self->color[3], g_value_get_float (value), prop_id); break;
	case PROP_CUTOUT:
		set_bool (self, &self->cutout, g_value_get_boolean (value), prop_id);
		break;
	case PROP_CAST_SHADOWS:
		set_bool (self, &self->cast_shadows, g_value_get_boolean (value), prop_id);
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_int (value))
		{
			self->layer = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_billboard_component_class_init (LrgBillboardComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_billboard_component_dispose;
	object_class->finalize = lrg_billboard_component_finalize;
	object_class->get_property = lrg_billboard_component_get_property;
	object_class->set_property = lrg_billboard_component_set_property;

	component_class->collect_render = lrg_billboard_component_collect_render;
	component_class->get_bounds = lrg_billboard_component_get_bounds;

	/**
	 * LrgBillboardComponent:texture:
	 *
	 * The #GrlTexture shown: explicit or resolved from the texture path.
	 */
	properties[PROP_TEXTURE] =
		g_param_spec_object ("texture", NULL, "Texture", GRL_TYPE_TEXTURE, rw);

	/**
	 * LrgBillboardComponent:texture-path:
	 *
	 * An image path or asset GUID (the editor SPRITE visual's asset).
	 */
	properties[PROP_TEXTURE_PATH] =
		g_param_spec_string ("texture-path", NULL, "Image path or GUID", NULL, rw);

	/**
	 * LrgBillboardComponent:asset-manager:
	 *
	 * The #LrgAssetManager the texture path resolves through (optional).
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", NULL, "Asset manager", LRG_TYPE_ASSET_MANAGER, rw);

	/**
	 * LrgBillboardComponent:width:
	 *
	 * Quad width in world units (without pixels-per-unit sizing).
	 */
	properties[PROP_WIDTH] = g_param_spec_float ("width", NULL, "Width", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgBillboardComponent:height:
	 *
	 * Quad height in world units (without pixels-per-unit sizing).
	 */
	properties[PROP_HEIGHT] = g_param_spec_float ("height", NULL, "Height", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgBillboardComponent:pixels-per-unit:
	 *
	 * Texture pixels per world unit; when positive and a texture is known
	 * the quad takes the texture's size.  0 uses width and height.
	 */
	properties[PROP_PIXELS_PER_UNIT] =
		g_param_spec_float ("pixels-per-unit", NULL, "Pixels per unit (0 off)", 0.0f, G_MAXFLOAT,
		                    0.0f, rw);

	/**
	 * LrgBillboardComponent:preserve-aspect:
	 *
	 * Without pixels-per-unit sizing, fits the texture's aspect ratio
	 * inside width x height (the editor draws SPRITE nodes this way in a
	 * 1 x 1 quad) instead of stretching it.
	 */
	properties[PROP_PRESERVE_ASPECT] =
		g_param_spec_boolean ("preserve-aspect", NULL, "Fit the texture aspect", FALSE, rw);

	/**
	 * LrgBillboardComponent:facing:
	 *
	 * How the quad is oriented.
	 */
	properties[PROP_FACING] =
		g_param_spec_enum ("facing", NULL, "Facing mode", LRG_TYPE_BILLBOARD_FACING,
		                   LRG_BILLBOARD_FACING_SPHERICAL, rw);

	/**
	 * LrgBillboardComponent:color-r:
	 *
	 * Tint red, 0 .. 1.
	 */
	properties[PROP_COLOR_R] = g_param_spec_float ("color-r", NULL, "Tint red", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgBillboardComponent:color-g:
	 *
	 * Tint green, 0 .. 1.
	 */
	properties[PROP_COLOR_G] = g_param_spec_float ("color-g", NULL, "Tint green", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgBillboardComponent:color-b:
	 *
	 * Tint blue, 0 .. 1.
	 */
	properties[PROP_COLOR_B] = g_param_spec_float ("color-b", NULL, "Tint blue", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgBillboardComponent:color-a:
	 *
	 * Tint alpha, 0 .. 1; below 1 the quad is blended.
	 */
	properties[PROP_COLOR_A] = g_param_spec_float ("color-a", NULL, "Tint alpha", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgBillboardComponent:cutout:
	 *
	 * Alpha-tests the texture (the default for sprites).
	 */
	properties[PROP_CUTOUT] = g_param_spec_boolean ("cutout", NULL, "Alpha tested", TRUE, rw);

	/**
	 * LrgBillboardComponent:cast-shadows:
	 *
	 * Whether the quad is a shadow caster.
	 */
	properties[PROP_CAST_SHADOWS] = g_param_spec_boolean ("cast-shadows", NULL, "Cast shadows",
	                                                      FALSE, rw);

	/**
	 * LrgBillboardComponent:layer:
	 *
	 * Render layer override 0 .. 31, or -1 to use the entity's layer.
	 */
	properties[PROP_LAYER] = g_param_spec_int ("layer", NULL, "Render layer (-1 entity)", -1, 31,
	                                           -1, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_billboard_component_init (LrgBillboardComponent *self)
{
	self->width = 1.0f;
	self->height = 1.0f;
	self->facing = LRG_BILLBOARD_FACING_SPHERICAL;
	self->color[0] = self->color[1] = self->color[2] = self->color[3] = 1.0f;
	self->cutout = TRUE;
	self->layer = -1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgBillboardComponent *
lrg_billboard_component_new (void)
{
	return (LrgBillboardComponent *) g_object_new (LRG_TYPE_BILLBOARD_COMPONENT, NULL);
}

LrgBillboardComponent *
lrg_billboard_component_new_with_texture_path (const gchar *path)
{
	return (LrgBillboardComponent *) g_object_new (LRG_TYPE_BILLBOARD_COMPONENT,
	                                               "texture-path", path, NULL);
}

GrlTexture *
lrg_billboard_component_get_texture (LrgBillboardComponent *self)
{
	g_return_val_if_fail (LRG_IS_BILLBOARD_COMPONENT (self), NULL);

	return self->texture;
}

void
lrg_billboard_component_set_texture (LrgBillboardComponent *self,
                                     GrlTexture            *texture)
{
	g_return_if_fail (LRG_IS_BILLBOARD_COMPONENT (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	if (self->texture == texture && self->texture_explicit == (texture != NULL))
		return;
	g_set_object (&self->texture, texture);
	self->texture_explicit = (texture != NULL);
	self->load_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
}

const gchar *
lrg_billboard_component_get_texture_path (LrgBillboardComponent *self)
{
	g_return_val_if_fail (LRG_IS_BILLBOARD_COMPONENT (self), NULL);

	return self->texture_path;
}

void
lrg_billboard_component_set_texture_path (LrgBillboardComponent *self,
                                          const gchar           *path)
{
	g_return_if_fail (LRG_IS_BILLBOARD_COMPONENT (self));

	if (path != NULL && *path == '\0')
		path = NULL;
	if (g_strcmp0 (self->texture_path, path) == 0)
		return;
	g_free (self->texture_path);
	self->texture_path = g_strdup (path);
	drop_resolved_texture (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE_PATH]);
}

void
lrg_billboard_component_set_asset_manager (LrgBillboardComponent *self,
                                           LrgAssetManager       *manager)
{
	g_return_if_fail (LRG_IS_BILLBOARD_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (!g_set_object (&self->asset_manager, manager))
		return;
	drop_resolved_texture (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

void
lrg_billboard_component_set_size (LrgBillboardComponent *self,
                                  gfloat                 width,
                                  gfloat                 height)
{
	g_return_if_fail (LRG_IS_BILLBOARD_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->width, MAX (width, 0.0f), PROP_WIDTH);
	set_float (self, &self->height, MAX (height, 0.0f), PROP_HEIGHT);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_billboard_component_get_effective_size (LrgBillboardComponent *self,
                                            gfloat                *out_width,
                                            gfloat                *out_height)
{
	gfloat w, h;

	g_return_if_fail (LRG_IS_BILLBOARD_COMPONENT (self));

	w = self->width;
	h = self->height;
	if (self->texture != NULL)
	{
		gint tw = grl_texture_get_width (self->texture);
		gint th = grl_texture_get_height (self->texture);

		if (tw > 0 && th > 0 && self->pixels_per_unit > 0.0f)
		{
			/* The texture's own size in world units */
			w = (gfloat) tw / self->pixels_per_unit;
			h = (gfloat) th / self->pixels_per_unit;
		}
		else if (tw > 0 && th > 0 && self->preserve_aspect)
		{
			/* Fit the texture's aspect inside width x height */
			gfloat aspect = (gfloat) tw / (gfloat) th;

			if (w / MAX (h, 1e-6f) > aspect)
				w = h * aspect;
			else
				h = w / aspect;
		}
	}
	if (out_width != NULL)
		*out_width = w;
	if (out_height != NULL)
		*out_height = h;
}

LrgBillboardFacing
lrg_billboard_component_get_facing (LrgBillboardComponent *self)
{
	g_return_val_if_fail (LRG_IS_BILLBOARD_COMPONENT (self), LRG_BILLBOARD_FACING_SPHERICAL);

	return self->facing;
}

void
lrg_billboard_component_set_facing (LrgBillboardComponent *self,
                                    LrgBillboardFacing     facing)
{
	g_return_if_fail (LRG_IS_BILLBOARD_COMPONENT (self));

	if (self->facing == facing)
		return;
	self->facing = facing;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FACING]);
}

void
lrg_billboard_component_set_color (LrgBillboardComponent *self,
                                   gfloat                 r,
                                   gfloat                 g,
                                   gfloat                 b,
                                   gfloat                 a)
{
	g_return_if_fail (LRG_IS_BILLBOARD_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->color[0], CLAMP (r, 0.0f, 1.0f), PROP_COLOR_R);
	set_float (self, &self->color[1], CLAMP (g, 0.0f, 1.0f), PROP_COLOR_G);
	set_float (self, &self->color[2], CLAMP (b, 0.0f, 1.0f), PROP_COLOR_B);
	set_float (self, &self->color[3], CLAMP (a, 0.0f, 1.0f), PROP_COLOR_A);
	g_object_thaw_notify (G_OBJECT (self));
}
