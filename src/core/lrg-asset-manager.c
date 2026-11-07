/* lrg-asset-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Asset manager implementation with caching and mod overlay support.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "lrg-asset-manager.h"
#include "lrg-asset-meta.h"
#include "lrg-data-loader.h"
#include <yaml-glib.h>
#include <string.h>
#include <math.h>
#include "../graphics/lrg-gltf-info.h"
#include <raylib.h>
#include <rlgl.h>
#include "../lrg-log.h"

typedef struct
{
    GPtrArray  *search_paths;    /* Array of gchar* (owned strings) */
    GHashTable *texture_cache;   /* gchar* -> GrlTexture* */
    GHashTable *font_cache;      /* gchar* (name:size) -> GrlFont* */
    GHashTable *sound_cache;     /* gchar* -> GrlSound* */
    LrgDataLoader *data_loader;
    GHashTable *object_cache;
    GHashTable *dependencies;
    gboolean reloading;
    GHashTable *object_watches;
    GHashTable *music_cache;     /* gchar* -> GrlMusic* */
    GHashTable *model_cache;     /* canonical path -> ModelAsset* */
    GHashTable *animation_cache; /* canonical path -> GPtrArray<GrlModelAnimation> */

    /* Since 0.9.0: project awareness, the guid index, import settings. */
    gchar      *project_root;    /* canonical, nullable */
    GPtrArray  *asset_dirs;      /* canonical absolute dirs (owned) */
    GHashTable *guid_to_path;    /* guid -> canonical absolute path */
    GHashTable *path_to_guid;    /* canonical absolute path -> guid */
    gboolean    index_valid;     /* the tables reflect the asset dirs */
    gboolean    index_cached;    /* the tables came from an index file */
    gboolean    apply_import_settings;
} LrgAssetManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAssetManager, lrg_asset_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_PROJECT_ROOT,
    PROP_APPLY_IMPORT_SETTINGS,
    N_PROPS
};

static GParamSpec *asset_properties[N_PROPS];

static gchar *resolve_guid_internal (LrgAssetManager *self, const gchar *guid);
static LrgAssetMeta *meta_for_path (LrgAssetManager *self, const gchar *path);

typedef struct
{
    GObject *object;
    gchar *path;
} ObjectAsset;

/*
 * ModelAsset:
 *
 * A cached model plus the GPU textures its loader created. raylib's
 * UnloadModel() leaves material textures to the caller, so the manager
 * records them at load time and releases them with the model. Recording
 * them up front means textures a caller later swaps into a material are
 * never unloaded by the manager.
 */
typedef struct
{
    GrlModel *model;
    GArray   *textures;   /* Texture2D created by the model loader */
} ModelAsset;

typedef struct
{
    LrgAssetManager *manager;
    gchar *name;
    GFile *file;
    GFileMonitor *monitor;
    GMainContext *context;
    GSource *pending;
} ObjectWatch;

enum
{
    OBJECT_RELOADED,
    OBJECT_RELOAD_FAILED,
    N_ASSET_SIGNALS
};

static guint asset_signals[N_ASSET_SIGNALS];

static void
object_asset_free (gpointer data)
{
    ObjectAsset *asset = data;

    g_object_unref (asset->object);
    g_free (asset->path);
    g_free (asset);
}

static void
model_asset_free (gpointer data)
{
    ModelAsset *asset = data;
    guint       i;

    /* Model first (meshes, maps, pose caches), then its loader textures.
     * Both need the GL context the model was created in. */
    g_clear_object (&asset->model);
    if (IsWindowReady ())
    {
        for (i = 0; i < asset->textures->len; i++)
            UnloadTexture (g_array_index (asset->textures, Texture2D, i));
    }
    g_array_unref (asset->textures);
    g_free (asset);
}

/* model_asset_collect_textures:
 * Records every distinct non-default texture referenced by the model's
 * material maps right after loading. */
static void
model_asset_collect_textures (ModelAsset *asset)
{
    Model *model;
    guint  default_id;
    gint   m;
    gint   k;

    model = grl_model_get_handle (asset->model);
    default_id = rlGetTextureIdDefault ();
    for (m = 0; m < model->materialCount; m++)
    {
        if (model->materials[m].maps == NULL)
            continue;
        for (k = 0; k <= MATERIAL_MAP_BRDF; k++)
        {
            Texture2D texture = model->materials[m].maps[k].texture;
            gboolean  seen = FALSE;
            guint     i;

            if (texture.id == 0 || texture.id == default_id)
                continue;
            for (i = 0; i < asset->textures->len && !seen; i++)
                seen = g_array_index (asset->textures, Texture2D, i).id == texture.id;
            if (!seen)
                g_array_append_val (asset->textures, texture);
        }
    }
}

static void
object_watch_free (gpointer data)
{
    ObjectWatch *watch = data;

    if (watch->pending != NULL)
    {
        g_source_destroy (watch->pending);
        g_source_unref (watch->pending);
    }
    g_signal_handlers_disconnect_by_data (watch->monitor, watch);
    g_file_monitor_cancel (watch->monitor);
    g_object_unref (watch->monitor);
    g_object_unref (watch->file);
    g_main_context_unref (watch->context);
    g_free (watch->name);
    g_free (watch);
}

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/**
 * resolve_asset_path:
 * @self: an #LrgAssetManager
 * @name: the relative asset name
 *
 * Resolves an asset name to a full path by searching paths in reverse
 * order (last added has highest priority).
 *
 * Returns: (transfer full) (nullable): The full path, or %NULL if not found
 */
static gchar *
resolve_asset_path (LrgAssetManager *self,
                    const gchar     *name)
{
    LrgAssetManagerPrivate *priv;
    guint                   i;

    priv = lrg_asset_manager_get_instance_private (self);

    /* `guid:<uuid>` references go through the guid index (since 0.9.0). */
    if (g_str_has_prefix (name, LRG_ASSET_GUID_PREFIX))
        return resolve_guid_internal (self, name);

    if (g_path_is_absolute (name))
        return g_file_test (name, G_FILE_TEST_IS_REGULAR) ? g_strdup (name) : NULL;

    /* Search in reverse order (last path has highest priority) */
    for (i = priv->search_paths->len; i > 0; i--)
    {
        const gchar      *search_path;
        g_autofree gchar *full_path = NULL;

        search_path = g_ptr_array_index (priv->search_paths, i - 1);
        full_path = g_build_filename (search_path, name, NULL);

        if (g_file_test (full_path, G_FILE_TEST_EXISTS))
        {
            return g_steal_pointer (&full_path);
        }
    }

    return NULL;
}

/**
 * make_font_cache_key:
 * @name: the font name
 * @size: the font size
 *
 * Creates a cache key for a font that includes the size.
 *
 * Returns: (transfer full): The cache key (name:size)
 */
static gchar *
make_font_cache_key (const gchar *name,
                     gint         size)
{
    return g_strdup_printf ("%s:%d", name, size);
}

/*
 * model_cache_key:
 * @self: an #LrgAssetManager
 * @name: asset name or absolute path
 *
 * Model and animation caches are keyed by the canonical absolute path of
 * the resolved file, so different spellings of one file share an entry and
 * the same relative name under different search paths does not. Absolute
 * names are canonicalized even when the file is gone so they can still be
 * unloaded.
 *
 * Returns: (transfer full) (nullable): the key, or %NULL if not found
 */
static gchar *
model_cache_key (LrgAssetManager *self,
                 const gchar     *name)
{
    g_autofree gchar *path = NULL;

    if (g_path_is_absolute (name))
        return g_canonicalize_filename (name, NULL);

    path = resolve_asset_path (self, name);
    if (path == NULL)
        return NULL;
    return g_canonicalize_filename (path, NULL);
}

/* ==========================================================================
 * Runtime import settings (since 0.9.0)
 *
 * Every loader resolves its file, then (when apply-import-settings is on
 * and the file has a `.meta` sidecar) applies the importer settings the
 * editor shows. Files without a sidecar load exactly as before 0.9.0.
 * ========================================================================== */

/*
 * meta_for_path:
 *
 * The sidecar of the resolved file @path when import settings apply, or
 * NULL (no sidecar, unreadable sidecar, or the policy is off).
 */
static LrgAssetMeta *
meta_for_path (LrgAssetManager *self,
               const gchar     *path)
{
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);

    if (!priv->apply_import_settings || path == NULL)
        return NULL;
    return lrg_asset_meta_load_for_asset (path, NULL);
}

static GrlTextureFilter
map_texture_filter (LrgAssetTextureFilter filter)
{
    switch (filter)
    {
    case LRG_ASSET_TEXTURE_FILTER_POINT:     return GRL_TEXTURE_FILTER_POINT;
    case LRG_ASSET_TEXTURE_FILTER_TRILINEAR: return GRL_TEXTURE_FILTER_TRILINEAR;
    case LRG_ASSET_TEXTURE_FILTER_BILINEAR:
    default:                                 return GRL_TEXTURE_FILTER_BILINEAR;
    }
}

static GrlTextureWrap
map_texture_wrap (LrgAssetTextureWrap wrap)
{
    switch (wrap)
    {
    case LRG_ASSET_TEXTURE_WRAP_CLAMP:  return GRL_TEXTURE_WRAP_CLAMP;
    case LRG_ASSET_TEXTURE_WRAP_MIRROR: return GRL_TEXTURE_WRAP_MIRROR_REPEAT;
    case LRG_ASSET_TEXTURE_WRAP_REPEAT:
    default:                            return GRL_TEXTURE_WRAP_REPEAT;
    }
}

/*
 * load_texture_file:
 * @path: the resolved texture file
 * @meta: (nullable): its sidecar
 *
 * Loads a texture applying the texture importer settings:
 *
 *  - max-size: the image is downscaled on the CPU (aspect kept) before the
 *    upload when its longest edge is larger;
 *  - mipmaps: a mip chain is generated on the GPU (also forced by the
 *    trilinear filter, which samples between mip levels);
 *  - filter, wrap: the GPU sampler state.
 *
 * `srgb` is recorded but not applied: the GL 3.3 forward renderer has no
 * sRGB texture formats; materials read it from the sidecar
 * (lrg_asset_manager_get_asset_meta()) to decide how to decode.
 */
static GrlTexture *
load_texture_file (const gchar  *path,
                   LrgAssetMeta *meta)
{
    GrlTexture            *texture = NULL;
    guint                  max_size;
    LrgAssetTextureFilter  filter;

    if (meta == NULL)
        return grl_texture_new_from_file (path);

    /* 1. Size limit: decode, shrink, upload. */
    max_size = lrg_asset_meta_get_texture_max_size (meta);
    if (max_size > 0)
    {
        g_autoptr(GrlImage) image = grl_image_new_from_file (path);

        if (image != NULL && grl_image_is_valid (image))
        {
            gint w = grl_image_get_width (image);
            gint h = grl_image_get_height (image);

            if ((guint) MAX (w, h) > max_size)
            {
                gdouble scale = (gdouble) max_size / (gdouble) MAX (w, h);
                gint    nw = MAX (1, (gint) floor ((gdouble) w * scale + 0.5));
                gint    nh = MAX (1, (gint) floor ((gdouble) h * scale + 0.5));

                grl_image_resize (image, nw, nh);
            }
            texture = grl_texture_new_from_image (image);
        }
    }
    if (texture == NULL)
        texture = grl_texture_new_from_file (path);
    if (texture == NULL || !grl_texture_is_valid (texture))
        return texture;

    /* 2. Mip chain, then the sampler state (the filter of a mipmapped
     *    texture is only trilinear once the chain exists). */
    filter = lrg_asset_meta_get_texture_filter (meta);
    if (lrg_asset_meta_get_texture_mipmaps (meta) || filter == LRG_ASSET_TEXTURE_FILTER_TRILINEAR)
        grl_texture_gen_mipmaps (texture);
    grl_texture_set_filter (texture, map_texture_filter (filter));
    grl_texture_set_wrap (texture, map_texture_wrap (lrg_asset_meta_get_texture_wrap (meta)));

    return texture;
}

/*
 * font_codepoints:
 *
 * The code points of the font importer's "codepoints" setting as the
 * gint array raylib wants, or NULL (raylib's default 32..126 set) when the
 * sidecar does not set the key or it does not parse.
 */
static gint *
font_codepoints (LrgAssetMeta *meta,
                 gint         *count)
{
    g_autofree gchar  *ranges = NULL;
    g_autoptr(GArray)  cps = NULL;
    gint              *out;
    guint              i;

    *count = 0;
    if (meta == NULL || !lrg_asset_meta_has_setting (meta, "codepoints"))
        return NULL;
    ranges = lrg_asset_meta_get_font_codepoints (meta);
    cps = lrg_asset_meta_parse_codepoints (ranges, NULL);
    if (cps == NULL || cps->len == 0)
        return NULL;

    out = g_new (gint, cps->len);
    for (i = 0; i < cps->len; i++)
        out[i] = (gint) g_array_index (cps, guint32, i);
    *count = (gint) cps->len;
    return out;
}

/*
 * generate_mesh_normals:
 *
 * Recomputes smooth vertex normals for one mesh: each triangle's face
 * normal (cross product, so weighted by area) is added to its three
 * vertices, then the sums are normalized. Indexed meshes share vertices
 * and so come out smooth; unindexed meshes come out flat. The normal
 * buffer on the GPU is updated (or created) so the shader sees them.
 */
static void
generate_mesh_normals (Mesh *mesh)
{
    gint i;
    gint tris;

    if (mesh->vertices == NULL || mesh->vertexCount <= 0)
        return;

    if (mesh->normals == NULL)
        mesh->normals = (float *) RL_CALLOC ((gsize) mesh->vertexCount * 3, sizeof (float));
    else
        memset (mesh->normals, 0, (gsize) mesh->vertexCount * 3 * sizeof (float));

    tris = mesh->triangleCount;
    if (tris <= 0)
        tris = mesh->vertexCount / 3;

    /* 1. Accumulate the (area weighted) face normal on each corner. */
    for (i = 0; i < tris; i++)
    {
        gint   idx[3];
        gint   k;
        gfloat e1[3], e2[3], n[3];

        for (k = 0; k < 3; k++)
            idx[k] = (mesh->indices != NULL) ? (gint) mesh->indices[i * 3 + k] : i * 3 + k;
        if (idx[0] >= mesh->vertexCount || idx[1] >= mesh->vertexCount || idx[2] >= mesh->vertexCount)
            continue;

        for (k = 0; k < 3; k++)
        {
            e1[k] = mesh->vertices[idx[1] * 3 + k] - mesh->vertices[idx[0] * 3 + k];
            e2[k] = mesh->vertices[idx[2] * 3 + k] - mesh->vertices[idx[0] * 3 + k];
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];

        for (k = 0; k < 3; k++)
        {
            mesh->normals[idx[k] * 3 + 0] += n[0];
            mesh->normals[idx[k] * 3 + 1] += n[1];
            mesh->normals[idx[k] * 3 + 2] += n[2];
        }
    }

    /* 2. Normalize (degenerate vertices point up). */
    for (i = 0; i < mesh->vertexCount; i++)
    {
        gfloat *v = &mesh->normals[i * 3];
        gfloat  len = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

        if (len > 1e-12f)
        {
            v[0] /= len;
            v[1] /= len;
            v[2] /= len;
        }
        else
        {
            v[0] = 0.0f;
            v[1] = 1.0f;
            v[2] = 0.0f;
        }
    }

    /* 3. Upload: overwrite the existing normal buffer, or create one in
     *    the mesh's vertex array (raylib leaves the slot empty when the
     *    file had no normals and feeds a constant attribute instead). */
    if (mesh->vaoId > 0 && mesh->vboId != NULL && IsWindowReady ())
    {
        gint size = mesh->vertexCount * 3 * (gint) sizeof (float);

        if (mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL] != 0)
            UpdateMeshBuffer (*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, mesh->normals, size, 0);
        else
        {
            rlEnableVertexArray (mesh->vaoId);
            mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL] = rlLoadVertexBuffer (mesh->normals, size, false);
            rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, 3, RL_FLOAT, 0, 0, 0);
            rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL);
            rlDisableVertexArray ();
        }
    }
}

/*
 * apply_model_settings:
 *
 * The model importer settings: `scale` (uniform) and `up-axis` (a Z-up
 * file is turned Y-up by a -90 degree rotation about X) are folded into
 * the model transform, which every draw applies; `generate-normals`
 * recomputes the vertex normals of every mesh.
 */
static void
apply_model_settings (GrlModel     *model,
                      LrgAssetMeta *meta)
{
    gdouble scale = lrg_asset_meta_get_model_scale (meta);
    gboolean z_up = lrg_asset_meta_get_model_up_axis (meta) == LRG_ASSET_UP_AXIS_Z;

    if (scale != 1.0 || z_up)
    {
        g_autoptr(GrlMatrix) current = grl_model_get_transform (model);
        g_autoptr(GrlMatrix) import = grl_matrix_new_scale ((gfloat) scale, (gfloat) scale, (gfloat) scale);
        g_autoptr(GrlMatrix) combined = NULL;

        if (z_up)
        {
            g_autoptr(GrlMatrix) rot = grl_matrix_new_rotate_xyz ((gfloat) (-G_PI / 2.0), 0.0f, 0.0f);
            g_autoptr(GrlMatrix) both = grl_matrix_multiply (import, rot);

            g_clear_pointer (&import, grl_matrix_free);
            import = g_steal_pointer (&both);
        }
        /* The import transform applies after whatever the file set. */
        combined = (current != NULL) ? grl_matrix_multiply (current, import) : grl_matrix_copy (import);
        grl_model_set_transform (model, combined);
    }

    if (lrg_asset_meta_get_model_generate_normals (meta))
    {
        Model *handle = grl_model_get_handle (model);
        gint   m;

        for (m = 0; handle != NULL && m < handle->meshCount; m++)
            generate_mesh_normals (&handle->meshes[m]);
    }
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static GrlTexture *
lrg_asset_manager_real_load_texture (LrgAssetManager  *self,
                                     const gchar      *name,
                                     GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlTexture             *texture;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first */
    texture = g_hash_table_lookup (priv->texture_cache, name);
    if (texture != NULL)
    {
        return texture;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Texture not found: %s",
                     name);
        return NULL;
    }

    if (!IsWindowReady ())
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                             "A graphics context is required to load this asset");
        return NULL;
    }

    /* Load texture (with the sidecar's importer settings, if any) */
    {
        g_autoptr(LrgAssetMeta) meta = meta_for_path (self, path);

        texture = load_texture_file (path, meta);
    }
    if (texture == NULL || !grl_texture_is_valid (texture))
    {
        g_clear_object (&texture);
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_LOAD_FAILED,
                     "Failed to load texture: %s",
                     path);
        return NULL;
    }

    /* Cache and return */
    g_hash_table_insert (priv->texture_cache, g_strdup (name), texture);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded texture '%s' from %s",
               name, path);

    return texture;
}

static GrlFont *
lrg_asset_manager_real_load_font (LrgAssetManager  *self,
                                  const gchar      *name,
                                  gint              size,
                                  GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlFont                *font;
    g_autofree gchar       *cache_key = NULL;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first (key includes size) */
    cache_key = make_font_cache_key (name, size);
    font = g_hash_table_lookup (priv->font_cache, cache_key);
    if (font != NULL)
    {
        return font;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Font not found: %s",
                     name);
        return NULL;
    }

    if (!IsWindowReady ())
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                             "A graphics context is required to load this asset");
        return NULL;
    }

    /* Load font (the sidecar may restrict the glyph set) */
    {
        g_autoptr(LrgAssetMeta) meta = meta_for_path (self, path);
        g_autofree gint        *codepoints = NULL;
        gint                    n_codepoints = 0;

        codepoints = font_codepoints (meta, &n_codepoints);
        font = grl_font_new_from_file_ex (path, size, codepoints, n_codepoints);
    }
    if (font == NULL || !grl_font_is_valid (font))
    {
        g_clear_object (&font);
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_LOAD_FAILED,
                     "Failed to load font: %s",
                     path);
        return NULL;
    }

    /* Cache and return */
    g_hash_table_insert (priv->font_cache, g_steal_pointer (&cache_key), font);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded font '%s' size %d from %s",
               name, size, path);

    return font;
}

static GrlSound *
lrg_asset_manager_real_load_sound (LrgAssetManager  *self,
                                   const gchar      *name,
                                   GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlSound               *sound;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first */
    sound = g_hash_table_lookup (priv->sound_cache, name);
    if (sound != NULL)
    {
        return sound;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Sound not found: %s",
                     name);
        return NULL;
    }

    /* Load sound */
    sound = grl_sound_new_from_file (path, error);
    if (sound == NULL)
    {
        /* Error already set by grl_sound_new_from_file */
        return NULL;
    }

    /* Importer settings: the sidecar's volume. `loop` cannot be set on a
     * raylib sound; players read it from the sidecar. */
    {
        g_autoptr(LrgAssetMeta) meta = meta_for_path (self, path);

        if (meta != NULL)
            grl_sound_set_volume (sound, (gfloat) lrg_asset_meta_get_audio_volume (meta));
    }

    /* Cache and return */
    g_hash_table_insert (priv->sound_cache, g_strdup (name), sound);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded sound '%s' from %s",
               name, path);

    return sound;
}

static GrlMusic *
lrg_asset_manager_real_load_music (LrgAssetManager  *self,
                                   const gchar      *name,
                                   GError          **error)
{
    LrgAssetManagerPrivate *priv;
    GrlMusic               *music;
    g_autofree gchar       *path = NULL;

    priv = lrg_asset_manager_get_instance_private (self);

    /* Check cache first */
    music = g_hash_table_lookup (priv->music_cache, name);
    if (music != NULL)
    {
        return music;
    }

    /* Resolve path */
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error,
                     LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Music not found: %s",
                     name);
        return NULL;
    }

    /* Load music */
    music = grl_music_new_from_file (path, error);
    if (music == NULL)
    {
        /* Error already set by grl_music_new_from_file */
        return NULL;
    }

    /* Importer settings: volume and looping. */
    {
        g_autoptr(LrgAssetMeta) meta = meta_for_path (self, path);

        if (meta != NULL)
        {
            grl_music_set_volume (music, (gfloat) lrg_asset_meta_get_audio_volume (meta));
            grl_music_set_looping (music, lrg_asset_meta_get_audio_loop (meta));
        }
    }

    /* Cache and return */
    g_hash_table_insert (priv->music_cache, g_strdup (name), music);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded music '%s' from %s",
               name, path);

    return music;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_asset_manager_finalize (GObject *object)
{
    LrgAssetManager        *self = LRG_ASSET_MANAGER (object);
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);

    g_clear_pointer (&priv->object_watches, g_hash_table_unref);
    g_clear_pointer (&priv->object_cache, g_hash_table_unref);
    g_clear_pointer (&priv->dependencies, g_hash_table_unref);
    g_clear_object (&priv->data_loader);
    g_clear_pointer (&priv->search_paths, g_ptr_array_unref);
    g_clear_pointer (&priv->texture_cache, g_hash_table_unref);
    g_clear_pointer (&priv->font_cache, g_hash_table_unref);
    g_clear_pointer (&priv->sound_cache, g_hash_table_unref);
    g_clear_pointer (&priv->music_cache, g_hash_table_unref);
    g_clear_pointer (&priv->model_cache, g_hash_table_unref);
    g_clear_pointer (&priv->animation_cache, g_hash_table_unref);
    g_clear_pointer (&priv->project_root, g_free);
    g_clear_pointer (&priv->asset_dirs, g_ptr_array_unref);
    g_clear_pointer (&priv->guid_to_path, g_hash_table_unref);
    g_clear_pointer (&priv->path_to_guid, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_asset_manager_parent_class)->finalize (object);
}

static void
lrg_asset_manager_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgAssetManager        *self = LRG_ASSET_MANAGER (object);
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_PROJECT_ROOT:
        g_value_set_string (value, priv->project_root);
        break;
    case PROP_APPLY_IMPORT_SETTINGS:
        g_value_set_boolean (value, priv->apply_import_settings);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_asset_manager_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgAssetManager *self = LRG_ASSET_MANAGER (object);

    switch (prop_id)
    {
    case PROP_PROJECT_ROOT:
        lrg_asset_manager_set_project_root (self, g_value_get_string (value));
        break;
    case PROP_APPLY_IMPORT_SETTINGS:
        lrg_asset_manager_set_apply_import_settings (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_asset_manager_class_init (LrgAssetManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_asset_manager_finalize;
    object_class->get_property = lrg_asset_manager_get_property;
    object_class->set_property = lrg_asset_manager_set_property;

    /**
     * LrgAssetManager:project-root:
     *
     * The project directory (see lrg_asset_manager_set_project_root()).
     * Setting it adds the root as a search path and reads the guid index
     * directories and the `apply_import_settings` key from its
     * `project.ryaml`.
     *
     * Since: 0.9.0
     */
    asset_properties[PROP_PROJECT_ROOT] =
        g_param_spec_string ("project-root", "Project root",
                             "Project directory holding project.ryaml",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAssetManager:apply-import-settings:
     *
     * Whether the loaders apply the importer settings of an asset's
     * `.meta` sidecar (texture filter, wrap, mipmaps and size limit; model
     * scale, up axis and normals; audio mode, volume and looping; font
     * size and code points). Assets without a sidecar always load
     * unchanged. Default %TRUE.
     *
     * Since: 0.9.0
     */
    asset_properties[PROP_APPLY_IMPORT_SETTINGS] =
        g_param_spec_boolean ("apply-import-settings", "Apply import settings",
                              "Apply .meta importer settings at load time",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, asset_properties);

    /**
     * LrgAssetManager::object-reloaded:
     * @self: the asset manager
     * @name: cached asset name
     * @previous: previous definition, valid during signal emission
     * @replacement: replacement definition already in the cache
     *
     * Consumers can retain @replacement and release their previous reference.
     */
    asset_signals[OBJECT_RELOADED] =
        g_signal_new ("object-reloaded", G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 3,
                      G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_OBJECT);

    /**
     * LrgAssetManager::object-reload-failed:
     * @self: the asset manager
     * @name: cached asset name
     * @error: reload failure; the previous definition remains cached
     *
     * Emitted for explicit and monitored reload failures.
     */
    asset_signals[OBJECT_RELOAD_FAILED] =
        g_signal_new ("object-reload-failed", G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
                      G_TYPE_STRING, G_TYPE_ERROR);

    /* Set default virtual method implementations */
    klass->load_texture = lrg_asset_manager_real_load_texture;
    klass->load_font = lrg_asset_manager_real_load_font;
    klass->load_sound = lrg_asset_manager_real_load_sound;
    klass->load_music = lrg_asset_manager_real_load_music;
}

static void
lrg_asset_manager_init (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);

    priv->dependencies = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, (GDestroyNotify) g_hash_table_unref);
    priv->object_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, object_asset_free);
    priv->object_watches = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, object_watch_free);
    priv->search_paths = g_ptr_array_new_with_free_func (g_free);
    priv->texture_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, g_object_unref);
    priv->font_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, g_object_unref);
    priv->sound_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
    priv->music_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
    priv->model_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, model_asset_free);
    priv->animation_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, (GDestroyNotify)g_ptr_array_unref);
    priv->asset_dirs = g_ptr_array_new_with_free_func (g_free);
    priv->guid_to_path = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    priv->path_to_guid = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    priv->apply_import_settings = TRUE;
}

/*
 * normalize_name:
 *
 * Loader names that are `guid:` references are replaced by the absolute
 * path they resolve to, so a guid and a path naming the same file share
 * one cache entry. Unresolvable references are kept (the loader then
 * reports them as not found, naming the guid).
 */
static gchar *
normalize_name (LrgAssetManager *self,
                const gchar     *name)
{
    if (g_str_has_prefix (name, LRG_ASSET_GUID_PREFIX))
    {
        gchar *path = resolve_guid_internal (self, name);

        if (path != NULL)
            return path;
    }
    return g_strdup (name);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
 * lrg_asset_manager_new:
 *
 * Creates a new asset manager.
 *
 * Returns: (transfer full): A new #LrgAssetManager
 */
LrgAssetManager *
lrg_asset_manager_new (void)
{
    return g_object_new (LRG_TYPE_ASSET_MANAGER, NULL);
}

/* ==========================================================================
 * Public API - Search Path Management
 * ========================================================================== */

/*
 * lrg_asset_manager_add_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to add
 *
 * Adds a directory to the search path.
 */
void
lrg_asset_manager_add_search_path (LrgAssetManager *self,
                                   const gchar     *path)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));
    g_return_if_fail (path != NULL);

    priv = lrg_asset_manager_get_instance_private (self);

    g_ptr_array_add (priv->search_paths, g_strdup (path));

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Added asset search path: %s",
               path);
}

/*
 * lrg_asset_manager_remove_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to remove
 *
 * Removes a directory from the search path.
 *
 * Returns: %TRUE if the path was found and removed
 */
gboolean
lrg_asset_manager_remove_search_path (LrgAssetManager *self,
                                      const gchar     *path)
{
    LrgAssetManagerPrivate *priv;
    guint                   i;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);

    for (i = 0; i < priv->search_paths->len; i++)
    {
        const gchar *existing = g_ptr_array_index (priv->search_paths, i);

        if (g_strcmp0 (existing, path) == 0)
        {
            g_ptr_array_remove_index (priv->search_paths, i);
            lrg_debug (LRG_LOG_DOMAIN_CORE,
                       "Removed asset search path: %s",
                       path);
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * lrg_asset_manager_clear_search_paths:
 * @self: an #LrgAssetManager
 *
 * Removes all search paths.
 */
void
lrg_asset_manager_clear_search_paths (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));

    priv = lrg_asset_manager_get_instance_private (self);

    g_ptr_array_set_size (priv->search_paths, 0);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Cleared all asset search paths");
}

/*
 * lrg_asset_manager_get_search_paths:
 * @self: an #LrgAssetManager
 *
 * Gets the list of search paths.
 *
 * Returns: (transfer none) (element-type utf8): The search paths array
 */
const GPtrArray *
lrg_asset_manager_get_search_paths (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);

    priv = lrg_asset_manager_get_instance_private (self);

    return priv->search_paths;
}

/* ==========================================================================
 * Public API - Synchronous Loading
 * ========================================================================== */

/*
 * lrg_asset_manager_load_texture:
 * @self: an #LrgAssetManager
 * @name: relative path to texture file
 * @error: (nullable): return location for error
 *
 * Loads a texture from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlTexture, or %NULL on error
 */
GrlTexture *
lrg_asset_manager_load_texture (LrgAssetManager  *self,
                                const gchar      *name,
                                GError          **error)
{
    LrgAssetManagerClass *klass;
    g_autofree gchar     *norm = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);
    norm = normalize_name (self, name);

    return klass->load_texture (self, norm, error);
}

/*
 * lrg_asset_manager_load_font:
 * @self: an #LrgAssetManager
 * @name: relative path to font file
 * @size: font size in pixels
 * @error: (nullable): return location for error
 *
 * Loads a font from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlFont, or %NULL on error
 */
GrlFont *
lrg_asset_manager_load_font (LrgAssetManager  *self,
                             const gchar      *name,
                             gint              size,
                             GError          **error)
{
    LrgAssetManagerClass *klass;
    g_autofree gchar     *norm = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);
    norm = normalize_name (self, name);

    /* size <= 0: the sidecar's rasterization size (since 0.9.0), else 32. */
    if (size <= 0)
    {
        g_autofree gchar       *path = resolve_asset_path (self, norm);
        g_autoptr(LrgAssetMeta) meta = meta_for_path (self, path);

        size = (meta != NULL) ? (gint) lrg_asset_meta_get_font_size (meta) : 32;
        if (size <= 0)
            size = 32;
    }

    return klass->load_font (self, norm, size, error);
}

/*
 * lrg_asset_manager_load_sound:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file
 * @error: (nullable): return location for error
 *
 * Loads a sound effect from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlSound, or %NULL on error
 */
GrlSound *
lrg_asset_manager_load_sound (LrgAssetManager  *self,
                              const gchar      *name,
                              GError          **error)
{
    LrgAssetManagerClass *klass;
    g_autofree gchar     *norm = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);
    norm = normalize_name (self, name);

    return klass->load_sound (self, norm, error);
}

/*
 * lrg_asset_manager_load_music:
 * @self: an #LrgAssetManager
 * @name: relative path to music file
 * @error: (nullable): return location for error
 *
 * Loads a streaming music track from the search paths.
 *
 * Returns: (transfer none) (nullable): The #GrlMusic, or %NULL on error
 */
GrlMusic *
lrg_asset_manager_load_music (LrgAssetManager  *self,
                              const gchar      *name,
                              GError          **error)
{
    LrgAssetManagerClass *klass;
    g_autofree gchar     *norm = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    klass = LRG_ASSET_MANAGER_GET_CLASS (self);
    norm = normalize_name (self, name);

    return klass->load_music (self, norm, error);
}

void
lrg_asset_manager_set_data_loader (LrgAssetManager *self,
                                    LrgDataLoader   *loader)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));
    g_return_if_fail (loader == NULL || LRG_IS_DATA_LOADER (loader));
    priv = lrg_asset_manager_get_instance_private (self);
    if (priv->data_loader == loader)
        return;
    g_hash_table_remove_all (priv->object_watches);
    g_hash_table_remove_all (priv->object_cache);
    g_hash_table_remove_all (priv->dependencies);
    g_set_object (&priv->data_loader, loader);
}

LrgDataLoader *
lrg_asset_manager_get_data_loader (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    priv = lrg_asset_manager_get_instance_private (self);
    return priv->data_loader;
}

GObject *
lrg_asset_manager_load_object (LrgAssetManager  *self,
                                const gchar      *name,
                                GError          **error)
{
    LrgAssetManagerPrivate *priv;
    ObjectAsset *asset;
    g_autofree gchar *path = NULL;
    g_autoptr(GObject) object = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    priv = lrg_asset_manager_get_instance_private (self);
    asset = g_hash_table_lookup (priv->object_cache, name);
    if (asset != NULL)
        return asset->object;

    if (priv->data_loader == NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                             "No data loader configured for YAML assets");
        return NULL;
    }
    path = resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error, LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND, "Definition not found: %s", name);
        return NULL;
    }
    object = lrg_data_loader_load_file_validated (priv->data_loader, path, error);
    if (object == NULL)
        return NULL;
    asset = g_new0 (ObjectAsset, 1);
    asset->object = g_steal_pointer (&object);
    asset->path = g_canonicalize_filename (path, NULL);
    g_hash_table_insert (priv->object_cache, g_strdup (name), asset);
    return asset->object;
}

/* Reachability is iterative so long dependency chains do not use the stack. */
static gboolean
object_depends_on (LrgAssetManagerPrivate *priv, const gchar *start, const gchar *target)
{
    g_autoptr(GHashTable) visited = g_hash_table_new (g_str_hash, g_str_equal);
    g_autoptr(GPtrArray) pending = g_ptr_array_new ();
    guint i;

    g_ptr_array_add (pending, (gpointer) start);
    g_hash_table_add (visited, (gpointer) start);
    for (i = 0; i < pending->len; i++)
    {
        const gchar *name = g_ptr_array_index (pending, i);
        GHashTable *deps = g_hash_table_lookup (priv->dependencies, name);
        GHashTableIter iter;
        gpointer key;

        if (g_str_equal (name, target))
            return TRUE;
        if (deps == NULL)
            continue;
        g_hash_table_iter_init (&iter, deps);
        while (g_hash_table_iter_next (&iter, &key, NULL))
            if (g_hash_table_add (visited, key))
                g_ptr_array_add (pending, key);
    }
    return FALSE;
}

gboolean
lrg_asset_manager_add_object_dependency (LrgAssetManager *self,
                                         const gchar *name,
                                         const gchar *dependency,
                                         GError **error)
{
    LrgAssetManagerPrivate *priv;
    GHashTable *deps;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL && dependency != NULL, FALSE);
    priv = lrg_asset_manager_get_instance_private (self);
    if (!g_hash_table_contains (priv->object_cache, name) ||
        !g_hash_table_contains (priv->object_cache, dependency))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Both dependency endpoints must be cached definitions");
        return FALSE;
    }
    if (object_depends_on (priv, dependency, name))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Object dependency would create a cycle");
        return FALSE;
    }
    deps = g_hash_table_lookup (priv->dependencies, name);
    if (deps == NULL)
    {
        deps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        g_hash_table_insert (priv->dependencies, g_strdup (name), deps);
    }
    g_hash_table_add (deps, g_strdup (dependency));
    return TRUE;
}

gboolean
lrg_asset_manager_remove_object_dependency (LrgAssetManager *self,
                                            const gchar *name,
                                            const gchar *dependency)
{
    LrgAssetManagerPrivate *priv;
    GHashTable *deps;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL && dependency != NULL, FALSE);
    priv = lrg_asset_manager_get_instance_private (self);
    deps = g_hash_table_lookup (priv->dependencies, name);
    return deps != NULL && g_hash_table_remove (deps, dependency);
}

static void
forget_object_dependencies (LrgAssetManagerPrivate *priv, const gchar *name)
{
    GHashTableIter iter;
    gpointer value;

    g_hash_table_remove (priv->dependencies, name);
    g_hash_table_iter_init (&iter, priv->dependencies);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_hash_table_remove (value, name);
}

gboolean
lrg_asset_manager_reload_object (LrgAssetManager *self,
                                  const gchar *name,
                                  GError **error)
{
    LrgAssetManagerPrivate *priv;
    g_autoptr(LrgAssetManager) keep_alive = NULL;
    g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
    g_autoptr(GPtrArray) previous = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr(GPtrArray) replacements = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr(GHashTable) affected = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_autoptr(GHashTable) ordered = g_hash_table_new (g_str_hash, g_str_equal);
    g_autoptr(GError) local_error = NULL;
    g_autofree gchar *root = NULL;
    GHashTableIter iter;
    gpointer key;
    guint i;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    priv = lrg_asset_manager_get_instance_private (self);
    if (priv->reloading)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PENDING,
                             "A definition reload is already in progress");
        return FALSE;
    }
    keep_alive = g_object_ref (self);
    root = g_strdup (name);
    priv->reloading = TRUE;
    if (!g_hash_table_contains (priv->object_cache, root))
    {
        g_set_error (&local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Definition is not cached: %s", root);
        goto failed;
    }
    g_hash_table_iter_init (&iter, priv->object_cache);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        if (object_depends_on (priv, key, root))
            g_hash_table_add (affected, g_strdup (key));

    /* Topological order, with lexical tie breaking for independent branches. */
    while (names->len < g_hash_table_size (affected))
    {
        const gchar *next = NULL;

        g_hash_table_iter_init (&iter, affected);
        while (g_hash_table_iter_next (&iter, &key, NULL))
        {
            GHashTable *deps;
            GHashTableIter dep_iter;
            gpointer dep;
            gboolean ready = TRUE;

            if (g_hash_table_contains (ordered, key))
                continue;
            deps = g_hash_table_lookup (priv->dependencies, key);
            if (deps != NULL)
            {
                g_hash_table_iter_init (&dep_iter, deps);
                while (g_hash_table_iter_next (&dep_iter, &dep, NULL))
                    if (g_hash_table_contains (affected, dep) &&
                        !g_hash_table_contains (ordered, dep))
                        ready = FALSE;
            }
            if (ready && (next == NULL || g_strcmp0 (key, next) < 0))
                next = key;
        }
        g_assert (next != NULL);
        g_hash_table_add (ordered, (gpointer) next);
        g_ptr_array_add (names, g_strdup (next));
    }
    for (i = 0; i < names->len; i++)
    {
        const gchar *current = g_ptr_array_index (names, i);
        ObjectAsset *asset = g_hash_table_lookup (priv->object_cache, current);
        GObject *replacement;

        replacement = lrg_data_loader_load_file_validated (priv->data_loader,
                                                           asset->path, &local_error);
        if (replacement == NULL)
        {
            if (local_error == NULL)
                g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                     "Definition loader returned no object");
            g_prefix_error (&local_error, "%s: ", current);
            goto failed;
        }
        g_ptr_array_add (replacements, replacement);
        if (G_OBJECT_TYPE (replacement) != G_OBJECT_TYPE (asset->object))
        {
            g_set_error (&local_error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE,
                         "Reload cannot change definition type: %s", current);
            goto failed;
        }
        g_ptr_array_add (previous, g_object_ref (asset->object));
    }
    /* Publish the entire batch before any observer can inspect the cache. */
    for (i = 0; i < names->len; i++)
    {
        ObjectAsset *asset = g_hash_table_lookup (priv->object_cache, g_ptr_array_index (names, i));

        g_set_object (&asset->object, g_ptr_array_index (replacements, i));
    }
    for (i = 0; i < names->len; i++)
        g_signal_emit (self, asset_signals[OBJECT_RELOADED], 0,
                       g_ptr_array_index (names, i), g_ptr_array_index (previous, i),
                       g_ptr_array_index (replacements, i));
    priv->reloading = FALSE;
    return TRUE;
failed:
    g_signal_emit (self, asset_signals[OBJECT_RELOAD_FAILED], 0, root, local_error);
    priv->reloading = FALSE;
    g_propagate_error (error, g_steal_pointer (&local_error));
    return FALSE;
}

static gboolean
reload_watched_object (gpointer data)
{
    ObjectWatch *watch = data;
    g_autoptr(LrgAssetManager) manager = g_object_ref (watch->manager);
    g_autofree gchar *name = g_strdup (watch->name);
    g_autoptr(GError) error = NULL;
    GSource *source = watch->pending;

    watch->pending = NULL;
    g_source_unref (source);
    /* Signal handlers may unwatch/unload this definition or release the manager. */
    lrg_asset_manager_reload_object (manager, name, &error);
    return G_SOURCE_REMOVE;
}

static void
object_file_changed (GFileMonitor      *monitor,
                     GFile             *file,
                     GFile             *other_file,
                     GFileMonitorEvent  event_type,
                     gpointer           data)
{
    ObjectWatch *watch = data;

    if (!g_file_equal (file, watch->file) &&
        (other_file == NULL || !g_file_equal (other_file, watch->file)))
        return;

    switch (event_type)
    {
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED:
    case G_FILE_MONITOR_EVENT_RENAMED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
        if (watch->pending != NULL)
        {
            g_source_destroy (watch->pending);
            g_source_unref (watch->pending);
        }
        watch->pending = g_timeout_source_new (100);
        g_source_set_callback (watch->pending, reload_watched_object, watch, NULL);
        g_source_attach (watch->pending, watch->context);
        break;
    default:
        break;
    }
}

gboolean
lrg_asset_manager_watch_object (LrgAssetManager  *self,
                                 const gchar      *name,
                                 GError          **error)
{
    LrgAssetManagerPrivate *priv;
    ObjectAsset *asset;
    ObjectWatch *watch;
    g_autofree gchar *path = NULL;
    g_autoptr(GFile) file = NULL;
    g_autoptr(GFile) parent = NULL;
    g_autoptr(GFileMonitor) monitor = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    priv = lrg_asset_manager_get_instance_private (self);
    if (g_hash_table_contains (priv->object_watches, name))
        return TRUE;
    asset = g_hash_table_lookup (priv->object_cache, name);
    path = asset != NULL ? g_strdup (asset->path) : resolve_asset_path (self, name);
    if (path == NULL)
    {
        g_set_error (error, LRG_ASSET_MANAGER_ERROR,
                     LRG_ASSET_MANAGER_ERROR_NOT_FOUND, "Definition not found: %s", name);
        return FALSE;
    }
    file = g_file_new_for_path (path);
    parent = g_file_get_parent (file);
    monitor = g_file_monitor_directory (parent, G_FILE_MONITOR_WATCH_MOVES, NULL, error);
    if (monitor == NULL)
        return FALSE;
    if (lrg_asset_manager_load_object (self, name, error) == NULL)
        return FALSE;

    watch = g_new0 (ObjectWatch, 1);
    watch->manager = self;
    watch->name = g_strdup (name);
    watch->file = g_steal_pointer (&file);
    watch->monitor = g_steal_pointer (&monitor);
    watch->context = g_main_context_ref_thread_default ();
    g_signal_connect (watch->monitor, "changed", G_CALLBACK (object_file_changed), watch);
    g_hash_table_insert (priv->object_watches, g_strdup (name), watch);
    return TRUE;
}

gboolean
lrg_asset_manager_unwatch_object (LrgAssetManager *self,
                                   const gchar     *name)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    priv = lrg_asset_manager_get_instance_private (self);
    return g_hash_table_remove (priv->object_watches, name);
}

GObject *
lrg_asset_manager_load_asset (LrgAssetManager  *self,
                               const gchar      *name,
                               GError          **error)
{
    const gchar *extension;
    const gchar *base;
    g_autofree gchar *lower = NULL;
    g_autofree gchar *norm = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    /* A guid reference dispatches on the extension of the file it names. */
    norm = normalize_name (self, name);
    base = strrchr (norm, G_DIR_SEPARATOR);
    extension = strrchr (base != NULL ? base : norm, '.');
    lower = g_ascii_strdown (extension != NULL ? extension : "", -1);

    if (g_str_equal (lower, ".yaml") || g_str_equal (lower, ".yml"))
        return lrg_asset_manager_load_object (self, norm, error);
    /* Fonts take the sidecar's size (32 without one). */
    if (g_str_equal (lower, ".ttf") || g_str_equal (lower, ".otf") || g_str_equal (lower, ".fnt"))
        return (GObject *)lrg_asset_manager_load_font (self, norm, 0, error);
    /* Audio honours the sidecar's stream/sample mode. */
    if (g_str_equal (lower, ".wav") || g_str_equal (lower, ".ogg") || g_str_equal (lower, ".mp3") ||
        g_str_equal (lower, ".flac") || g_str_equal (lower, ".xm") || g_str_equal (lower, ".mod"))
        return lrg_asset_manager_load_audio (self, norm, error);
    if (g_str_equal (lower, ".png") || g_str_equal (lower, ".jpg") ||
        g_str_equal (lower, ".jpeg") || g_str_equal (lower, ".bmp") ||
        g_str_equal (lower, ".tga") || g_str_equal (lower, ".gif") ||
        g_str_equal (lower, ".qoi") || g_str_equal (lower, ".dds") ||
        g_str_equal (lower, ".ktx") || g_str_equal (lower, ".pkm") ||
        g_str_equal (lower, ".pvr") || g_str_equal (lower, ".astc") ||
        g_str_equal (lower, ".pep"))
        return (GObject *)lrg_asset_manager_load_texture (self, norm, error);
    if (g_str_equal (lower, ".glb") || g_str_equal (lower, ".gltf") ||
        g_str_equal (lower, ".obj") || g_str_equal (lower, ".iqm") ||
        g_str_equal (lower, ".m3d") || g_str_equal (lower, ".vox"))
        return (GObject *)lrg_asset_manager_load_model (self, norm, error);
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                 "Unsupported asset extension: %s", name);
    return NULL;
}

#ifdef LRG_HAS_LIBDEX
/* ==========================================================================
 * Async Loading - Data Structures
 * ========================================================================== */

typedef struct
{
    LrgAssetManager *manager;
    gchar           *name;
    gint             size;  /* For fonts only */
} LoadAssetData;

static void
load_asset_data_free (LoadAssetData *data)
{
    g_clear_object (&data->manager);
    g_free (data->name);
    g_free (data);
}

/* ==========================================================================
 * Async Loading - Fiber Functions
 * ========================================================================== */

static DexFuture *
load_texture_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlTexture       *texture;

    texture = lrg_asset_manager_load_texture (data->manager, data->name, &error);
    if (texture == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    /* Return ref'd pointer since cache owns primary reference */
    return dex_future_new_for_pointer (g_object_ref (texture));
}

static DexFuture *
load_font_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlFont          *font;

    font = lrg_asset_manager_load_font (data->manager, data->name, data->size, &error);
    if (font == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (g_object_ref (font));
}

static DexFuture *
load_sound_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlSound         *sound;

    sound = lrg_asset_manager_load_sound (data->manager, data->name, &error);
    if (sound == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (g_object_ref (sound));
}

static DexFuture *
load_music_fiber (gpointer user_data)
{
    LoadAssetData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GrlMusic         *music;

    music = lrg_asset_manager_load_music (data->manager, data->name, &error);
    if (music == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (g_object_ref (music));
}

/* ==========================================================================
 * Public API - Asynchronous Loading
 * ========================================================================== */

/**
 * lrg_asset_manager_load_texture_async:
 * @self: an #LrgAssetManager
 * @name: relative path to texture file
 *
 * Asynchronously loads a texture.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlTexture
 */
DexFuture *
lrg_asset_manager_load_texture_async (LrgAssetManager *self,
                                      const gchar     *name)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_texture_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}

/**
 * lrg_asset_manager_load_font_async:
 * @self: an #LrgAssetManager
 * @name: relative path to font file
 * @size: font size in pixels
 *
 * Asynchronously loads a font.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlFont
 */
DexFuture *
lrg_asset_manager_load_font_async (LrgAssetManager *self,
                                   const gchar     *name,
                                   gint             size)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (size > 0, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);
    data->size = size;

    return dex_scheduler_spawn (NULL,
                                0,
                                load_font_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}

/**
 * lrg_asset_manager_load_sound_async:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file
 *
 * Asynchronously loads a sound effect.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlSound
 */
DexFuture *
lrg_asset_manager_load_sound_async (LrgAssetManager *self,
                                    const gchar     *name)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_sound_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}

/**
 * lrg_asset_manager_load_music_async:
 * @self: an #LrgAssetManager
 * @name: relative path to music file
 *
 * Asynchronously loads a music track.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlMusic
 */
DexFuture *
lrg_asset_manager_load_music_async (LrgAssetManager *self,
                                    const gchar     *name)
{
    LoadAssetData *data;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    data = g_new0 (LoadAssetData, 1);
    data->manager = g_object_ref (self);
    data->name = g_strdup (name);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_music_fiber,
                                data,
                                (GDestroyNotify)load_asset_data_free);
}
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Public API - Cache Management
 * ========================================================================== */

/*
 * lrg_asset_manager_unload:
 * @self: an #LrgAssetManager
 * @name: the asset name to unload
 *
 * Removes an asset from all caches.
 *
 * Returns: %TRUE if the asset was in a cache and removed
 */
gboolean
lrg_asset_manager_unload (LrgAssetManager *self,
                          const gchar     *name)
{
    LrgAssetManagerPrivate *priv;
    gboolean                removed = FALSE;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);

    /* guid references name the file they resolve to. */
    if (g_str_has_prefix (name, LRG_ASSET_GUID_PREFIX))
    {
        g_autofree gchar *path = resolve_guid_internal (self, name);

        if (path != NULL)
            return lrg_asset_manager_unload (self, path);
    }

    forget_object_dependencies (priv, name);
    g_hash_table_remove (priv->object_watches, name);
    if (g_hash_table_remove (priv->object_cache, name))
        removed = TRUE;

    /* Try to remove from each cache */
    if (g_hash_table_remove (priv->texture_cache, name))
    {
        removed = TRUE;
    }

    if (g_hash_table_remove (priv->sound_cache, name))
    {
        removed = TRUE;
    }

    if (g_hash_table_remove (priv->music_cache, name))
    {
        removed = TRUE;
    }

    /* Models and animation sets are keyed by canonical path. */
    {
        g_autofree gchar *key = model_cache_key (self, name);

        if (key != NULL && g_hash_table_remove (priv->model_cache, key))
            removed = TRUE;
        if (key != NULL && g_hash_table_remove (priv->animation_cache, key))
            removed = TRUE;
    }

    /* For fonts, we need to check keys that start with name: */
    {
        GHashTableIter iter;
        gpointer       key;
        GList         *to_remove = NULL;
        GList         *l;

        g_hash_table_iter_init (&iter, priv->font_cache);
        while (g_hash_table_iter_next (&iter, &key, NULL))
        {
            const gchar *cache_key = key;

            if (g_str_has_prefix (cache_key, name) &&
                cache_key[strlen (name)] == ':')
            {
                to_remove = g_list_prepend (to_remove, g_strdup (cache_key));
            }
        }

        for (l = to_remove; l != NULL; l = l->next)
        {
            g_hash_table_remove (priv->font_cache, l->data);
            removed = TRUE;
        }

        g_list_free_full (to_remove, g_free);
    }

    if (removed)
    {
        lrg_debug (LRG_LOG_DOMAIN_CORE,
                   "Unloaded asset: %s",
                   name);
    }

    return removed;
}

/*
 * lrg_asset_manager_unload_all:
 * @self: an #LrgAssetManager
 *
 * Clears all cached assets.
 */
void
lrg_asset_manager_unload_all (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));

    priv = lrg_asset_manager_get_instance_private (self);

    g_hash_table_remove_all (priv->object_watches);
    g_hash_table_remove_all (priv->object_cache);
    g_hash_table_remove_all (priv->dependencies);
    g_hash_table_remove_all (priv->texture_cache);
    g_hash_table_remove_all (priv->font_cache);
    g_hash_table_remove_all (priv->sound_cache);
    g_hash_table_remove_all (priv->music_cache);
    g_hash_table_remove_all (priv->model_cache);
    g_hash_table_remove_all (priv->animation_cache);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Unloaded all cached assets");
}

/*
 * lrg_asset_manager_is_cached:
 * @self: an #LrgAssetManager
 * @name: the asset name to check
 *
 * Checks if an asset is currently in any cache.
 *
 * Returns: %TRUE if the asset is cached
 */
gboolean
lrg_asset_manager_is_cached (LrgAssetManager *self,
                             const gchar     *name)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);

    /* guid references name the file they resolve to. */
    if (g_str_has_prefix (name, LRG_ASSET_GUID_PREFIX))
    {
        g_autofree gchar *path = resolve_guid_internal (self, name);

        if (path != NULL)
            return lrg_asset_manager_is_cached (self, path);
    }

    if (g_hash_table_contains (priv->object_cache, name))
        return TRUE;

    /* Check each cache */
    if (g_hash_table_contains (priv->texture_cache, name))
    {
        return TRUE;
    }

    if (g_hash_table_contains (priv->sound_cache, name))
    {
        return TRUE;
    }

    if (g_hash_table_contains (priv->music_cache, name))
    {
        return TRUE;
    }

    {
        g_autofree gchar *key = model_cache_key (self, name);

        if (key != NULL && (g_hash_table_contains (priv->model_cache, key) ||
                            g_hash_table_contains (priv->animation_cache, key)))
            return TRUE;
    }

    /* For fonts, check if any key starts with name: */
    {
        GHashTableIter iter;
        gpointer       key;

        g_hash_table_iter_init (&iter, priv->font_cache);
        while (g_hash_table_iter_next (&iter, &key, NULL))
        {
            const gchar *cache_key = key;

            if (g_str_has_prefix (cache_key, name) &&
                cache_key[strlen (name)] == ':')
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/*
 * lrg_asset_manager_get_texture_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached textures.
 *
 * Returns: The texture cache size
 */
guint
lrg_asset_manager_get_texture_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->texture_cache);
}

/*
 * lrg_asset_manager_get_font_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached fonts.
 *
 * Returns: The font cache size
 */
guint
lrg_asset_manager_get_font_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->font_cache);
}

/*
 * lrg_asset_manager_get_sound_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached sounds.
 *
 * Returns: The sound cache size
 */
guint
lrg_asset_manager_get_sound_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->sound_cache);
}

/*
 * lrg_asset_manager_get_music_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached music tracks.
 *
 * Returns: The music cache size
 */
guint
lrg_asset_manager_get_music_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);

    return g_hash_table_size (priv->music_cache);
}

/* ==========================================================================
 * Models
 * ========================================================================== */

gchar *
lrg_asset_manager_resolve_path (LrgAssetManager *self,
                                const gchar     *name)
{
    g_autofree gchar *path = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    path = resolve_asset_path (self, name);
    if (path == NULL)
        return NULL;
    return g_canonicalize_filename (path, NULL);
}

GrlModel *
lrg_asset_manager_load_model (LrgAssetManager  *self,
                              const gchar      *name,
                              GError          **error)
{
    LrgAssetManagerPrivate *priv;
    g_autofree gchar       *key = NULL;
    ModelAsset             *asset;
    GrlModel               *model;
    GError                 *load_error = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    priv = lrg_asset_manager_get_instance_private (self);

    /* Resolve to the canonical file first; an absolute name must exist. */
    key = model_cache_key (self, name);
    if (key != NULL)
    {
        asset = g_hash_table_lookup (priv->model_cache, key);
        if (asset != NULL)
            return asset->model;
    }
    if (key == NULL || !g_file_test (key, G_FILE_TEST_IS_REGULAR))
    {
        g_set_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Model not found: %s", name);
        return NULL;
    }

    /* Meshes are uploaded to VRAM while loading. */
    if (!IsWindowReady ())
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                             "A graphics context is required to load this asset");
        return NULL;
    }

    model = grl_model_new_from_file (key, &load_error);
    if (model == NULL || !grl_model_is_valid (model))
    {
        g_set_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_LOAD_FAILED,
                     "Failed to load model %s: %s", key,
                     load_error != NULL ? load_error->message : "invalid model");
        g_clear_error (&load_error);
        g_clear_object (&model);
        return NULL;
    }

    /* Importer settings: scale, up axis, normals. */
    {
        g_autoptr(LrgAssetMeta) meta = meta_for_path (self, key);

        if (meta != NULL)
            apply_model_settings (model, meta);
    }

    asset = g_new0 (ModelAsset, 1);
    asset->model = model;
    asset->textures = g_array_new (FALSE, FALSE, sizeof (Texture2D));
    model_asset_collect_textures (asset);
    g_hash_table_insert (priv->model_cache, g_strdup (key), asset);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Loaded model '%s' from %s (%d meshes, %u textures)",
               name, key, grl_model_get_mesh_count (model), asset->textures->len);
    return model;
}

GPtrArray *
lrg_asset_manager_load_model_animations (LrgAssetManager  *self,
                                         const gchar      *name,
                                         GError          **error)
{
    LrgAssetManagerPrivate *priv;
    g_autofree gchar       *key = NULL;
    GPtrArray              *clips;
    GrlModelAnimation     **loaded;
    gint                    count = 0;
    gint                    i;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    priv = lrg_asset_manager_get_instance_private (self);

    key = model_cache_key (self, name);
    if (key != NULL)
    {
        clips = g_hash_table_lookup (priv->animation_cache, key);
        if (clips != NULL)
            return clips;
    }
    if (key == NULL || !g_file_test (key, G_FILE_TEST_IS_REGULAR))
    {
        g_set_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
                     "Model animations not found: %s", name);
        return NULL;
    }

    /* Animation data is CPU-only and loads without a graphics context.
     * raylib reports "no animations" and "cannot parse" the same way, so
     * both yield an empty (cached) set. */
    clips = g_ptr_array_new_with_free_func (g_object_unref);
    loaded = grl_model_animation_load (key, &count, NULL);
    for (i = 0; loaded != NULL && i < count; i++)
        g_ptr_array_add (clips, loaded[i]);
    g_free (loaded);

    /* raylib 6.0 poses every root joint but the first without its
     * armature transform; correct glTF clips before they are shared. */
    if (clips->len > 0 && (g_str_has_suffix (key, ".glb") || g_str_has_suffix (key, ".gltf") ||
                           g_str_has_suffix (key, ".GLB") || g_str_has_suffix (key, ".GLTF")))
    {
        g_autoptr(GError)      info_error = NULL;
        g_autoptr(LrgGltfInfo) info = lrg_gltf_info_new_from_file (key, &info_error);

        if (info != NULL)
        {
            guint patched = lrg_gltf_info_fix_animation_roots (info, clips);

            lrg_debug (LRG_LOG_DOMAIN_CORE, "Fixed multi-root poses in %u clips of %s",
                       patched, key);
        }
        else
            lrg_debug (LRG_LOG_DOMAIN_CORE, "Cannot inspect %s for root fix-up: %s",
                       key, info_error->message);
    }

    g_hash_table_insert (priv->animation_cache, g_strdup (key), clips);
    lrg_debug (LRG_LOG_DOMAIN_CORE, "Loaded %u animation clips from %s", clips->len, key);
    return clips;
}

guint
lrg_asset_manager_get_model_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);
    return g_hash_table_size (priv->model_cache);
}

guint
lrg_asset_manager_get_animation_cache_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);
    return g_hash_table_size (priv->animation_cache);
}

/* ==========================================================================
 * Projects and the guid index (since 0.9.0)
 *
 * The index maps asset guids (from `.meta` sidecars, or the legacy
 * path-derived guid for files without one) to canonical absolute paths.
 * It is built with the same walk as the editor's LrgAssetDatabase scan
 * (sorted names, version-control directories skipped, sidecars never
 * indexed as assets, first file wins on a duplicate guid), so a guid the
 * editor shows resolves to the same file at runtime.
 * ========================================================================== */

static gboolean
is_vcs_dir_name (const gchar *name)
{
    return g_str_equal (name, ".git") || g_str_equal (name, ".hg") || g_str_equal (name, ".svn");
}

static gint
cmp_names (gconstpointer a,
           gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* Adds one asset file; @rel is its path relative to the asset dir. */
static void
index_add_file (LrgAssetManagerPrivate *priv,
                const gchar            *full,
                const gchar            *rel)
{
    g_autofree gchar       *meta_path = lrg_asset_meta_path_for_asset (full);
    g_autofree gchar       *guid = NULL;
    g_autofree gchar       *canon = g_canonicalize_filename (full, NULL);
    g_autoptr(LrgAssetMeta) meta = NULL;

    /* A readable sidecar with a guid wins; a missing, broken or guid-less
     * sidecar falls back to the legacy guid, as in the editor. */
    if (g_file_test (meta_path, G_FILE_TEST_IS_REGULAR))
        meta = lrg_asset_meta_new_from_file (meta_path, NULL);
    if (meta != NULL && lrg_asset_meta_get_guid (meta) != NULL)
        guid = g_strdup (lrg_asset_meta_get_guid (meta));
    else
        guid = lrg_asset_meta_compute_legacy_guid (rel);

    if (!g_hash_table_contains (priv->guid_to_path, guid))
    {
        g_hash_table_insert (priv->guid_to_path, g_strdup (guid), g_strdup (canon));
        if (!g_hash_table_contains (priv->path_to_guid, canon))
            g_hash_table_insert (priv->path_to_guid, g_strdup (canon), g_strdup (guid));
    }
    else
        lrg_debug (LRG_LOG_DOMAIN_CORE, "Asset %s duplicates guid %s; keeping %s",
                   canon, guid, (const gchar *) g_hash_table_lookup (priv->guid_to_path, guid));
}

static void
index_scan_dir (LrgAssetManagerPrivate *priv,
                const gchar            *base,
                const gchar            *rel)
{
    g_autofree gchar    *full = (rel != NULL) ? g_build_filename (base, rel, NULL) : g_strdup (base);
    g_autoptr(GDir)      dir = NULL;
    g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
    const gchar         *name;
    guint                i;

    dir = g_dir_open (full, 0, NULL);
    if (dir == NULL)
        return;

    /* Sorted so duplicate-guid resolution matches the editor. */
    while ((name = g_dir_read_name (dir)) != NULL)
        g_ptr_array_add (names, g_strdup (name));
    g_ptr_array_sort (names, cmp_names);

    for (i = 0; i < names->len; i++)
    {
        const gchar      *child = g_ptr_array_index (names, i);
        g_autofree gchar *child_rel = (rel != NULL) ? g_build_filename (rel, child, NULL) : g_strdup (child);
        g_autofree gchar *child_full = g_build_filename (full, child, NULL);

        if (g_file_test (child_full, G_FILE_TEST_IS_DIR))
        {
            if (!is_vcs_dir_name (child))
                index_scan_dir (priv, base, child_rel);
        }
        else if (g_file_test (child_full, G_FILE_TEST_IS_REGULAR) && !lrg_asset_meta_is_meta_path (child))
            index_add_file (priv, child_full, child_rel);
    }
}

static guint
rebuild_index (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);
    GPtrArray              *dirs;
    guint                   i;

    g_hash_table_remove_all (priv->guid_to_path);
    g_hash_table_remove_all (priv->path_to_guid);

    /* Asset dirs when set, else the search paths (lowest priority first,
     * like the editor's directory order). */
    dirs = priv->asset_dirs->len > 0 ? priv->asset_dirs : priv->search_paths;
    for (i = 0; i < dirs->len; i++)
    {
        g_autofree gchar *abs = g_canonicalize_filename (g_ptr_array_index (dirs, i), NULL);

        if (g_file_test (abs, G_FILE_TEST_IS_DIR))
            index_scan_dir (priv, abs, NULL);
    }

    priv->index_valid = TRUE;
    priv->index_cached = FALSE;
    lrg_debug (LRG_LOG_DOMAIN_CORE, "Indexed %u asset guids",
               g_hash_table_size (priv->guid_to_path));
    return g_hash_table_size (priv->guid_to_path);
}

static void
invalidate_index (LrgAssetManagerPrivate *priv)
{
    priv->index_valid = FALSE;
    priv->index_cached = FALSE;
    g_hash_table_remove_all (priv->guid_to_path);
    g_hash_table_remove_all (priv->path_to_guid);
}

/* @guid without its "guid:" prefix. */
static const gchar *
strip_guid_prefix (const gchar *guid)
{
    return g_str_has_prefix (guid, LRG_ASSET_GUID_PREFIX) ? guid + strlen (LRG_ASSET_GUID_PREFIX) : guid;
}

/*
 * resolve_guid_internal:
 *
 * Index lookup with one self-healing rebuild: when the entry is missing
 * from a cached index (a new asset) or names a file that is gone (a
 * moved asset), the index is rebuilt from disk and the lookup retried.
 */
static gchar *
resolve_guid_internal (LrgAssetManager *self,
                       const gchar     *guid)
{
    LrgAssetManagerPrivate *priv = lrg_asset_manager_get_instance_private (self);
    const gchar            *key = strip_guid_prefix (guid);
    const gchar            *path;
    gboolean                rebuilt = FALSE;

    if (*key == '\0')
        return NULL;
    if (!priv->index_valid)
    {
        rebuild_index (self);
        rebuilt = TRUE;
    }

    path = g_hash_table_lookup (priv->guid_to_path, key);
    if ((path == NULL && priv->index_cached) ||
        (path != NULL && !g_file_test (path, G_FILE_TEST_IS_REGULAR) && !rebuilt))
    {
        rebuild_index (self);
        path = g_hash_table_lookup (priv->guid_to_path, key);
    }

    if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
        return NULL;
    return g_strdup (path);
}

/*
 * read_project_manifest:
 *
 * Reads the keys of `<root>/project.ryaml` the runtime cares about:
 * `project.asset_dirs` (added as guid index dirs) and
 * `project.apply_import_settings` (also spelled `apply-import-settings`).
 * Returns FALSE when there is no readable manifest.
 */
static gboolean
read_project_manifest (LrgAssetManager *self,
                       const gchar     *root)
{
    g_autofree gchar      *path = g_build_filename (root, "project.ryaml", NULL);
    g_autoptr(YamlParser)  parser = NULL;
    YamlNode              *doc;
    YamlNode              *proj_node;
    YamlMapping           *proj;
    YamlNode              *dirs;
    const gchar           *apply_key = NULL;

    if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
        return FALSE;
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, NULL))
        return FALSE;
    doc = yaml_parser_get_root (parser);
    if (doc == NULL || yaml_node_get_node_type (doc) != YAML_NODE_MAPPING)
        return FALSE;
    proj_node = yaml_mapping_get_member (yaml_node_get_mapping (doc), "project");
    if (proj_node == NULL || yaml_node_get_node_type (proj_node) != YAML_NODE_MAPPING)
        return FALSE;
    proj = yaml_node_get_mapping (proj_node);

    dirs = yaml_mapping_get_member (proj, "asset_dirs");
    if (dirs != NULL && yaml_node_get_node_type (dirs) == YAML_NODE_SEQUENCE)
    {
        YamlSequence *seq = yaml_node_get_sequence (dirs);
        guint         i;

        for (i = 0; i < yaml_sequence_get_length (seq); i++)
        {
            const gchar *dir = yaml_sequence_get_string_element (seq, i);

            if (dir != NULL && *dir != '\0')
                lrg_asset_manager_add_asset_dir (self, dir);
        }
    }

    if (yaml_mapping_has_member (proj, "apply_import_settings"))
        apply_key = "apply_import_settings";
    else if (yaml_mapping_has_member (proj, "apply-import-settings"))
        apply_key = "apply-import-settings";
    if (apply_key != NULL)
        lrg_asset_manager_set_apply_import_settings (self, yaml_mapping_get_boolean_member (proj, apply_key));

    return TRUE;
}

void
lrg_asset_manager_set_project_root (LrgAssetManager *self,
                                    const gchar     *root)
{
    LrgAssetManagerPrivate *priv;
    g_autofree gchar       *canon = NULL;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));

    priv = lrg_asset_manager_get_instance_private (self);
    canon = (root != NULL) ? g_canonicalize_filename (root, NULL) : NULL;
    if (g_strcmp0 (canon, priv->project_root) == 0)
        return;

    /* Forget the old project's root search path and index dirs. */
    if (priv->project_root != NULL)
        lrg_asset_manager_remove_search_path (self, priv->project_root);
    g_ptr_array_set_size (priv->asset_dirs, 0);
    invalidate_index (priv);
    g_free (priv->project_root);
    priv->project_root = g_steal_pointer (&canon);

    if (priv->project_root != NULL)
    {
        /* Lowest priority: explicit search paths (mods) still override. */
        g_ptr_array_insert (priv->search_paths, 0, g_strdup (priv->project_root));
        if (!read_project_manifest (self, priv->project_root))
            lrg_asset_manager_add_asset_dir (self, priv->project_root);
    }

    g_object_notify_by_pspec (G_OBJECT (self), asset_properties[PROP_PROJECT_ROOT]);
}

const gchar *
lrg_asset_manager_get_project_root (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);

    priv = lrg_asset_manager_get_instance_private (self);
    return priv->project_root;
}

void
lrg_asset_manager_add_asset_dir (LrgAssetManager *self,
                                 const gchar     *dir)
{
    LrgAssetManagerPrivate *priv;
    gchar                  *abs;
    guint                   i;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));
    g_return_if_fail (dir != NULL);

    priv = lrg_asset_manager_get_instance_private (self);
    abs = g_canonicalize_filename (dir, priv->project_root);
    for (i = 0; i < priv->asset_dirs->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (priv->asset_dirs, i), abs) == 0)
        {
            g_free (abs);
            return;
        }
    }
    g_ptr_array_add (priv->asset_dirs, abs);
    invalidate_index (priv);
}

const GPtrArray *
lrg_asset_manager_get_asset_dirs (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);

    priv = lrg_asset_manager_get_instance_private (self);
    return priv->asset_dirs;
}

void
lrg_asset_manager_clear_asset_dirs (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));

    priv = lrg_asset_manager_get_instance_private (self);
    g_ptr_array_set_size (priv->asset_dirs, 0);
    invalidate_index (priv);
}

guint
lrg_asset_manager_rebuild_guid_index (LrgAssetManager *self)
{
    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    return rebuild_index (self);
}

guint
lrg_asset_manager_get_guid_index_size (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), 0);

    priv = lrg_asset_manager_get_instance_private (self);
    return g_hash_table_size (priv->guid_to_path);
}

gchar *
lrg_asset_manager_resolve_guid (LrgAssetManager *self,
                                const gchar     *guid)
{
    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (guid != NULL, NULL);

    return resolve_guid_internal (self, guid);
}

gchar *
lrg_asset_manager_lookup_guid (LrgAssetManager *self,
                               const gchar     *name)
{
    LrgAssetManagerPrivate *priv;
    g_autofree gchar       *path = NULL;
    g_autofree gchar       *canon = NULL;
    const gchar            *guid;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_asset_manager_get_instance_private (self);
    if (g_str_has_prefix (name, LRG_ASSET_GUID_PREFIX))
    {
        path = resolve_guid_internal (self, name);
        return path != NULL ? g_strdup (strip_guid_prefix (name)) : NULL;
    }

    path = resolve_asset_path (self, name);
    if (path == NULL)
        return NULL;
    canon = g_canonicalize_filename (path, NULL);
    if (!priv->index_valid)
        rebuild_index (self);
    guid = g_hash_table_lookup (priv->path_to_guid, canon);
    if (guid == NULL && priv->index_cached)
    {
        rebuild_index (self);
        guid = g_hash_table_lookup (priv->path_to_guid, canon);
    }
    return g_strdup (guid);
}

gchar *
lrg_asset_manager_resolve_reference (LrgAssetManager *self,
                                     const gchar     *ref)
{
    g_autofree gchar *path = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (ref != NULL, NULL);

    path = resolve_asset_path (self, ref);
    if (path == NULL)
        return NULL;
    return g_canonicalize_filename (path, NULL);
}

gboolean
lrg_asset_manager_save_guid_index (LrgAssetManager  *self,
                                   const gchar      *path,
                                   GError          **error)
{
    LrgAssetManagerPrivate   *priv;
    g_autoptr(YamlMapping)    root_map = yaml_mapping_new ();
    g_autoptr(YamlMapping)    assets = yaml_mapping_new ();
    g_autoptr(YamlNode)       root = NULL;
    g_autoptr(YamlDocument)   doc = NULL;
    g_autoptr(YamlGenerator)  gen = NULL;
    g_autoptr(GList)          keys = NULL;
    g_autofree gchar         *data = NULL;
    GList                    *l;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);
    if (!priv->index_valid)
        rebuild_index (self);

    /* Sorted by guid so the file diffs cleanly. */
    keys = g_hash_table_get_keys (priv->guid_to_path);
    keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);
    for (l = keys; l != NULL; l = l->next)
    {
        const gchar *abs = g_hash_table_lookup (priv->guid_to_path, l->data);
        gsize        n = priv->project_root != NULL ? strlen (priv->project_root) : 0;

        if (priv->project_root != NULL && g_str_has_prefix (abs, priv->project_root) &&
            abs[n] == G_DIR_SEPARATOR)
            yaml_mapping_set_string_member (assets, l->data, abs + n + 1);
        else
            yaml_mapping_set_string_member (assets, l->data, abs);
    }

    yaml_mapping_set_int_member (root_map, "version", LRG_ASSET_GUID_INDEX_VERSION);
    yaml_mapping_set_mapping_member (root_map, "assets", assets);
    root = yaml_node_new_mapping (root_map);
    doc = yaml_document_new_with_root (root);
    gen = yaml_generator_new ();
    yaml_generator_set_document (gen, doc);
    data = yaml_generator_to_data (gen, NULL, error);
    if (data == NULL)
        return FALSE;
    return g_file_set_contents (path, data, -1, error);
}

gboolean
lrg_asset_manager_load_guid_index (LrgAssetManager  *self,
                                   const gchar      *path,
                                   GError          **error)
{
    LrgAssetManagerPrivate *priv;
    g_autoptr(YamlParser)   parser = NULL;
    g_autofree gchar       *dir = NULL;
    YamlNode               *doc;
    YamlNode               *assets_node;
    YamlMapping            *assets;
    GList                  *members;
    GList                  *l;
    const gchar            *base;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    priv = lrg_asset_manager_get_instance_private (self);
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, error))
        return FALSE;
    doc = yaml_parser_get_root (parser);
    if (doc == NULL || yaml_node_get_node_type (doc) != YAML_NODE_MAPPING)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Guid index %s is not a YAML mapping", path);
        return FALSE;
    }
    assets_node = yaml_mapping_get_member (yaml_node_get_mapping (doc), "assets");
    if (assets_node == NULL || yaml_node_get_node_type (assets_node) != YAML_NODE_MAPPING)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Guid index %s has no 'assets' mapping", path);
        return FALSE;
    }

    /* Relative entries are project-relative (else index-file-relative). */
    dir = g_path_get_dirname (path);
    base = priv->project_root != NULL ? priv->project_root : dir;

    g_hash_table_remove_all (priv->guid_to_path);
    g_hash_table_remove_all (priv->path_to_guid);
    assets = yaml_node_get_mapping (assets_node);
    members = yaml_mapping_get_members (assets);
    for (l = members; l != NULL; l = l->next)
    {
        const gchar      *rel = yaml_mapping_get_string_member (assets, l->data);
        g_autofree gchar *abs = NULL;

        if (rel == NULL || *rel == '\0')
            continue;
        abs = g_canonicalize_filename (rel, base);
        g_hash_table_insert (priv->guid_to_path, g_strdup (l->data), g_strdup (abs));
        if (!g_hash_table_contains (priv->path_to_guid, abs))
            g_hash_table_insert (priv->path_to_guid, g_strdup (abs), g_strdup (l->data));
    }
    g_list_free (members);

    priv->index_valid = TRUE;
    priv->index_cached = TRUE;
    return TRUE;
}

/* ==========================================================================
 * Runtime import settings - public API (since 0.9.0)
 * ========================================================================== */

void
lrg_asset_manager_set_apply_import_settings (LrgAssetManager *self,
                                             gboolean         apply)
{
    LrgAssetManagerPrivate *priv;

    g_return_if_fail (LRG_IS_ASSET_MANAGER (self));

    priv = lrg_asset_manager_get_instance_private (self);
    apply = !!apply;
    if (priv->apply_import_settings == apply)
        return;
    priv->apply_import_settings = apply;
    g_object_notify_by_pspec (G_OBJECT (self), asset_properties[PROP_APPLY_IMPORT_SETTINGS]);
}

gboolean
lrg_asset_manager_get_apply_import_settings (LrgAssetManager *self)
{
    LrgAssetManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), FALSE);

    priv = lrg_asset_manager_get_instance_private (self);
    return priv->apply_import_settings;
}

LrgAssetMeta *
lrg_asset_manager_get_asset_meta (LrgAssetManager *self,
                                  const gchar     *name)
{
    g_autofree gchar *path = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    path = resolve_asset_path (self, name);
    if (path == NULL)
        return NULL;
    return lrg_asset_meta_load_for_asset (path, NULL);
}

GObject *
lrg_asset_manager_load_audio (LrgAssetManager  *self,
                              const gchar      *name,
                              GError          **error)
{
    g_autofree gchar       *norm = NULL;
    g_autofree gchar       *path = NULL;
    g_autofree gchar       *lower = NULL;
    g_autoptr(LrgAssetMeta) meta = NULL;
    gboolean                stream;

    g_return_val_if_fail (LRG_IS_ASSET_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    norm = normalize_name (self, name);
    path = resolve_asset_path (self, norm);
    meta = meta_for_path (self, path);

    /* The sidecar decides; without one: wav is a sample, the rest stream. */
    lower = g_ascii_strdown (norm, -1);
    if (meta != NULL)
        stream = lrg_asset_meta_get_audio_mode (meta) == LRG_ASSET_AUDIO_MODE_STREAM;
    else
        stream = !g_str_has_suffix (lower, ".wav");

    if (stream)
        return (GObject *) lrg_asset_manager_load_music (self, norm, error);
    return (GObject *) lrg_asset_manager_load_sound (self, norm, error);
}
