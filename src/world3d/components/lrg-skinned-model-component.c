/* lrg-skinned-model-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedModelComponent: clip playback through an #LrgModelAnimator and
 * a posed model render item.
 */

#include "config.h"

#include <string.h>

#include "lrg-skinned-model-component.h"
#include "../lrg-entity3d.h"
#include "../../core/lrg-asset-manager.h"

struct _LrgSkinnedModelComponent
{
	LrgComponent       parent_instance;

	gchar             *model_path;
	LrgAssetManager   *asset_manager;
	LrgModelAnimator  *animator;
	gboolean           animator_explicit;
	gboolean           load_failed;
	LrgModelAnimState  state;

	gchar             *clip;
	gboolean           clip_dirty;   /* apply the requested clip next update */
	gfloat             speed;
	gboolean           loop;
	gboolean           playing;
	gfloat             crossfade;

	gfloat             color[4];
	gboolean           cast_shadows;
	gboolean           receive_shadows;
	gint               layer;
	LrgMaterial       *material;      /* nullable */
	GPtrArray         *submesh;       /* LrgMaterial* or NULL per mesh index */
};

G_DEFINE_FINAL_TYPE (LrgSkinnedModelComponent, lrg_skinned_model_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_MODEL_PATH,
	PROP_ASSET_MANAGER,
	PROP_ANIMATOR,
	PROP_CLIP,
	PROP_SPEED,
	PROP_LOOP,
	PROP_PLAYING,
	PROP_CROSSFADE,
	PROP_COLOR_R,
	PROP_COLOR_G,
	PROP_COLOR_B,
	PROP_COLOR_A,
	PROP_CAST_SHADOWS,
	PROP_RECEIVE_SHADOWS,
	PROP_LAYER,
	PROP_MATERIAL,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Loads the model and its clips through the asset manager once and wraps
 * them in an animator. */
static void
ensure_animator (LrgSkinnedModelComponent *self)
{
	g_autoptr(GError) error = NULL;
	GrlModel         *model;
	GPtrArray        *clips;

	if (self->animator != NULL || self->model_path == NULL || self->asset_manager == NULL ||
	    self->load_failed)
		return;

	model = lrg_asset_manager_load_model (self->asset_manager, self->model_path, &error);
	if (model != NULL)
		clips = lrg_asset_manager_load_model_animations (self->asset_manager, self->model_path, &error);
	else
		clips = NULL;
	if (model == NULL || clips == NULL)
	{
		g_debug ("LrgSkinnedModelComponent: cannot load '%s': %s", self->model_path,
		         error != NULL ? error->message : "unknown error");
		self->load_failed = TRUE;
		return;
	}
	self->animator = lrg_model_animator_new (model, clips);
	lrg_model_anim_state_reset (&self->state);
	self->clip_dirty = TRUE;
}

/* Drops an animator created from the model path (not an explicit one). */
static void
drop_loaded_animator (LrgSkinnedModelComponent *self)
{
	if (!self->animator_explicit)
		g_clear_object (&self->animator);
	self->load_failed = FALSE;
	lrg_model_anim_state_reset (&self->state);
	self->clip_dirty = TRUE;
}

/* Switches the state to the requested clip (crossfading) if it changed. */
static gboolean
apply_clip (LrgSkinnedModelComponent *self)
{
	gint index;

	if (self->animator == NULL)
		return FALSE;
	self->clip_dirty = FALSE;
	index = (self->clip != NULL) ? lrg_model_animator_find_clip (self->animator, self->clip) : -1;
	if (index < 0 && self->clip != NULL)
		return FALSE;
	return lrg_model_animator_play (self->animator, &self->state, index, self->loop,
	                                (gdouble) self->crossfade);
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

/* Advances the playback by @delta times the speed. */
static void
lrg_skinned_model_component_update (LrgComponent *component,
                                    gfloat        delta)
{
	LrgSkinnedModelComponent *self = LRG_SKINNED_MODEL_COMPONENT (component);

	ensure_animator (self);
	if (self->animator == NULL)
		return;
	if (self->clip_dirty)
		apply_clip (self);
	if (!self->playing)
		return;
	self->state.rate = (gdouble) self->speed;
	lrg_model_animator_advance (self->animator, &self->state, (gdouble) delta);
}

static gboolean
lrg_skinned_model_component_get_bounds (LrgComponent     *component,
                                        LrgBoundingBox3D *out_local)
{
	GrlModel       *model = lrg_skinned_model_component_get_model (LRG_SKINNED_MODEL_COMPONENT (component));
	GrlBoundingBox *box;

	if (model == NULL)
		return FALSE;
	box = grl_model_get_bounding_box (model);
	if (box == NULL)
		return FALSE;
	out_local->min = box->min;
	out_local->max = box->max;
	grl_bounding_box_free (box);
	return TRUE;
}

/* Free function of the submesh array, whose holes are NULL. */
static void
unref_nullable (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}

/* Colour, material and material flags of one item: the component colour
 * (times the material's base colour) and, with a material, the flags its
 * render queue, cull mode and shading model imply. */
static void
item_set_material (LrgSkinnedModelComponent *self,
                   LrgRenderItem            *item,
                   LrgMaterial              *material)
{
	guint flags = (guint) item->flags;
	guint k;

	memcpy (item->color, self->color, sizeof item->color);
	item->material = material;
	if (material == NULL)
	{
		if (self->color[3] < 1.0f)
			flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		item->flags = (LrgRenderItemFlags) flags;
		return;
	}
	{
		gfloat base[4];

		lrg_material_get_base_color (material, base);
		for (k = 0; k < 4; k++)
			item->color[k] *= base[k];
	}
	switch (lrg_material_resolve_render_queue (material))
	{
	case LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT:
		flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		break;
	case LRG_MATERIAL_RENDER_QUEUE_CUTOUT:
		flags |= LRG_RENDER_ITEM_FLAG_CUTOUT;
		break;
	case LRG_MATERIAL_RENDER_QUEUE_AUTO:
	case LRG_MATERIAL_RENDER_QUEUE_OPAQUE:
	default:
		if (self->color[3] < 1.0f)
			flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		break;
	}
	if (lrg_material_get_cull_mode (material) == LRG_MATERIAL_CULL_NONE)
		flags |= LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED;
	if (lrg_material_get_shading_model (material) == LRG_SHADING_MODEL_UNLIT)
		flags |= LRG_RENDER_ITEM_FLAG_UNLIT;
	item->flags = (LrgRenderItemFlags) flags;
}

/* One MODEL item with the animator's model, posed for this component. */
static void
lrg_skinned_model_component_collect_render (LrgComponent  *component,
                                            LrgRenderList *list)
{
	LrgSkinnedModelComponent *self = LRG_SKINNED_MODEL_COMPONENT (component);
	LrgEntity3D              *entity = lrg_component_get_entity (component);
	GrlModel                 *model;
	LrgRenderItem             item;
	LrgBoundingBox3D          local;
	guint                     flags = LRG_RENDER_ITEM_FLAG_NONE;

	if (entity == NULL)
		return;
	model = lrg_skinned_model_component_get_model (self);
	if (model == NULL)
		return;

	lrg_skinned_model_component_apply_pose (self);

	lrg_render_item_init_for_entity (&item, entity, component);
	item.kind = LRG_RENDER_ITEM_MODEL;
	item.model = model;
	item.mesh_index = -1;
	if (self->cast_shadows)
		flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
	if (self->receive_shadows)
		flags |= LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
	item.flags = (LrgRenderItemFlags) flags;
	item_set_material (self, &item, self->material);
	if (self->layer >= 0)
		item.layer = (guint) self->layer;
	item.sort_key = ((guint64) LRG_RENDER_ITEM_MODEL << 56) |
	                ((guint64) (guintptr) model & G_GUINT64_CONSTANT (0x00ffffffffffffff));
	if (lrg_skinned_model_component_get_bounds (component, &local))
		lrg_render_item_set_local_bounds (&item, &local);

	/* Submesh materials: one posed item per mesh. */
	if (self->submesh != NULL && self->submesh->len > 0)
	{
		gint n = grl_model_get_mesh_count (model);
		gint i;

		for (i = 0; i < n; i++)
		{
			LrgRenderItem sub = item;
			LrgMaterial  *m = lrg_skinned_model_component_get_submesh_material (self, (guint) i);

			sub.mesh_index = i;
			sub.flags = (LrgRenderItemFlags) flags;
			item_set_material (self, &sub, m != NULL ? m : self->material);
			if (lrg_render_list_accepts (list, &sub))
				lrg_render_list_add (list, &sub);
		}
		return;
	}

	if (lrg_render_list_accepts (list, &item))
		lrg_render_list_add (list, &item);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_skinned_model_component_dispose (GObject *object)
{
	LrgSkinnedModelComponent *self = LRG_SKINNED_MODEL_COMPONENT (object);

	g_clear_object (&self->animator);
	g_clear_object (&self->material);
	g_clear_pointer (&self->submesh, g_ptr_array_unref);
	g_clear_object (&self->asset_manager);

	G_OBJECT_CLASS (lrg_skinned_model_component_parent_class)->dispose (object);
}

static void
lrg_skinned_model_component_finalize (GObject *object)
{
	LrgSkinnedModelComponent *self = LRG_SKINNED_MODEL_COMPONENT (object);

	g_free (self->model_path);
	g_free (self->clip);

	G_OBJECT_CLASS (lrg_skinned_model_component_parent_class)->finalize (object);
}

/* Stores a float and notifies when it changed. */
static void
set_float (LrgSkinnedModelComponent *self,
           gfloat                   *slot,
           gfloat                    value,
           guint                     prop)
{
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* Stores a boolean and notifies when it changed; TRUE on a change. */
static gboolean
set_bool (LrgSkinnedModelComponent *self,
          gboolean                 *slot,
          gboolean                  value,
          guint                     prop)
{
	value = !!value;
	if (*slot == value)
		return FALSE;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
	return TRUE;
}

static void
lrg_skinned_model_component_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
	LrgSkinnedModelComponent *self = LRG_SKINNED_MODEL_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_MODEL_PATH:      g_value_set_string (value, self->model_path); break;
	case PROP_ASSET_MANAGER:   g_value_set_object (value, self->asset_manager); break;
	case PROP_ANIMATOR:        g_value_set_object (value, self->animator); break;
	case PROP_CLIP:            g_value_set_string (value, self->clip); break;
	case PROP_SPEED:           g_value_set_float (value, self->speed); break;
	case PROP_LOOP:            g_value_set_boolean (value, self->loop); break;
	case PROP_PLAYING:         g_value_set_boolean (value, self->playing); break;
	case PROP_CROSSFADE:       g_value_set_float (value, self->crossfade); break;
	case PROP_COLOR_R:         g_value_set_float (value, self->color[0]); break;
	case PROP_COLOR_G:         g_value_set_float (value, self->color[1]); break;
	case PROP_COLOR_B:         g_value_set_float (value, self->color[2]); break;
	case PROP_COLOR_A:         g_value_set_float (value, self->color[3]); break;
	case PROP_CAST_SHADOWS:    g_value_set_boolean (value, self->cast_shadows); break;
	case PROP_RECEIVE_SHADOWS: g_value_set_boolean (value, self->receive_shadows); break;
	case PROP_LAYER:           g_value_set_int (value, self->layer); break;
	case PROP_MATERIAL:        g_value_set_object (value, self->material); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skinned_model_component_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
	LrgSkinnedModelComponent *self = LRG_SKINNED_MODEL_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_MODEL_PATH:
		lrg_skinned_model_component_set_model_path (self, g_value_get_string (value));
		break;
	case PROP_ASSET_MANAGER:
		lrg_skinned_model_component_set_asset_manager (self, g_value_get_object (value));
		break;
	case PROP_ANIMATOR:
		lrg_skinned_model_component_set_animator (self, g_value_get_object (value));
		break;
	case PROP_CLIP:
		lrg_skinned_model_component_set_clip (self, g_value_get_string (value));
		break;
	case PROP_SPEED:
		set_float (self, &self->speed, g_value_get_float (value), prop_id);
		break;
	case PROP_LOOP:
		/* The loop mode reaches the state with the next clip application */
		if (set_bool (self, &self->loop, g_value_get_boolean (value), prop_id))
			self->clip_dirty = TRUE;
		break;
	case PROP_PLAYING:
		set_bool (self, &self->playing, g_value_get_boolean (value), prop_id);
		break;
	case PROP_CROSSFADE:
		set_float (self, &self->crossfade, g_value_get_float (value), prop_id);
		break;
	case PROP_COLOR_R: set_float (self, &self->color[0], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_G: set_float (self, &self->color[1], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_B: set_float (self, &self->color[2], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_A: set_float (self, &self->color[3], g_value_get_float (value), prop_id); break;
	case PROP_CAST_SHADOWS:
		set_bool (self, &self->cast_shadows, g_value_get_boolean (value), prop_id);
		break;
	case PROP_RECEIVE_SHADOWS:
		set_bool (self, &self->receive_shadows, g_value_get_boolean (value), prop_id);
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_int (value))
		{
			self->layer = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_MATERIAL:
		lrg_skinned_model_component_set_material (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skinned_model_component_class_init (LrgSkinnedModelComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_skinned_model_component_dispose;
	object_class->finalize = lrg_skinned_model_component_finalize;
	object_class->get_property = lrg_skinned_model_component_get_property;
	object_class->set_property = lrg_skinned_model_component_set_property;

	component_class->update = lrg_skinned_model_component_update;
	component_class->collect_render = lrg_skinned_model_component_collect_render;
	component_class->get_bounds = lrg_skinned_model_component_get_bounds;

	/**
	 * LrgSkinnedModelComponent:model-path:
	 *
	 * The glTF/GLB model path or asset GUID.
	 */
	properties[PROP_MODEL_PATH] = g_param_spec_string ("model-path", NULL, "Model path", NULL, rw);

	/**
	 * LrgSkinnedModelComponent:asset-manager:
	 *
	 * The #LrgAssetManager the model loads through.
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", NULL, "Asset manager", LRG_TYPE_ASSET_MANAGER, rw);

	/**
	 * LrgSkinnedModelComponent:animator:
	 *
	 * The #LrgModelAnimator (explicit or created from the model path).
	 */
	properties[PROP_ANIMATOR] =
		g_param_spec_object ("animator", NULL, "Model animator", LRG_TYPE_MODEL_ANIMATOR, rw);

	/**
	 * LrgSkinnedModelComponent:clip:
	 *
	 * The clip name or alias to play.
	 */
	properties[PROP_CLIP] = g_param_spec_string ("clip", NULL, "Clip", NULL, rw);

	/**
	 * LrgSkinnedModelComponent:speed:
	 *
	 * Playback rate (negative plays backwards).
	 */
	properties[PROP_SPEED] = g_param_spec_float ("speed", NULL, "Playback speed", -100.0f, 100.0f,
	                                             1.0f, rw);

	/**
	 * LrgSkinnedModelComponent:loop:
	 *
	 * Whether the clip wraps around.
	 */
	properties[PROP_LOOP] = g_param_spec_boolean ("loop", NULL, "Loop", TRUE, rw);

	/**
	 * LrgSkinnedModelComponent:playing:
	 *
	 * Whether time advances (a paused model holds its pose).
	 */
	properties[PROP_PLAYING] = g_param_spec_boolean ("playing", NULL, "Playing", TRUE, rw);

	/**
	 * LrgSkinnedModelComponent:crossfade:
	 *
	 * Seconds of crossfade when the clip changes (0 cuts).
	 */
	properties[PROP_CROSSFADE] = g_param_spec_float ("crossfade", NULL, "Crossfade seconds", 0.0f,
	                                                 60.0f, 0.0f, rw);

	/**
	 * LrgSkinnedModelComponent:color-r:
	 *
	 * Tint red, 0 .. 1.
	 */
	properties[PROP_COLOR_R] = g_param_spec_float ("color-r", NULL, "Tint red", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgSkinnedModelComponent:color-g:
	 *
	 * Tint green, 0 .. 1.
	 */
	properties[PROP_COLOR_G] = g_param_spec_float ("color-g", NULL, "Tint green", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgSkinnedModelComponent:color-b:
	 *
	 * Tint blue, 0 .. 1.
	 */
	properties[PROP_COLOR_B] = g_param_spec_float ("color-b", NULL, "Tint blue", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgSkinnedModelComponent:color-a:
	 *
	 * Tint alpha, 0 .. 1.
	 */
	properties[PROP_COLOR_A] = g_param_spec_float ("color-a", NULL, "Tint alpha", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgSkinnedModelComponent:cast-shadows:
	 *
	 * Whether the model casts shadows.
	 */
	properties[PROP_CAST_SHADOWS] = g_param_spec_boolean ("cast-shadows", NULL, "Cast shadows",
	                                                      TRUE, rw);

	/**
	 * LrgSkinnedModelComponent:receive-shadows:
	 *
	 * Whether the model is shadowed.
	 */
	properties[PROP_RECEIVE_SHADOWS] = g_param_spec_boolean ("receive-shadows", NULL,
	                                                         "Receive shadows", TRUE, rw);

	/**
	 * LrgSkinnedModelComponent:layer:
	 *
	 * Render layer override 0 .. 31, or -1 to use the entity's layer.
	 */
	properties[PROP_LAYER] = g_param_spec_int ("layer", NULL, "Render layer (-1 entity)", -1, 31,
	                                           -1, rw);

	/**
	 * LrgSkinnedModelComponent:material:
	 *
	 * The #LrgMaterial the model draws with (unless a mesh has a submesh
	 * material), or %NULL for the model's own materials.
	 */
	properties[PROP_MATERIAL] = g_param_spec_object ("material", NULL, "Material",
	                                                 LRG_TYPE_MATERIAL, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_skinned_model_component_init (LrgSkinnedModelComponent *self)
{
	lrg_model_anim_state_reset (&self->state);
	self->speed = 1.0f;
	self->loop = TRUE;
	self->playing = TRUE;
	self->color[0] = self->color[1] = self->color[2] = self->color[3] = 1.0f;
	self->cast_shadows = TRUE;
	self->receive_shadows = TRUE;
	self->layer = -1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgSkinnedModelComponent *
lrg_skinned_model_component_new (void)
{
	return (LrgSkinnedModelComponent *) g_object_new (LRG_TYPE_SKINNED_MODEL_COMPONENT, NULL);
}

LrgSkinnedModelComponent *
lrg_skinned_model_component_new_with_model (const gchar *model_path,
                                            const gchar *clip)
{
	return (LrgSkinnedModelComponent *) g_object_new (LRG_TYPE_SKINNED_MODEL_COMPONENT,
	                                                  "model-path", model_path, "clip", clip, NULL);
}

const gchar *
lrg_skinned_model_component_get_model_path (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);

	return self->model_path;
}

void
lrg_skinned_model_component_set_model_path (LrgSkinnedModelComponent *self,
                                            const gchar              *model_path)
{
	g_return_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self));

	if (model_path != NULL && *model_path == '\0')
		model_path = NULL;
	if (g_strcmp0 (self->model_path, model_path) == 0)
		return;
	g_free (self->model_path);
	self->model_path = g_strdup (model_path);
	drop_loaded_animator (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL_PATH]);
}

void
lrg_skinned_model_component_set_asset_manager (LrgSkinnedModelComponent *self,
                                               LrgAssetManager          *manager)
{
	g_return_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (!g_set_object (&self->asset_manager, manager))
		return;
	drop_loaded_animator (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

LrgModelAnimator *
lrg_skinned_model_component_get_animator (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);

	return self->animator;
}

void
lrg_skinned_model_component_set_animator (LrgSkinnedModelComponent *self,
                                          LrgModelAnimator         *animator)
{
	g_return_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self));
	g_return_if_fail (animator == NULL || LRG_IS_MODEL_ANIMATOR (animator));

	if (self->animator == animator && self->animator_explicit == (animator != NULL))
		return;
	g_set_object (&self->animator, animator);
	self->animator_explicit = (animator != NULL);
	self->load_failed = FALSE;
	lrg_model_anim_state_reset (&self->state);
	self->clip_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATOR]);
}

const gchar *
lrg_skinned_model_component_get_clip (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);

	return self->clip;
}

void
lrg_skinned_model_component_set_clip (LrgSkinnedModelComponent *self,
                                      const gchar              *clip)
{
	g_return_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self));

	if (clip != NULL && *clip == '\0')
		clip = NULL;
	if (g_strcmp0 (self->clip, clip) == 0)
		return;
	g_free (self->clip);
	self->clip = g_strdup (clip);
	self->clip_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLIP]);
}

gboolean
lrg_skinned_model_component_play (LrgSkinnedModelComponent *self,
                                  const gchar              *clip,
                                  gboolean                  loop)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), FALSE);
	g_return_val_if_fail (clip != NULL, FALSE);

	g_object_freeze_notify (G_OBJECT (self));
	lrg_skinned_model_component_set_clip (self, clip);
	set_bool (self, &self->loop, loop, PROP_LOOP);
	set_bool (self, &self->playing, TRUE, PROP_PLAYING);
	g_object_thaw_notify (G_OBJECT (self));

	ensure_animator (self);
	return apply_clip (self);
}

const LrgModelAnimState *
lrg_skinned_model_component_get_state (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);

	return &self->state;
}

const gchar *
lrg_skinned_model_component_get_current_clip_name (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);

	if (self->animator == NULL || self->state.clip < 0)
		return NULL;
	return lrg_model_animator_get_clip_name (self->animator, self->state.clip);
}

gboolean
lrg_skinned_model_component_is_finished (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), FALSE);

	return self->animator != NULL && lrg_model_animator_is_finished (self->animator, &self->state);
}

GrlModel *
lrg_skinned_model_component_get_model (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);

	return (self->animator != NULL) ? lrg_model_animator_get_model (self->animator) : NULL;
}

gboolean
lrg_skinned_model_component_apply_pose (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), FALSE);

	if (self->animator == NULL)
		return FALSE;
	return lrg_model_animator_apply (self->animator, &self->state);
}

LrgMaterial *
lrg_skinned_model_component_get_material (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);
	return self->material;
}

void
lrg_skinned_model_component_set_material (LrgSkinnedModelComponent *self,
                                          LrgMaterial              *material)
{
	g_return_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self));
	g_return_if_fail (material == NULL || LRG_IS_MATERIAL (material));

	if (g_set_object (&self->material, material))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MATERIAL]);
}

void
lrg_skinned_model_component_set_submesh_material (LrgSkinnedModelComponent *self,
                                                  guint                     index,
                                                  LrgMaterial              *material)
{
	g_return_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self));
	g_return_if_fail (material == NULL || LRG_IS_MATERIAL (material));
	g_return_if_fail (index < 4096);

	if (self->submesh == NULL)
	{
		if (material == NULL)
			return;
		self->submesh = g_ptr_array_new_with_free_func (unref_nullable);
	}
	while (self->submesh->len <= index)
		g_ptr_array_add (self->submesh, NULL);
	unref_nullable (g_ptr_array_index (self->submesh, index));
	g_ptr_array_index (self->submesh, index) = material != NULL ? g_object_ref (material) : NULL;
	/* trailing empty entries do not count */
	while (self->submesh->len > 0 && g_ptr_array_index (self->submesh, self->submesh->len - 1) == NULL)
		g_ptr_array_set_size (self->submesh, self->submesh->len - 1);
}

LrgMaterial *
lrg_skinned_model_component_get_submesh_material (LrgSkinnedModelComponent *self,
                                                  guint                     index)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), NULL);

	if (self->submesh == NULL || index >= self->submesh->len)
		return NULL;
	return g_ptr_array_index (self->submesh, index);
}

guint
lrg_skinned_model_component_get_n_submesh_materials (LrgSkinnedModelComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_MODEL_COMPONENT (self), 0);
	return self->submesh != NULL ? self->submesh->len : 0;
}
