/* lrg-atlas-packer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Build-time texture atlas packer.
 */

#include <gio/gio.h>

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
    while (power < n)
        power <<= 1;
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

    return img_b->height - img_a->height;
}

/*
 * pack_shelf:
 *
 * Shelf packing algorithm. Simple but decent results.
 * Sorts images by height and packs into horizontal rows.
 */
static gboolean
pack_shelf (LrgAtlasPacker  *self,
            GError         **error)
{
    GArray *shelves;
    GPtrArray *sorted;
    gint total_width = 0;
    gint total_height = 0;
    guint i;

    /* Create sorted copy of images */
    sorted = g_ptr_array_new ();
    for (i = 0; i < self->images->len; i++)
    {
        g_ptr_array_add (sorted, g_ptr_array_index (self->images, i));
    }
    g_ptr_array_sort (sorted, compare_images_by_height_desc);

    /* Initialize shelf array */
    shelves = g_array_new (FALSE, FALSE, sizeof (ShelfRow));

    /* Pack each image */
    for (i = 0; i < sorted->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (sorted, i);
        gint img_w, img_h;
        gboolean placed = FALSE;
        guint j;

        img_w = image->width + self->padding;
        img_h = image->height + self->padding;

        /* Try to fit in existing shelf */
        for (j = 0; j < shelves->len; j++)
        {
            ShelfRow *shelf = &g_array_index (shelves, ShelfRow, j);

            if (shelf->x_used + img_w <= self->max_width &&
                img_h <= shelf->height)
            {
                image->packed_x = shelf->x_used;
                image->packed_y = shelf->y;
                image->rotated = FALSE;
                image->packed = TRUE;

                shelf->x_used += img_w;
                if (shelf->x_used > total_width)
                    total_width = shelf->x_used;

                placed = TRUE;
                break;
            }
        }

        if (!placed)
        {
            /* Create new shelf */
            ShelfRow new_shelf;
            gint new_y = 0;

            if (shelves->len > 0)
            {
                ShelfRow *last = &g_array_index (shelves, ShelfRow, shelves->len - 1);
                new_y = last->y + last->height;
            }

            if (new_y + img_h > self->max_height)
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "Image '%s' (%dx%d) does not fit in atlas",
                             image->name, image->width, image->height);
                g_array_unref (shelves);
                g_ptr_array_unref (sorted);
                return FALSE;
            }

            new_shelf.y = new_y;
            new_shelf.height = img_h;
            new_shelf.x_used = img_w;

            image->packed_x = 0;
            image->packed_y = new_y;
            image->rotated = FALSE;
            image->packed = TRUE;

            g_array_append_val (shelves, new_shelf);

            if (img_w > total_width)
                total_width = img_w;

            total_height = new_y + img_h;
        }
    }

    /* Set final dimensions */
    self->packed_width = total_width;
    self->packed_height = total_height;

    if (self->power_of_two)
    {
        self->packed_width = next_power_of_two (self->packed_width);
        self->packed_height = next_power_of_two (self->packed_height);
    }

    g_array_unref (shelves);
    g_ptr_array_unref (sorted);

    return TRUE;
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

    /* Run packing algorithm */
    switch (self->method)
    {
    case LRG_ATLAS_PACK_METHOD_SHELF:
        result = pack_shelf (self, error);
        break;

    case LRG_ATLAS_PACK_METHOD_MAXRECTS:
        /* TODO: Implement maxrects algorithm */
        g_warning ("MaxRects algorithm not yet implemented, using Shelf");
        result = pack_shelf (self, error);
        break;

    case LRG_ATLAS_PACK_METHOD_GUILLOTINE:
        /* TODO: Implement guillotine algorithm */
        g_warning ("Guillotine algorithm not yet implemented, using Shelf");
        result = pack_shelf (self, error);
        break;

    default:
        result = pack_shelf (self, error);
        break;
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
    return self->packed_width;
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
    return self->packed_height;
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
    gint total_area;
    gint used_area = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_ATLAS_PACKER (self), 0.0f);

    if (!self->is_packed)
        return 0.0f;

    total_area = self->packed_width * self->packed_height;
    if (total_area <= 0)
        return 0.0f;

    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images, i);
        if (image->packed)
        {
            used_area += image->width * image->height;
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
                                                        image->width,
                                                        image->height);

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
    if (image == NULL || !image->packed)
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

    for (i = 0; i < self->images->len; i++)
    {
        LrgAtlasPackerImage *image = g_ptr_array_index (self->images, i);
        if (image->packed)
        {
            func (image, user_data);
        }
    }
}
