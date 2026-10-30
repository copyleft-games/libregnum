/* lrg-procgen-generator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class of the procedural map generators.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-generator.h"
#include "lrg-procgen-bsp.h"
#include "lrg-procgen-caves.h"
#include "lrg-procgen-wfc.h"
#include "lrg-procgen-drunkard.h"
#include "lrg-procgen-noise.h"
#include "lrg-procgen-dungeon.h"
#include "../lrg-log.h"

typedef struct
{
	guint width;
	guint height;
} LrgProcgenGeneratorPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgProcgenGenerator, lrg_procgen_generator, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_WIDTH,
	PROP_HEIGHT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_procgen_generator_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgProcgenGenerator *self = LRG_PROCGEN_GENERATOR (object);
	LrgProcgenGeneratorPrivate *priv = lrg_procgen_generator_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_WIDTH:
		g_value_set_uint (value, priv->width);
		break;
	case PROP_HEIGHT:
		g_value_set_uint (value, priv->height);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_generator_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgProcgenGenerator *self = LRG_PROCGEN_GENERATOR (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		lrg_procgen_generator_set_width (self, g_value_get_uint (value));
		break;
	case PROP_HEIGHT:
		lrg_procgen_generator_set_height (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_generator_class_init (LrgProcgenGeneratorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_procgen_generator_get_property;
	object_class->set_property = lrg_procgen_generator_set_property;

	/**
	 * LrgProcgenGenerator:width:
	 *
	 * Width of the generated grids, in cells.
	 */
	properties[PROP_WIDTH] =
		g_param_spec_uint ("width", "Width", "Grid width in cells",
		                   1, 4096, 64,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenGenerator:height:
	 *
	 * Height of the generated grids, in cells.
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_uint ("height", "Height", "Grid height in cells",
		                   1, 4096, 48,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procgen_generator_init (LrgProcgenGenerator *self)
{
	LrgProcgenGeneratorPrivate *priv = lrg_procgen_generator_get_instance_private (self);

	priv->width = 64;
	priv->height = 48;
}

guint
lrg_procgen_generator_get_width (LrgProcgenGenerator *self)
{
	LrgProcgenGeneratorPrivate *priv;

	g_return_val_if_fail (LRG_IS_PROCGEN_GENERATOR (self), 0);
	priv = lrg_procgen_generator_get_instance_private (self);
	return priv->width;
}

void
lrg_procgen_generator_set_width (LrgProcgenGenerator *self,
                                 guint                width)
{
	LrgProcgenGeneratorPrivate *priv;

	g_return_if_fail (LRG_IS_PROCGEN_GENERATOR (self));
	g_return_if_fail (width >= 1 && width <= 4096);
	priv = lrg_procgen_generator_get_instance_private (self);
	if (priv->width == width)
		return;
	priv->width = width;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
}

guint
lrg_procgen_generator_get_height (LrgProcgenGenerator *self)
{
	LrgProcgenGeneratorPrivate *priv;

	g_return_val_if_fail (LRG_IS_PROCGEN_GENERATOR (self), 0);
	priv = lrg_procgen_generator_get_instance_private (self);
	return priv->height;
}

void
lrg_procgen_generator_set_height (LrgProcgenGenerator *self,
                                  guint                height)
{
	LrgProcgenGeneratorPrivate *priv;

	g_return_if_fail (LRG_IS_PROCGEN_GENERATOR (self));
	g_return_if_fail (height >= 1 && height <= 4096);
	priv = lrg_procgen_generator_get_instance_private (self);
	if (priv->height == height)
		return;
	priv->height = height;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
}

LrgProcgenGrid *
lrg_procgen_generator_generate_with_rng (LrgProcgenGenerator  *self,
                                         LrgRandomStream      *rng,
                                         GError              **error)
{
	LrgProcgenGeneratorPrivate *priv;
	LrgProcgenGeneratorClass *klass;
	g_autoptr(LrgProcgenGrid) grid = NULL;

	g_return_val_if_fail (LRG_IS_PROCGEN_GENERATOR (self), NULL);
	g_return_val_if_fail (LRG_IS_RANDOM_STREAM (rng), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	priv = lrg_procgen_generator_get_instance_private (self);
	klass = LRG_PROCGEN_GENERATOR_GET_CLASS (self);
	if (klass->generate == NULL)
	{
		g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_FAILED,
		             "%s does not implement generate()", G_OBJECT_TYPE_NAME (self));
		return NULL;
	}

	/* a blank wall-filled grid for the subclass to carve */
	grid = lrg_procgen_grid_new (priv->width, priv->height);
	if (!klass->generate (self, grid, rng, error))
		return NULL;
	lrg_procgen_grid_label_regions (grid);
	return g_steal_pointer (&grid);
}

LrgProcgenGrid *
lrg_procgen_generator_generate (LrgProcgenGenerator  *self,
                                guint64               seed,
                                GError              **error)
{
	g_autoptr(LrgRandomStream) rng = NULL;

	g_return_val_if_fail (LRG_IS_PROCGEN_GENERATOR (self), NULL);

	rng = lrg_random_stream_new (seed, LRG_PROCGEN_STREAM);
	return lrg_procgen_generator_generate_with_rng (self, rng, error);
}

/* the built-in generators and their short names */
static GType
builtin_type (guint i)
{
	switch (i)
	{
	case 0: return LRG_TYPE_PROCGEN_BSP;
	case 1: return LRG_TYPE_PROCGEN_CAVES;
	case 2: return LRG_TYPE_PROCGEN_WFC;
	case 3: return LRG_TYPE_PROCGEN_DRUNKARD;
	case 4: return LRG_TYPE_PROCGEN_NOISE;
	default: return G_TYPE_INVALID;
	}
}

static const gchar * const builtin_names[] = { "bsp", "caves", "wfc", "drunkard", "noise" };

GType
lrg_procgen_generator_type_from_name (const gchar *name)
{
	GType type;
	guint i;

	g_return_val_if_fail (name != NULL, G_TYPE_INVALID);

	for (i = 0; i < G_N_ELEMENTS (builtin_names); i++)
		if (g_strcmp0 (name, builtin_names[i]) == 0)
			return builtin_type (i);

	/* a game's own generator, by GType name */
	type = g_type_from_name (name);
	if (type != G_TYPE_INVALID && g_type_is_a (type, LRG_TYPE_PROCGEN_GENERATOR) &&
	    !G_TYPE_IS_ABSTRACT (type))
		return type;
	return G_TYPE_INVALID;
}

const gchar *
lrg_procgen_generator_type_to_name (GType type)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (builtin_names); i++)
		if (builtin_type (i) == type)
			return builtin_names[i];
	return g_type_name (type);
}

void
lrg_procgen_register_types (LrgRegistry *registry)
{
	guint i;

	g_return_if_fail (LRG_IS_REGISTRY (registry));

	for (i = 0; i < G_N_ELEMENTS (builtin_names); i++)
	{
		g_autofree gchar *name = g_strconcat ("procgen-", builtin_names[i], NULL);

		lrg_registry_register (registry, name, builtin_type (i));
	}
	lrg_registry_register (registry, "procgen-dungeon", LRG_TYPE_PROCGEN_DUNGEON);
}
