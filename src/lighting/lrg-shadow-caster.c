/* lrg-shadow-caster.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shadow caster interface implementation.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-shadow-caster.h"

G_DEFINE_INTERFACE (LrgShadowCaster, lrg_shadow_caster, G_TYPE_OBJECT)

static void
lrg_shadow_caster_default_init (LrgShadowCasterInterface *iface)
{
    /* Default implementations are NULL */
}

/* LrgShadowEdge boxed type */

G_DEFINE_BOXED_TYPE (LrgShadowEdge, lrg_shadow_edge,
                     lrg_shadow_edge_copy, lrg_shadow_edge_free)

LrgShadowEdge *
lrg_shadow_edge_new (gfloat x1,
                     gfloat y1,
                     gfloat x2,
                     gfloat y2)
{
    LrgShadowEdge *edge;

    edge = g_new (LrgShadowEdge, 1);
    edge->x1 = x1;
    edge->y1 = y1;
    edge->x2 = x2;
    edge->y2 = y2;

    return edge;
}

LrgShadowEdge *
lrg_shadow_edge_copy (const LrgShadowEdge *edge)
{
    g_return_val_if_fail (edge != NULL, NULL);
    return lrg_shadow_edge_new (edge->x1, edge->y1, edge->x2, edge->y2);
}

void
lrg_shadow_edge_free (LrgShadowEdge *edge)
{
    g_free (edge);
}

/* Interface method implementations */

GPtrArray *
lrg_shadow_caster_get_edges (LrgShadowCaster *self)
{
    LrgShadowCasterInterface *iface;

    g_return_val_if_fail (LRG_IS_SHADOW_CASTER (self), NULL);

    iface = LRG_SHADOW_CASTER_GET_IFACE (self);

    if (iface->get_edges != NULL)
        return iface->get_edges (self);

    return g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_shadow_edge_free);
}

gboolean
lrg_shadow_caster_is_opaque (LrgShadowCaster *self)
{
    LrgShadowCasterInterface *iface;

    g_return_val_if_fail (LRG_IS_SHADOW_CASTER (self), TRUE);

    iface = LRG_SHADOW_CASTER_GET_IFACE (self);

    if (iface->is_opaque != NULL)
        return iface->is_opaque (self);

    return TRUE;
}

gfloat
lrg_shadow_caster_get_shadow_opacity (LrgShadowCaster *self)
{
    LrgShadowCasterInterface *iface;

    g_return_val_if_fail (LRG_IS_SHADOW_CASTER (self), 1.0f);

    iface = LRG_SHADOW_CASTER_GET_IFACE (self);

    if (iface->get_shadow_opacity != NULL)
        return iface->get_shadow_opacity (self);

    return 1.0f;
}

/* ==========================================================================
 * LrgShadowOccluder2D
 *
 * The occluder is a list of oriented edges (x1, y1) -> (x2, y2) running
 * clockwise round solids on a y-down screen, so the solid side of every
 * edge is on its right and its outward normal is (dy, -dx).  Polygons are
 * normalised to that winding when added.  Every edge is also recorded in
 * a hash set keyed by its quantised end points; adding an edge whose exact
 * reverse is present removes both (two solids sharing a side), which is
 * how tile walls and adjacent polygons merge into their outline.
 * ========================================================================== */

struct _LrgShadowOccluder2D
{
    GObject     parent_instance;

    GArray     *edges;      /* gfloat x1, y1, x2, y2 per edge */
    GHashTable *index;      /* quantised edge key -> edge slot + 1 */
    gfloat      x;
    gfloat      y;
    gfloat      opacity;
    gboolean    self_shadow;
    gboolean    enabled;
};

static void lrg_shadow_occluder2d_caster_init (LrgShadowCasterInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgShadowOccluder2D, lrg_shadow_occluder2d, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_SHADOW_CASTER,
                                                      lrg_shadow_occluder2d_caster_init))

enum
{
    OCC_PROP_0,
    OCC_PROP_X,
    OCC_PROP_Y,
    OCC_PROP_OPACITY,
    OCC_PROP_SELF_SHADOW,
    OCC_PROP_ENABLED,
    OCC_N_PROPS
};

static GParamSpec *occ_properties[OCC_N_PROPS];

/* edge keys: end points quantised to 1/64 pixel */
typedef struct
{
    gint32 v[4];
} EdgeKey;

static guint
edge_key_hash (gconstpointer data)
{
    const EdgeKey *k = data;
    guint h = 2166136261u;
    guint i;

    for (i = 0; i < 4; i++)
        h = (h ^ (guint) k->v[i]) * 16777619u;
    return h;
}

static gboolean
edge_key_equal (gconstpointer a,
                gconstpointer b)
{
    return memcmp (a, b, sizeof (EdgeKey)) == 0;
}

static void
edge_key_set (EdgeKey *k,
              gfloat   x1,
              gfloat   y1,
              gfloat   x2,
              gfloat   y2)
{
    k->v[0] = (gint32) lrintf (x1 * 64.0f);
    k->v[1] = (gint32) lrintf (y1 * 64.0f);
    k->v[2] = (gint32) lrintf (x2 * 64.0f);
    k->v[3] = (gint32) lrintf (y2 * 64.0f);
}

static void
lrg_shadow_occluder2d_finalize (GObject *object)
{
    LrgShadowOccluder2D *self = LRG_SHADOW_OCCLUDER2D (object);

    g_array_unref (self->edges);
    g_hash_table_unref (self->index);

    G_OBJECT_CLASS (lrg_shadow_occluder2d_parent_class)->finalize (object);
}

static void
lrg_shadow_occluder2d_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgShadowOccluder2D *self = LRG_SHADOW_OCCLUDER2D (object);

    switch (prop_id)
    {
    case OCC_PROP_X:
        g_value_set_float (value, self->x);
        break;
    case OCC_PROP_Y:
        g_value_set_float (value, self->y);
        break;
    case OCC_PROP_OPACITY:
        g_value_set_float (value, self->opacity);
        break;
    case OCC_PROP_SELF_SHADOW:
        g_value_set_boolean (value, self->self_shadow);
        break;
    case OCC_PROP_ENABLED:
        g_value_set_boolean (value, self->enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_shadow_occluder2d_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgShadowOccluder2D *self = LRG_SHADOW_OCCLUDER2D (object);

    switch (prop_id)
    {
    case OCC_PROP_X:
        lrg_shadow_occluder2d_set_position (self, g_value_get_float (value), self->y);
        break;
    case OCC_PROP_Y:
        lrg_shadow_occluder2d_set_position (self, self->x, g_value_get_float (value));
        break;
    case OCC_PROP_OPACITY:
        lrg_shadow_occluder2d_set_opacity (self, g_value_get_float (value));
        break;
    case OCC_PROP_SELF_SHADOW:
        lrg_shadow_occluder2d_set_self_shadow (self, g_value_get_boolean (value));
        break;
    case OCC_PROP_ENABLED:
        lrg_shadow_occluder2d_set_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_shadow_occluder2d_class_init (LrgShadowOccluder2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_shadow_occluder2d_finalize;
    object_class->get_property = lrg_shadow_occluder2d_get_property;
    object_class->set_property = lrg_shadow_occluder2d_set_property;

    /**
     * LrgShadowOccluder2D:x:
     *
     * World x of the occluder's origin.
     */
    occ_properties[OCC_PROP_X] =
        g_param_spec_float ("x", NULL, NULL, -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgShadowOccluder2D:y:
     *
     * World y of the occluder's origin.
     */
    occ_properties[OCC_PROP_Y] =
        g_param_spec_float ("y", NULL, NULL, -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgShadowOccluder2D:opacity:
     *
     * How much light the occluder's shadows remove (1: all of it).
     */
    occ_properties[OCC_PROP_OPACITY] =
        g_param_spec_float ("opacity", NULL, NULL, 0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgShadowOccluder2D:self-shadow:
     *
     * Whether the occluder's own area is shadowed (every edge projected)
     * or lit (only edges facing away from the light projected).
     */
    occ_properties[OCC_PROP_SELF_SHADOW] =
        g_param_spec_boolean ("self-shadow", NULL, NULL, FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgShadowOccluder2D:enabled:
     *
     * Whether the occluder casts shadows.
     */
    occ_properties[OCC_PROP_ENABLED] =
        g_param_spec_boolean ("enabled", NULL, NULL, TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, OCC_N_PROPS, occ_properties);
}

static void
lrg_shadow_occluder2d_init (LrgShadowOccluder2D *self)
{
    self->edges = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->index = g_hash_table_new_full (edge_key_hash, edge_key_equal, g_free, NULL);
    self->opacity = 1.0f;
    self->enabled = TRUE;
}

LrgShadowOccluder2D *
lrg_shadow_occluder2d_new (void)
{
    return g_object_new (LRG_TYPE_SHADOW_OCCLUDER2D, NULL);
}

/*
 * remove_edge_slot:
 * Removes edge @slot by moving the last edge into its place (order is
 * irrelevant for shadows) and fixing the moved edge's index entry.
 */
static void
remove_edge_slot (LrgShadowOccluder2D *self,
                  guint                slot)
{
    guint last = self->edges->len / 4 - 1;
    gfloat *e = (gfloat *) self->edges->data;
    EdgeKey key;

    edge_key_set (&key, e[slot * 4], e[slot * 4 + 1], e[slot * 4 + 2], e[slot * 4 + 3]);
    g_hash_table_remove (self->index, &key);

    if (slot != last)
    {
        memcpy (e + slot * 4, e + last * 4, 4 * sizeof (gfloat));
        edge_key_set (&key, e[slot * 4], e[slot * 4 + 1], e[slot * 4 + 2], e[slot * 4 + 3]);
        g_hash_table_insert (self->index, g_memdup2 (&key, sizeof key), GUINT_TO_POINTER (slot + 1));
    }
    g_array_set_size (self->edges, last * 4);
}

/* adds one oriented edge, cancelling its exact reverse */
static void
add_edge (LrgShadowOccluder2D *self,
          gfloat               x1,
          gfloat               y1,
          gfloat               x2,
          gfloat               y2)
{
    EdgeKey key;
    gpointer slot;
    gfloat e[4];

    if (fabsf (x2 - x1) < 1e-4f && fabsf (y2 - y1) < 1e-4f)
        return;

    edge_key_set (&key, x2, y2, x1, y1);
    if (g_hash_table_lookup_extended (self->index, &key, NULL, &slot))
    {
        remove_edge_slot (self, GPOINTER_TO_UINT (slot) - 1);
        return;
    }

    edge_key_set (&key, x1, y1, x2, y2);
    if (g_hash_table_contains (self->index, &key))
        return; /* duplicate */

    e[0] = x1;
    e[1] = y1;
    e[2] = x2;
    e[3] = y2;
    g_array_append_vals (self->edges, e, 4);
    g_hash_table_insert (self->index, g_memdup2 (&key, sizeof key),
                         GUINT_TO_POINTER (self->edges->len / 4));
}

/* signed area, positive for clockwise on a y-down screen */
static gfloat
polygon_area (const gfloat *c,
              guint         n_points)
{
    gfloat a = 0.0f;
    guint i;

    for (i = 0; i < n_points; i++)
    {
        guint j = (i + 1) % n_points;

        a += c[i * 2] * c[j * 2 + 1] - c[j * 2] * c[i * 2 + 1];
    }
    return a * 0.5f;
}

void
lrg_shadow_occluder2d_add_polygon (LrgShadowOccluder2D *self,
                                   const gfloat        *coords,
                                   guint                n_coords)
{
    guint n;
    guint i;
    gboolean reverse;

    g_return_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self));
    g_return_if_fail (coords != NULL);
    g_return_if_fail (n_coords >= 6 && n_coords % 2 == 0);

    n = n_coords / 2;
    reverse = polygon_area (coords, n) < 0.0f;
    for (i = 0; i < n; i++)
    {
        guint a = reverse ? n - 1 - i : i;
        guint b = reverse ? (2 * n - 2 - i) % n : (i + 1) % n;

        add_edge (self, coords[a * 2], coords[a * 2 + 1], coords[b * 2], coords[b * 2 + 1]);
    }
}

void
lrg_shadow_occluder2d_add_rect (LrgShadowOccluder2D *self,
                                gfloat               x,
                                gfloat               y,
                                gfloat               width,
                                gfloat               height)
{
    gfloat c[8];

    c[0] = x;          c[1] = y;
    c[2] = x + width;  c[3] = y;
    c[4] = x + width;  c[5] = y + height;
    c[6] = x;          c[7] = y + height;
    lrg_shadow_occluder2d_add_polygon (self, c, 8);
}

/* adds one layer's collision; returns the tiles added */
static guint
add_tilemap_layer (LrgShadowOccluder2D *self,
                   LrgTilemap          *tilemap,
                   LrgTilemapLayer     *layer,
                   gfloat               ox,
                   gfloat               oy)
{
    LrgTileset *tileset = lrg_tilemap_get_tileset (tilemap);
    guint width = lrg_tilemap_layer_get_width (layer);
    guint height = lrg_tilemap_layer_get_height (layer);
    gfloat tw = (gfloat) lrg_tileset_get_tile_width (tileset);
    gfloat th = (gfloat) lrg_tileset_get_tile_height (tileset);
    gint off_x = 0;
    gint off_y = 0;
    guint added = 0;
    guint x;
    guint y;

    lrg_tileset_get_draw_offset (tileset, &off_x, &off_y);

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            guint raw = lrg_tilemap_layer_get_tile (layer, x, y);
            guint id = lrg_tile_get_id (raw);
            guint n_polys;
            guint p;
            GrlRectangle box;

            if (id == 0 || id - 1 >= lrg_tileset_get_tile_count (tileset))
                continue;

            n_polys = lrg_tileset_get_tile_collision_polygon_count (tileset, id - 1);
            if (n_polys == 0)
            {
                gfloat cell[12];
                guint n;
                guint i;

                if (!lrg_tileset_tile_has_property (tileset, id - 1, LRG_TILE_PROPERTY_SOLID))
                    continue;
                n = lrg_tilemap_get_cell_polygon (tilemap, (gint) x, (gint) y, cell);
                for (i = 0; i < n; i++)
                {
                    cell[i * 2] += ox;
                    cell[i * 2 + 1] += oy;
                }
                lrg_shadow_occluder2d_add_polygon (self, cell, n * 2);
                added++;
                continue;
            }

            /*
             * Polygons are in tile-image pixels: transform them with the
             * cell (about the image centre), then place the image like the
             * renderer does: bottom-left on the cell box's bottom-left plus
             * the tileset's draw offset.
             */
            lrg_tilemap_get_cell_bounds (tilemap, (gint) x, (gint) y, &box);
            for (p = 0; p < n_polys; p++)
            {
                guint n_coords = 0;
                const gfloat *src = lrg_tileset_get_tile_collision_polygon (tileset, id - 1, p, &n_coords);
                g_autofree gfloat *dst = NULL;
                guint i;

                if (src == NULL || n_coords < 6)
                    continue;
                dst = g_new (gfloat, n_coords);
                for (i = 0; i + 1 < n_coords; i += 2)
                {
                    gfloat px = src[i];
                    gfloat py = src[i + 1];

                    lrg_tile_transform_point (raw, tw, th, &px, &py);
                    dst[i] = ox + box.x + (gfloat) off_x + px;
                    dst[i + 1] = oy + box.y + box.height - th + (gfloat) off_y + py;
                }
                lrg_shadow_occluder2d_add_polygon (self, dst, n_coords);
            }
            added++;
        }
    }
    return added;
}

guint
lrg_shadow_occluder2d_add_tilemap (LrgShadowOccluder2D *self,
                                   LrgTilemap          *tilemap,
                                   gint                 layer_index,
                                   gfloat               x,
                                   gfloat               y)
{
    guint added = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), 0);
    g_return_val_if_fail (LRG_IS_TILEMAP (tilemap), 0);

    if (lrg_tilemap_get_tileset (tilemap) == NULL)
        return 0;

    if (layer_index >= 0)
    {
        LrgTilemapLayer *layer = lrg_tilemap_get_layer (tilemap, (guint) layer_index);

        return layer != NULL ? add_tilemap_layer (self, tilemap, layer, x, y) : 0;
    }

    for (i = 0; i < lrg_tilemap_get_layer_count (tilemap); i++)
    {
        LrgTilemapLayer *layer = lrg_tilemap_get_layer (tilemap, i);

        if (lrg_tilemap_layer_get_collision_enabled (layer))
            added += add_tilemap_layer (self, tilemap, layer, x, y);
    }
    return added;
}

/* --------------------------------------------------------------------------
 * Alpha outlines
 *
 * 1. Every opaque pixel contributes its sides that border a transparent
 *    pixel (or the image edge) as directed edges running clockwise round
 *    the pixel, so opaque regions are on the right.
 * 2. Edges are chained into closed loops through their start vertices
 *    (every vertex has as many edges in as out, so loops always close).
 * 3. Collinear corners are dropped and each loop is simplified with
 *    Douglas-Peucker, split at the vertex farthest from its first one.
 * -------------------------------------------------------------------------- */

static gboolean
pixel_solid (const guint8 *rgba,
             guint         width,
             guint         height,
             guint         stride,
             guint8        threshold,
             gint          x,
             gint          y)
{
    if (x < 0 || y < 0 || (guint) x >= width || (guint) y >= height)
        return FALSE;
    return rgba[(gsize) y * stride + (gsize) x * 4 + 3] > threshold;
}

/* perpendicular distance of p from segment a-b */
static gfloat
segment_distance (const gfloat *a,
                  const gfloat *b,
                  const gfloat *p)
{
    gfloat dx = b[0] - a[0];
    gfloat dy = b[1] - a[1];
    gfloat len2 = dx * dx + dy * dy;
    gfloat t;
    gfloat qx;
    gfloat qy;

    if (len2 < 1e-12f)
        return hypotf (p[0] - a[0], p[1] - a[1]);
    t = CLAMP (((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2, 0.0f, 1.0f);
    qx = a[0] + t * dx - p[0];
    qy = a[1] + t * dy - p[1];
    return sqrtf (qx * qx + qy * qy);
}

/* Douglas-Peucker on pts[first..last] (inclusive); marks kept points */
static void
douglas_peucker (const gfloat *pts,
                 guint         first,
                 guint         last,
                 gfloat        tolerance,
                 gboolean     *keep)
{
    gfloat best = -1.0f;
    guint best_i = first;
    guint i;

    if (last <= first + 1)
        return;
    for (i = first + 1; i < last; i++)
    {
        gfloat d = segment_distance (pts + first * 2, pts + last * 2, pts + i * 2);

        if (d > best)
        {
            best = d;
            best_i = i;
        }
    }
    if (best > tolerance)
    {
        keep[best_i] = TRUE;
        douglas_peucker (pts, first, best_i, tolerance, keep);
        douglas_peucker (pts, best_i, last, tolerance, keep);
    }
}

/* simplifies a closed loop in place; returns the new point count */
static guint
simplify_loop (GArray *loop,
               gfloat  tolerance)
{
    gfloat *pts = (gfloat *) loop->data;
    guint n = loop->len / 2;
    g_autofree gboolean *keep = NULL;
    g_autoptr(GArray) out = NULL;
    gfloat best = -1.0f;
    guint far = 0;
    guint i;

    if (n < 4 || tolerance <= 0.0f)
        return n;

    /* split at the point farthest from point 0 and simplify both halves;
     * the loop is closed by repeating point 0 at the end */
    for (i = 1; i < n; i++)
    {
        gfloat d = hypotf (pts[i * 2] - pts[0], pts[i * 2 + 1] - pts[1]);

        if (d > best)
        {
            best = d;
            far = i;
        }
    }
    g_array_append_vals (loop, pts, 2);
    pts = (gfloat *) loop->data;

    keep = g_new0 (gboolean, n + 1);
    keep[0] = TRUE;
    keep[far] = TRUE;
    keep[n] = TRUE;
    douglas_peucker (pts, 0, far, tolerance, keep);
    douglas_peucker (pts, far, n, tolerance, keep);

    out = g_array_new (FALSE, FALSE, sizeof (gfloat));
    for (i = 0; i < n; i++)
        if (keep[i])
            g_array_append_vals (out, pts + i * 2, 2);
    g_array_set_size (loop, 0);
    g_array_append_vals (loop, out->data, out->len);
    return loop->len / 2;
}

guint
lrg_shadow_occluder2d_add_alpha_outline (LrgShadowOccluder2D *self,
                                         const guint8        *rgba,
                                         gsize                n_bytes,
                                         guint                width,
                                         guint                height,
                                         guint                stride,
                                         guint8               alpha_threshold,
                                         gfloat               tolerance,
                                         gfloat               x,
                                         gfloat               y,
                                         gfloat               scale)
{
    g_autoptr(GArray) edges = NULL;        /* gint x1, y1, x2, y2 */
    g_autoptr(GHashTable) starts = NULL;   /* vertex key -> GSList of edge indices */
    g_autofree gboolean *used = NULL;
    guint n_edges;
    guint loops = 0;
    guint e;
    gint px;
    gint py;

    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), 0);
    g_return_val_if_fail (rgba != NULL, 0);

    if (stride == 0)
        stride = width * 4;
    g_return_val_if_fail (width > 0 && height > 0, 0);
    g_return_val_if_fail (n_bytes >= (gsize) stride * (height - 1) + (gsize) width * 4, 0);

    /* 1. boundary edges, clockwise round each opaque pixel */
    edges = g_array_new (FALSE, FALSE, sizeof (gint));
    for (py = 0; py < (gint) height; py++)
    {
        for (px = 0; px < (gint) width; px++)
        {
            gint e4[4];

            if (!pixel_solid (rgba, width, height, stride, alpha_threshold, px, py))
                continue;
            if (!pixel_solid (rgba, width, height, stride, alpha_threshold, px, py - 1))
            {
                e4[0] = px; e4[1] = py; e4[2] = px + 1; e4[3] = py;
                g_array_append_vals (edges, e4, 4);
            }
            if (!pixel_solid (rgba, width, height, stride, alpha_threshold, px + 1, py))
            {
                e4[0] = px + 1; e4[1] = py; e4[2] = px + 1; e4[3] = py + 1;
                g_array_append_vals (edges, e4, 4);
            }
            if (!pixel_solid (rgba, width, height, stride, alpha_threshold, px, py + 1))
            {
                e4[0] = px + 1; e4[1] = py + 1; e4[2] = px; e4[3] = py + 1;
                g_array_append_vals (edges, e4, 4);
            }
            if (!pixel_solid (rgba, width, height, stride, alpha_threshold, px - 1, py))
            {
                e4[0] = px; e4[1] = py + 1; e4[2] = px; e4[3] = py;
                g_array_append_vals (edges, e4, 4);
            }
        }
    }

    /* 2. chain into loops */
    n_edges = edges->len / 4;
    if (n_edges == 0)
        return 0;
    starts = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) g_slist_free);
    for (e = 0; e < n_edges; e++)
    {
        gint *v = &g_array_index (edges, gint, e * 4);
        gpointer key = GUINT_TO_POINTER (((guint) v[1] * (width + 2u) + (guint) v[0]) + 1u);
        GSList *list = g_hash_table_lookup (starts, key);

        g_hash_table_steal (starts, key);
        g_hash_table_insert (starts, key, g_slist_prepend (list, GUINT_TO_POINTER (e)));
    }

    used = g_new0 (gboolean, n_edges);
    for (e = 0; e < n_edges; e++)
    {
        g_autoptr(GArray) loop = NULL;
        guint cur = e;
        guint n_points;
        guint i;

        if (used[e])
            continue;

        loop = g_array_new (FALSE, FALSE, sizeof (gfloat));
        while (!used[cur])
        {
            gint *v = &g_array_index (edges, gint, cur * 4);
            gpointer key;
            GSList *l;
            gfloat pt[2];

            used[cur] = TRUE;
            pt[0] = (gfloat) v[0];
            pt[1] = (gfloat) v[1];
            g_array_append_vals (loop, pt, 2);

            /* next: an unused edge starting where this one ends */
            key = GUINT_TO_POINTER (((guint) v[3] * (width + 2u) + (guint) v[2]) + 1u);
            for (l = g_hash_table_lookup (starts, key); l != NULL; l = l->next)
            {
                if (!used[GPOINTER_TO_UINT (l->data)])
                {
                    cur = GPOINTER_TO_UINT (l->data);
                    break;
                }
            }
        }

        /* 3. drop collinear corners, then simplify */
        {
            g_autoptr(GArray) clean = g_array_new (FALSE, FALSE, sizeof (gfloat));
            guint n = loop->len / 2;
            gfloat *p = (gfloat *) loop->data;

            for (i = 0; i < n; i++)
            {
                const gfloat *a = p + ((i + n - 1) % n) * 2;
                const gfloat *b = p + i * 2;
                const gfloat *c = p + ((i + 1) % n) * 2;
                gfloat cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);

                if (fabsf (cross) > 1e-6f)
                    g_array_append_vals (clean, b, 2);
            }
            g_array_set_size (loop, 0);
            g_array_append_vals (loop, clean->data, clean->len);
        }
        n_points = simplify_loop (loop, tolerance);
        if (n_points < 3)
            continue;

        /*
         * Add the loop edge by edge (keeping its winding: holes run the
         * other way, which is right for them) in occluder space.
         */
        for (i = 0; i < n_points; i++)
        {
            gfloat *a = &g_array_index (loop, gfloat, i * 2);
            gfloat *b = &g_array_index (loop, gfloat, ((i + 1) % n_points) * 2);

            add_edge (self, x + a[0] * scale, y + a[1] * scale, x + b[0] * scale, y + b[1] * scale);
        }
        loops++;
    }
    return loops;
}

guint
lrg_shadow_occluder2d_add_image_outline (LrgShadowOccluder2D *self,
                                         GrlImage            *image,
                                         const GrlRectangle  *source,
                                         guint8               alpha_threshold,
                                         gfloat               tolerance,
                                         gfloat               x,
                                         gfloat               y,
                                         gfloat               scale)
{
    guint8 *pixels;
    gsize size = 0;
    gint iw;
    gint ih;
    gint sx = 0;
    gint sy = 0;
    gint sw;
    gint sh;

    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), 0);
    g_return_val_if_fail (GRL_IS_IMAGE (image), 0);

    iw = grl_image_get_width (image);
    ih = grl_image_get_height (image);
    sw = iw;
    sh = ih;
    if (source != NULL)
    {
        sx = CLAMP ((gint) source->x, 0, iw);
        sy = CLAMP ((gint) source->y, 0, ih);
        sw = CLAMP ((gint) source->width, 0, iw - sx);
        sh = CLAMP ((gint) source->height, 0, ih - sy);
    }
    if (sw <= 0 || sh <= 0)
        return 0;

    pixels = grl_image_get_pixels (image, &size);
    if (pixels == NULL)
        return 0;

    /* a region is a sub-block of rows: offset the start, keep the stride */
    return lrg_shadow_occluder2d_add_alpha_outline (self,
                                                    pixels + (gsize) sy * (gsize) iw * 4 + (gsize) sx * 4,
                                                    size - ((gsize) sy * (gsize) iw * 4 + (gsize) sx * 4),
                                                    (guint) sw, (guint) sh, (guint) iw * 4,
                                                    alpha_threshold, tolerance, x, y, scale);
}

void
lrg_shadow_occluder2d_clear (LrgShadowOccluder2D *self)
{
    g_return_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self));

    g_array_set_size (self->edges, 0);
    g_hash_table_remove_all (self->index);
}

guint
lrg_shadow_occluder2d_get_edge_count (LrgShadowOccluder2D *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), 0);
    return self->edges->len / 4;
}

gboolean
lrg_shadow_occluder2d_get_edge (LrgShadowOccluder2D *self,
                                guint                index,
                                LrgShadowEdge       *out_edge)
{
    const gfloat *e;

    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), FALSE);
    g_return_val_if_fail (out_edge != NULL, FALSE);

    if (index >= self->edges->len / 4)
        return FALSE;
    e = &g_array_index (self->edges, gfloat, index * 4);
    out_edge->x1 = self->x + e[0];
    out_edge->y1 = self->y + e[1];
    out_edge->x2 = self->x + e[2];
    out_edge->y2 = self->y + e[3];
    return TRUE;
}

void
lrg_shadow_occluder2d_get_position (LrgShadowOccluder2D *self,
                                    gfloat              *x,
                                    gfloat              *y)
{
    g_return_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self));
    if (x != NULL)
        *x = self->x;
    if (y != NULL)
        *y = self->y;
}

void
lrg_shadow_occluder2d_set_position (LrgShadowOccluder2D *self,
                                    gfloat               x,
                                    gfloat               y)
{
    g_return_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self));

    if (self->x != x)
    {
        self->x = x;
        g_object_notify_by_pspec (G_OBJECT (self), occ_properties[OCC_PROP_X]);
    }
    if (self->y != y)
    {
        self->y = y;
        g_object_notify_by_pspec (G_OBJECT (self), occ_properties[OCC_PROP_Y]);
    }
}

gfloat
lrg_shadow_occluder2d_get_opacity (LrgShadowOccluder2D *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), 1.0f);
    return self->opacity;
}

void
lrg_shadow_occluder2d_set_opacity (LrgShadowOccluder2D *self,
                                   gfloat               opacity)
{
    g_return_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self));

    opacity = CLAMP (opacity, 0.0f, 1.0f);
    if (self->opacity == opacity)
        return;
    self->opacity = opacity;
    g_object_notify_by_pspec (G_OBJECT (self), occ_properties[OCC_PROP_OPACITY]);
}

gboolean
lrg_shadow_occluder2d_get_self_shadow (LrgShadowOccluder2D *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), FALSE);
    return self->self_shadow;
}

void
lrg_shadow_occluder2d_set_self_shadow (LrgShadowOccluder2D *self,
                                       gboolean             self_shadow)
{
    g_return_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self));

    self_shadow = !!self_shadow;
    if (self->self_shadow == self_shadow)
        return;
    self->self_shadow = self_shadow;
    g_object_notify_by_pspec (G_OBJECT (self), occ_properties[OCC_PROP_SELF_SHADOW]);
}

gboolean
lrg_shadow_occluder2d_get_enabled (LrgShadowOccluder2D *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self), FALSE);
    return self->enabled;
}

void
lrg_shadow_occluder2d_set_enabled (LrgShadowOccluder2D *self,
                                   gboolean             enabled)
{
    g_return_if_fail (LRG_IS_SHADOW_OCCLUDER2D (self));

    enabled = !!enabled;
    if (self->enabled == enabled)
        return;
    self->enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), occ_properties[OCC_PROP_ENABLED]);
}

/* LrgShadowCaster: the edges in world space */
static GPtrArray *
occluder_get_edges (LrgShadowCaster *caster)
{
    LrgShadowOccluder2D *self = LRG_SHADOW_OCCLUDER2D (caster);
    GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_shadow_edge_free);
    guint i;

    if (!self->enabled)
        return out;
    for (i = 0; i < self->edges->len / 4; i++)
    {
        const gfloat *e = &g_array_index (self->edges, gfloat, i * 4);

        g_ptr_array_add (out, lrg_shadow_edge_new (self->x + e[0], self->y + e[1],
                                                   self->x + e[2], self->y + e[3]));
    }
    return out;
}

static gboolean
occluder_is_opaque (LrgShadowCaster *caster)
{
    return LRG_SHADOW_OCCLUDER2D (caster)->opacity >= 1.0f;
}

static gfloat
occluder_get_shadow_opacity (LrgShadowCaster *caster)
{
    LrgShadowOccluder2D *self = LRG_SHADOW_OCCLUDER2D (caster);

    return self->enabled ? self->opacity : 0.0f;
}

static void
lrg_shadow_occluder2d_caster_init (LrgShadowCasterInterface *iface)
{
    iface->get_edges = occluder_get_edges;
    iface->is_opaque = occluder_is_opaque;
    iface->get_shadow_opacity = occluder_get_shadow_opacity;
}
