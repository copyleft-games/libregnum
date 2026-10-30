/* lrg-procgen-wfc.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Wave function collapse, simple tiled model. A cell's possible tiles are
 * a 64-bit set; weights and entropies are integers (fixed point), so a
 * seed gives the same map on every platform.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-wfc.h"
#include "../lrg-log.h"

#include <string.h>

typedef struct
{
	gchar   *name;
	guint8   tag;
	gdouble  weight;
} WfcTile;

struct _LrgProcgenWfc
{
	LrgProcgenGenerator parent_instance;

	GArray  *tiles;            /* WfcTile */
	guint64  compat[4][LRG_PROCGEN_WFC_MAX_TILES]; /* [dir][a] = tiles allowed at dir of a */
	guint    max_attempts;
	guint    backtrack_depth;
	guint    max_backtracks;
	gchar   *border_tile;
	guint    last_attempts;
	guint    last_backtracks;
};

G_DEFINE_FINAL_TYPE (LrgProcgenWfc, lrg_procgen_wfc, LRG_TYPE_PROCGEN_GENERATOR)

enum
{
	PROP_0,
	PROP_MAX_ATTEMPTS,
	PROP_BACKTRACK_DEPTH,
	PROP_MAX_BACKTRACKS,
	PROP_BORDER_TILE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* neighbour offsets by LrgProcgenDirection (up, right, down, left) */
static const gint dir_dx[4] = { 0, 1, 0, -1 };
static const gint dir_dy[4] = { -1, 0, 1, 0 };

#define BIT(t) (G_GUINT64_CONSTANT (1) << (t))

/* ==========================================================================
 * Fixed-point helpers
 * ========================================================================== */

/* log2 (x) in Q16 for x >= 1, by repeated squaring of the mantissa */
static guint64
log2_q16 (guint64 x)
{
	guint ip = 63 - (guint) __builtin_clzll (x);
	guint64 m;
	guint64 frac = 0;
	guint b;

	m = ip >= 30 ? x >> (ip - 30) : x << (30 - ip); /* Q30 in [1, 2) */
	for (b = 0; b < 16; b++)
	{
		m = (m * m) >> 30;
		frac <<= 1;
		if (m >= (G_GUINT64_CONSTANT (1) << 31))
		{
			m >>= 1;
			frac |= 1;
		}
	}
	return ((guint64) ip << 16) | frac;
}

/* ==========================================================================
 * One generation run
 * ========================================================================== */

typedef struct
{
	gint      w;
	gint      h;
	guint     n;                /* cells */
	guint     n_tiles;
	guint64  *wave;             /* possible tiles per cell */
	guint64  *sum_w;            /* sum of weights of the possible tiles */
	guint64  *sum_wl;           /* sum of weight * log2 (weight), Q16 */
	guint32   weight[LRG_PROCGEN_WFC_MAX_TILES];
	guint64   wlog[LRG_PROCGEN_WFC_MAX_TILES];
	guint32  *stack;            /* cells to propagate from */
	guint8   *queued;
	guint     stack_len;
	guint64 **snap_wave;        /* ring of recent states, before a choice */
	guint    *snap_cell;
	guint    *snap_tile;
	guint     snap_cap;
	guint     snap_head;
	guint     snap_len;
	guint     backtracks;
} WfcRun;

static void
run_free (WfcRun *r)
{
	guint i;

	for (i = 0; i < r->snap_cap; i++)
		g_free (r->snap_wave[i]);
	g_free (r->snap_wave);
	g_free (r->snap_cell);
	g_free (r->snap_tile);
	g_free (r->wave);
	g_free (r->sum_w);
	g_free (r->sum_wl);
	g_free (r->stack);
	g_free (r->queued);
}

/* recomputes the entropy sums of every cell from the wave */
static void
run_recompute_sums (WfcRun *r)
{
	guint c;

	for (c = 0; c < r->n; c++)
	{
		guint64 bits = r->wave[c];

		r->sum_w[c] = 0;
		r->sum_wl[c] = 0;
		while (bits != 0)
		{
			guint t = (guint) __builtin_ctzll (bits);

			r->sum_w[c] += r->weight[t];
			r->sum_wl[c] += r->wlog[t];
			bits &= bits - 1;
		}
	}
}

/* narrows a cell to @keep (a subset of its tiles), updating its sums */
static void
run_restrict (WfcRun  *r,
              guint    c,
              guint64  keep)
{
	guint64 removed = r->wave[c] & ~keep;

	while (removed != 0)
	{
		guint t = (guint) __builtin_ctzll (removed);

		r->sum_w[c] -= r->weight[t];
		r->sum_wl[c] -= r->wlog[t];
		removed &= removed - 1;
	}
	r->wave[c] = keep;
}

static void
run_push (WfcRun *r,
          guint   c)
{
	if (r->queued[c])
		return;
	r->queued[c] = 1;
	r->stack[r->stack_len++] = c;
}

/* applies the adjacency rules until nothing changes; FALSE on a contradiction */
static gboolean
run_propagate (LrgProcgenWfc *self,
               WfcRun        *r)
{
	while (r->stack_len > 0)
	{
		guint c = r->stack[--r->stack_len];
		gint cx = (gint) (c % (guint) r->w);
		gint cy = (gint) (c / (guint) r->w);
		guint d;

		r->queued[c] = 0;
		for (d = 0; d < 4; d++)
		{
			gint nx = cx + dir_dx[d];
			gint ny = cy + dir_dy[d];
			guint ni;
			guint64 allowed = 0;
			guint64 bits = r->wave[c];
			guint64 narrowed;

			if (nx < 0 || ny < 0 || nx >= r->w || ny >= r->h)
				continue;
			ni = (guint) (ny * r->w + nx);

			/* what any remaining tile of c allows on that side */
			while (bits != 0)
			{
				allowed |= self->compat[d][__builtin_ctzll (bits)];
				bits &= bits - 1;
			}
			narrowed = r->wave[ni] & allowed;
			if (narrowed == r->wave[ni])
				continue;
			if (narrowed == 0)
			{
				/* contradiction: drop the pending work */
				while (r->stack_len > 0)
					r->queued[r->stack[--r->stack_len]] = 0;
				return FALSE;
			}
			run_restrict (r, ni, narrowed);
			run_push (r, ni);
		}
	}
	return TRUE;
}

/* the undecided cell of lowest entropy (random among ties), or -1 */
static gint
run_select (WfcRun          *r,
            LrgRandomStream *rng)
{
	gint best = -1;
	gint64 best_e = 0;
	guint ties = 0;
	guint c;

	for (c = 0; c < r->n; c++)
	{
		gint64 e;

		if ((r->wave[c] & (r->wave[c] - 1)) == 0)
			continue; /* decided */

		/* H = log2 (W) - sum (w log2 w) / W, all in Q16 */
		e = (gint64) log2_q16 (r->sum_w[c]) - (gint64) (r->sum_wl[c] / r->sum_w[c]);
		if (best < 0 || e < best_e)
		{
			best = (gint) c;
			best_e = e;
			ties = 1;
		}
		else if (e == best_e)
		{
			ties++;
			if (lrg_random_stream_bounded (rng, ties) == 0)
				best = (gint) c;
		}
	}
	return best;
}

/* a tile of a cell, picked by weight */
static guint
run_choose (WfcRun          *r,
            guint            c,
            LrgRandomStream *rng)
{
	guint64 bits = r->wave[c];
	guint32 pick = lrg_random_stream_bounded (rng, (guint32) r->sum_w[c]);
	guint t = 0;

	while (bits != 0)
	{
		t = (guint) __builtin_ctzll (bits);
		if (pick < r->weight[t])
			return t;
		pick -= r->weight[t];
		bits &= bits - 1;
	}
	return t;
}

/* remembers the state before choosing @tile for cell @c */
static void
run_snapshot (WfcRun *r,
              guint   c,
              guint   tile)
{
	guint slot;

	if (r->snap_cap == 0)
		return;
	if (r->snap_len == r->snap_cap)
	{
		/* full: the oldest slot is reused */
		slot = r->snap_head;
		r->snap_head = (r->snap_head + 1) % r->snap_cap;
	}
	else
		slot = (r->snap_head + r->snap_len++) % r->snap_cap;
	memcpy (r->snap_wave[slot], r->wave, r->n * sizeof (guint64));
	r->snap_cell[slot] = c;
	r->snap_tile[slot] = tile;
}

/* one attempt from a blank wave; TRUE when every cell is decided */
static gboolean
run_attempt (LrgProcgenWfc   *self,
             WfcRun          *r,
             LrgRandomStream *rng,
             gint             border)
{
	guint64 full = r->n_tiles == 64 ? G_MAXUINT64 : BIT (r->n_tiles) - 1;
	guint c;

	/* --- a blank wave, the border constraint, the rules everywhere --- */
	for (c = 0; c < r->n; c++)
		r->wave[c] = full;
	run_recompute_sums (r);
	r->snap_len = 0;
	r->snap_head = 0;
	for (c = 0; c < r->n; c++)
	{
		gint x = (gint) (c % (guint) r->w);
		gint y = (gint) (c / (guint) r->w);

		if (border >= 0 && (x == 0 || y == 0 || x == r->w - 1 || y == r->h - 1))
			run_restrict (r, c, BIT (border));
		run_push (r, c);
	}
	if (!run_propagate (self, r))
		return FALSE;

	/* --- observe and propagate until decided --- */
	for (;;)
	{
		gint cell = run_select (r, rng);
		guint tile;

		if (cell < 0)
			return TRUE;
		tile = run_choose (r, (guint) cell, rng);
		run_snapshot (r, (guint) cell, tile);
		run_restrict (r, (guint) cell, BIT (tile));
		run_push (r, (guint) cell);

		/* --- on a contradiction, undo recent choices, banning each --- */
		while (!run_propagate (self, r))
		{
			gboolean resumed = FALSE;

			while (r->snap_len > 0)
			{
				guint slot = (r->snap_head + r->snap_len - 1) % r->snap_cap;
				guint64 keep;

				if (r->backtracks >= self->max_backtracks)
					return FALSE;
				r->snap_len--;
				r->backtracks++;
				memcpy (r->wave, r->snap_wave[slot], r->n * sizeof (guint64));
				run_recompute_sums (r);
				keep = r->wave[r->snap_cell[slot]] & ~BIT (r->snap_tile[slot]);
				if (keep == 0)
					continue; /* nothing left there: undo one more */
				run_restrict (r, r->snap_cell[slot], keep);
				run_push (r, r->snap_cell[slot]);
				resumed = TRUE;
				break;
			}
			if (!resumed)
				return FALSE;
		}
	}
}

static gboolean
lrg_procgen_wfc_generate (LrgProcgenGenerator  *generator,
                          LrgProcgenGrid       *grid,
                          LrgRandomStream      *rng,
                          GError              **error)
{
	LrgProcgenWfc *self = LRG_PROCGEN_WFC (generator);
	WfcRun r;
	gint border = -1;
	guint attempt;
	guint t;
	guint c;
	gboolean ok = FALSE;

	if (self->tiles->len == 0)
	{
		g_set_error_literal (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
		                     "wfc: the model has no tiles");
		return FALSE;
	}
	if (self->border_tile != NULL)
	{
		border = lrg_procgen_wfc_find_tile (self, self->border_tile);
		if (border < 0)
		{
			g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
			             "wfc: unknown border tile '%s'", self->border_tile);
			return FALSE;
		}
	}

	/* --- run state: integer weights (1/256 steps) and their w log2 w --- */
	memset (&r, 0, sizeof r);
	r.w = (gint) lrg_procgen_grid_get_width (grid);
	r.h = (gint) lrg_procgen_grid_get_height (grid);
	r.n = (guint) (r.w * r.h);
	r.n_tiles = self->tiles->len;
	for (t = 0; t < r.n_tiles; t++)
	{
		gdouble w = g_array_index (self->tiles, WfcTile, t).weight * 256.0 + 0.5;

		r.weight[t] = (guint32) CLAMP (w, 1.0, 1048576.0);
		r.wlog[t] = (guint64) r.weight[t] * log2_q16 (r.weight[t]);
	}
	r.wave = g_new (guint64, r.n);
	r.sum_w = g_new (guint64, r.n);
	r.sum_wl = g_new (guint64, r.n);
	r.stack = g_new (guint32, r.n);
	r.queued = g_new0 (guint8, r.n);
	r.snap_cap = self->backtrack_depth;
	r.snap_wave = g_new0 (guint64 *, MAX (r.snap_cap, 1));
	r.snap_cell = g_new0 (guint, MAX (r.snap_cap, 1));
	r.snap_tile = g_new0 (guint, MAX (r.snap_cap, 1));
	for (t = 0; t < r.snap_cap; t++)
		r.snap_wave[t] = g_new (guint64, r.n);

	/* --- attempts, each continuing the same random stream --- */
	self->last_backtracks = 0;
	for (attempt = 1; attempt <= self->max_attempts && !ok; attempt++)
	{
		self->last_attempts = attempt;
		r.backtracks = 0;
		ok = run_attempt (self, &r, rng, border);
		self->last_backtracks += r.backtracks;
		if (!ok)
			lrg_debug (LRG_LOG_DOMAIN_PROCGEN, "wfc: attempt %u hit a contradiction", attempt);
	}

	if (ok)
		for (c = 0; c < r.n; c++)
			lrg_procgen_grid_set_cell (grid, (gint) (c % (guint) r.w), (gint) (c / (guint) r.w),
			                           g_array_index (self->tiles, WfcTile,
			                                          __builtin_ctzll (r.wave[c])).tag);
	run_free (&r);

	if (!ok)
	{
		g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_CONTRADICTION,
		             "wfc: contradiction on all %u attempts", self->max_attempts);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
wfc_tile_clear (gpointer data)
{
	WfcTile *tile = data;

	g_clear_pointer (&tile->name, g_free);
}

static void
lrg_procgen_wfc_finalize (GObject *object)
{
	LrgProcgenWfc *self = LRG_PROCGEN_WFC (object);

	g_clear_pointer (&self->tiles, g_array_unref);
	g_clear_pointer (&self->border_tile, g_free);
	G_OBJECT_CLASS (lrg_procgen_wfc_parent_class)->finalize (object);
}

static void
lrg_procgen_wfc_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgProcgenWfc *self = LRG_PROCGEN_WFC (object);

	switch (prop_id)
	{
	case PROP_MAX_ATTEMPTS:
		g_value_set_uint (value, self->max_attempts);
		break;
	case PROP_BACKTRACK_DEPTH:
		g_value_set_uint (value, self->backtrack_depth);
		break;
	case PROP_MAX_BACKTRACKS:
		g_value_set_uint (value, self->max_backtracks);
		break;
	case PROP_BORDER_TILE:
		g_value_set_string (value, self->border_tile);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_wfc_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgProcgenWfc *self = LRG_PROCGEN_WFC (object);

	switch (prop_id)
	{
	case PROP_MAX_ATTEMPTS:
		lrg_procgen_wfc_set_max_attempts (self, g_value_get_uint (value));
		break;
	case PROP_BACKTRACK_DEPTH:
		lrg_procgen_wfc_set_backtrack_depth (self, g_value_get_uint (value));
		break;
	case PROP_MAX_BACKTRACKS:
		lrg_procgen_wfc_set_max_backtracks (self, g_value_get_uint (value));
		break;
	case PROP_BORDER_TILE:
		lrg_procgen_wfc_set_border_tile (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_wfc_class_init (LrgProcgenWfcClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgProcgenGeneratorClass *generator_class = LRG_PROCGEN_GENERATOR_CLASS (klass);

	object_class->finalize = lrg_procgen_wfc_finalize;
	object_class->get_property = lrg_procgen_wfc_get_property;
	object_class->set_property = lrg_procgen_wfc_set_property;
	generator_class->generate = lrg_procgen_wfc_generate;

	/**
	 * LrgProcgenWfc:max-attempts:
	 *
	 * Attempts (a restart after each failed one) before giving up.
	 */
	properties[PROP_MAX_ATTEMPTS] =
		g_param_spec_uint ("max-attempts", NULL, NULL, 1, 1000, 10,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenWfc:backtrack-depth:
	 *
	 * How many recent choices are kept to backtrack through on a
	 * contradiction (0 restarts at once). Each costs one copy of the wave
	 * (8 bytes a cell).
	 */
	properties[PROP_BACKTRACK_DEPTH] =
		g_param_spec_uint ("backtrack-depth", NULL, NULL, 0, 4096, 32,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenWfc:max-backtracks:
	 *
	 * Choices undone in one attempt before it restarts.
	 */
	properties[PROP_MAX_BACKTRACKS] =
		g_param_spec_uint ("max-backtracks", NULL, NULL, 0, G_MAXUINT, 10000,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenWfc:border-tile:
	 *
	 * Name of the tile every border cell must be, or %NULL.
	 */
	properties[PROP_BORDER_TILE] =
		g_param_spec_string ("border-tile", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procgen_wfc_init (LrgProcgenWfc *self)
{
	self->tiles = g_array_new (FALSE, TRUE, sizeof (WfcTile));
	g_array_set_clear_func (self->tiles, wfc_tile_clear);
	self->max_attempts = 10;
	self->backtrack_depth = 32;
	self->max_backtracks = 10000;
}

LrgProcgenWfc *
lrg_procgen_wfc_new (guint width,
                     guint height)
{
	return g_object_new (LRG_TYPE_PROCGEN_WFC, "width", width, "height", height, NULL);
}

/* ==========================================================================
 * Model
 * ========================================================================== */

gint
lrg_procgen_wfc_add_tile (LrgProcgenWfc *self,
                          const gchar   *name,
                          guint8         tag,
                          gdouble        weight)
{
	WfcTile tile;

	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), -1);
	g_return_val_if_fail (name != NULL, -1);
	g_return_val_if_fail (weight > 0.0, -1);

	if (self->tiles->len >= LRG_PROCGEN_WFC_MAX_TILES || lrg_procgen_wfc_find_tile (self, name) >= 0)
		return -1;
	tile.name = g_strdup (name);
	tile.tag = tag;
	tile.weight = weight;
	g_array_append_val (self->tiles, tile);
	return (gint) self->tiles->len - 1;
}

guint
lrg_procgen_wfc_get_tile_count (LrgProcgenWfc *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0);
	return self->tiles->len;
}

gint
lrg_procgen_wfc_find_tile (LrgProcgenWfc *self,
                           const gchar   *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), -1);
	for (i = 0; i < self->tiles->len; i++)
		if (g_strcmp0 (g_array_index (self->tiles, WfcTile, i).name, name) == 0)
			return (gint) i;
	return -1;
}

const gchar *
lrg_procgen_wfc_get_tile_name (LrgProcgenWfc *self,
                               guint          index)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), NULL);
	if (index >= self->tiles->len)
		return NULL;
	return g_array_index (self->tiles, WfcTile, index).name;
}

guint8
lrg_procgen_wfc_get_tile_tag (LrgProcgenWfc *self,
                              guint          index)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0);
	g_return_val_if_fail (index < self->tiles->len, 0);
	return g_array_index (self->tiles, WfcTile, index).tag;
}

gdouble
lrg_procgen_wfc_get_tile_weight (LrgProcgenWfc *self,
                                 guint          index)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0.0);
	g_return_val_if_fail (index < self->tiles->len, 0.0);
	return g_array_index (self->tiles, WfcTile, index).weight;
}

void
lrg_procgen_wfc_allow (LrgProcgenWfc       *self,
                       guint                a,
                       guint                b,
                       LrgProcgenDirection  direction)
{
	g_return_if_fail (LRG_IS_PROCGEN_WFC (self));
	g_return_if_fail (a < self->tiles->len && b < self->tiles->len);
	g_return_if_fail (direction <= LRG_PROCGEN_DIRECTION_LEFT);

	self->compat[direction][a] |= BIT (b);
	self->compat[(direction + 2) % 4][b] |= BIT (a);
}

gboolean
lrg_procgen_wfc_is_allowed (LrgProcgenWfc       *self,
                            guint                a,
                            guint                b,
                            LrgProcgenDirection  direction)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), FALSE);
	g_return_val_if_fail (direction <= LRG_PROCGEN_DIRECTION_LEFT, FALSE);
	if (a >= self->tiles->len || b >= self->tiles->len)
		return FALSE;
	return (self->compat[direction][a] & BIT (b)) != 0;
}

void
lrg_procgen_wfc_clear (LrgProcgenWfc *self)
{
	g_return_if_fail (LRG_IS_PROCGEN_WFC (self));
	g_array_set_size (self->tiles, 0);
	memset (self->compat, 0, sizeof self->compat);
}

void
lrg_procgen_wfc_learn (LrgProcgenWfc  *self,
                       LrgProcgenGrid *sample)
{
	g_autoptr(GEnumClass) cells = NULL;
	guint count[256];
	gint tile_of[256];
	const guint8 *data;
	gsize n;
	gsize i;
	guint tag;
	guint w;
	guint h;
	guint x;
	guint y;

	g_return_if_fail (LRG_IS_PROCGEN_WFC (self));
	g_return_if_fail (LRG_IS_PROCGEN_GRID (sample));

	lrg_procgen_wfc_clear (self);
	data = lrg_procgen_grid_get_cells (sample, &n);
	w = lrg_procgen_grid_get_width (sample);
	h = lrg_procgen_grid_get_height (sample);

	/* --- one tile per tag present, weighted by frequency --- */
	memset (count, 0, sizeof count);
	for (i = 0; i < n; i++)
		count[data[i]]++;
	cells = g_type_class_ref (LRG_TYPE_PROCGEN_CELL);
	for (tag = 0; tag < 256; tag++)
	{
		GEnumValue *value;
		g_autofree gchar *name = NULL;

		tile_of[tag] = -1;
		if (count[tag] == 0)
			continue;
		value = g_enum_get_value (cells, (gint) tag);
		name = value != NULL ? g_strdup (value->value_nick) : g_strdup_printf ("tag-%u", tag);
		tile_of[tag] = lrg_procgen_wfc_add_tile (self, name, (guint8) tag, (gdouble) count[tag]);
		if (tile_of[tag] < 0)
			lrg_debug (LRG_LOG_DOMAIN_PROCGEN, "wfc: sample tag %u dropped (model full)", tag);
	}

	/* --- every adjacency seen in the sample is allowed --- */
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gint a = tile_of[data[y * w + x]];

			if (a < 0)
				continue;
			if (x + 1 < w && tile_of[data[y * w + x + 1]] >= 0)
				lrg_procgen_wfc_allow (self, (guint) a, (guint) tile_of[data[y * w + x + 1]],
				                       LRG_PROCGEN_DIRECTION_RIGHT);
			if (y + 1 < h && tile_of[data[(y + 1) * w + x]] >= 0)
				lrg_procgen_wfc_allow (self, (guint) a, (guint) tile_of[data[(y + 1) * w + x]],
				                       LRG_PROCGEN_DIRECTION_DOWN);
		}
}

gboolean
lrg_procgen_wfc_check (LrgProcgenWfc  *self,
                       LrgProcgenGrid *grid)
{
	guint64 tiles_of_tag[256];
	gint w;
	gint h;
	gint x;
	gint y;
	guint t;

	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), FALSE);
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (grid), FALSE);

	memset (tiles_of_tag, 0, sizeof tiles_of_tag);
	for (t = 0; t < self->tiles->len; t++)
		tiles_of_tag[g_array_index (self->tiles, WfcTile, t).tag] |= BIT (t);

	w = (gint) lrg_procgen_grid_get_width (grid);
	h = (gint) lrg_procgen_grid_get_height (grid);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			guint64 here = tiles_of_tag[lrg_procgen_grid_get_cell (grid, x, y)];
			guint d;

			if (here == 0)
				return FALSE;
			/* right and down cover every pair once (rules are symmetric) */
			for (d = LRG_PROCGEN_DIRECTION_RIGHT; d <= LRG_PROCGEN_DIRECTION_DOWN; d++)
			{
				gint nx = x + dir_dx[d];
				gint ny = y + dir_dy[d];
				guint64 there;
				guint64 bits = here;
				gboolean ok = FALSE;

				if (nx >= w || ny >= h)
					continue;
				there = tiles_of_tag[lrg_procgen_grid_get_cell (grid, nx, ny)];
				while (bits != 0 && !ok)
				{
					ok = (self->compat[d][__builtin_ctzll (bits)] & there) != 0;
					bits &= bits - 1;
				}
				if (!ok)
					return FALSE;
			}
		}
	return TRUE;
}

guint
lrg_procgen_wfc_get_last_attempts (LrgProcgenWfc *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0);
	return self->last_attempts;
}

guint
lrg_procgen_wfc_get_last_backtracks (LrgProcgenWfc *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0);
	return self->last_backtracks;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

guint
lrg_procgen_wfc_get_max_attempts (LrgProcgenWfc *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0);
	return self->max_attempts;
}

void
lrg_procgen_wfc_set_max_attempts (LrgProcgenWfc *self,
                                  guint          value)
{
	g_return_if_fail (LRG_IS_PROCGEN_WFC (self));
	g_return_if_fail (value >= 1 && value <= 1000);
	if (self->max_attempts == value)
		return;
	self->max_attempts = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_ATTEMPTS]);
}

guint
lrg_procgen_wfc_get_backtrack_depth (LrgProcgenWfc *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0);
	return self->backtrack_depth;
}

void
lrg_procgen_wfc_set_backtrack_depth (LrgProcgenWfc *self,
                                     guint          value)
{
	g_return_if_fail (LRG_IS_PROCGEN_WFC (self));
	g_return_if_fail (value <= 4096);
	if (self->backtrack_depth == value)
		return;
	self->backtrack_depth = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKTRACK_DEPTH]);
}

guint
lrg_procgen_wfc_get_max_backtracks (LrgProcgenWfc *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), 0);
	return self->max_backtracks;
}

void
lrg_procgen_wfc_set_max_backtracks (LrgProcgenWfc *self,
                                    guint          value)
{
	g_return_if_fail (LRG_IS_PROCGEN_WFC (self));
	if (self->max_backtracks == value)
		return;
	self->max_backtracks = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_BACKTRACKS]);
}

const gchar *
lrg_procgen_wfc_get_border_tile (LrgProcgenWfc *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_WFC (self), NULL);
	return self->border_tile;
}

void
lrg_procgen_wfc_set_border_tile (LrgProcgenWfc *self,
                                 const gchar   *name)
{
	g_return_if_fail (LRG_IS_PROCGEN_WFC (self));
	if (g_set_str (&self->border_tile, name))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_TILE]);
}
