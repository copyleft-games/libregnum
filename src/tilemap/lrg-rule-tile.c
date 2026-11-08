/* lrg-rule-tile.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRuleTile: autotiling with 3x3 neighbour rules, weighted variants,
 * rotated/mirrored rule matching and the 16/47-tile presets.  All the
 * cell work happens on plain row-major tile arrays so the runtime
 * tilemap, the level data and the editor share one implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-rule-tile.h"
#include "lrg-tileset.h"
#include "lrg-tilemap-layer.h"
#include "../lrg-log.h"
#include "../core/lrg-data-loader.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Enum types
 * ========================================================================== */

GType
lrg_tile_neighbor_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_TILE_NEIGHBOR_NONE, "LRG_TILE_NEIGHBOR_NONE", "none" },
			{ LRG_TILE_NEIGHBOR_N, "LRG_TILE_NEIGHBOR_N", "n" },
			{ LRG_TILE_NEIGHBOR_NE, "LRG_TILE_NEIGHBOR_NE", "ne" },
			{ LRG_TILE_NEIGHBOR_E, "LRG_TILE_NEIGHBOR_E", "e" },
			{ LRG_TILE_NEIGHBOR_SE, "LRG_TILE_NEIGHBOR_SE", "se" },
			{ LRG_TILE_NEIGHBOR_S, "LRG_TILE_NEIGHBOR_S", "s" },
			{ LRG_TILE_NEIGHBOR_SW, "LRG_TILE_NEIGHBOR_SW", "sw" },
			{ LRG_TILE_NEIGHBOR_W, "LRG_TILE_NEIGHBOR_W", "w" },
			{ LRG_TILE_NEIGHBOR_NW, "LRG_TILE_NEIGHBOR_NW", "nw" },
			{ LRG_TILE_NEIGHBOR_ALL, "LRG_TILE_NEIGHBOR_ALL", "all" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgTileNeighbor"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_tile_neighbor_layout_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, "LRG_TILE_NEIGHBOR_LAYOUT_SQUARE", "square" },
			{ LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD, "LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD", "staggered-odd" },
			{ LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_EVEN, "LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_EVEN", "staggered-even" },
			{ LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, "LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD", "hex-rows-odd" },
			{ LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN, "LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN", "hex-rows-even" },
			{ LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD, "LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD", "hex-columns-odd" },
			{ LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN, "LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN", "hex-columns-even" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTileNeighborLayout"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_tile_rule_match_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TILE_RULE_MATCH_ANY, "LRG_TILE_RULE_MATCH_ANY", "any" },
			{ LRG_TILE_RULE_MATCH_THIS, "LRG_TILE_RULE_MATCH_THIS", "this" },
			{ LRG_TILE_RULE_MATCH_NOT_THIS, "LRG_TILE_RULE_MATCH_NOT_THIS", "not-this" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTileRuleMatch"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* nicks double as the .rtileset vocabulary for `transform:` */
static const GEnumValue transform_values[] = {
	{ LRG_TILE_RULE_TRANSFORM_FIXED, "LRG_TILE_RULE_TRANSFORM_FIXED", "fixed" },
	{ LRG_TILE_RULE_TRANSFORM_ROTATED, "LRG_TILE_RULE_TRANSFORM_ROTATED", "rotated" },
	{ LRG_TILE_RULE_TRANSFORM_MIRROR_X, "LRG_TILE_RULE_TRANSFORM_MIRROR_X", "mirror-x" },
	{ LRG_TILE_RULE_TRANSFORM_MIRROR_Y, "LRG_TILE_RULE_TRANSFORM_MIRROR_Y", "mirror-y" },
	{ LRG_TILE_RULE_TRANSFORM_MIRROR_XY, "LRG_TILE_RULE_TRANSFORM_MIRROR_XY", "mirror-xy" },
	{ 0, NULL, NULL }
};

GType
lrg_tile_rule_transform_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		GType id = g_enum_register_static (g_intern_static_string ("LrgTileRuleTransform"),
		                                   transform_values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Neighbours
 *
 * Direction index i (0..7) is the bit 1 << i of LrgTileNeighbor, clockwise
 * from north.
 * ========================================================================== */

static const gint square_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const gint square_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

/* the bit index of a single-bit direction, or -1 */
static gint
direction_index (LrgTileNeighbor direction)
{
	gint i;

	for (i = 0; i < 8; i++)
		if ((guint) direction == (1u << i))
			return i;
	return -1;
}

/*
 * staggered_screen_neighbor:
 * Neighbours on a grid with staggered rows (odd or even rows shifted half
 * a cell right).  @screen_dir is a screen direction index: E/W are the
 * cells beside in the same row, N/S two rows away (staggered isometric
 * only), the diagonals the cells of the rows above and below.
 */
static gboolean
staggered_row_neighbor (gboolean  odd_shifted,
                        gint      x,
                        gint      y,
                        gint      screen_dir,
                        gint     *nx,
                        gint     *ny)
{
	gboolean shifted = ((y & 1) != 0) == odd_shifted;

	switch (screen_dir)
	{
	case 0: *nx = x; *ny = y - 2; return TRUE;                   /* N */
	case 4: *nx = x; *ny = y + 2; return TRUE;                   /* S */
	case 2: *nx = x + 1; *ny = y; return TRUE;                   /* E */
	case 6: *nx = x - 1; *ny = y; return TRUE;                   /* W */
	case 1: *nx = shifted ? x + 1 : x; *ny = y - 1; return TRUE; /* NE */
	case 7: *nx = shifted ? x : x - 1; *ny = y - 1; return TRUE; /* NW */
	case 3: *nx = shifted ? x + 1 : x; *ny = y + 1; return TRUE; /* SE */
	case 5: *nx = shifted ? x : x - 1; *ny = y + 1; return TRUE; /* SW */
	default: return FALSE;
	}
}

/* flat-top hexagons in columns: odd or even columns shifted half down */
static gboolean
staggered_column_neighbor (gboolean  odd_shifted,
                           gint      x,
                           gint      y,
                           gint      dir,
                           gint     *nx,
                           gint     *ny)
{
	gboolean shifted = ((x & 1) != 0) == odd_shifted;

	switch (dir)
	{
	case 0: *nx = x; *ny = y - 1; return TRUE;                       /* N */
	case 4: *nx = x; *ny = y + 1; return TRUE;                       /* S */
	case 1: *nx = x + 1; *ny = shifted ? y : y - 1; return TRUE;     /* NE */
	case 3: *nx = x + 1; *ny = shifted ? y + 1 : y; return TRUE;     /* SE */
	case 7: *nx = x - 1; *ny = shifted ? y : y - 1; return TRUE;     /* NW */
	case 5: *nx = x - 1; *ny = shifted ? y + 1 : y; return TRUE;     /* SW */
	default: return FALSE;                                           /* E, W */
	}
}

/*
 * A staggered isometric map is a diamond map drawn in rows.  To let one
 * rule tile serve both, logical (diamond) directions are mapped onto the
 * staggered screen directions: diamond N (x, y-1) is up and to the right
 * on screen, i.e. screen NE, and so on round the compass (a 45 degree
 * turn).
 */
static const gint staggered_logical_to_screen[8] = { 1, 2, 3, 4, 5, 6, 7, 0 };

gboolean
lrg_tile_neighbor_offset (LrgTileNeighborLayout  layout,
                          gint                   x,
                          gint                   y,
                          LrgTileNeighbor        direction,
                          gint                  *out_x,
                          gint                  *out_y)
{
	gint dir;
	gint nx = x;
	gint ny = y;
	gboolean ok = FALSE;

	dir = direction_index (direction);
	if (dir >= 0)
	{
		switch (layout)
		{
		case LRG_TILE_NEIGHBOR_LAYOUT_SQUARE:
			nx = x + square_dx[dir];
			ny = y + square_dy[dir];
			ok = TRUE;
			break;
		case LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD:
		case LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_EVEN:
			ok = staggered_row_neighbor (layout == LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD, x, y,
			                             staggered_logical_to_screen[dir], &nx, &ny);
			break;
		case LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD:
		case LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN:
			/* pointy-top rows have no N or S neighbour */
			if (dir != 0 && dir != 4)
				ok = staggered_row_neighbor (layout == LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD,
				                             x, y, dir, &nx, &ny);
			break;
		case LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD:
		case LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN:
			ok = staggered_column_neighbor (layout == LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD,
			                                x, y, dir, &nx, &ny);
			break;
		default:
			break;
		}
	}

	if (out_x != NULL)
		*out_x = ok ? nx : x;
	if (out_y != NULL)
		*out_y = ok ? ny : y;
	return ok;
}

LrgTileNeighbor
lrg_tile_neighbor_layout_get_mask (LrgTileNeighborLayout layout)
{
	switch (layout)
	{
	case LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD:
	case LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN:
		return (LrgTileNeighbor) (LRG_TILE_NEIGHBOR_ALL & ~(LRG_TILE_NEIGHBOR_N | LRG_TILE_NEIGHBOR_S));
	case LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD:
	case LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN:
		return (LrgTileNeighbor) (LRG_TILE_NEIGHBOR_ALL & ~(LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_W));
	default:
		return LRG_TILE_NEIGHBOR_ALL;
	}
}

static gboolean
layout_is_hex (LrgTileNeighborLayout layout)
{
	return layout >= LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD;
}

/* ==========================================================================
 * Mask transforms
 * ========================================================================== */

/* the pattern turned a quarter turn clockwise: direction i moves to i+2 */
static guint
mask_rotate (guint mask,
             guint quarter_turns)
{
	guint shift = (quarter_turns & 3u) * 2u;

	mask &= 0xFFu;
	if (shift == 0)
		return mask;
	return ((mask << shift) | (mask >> (8u - shift))) & 0xFFu;
}

/* remaps every bit i of @mask to bit map[i] */
static guint
mask_permute (guint        mask,
              const guint *map)
{
	guint out = 0;
	guint i;

	for (i = 0; i < 8; i++)
		if ((mask & (1u << i)) != 0)
			out |= 1u << map[i];
	return out;
}

/* left-right mirror: i -> (8 - i) % 8; top-bottom: i -> (12 - i) % 8 */
static const guint mirror_x_map[8] = { 0, 7, 6, 5, 4, 3, 2, 1 };
static const guint mirror_y_map[8] = { 4, 3, 2, 1, 0, 7, 6, 5 };

/* ==========================================================================
 * LrgTileRule
 * ========================================================================== */

typedef struct
{
	guint  tile;    /* cell value: 1-based id + transform bits */
	gfloat weight;
} RuleOutput;

struct _LrgTileRule
{
	gint                  ref_count;
	guint8                this_mask;
	guint8                not_mask;
	LrgTileRuleTransform  transform;
	GArray               *outputs;   /* RuleOutput */
	YamlMapping          *extra;     /* unknown keys of the loaded rule */
};

G_DEFINE_BOXED_TYPE (LrgTileRule, lrg_tile_rule, lrg_tile_rule_ref, lrg_tile_rule_unref)

LrgTileRule *
lrg_tile_rule_new (void)
{
	LrgTileRule *self = g_new0 (LrgTileRule, 1);

	self->ref_count = 1;
	self->transform = LRG_TILE_RULE_TRANSFORM_FIXED;
	self->outputs = g_array_new (FALSE, FALSE, sizeof (RuleOutput));
	return self;
}

LrgTileRule *
lrg_tile_rule_ref (LrgTileRule *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	g_atomic_int_inc (&self->ref_count);
	return self;
}

void
lrg_tile_rule_unref (LrgTileRule *self)
{
	g_return_if_fail (self != NULL);

	if (!g_atomic_int_dec_and_test (&self->ref_count))
		return;
	g_array_unref (self->outputs);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);
	g_free (self);
}

LrgTileRule *
lrg_tile_rule_copy (const LrgTileRule *self)
{
	LrgTileRule *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = lrg_tile_rule_new ();
	copy->this_mask = self->this_mask;
	copy->not_mask = self->not_mask;
	copy->transform = self->transform;
	g_array_append_vals (copy->outputs, self->outputs->data, self->outputs->len);
	if (self->extra != NULL)
		copy->extra = yaml_mapping_ref (self->extra);
	return copy;
}

void
lrg_tile_rule_set_neighbor (LrgTileRule      *self,
                            LrgTileNeighbor   direction,
                            LrgTileRuleMatch  match)
{
	guint8 bits;

	g_return_if_fail (self != NULL);

	bits = (guint8) ((guint) direction & 0xFFu);
	self->this_mask &= (guint8) ~bits;
	self->not_mask &= (guint8) ~bits;
	if (match == LRG_TILE_RULE_MATCH_THIS)
		self->this_mask |= bits;
	else if (match == LRG_TILE_RULE_MATCH_NOT_THIS)
		self->not_mask |= bits;
}

LrgTileRuleMatch
lrg_tile_rule_get_neighbor (const LrgTileRule *self,
                            LrgTileNeighbor    direction)
{
	g_return_val_if_fail (self != NULL, LRG_TILE_RULE_MATCH_ANY);

	if ((self->this_mask & (guint) direction) != 0)
		return LRG_TILE_RULE_MATCH_THIS;
	if ((self->not_mask & (guint) direction) != 0)
		return LRG_TILE_RULE_MATCH_NOT_THIS;
	return LRG_TILE_RULE_MATCH_ANY;
}

LrgTileNeighbor
lrg_tile_rule_get_this_mask (const LrgTileRule *self)
{
	g_return_val_if_fail (self != NULL, LRG_TILE_NEIGHBOR_NONE);
	return (LrgTileNeighbor) self->this_mask;
}

LrgTileNeighbor
lrg_tile_rule_get_not_this_mask (const LrgTileRule *self)
{
	g_return_val_if_fail (self != NULL, LRG_TILE_NEIGHBOR_NONE);
	return (LrgTileNeighbor) self->not_mask;
}

void
lrg_tile_rule_set_transform (LrgTileRule          *self,
                             LrgTileRuleTransform  transform)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (transform <= LRG_TILE_RULE_TRANSFORM_MIRROR_XY);
	self->transform = transform;
}

LrgTileRuleTransform
lrg_tile_rule_get_transform (const LrgTileRule *self)
{
	g_return_val_if_fail (self != NULL, LRG_TILE_RULE_TRANSFORM_FIXED);
	return self->transform;
}

void
lrg_tile_rule_add_output (LrgTileRule *self,
                          guint        tile,
                          gfloat       weight)
{
	RuleOutput out;

	g_return_if_fail (self != NULL);
	g_return_if_fail (lrg_tile_get_id (tile) != LRG_TILEMAP_EMPTY_TILE);
	g_return_if_fail (weight > 0.0f);

	out.tile = tile;
	out.weight = weight;
	g_array_append_val (self->outputs, out);
}

guint
lrg_tile_rule_get_n_outputs (const LrgTileRule *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->outputs->len;
}

guint
lrg_tile_rule_get_output (const LrgTileRule *self,
                          guint              index,
                          gfloat            *out_weight)
{
	RuleOutput *out;

	g_return_val_if_fail (self != NULL, 0);

	if (index >= self->outputs->len)
	{
		if (out_weight != NULL)
			*out_weight = 0.0f;
		return 0;
	}
	out = &g_array_index (self->outputs, RuleOutput, index);
	if (out_weight != NULL)
		*out_weight = out->weight;
	return out->tile;
}

gboolean
lrg_tile_rule_remove_output (LrgTileRule *self,
                             guint        index)
{
	g_return_val_if_fail (self != NULL, FALSE);

	if (index >= self->outputs->len)
		return FALSE;
	g_array_remove_index (self->outputs, index);
	return TRUE;
}

void
lrg_tile_rule_clear_outputs (LrgTileRule *self)
{
	g_return_if_fail (self != NULL);
	g_array_set_size (self->outputs, 0);
}

/* tests one (transformed) pattern */
static gboolean
pattern_matches (guint this_mask,
                 guint not_mask,
                 guint mask,
                 guint valid)
{
	this_mask &= valid;
	not_mask &= valid;
	return (mask & this_mask) == this_mask && (mask & not_mask) == 0;
}

/*
 * match_with_layout:
 * Tries the pattern and the variants its transform allows, in order:
 * as written; rotated 90/180/270 (not on hexagonal layouts, where a
 * quarter turn is not a symmetry); mirrored X; mirrored Y; mirrored both.
 * On a match reports the transform for the output tile in the canonical
 * (mirror first, then rotate clockwise) form.
 */
static gboolean
match_with_layout (const LrgTileRule *self,
                   guint              mask,
                   guint              valid,
                   gboolean           allow_rotation,
                   guint             *out_rotation,
                   gboolean          *out_mirrored)
{
	guint rotation = 0;
	gboolean mirrored = FALSE;
	gboolean found = FALSE;

	mask &= 0xFFu;

	if (pattern_matches (self->this_mask, self->not_mask, mask, valid))
		found = TRUE;

	if (!found && self->transform == LRG_TILE_RULE_TRANSFORM_ROTATED && allow_rotation)
	{
		guint r;

		for (r = 1; r < 4 && !found; r++)
		{
			if (pattern_matches (mask_rotate (self->this_mask, r), mask_rotate (self->not_mask, r),
			                     mask, valid))
			{
				found = TRUE;
				rotation = r;
			}
		}
	}

	if (!found && (self->transform == LRG_TILE_RULE_TRANSFORM_MIRROR_X ||
	               self->transform == LRG_TILE_RULE_TRANSFORM_MIRROR_XY))
	{
		if (pattern_matches (mask_permute (self->this_mask, mirror_x_map),
		                     mask_permute (self->not_mask, mirror_x_map), mask, valid))
		{
			found = TRUE;
			mirrored = TRUE;     /* M */
		}
	}

	if (!found && (self->transform == LRG_TILE_RULE_TRANSFORM_MIRROR_Y ||
	               self->transform == LRG_TILE_RULE_TRANSFORM_MIRROR_XY))
	{
		if (pattern_matches (mask_permute (self->this_mask, mirror_y_map),
		                     mask_permute (self->not_mask, mirror_y_map), mask, valid))
		{
			found = TRUE;
			mirrored = TRUE;     /* a vertical mirror is R^2 M */
			rotation = 2;
		}
	}

	if (!found && self->transform == LRG_TILE_RULE_TRANSFORM_MIRROR_XY)
	{
		guint t = mask_permute (mask_permute (self->this_mask, mirror_x_map), mirror_y_map);
		guint n = mask_permute (mask_permute (self->not_mask, mirror_x_map), mirror_y_map);

		if (pattern_matches (t, n, mask, valid))
		{
			found = TRUE;
			rotation = 2;        /* both mirrors are a half turn */
		}
	}

	if (out_rotation != NULL)
		*out_rotation = found ? rotation : 0;
	if (out_mirrored != NULL)
		*out_mirrored = found ? mirrored : FALSE;
	return found;
}

gboolean
lrg_tile_rule_match (const LrgTileRule *self,
                     LrgTileNeighbor    mask,
                     LrgTileNeighbor    valid,
                     guint             *out_rotation,
                     gboolean          *out_mirrored)
{
	g_return_val_if_fail (self != NULL, FALSE);
	return match_with_layout (self, (guint) mask, (guint) valid, TRUE, out_rotation, out_mirrored);
}

/* applies a canonical (mirror, rotation) transform on top of a cell value */
static guint
transform_tile (guint    tile,
                gboolean mirrored,
                guint    rotation)
{
	if (mirrored)
		tile = lrg_tile_flip_horizontally (tile);
	if (rotation != 0)
		tile = lrg_tile_rotate (tile, (gint) rotation);
	return tile;
}

/* ==========================================================================
 * LrgRuleTile
 * ========================================================================== */

struct _LrgRuleTile
{
	GObject      parent_instance;

	gchar       *name;
	guint        default_tile;
	guint        seed;
	gboolean     border_is_this;
	GArray      *match_tiles;   /* guint tile ids */
	GPtrArray   *rules;         /* LrgTileRule* */
	YamlMapping *extra;         /* unknown keys of the loaded rule tile */
};

G_DEFINE_FINAL_TYPE (LrgRuleTile, lrg_rule_tile, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_DEFAULT_TILE,
	PROP_SEED,
	PROP_BORDER_IS_THIS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_rule_tile_finalize (GObject *object)
{
	LrgRuleTile *self = LRG_RULE_TILE (object);

	g_free (self->name);
	g_array_unref (self->match_tiles);
	g_ptr_array_unref (self->rules);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);

	G_OBJECT_CLASS (lrg_rule_tile_parent_class)->finalize (object);
}

static void
lrg_rule_tile_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgRuleTile *self = LRG_RULE_TILE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_DEFAULT_TILE:
		g_value_set_uint (value, self->default_tile);
		break;
	case PROP_SEED:
		g_value_set_uint (value, self->seed);
		break;
	case PROP_BORDER_IS_THIS:
		g_value_set_boolean (value, self->border_is_this);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_rule_tile_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgRuleTile *self = LRG_RULE_TILE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		lrg_rule_tile_set_name (self, g_value_get_string (value));
		break;
	case PROP_DEFAULT_TILE:
		lrg_rule_tile_set_default_tile (self, g_value_get_uint (value));
		break;
	case PROP_SEED:
		lrg_rule_tile_set_seed (self, g_value_get_uint (value));
		break;
	case PROP_BORDER_IS_THIS:
		lrg_rule_tile_set_border_is_this (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_rule_tile_class_init (LrgRuleTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_rule_tile_finalize;
	object_class->get_property = lrg_rule_tile_get_property;
	object_class->set_property = lrg_rule_tile_set_property;

	/**
	 * LrgRuleTile:name:
	 *
	 * The rule tile's name, unique within its tileset (the brush name the
	 * editor shows and lrg_tileset_get_rule_tile_by_name() looks up).
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRuleTile:default-tile:
	 *
	 * The cell value used when no rule matches; 0 uses the first output
	 * of the first rule.
	 */
	properties[PROP_DEFAULT_TILE] =
		g_param_spec_uint ("default-tile", NULL, NULL, 0, G_MAXUINT, 0,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRuleTile:seed:
	 *
	 * Seed of the per-cell weighted variant choice.  Changing it reshuffles
	 * the variants; keeping it keeps every cell's variant across repaints.
	 */
	properties[PROP_SEED] =
		g_param_spec_uint ("seed", NULL, NULL, 0, G_MAXUINT, 0,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRuleTile:border-is-this:
	 *
	 * Whether cells outside the layer count as belonging to the rule tile,
	 * so terrain reaching the map edge looks like it continues.
	 */
	properties[PROP_BORDER_IS_THIS] =
		g_param_spec_boolean ("border-is-this", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgRuleTile::changed:
	 * @self: the rule tile
	 *
	 * Emitted when the rules, the match tiles or a property that changes
	 * the result of lrg_rule_tile_resolve() change, so editors and
	 * previews can re-apply the rule tile.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_rule_tile_init (LrgRuleTile *self)
{
	self->match_tiles = g_array_new (FALSE, FALSE, sizeof (guint));
	self->rules = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_tile_rule_unref);
}

LrgRuleTile *
lrg_rule_tile_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_RULE_TILE, "name", name, NULL);
}

/* ==========================================================================
 * Presets
 * ========================================================================== */

/* reduces a mask to blob form: a corner only counts with both its edges */
static guint
blob_reduce (guint mask)
{
	static const guint corner[4] = { LRG_TILE_NEIGHBOR_NE, LRG_TILE_NEIGHBOR_SE,
	                                 LRG_TILE_NEIGHBOR_SW, LRG_TILE_NEIGHBOR_NW };
	static const guint edge_a[4] = { LRG_TILE_NEIGHBOR_N, LRG_TILE_NEIGHBOR_S,
	                                 LRG_TILE_NEIGHBOR_S, LRG_TILE_NEIGHBOR_N };
	static const guint edge_b[4] = { LRG_TILE_NEIGHBOR_E, LRG_TILE_NEIGHBOR_E,
	                                 LRG_TILE_NEIGHBOR_W, LRG_TILE_NEIGHBOR_W };
	guint i;

	mask &= 0xFFu;
	for (i = 0; i < 4; i++)
		if ((mask & edge_a[i]) == 0 || (mask & edge_b[i]) == 0)
			mask &= ~corner[i];
	return mask;
}

/* the 47 blob masks in ascending order, built once */
static const guint8 *
blob47_table (void)
{
	static guint8 table[LRG_RULE_TILE_BLOB47_COUNT];
	static gsize init = 0;

	if (g_once_init_enter (&init))
	{
		guint n = 0;
		guint m;

		for (m = 0; m < 256; m++)
			if (blob_reduce (m) == m)
				table[n++] = (guint8) m;
		g_assert (n == LRG_RULE_TILE_BLOB47_COUNT);
		g_once_init_leave (&init, 1);
	}
	return table;
}

LrgTileNeighbor
lrg_rule_tile_blob47_mask (guint rank)
{
	if (rank >= LRG_RULE_TILE_BLOB47_COUNT)
		return LRG_TILE_NEIGHBOR_NONE;
	return (LrgTileNeighbor) blob47_table ()[rank];
}

guint
lrg_rule_tile_blob47_rank (LrgTileNeighbor mask)
{
	const guint8 *table = blob47_table ();
	guint reduced = blob_reduce ((guint) mask);
	guint i;

	for (i = 0; i < LRG_RULE_TILE_BLOB47_COUNT; i++)
		if (table[i] == reduced)
			return i;
	return 0;
}

LrgRuleTile *
lrg_rule_tile_new_edges16 (const gchar *name,
                           guint        first_tile)
{
	static const guint edges[4] = { LRG_TILE_NEIGHBOR_N, LRG_TILE_NEIGHBOR_E,
	                                LRG_TILE_NEIGHBOR_S, LRG_TILE_NEIGHBOR_W };
	LrgRuleTile *self;
	guint m;

	g_return_val_if_fail (lrg_tile_get_id (first_tile) != LRG_TILEMAP_EMPTY_TILE, NULL);

	self = lrg_rule_tile_new (name);
	for (m = 0; m < LRG_RULE_TILE_EDGES16_COUNT; m++)
	{
		g_autoptr(LrgTileRule) rule = lrg_tile_rule_new ();
		guint e;

		/* each edge is fully specified; corners don't matter */
		for (e = 0; e < 4; e++)
			lrg_tile_rule_set_neighbor (rule, (LrgTileNeighbor) edges[e],
			                            (m & (1u << e)) != 0 ? LRG_TILE_RULE_MATCH_THIS
			                                                 : LRG_TILE_RULE_MATCH_NOT_THIS);
		lrg_tile_rule_add_output (rule, lrg_tile_get_id (first_tile) + m, 1.0f);
		g_ptr_array_add (self->rules, lrg_tile_rule_ref (rule));
	}
	self->default_tile = lrg_tile_get_id (first_tile);
	return self;
}

LrgRuleTile *
lrg_rule_tile_new_blob47 (const gchar *name,
                          guint        first_tile)
{
	static const guint corner[4] = { LRG_TILE_NEIGHBOR_NE, LRG_TILE_NEIGHBOR_SE,
	                                 LRG_TILE_NEIGHBOR_SW, LRG_TILE_NEIGHBOR_NW };
	static const guint edge_a[4] = { LRG_TILE_NEIGHBOR_N, LRG_TILE_NEIGHBOR_S,
	                                 LRG_TILE_NEIGHBOR_S, LRG_TILE_NEIGHBOR_N };
	static const guint edge_b[4] = { LRG_TILE_NEIGHBOR_E, LRG_TILE_NEIGHBOR_E,
	                                 LRG_TILE_NEIGHBOR_W, LRG_TILE_NEIGHBOR_W };
	static const guint edges[4] = { LRG_TILE_NEIGHBOR_N, LRG_TILE_NEIGHBOR_E,
	                                LRG_TILE_NEIGHBOR_S, LRG_TILE_NEIGHBOR_W };
	const guint8 *table = blob47_table ();
	LrgRuleTile *self;
	guint rank;

	g_return_val_if_fail (lrg_tile_get_id (first_tile) != LRG_TILEMAP_EMPTY_TILE, NULL);

	self = lrg_rule_tile_new (name);
	for (rank = 0; rank < LRG_RULE_TILE_BLOB47_COUNT; rank++)
	{
		g_autoptr(LrgTileRule) rule = lrg_tile_rule_new ();
		guint mask = table[rank];
		guint i;

		/* edges always matter */
		for (i = 0; i < 4; i++)
			lrg_tile_rule_set_neighbor (rule, (LrgTileNeighbor) edges[i],
			                            (mask & edges[i]) != 0 ? LRG_TILE_RULE_MATCH_THIS
			                                                   : LRG_TILE_RULE_MATCH_NOT_THIS);
		/* a corner matters only when both of its edges are set */
		for (i = 0; i < 4; i++)
			if ((mask & edge_a[i]) != 0 && (mask & edge_b[i]) != 0)
				lrg_tile_rule_set_neighbor (rule, (LrgTileNeighbor) corner[i],
				                            (mask & corner[i]) != 0 ? LRG_TILE_RULE_MATCH_THIS
				                                                    : LRG_TILE_RULE_MATCH_NOT_THIS);
		lrg_tile_rule_add_output (rule, lrg_tile_get_id (first_tile) + rank, 1.0f);
		g_ptr_array_add (self->rules, lrg_tile_rule_ref (rule));
	}
	self->default_tile = lrg_tile_get_id (first_tile);
	return self;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

const gchar *
lrg_rule_tile_get_name (LrgRuleTile *self)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), NULL);
	return self->name;
}

void
lrg_rule_tile_set_name (LrgRuleTile *self,
                        const gchar *name)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));

	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

guint
lrg_rule_tile_get_default_tile (LrgRuleTile *self)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), 0);
	return self->default_tile;
}

void
lrg_rule_tile_set_default_tile (LrgRuleTile *self,
                                guint        tile)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));

	if (self->default_tile == tile)
		return;
	self->default_tile = tile;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_TILE]);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

guint
lrg_rule_tile_get_seed (LrgRuleTile *self)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), 0);
	return self->seed;
}

void
lrg_rule_tile_set_seed (LrgRuleTile *self,
                        guint        seed)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));

	if (self->seed == seed)
		return;
	self->seed = seed;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEED]);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gboolean
lrg_rule_tile_get_border_is_this (LrgRuleTile *self)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), FALSE);
	return self->border_is_this;
}

void
lrg_rule_tile_set_border_is_this (LrgRuleTile *self,
                                  gboolean     border_is_this)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));

	border_is_this = !!border_is_this;
	if (self->border_is_this == border_is_this)
		return;
	self->border_is_this = border_is_this;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_IS_THIS]);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_rule_tile_add_match_tile (LrgRuleTile *self,
                              guint        tile_id)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));

	tile_id = lrg_tile_get_id (tile_id);
	g_return_if_fail (tile_id != LRG_TILEMAP_EMPTY_TILE);
	g_array_append_val (self->match_tiles, tile_id);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

const guint *
lrg_rule_tile_get_match_tiles (LrgRuleTile *self,
                               guint       *n_tiles)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), NULL);

	if (n_tiles != NULL)
		*n_tiles = self->match_tiles->len;
	return (const guint *) self->match_tiles->data;
}

void
lrg_rule_tile_clear_match_tiles (LrgRuleTile *self)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));

	if (self->match_tiles->len == 0)
		return;
	g_array_set_size (self->match_tiles, 0);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * Rules
 * ========================================================================== */

void
lrg_rule_tile_add_rule (LrgRuleTile *self,
                        LrgTileRule *rule)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));
	g_return_if_fail (rule != NULL);

	g_ptr_array_add (self->rules, lrg_tile_rule_ref (rule));
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_rule_tile_insert_rule (LrgRuleTile *self,
                           guint        index,
                           LrgTileRule *rule)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));
	g_return_if_fail (rule != NULL);

	index = MIN (index, self->rules->len);
	g_ptr_array_insert (self->rules, (gint) index, lrg_tile_rule_ref (rule));
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gboolean
lrg_rule_tile_remove_rule (LrgRuleTile *self,
                           guint        index)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), FALSE);

	if (index >= self->rules->len)
		return FALSE;
	g_ptr_array_remove_index (self->rules, index);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
	return TRUE;
}

gboolean
lrg_rule_tile_move_rule (LrgRuleTile *self,
                         guint        from,
                         guint        to)
{
	LrgTileRule *rule;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), FALSE);

	if (from >= self->rules->len || to >= self->rules->len)
		return FALSE;
	if (from == to)
		return TRUE;

	/* steal the slot's reference and put it back elsewhere */
	rule = g_ptr_array_steal_index (self->rules, from);
	g_ptr_array_insert (self->rules, (gint) to, rule);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
	return TRUE;
}

LrgTileRule *
lrg_rule_tile_get_rule (LrgRuleTile *self,
                        guint        index)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), NULL);

	if (index >= self->rules->len)
		return NULL;
	return g_ptr_array_index (self->rules, index);
}

guint
lrg_rule_tile_get_n_rules (LrgRuleTile *self)
{
	g_return_val_if_fail (LRG_IS_RULE_TILE (self), 0);
	return self->rules->len;
}

void
lrg_rule_tile_clear_rules (LrgRuleTile *self)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));

	if (self->rules->len == 0)
		return;
	g_ptr_array_set_size (self->rules, 0);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_rule_tile_changed (LrgRuleTile *self)
{
	g_return_if_fail (LRG_IS_RULE_TILE (self));
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * Ownership and resolution
 * ========================================================================== */

/* the default cell value, falling back to the first rule's first output */
static guint
effective_default (LrgRuleTile *self)
{
	guint i;

	if (self->default_tile != 0)
		return self->default_tile;
	for (i = 0; i < self->rules->len; i++)
	{
		LrgTileRule *rule = g_ptr_array_index (self->rules, i);

		if (rule->outputs->len > 0)
			return g_array_index (rule->outputs, RuleOutput, 0).tile;
	}
	return 0;
}

gboolean
lrg_rule_tile_owns_tile (LrgRuleTile *self,
                         guint        tile)
{
	guint id;
	guint i;
	guint k;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), FALSE);

	id = lrg_tile_get_id (tile);
	if (id == LRG_TILEMAP_EMPTY_TILE)
		return FALSE;
	if (lrg_tile_get_id (self->default_tile) == id)
		return TRUE;
	for (i = 0; i < self->rules->len; i++)
	{
		LrgTileRule *rule = g_ptr_array_index (self->rules, i);

		for (k = 0; k < rule->outputs->len; k++)
			if (lrg_tile_get_id (g_array_index (rule->outputs, RuleOutput, k).tile) == id)
				return TRUE;
	}
	return FALSE;
}

gboolean
lrg_rule_tile_matches_tile (LrgRuleTile *self,
                            guint        tile)
{
	guint id;
	guint i;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), FALSE);

	id = lrg_tile_get_id (tile);
	if (id == LRG_TILEMAP_EMPTY_TILE)
		return FALSE;
	for (i = 0; i < self->match_tiles->len; i++)
		if (g_array_index (self->match_tiles, guint, i) == id)
			return TRUE;
	return lrg_rule_tile_owns_tile (self, tile);
}

/* a well-mixed 32-bit hash of a cell, the seed and the rule (murmur3 fmix) */
static guint32
cell_hash (gint    x,
           gint    y,
           guint   seed,
           guint   rule)
{
	guint32 h = (guint32) seed ^ 0x9E3779B9u;

	h ^= (guint32) x * 0x85EBCA6Bu;
	h = (h << 13) | (h >> 19);
	h ^= (guint32) y * 0xC2B2AE35u;
	h = (h << 17) | (h >> 15);
	h ^= (guint32) rule * 0x27D4EB2Fu;
	h ^= h >> 16;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h;
}

/* picks a weighted output of @rule for a cell */
static guint
pick_output (LrgRuleTile *self,
             LrgTileRule *rule,
             guint        rule_index,
             gint         x,
             gint         y)
{
	gdouble total = 0.0;
	gdouble pick;
	guint i;

	if (rule->outputs->len == 1)
		return g_array_index (rule->outputs, RuleOutput, 0).tile;

	for (i = 0; i < rule->outputs->len; i++)
		total += (gdouble) g_array_index (rule->outputs, RuleOutput, i).weight;

	/* 24 bits of the hash as a fraction of the total weight */
	pick = ((gdouble) (cell_hash (x, y, self->seed, rule_index) >> 8) / 16777216.0) * total;
	for (i = 0; i < rule->outputs->len; i++)
	{
		gdouble w = (gdouble) g_array_index (rule->outputs, RuleOutput, i).weight;

		if (pick < w)
			return g_array_index (rule->outputs, RuleOutput, i).tile;
		pick -= w;
	}
	return g_array_index (rule->outputs, RuleOutput, rule->outputs->len - 1).tile;
}

guint
lrg_rule_tile_resolve (LrgRuleTile           *self,
                       LrgTileNeighbor        mask,
                       LrgTileNeighborLayout  layout,
                       gint                   x,
                       gint                   y,
                       gint                  *out_rule)
{
	guint valid;
	guint i;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), 0);

	valid = (guint) lrg_tile_neighbor_layout_get_mask (layout);
	for (i = 0; i < self->rules->len; i++)
	{
		LrgTileRule *rule = g_ptr_array_index (self->rules, i);
		guint rotation;
		gboolean mirrored;

		if (rule->outputs->len == 0)
			continue;
		if (!match_with_layout (rule, (guint) mask, valid, !layout_is_hex (layout),
		                        &rotation, &mirrored))
			continue;
		if (out_rule != NULL)
			*out_rule = (gint) i;
		return transform_tile (pick_output (self, rule, i, x, y), mirrored, rotation);
	}

	if (out_rule != NULL)
		*out_rule = -1;
	return effective_default (self);
}

/* ==========================================================================
 * Applying to tile arrays
 * ========================================================================== */

/*
 * TileView:
 * The layer as the masks see it: the live array, except inside the
 * rectangle being rewritten, where a snapshot taken before the rewrite is
 * read so the result does not depend on the scan order.  @owned and
 * @matches are hash sets of tile ids built once per call.
 */
typedef struct
{
	const guint *tiles;
	guint        width;
	guint        height;
	const guint *snapshot;
	gint         snap_x;
	gint         snap_y;
	guint        snap_w;
	guint        snap_h;
	GHashTable  *owned;
	GHashTable  *matches;
	gboolean     border_is_this;
} TileView;

static void
view_build_sets (LrgRuleTile *self,
                 TileView    *view)
{
	guint i;
	guint k;
	guint id;

	view->owned = g_hash_table_new (g_direct_hash, g_direct_equal);
	view->matches = g_hash_table_new (g_direct_hash, g_direct_equal);

	id = lrg_tile_get_id (effective_default (self));
	if (id != 0)
		g_hash_table_add (view->owned, GUINT_TO_POINTER (id));
	for (i = 0; i < self->rules->len; i++)
	{
		LrgTileRule *rule = g_ptr_array_index (self->rules, i);

		for (k = 0; k < rule->outputs->len; k++)
		{
			id = lrg_tile_get_id (g_array_index (rule->outputs, RuleOutput, k).tile);
			g_hash_table_add (view->owned, GUINT_TO_POINTER (id));
		}
	}
	for (i = 0; i < self->match_tiles->len; i++)
		g_hash_table_add (view->matches, GUINT_TO_POINTER (g_array_index (self->match_tiles, guint, i)));
	view->border_is_this = self->border_is_this;
}

static void
view_clear (TileView *view)
{
	g_clear_pointer (&view->owned, g_hash_table_unref);
	g_clear_pointer (&view->matches, g_hash_table_unref);
}

/* reads a cell; out of bounds reports FALSE */
static gboolean
view_get (const TileView *view,
          gint            x,
          gint            y,
          guint          *out)
{
	if (x < 0 || y < 0 || (guint) x >= view->width || (guint) y >= view->height)
		return FALSE;
	if (view->snapshot != NULL &&
	    x >= view->snap_x && y >= view->snap_y &&
	    (guint) (x - view->snap_x) < view->snap_w && (guint) (y - view->snap_y) < view->snap_h)
		*out = view->snapshot[(gsize) (y - view->snap_y) * view->snap_w + (gsize) (x - view->snap_x)];
	else
		*out = view->tiles[(gsize) y * view->width + (gsize) x];
	return TRUE;
}

static gboolean
view_is_this (const TileView *view,
              gint            x,
              gint            y)
{
	guint tile;
	gpointer key;

	if (!view_get (view, x, y, &tile))
		return view->border_is_this;
	key = GUINT_TO_POINTER (lrg_tile_get_id (tile));
	if (lrg_tile_get_id (tile) == LRG_TILEMAP_EMPTY_TILE)
		return FALSE;
	return g_hash_table_contains (view->owned, key) || g_hash_table_contains (view->matches, key);
}

static guint
view_mask (const TileView        *view,
           LrgTileNeighborLayout  layout,
           gint                   x,
           gint                   y)
{
	guint mask = 0;
	guint i;

	for (i = 0; i < 8; i++)
	{
		gint nx;
		gint ny;

		if (!lrg_tile_neighbor_offset (layout, x, y, (LrgTileNeighbor) (1u << i), &nx, &ny))
			continue;
		if (view_is_this (view, nx, ny))
			mask |= 1u << i;
	}
	return mask;
}

LrgTileNeighbor
lrg_rule_tile_compute_mask (LrgRuleTile           *self,
                            const guint           *tiles,
                            gsize                  n_tiles,
                            guint                  width,
                            LrgTileNeighborLayout  layout,
                            gint                   x,
                            gint                   y)
{
	TileView view = { 0 };
	guint mask;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), LRG_TILE_NEIGHBOR_NONE);
	g_return_val_if_fail (tiles != NULL || n_tiles == 0, LRG_TILE_NEIGHBOR_NONE);
	g_return_val_if_fail (width > 0 && n_tiles % width == 0, LRG_TILE_NEIGHBOR_NONE);

	view.tiles = tiles;
	view.width = width;
	view.height = (guint) (n_tiles / width);
	view_build_sets (self, &view);
	mask = view_mask (&view, layout, x, y);
	view_clear (&view);
	return (LrgTileNeighbor) mask;
}

/* clamps a rectangle to the layer; FALSE when nothing is left */
static gboolean
clamp_rect (guint  width,
            guint  height,
            gint  *x,
            gint  *y,
            guint *w,
            guint *h)
{
	gint64 x0 = MAX (*x, 0);
	gint64 y0 = MAX (*y, 0);
	gint64 x1 = MIN ((gint64) *x + (gint64) *w, (gint64) width);
	gint64 y1 = MIN ((gint64) *y + (gint64) *h, (gint64) height);

	if (x1 <= x0 || y1 <= y0)
		return FALSE;
	*x = (gint) x0;
	*y = (gint) y0;
	*w = (guint) (x1 - x0);
	*h = (guint) (y1 - y0);
	return TRUE;
}

guint
lrg_rule_tile_apply (LrgRuleTile           *self,
                     guint                 *tiles,
                     gsize                  n_tiles,
                     guint                  width,
                     LrgTileNeighborLayout  layout,
                     gint                   x,
                     gint                   y,
                     guint                  rect_width,
                     guint                  rect_height)
{
	g_autofree guint *snapshot = NULL;
	TileView view = { 0 };
	guint changed = 0;
	guint height;
	guint row;
	guint col;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), 0);
	g_return_val_if_fail (tiles != NULL || n_tiles == 0, 0);
	g_return_val_if_fail (width > 0 && n_tiles % width == 0, 0);

	height = (guint) (n_tiles / width);
	if (!clamp_rect (width, height, &x, &y, &rect_width, &rect_height))
		return 0;

	/* snapshot the rectangle: masks read the cells as they were */
	snapshot = g_new (guint, (gsize) rect_width * rect_height);
	for (row = 0; row < rect_height; row++)
		memcpy (snapshot + (gsize) row * rect_width,
		        tiles + ((gsize) (y + (gint) row) * width + (gsize) x),
		        rect_width * sizeof (guint));

	view.tiles = tiles;
	view.width = width;
	view.height = height;
	view.snapshot = snapshot;
	view.snap_x = x;
	view.snap_y = y;
	view.snap_w = rect_width;
	view.snap_h = rect_height;
	view_build_sets (self, &view);

	for (row = 0; row < rect_height; row++)
	{
		for (col = 0; col < rect_width; col++)
		{
			gint cx = x + (gint) col;
			gint cy = y + (gint) row;
			guint old = snapshot[(gsize) row * rect_width + col];
			guint value;

			if (lrg_tile_get_id (old) == LRG_TILEMAP_EMPTY_TILE ||
			    !g_hash_table_contains (view.owned, GUINT_TO_POINTER (lrg_tile_get_id (old))))
				continue;

			value = lrg_rule_tile_resolve (self, (LrgTileNeighbor) view_mask (&view, layout, cx, cy),
			                               layout, cx, cy, NULL);
			if (value == 0 || value == old)
				continue;
			tiles[(gsize) cy * width + (gsize) cx] = value;
			changed++;
		}
	}

	view_clear (&view);
	return changed;
}

/* counts the cells of the 5x5 block around (x, y) that differ from @before */
static guint
count_block_changes (const guint *tiles,
                     guint        width,
                     guint        height,
                     gint         x,
                     gint         y,
                     const guint *before)
{
	guint changed = 0;
	gint dy;
	gint dx;

	for (dy = -2; dy <= 2; dy++)
	{
		for (dx = -2; dx <= 2; dx++)
		{
			gint cx = x + dx;
			gint cy = y + dy;

			if (cx < 0 || cy < 0 || (guint) cx >= width || (guint) cy >= height)
				continue;
			if (tiles[(gsize) cy * width + (gsize) cx] != before[(dy + 2) * 5 + (dx + 2)])
				changed++;
		}
	}
	return changed;
}

/* copies the 5x5 block around (x, y); cells outside the layer read 0 */
static void
copy_block (const guint *tiles,
            guint        width,
            guint        height,
            gint         x,
            gint         y,
            guint       *out)
{
	gint dy;
	gint dx;

	for (dy = -2; dy <= 2; dy++)
	{
		for (dx = -2; dx <= 2; dx++)
		{
			gint cx = x + dx;
			gint cy = y + dy;

			out[(dy + 2) * 5 + (dx + 2)] =
				(cx < 0 || cy < 0 || (guint) cx >= width || (guint) cy >= height)
				? 0 : tiles[(gsize) cy * width + (gsize) cx];
		}
	}
}

guint
lrg_rule_tile_paint (LrgRuleTile           *self,
                     guint                 *tiles,
                     gsize                  n_tiles,
                     guint                  width,
                     LrgTileNeighborLayout  layout,
                     gint                   x,
                     gint                   y)
{
	guint before[25];
	guint height;
	guint seed_tile;
	gsize index;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), 0);
	g_return_val_if_fail (tiles != NULL || n_tiles == 0, 0);
	g_return_val_if_fail (width > 0 && n_tiles % width == 0, 0);

	height = (guint) (n_tiles / width);
	if (x < 0 || y < 0 || (guint) x >= width || (guint) y >= height)
		return 0;

	seed_tile = effective_default (self);
	if (seed_tile == 0)
		return 0;

	/* mark the cell as ours (any owned tile will do), then resolve the
	 * 5x5 block around it: staggered layouts reach two rows away */
	copy_block (tiles, width, height, x, y, before);
	index = (gsize) y * width + (gsize) x;
	if (!lrg_rule_tile_owns_tile (self, tiles[index]))
		tiles[index] = seed_tile;
	lrg_rule_tile_apply (self, tiles, n_tiles, width, layout, x - 2, y - 2, 5, 5);
	return count_block_changes (tiles, width, height, x, y, before);
}

guint
lrg_rule_tile_erase (LrgRuleTile           *self,
                     guint                 *tiles,
                     gsize                  n_tiles,
                     guint                  width,
                     LrgTileNeighborLayout  layout,
                     gint                   x,
                     gint                   y)
{
	guint before[25];
	guint height;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), 0);
	g_return_val_if_fail (tiles != NULL || n_tiles == 0, 0);
	g_return_val_if_fail (width > 0 && n_tiles % width == 0, 0);

	height = (guint) (n_tiles / width);
	if (x < 0 || y < 0 || (guint) x >= width || (guint) y >= height)
		return 0;

	copy_block (tiles, width, height, x, y, before);
	tiles[(gsize) y * width + (gsize) x] = LRG_TILEMAP_EMPTY_TILE;
	lrg_rule_tile_apply (self, tiles, n_tiles, width, layout, x - 2, y - 2, 5, 5);
	return count_block_changes (tiles, width, height, x, y, before);
}

/* ==========================================================================
 * .rtileset: rule tiles
 *
 * - name: grass                 # required, unique in the tileset
 *   default: 12                 # tile index or {tile, rotate, flip}
 *   seed: 7
 *   border_is_this: true
 *   match_tiles: [3, 4]         # tile indices
 *   rules:
 *     - pattern: ["#.#", "-@-", "..."]   # rows N..S; # this, - not, . any
 *       transform: rotated
 *       tiles: [5, {tile: 6, weight: 0.5, rotate: 1, flip: true}]
 *
 * Tile references in the file are 0-based tile indices (like the rest of
 * the .rtileset); in memory they are 1-based cell values.
 * ========================================================================== */

static gboolean
rd_fail (GError      **error,
         const gchar  *source,
         const gchar  *path,
         const gchar  *message)
{
	g_set_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_INVALID,
	             "%s: %s: %s", source, path, message);
	return FALSE;
}

static const gchar *
rd_scalar (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
rd_uint (YamlNode     *node,
         const gchar  *source,
         const gchar  *path,
         guint64       max,
         guint        *out,
         GError      **error)
{
	const gchar *text = rd_scalar (node);
	guint64 value;
	gchar *end = NULL;

	if (text == NULL || text[0] == '\0' || text[0] == '-' || text[0] == '+')
		return rd_fail (error, source, path, "expected a whole number");
	value = g_ascii_strtoull (text, &end, 10);
	if (end == NULL || *end != '\0')
		return rd_fail (error, source, path, "expected a whole number");
	if (value > max)
	{
		g_autofree gchar *msg = g_strdup_printf ("expected a whole number from 0 to %" G_GUINT64_FORMAT, max);

		return rd_fail (error, source, path, msg);
	}
	*out = (guint) value;
	return TRUE;
}

static gboolean
rd_float (YamlNode     *node,
          const gchar  *source,
          const gchar  *path,
          gfloat       *out,
          GError      **error)
{
	const gchar *text = rd_scalar (node);
	gdouble value;
	gchar *end = NULL;

	if (text == NULL || text[0] == '\0')
		return rd_fail (error, source, path, "expected a number");
	value = g_ascii_strtod (text, &end);
	if (end == NULL || *end != '\0' || !isfinite (value))
		return rd_fail (error, source, path, "expected a number");
	*out = (gfloat) value;
	return TRUE;
}

static gboolean
rd_bool (YamlNode     *node,
         const gchar  *source,
         const gchar  *path,
         gboolean     *out,
         GError      **error)
{
	const gchar *text = rd_scalar (node);

	if (g_strcmp0 (text, "true") == 0)
		*out = TRUE;
	else if (g_strcmp0 (text, "false") == 0)
		*out = FALSE;
	else
		return rd_fail (error, source, path, "expected true or false");
	return TRUE;
}

/*
 * read_tile_ref:
 * A tile reference: a 0-based tile index, or a mapping
 * {tile, rotate, flip[, weight]} (weight only where @out_weight is set).
 * Produces a 1-based cell value.
 */
static gboolean
read_tile_ref (YamlNode     *node,
               const gchar  *source,
               const gchar  *path,
               guint         tile_count,
               guint        *out_tile,
               gfloat       *out_weight,
               GError      **error)
{
	guint index = 0;
	guint rotate = 0;
	gboolean flip = FALSE;
	gfloat weight = 1.0f;
	YamlMapping *map;
	guint i;
	gboolean have_tile = FALSE;

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
	{
		if (!rd_uint (node, source, path, tile_count > 0 ? tile_count - 1 : LRG_TILE_ID_MASK - 1,
		              &index, error))
			return FALSE;
		*out_tile = index + 1;
		if (out_weight != NULL)
			*out_weight = 1.0f;
		return TRUE;
	}
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return rd_fail (error, source, path, "expected a tile index or a mapping with tile:");

	map = yaml_node_get_mapping (node);
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "tile") == 0)
		{
			if (!rd_uint (value, source, p, tile_count > 0 ? tile_count - 1 : LRG_TILE_ID_MASK - 1,
			              &index, error))
				return FALSE;
			have_tile = TRUE;
		}
		else if (g_strcmp0 (key, "rotate") == 0)
		{
			if (!rd_uint (value, source, p, 3, &rotate, error))
				return FALSE;
		}
		else if (g_strcmp0 (key, "flip") == 0)
		{
			if (!rd_bool (value, source, p, &flip, error))
				return FALSE;
		}
		else if (g_strcmp0 (key, "weight") == 0 && out_weight != NULL)
		{
			if (!rd_float (value, source, p, &weight, error))
				return FALSE;
			if (weight <= 0.0f)
				return rd_fail (error, source, p, "expected a number greater than 0");
		}
		else
		{
			g_autofree gchar *msg = g_strdup_printf ("unknown key \"%s\" (expected tile, rotate, flip%s)",
			                                         key, out_weight != NULL ? ", weight" : "");

			return rd_fail (error, source, path, msg);
		}
	}
	if (!have_tile)
		return rd_fail (error, source, path, "missing key \"tile\"");

	*out_tile = lrg_tile_make (index + 1, flip, rotate);
	if (out_weight != NULL)
		*out_weight = weight;
	return TRUE;
}

/* pattern cells: row-major 3x3, NW N NE / W c E / SW S SE */
static const gint pattern_dir[9] = { 7, 0, 1, 6, -1, 2, 5, 4, 3 };

static gboolean
read_pattern (YamlNode     *node,
              const gchar  *source,
              const gchar  *path,
              LrgTileRule  *rule,
              GError      **error)
{
	YamlSequence *seq;
	guint row;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return rd_fail (error, source, path, "expected three strings of three characters");
	seq = yaml_node_get_sequence (node);
	if (yaml_sequence_get_length (seq) != 3)
		return rd_fail (error, source, path, "expected three strings of three characters");

	for (row = 0; row < 3; row++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s/%u", path, row);
		const gchar *text = rd_scalar (yaml_sequence_get_element (seq, row));
		guint col;

		if (text == NULL || strlen (text) != 3)
			return rd_fail (error, source, p, "expected a string of three characters");
		for (col = 0; col < 3; col++)
		{
			gint dir = pattern_dir[row * 3 + col];
			gchar c = text[col];

			if (dir < 0)
			{
				if (c != '@')
					return rd_fail (error, source, p, "the centre character must be '@'");
				continue;
			}
			if (c == '#')
				lrg_tile_rule_set_neighbor (rule, (LrgTileNeighbor) (1u << dir), LRG_TILE_RULE_MATCH_THIS);
			else if (c == '-')
				lrg_tile_rule_set_neighbor (rule, (LrgTileNeighbor) (1u << dir), LRG_TILE_RULE_MATCH_NOT_THIS);
			else if (c != '.')
				return rd_fail (error, source, p,
				                "expected '#' (this), '-' (not this) or '.' (any) around '@'");
		}
	}
	return TRUE;
}

static LrgTileRule *
read_rule (YamlNode     *node,
           const gchar  *source,
           const gchar  *path,
           guint         tile_count,
           GError      **error)
{
	g_autoptr(LrgTileRule) rule = lrg_tile_rule_new ();
	YamlMapping *map;
	gboolean have_tiles = FALSE;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		rd_fail (error, source, path, "expected a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (node);

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "pattern") == 0)
		{
			if (!read_pattern (value, source, p, rule, error))
				return NULL;
		}
		else if (g_strcmp0 (key, "transform") == 0)
		{
			const gchar *text = rd_scalar (value);
			guint t;
			gboolean found = FALSE;

			for (t = 0; transform_values[t].value_nick != NULL; t++)
			{
				if (g_strcmp0 (text, transform_values[t].value_nick) == 0)
				{
					rule->transform = (LrgTileRuleTransform) transform_values[t].value;
					found = TRUE;
				}
			}
			if (!found)
			{
				rd_fail (error, source, p, "expected fixed, rotated, mirror-x, mirror-y or mirror-xy");
				return NULL;
			}
		}
		else if (g_strcmp0 (key, "tiles") == 0)
		{
			YamlSequence *seq;
			guint k;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE ||
			    yaml_sequence_get_length (yaml_node_get_sequence (value)) == 0)
			{
				rd_fail (error, source, p, "expected a non-empty list of tiles");
				return NULL;
			}
			seq = yaml_node_get_sequence (value);
			for (k = 0; k < yaml_sequence_get_length (seq); k++)
			{
				g_autofree gchar *tp = g_strdup_printf ("%s/%u", p, k);
				guint tile = 0;
				gfloat weight = 1.0f;

				if (!read_tile_ref (yaml_sequence_get_element (seq, k), source, tp, tile_count,
				                    &tile, &weight, error))
					return NULL;
				lrg_tile_rule_add_output (rule, tile, weight);
			}
			have_tiles = TRUE;
		}
		else
		{
			/* unknown keys are kept and written back */
			if (rule->extra == NULL)
				rule->extra = yaml_mapping_new ();
			yaml_mapping_set_member (rule->extra, key, value);
		}
	}

	if (!have_tiles)
	{
		rd_fail (error, source, path, "missing key \"tiles\"");
		return NULL;
	}
	return g_steal_pointer (&rule);
}

LrgRuleTile *
lrg_rule_tile_from_yaml (YamlNode     *node,
                         const gchar  *source,
                         const gchar  *path,
                         guint         tile_count,
                         GError      **error)
{
	g_autoptr(LrgRuleTile) self = NULL;
	YamlMapping *map;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		rd_fail (error, source, path, "expected a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (node);
	if (!yaml_mapping_has_member (map, "name") || rd_scalar (yaml_mapping_get_member (map, "name")) == NULL)
	{
		rd_fail (error, source, path, "missing key \"name\" (a string)");
		return NULL;
	}

	self = lrg_rule_tile_new (rd_scalar (yaml_mapping_get_member (map, "name")));

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "name") == 0)
			continue;
		else if (g_strcmp0 (key, "default") == 0)
		{
			if (!read_tile_ref (value, source, p, tile_count, &self->default_tile, NULL, error))
				return NULL;
		}
		else if (g_strcmp0 (key, "seed") == 0)
		{
			if (!rd_uint (value, source, p, G_MAXUINT, &self->seed, error))
				return NULL;
		}
		else if (g_strcmp0 (key, "border_is_this") == 0)
		{
			if (!rd_bool (value, source, p, &self->border_is_this, error))
				return NULL;
		}
		else if (g_strcmp0 (key, "match_tiles") == 0)
		{
			YamlSequence *seq;
			guint k;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE)
			{
				rd_fail (error, source, p, "expected a list of tile indices");
				return NULL;
			}
			seq = yaml_node_get_sequence (value);
			for (k = 0; k < yaml_sequence_get_length (seq); k++)
			{
				g_autofree gchar *tp = g_strdup_printf ("%s/%u", p, k);
				guint index = 0;

				if (!rd_uint (yaml_sequence_get_element (seq, k), source, tp,
				              tile_count > 0 ? tile_count - 1 : LRG_TILE_ID_MASK - 1, &index, error))
					return NULL;
				index += 1;
				g_array_append_val (self->match_tiles, index);
			}
		}
		else if (g_strcmp0 (key, "rules") == 0)
		{
			YamlSequence *seq;
			guint k;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE)
			{
				rd_fail (error, source, p, "expected a list of rules");
				return NULL;
			}
			seq = yaml_node_get_sequence (value);
			for (k = 0; k < yaml_sequence_get_length (seq); k++)
			{
				g_autofree gchar *rp = g_strdup_printf ("%s/%u", p, k);
				LrgTileRule *rule = read_rule (yaml_sequence_get_element (seq, k), source, rp,
				                               tile_count, error);

				if (rule == NULL)
					return NULL;
				g_ptr_array_add (self->rules, rule);
			}
		}
		else
		{
			if (self->extra == NULL)
				self->extra = yaml_mapping_new ();
			yaml_mapping_set_member (self->extra, key, value);
		}
	}
	return g_steal_pointer (&self);
}

/* a tile reference: a plain index when untransformed and weight 1 */
static YamlNode *
tile_ref_to_yaml (guint    tile,
                  gfloat   weight,
                  gboolean with_weight)
{
	g_autoptr(YamlMapping) map = NULL;
	guint rotation = lrg_tile_get_rotation (tile);
	gboolean mirrored = lrg_tile_get_mirrored (tile);

	if (rotation == 0 && !mirrored && (!with_weight || weight == 1.0f))
		return lrg_data_yaml_int ((gint64) lrg_tile_get_id (tile) - 1);

	map = yaml_mapping_new ();
	{
		g_autoptr(YamlNode) n = lrg_data_yaml_int ((gint64) lrg_tile_get_id (tile) - 1);

		yaml_mapping_set_member (map, "tile", n);
	}
	if (with_weight && weight != 1.0f)
	{
		g_autoptr(YamlNode) n = lrg_data_yaml_float (weight);

		yaml_mapping_set_member (map, "weight", n);
	}
	if (rotation != 0)
	{
		g_autoptr(YamlNode) n = lrg_data_yaml_int (rotation);

		yaml_mapping_set_member (map, "rotate", n);
	}
	if (mirrored)
	{
		g_autoptr(YamlNode) n = lrg_data_yaml_boolean (TRUE);

		yaml_mapping_set_member (map, "flip", n);
	}
	return yaml_node_new_mapping (map);
}

/* appends the unknown keys a mapping was loaded with */
static void
append_extra (YamlMapping *map,
              YamlMapping *extra)
{
	guint i;

	if (extra == NULL)
		return;
	for (i = 0; i < yaml_mapping_get_size (extra); i++)
		yaml_mapping_set_member (map, yaml_mapping_get_key (extra, i), yaml_mapping_get_value (extra, i));
}

static YamlNode *
rule_to_yaml (LrgTileRule *rule)
{
	g_autoptr(YamlMapping) map = yaml_mapping_new ();
	guint k;

	/* pattern, as three rows of three characters */
	if (rule->this_mask != 0 || rule->not_mask != 0)
	{
		g_autoptr(YamlSequence) rows = yaml_sequence_new ();
		g_autoptr(YamlNode) node = NULL;
		guint row;

		for (row = 0; row < 3; row++)
		{
			gchar text[4];
			guint col;
			g_autoptr(YamlNode) cell = NULL;

			for (col = 0; col < 3; col++)
			{
				gint dir = pattern_dir[row * 3 + col];

				if (dir < 0)
					text[col] = '@';
				else if ((rule->this_mask & (1u << dir)) != 0)
					text[col] = '#';
				else if ((rule->not_mask & (1u << dir)) != 0)
					text[col] = '-';
				else
					text[col] = '.';
			}
			text[3] = '\0';
			cell = yaml_node_new_scalar (text, YAML_SCALAR_STYLE_DOUBLE_QUOTED);
			yaml_sequence_add_element (rows, cell);
		}
		node = yaml_node_new_sequence (rows);
		yaml_mapping_set_member (map, "pattern", node);
	}

	if (rule->transform != LRG_TILE_RULE_TRANSFORM_FIXED)
	{
		g_autoptr(YamlNode) node = lrg_data_yaml_string (transform_values[rule->transform].value_nick);

		yaml_mapping_set_member (map, "transform", node);
	}

	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();
		g_autoptr(YamlNode) node = NULL;

		for (k = 0; k < rule->outputs->len; k++)
		{
			RuleOutput *out = &g_array_index (rule->outputs, RuleOutput, k);
			g_autoptr(YamlNode) t = tile_ref_to_yaml (out->tile, out->weight, TRUE);

			yaml_sequence_add_element (seq, t);
		}
		node = yaml_node_new_sequence (seq);
		yaml_mapping_set_member (map, "tiles", node);
	}

	append_extra (map, rule->extra);
	return yaml_node_new_mapping (map);
}

YamlNode *
lrg_rule_tile_to_yaml (LrgRuleTile *self)
{
	g_autoptr(YamlMapping) map = yaml_mapping_new ();
	guint i;

	g_return_val_if_fail (LRG_IS_RULE_TILE (self), NULL);

	{
		g_autoptr(YamlNode) n = lrg_data_yaml_string (self->name != NULL ? self->name : "");

		yaml_mapping_set_member (map, "name", n);
	}
	if (self->default_tile != 0)
	{
		g_autoptr(YamlNode) n = tile_ref_to_yaml (self->default_tile, 1.0f, FALSE);

		yaml_mapping_set_member (map, "default", n);
	}
	if (self->seed != 0)
	{
		g_autoptr(YamlNode) n = lrg_data_yaml_int (self->seed);

		yaml_mapping_set_member (map, "seed", n);
	}
	if (self->border_is_this)
	{
		g_autoptr(YamlNode) n = lrg_data_yaml_boolean (TRUE);

		yaml_mapping_set_member (map, "border_is_this", n);
	}
	if (self->match_tiles->len > 0)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();
		g_autoptr(YamlNode) node = NULL;

		for (i = 0; i < self->match_tiles->len; i++)
		{
			g_autoptr(YamlNode) n = lrg_data_yaml_int ((gint64) g_array_index (self->match_tiles, guint, i) - 1);

			yaml_sequence_add_element (seq, n);
		}
		node = yaml_node_new_sequence (seq);
		yaml_mapping_set_member (map, "match_tiles", node);
	}
	if (self->rules->len > 0)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();
		g_autoptr(YamlNode) node = NULL;

		for (i = 0; i < self->rules->len; i++)
		{
			g_autoptr(YamlNode) n = rule_to_yaml (g_ptr_array_index (self->rules, i));

			yaml_sequence_add_element (seq, n);
		}
		node = yaml_node_new_sequence (seq);
		yaml_mapping_set_member (map, "rules", node);
	}

	append_extra (map, self->extra);
	return yaml_node_new_mapping (map);
}
