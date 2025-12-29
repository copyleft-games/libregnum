/* lrg-sprite-sheet.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sprite sheet for animation frames.
 */

#include <gio/gio.h>

#include "lrg-sprite-sheet.h"

/**
 * LrgAnimationDef:
 *
 * Internal structure defining an animation sequence.
 */
typedef struct
{
    gchar   *name;
    GArray  *frame_indices;   /* guint array */
    gfloat   frame_duration;
    gboolean loop;
} LrgAnimationDef;

static LrgAnimationDef *
animation_def_new (const gchar *name,
                   gfloat       frame_duration,
                   gboolean     loop)
{
    LrgAnimationDef *def;

    def = g_new0 (LrgAnimationDef, 1);
    def->name = g_strdup (name);
    def->frame_indices = g_array_new (FALSE, FALSE, sizeof (guint));
    def->frame_duration = frame_duration;
    def->loop = loop;

    return def;
}

static void
animation_def_free (LrgAnimationDef *def)
{
    if (def == NULL)
        return;

    g_free (def->name);
    g_array_unref (def->frame_indices);
    g_free (def);
}

struct _LrgSpriteSheet
{
    GObject parent_instance;

    gchar               *name;
    gchar               *texture_path;
    gint                 texture_width;
    gint                 texture_height;
    LrgSpriteSheetFormat format;

    GPtrArray   *frames;      /* LrgAtlasRegion* array */
    GHashTable  *frames_by_name;  /* name -> LrgAtlasRegion* */
    GHashTable  *animations;  /* name -> LrgAnimationDef* */
};

G_DEFINE_FINAL_TYPE (LrgSpriteSheet, lrg_sprite_sheet, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_TEXTURE_PATH,
    PROP_TEXTURE_WIDTH,
    PROP_TEXTURE_HEIGHT,
    PROP_FORMAT,
    PROP_FRAME_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_sprite_sheet_finalize (GObject *object)
{
    LrgSpriteSheet *self = LRG_SPRITE_SHEET (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->texture_path, g_free);
    g_clear_pointer (&self->frames, g_ptr_array_unref);
    g_clear_pointer (&self->frames_by_name, g_hash_table_unref);
    g_clear_pointer (&self->animations, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_sprite_sheet_parent_class)->finalize (object);
}

static void
lrg_sprite_sheet_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgSpriteSheet *self = LRG_SPRITE_SHEET (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_TEXTURE_PATH:
        g_value_set_string (value, self->texture_path);
        break;
    case PROP_TEXTURE_WIDTH:
        g_value_set_int (value, self->texture_width);
        break;
    case PROP_TEXTURE_HEIGHT:
        g_value_set_int (value, self->texture_height);
        break;
    case PROP_FORMAT:
        g_value_set_int (value, self->format);
        break;
    case PROP_FRAME_COUNT:
        g_value_set_uint (value, self->frames->len);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sprite_sheet_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgSpriteSheet *self = LRG_SPRITE_SHEET (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_clear_pointer (&self->name, g_free);
        self->name = g_value_dup_string (value);
        break;
    case PROP_TEXTURE_PATH:
        g_clear_pointer (&self->texture_path, g_free);
        self->texture_path = g_value_dup_string (value);
        break;
    case PROP_TEXTURE_WIDTH:
        self->texture_width = g_value_get_int (value);
        break;
    case PROP_TEXTURE_HEIGHT:
        self->texture_height = g_value_get_int (value);
        break;
    case PROP_FORMAT:
        self->format = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sprite_sheet_class_init (LrgSpriteSheetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_sprite_sheet_finalize;
    object_class->get_property = lrg_sprite_sheet_get_property;
    object_class->set_property = lrg_sprite_sheet_set_property;

    /**
     * LrgSpriteSheet:name:
     *
     * The name identifier of the sprite sheet.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSpriteSheet:texture-path:
     *
     * Path to the texture file.
     *
     * Since: 1.0
     */
    properties[PROP_TEXTURE_PATH] =
        g_param_spec_string ("texture-path", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSpriteSheet:texture-width:
     *
     * Width of the texture in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_TEXTURE_WIDTH] =
        g_param_spec_int ("texture-width", NULL, NULL,
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSpriteSheet:texture-height:
     *
     * Height of the texture in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_TEXTURE_HEIGHT] =
        g_param_spec_int ("texture-height", NULL, NULL,
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSpriteSheet:format:
     *
     * The sprite sheet format type.
     *
     * Since: 1.0
     */
    properties[PROP_FORMAT] =
        g_param_spec_int ("format", NULL, NULL,
                          LRG_SPRITE_SHEET_FORMAT_GRID,
                          LRG_SPRITE_SHEET_FORMAT_LIBREGNUM,
                          LRG_SPRITE_SHEET_FORMAT_LIBREGNUM,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSpriteSheet:frame-count:
     *
     * The total number of frames (read-only).
     *
     * Since: 1.0
     */
    properties[PROP_FRAME_COUNT] =
        g_param_spec_uint ("frame-count", NULL, NULL,
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_sprite_sheet_init (LrgSpriteSheet *self)
{
    self->format = LRG_SPRITE_SHEET_FORMAT_LIBREGNUM;
    self->frames = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_atlas_region_free);
    self->frames_by_name = g_hash_table_new (g_str_hash, g_str_equal);
    self->animations = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               NULL, (GDestroyNotify) animation_def_free);
}

/**
 * lrg_sprite_sheet_new:
 * @name: Name identifier for the sprite sheet
 *
 * Creates a new empty sprite sheet.
 *
 * Returns: (transfer full): A new #LrgSpriteSheet
 *
 * Since: 1.0
 */
LrgSpriteSheet *
lrg_sprite_sheet_new (const gchar *name)
{
    return g_object_new (LRG_TYPE_SPRITE_SHEET,
                         "name", name,
                         NULL);
}

/**
 * lrg_sprite_sheet_new_from_grid:
 * @name: Name identifier
 * @texture_path: Path to the texture file
 * @frame_width: Width of each frame in pixels
 * @frame_height: Height of each frame in pixels
 * @frame_count: Total number of frames (0 = auto-calculate)
 * @columns: Number of columns in the grid (0 = auto-calculate)
 *
 * Creates a sprite sheet from a regular grid layout.
 * The texture dimensions are inferred from the grid parameters.
 *
 * Returns: (transfer full): A new #LrgSpriteSheet
 *
 * Since: 1.0
 */
LrgSpriteSheet *
lrg_sprite_sheet_new_from_grid (const gchar *name,
                                const gchar *texture_path,
                                gint         frame_width,
                                gint         frame_height,
                                guint        frame_count,
                                guint        columns)
{
    LrgSpriteSheet *self;

    self = lrg_sprite_sheet_new (name);
    self->format = LRG_SPRITE_SHEET_FORMAT_GRID;
    self->texture_path = g_strdup (texture_path);

    /*
     * Grid parameters will be used when texture dimensions are set
     * or when generate_grid is called explicitly.
     */
    if (columns > 0 && frame_count > 0)
    {
        guint rows;

        rows = (frame_count + columns - 1) / columns;
        self->texture_width = (gint)(columns * frame_width);
        self->texture_height = (gint)(rows * frame_height);

        lrg_sprite_sheet_generate_grid (self, frame_width, frame_height,
                                        columns, rows, 0, 0, 0);

        /* Trim excess frames if frame_count specified */
        while (self->frames->len > frame_count)
        {
            lrg_sprite_sheet_remove_frame (self, self->frames->len - 1);
        }
    }

    return self;
}

/**
 * lrg_sprite_sheet_new_from_file:
 * @path: Path to the sprite sheet definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a sprite sheet by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgSpriteSheet or %NULL on error
 *
 * Since: 1.0
 */
LrgSpriteSheet *
lrg_sprite_sheet_new_from_file (const gchar  *path,
                                GError      **error)
{
    g_return_val_if_fail (path != NULL, NULL);

    /* TODO: Implement YAML loading with yaml-glib */
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                 "YAML loading not yet implemented");
    return NULL;
}

/**
 * lrg_sprite_sheet_get_name:
 * @self: A #LrgSpriteSheet
 *
 * Gets the name of the sprite sheet.
 *
 * Returns: (transfer none) (nullable): The sprite sheet name
 *
 * Since: 1.0
 */
const gchar *
lrg_sprite_sheet_get_name (LrgSpriteSheet *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);
    return self->name;
}

/**
 * lrg_sprite_sheet_get_texture_path:
 * @self: A #LrgSpriteSheet
 *
 * Gets the path to the texture file.
 *
 * Returns: (transfer none) (nullable): The texture path
 *
 * Since: 1.0
 */
const gchar *
lrg_sprite_sheet_get_texture_path (LrgSpriteSheet *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);
    return self->texture_path;
}

/**
 * lrg_sprite_sheet_set_texture_path:
 * @self: A #LrgSpriteSheet
 * @path: Path to the texture file
 *
 * Sets the path to the texture file.
 *
 * Since: 1.0
 */
void
lrg_sprite_sheet_set_texture_path (LrgSpriteSheet *self,
                                   const gchar    *path)
{
    g_return_if_fail (LRG_IS_SPRITE_SHEET (self));

    g_clear_pointer (&self->texture_path, g_free);
    self->texture_path = g_strdup (path);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE_PATH]);
}

/**
 * lrg_sprite_sheet_get_texture_width:
 * @self: A #LrgSpriteSheet
 *
 * Gets the width of the texture.
 *
 * Returns: Width in pixels
 *
 * Since: 1.0
 */
gint
lrg_sprite_sheet_get_texture_width (LrgSpriteSheet *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
    return self->texture_width;
}

/**
 * lrg_sprite_sheet_set_texture_width:
 * @self: A #LrgSpriteSheet
 * @width: Width in pixels
 *
 * Sets the texture width.
 *
 * Since: 1.0
 */
void
lrg_sprite_sheet_set_texture_width (LrgSpriteSheet *self,
                                    gint            width)
{
    g_return_if_fail (LRG_IS_SPRITE_SHEET (self));

    self->texture_width = width;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE_WIDTH]);
}

/**
 * lrg_sprite_sheet_get_texture_height:
 * @self: A #LrgSpriteSheet
 *
 * Gets the height of the texture.
 *
 * Returns: Height in pixels
 *
 * Since: 1.0
 */
gint
lrg_sprite_sheet_get_texture_height (LrgSpriteSheet *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
    return self->texture_height;
}

/**
 * lrg_sprite_sheet_set_texture_height:
 * @self: A #LrgSpriteSheet
 * @height: Height in pixels
 *
 * Sets the texture height.
 *
 * Since: 1.0
 */
void
lrg_sprite_sheet_set_texture_height (LrgSpriteSheet *self,
                                     gint            height)
{
    g_return_if_fail (LRG_IS_SPRITE_SHEET (self));

    self->texture_height = height;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE_HEIGHT]);
}

/**
 * lrg_sprite_sheet_set_texture_size:
 * @self: A #LrgSpriteSheet
 * @width: Width in pixels
 * @height: Height in pixels
 *
 * Sets both texture dimensions.
 *
 * Since: 1.0
 */
void
lrg_sprite_sheet_set_texture_size (LrgSpriteSheet *self,
                                   gint            width,
                                   gint            height)
{
    g_return_if_fail (LRG_IS_SPRITE_SHEET (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_sprite_sheet_set_texture_width (self, width);
    lrg_sprite_sheet_set_texture_height (self, height);
    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_sprite_sheet_get_format:
 * @self: A #LrgSpriteSheet
 *
 * Gets the sprite sheet format.
 *
 * Returns: The format type
 *
 * Since: 1.0
 */
LrgSpriteSheetFormat
lrg_sprite_sheet_get_format (LrgSpriteSheet *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), LRG_SPRITE_SHEET_FORMAT_GRID);
    return self->format;
}

/**
 * lrg_sprite_sheet_set_format:
 * @self: A #LrgSpriteSheet
 * @format: The format type
 *
 * Sets the sprite sheet format.
 *
 * Since: 1.0
 */
void
lrg_sprite_sheet_set_format (LrgSpriteSheet      *self,
                             LrgSpriteSheetFormat format)
{
    g_return_if_fail (LRG_IS_SPRITE_SHEET (self));

    self->format = format;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FORMAT]);
}

/**
 * lrg_sprite_sheet_get_frame_count:
 * @self: A #LrgSpriteSheet
 *
 * Gets the total number of frames.
 *
 * Returns: The frame count
 *
 * Since: 1.0
 */
guint
lrg_sprite_sheet_get_frame_count (LrgSpriteSheet *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
    return self->frames->len;
}

/**
 * lrg_sprite_sheet_get_frame:
 * @self: A #LrgSpriteSheet
 * @index: Frame index (0-based)
 *
 * Gets a frame by index.
 *
 * Returns: (transfer none) (nullable): The frame region, or %NULL if out of bounds
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_sprite_sheet_get_frame (LrgSpriteSheet *self,
                            guint           index)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);

    if (index >= self->frames->len)
        return NULL;

    return g_ptr_array_index (self->frames, index);
}

/**
 * lrg_sprite_sheet_get_frame_by_name:
 * @self: A #LrgSpriteSheet
 * @name: Frame name
 *
 * Gets a frame by name.
 *
 * Returns: (transfer none) (nullable): The frame region, or %NULL if not found
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_sprite_sheet_get_frame_by_name (LrgSpriteSheet *self,
                                    const gchar    *name)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->frames_by_name, name);
}

/**
 * lrg_sprite_sheet_add_frame:
 * @self: A #LrgSpriteSheet
 * @frame: The frame region to add
 *
 * Adds a frame to the sprite sheet. Takes ownership of the region.
 *
 * Returns: The index of the added frame
 *
 * Since: 1.0
 */
guint
lrg_sprite_sheet_add_frame (LrgSpriteSheet *self,
                            LrgAtlasRegion *frame)
{
    const gchar *name;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
    g_return_val_if_fail (frame != NULL, 0);

    g_ptr_array_add (self->frames, frame);

    name = lrg_atlas_region_get_name (frame);
    if (name != NULL)
    {
        g_hash_table_insert (self->frames_by_name, (gpointer) name, frame);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_COUNT]);

    return self->frames->len - 1;
}

/**
 * lrg_sprite_sheet_add_frame_rect:
 * @self: A #LrgSpriteSheet
 * @name: (nullable): Frame name (can be NULL for numbered frames)
 * @x: X position in the texture
 * @y: Y position in the texture
 * @width: Width of the frame
 * @height: Height of the frame
 *
 * Convenience function to add a frame by rectangle.
 * UV coordinates are calculated automatically.
 *
 * Returns: The index of the added frame
 *
 * Since: 1.0
 */
guint
lrg_sprite_sheet_add_frame_rect (LrgSpriteSheet *self,
                                 const gchar    *name,
                                 gint            x,
                                 gint            y,
                                 gint            width,
                                 gint            height)
{
    LrgAtlasRegion *frame;
    g_autofree gchar *auto_name = NULL;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);

    /* Generate name if not provided */
    if (name == NULL)
    {
        auto_name = g_strdup_printf ("frame_%u", self->frames->len);
        name = auto_name;
    }

    frame = lrg_atlas_region_new (name, x, y, width, height);

    /* Calculate UVs if texture dimensions are known */
    if (self->texture_width > 0 && self->texture_height > 0)
    {
        lrg_atlas_region_calculate_uv (frame, self->texture_width, self->texture_height);
    }

    return lrg_sprite_sheet_add_frame (self, frame);
}

/**
 * lrg_sprite_sheet_remove_frame:
 * @self: A #LrgSpriteSheet
 * @index: Frame index to remove
 *
 * Removes a frame by index.
 *
 * Returns: %TRUE if the frame was removed
 *
 * Since: 1.0
 */
gboolean
lrg_sprite_sheet_remove_frame (LrgSpriteSheet *self,
                               guint           index)
{
    LrgAtlasRegion *frame;
    const gchar *name;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);

    if (index >= self->frames->len)
        return FALSE;

    frame = g_ptr_array_index (self->frames, index);
    name = lrg_atlas_region_get_name (frame);

    if (name != NULL)
    {
        g_hash_table_remove (self->frames_by_name, name);
    }

    g_ptr_array_remove_index (self->frames, index);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_COUNT]);

    return TRUE;
}

/**
 * lrg_sprite_sheet_clear_frames:
 * @self: A #LrgSpriteSheet
 *
 * Removes all frames from the sprite sheet.
 *
 * Since: 1.0
 */
void
lrg_sprite_sheet_clear_frames (LrgSpriteSheet *self)
{
    g_return_if_fail (LRG_IS_SPRITE_SHEET (self));

    g_ptr_array_set_size (self->frames, 0);
    g_hash_table_remove_all (self->frames_by_name);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_COUNT]);
}

/**
 * lrg_sprite_sheet_define_animation:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 * @start_frame: First frame index
 * @end_frame: Last frame index (inclusive)
 * @frame_duration: Duration per frame in seconds
 * @loop: Whether the animation loops
 *
 * Defines a named animation sequence using consecutive frames.
 *
 * Returns: %TRUE if the animation was defined successfully
 *
 * Since: 1.0
 */
gboolean
lrg_sprite_sheet_define_animation (LrgSpriteSheet *self,
                                   const gchar    *name,
                                   guint           start_frame,
                                   guint           end_frame,
                                   gfloat          frame_duration,
                                   gboolean        loop)
{
    LrgAnimationDef *def;
    guint i;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (start_frame <= end_frame, FALSE);
    g_return_val_if_fail (end_frame < self->frames->len, FALSE);
    g_return_val_if_fail (frame_duration > 0.0f, FALSE);

    def = animation_def_new (name, frame_duration, loop);

    for (i = start_frame; i <= end_frame; i++)
    {
        g_array_append_val (def->frame_indices, i);
    }

    g_hash_table_insert (self->animations, def->name, def);

    return TRUE;
}

/**
 * lrg_sprite_sheet_define_animation_frames:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 * @frames: (array length=n_frames): Array of frame indices
 * @n_frames: Number of frames
 * @frame_duration: Duration per frame in seconds
 * @loop: Whether the animation loops
 *
 * Defines a named animation sequence using arbitrary frames.
 *
 * Returns: %TRUE if the animation was defined successfully
 *
 * Since: 1.0
 */
gboolean
lrg_sprite_sheet_define_animation_frames (LrgSpriteSheet *self,
                                          const gchar    *name,
                                          const guint    *frames,
                                          guint           n_frames,
                                          gfloat          frame_duration,
                                          gboolean        loop)
{
    LrgAnimationDef *def;
    guint i;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (frames != NULL, FALSE);
    g_return_val_if_fail (n_frames > 0, FALSE);
    g_return_val_if_fail (frame_duration > 0.0f, FALSE);

    /* Validate all frame indices */
    for (i = 0; i < n_frames; i++)
    {
        if (frames[i] >= self->frames->len)
        {
            g_warning ("Invalid frame index %u in animation '%s'", frames[i], name);
            return FALSE;
        }
    }

    def = animation_def_new (name, frame_duration, loop);

    for (i = 0; i < n_frames; i++)
    {
        g_array_append_val (def->frame_indices, frames[i]);
    }

    g_hash_table_insert (self->animations, def->name, def);

    return TRUE;
}

/**
 * lrg_sprite_sheet_has_animation:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Checks if an animation exists.
 *
 * Returns: %TRUE if the animation exists
 *
 * Since: 1.0
 */
gboolean
lrg_sprite_sheet_has_animation (LrgSpriteSheet *self,
                                const gchar    *name)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->animations, name);
}

/**
 * lrg_sprite_sheet_get_animation_frame_count:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Gets the number of frames in an animation.
 *
 * Returns: Frame count, or 0 if animation not found
 *
 * Since: 1.0
 */
guint
lrg_sprite_sheet_get_animation_frame_count (LrgSpriteSheet *self,
                                            const gchar    *name)
{
    LrgAnimationDef *def;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
    g_return_val_if_fail (name != NULL, 0);

    def = g_hash_table_lookup (self->animations, name);
    if (def == NULL)
        return 0;

    return def->frame_indices->len;
}

/**
 * lrg_sprite_sheet_get_animation_duration:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Gets the total duration of an animation.
 *
 * Returns: Duration in seconds, or 0 if animation not found
 *
 * Since: 1.0
 */
gfloat
lrg_sprite_sheet_get_animation_duration (LrgSpriteSheet *self,
                                         const gchar    *name)
{
    LrgAnimationDef *def;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0.0f);
    g_return_val_if_fail (name != NULL, 0.0f);

    def = g_hash_table_lookup (self->animations, name);
    if (def == NULL)
        return 0.0f;

    return def->frame_duration * def->frame_indices->len;
}

/**
 * lrg_sprite_sheet_get_animation_frame:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 * @time: Time in seconds since animation start
 *
 * Gets the frame region for an animation at a given time.
 * Handles looping automatically based on animation settings.
 *
 * Returns: (transfer none) (nullable): The frame region, or %NULL if not found
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_sprite_sheet_get_animation_frame (LrgSpriteSheet *self,
                                      const gchar    *name,
                                      gfloat          time)
{
    LrgAnimationDef *def;
    guint frame_idx;
    guint frame_count;
    guint animation_frame;
    gfloat duration;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    def = g_hash_table_lookup (self->animations, name);
    if (def == NULL)
        return NULL;

    frame_count = def->frame_indices->len;
    duration = def->frame_duration * frame_count;

    if (def->loop)
    {
        /* Wrap time for looping animations */
        while (time >= duration)
            time -= duration;
        while (time < 0.0f)
            time += duration;
    }
    else
    {
        /* Clamp time for non-looping animations */
        if (time < 0.0f)
            time = 0.0f;
        if (time >= duration)
            time = duration - 0.0001f;  /* Stay on last frame */
    }

    animation_frame = (guint)(time / def->frame_duration);
    if (animation_frame >= frame_count)
        animation_frame = frame_count - 1;

    frame_idx = g_array_index (def->frame_indices, guint, animation_frame);

    return lrg_sprite_sheet_get_frame (self, frame_idx);
}

/**
 * lrg_sprite_sheet_get_animation_names:
 * @self: A #LrgSpriteSheet
 *
 * Gets all animation names.
 *
 * Returns: (transfer full) (element-type utf8): Array of animation names
 *
 * Since: 1.0
 */
GPtrArray *
lrg_sprite_sheet_get_animation_names (LrgSpriteSheet *self)
{
    GPtrArray *names;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);

    names = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->animations);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        g_ptr_array_add (names, g_strdup ((const gchar *)key));
    }

    return names;
}

/**
 * lrg_sprite_sheet_remove_animation:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Removes an animation definition.
 *
 * Returns: %TRUE if the animation was removed
 *
 * Since: 1.0
 */
gboolean
lrg_sprite_sheet_remove_animation (LrgSpriteSheet *self,
                                   const gchar    *name)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_remove (self->animations, name);
}

/**
 * lrg_sprite_sheet_recalculate_uvs:
 * @self: A #LrgSpriteSheet
 *
 * Recalculates UV coordinates for all frames based on their
 * pixel positions and the texture dimensions.
 *
 * Since: 1.0
 */
void
lrg_sprite_sheet_recalculate_uvs (LrgSpriteSheet *self)
{
    guint i;

    g_return_if_fail (LRG_IS_SPRITE_SHEET (self));

    if (self->texture_width <= 0 || self->texture_height <= 0)
    {
        g_warning ("Cannot recalculate UVs: texture dimensions not set");
        return;
    }

    for (i = 0; i < self->frames->len; i++)
    {
        LrgAtlasRegion *frame = g_ptr_array_index (self->frames, i);
        lrg_atlas_region_calculate_uv (frame, self->texture_width, self->texture_height);
    }
}

/**
 * lrg_sprite_sheet_generate_grid:
 * @self: A #LrgSpriteSheet
 * @frame_width: Width of each frame
 * @frame_height: Height of each frame
 * @columns: Number of columns (0 = auto from texture width)
 * @rows: Number of rows (0 = auto from texture height)
 * @padding: Padding between frames
 * @offset_x: X offset from texture edge
 * @offset_y: Y offset from texture edge
 *
 * Generates frames from a grid layout. Clears existing frames first.
 *
 * Returns: Number of frames generated
 *
 * Since: 1.0
 */
guint
lrg_sprite_sheet_generate_grid (LrgSpriteSheet *self,
                                gint            frame_width,
                                gint            frame_height,
                                guint           columns,
                                guint           rows,
                                gint            padding,
                                gint            offset_x,
                                gint            offset_y)
{
    guint col, row;
    guint count = 0;
    gint cell_width, cell_height;

    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
    g_return_val_if_fail (frame_width > 0, 0);
    g_return_val_if_fail (frame_height > 0, 0);

    /* Clear existing frames */
    lrg_sprite_sheet_clear_frames (self);

    /* Auto-calculate columns/rows if not specified */
    cell_width = frame_width + padding;
    cell_height = frame_height + padding;

    if (columns == 0 && self->texture_width > 0)
    {
        columns = (self->texture_width - offset_x) / cell_width;
    }

    if (rows == 0 && self->texture_height > 0)
    {
        rows = (self->texture_height - offset_y) / cell_height;
    }

    if (columns == 0 || rows == 0)
    {
        g_warning ("Cannot generate grid: columns or rows is 0 and texture dimensions not set");
        return 0;
    }

    /* Generate frames row by row, left to right */
    for (row = 0; row < rows; row++)
    {
        for (col = 0; col < columns; col++)
        {
            gint x, y;

            x = offset_x + (gint)col * cell_width;
            y = offset_y + (gint)row * cell_height;

            lrg_sprite_sheet_add_frame_rect (self, NULL, x, y, frame_width, frame_height);
            count++;
        }
    }

    return count;
}

/**
 * lrg_sprite_sheet_save_to_file:
 * @self: A #LrgSpriteSheet
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the sprite sheet definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_sprite_sheet_save_to_file (LrgSpriteSheet  *self,
                               const gchar     *path,
                               GError         **error)
{
    g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    /* TODO: Implement YAML saving with yaml-glib */
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                 "YAML saving not yet implemented");
    return FALSE;
}
