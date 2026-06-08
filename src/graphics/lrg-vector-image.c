/* lrg-vector-image.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVectorImage - Load and rasterize SVG vector assets.
 */

/*
 * Design notes
 * ------------
 *
 * Ownership of the shape array
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * grl_svg_load_from_file() and grl_svg_load_from_memory() return a
 * heap-allocated array of GrlVectorShape pointers (transfer full).  We
 * store both the pointer-to-pointer-array (shapes) and its length
 * (n_shapes) on the instance.  In finalize() we free each shape with
 * grl_vector_shape_free() and then g_free() the array itself.
 *
 * Source bounds computation
 * ~~~~~~~~~~~~~~~~~~~~~~~~~
 * SVG shapes carry coordinates in the SVG user-unit space.  To determine
 * the intrinsic extent of the artwork we compute the union bounding box of
 * every shape's path (via grl_path_get_bounds()).  The result is stored as
 * src_bounds (a GrlRectangle value type embedded in the struct).
 *
 * Scaling strategy inside lrg_vector_image_render()
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * We use grl_image_translate() + grl_image_scale() to map the source
 * coordinate space into the target pixel space before calling
 * grl_image_draw_svg_shapes().  The GrlImage transform stack is a
 * cumulative model-space transform: translation and scale calls are
 * composed in order.
 *
 * Without preserve_aspect (stretch mode):
 *   - translate by (-src_bounds.x, -src_bounds.y)   — move origin to (0,0)
 *   - scale by (width / src_w, height / src_h)       — map to target rect
 *
 * With preserve_aspect (letterbox/fit mode):
 *   - choose uniform scale = min(width/src_w, height/src_h)
 *   - translate by (-src_bounds.x, -src_bounds.y) to normalise origin
 *   - then scale uniformly
 *   - then translate again to centre within target
 *   Because the GrlImage transform API accumulates transforms we perform
 *   the centering offset *before* the scale (pre-multiply order):
 *
 *     total transform = T_centre · S_uniform · T_origin
 *
 *   In practice we call:
 *     grl_image_translate (img, centre_ox, centre_oy)   — pixels to centre
 *     grl_image_scale     (img, sx, sy)                 — uniform scale
 *     grl_image_translate (img, -src_x,   -src_y)       — normalise origin
 *
 *   This matches the order in which the GrlImage rasterizer applies the
 *   stack (innermost last-pushed = applied first to shape vertices).
 */

/* Use the same log domain string as the other graphics-module sources */
#define LRG_LOG_DOMAIN "Libregnum-Graphics"

#include "config.h"

#include "lrg-vector-image.h"
#include "../lrg-log.h"

/* -------------------------------------------------------------------------- */

struct _LrgVectorImage
{
    GObject parent_instance;

    /* Parsed shapes — owned; freed in finalize */
    GrlVectorShape **shapes;
    guint            n_shapes;

    /* Union bounding box of all shapes (SVG user units) */
    GrlRectangle     src_bounds;
    gboolean         src_bounds_valid;
};

G_DEFINE_TYPE (LrgVectorImage, lrg_vector_image, G_TYPE_OBJECT)

/* -------------------------------------------------------------------------- */

GQuark
lrg_vector_image_error_quark (void)
{
    return g_quark_from_static_string ("lrg-vector-image-error-quark");
}

/* -------------------------------------------------------------------------- */
/* Private helpers                                                             */
/* -------------------------------------------------------------------------- */

/*
 * compute_union_bounds:
 *
 * Walk every shape in @shapes and expand @out to contain all of their
 * path bounding boxes.  Returns %TRUE if at least one shape yielded a
 * valid bound.
 */
static gboolean
compute_union_bounds (GrlVectorShape * const *shapes,
                      guint                   n,
                      GrlRectangle           *out)
{
    gboolean have_bound;
    guint    i;

    have_bound = FALSE;

    for (i = 0; i < n; i++)
    {
        GrlPath      *path;
        GrlRectangle  b;
        gfloat        right;
        gfloat        bottom;

        path = grl_vector_shape_get_path (shapes[i]);
        if (path == NULL)
            continue;

        grl_path_get_bounds (path, &b);

        if (!have_bound)
        {
            *out       = b;
            have_bound = TRUE;
            continue;
        }

        /* Expand the running union */
        if (b.x < out->x)
        {
            out->width += (out->x - b.x);
            out->x      = b.x;
        }
        if (b.y < out->y)
        {
            out->height += (out->y - b.y);
            out->y       = b.y;
        }

        right  = b.x + b.width;
        bottom = b.y + b.height;

        if (right > out->x + out->width)
            out->width = right - out->x;
        if (bottom > out->y + out->height)
            out->height = bottom - out->y;
    }

    return have_bound;
}

/*
 * init_from_shapes:
 *
 * Called after @self->shapes / @self->n_shapes have been set.
 * Computes the source bounds and populates @self->src_bounds.
 */
/*
 * parse_svg_canvas:
 *
 * Extracts the SVG's coordinate extent from the root <svg> element so that
 * author-intended margins are preserved: prefers viewBox="minx miny w h",
 * else the width/height attributes (numeric prefix; unit suffixes like "px"
 * are ignored by g_ascii_strtod). Returns %TRUE and fills @out on success.
 * GLib-only string handling (no <string.h>).
 */
static gboolean
parse_svg_canvas (const gchar  *text,
                  gssize        len,
                  GrlRectangle *out)
{
    const gchar *svg;
    const gchar *tag_end;
    gchar       *region;
    const gchar *vb;
    const gchar *wp;
    const gchar *hp;
    gboolean     found = FALSE;

    if (text == NULL)
        return FALSE;

    svg = g_strstr_len (text, len, "<svg");
    if (svg == NULL)
        return FALSE;

    tag_end = g_strstr_len (svg, -1, ">");
    if (tag_end == NULL)
        return FALSE;

    region = g_strndup (svg, (gsize) (tag_end - svg));

    /* viewBox="minx miny w h" takes precedence over width/height. */
    vb = g_strstr_len (region, -1, "viewBox");
    if (vb != NULL)
    {
        const gchar *q = g_strstr_len (vb, -1, "\"");
        const gchar *q2 = g_strstr_len (vb, -1, "'");

        if (q == NULL || (q2 != NULL && q2 < q))
            q = q2;

        if (q != NULL)
        {
            gdouble      nums[4];
            gint         count = 0;
            const gchar *p = q + 1;

            while (count < 4)
            {
                gchar *endp = NULL;

                while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n')
                    p++;
                nums[count] = g_ascii_strtod (p, &endp);
                if (endp == p)
                    break;
                p = endp;
                count++;
            }

            if (count == 4 && nums[2] > 0.0 && nums[3] > 0.0)
            {
                out->x      = (gfloat) nums[0];
                out->y      = (gfloat) nums[1];
                out->width  = (gfloat) nums[2];
                out->height = (gfloat) nums[3];
                found = TRUE;
            }
        }
    }

    if (!found)
    {
        gdouble w = 0.0;
        gdouble h = 0.0;

        wp = g_strstr_len (region, -1, "width");
        hp = g_strstr_len (region, -1, "height");
        if (wp != NULL)
        {
            const gchar *q = g_strstr_len (wp, -1, "\"");
            const gchar *q2 = g_strstr_len (wp, -1, "'");
            if (q == NULL || (q2 != NULL && q2 < q)) q = q2;
            if (q != NULL) w = g_ascii_strtod (q + 1, NULL);
        }
        if (hp != NULL)
        {
            const gchar *q = g_strstr_len (hp, -1, "\"");
            const gchar *q2 = g_strstr_len (hp, -1, "'");
            if (q == NULL || (q2 != NULL && q2 < q)) q = q2;
            if (q != NULL) h = g_ascii_strtod (q + 1, NULL);
        }
        if (w > 0.0 && h > 0.0)
        {
            out->x      = 0.0f;
            out->y      = 0.0f;
            out->width  = (gfloat) w;
            out->height = (gfloat) h;
            found = TRUE;
        }
    }

    g_free (region);
    return found;
}

static void
init_from_shapes (LrgVectorImage *self,
                  const gchar    *svg_text,
                  gssize          svg_len)
{
    GrlRectangle bounds;
    GrlRectangle canvas;

    bounds.x      = 0.0f;
    bounds.y      = 0.0f;
    bounds.width  = 0.0f;
    bounds.height = 0.0f;

    self->src_bounds_valid = compute_union_bounds (
        (GrlVectorShape * const *) self->shapes,
        self->n_shapes,
        &bounds);

    self->src_bounds = bounds;

    /* Prefer the SVG's declared canvas (viewBox / width+height) so that the
     * author's intended coordinate space and margins are preserved; fall back
     * to the content union bounds when the document declares no extent. */
    if (parse_svg_canvas (svg_text, svg_len, &canvas))
    {
        self->src_bounds       = canvas;
        self->src_bounds_valid = TRUE;
    }

    lrg_log_debug ("LrgVectorImage: loaded %u shapes, "
                   "src_bounds=(%.2f,%.2f, %.2fx%.2f), valid=%s",
                   self->n_shapes,
                   self->src_bounds.x,
                   self->src_bounds.y,
                   self->src_bounds.width,
                   self->src_bounds.height,
                   self->src_bounds_valid ? "yes" : "no");
}

/*
 * do_render:
 *
 * Internal rasterization.  Creates a @width × @height RGBA GrlImage,
 * optionally fills it with @bg, sets up the coordinate transform, and
 * calls grl_image_draw_svg_shapes().
 */
static GrlImage *
do_render (LrgVectorImage  *self,
           gint             width,
           gint             height,
           const GrlColor  *bg_or_null,
           gboolean         preserve_aspect)
{
    g_autoptr(GrlImage) img  = NULL;
    g_autoptr(GrlColor) fill = NULL;
    gfloat              src_x;
    gfloat              src_y;
    gfloat              src_w;
    gfloat              src_h;
    gfloat              sx;
    gfloat              sy;

    /*
     * grl_image_new_color() takes a GrlColor* (non-const) and uses it only
     * for the initial fill; it does not take ownership.  We either copy the
     * caller's colour or create a fully-transparent one.
     */
    if (bg_or_null != NULL)
        fill = grl_color_copy (bg_or_null);
    else
        fill = grl_color_new (0, 0, 0, 0);

    img = grl_image_new_color (width, height, fill);

    if (img == NULL)
    {
        lrg_log_debug ("LrgVectorImage: grl_image_new_color returned NULL");
        return NULL;
    }

    grl_image_set_antialias (img, TRUE);

    /* Determine source extent.  Fall back to (0,0,1,1) so we do not
     * divide by zero even with a degenerate SVG. */
    if (self->src_bounds_valid &&
        self->src_bounds.width  > 0.0f &&
        self->src_bounds.height > 0.0f)
    {
        src_x = self->src_bounds.x;
        src_y = self->src_bounds.y;
        src_w = self->src_bounds.width;
        src_h = self->src_bounds.height;
    }
    else
    {
        src_x = 0.0f;
        src_y = 0.0f;
        src_w = 1.0f;
        src_h = 1.0f;
    }

    if (preserve_aspect)
    {
        gfloat scale;
        gfloat ox;
        gfloat oy;

        /* Uniform scale: fit the source inside the target rect */
        sx = (gfloat) width  / src_w;
        sy = (gfloat) height / src_h;
        scale = (sx < sy) ? sx : sy;

        /* Centre offset in target pixels */
        ox = ((gfloat) width  - src_w * scale) / 2.0f;
        oy = ((gfloat) height - src_h * scale) / 2.0f;

        /*
         * Set up transform stack (applied in push order, innermost first):
         *
         *   1. grl_image_translate(-src_x, -src_y)  — move SVG origin to (0,0)
         *   2. grl_image_scale(scale, scale)         — scale to target pixel size
         *   3. grl_image_translate(ox, oy)           — centre in target
         *
         * The rasterizer applies the stack from bottom to top (last-set
         * innermost), so vertices are processed as: T_origin · S · T_centre.
         */
        grl_image_translate (img, ox,     oy);
        grl_image_scale     (img, scale,  scale);
        grl_image_translate (img, -src_x, -src_y);
    }
    else
    {
        /* Stretch mode: map source bounds directly to target rect */
        sx = (gfloat) width  / src_w;
        sy = (gfloat) height / src_h;

        grl_image_scale     (img, sx,     sy);
        grl_image_translate (img, -src_x, -src_y);
    }

    grl_image_draw_svg_shapes (img,
                               (GrlVectorShape * const *) self->shapes,
                               self->n_shapes);

    lrg_log_debug ("LrgVectorImage: rendered %ux%u (preserve_aspect=%s)",
                   (guint) width, (guint) height,
                   preserve_aspect ? "yes" : "no");

    return g_steal_pointer (&img);
}

/* -------------------------------------------------------------------------- */
/* GObject implementation                                                      */
/* -------------------------------------------------------------------------- */

static void
lrg_vector_image_finalize (GObject *object)
{
    LrgVectorImage *self = LRG_VECTOR_IMAGE (object);
    guint           i;

    if (self->shapes != NULL)
    {
        for (i = 0; i < self->n_shapes; i++)
            grl_vector_shape_free (self->shapes[i]);
        g_free (self->shapes);
        self->shapes   = NULL;
        self->n_shapes = 0;
    }

    G_OBJECT_CLASS (lrg_vector_image_parent_class)->finalize (object);
}

static void
lrg_vector_image_class_init (LrgVectorImageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_vector_image_finalize;
}

static void
lrg_vector_image_init (LrgVectorImage *self)
{
    self->shapes           = NULL;
    self->n_shapes         = 0;
    self->src_bounds.x     = 0.0f;
    self->src_bounds.y     = 0.0f;
    self->src_bounds.width  = 0.0f;
    self->src_bounds.height = 0.0f;
    self->src_bounds_valid  = FALSE;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * lrg_vector_image_new_from_file:
 * @filename: path to the SVG file
 * @error: (nullable): return location for a #GError
 *
 * Loads an SVG file from disk.
 *
 * Returns: (transfer full) (nullable): a new #LrgVectorImage, or %NULL on error
 */
LrgVectorImage *
lrg_vector_image_new_from_file (const gchar  *filename,
                                GError      **error)
{
    LrgVectorImage  *self;
    GrlVectorShape **shapes;
    guint            n;
    gchar           *contents = NULL;
    gsize            clen = 0;

    g_return_val_if_fail (filename != NULL, NULL);

    /* Read the file once: we need the text both to load shapes and to read the
     * root <svg> canvas extent (so author margins are preserved on render). */
    if (!g_file_get_contents (filename, &contents, &clen, error))
        return NULL;

    shapes = grl_svg_load_from_memory (contents,
                                       clen,
                                       96.0f,   /* standard screen DPI */
                                       &n,
                                       error);
    if (shapes == NULL)
    {
        /* error is already set by graylib */
        if (error != NULL && *error == NULL)
        {
            g_set_error (error,
                         LRG_VECTOR_IMAGE_ERROR,
                         LRG_VECTOR_IMAGE_ERROR_LOAD,
                         "Failed to load SVG from file: %s", filename);
        }
        g_free (contents);
        return NULL;
    }

    self           = g_object_new (LRG_TYPE_VECTOR_IMAGE, NULL);
    self->shapes   = shapes;
    self->n_shapes = n;

    init_from_shapes (self, contents, (gssize) clen);

    g_free (contents);
    return self;
}

/**
 * lrg_vector_image_new_from_data:
 * @data: (array length=len): SVG source bytes
 * @len: length of @data in bytes
 * @error: (nullable): return location for a #GError
 *
 * Loads an SVG from an in-memory buffer.
 *
 * Returns: (transfer full) (nullable): a new #LrgVectorImage, or %NULL on error
 */
LrgVectorImage *
lrg_vector_image_new_from_data (const gchar  *data,
                                gsize         len,
                                GError      **error)
{
    LrgVectorImage  *self;
    GrlVectorShape **shapes;
    guint            n;

    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (len > 0, NULL);

    shapes = grl_svg_load_from_memory (data,
                                       len,
                                       96.0f,   /* standard screen DPI */
                                       &n,
                                       error);
    if (shapes == NULL)
    {
        if (error != NULL && *error == NULL)
        {
            g_set_error (error,
                         LRG_VECTOR_IMAGE_ERROR,
                         LRG_VECTOR_IMAGE_ERROR_LOAD,
                         "Failed to load SVG from memory (%"G_GSIZE_FORMAT" bytes)", len);
        }
        return NULL;
    }

    self           = g_object_new (LRG_TYPE_VECTOR_IMAGE, NULL);
    self->shapes   = shapes;
    self->n_shapes = n;

    init_from_shapes (self, data, (gssize) len);

    return self;
}

/**
 * lrg_vector_image_get_source_size:
 * @self: an #LrgVectorImage
 * @out_w: (out) (nullable): return location for the intrinsic width
 * @out_h: (out) (nullable): return location for the intrinsic height
 *
 * Returns the intrinsic size derived from the union bounding box of all shapes.
 *
 * Returns: %TRUE if a valid size is available
 */
gboolean
lrg_vector_image_get_source_size (LrgVectorImage *self,
                                  gfloat         *out_w,
                                  gfloat         *out_h)
{
    g_return_val_if_fail (LRG_IS_VECTOR_IMAGE (self), FALSE);

    if (out_w != NULL)
        *out_w = self->src_bounds_valid ? self->src_bounds.width  : 0.0f;
    if (out_h != NULL)
        *out_h = self->src_bounds_valid ? self->src_bounds.height : 0.0f;

    return self->src_bounds_valid;
}

/**
 * lrg_vector_image_get_shape_count:
 * @self: an #LrgVectorImage
 *
 * Returns the number of #GrlVectorShape objects parsed from the SVG.
 *
 * Returns: number of shapes
 */
guint
lrg_vector_image_get_shape_count (LrgVectorImage *self)
{
    g_return_val_if_fail (LRG_IS_VECTOR_IMAGE (self), 0);

    return self->n_shapes;
}

/**
 * lrg_vector_image_render:
 * @self: an #LrgVectorImage
 * @width: target width in pixels (must be > 0)
 * @height: target height in pixels (must be > 0)
 * @bg_or_null: (nullable): background fill colour
 * @preserve_aspect: if %TRUE, letterbox the artwork
 *
 * Rasterizes the vector image to a new #GrlImage.
 *
 * Returns: (transfer full) (nullable): a new #GrlImage, or %NULL on error
 */
GrlImage *
lrg_vector_image_render (LrgVectorImage  *self,
                         gint             width,
                         gint             height,
                         const GrlColor  *bg_or_null,
                         gboolean         preserve_aspect)
{
    g_return_val_if_fail (LRG_IS_VECTOR_IMAGE (self), NULL);

    if (width <= 0 || height <= 0)
    {
        lrg_log_debug ("LrgVectorImage: render called with non-positive dimensions (%dx%d)",
                       width, height);
        return NULL;
    }

    return do_render (self, width, height, bg_or_null, preserve_aspect);
}

/**
 * lrg_vector_image_render_to_texture:
 * @self: an #LrgVectorImage
 * @width: target width in pixels (must be > 0)
 * @height: target height in pixels (must be > 0)
 * @bg_or_null: (nullable): background fill colour
 * @preserve_aspect: if %TRUE, letterbox the artwork
 *
 * Rasterizes the vector image and uploads it to the GPU as a #GrlTexture.
 *
 * Returns: (transfer full) (nullable): a new #GrlTexture, or %NULL on error
 */
GrlTexture *
lrg_vector_image_render_to_texture (LrgVectorImage  *self,
                                    gint             width,
                                    gint             height,
                                    const GrlColor  *bg_or_null,
                                    gboolean         preserve_aspect)
{
    g_autoptr(GrlImage) image = NULL;

    g_return_val_if_fail (LRG_IS_VECTOR_IMAGE (self), NULL);

    if (width <= 0 || height <= 0)
    {
        lrg_log_debug ("LrgVectorImage: render_to_texture called with non-positive dimensions (%dx%d)",
                       width, height);
        return NULL;
    }

    image = do_render (self, width, height, bg_or_null, preserve_aspect);
    if (image == NULL)
        return NULL;

    return grl_texture_new_from_image (image);
}
