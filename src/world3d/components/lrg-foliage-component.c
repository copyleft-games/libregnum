/* lrg-foliage-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFoliageComponent: draws a stand-alone LrgFoliage (re-scattered on an
 * optional terrain asset when patches are dirty).  The drawing itself is
 * _lrg_foliage_collect_render(), shared with LrgTerrainComponent.
 */

#include "config.h"

#include <math.h>

#include "lrg-foliage-component.h"
#include "../lrg-entity3d.h"
#include "../../graphics/lrg-terrain-asset-private.h"

struct _LrgFoliageComponent
{
	LrgComponent     parent_instance;

	LrgFoliage      *foliage;
	LrgTerrainAsset *terrain;
	gint             layer;
	gfloat           time;
};

G_DEFINE_FINAL_TYPE (LrgFoliageComponent, lrg_foliage_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_FOLIAGE_SET,
	PROP_TERRAIN,
	PROP_LAYER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_foliage_component_collect_render (LrgComponent  *component,
                                      LrgRenderList *list)
{
	LrgFoliageComponent *self = LRG_FOLIAGE_COMPONENT (component);
	LrgEntity3D         *entity = lrg_component_get_entity (component);

	if (entity == NULL || self->foliage == NULL)
		return;
	_lrg_foliage_collect_render (self->foliage, self->terrain, list, entity, component, self->layer, self->time);
}

static void
lrg_foliage_component_update (LrgComponent *component,
                              gfloat        delta)
{
	LrgFoliageComponent *self = LRG_FOLIAGE_COMPONENT (component);

	if (isfinite (delta) && delta > 0.0f)
		self->time += delta;
}

static void
lrg_foliage_component_dispose (GObject *object)
{
	LrgFoliageComponent *self = LRG_FOLIAGE_COMPONENT (object);

	g_clear_object (&self->foliage);
	g_clear_object (&self->terrain);
	G_OBJECT_CLASS (lrg_foliage_component_parent_class)->dispose (object);
}

static void
lrg_foliage_component_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgFoliageComponent *self = LRG_FOLIAGE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_FOLIAGE_SET:
		g_value_set_object (value, self->foliage);
		break;
	case PROP_TERRAIN:
		g_value_set_object (value, self->terrain);
		break;
	case PROP_LAYER:
		g_value_set_int (value, self->layer);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_foliage_component_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgFoliageComponent *self = LRG_FOLIAGE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_FOLIAGE_SET:
		lrg_foliage_component_set_foliage_set (self, g_value_get_object (value));
		break;
	case PROP_TERRAIN:
		lrg_foliage_component_set_terrain (self, g_value_get_object (value));
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
		break;
	}
}

static void
lrg_foliage_component_class_init (LrgFoliageComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_foliage_component_dispose;
	object_class->get_property = lrg_foliage_component_get_property;
	object_class->set_property = lrg_foliage_component_set_property;
	component_class->collect_render = lrg_foliage_component_collect_render;
	component_class->update = lrg_foliage_component_update;

	/**
	 * LrgFoliageComponent:foliage-set:
	 *
	 * The foliage drawn.
	 */
	properties[PROP_FOLIAGE_SET] = g_param_spec_object ("foliage-set", NULL, "Foliage", LRG_TYPE_FOLIAGE, rw);

	/**
	 * LrgFoliageComponent:terrain:
	 *
	 * The terrain asset dirty patches re-scatter on (optional).
	 */
	properties[PROP_TERRAIN] = g_param_spec_object ("terrain", NULL, "Ground terrain", LRG_TYPE_TERRAIN_ASSET, rw);

	/**
	 * LrgFoliageComponent:layer:
	 *
	 * Render layer override 0 .. 31, or -1 for the entity's layer.
	 */
	properties[PROP_LAYER] = g_param_spec_int ("layer", NULL, "Render layer (-1 entity)", -1, 31, -1, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_foliage_component_init (LrgFoliageComponent *self)
{
	self->layer = -1;
}

LrgFoliageComponent *
lrg_foliage_component_new (void)
{
	return (LrgFoliageComponent *) g_object_new (LRG_TYPE_FOLIAGE_COMPONENT, NULL);
}

LrgFoliage *
lrg_foliage_component_get_foliage_set (LrgFoliageComponent *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_COMPONENT (self), NULL);
	return self->foliage;
}

void
lrg_foliage_component_set_foliage_set (LrgFoliageComponent *self,
                                       LrgFoliage          *foliage)
{
	g_return_if_fail (LRG_IS_FOLIAGE_COMPONENT (self));
	if (g_set_object (&self->foliage, foliage))
	{
		if (foliage != NULL && self->terrain != NULL)
		{
			guint cx, cz;

			lrg_terrain_asset_get_cells (self->terrain, &cx, &cz);
			lrg_foliage_set_grid (foliage, cx, cz, lrg_terrain_asset_get_cell_size (self->terrain));
		}
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLIAGE_SET]);
	}
}

LrgTerrainAsset *
lrg_foliage_component_get_terrain (LrgFoliageComponent *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_COMPONENT (self), NULL);
	return self->terrain;
}

void
lrg_foliage_component_set_terrain (LrgFoliageComponent *self,
                                   LrgTerrainAsset     *terrain)
{
	g_return_if_fail (LRG_IS_FOLIAGE_COMPONENT (self));
	if (!g_set_object (&self->terrain, terrain))
		return;
	if (terrain != NULL && self->foliage != NULL)
	{
		guint cx, cz;

		lrg_terrain_asset_get_cells (terrain, &cx, &cz);
		lrg_foliage_set_grid (self->foliage, cx, cz, lrg_terrain_asset_get_cell_size (terrain));
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TERRAIN]);
}
