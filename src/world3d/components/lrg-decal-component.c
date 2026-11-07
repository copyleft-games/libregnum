/* lrg-decal-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecalComponent: owns an LrgDecal, keeps it on the entity (world
 * matrix times the box size) and registers it with the decal set its
 * world carries as object data (lrg_decal_component_get_world_set()).
 * Decals are not render items: the lit shader applies them while
 * drawing the geometry they cover, so collect_render adds nothing; the
 * component reports its box through get_bounds for picking and the
 * octree.
 *
 * The decal's look (colour, fades, order, layers, blend, channels,
 * material values) is mirrored as component properties of the same
 * names: setting one sets the decal's, and the decal's notify is
 * re-emitted on the component, so `.rlevel` visual params map by name.
 */

#include "config.h"

#include <string.h>

#include "lrg-decal-component.h"
#include "../lrg-entity3d.h"
#include "../../core/lrg-asset-manager.h"
#include "../../core/lrg-math3d.h"

/* the quark of the world's decal set */
#define WORLD_SET_KEY "lrg-decal-component-world-set"

struct _LrgDecalComponent
{
	LrgComponent     parent_instance;

	LrgDecal        *decal;
	gfloat           size[3];
	gchar           *paths[3];        /* albedo, normal, ORM */
	gboolean         failed[3];       /* the path did not load */
	LrgAssetManager *asset_manager;
	LrgDecalSet     *registered;      /* the set holding the decal (ref) */
};

G_DEFINE_FINAL_TYPE (LrgDecalComponent, lrg_decal_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_SIZE_X,
	PROP_SIZE_Y,
	PROP_SIZE_Z,
	PROP_ALBEDO_PATH,
	PROP_NORMAL_PATH,
	PROP_ORM_PATH,
	PROP_ASSET_MANAGER,
	/* mirrored from the decal (same names) */
	PROP_COLOR_R,
	PROP_COLOR_G,
	PROP_COLOR_B,
	PROP_COLOR_A,
	PROP_OPACITY,
	PROP_FADE_ANGLE,
	PROP_FADE_WIDTH,
	PROP_DEPTH_FADE,
	PROP_SORT_ORDER,
	PROP_LAYER_MASK,
	PROP_BLEND,
	PROP_CHANNELS,
	PROP_NORMAL_STRENGTH,
	PROP_ROUGHNESS,
	PROP_METALLIC,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* set_slot_texture: the decal texture of slot @slot (0 albedo, 1 normal,
 * 2 ORM) */
static void
set_slot_texture (LrgDecal   *decal,
                  guint       slot,
                  GrlTexture *texture)
{
	if (slot == 0)
		lrg_decal_set_albedo_texture (decal, texture);
	else if (slot == 1)
		lrg_decal_set_normal_texture (decal, texture);
	else
		lrg_decal_set_orm_texture (decal, texture);
}

static GrlTexture *
get_slot_texture (LrgDecal *decal,
                  guint     slot)
{
	if (slot == 0)
		return lrg_decal_get_albedo_texture (decal);
	if (slot == 1)
		return lrg_decal_get_normal_texture (decal);
	return lrg_decal_get_orm_texture (decal);
}

/* resolve_textures:
 * Loads the unresolved paths through the asset manager, or @resolver
 * when there is none.  A failed path is not retried until it changes. */
static void
resolve_textures (LrgDecalComponent   *self,
                  LrgDecalTextureFunc  resolver,
                  gpointer             user_data)
{
	guint slot;

	for (slot = 0; slot < 3; slot++)
	{
		GrlTexture *tex = NULL;

		if (self->paths[slot] == NULL || self->failed[slot]
		    || get_slot_texture (self->decal, slot) != NULL)
			continue;
		if (self->asset_manager != NULL)
		{
			g_autoptr(GError) error = NULL;

			tex = lrg_asset_manager_load_texture (self->asset_manager, self->paths[slot], &error);
			if (tex == NULL)
				g_debug ("LrgDecalComponent: cannot load '%s': %s", self->paths[slot],
				         error != NULL ? error->message : "unknown error");
		}
		else if (resolver != NULL)
		{
			tex = resolver (self->paths[slot], user_data);
		}
		else
		{
			continue;   /* wait for a resolver */
		}
		if (tex == NULL)
		{
			self->failed[slot] = TRUE;
			continue;
		}
		set_slot_texture (self->decal, slot, tex);
	}
}

/* set_path: replaces slot @slot's path; the old texture goes */
static void
set_path (LrgDecalComponent *self,
          guint              slot,
          const gchar       *path,
          guint              prop)
{
	if (g_strcmp0 (self->paths[slot], path) == 0)
		return;
	g_free (self->paths[slot]);
	self->paths[slot] = g_strdup (path);
	self->failed[slot] = FALSE;
	set_slot_texture (self->decal, slot, NULL);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* world_set_free: GDestroyNotify for the world's set */
static void
world_set_free (gpointer data)
{
	g_object_unref (data);
}

/* register_with: moves the decal into @set (NULL: out of every set) */
static void
register_with (LrgDecalComponent *self,
               LrgDecalSet       *set)
{
	if (self->registered == set)
		return;
	if (self->registered != NULL)
	{
		lrg_decal_set_remove (self->registered, self->decal);
		g_clear_object (&self->registered);
	}
	if (set != NULL)
	{
		lrg_decal_set_add (set, self->decal);
		self->registered = (LrgDecalSet *) g_object_ref (set);
	}
}

/* decal_notify: re-emits the decal's property notifications on the
 * component for the mirrored properties */
static void
decal_notify (GObject    *decal,
              GParamSpec *pspec,
              gpointer    user_data)
{
	LrgDecalComponent *self = LRG_DECAL_COMPONENT (user_data);
	GParamSpec        *own;

	(void) decal;
	own = g_object_class_find_property (G_OBJECT_GET_CLASS (self), pspec->name);
	if (own != NULL)
		g_object_notify_by_pspec (G_OBJECT (self), own);
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static void
lrg_decal_component_world_entered (LrgComponent *component,
                                   LrgWorld3D   *world)
{
	LrgDecalComponent *self = LRG_DECAL_COMPONENT (component);

	register_with (self, lrg_decal_component_get_world_set (world));
	lrg_decal_component_sync (self);
}

static void
lrg_decal_component_world_exited (LrgComponent *component,
                                  LrgWorld3D   *world)
{
	(void) world;
	register_with (LRG_DECAL_COMPONENT (component), NULL);
}

static void
lrg_decal_component_late_update (LrgComponent *component,
                                 gfloat        delta)
{
	(void) delta;
	lrg_decal_component_sync (LRG_DECAL_COMPONENT (component));
}

static gboolean
lrg_decal_component_get_bounds (LrgComponent     *component,
                                LrgBoundingBox3D *out_local)
{
	LrgDecalComponent *self = LRG_DECAL_COMPONENT (component);

	out_local->min.x = -self->size[0] * 0.5f;
	out_local->min.y = -self->size[1] * 0.5f;
	out_local->min.z = -self->size[2] * 0.5f;
	out_local->max.x = self->size[0] * 0.5f;
	out_local->max.y = self->size[1] * 0.5f;
	out_local->max.z = self->size[2] * 0.5f;
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_decal_component_dispose (GObject *object)
{
	LrgDecalComponent *self = LRG_DECAL_COMPONENT (object);

	if (self->decal != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->decal, self);
		register_with (self, NULL);
	}
	g_clear_object (&self->decal);
	g_clear_object (&self->asset_manager);

	G_OBJECT_CLASS (lrg_decal_component_parent_class)->dispose (object);
}

static void
lrg_decal_component_finalize (GObject *object)
{
	LrgDecalComponent *self = LRG_DECAL_COMPONENT (object);

	g_free (self->paths[0]);
	g_free (self->paths[1]);
	g_free (self->paths[2]);

	G_OBJECT_CLASS (lrg_decal_component_parent_class)->finalize (object);
}

static void
lrg_decal_component_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgDecalComponent *self = LRG_DECAL_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SIZE_X:
	case PROP_SIZE_Y:
	case PROP_SIZE_Z:
		g_value_set_float (value, self->size[prop_id - PROP_SIZE_X]);
		break;
	case PROP_ALBEDO_PATH:
	case PROP_NORMAL_PATH:
	case PROP_ORM_PATH:
		g_value_set_string (value, self->paths[prop_id - PROP_ALBEDO_PATH]);
		break;
	case PROP_ASSET_MANAGER:
		g_value_set_object (value, self->asset_manager);
		break;
	default:
		if (prop_id >= PROP_COLOR_R && prop_id < N_PROPS)
			g_object_get_property (G_OBJECT (self->decal), pspec->name, value);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_decal_component_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgDecalComponent *self = LRG_DECAL_COMPONENT (object);
	gfloat             size[3];

	switch (prop_id)
	{
	case PROP_SIZE_X:
	case PROP_SIZE_Y:
	case PROP_SIZE_Z:
		memcpy (size, self->size, sizeof size);
		size[prop_id - PROP_SIZE_X] = g_value_get_float (value);
		lrg_decal_component_set_size (self, size[0], size[1], size[2]);
		break;
	case PROP_ALBEDO_PATH:
		lrg_decal_component_set_albedo_path (self, g_value_get_string (value));
		break;
	case PROP_NORMAL_PATH:
		lrg_decal_component_set_normal_path (self, g_value_get_string (value));
		break;
	case PROP_ORM_PATH:
		lrg_decal_component_set_orm_path (self, g_value_get_string (value));
		break;
	case PROP_ASSET_MANAGER:
		lrg_decal_component_set_asset_manager (self, g_value_get_object (value));
		break;
	default:
		/* mirrored: the decal notifies, decal_notify() re-emits */
		if (prop_id >= PROP_COLOR_R && prop_id < N_PROPS)
			g_object_set_property (G_OBJECT (self->decal), pspec->name, value);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_decal_component_class_init (LrgDecalComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_decal_component_dispose;
	object_class->finalize = lrg_decal_component_finalize;
	object_class->get_property = lrg_decal_component_get_property;
	object_class->set_property = lrg_decal_component_set_property;

	component_class->world_entered = lrg_decal_component_world_entered;
	component_class->world_exited = lrg_decal_component_world_exited;
	component_class->late_update = lrg_decal_component_late_update;
	component_class->get_bounds = lrg_decal_component_get_bounds;

	/**
	 * LrgDecalComponent:size-x:
	 *
	 * Box width along the entity's local X.
	 */
	properties[PROP_SIZE_X] =
		g_param_spec_float ("size-x", NULL, "Box width", 0.001f, G_MAXFLOAT, 1.0f, flags);

	/**
	 * LrgDecalComponent:size-y:
	 *
	 * Projection depth along the entity's local Y.
	 */
	properties[PROP_SIZE_Y] =
		g_param_spec_float ("size-y", NULL, "Box depth", 0.001f, G_MAXFLOAT, 1.0f, flags);

	/**
	 * LrgDecalComponent:size-z:
	 *
	 * Box height along the entity's local Z.
	 */
	properties[PROP_SIZE_Z] =
		g_param_spec_float ("size-z", NULL, "Box height", 0.001f, G_MAXFLOAT, 1.0f, flags);

	/**
	 * LrgDecalComponent:albedo-path:
	 *
	 * Albedo texture path or `guid:`.
	 */
	properties[PROP_ALBEDO_PATH] =
		g_param_spec_string ("albedo-path", NULL, "Albedo texture path", NULL, flags);

	/**
	 * LrgDecalComponent:normal-path:
	 *
	 * Normal map path or `guid:`.
	 */
	properties[PROP_NORMAL_PATH] =
		g_param_spec_string ("normal-path", NULL, "Normal map path", NULL, flags);

	/**
	 * LrgDecalComponent:orm-path:
	 *
	 * ORM map path or `guid:`.
	 */
	properties[PROP_ORM_PATH] =
		g_param_spec_string ("orm-path", NULL, "ORM map path", NULL, flags);

	/**
	 * LrgDecalComponent:asset-manager:
	 *
	 * Asset manager the paths load through (%NULL: the renderer's
	 * resolver, see lrg_decal_component_sync_world()).
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", NULL, "Asset manager", LRG_TYPE_ASSET_MANAGER, flags);

	/**
	 * LrgDecalComponent:color-r:
	 *
	 * Mirror of #LrgDecal:color-r.
	 */
	properties[PROP_COLOR_R] =
		g_param_spec_float ("color-r", NULL, "Colour red", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecalComponent:color-g:
	 *
	 * Mirror of #LrgDecal:color-g.
	 */
	properties[PROP_COLOR_G] =
		g_param_spec_float ("color-g", NULL, "Colour green", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecalComponent:color-b:
	 *
	 * Mirror of #LrgDecal:color-b.
	 */
	properties[PROP_COLOR_B] =
		g_param_spec_float ("color-b", NULL, "Colour blue", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecalComponent:color-a:
	 *
	 * Mirror of #LrgDecal:color-a.
	 */
	properties[PROP_COLOR_A] =
		g_param_spec_float ("color-a", NULL, "Colour alpha", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecalComponent:opacity:
	 *
	 * Mirror of #LrgDecal:opacity.
	 */
	properties[PROP_OPACITY] =
		g_param_spec_float ("opacity", NULL, "Opacity", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecalComponent:fade-angle:
	 *
	 * Mirror of #LrgDecal:fade-angle.
	 */
	properties[PROP_FADE_ANGLE] =
		g_param_spec_float ("fade-angle", NULL, "Full-strength angle", 0.0f, 180.0f, 60.0f, flags);

	/**
	 * LrgDecalComponent:fade-width:
	 *
	 * Mirror of #LrgDecal:fade-width.
	 */
	properties[PROP_FADE_WIDTH] =
		g_param_spec_float ("fade-width", NULL, "Fade width", 0.0f, 180.0f, 20.0f, flags);

	/**
	 * LrgDecalComponent:depth-fade:
	 *
	 * Mirror of #LrgDecal:depth-fade.
	 */
	properties[PROP_DEPTH_FADE] =
		g_param_spec_float ("depth-fade", NULL, "Depth fade", 0.0f, 1.0f, 0.0f, flags);

	/**
	 * LrgDecalComponent:sort-order:
	 *
	 * Mirror of #LrgDecal:sort-order.
	 */
	properties[PROP_SORT_ORDER] =
		g_param_spec_int ("sort-order", NULL, "Sort order", G_MININT, G_MAXINT, 0, flags);

	/**
	 * LrgDecalComponent:layer-mask:
	 *
	 * Mirror of #LrgDecal:layer-mask.
	 */
	properties[PROP_LAYER_MASK] =
		g_param_spec_uint ("layer-mask", NULL, "Receiving layers", 0, G_MAXUINT32, G_MAXUINT32,
		                   flags);

	/**
	 * LrgDecalComponent:blend:
	 *
	 * Mirror of #LrgDecal:blend.
	 */
	properties[PROP_BLEND] =
		g_param_spec_enum ("blend", NULL, "Blend mode", LRG_TYPE_DECAL_BLEND,
		                   LRG_DECAL_BLEND_ALPHA, flags);

	/**
	 * LrgDecalComponent:channels:
	 *
	 * Mirror of #LrgDecal:channels.
	 */
	properties[PROP_CHANNELS] =
		g_param_spec_flags ("channels", NULL, "Written channels", LRG_TYPE_DECAL_CHANNELS,
		                    LRG_DECAL_CHANNEL_ALBEDO | LRG_DECAL_CHANNEL_NORMAL, flags);

	/**
	 * LrgDecalComponent:normal-strength:
	 *
	 * Mirror of #LrgDecal:normal-strength.
	 */
	properties[PROP_NORMAL_STRENGTH] =
		g_param_spec_float ("normal-strength", NULL, "Normal strength", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecalComponent:roughness:
	 *
	 * Mirror of #LrgDecal:roughness.
	 */
	properties[PROP_ROUGHNESS] =
		g_param_spec_float ("roughness", NULL, "Roughness", 0.0f, 1.0f, 0.5f, flags);

	/**
	 * LrgDecalComponent:metallic:
	 *
	 * Mirror of #LrgDecal:metallic.
	 */
	properties[PROP_METALLIC] =
		g_param_spec_float ("metallic", NULL, "Metallic", 0.0f, 1.0f, 0.0f, flags);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_decal_component_init (LrgDecalComponent *self)
{
	self->decal = lrg_decal_new ();
	self->size[0] = self->size[1] = self->size[2] = 1.0f;
	g_signal_connect (self->decal, "notify", G_CALLBACK (decal_notify), self);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgDecalComponent *
lrg_decal_component_new (void)
{
	return (LrgDecalComponent *) g_object_new (LRG_TYPE_DECAL_COMPONENT, NULL);
}

LrgDecal *
lrg_decal_component_get_decal (LrgDecalComponent *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_COMPONENT (self), NULL);

	return self->decal;
}

void
lrg_decal_component_set_size (LrgDecalComponent *self,
                              gfloat             x,
                              gfloat             y,
                              gfloat             z)
{
	gfloat   v[3];
	guint    k;
	gboolean any = FALSE;

	g_return_if_fail (LRG_IS_DECAL_COMPONENT (self));

	v[0] = MAX (x, 0.001f);
	v[1] = MAX (y, 0.001f);
	v[2] = MAX (z, 0.001f);
	g_object_freeze_notify (G_OBJECT (self));
	for (k = 0; k < 3; k++)
		if (self->size[k] != v[k])
		{
			self->size[k] = v[k];
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SIZE_X + k]);
			any = TRUE;
		}
	g_object_thaw_notify (G_OBJECT (self));
	if (any)
	{
		LrgEntity3D *entity = lrg_component_get_entity (LRG_COMPONENT (self));
		LrgWorld3D  *world = lrg_component_get_world3d (LRG_COMPONENT (self));

		if (world != NULL && entity != NULL)
			lrg_world3d_invalidate_bounds (world, entity);
		lrg_decal_component_sync (self);
	}
}

void
lrg_decal_component_get_size (LrgDecalComponent *self,
                              gfloat            *out_size)
{
	g_return_if_fail (LRG_IS_DECAL_COMPONENT (self));
	g_return_if_fail (out_size != NULL);

	memcpy (out_size, self->size, sizeof self->size);
}

void
lrg_decal_component_set_albedo_path (LrgDecalComponent *self,
                                     const gchar       *path)
{
	g_return_if_fail (LRG_IS_DECAL_COMPONENT (self));

	set_path (self, 0, path, PROP_ALBEDO_PATH);
}

const gchar *
lrg_decal_component_get_albedo_path (LrgDecalComponent *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_COMPONENT (self), NULL);

	return self->paths[0];
}

void
lrg_decal_component_set_normal_path (LrgDecalComponent *self,
                                     const gchar       *path)
{
	g_return_if_fail (LRG_IS_DECAL_COMPONENT (self));

	set_path (self, 1, path, PROP_NORMAL_PATH);
}

const gchar *
lrg_decal_component_get_normal_path (LrgDecalComponent *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_COMPONENT (self), NULL);

	return self->paths[1];
}

void
lrg_decal_component_set_orm_path (LrgDecalComponent *self,
                                  const gchar       *path)
{
	g_return_if_fail (LRG_IS_DECAL_COMPONENT (self));

	set_path (self, 2, path, PROP_ORM_PATH);
}

const gchar *
lrg_decal_component_get_orm_path (LrgDecalComponent *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_COMPONENT (self), NULL);

	return self->paths[2];
}

void
lrg_decal_component_set_asset_manager (LrgDecalComponent *self,
                                       LrgAssetManager   *manager)
{
	g_return_if_fail (LRG_IS_DECAL_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (!g_set_object (&self->asset_manager, manager))
		return;
	/* paths resolve again through the new source */
	memset (self->failed, 0, sizeof self->failed);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

LrgAssetManager *
lrg_decal_component_get_asset_manager (LrgDecalComponent *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_COMPONENT (self), NULL);

	return self->asset_manager;
}

void
lrg_decal_component_sync (LrgDecalComponent *self)
{
	LrgEntity3D  *entity;
	const gfloat *world;
	gfloat        scale[16];
	gfloat        m[16];
	gboolean      shown;

	g_return_if_fail (LRG_IS_DECAL_COMPONENT (self));

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	shown = entity != NULL && lrg_component_get_enabled (LRG_COMPONENT (self))
	        && lrg_entity3d_is_active_in_hierarchy (entity)
	        && lrg_entity3d_is_visible_in_hierarchy (entity);
	lrg_decal_set_enabled (self->decal, shown);
	if (entity == NULL)
		return;

	/* box = entity world matrix * diag (size) */
	world = lrg_entity3d_peek_world_matrix (entity);
	lrg_math3d_identity (scale);
	scale[0] = self->size[0];
	scale[5] = self->size[1];
	scale[10] = self->size[2];
	lrg_math3d_multiply (world, scale, m);
	lrg_decal_set_transform (self->decal, m);

	resolve_textures (self, NULL, NULL);
}

LrgDecalSet *
lrg_decal_component_get_world_set (LrgWorld3D *world)
{
	LrgDecalSet *set;

	g_return_val_if_fail (LRG_IS_WORLD3D (world), NULL);

	set = g_object_get_data (G_OBJECT (world), WORLD_SET_KEY);
	if (set == NULL)
	{
		set = lrg_decal_set_new ();
		g_object_set_data_full (G_OBJECT (world), WORLD_SET_KEY, set, world_set_free);
	}
	return set;
}

guint
lrg_decal_component_sync_world (LrgWorld3D          *world,
                                LrgDecalTextureFunc  resolver,
                                gpointer             user_data)
{
	g_autoptr(GPtrArray) comps = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_WORLD3D (world), 0);

	comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_DECAL_COMPONENT);
	if (comps == NULL)
		return 0;
	for (i = 0; i < comps->len; i++)
	{
		LrgDecalComponent *self = g_ptr_array_index (comps, i);

		lrg_decal_component_sync (self);
		if (resolver != NULL && self->asset_manager == NULL)
			resolve_textures (self, resolver, user_data);
	}
	return comps->len;
}
