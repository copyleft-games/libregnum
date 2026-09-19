/* lrg-atlas-packer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Build-time texture atlas packer.
 */

#include <gio/gio.h>
#include <math.h>

#include "lrg-atlas-packer.h"

/**
 * LrgAtlasPackerImage:
 *
 * Internal structure for images to be packed.
 */
struct _LrgAtlasPackerImage
{
    gchar    *name;
    gint      width;
    gint      height;
    gpointer  user_data;

    /* Packed result (set after pack()) */
    gint      packed_x;
    gint      packed_y;
    gboolean  rotated;
    gboolean  packed;
};

static LrgAtlasPackerImage *
packer_image_new (const gchar *name,
                  gint         width,
                  gint         height,
                  gpointer     user_data)
{
    LrgAtlasPackerImage *image;

    image = g_new0 (LrgAtlasPackerImage, 1);
    image->name = g_strdup (name);
    image->width = width;
    image->height = height;
    image->user_data = user_data;
    image->packed_x = 0;
    image->packed_y = 0;
    image->rotated = FALSE;
    image->packed = FALSE;

    return image;
}

static void
packer_image_free (LrgAtlasPackerImage *image)
{
    if (image == NULL)
        return;

    g_free (image->name);
    g_free (image);
}

/**
 * ShelfRow:
 *
 * A row in the shelf packing algorithm.
 */
typedef struct
{
    gint y;       /* Y position of this shelf */
    gint height;  /* Height of this shelf */
    gint x_used;  /* How much X space is used */
} ShelfRow;

struct _LrgAtlasPacker
{
    GObject parent_instance;

    /* Configuration */
    gint                max_width;
    gint                max_height;
    gint                padding;
    LrgAtlasPackMethod  method;
    gboolean            power_of_two;
    gboolean            allow_rotation;

    /* Images to pack */
    GPtrArray  *images;         /* LrgAtlasPackerImage* */
    GHashTable *images_by_name; /* name -> LrgAtlasPackerImage* */

    /* Packed result */
    gint packed_width;
    gint packed_height;
    gboolean is_packed;
};

G_DEFINE_FINAL_TYPE (LrgAtlasPacker, lrg_atlas_packer, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MAX_WIDTH,
    PROP_MAX_HEIGHT,
    PROP_PADDING,
    PROP_METHOD,
    PROP_POWER_OF_TWO,
    PROP_ALLOW_ROTATION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_atlas_packer_finalize (GObject *object)
{
    LrgAtlasPacker *self = LRG_ATLAS_PACKER (object);

    g_clear_pointer (&self->images, g_ptr_array_unref);
    g_clear_pointer (&self->images_by_name, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_atlas_packer_parent_class)->finalize (object);
}

static void
lrg_atlas_packer_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgAtlasPacker *self = LRG_ATLAS_PACKER (object);

    switch (prop_id)
    {
    case PROP_MAX_WIDTH:
        g_value_set_int (value, self->max_width);
        break;
    case PROP_MAX_HEIGHT:
        g_value_set_int (value, self->max_height);
        break;
    case PROP_PADDING:
        g_value_set_int (value, self->padding);
        break;
    case PROP_METHOD:
        g_value_set_int (value, self->method);
        break;
    case PROP_POWER_OF_TWO:
        g_value_set_boolean (value, self->power_of_two);
        break;
    case PROP_ALLOW_ROTATION:
        g_value_set_boolean (value, self->allow_rotation);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_atlas_packer_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgAtlasPacker *self = LRG_ATLAS_PACKER (object);

    self->is_packed = FALSE;

    switch (prop_id)
    {
    case PROP_MAX_WIDTH:
        self->max_width = g_value_get_int (value);
        break;
    case PROP_MAX_HEIGHT:
        self->max_height = g_value_get_int (value);
        break;
    case PROP_PADDING:
        self->padding = g_value_get_int (value);
        break;
    case PROP_METHOD:
        self->method = g_value_get_int (value);
        break;
    case PROP_POWER_OF_TWO:
        self->power_of_two = g_value_get_boolean (value);
        break;
    case PROP_ALLOW_ROTATION:
        self->allow_rotation = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_atlas_packer_class_init (LrgAtlasPackerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_atlas_packer_finalize;
    object_class->get_property = lrg_atlas_packer_get_property;
    object_class->set_property = lrg_atlas_packer_set_property;

    /**
     * LrgAtlasPacker:max-width:
     *
     * Maximum atlas width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_WIDTH] =
        g_param_spec_int ("max-width", NULL, NULL,
                          1, G_MAXINT, 4096,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAtlasPacker:max-height:
     *
     * Maximum atlas height in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_HEIGHT] =
        g_param_spec_int ("max-height", NULL, NULL,
                          1, G_MAXINT, 4096,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAtlasPacker:padding:
     *
     * Padding between packed images in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_PADDING] =
        g_param_spec_int ("padding", NULL, NULL,
                          0, G_MAXINT, 1,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAtlasPacker:method:
     *
     * The packing algorithm to use.
     *
     * Since: 1.0
     */
    properties[PROP_METHOD] =
        g_param_spec_int ("method", NULL, NULL,
                          LRG_ATLAS_PACK_METHOD_SHELF,
                          LRG_ATLAS_PACK_METHOD_GUILLOTINE,
                          LRG_ATLAS_PACK_METHOD_SHELF,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAtlasPacker:power-of-two:
     *
     * Whether output dimensions should be power-of-two.
     *
     * Since: 1.0
     */
    properties[PROP_POWER_OF_TWO] =
        g_param_spec_boolean ("power-of-two", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAtlasPacker:allow-rotation:
     *
     * Whether images can be rotated 90 degrees.
     *
     * Since: 1.0
     */
    properties[PROP_ALLOW_ROTATION] =
        g_param_spec_boolean ("allow-rotation", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_atlas_packer_init (LrgAtlasPacker *self)
{
    self->max_width = 4096;
    self->max_height = 4096;
    self->padding = 1;
    self->method = LRG_ATLAS_PACK_METHOD_SHELF;
    self->power_of_two = TRUE;
    self->allow_rotation = FALSE;

    self->images = g_ptr_array_new_with_free_func ((GDestroyNotify) packer_image_free);
    self->images_by_name = g_hash_table_new (g_str_hash, g_str_equal);
}

/**
 * lrg_atlas_packer_new:
 *
 * Creates a new atlas packer.
 *
 * Returns: (transfer full): A new #LrgAtlasPacker
 *
 * Since: 1.0
 */
LrgAtlasPacker *
lrg_atlas_packer_new (void)
{
    return g_object_new (LRG_TYPE_ATLAS_PACKER, NULL);
}

/**
 * lrg_atlas_packer_set_max_size:
 * @self: A #LrgAtlasPacker
 * @width: Maximum atlas width
 * @height: Maximum atlas height
 *
 * Sets the maximum atlas dimensions.
 *
 * Since: 1.0
 */
void
lrg_atlas_packer_set_max_size (LrgAtlasPacker *self,
                               gint            width,
                               gint            height)
{
    g_return_if_fail (LRG_IS_ATLAS_PACKER (self));

    self->max_width = width;
    self->max_height = height;
    self->is_packed = FALSE;
}

/**
 * lrg_atlas_packer_get_max_width:
 * @self: A #LrgAtlasPacker
 *
 * Gets the maximum atlas width.
 *
 * Returns: Maximum width in pixels
 *
 * Since: 1.0
 */
gint
lrg_atlas_packer_get_max_width (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0);
    return self->max_width;
}

/**
 * lrg_atlas_packer_get_max_height:
 * @self: A #LrgAtlasPacker
 *
 * Gets the maximum atlas height.
 *
 * Returns: Maximum height in pixels
 *
 * Since: 1.0
 */
gint
lrg_atlas_packer_get_max_height (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0);
    return self->max_height;
}

/**
 * lrg_atlas_packer_set_padding:
 * @self: A #LrgAtlasPacker
 * @padding: Padding between images in pixels
 *
 * Sets the padding between packed images.
 *
 * Since: 1.0
 */
void
lrg_atlas_packer_set_padding (LrgAtlasPacker *self,
                              gint            padding)
{
    g_return_if_fail (LRG_IS_ATLAS_PACKER (self));

    self->padding = padding;
    self->is_packed = FALSE;
}

/**
 * lrg_atlas_packer_get_padding:
 * @self: A #LrgAtlasPacker
 *
 * Gets the padding between packed images.
 *
 * Returns: Padding in pixels
 *
 * Since: 1.0
 */
gint
lrg_atlas_packer_get_padding (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0);
    return self->padding;
}

/**
 * lrg_atlas_packer_set_method:
 * @self: A #LrgAtlasPacker
 * @method: The packing algorithm to use
 *
 * Sets the packing algorithm.
 *
 * Since: 1.0
 */
void
lrg_atlas_packer_set_method (LrgAtlasPacker   *self,
                             LrgAtlasPackMethod method)
{
    g_return_if_fail (LRG_IS_ATLAS_PACKER (self));

    self->method = method;
    self->is_packed = FALSE;
}

/**
 * lrg_atlas_packer_get_method:
 * @self: A #LrgAtlasPacker
 *
 * Gets the packing algorithm.
 *
 * Returns: The packing method
 *
 * Since: 1.0
 */
LrgAtlasPackMethod
lrg_atlas_packer_get_method (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), LRG_ATLAS_PACK_METHOD_SHELF);
    return self->method;
}

/**
 * lrg_atlas_packer_set_power_of_two:
 * @self: A #LrgAtlasPacker
 * @power_of_two: Whether to use power-of-two dimensions
 *
 * Sets whether the output atlas should have power-of-two dimensions.
 *
 * Since: 1.0
 */
void
lrg_atlas_packer_set_power_of_two (LrgAtlasPacker *self,
                                   gboolean        power_of_two)
{
    g_return_if_fail (LRG_IS_ATLAS_PACKER (self));

    self->power_of_two = power_of_two;
    self->is_packed = FALSE;
}

/**
 * lrg_atlas_packer_get_power_of_two:
 * @self: A #LrgAtlasPacker
 *
 * Gets whether power-of-two dimensions are required.
 *
 * Returns: %TRUE if power-of-two is enabled
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_packer_get_power_of_two (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), FALSE);
    return self->power_of_two;
}

/**
 * lrg_atlas_packer_set_allow_rotation:
 * @self: A #LrgAtlasPacker
 * @allow: Whether to allow 90-degree rotation
 *
 * Sets whether images can be rotated 90 degrees to fit better.
 *
 * Since: 1.0
 */
void
lrg_atlas_packer_set_allow_rotation (LrgAtlasPacker *self,
                                     gboolean        allow)
{
    g_return_if_fail (LRG_IS_ATLAS_PACKER (self));

    self->allow_rotation = allow;
    self->is_packed = FALSE;
}

/**
 * lrg_atlas_packer_get_allow_rotation:
 * @self: A #LrgAtlasPacker
 *
 * Gets whether rotation is allowed.
 *
 * Returns: %TRUE if rotation is allowed
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_packer_get_allow_rotation (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), FALSE);
    return self->allow_rotation;
}

/**
 * lrg_atlas_packer_add_image:
 * @self: A #LrgAtlasPacker
 * @name: Unique name for the image (used as region name)
 * @width: Image width in pixels
 * @height: Image height in pixels
 * @user_data: (nullable): User data to associate with this image
 *
 * Adds an image to be packed. The actual image data is not stored;
 * only dimensions are needed for packing.
 *
 * Returns: %TRUE if the image was added
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_packer_add_image (LrgAtlasPacker *self,
                            const gchar    *name,
                            gint            width,
                            gint            height,
                            gpointer        user_data)
{
    LrgAtlasPackerImage *image;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (width > 0, FALSE);
    g_return_val_if_fail (height > 0, FALSE);

    /* Check for duplicate name */
    if (g_hash_table_contains (self->images_by_name, name))
    {
        g_warning ("Image '%s' already exists in packer", name);
        return FALSE;
    }

    image = packer_image_new (name, width, height, user_data);
    g_ptr_array_add (self->images, image);
    g_hash_table_insert (self->images_by_name, image->name, image);

    self->is_packed = FALSE;

    return TRUE;
}

/**
 * lrg_atlas_packer_remove_image:
 * @self: A #LrgAtlasPacker
 * @name: Name of the image to remove
 *
 * Removes an image from the packer.
 *
 * Returns: %TRUE if the image was found and removed
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_packer_remove_image (LrgAtlasPacker *self,
                               const gchar    *name)
{
    LrgAtlasPackerImage *image;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    image = g_hash_table_lookup (self->images_by_name, name);
    if (image == NULL)
        return FALSE;

    g_hash_table_remove (self->images_by_name, name);
    g_ptr_array_remove (self->images, image);

    self->is_packed = FALSE;

    return TRUE;
}

/**
 * lrg_atlas_packer_clear_images:
 * @self: A #LrgAtlasPacker
 *
 * Removes all images from the packer.
 *
 * Since: 1.0
 */
void
lrg_atlas_packer_clear_images (LrgAtlasPacker *self)
{
    g_return_if_fail (LRG_IS_ATLAS_PACKER (self));

    g_ptr_array_set_size (self->images, 0);
    g_hash_table_remove_all (self->images_by_name);

    self->is_packed = FALSE;
    self->packed_width = 0;
    self->packed_height = 0;
}

/**
 * lrg_atlas_packer_get_image_count:
 * @self: A #LrgAtlasPacker
 *
 * Gets the number of images to pack.
 *
 * Returns: Image count
 *
 * Since: 1.0
 */
guint
lrg_atlas_packer_get_image_count (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0);
    return self->images->len;
}

/*
 * next_power_of_two:
 *
 * Rounds up to the next power of two.
 */
static gint
next_power_of_two (gint n)
{
    gint power = 1;
    while (power < n && power <= G_MAXINT / 2)
        power *= 2;
    return power;
}

/*
 * compare_images_by_height_desc:
 *
 * Comparison function for sorting images by height (descending).
 */
static gint
compare_images_by_height_desc (gconstpointer a,
                               gconstpointer b)
{
    const LrgAtlasPackerImage *img_a = *(const LrgAtlasPackerImage **)a;
    const LrgAtlasPackerImage *img_b = *(const LrgAtlasPackerImage **)b;

    if (img_a->height != img_b->height)
        return img_b->height - img_a->height;
    if (img_a->width != img_b->width)
        return img_b->width - img_a->width;
    return g_strcmp0 (img_a->name, img_b->name);
}

/*
 * pack_shelf:
 *
 * Shelf packing algorithm. Simple but decent results.
 * Sorts images by height and packs into horizontal rows.
 */
typedef struct
{
    gint x, y, w, h;
} PackRect;

static gint
packing_limit (gint maximum,
               gboolean power_of_two)
{
    gint power = 1;

    if (!power_of_two)
        return maximum;
    while (power <= maximum / 2)
        power *= 2;
    return power;
}

static void
append_rect (GArray *rects,
             gint x, gint y, gint w, gint h)
{
    PackRect rect = { x, y, w, h };

    if (w > 0 && h > 0)
        g_array_append_val (rects, rect);
}

static gboolean
contains_rect (PackRect a,
               PackRect b)
{
    return a.x <= b.x && a.y <= b.y &&
           a.x + a.w >= b.x + b.w && a.y + a.h >= b.y + b.h;
}

static void
split_maxrects (GArray *free_rects,
                PackRect used)
{
    guint i;
    guint j;
    guint old_len = free_rects->len;

    /* Split every intersecting free rectangle, not only the selected one. */
    for (i = 0; i < old_len; )
    {
        PackRect rect = g_array_index (free_rects, PackRect, i);

        if (used.x >= rect.x + rect.w || used.x + used.w <= rect.x ||
            used.y >= rect.y + rect.h || used.y + used.h <= rect.y)
        {
            i++;
            continue;
        }
        g_array_remove_index (free_rects, i);
        old_len--;
        if (used.x > rect.x)
            append_rect (free_rects, rect.x, rect.y, used.x - rect.x, rect.h);
        if (used.x + used.w < rect.x + rect.w)
            append_rect (free_rects, used.x + used.w, rect.y,
                         rect.x + rect.w - used.x - used.w, rect.h);
        if (used.y > rect.y)
            append_rect (free_rects, rect.x, rect.y, rect.w, used.y - rect.y);
        if (used.y + used.h < rect.y + rect.h)
            append_rect (free_rects, rect.x, used.y + used.h, rect.w,
                         rect.y + rect.h - used.y - used.h);
    }
    /* Maximal free rectangles can overlap, but contained ones are redundant. */
    for (i = 0; i < free_rects->len; )
    {
        gboolean redundant = FALSE;

        for (j = 0; j < free_rects->len; j++)
        {
            if (i != j && contains_rect (g_array_index (free_rects, PackRect, j),
                                         g_array_index (free_rects, PackRect, i)))
            {
                redundant = TRUE;
                break;
            }
        }
        if (redundant)
            g_array_remove_index (free_rects, i);
        else
            i++;
    }
}

static gboolean
pack_rectangles (LrgAtlasPacker *self,
                 gint limit_w,
                 gint limit_h,
                 GError **error)
{
    g_autoptr(GPtrArray) sorted = g_ptr_array_new ();
    g_autoptr(GArray) free_rects = g_array_new (FALSE, FALSE, sizeof (PackRect));
    g_autoptr(GArray) shelves = g_array_new (FALSE, FALSE, sizeof (ShelfRow));
    gint total_w = 0;
    gint total_h = 0;
    guint i;

    append_rect (free_rects, 0, 0, limit_w, limit_h);
    for (i = 0; i < self->images->len; i++)
        g_ptr_array_add (sorted, g_ptr_array_index (self->images, i));
    g_ptr_array_sort (sorted, compare_images_by_height_desc);

    for (i = 0; i < sorted->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (sorted, i);
        PackRect placed = { 0, 0, 0, 0 };
        gint best = -1;
        gint64 best_score = G_MAXINT64;
        gint64 best_secondary = G_MAXINT64;
        gboolean rotated = FALSE;
        guint orientation;
        guint j;

        for (orientation = 0; orientation < (self->allow_rotation ? 2u : 1u); orientation++)
        {
            gint64 wide = (orientation ? image->height : image->width);
            gint64 high = (orientation ? image->width : image->height);
            gint w, h;

            wide += self->padding;
            high += self->padding;
            if (wide > limit_w || high > limit_h)
                continue;
            w = (gint)wide;
            h = (gint)high;
            if (self->method == LRG_ATLAS_PACK_METHOD_SHELF)
            {
                for (j = 0; j <= shelves->len; j++)
                {
                    ShelfRow row = { total_h, h, 0 };
                    gint64 score;

                    if (j < shelves->len)
                        row = g_array_index (shelves, ShelfRow, j);
                    if (w > limit_w - row.x_used || h > row.height || h > limit_h - row.y)
                        continue;
                    score = (gint64)row.y * limit_w + row.x_used;
                    if (score < best_score)
                    {
                        best_score = score;
                        best = (gint)j;
                        placed = (PackRect){ row.x_used, row.y, w, h };
                        rotated = orientation != 0;
                    }
                }
            }
            else
            {
                for (j = 0; j < free_rects->len; j++)
                {
                    PackRect rect = g_array_index (free_rects, PackRect, j);
                    gint64 score;
                    gint64 secondary;

                    if (w > rect.w || h > rect.h)
                        continue;
                    score = self->method == LRG_ATLAS_PACK_METHOD_MAXRECTS
                        ? MIN (rect.w - w, rect.h - h)
                        : (gint64)rect.w * rect.h - (gint64)w * h;
                    secondary = MAX (rect.w - w, rect.h - h);
                    if (score < best_score || (score == best_score && secondary < best_secondary))
                    {
                        best_score = score;
                        best_secondary = secondary;
                        best = (gint)j;
                        placed = (PackRect){ rect.x, rect.y, w, h };
                        rotated = orientation != 0;
                    }
                }
            }
        }
        if (best < 0)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                         "Image '%s' (%dx%d) does not fit in atlas",
                         image->name, image->width, image->height);
            return FALSE;
        }
        if (self->method == LRG_ATLAS_PACK_METHOD_SHELF)
        {
            if ((guint)best == shelves->len)
            {
                ShelfRow row = { placed.y, placed.h, placed.w };

                g_array_append_val (shelves, row);
            }
            else
                g_array_index (shelves, ShelfRow, best).x_used += placed.w;
        }
        else if (self->method == LRG_ATLAS_PACK_METHOD_MAXRECTS)
            split_maxrects (free_rects, placed);
        else
        {
            PackRect rect = g_array_index (free_rects, PackRect, best);
            gint dw = rect.w - placed.w;
            gint dh = rect.h - placed.h;

            g_array_remove_index (free_rects, best);
            /* Split along the shorter leftover axis into disjoint rectangles. */
            if (dw > dh)
            {
                append_rect (free_rects, rect.x + placed.w, rect.y, dw, rect.h);
                append_rect (free_rects, rect.x, rect.y + placed.h, placed.w, dh);
            }
            else
            {
                append_rect (free_rects, rect.x + placed.w, rect.y, dw, placed.h);
                append_rect (free_rects, rect.x, rect.y + placed.h, rect.w, dh);
            }
        }
        image->packed_x = placed.x;
        image->packed_y = placed.y;
        image->rotated = rotated;
        image->packed = TRUE;
        total_w = MAX (total_w, placed.x + placed.w);
        total_h = MAX (total_h, placed.y + placed.h);
    }
    self->packed_width = self->power_of_two ? next_power_of_two (total_w) : total_w;
    self->packed_height = self->power_of_two ? next_power_of_two (total_h) : total_h;
    return TRUE;
}

/* Search bounded bin shapes so free-rectangle heuristics do not spread a
 * small sprite set across the entire configured maximum atlas. */
static gboolean
pack_search (LrgAtlasPacker *self,
             GError **error)
{
    g_autofree PackRect *best_positions = NULL;
    g_autofree gboolean *best_rotations = NULL;
    gint max_w = packing_limit (self->max_width,self->power_of_two);
    gint max_h = packing_limit (self->max_height,self->power_of_two);
    gint min_w = 1, min_h = 1, width;
    gint best_w = 0, best_h = 0;
    gint64 best_area = G_MAXINT64;
    gdouble area = 0;
    guint i;

    if (self->method == LRG_ATLAS_PACK_METHOD_SHELF)
        return pack_rectangles (self,max_w,max_h,error);
    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images,i);
        gint64 w = (gint64)image->width + self->padding;
        gint64 h = (gint64)image->height + self->padding;
        gint64 low_w = self->allow_rotation ? MIN (w,h) : w;
        gint64 low_h = self->allow_rotation ? MIN (w,h) : h;

        if (low_w > max_w || low_h > max_h ||
            ((w > max_w || h > max_h) && (!self->allow_rotation || h > max_w || w > max_h)))
            goto no_space;
        min_w = MAX (min_w,(gint)low_w);
        min_h = MAX (min_h,(gint)low_h);
        area += (gdouble)w * h;
    }
    if (area > (gdouble)max_w * max_h) goto no_space;
    best_positions = g_new (PackRect,self->images->len);
    best_rotations = g_new (gboolean,self->images->len);
    width = self->power_of_two ? next_power_of_two (min_w) : min_w;
    for (;;)
    {
        gdouble required = ceil (area / width);
        gint height;

        if (required <= max_h)
        {
            height = MAX (min_h,(gint)required);
            if (self->power_of_two) height = next_power_of_two (height);
            for (;;)
            {
                if (pack_rectangles (self,width,height,NULL))
                {
                    gint64 packed_area = (gint64)self->packed_width * self->packed_height;

                    if (packed_area < best_area)
                    {
                        best_area = packed_area;
                        best_w = self->packed_width;
                        best_h = self->packed_height;
                        for (i = 0; i < self->images->len; i++)
                        {
                            LrgAtlasPackerImage *image = g_ptr_array_index (self->images,i);

                            best_positions[i].x = image->packed_x;
                            best_positions[i].y = image->packed_y;
                            best_rotations[i] = image->rotated;
                        }
                    }
                    break;
                }
                if (height == max_h) break;
                height = (gint)MIN ((gint64)max_h,
                                   self->power_of_two ? (gint64)height*2 : (gint64)height+MAX (1,height/4));
            }
        }
        if (width == max_w || best_area == area) break;
        width = (gint)MIN ((gint64)max_w,(gint64)width*2);
    }
    if (best_w == 0) goto no_space;
    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images,i);

        image->packed_x = best_positions[i].x;
        image->packed_y = best_positions[i].y;
        image->rotated = best_rotations[i];
        image->packed = TRUE;
    }
    self->packed_width = best_w;
    self->packed_height = best_h;
    return TRUE;
no_space:
    g_set_error_literal (error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,
                         "Images do not fit within the configured atlas limits");
    return FALSE;
}

/**
 * lrg_atlas_packer_pack:
 * @self: A #LrgAtlasPacker
 * @error: (nullable): Return location for error
 *
 * Performs the packing algorithm to arrange all images.
 * After packing, use lrg_atlas_packer_create_atlas() to get the result.
 *
 * Returns: %TRUE if packing succeeded
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_packer_pack (LrgAtlasPacker  *self,
                       GError         **error)
{
    guint i;
    gboolean result;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), FALSE);

    if (self->images->len == 0)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "No images to pack");
        return FALSE;
    }

    /* Reset packed state */
    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images, i);
        image->packed = FALSE;
    }

    self->packed_width = 0;
    self->packed_height = 0;

    if (self->max_width <= 0 || self->max_height <= 0 || self->padding < 0 ||
        self->method < LRG_ATLAS_PACK_METHOD_SHELF ||
        self->method > LRG_ATLAS_PACK_METHOD_GUILLOTINE)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Invalid atlas packing configuration");
        result = FALSE;
    }
    else
        result = pack_search (self, error);
    if (!result)
    {
        for (i = 0; i < self->images->len; i++)
            ((LrgAtlasPackerImage *)g_ptr_array_index (self->images, i))->packed = FALSE;
        self->packed_width = 0;
        self->packed_height = 0;
    }

    self->is_packed = result;
    return result;
}

/**
 * lrg_atlas_packer_get_packed_width:
 * @self: A #LrgAtlasPacker
 *
 * Gets the width of the packed atlas (available after pack()).
 *
 * Returns: Packed width in pixels, or 0 if not yet packed
 *
 * Since: 1.0
 */
gint
lrg_atlas_packer_get_packed_width (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0);
    return self->is_packed ? self->packed_width : 0;
}

/**
 * lrg_atlas_packer_get_packed_height:
 * @self: A #LrgAtlasPacker
 *
 * Gets the height of the packed atlas (available after pack()).
 *
 * Returns: Packed height in pixels, or 0 if not yet packed
 *
 * Since: 1.0
 */
gint
lrg_atlas_packer_get_packed_height (LrgAtlasPacker *self)
{
    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0);
    return self->is_packed ? self->packed_height : 0;
}

/**
 * lrg_atlas_packer_get_efficiency:
 * @self: A #LrgAtlasPacker
 *
 * Gets the packing efficiency (used area / total area).
 *
 * Returns: Efficiency as a value 0.0-1.0, or 0 if not yet packed
 *
 * Since: 1.0
 */
gfloat
lrg_atlas_packer_get_efficiency (LrgAtlasPacker *self)
{
    gdouble total_area;
    gdouble used_area = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0.0f);

    if (!self->is_packed)
        return 0.0f;

    total_area = (gdouble)self->packed_width * self->packed_height;
    if (total_area <= 0)
        return 0.0f;

    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images, i);
        if (image->packed)
        {
            used_area += (gdouble)image->width * image->height;
        }
    }

    return (gfloat)used_area / (gfloat)total_area;
}

/**
 * lrg_atlas_packer_create_atlas:
 * @self: A #LrgAtlasPacker
 * @name: Name for the atlas
 *
 * Creates a texture atlas from the packed result.
 * Must call lrg_atlas_packer_pack() first.
 *
 * Returns: (transfer full) (nullable): A new #LrgTextureAtlas, or %NULL if not packed
 *
 * Since: 1.0
 */
LrgTextureAtlas *
lrg_atlas_packer_create_atlas (LrgAtlasPacker *self,
                               const gchar    *name)
{
    LrgTextureAtlas *atlas;
    guint i;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), NULL);

    if (!self->is_packed)
    {
        g_warning ("Cannot create atlas: pack() has not been called");
        return NULL;
    }

    atlas = lrg_texture_atlas_new (name);
    lrg_texture_atlas_set_size (atlas, self->packed_width, self->packed_height);

    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images, i);

        if (image->packed)
        {
            LrgAtlasRegion *region;

            region = lrg_texture_atlas_add_region_rect (atlas, image->name,
                                                        image->packed_x,
                                                        image->packed_y,
                                                        image->rotated ? image->height : image->width,
                                                        image->rotated ? image->width : image->height);

            if (image->rotated && region != NULL)
            {
                lrg_atlas_region_set_rotated (region, TRUE);
            }
        }
    }

    return atlas;
}

/**
 * lrg_atlas_packer_get_image_position:
 * @self: A #LrgAtlasPacker
 * @name: Name of the image
 * @out_x: (out) (nullable): Return location for X position
 * @out_y: (out) (nullable): Return location for Y position
 * @out_rotated: (out) (nullable): Return location for rotation flag
 *
 * Gets the packed position of an image.
 * Must call lrg_atlas_packer_pack() first.
 *
 * Returns: %TRUE if the image was found
 *
 * Since: 1.0
 */
gboolean
lrg_atlas_packer_get_image_position (LrgAtlasPacker *self,
                                     const gchar    *name,
                                     gint           *out_x,
                                     gint           *out_y,
                                     gboolean       *out_rotated)
{
    LrgAtlasPackerImage *image;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    image = g_hash_table_lookup (self->images_by_name, name);
    if (!self->is_packed || image == NULL || !image->packed)
        return FALSE;

    if (out_x != NULL)
        *out_x = image->packed_x;
    if (out_y != NULL)
        *out_y = image->packed_y;
    if (out_rotated != NULL)
        *out_rotated = image->rotated;

    return TRUE;
}

/**
 * lrg_atlas_packer_get_image_user_data:
 * @self: A #LrgAtlasPacker
 * @name: Name of the image
 *
 * Gets the user data associated with an image.
 *
 * Returns: (transfer none) (nullable): The user data, or %NULL if not found
 *
 * Since: 1.0
 */
gpointer
lrg_atlas_packer_get_image_user_data (LrgAtlasPacker *self,
                                      const gchar    *name)
{
    LrgAtlasPackerImage *image;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    image = g_hash_table_lookup (self->images_by_name, name);
    if (image == NULL)
        return NULL;

    return image->user_data;
}

/**
 * lrg_atlas_packer_foreach_image:
 * @self: A #LrgAtlasPacker
 * @func: (scope call): Function to call for each image
 * @user_data: User data to pass to the function
 *
 * Iterates over all packed images with their positions.
 * Must call lrg_atlas_packer_pack() first.
 *
 * The function receives the #LrgAtlasPackerImage as data.
 *
 * Since: 1.0
 */
void
lrg_atlas_packer_foreach_image (LrgAtlasPacker *self,
                                GFunc           func,
                                gpointer        user_data)
{
    guint i;

    g_return_if_fail (LRG_IS_ATLAS_PACKER (self));
    g_return_if_fail (func != NULL);

    if (!self->is_packed)
        return;

    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images, i);
        if (image->packed)
        {
            func (image, user_data);
        }
    }
}
