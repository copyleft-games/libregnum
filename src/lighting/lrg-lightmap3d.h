/* lrg-lightmap3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmap3D - baked lighting of static 3D geometry.
 *
 * A lightmap is a set of HDR atlas pages (linear float RGB, one
 * #LrgHdrImage each) plus one entry per lightmapped object, keyed by the
 * object's node / entity GUID string: the page the object's charts live
 * on and the scale / offset that maps the object's second UV set (uv2,
 * glTF TEXCOORD_1) into that page:
 *
 *   atlas_uv = uv2 * scale_offset.xy + scale_offset.zw
 *
 * Each texel holds the irradiance / pi reaching the surface (the
 * diffuse light a white Lambert surface reflects), in linear radiance,
 * so the lit shader multiplies it by the albedo.  Only that
 * non-directional term is stored: normal maps do not modulate baked
 * light in 0.9.0 (see docs/modules/lighting/lightmap3d.org).
 *
 * On disk a lightmap is a `.rlightmap` YAML index next to its pages,
 * which are Radiance `.hdr` files (lrg_lightmap3d_save()).  The data
 * side is headless; lrg_lightmap3d_upload() needs a GL context.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../graphics/lrg-hdr-image.h"

G_BEGIN_DECLS

/**
 * LRG_LIGHTMAP3D_FORMAT_VERSION:
 *
 * The `version:` written into `.rlightmap` files; files of a newer
 * version are refused.
 */
#define LRG_LIGHTMAP3D_FORMAT_VERSION (1)

/**
 * LRG_LIGHTMAP3D_MAX_PAGES:
 *
 * Most atlas pages one lightmap holds.
 */
#define LRG_LIGHTMAP3D_MAX_PAGES (64)

/**
 * LrgLightMode:
 * @LRG_LIGHT_MODE_REALTIME: lit at runtime only; the baker ignores it
 * @LRG_LIGHT_MODE_MIXED: lit at runtime everywhere; the baker adds its
 *   indirect (bounced) light to lightmaps and probes
 * @LRG_LIGHT_MODE_BAKED: the baker stores its direct and indirect light
 *   in the lightmaps (indirect only in probes); at runtime it is skipped
 *   on lightmapped draws and still lights everything else (dynamic and
 *   probe-lit objects)
 *
 * How a light takes part in baked lighting (#LrgLight3DDesc's
 * bake_mode, #LrgLightComponent:bake-mode, lrg_light_set_set_light_mode()).
 */
typedef enum
{
	LRG_LIGHT_MODE_REALTIME,
	LRG_LIGHT_MODE_MIXED,
	LRG_LIGHT_MODE_BAKED
} LrgLightMode;

#define LRG_TYPE_LIGHT_MODE (lrg_light_mode_get_type ())

/**
 * lrg_light_mode_get_type:
 *
 * Returns: the #GType of #LrgLightMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_light_mode_get_type (void) G_GNUC_CONST;

/**
 * LrgLightmap3DError:
 * @LRG_LIGHTMAP3D_ERROR_FAILED: generic failure
 * @LRG_LIGHTMAP3D_ERROR_IO: a file could not be read or written
 * @LRG_LIGHTMAP3D_ERROR_FORMAT: the index or a page is malformed
 * @LRG_LIGHTMAP3D_ERROR_VERSION: the file is of a newer format version
 * @LRG_LIGHTMAP3D_ERROR_GPU: no GL context or the upload failed
 *
 * Errors of #LrgLightmap3D.
 */
typedef enum
{
	LRG_LIGHTMAP3D_ERROR_FAILED,
	LRG_LIGHTMAP3D_ERROR_IO,
	LRG_LIGHTMAP3D_ERROR_FORMAT,
	LRG_LIGHTMAP3D_ERROR_VERSION,
	LRG_LIGHTMAP3D_ERROR_GPU
} LrgLightmap3DError;

/**
 * LRG_LIGHTMAP3D_ERROR:
 *
 * Error domain of #LrgLightmap3DError.
 */
#define LRG_LIGHTMAP3D_ERROR (lrg_lightmap3d_error_quark ())

#define LRG_TYPE_LIGHTMAP3D_ERROR (lrg_lightmap3d_error_get_type ())

/**
 * lrg_lightmap3d_error_get_type:
 *
 * Returns: the #GType of #LrgLightmap3DError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_lightmap3d_error_get_type (void) G_GNUC_CONST;

/**
 * lrg_lightmap3d_error_quark:
 *
 * Returns: the error quark of %LRG_LIGHTMAP3D_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_lightmap3d_error_quark (void);

#define LRG_TYPE_LIGHTMAP3D (lrg_lightmap3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightmap3D, lrg_lightmap3d, LRG, LIGHTMAP3D, GObject)

/**
 * lrg_lightmap3d_new:
 *
 * Creates an empty lightmap: no pages, no entries, no settings,
 * intensity 1.
 *
 * Returns: (transfer full): a new #LrgLightmap3D
 */
LRG_AVAILABLE_IN_ALL
LrgLightmap3D * lrg_lightmap3d_new (void);

/**
 * lrg_lightmap3d_new_from_file:
 * @path: a `.rlightmap` index file
 * @error: return location for a #GError
 *
 * Loads an index and its pages (paths in `pages:` are relative to the
 * index's directory).  The result remembers @path
 * (lrg_lightmap3d_get_path()).
 *
 * Returns: (transfer full) (nullable): the lightmap, %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgLightmap3D * lrg_lightmap3d_new_from_file (const gchar  *path,
                                              GError      **error);

/**
 * lrg_lightmap3d_save:
 * @self: an #LrgLightmap3D
 * @path: the `.rlightmap` index to write
 * @error: return location for a #GError
 *
 * Writes the index to @path and each page next to it as
 * `<basename>-<n>.hdr` (basename = @path without its extension).
 * Remembers @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_save (LrgLightmap3D  *self,
                              const gchar    *path,
                              GError        **error);

/**
 * lrg_lightmap3d_get_path:
 * @self: an #LrgLightmap3D
 *
 * Returns: (transfer none) (nullable): the file the lightmap was last
 *   loaded from or saved to
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_lightmap3d_get_path (LrgLightmap3D *self);

/**
 * lrg_lightmap3d_clear:
 * @self: an #LrgLightmap3D
 *
 * Removes every page, entry and setting (and the GPU textures).  Emits
 * #LrgLightmap3D::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap3d_clear (LrgLightmap3D *self);

/* --- pages -------------------------------------------------------------- */

/**
 * lrg_lightmap3d_add_page:
 * @self: an #LrgLightmap3D
 * @image: (transfer none): the page (linear RGB, rows from the top =
 *   atlas v 0); a reference is kept
 *
 * Appends a page.  Emits #LrgLightmap3D::changed.
 *
 * Returns: the page index, or %G_MAXUINT when %LRG_LIGHTMAP3D_MAX_PAGES
 *   pages exist already
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap3d_add_page (LrgLightmap3D *self,
                               LrgHdrImage   *image);

/**
 * lrg_lightmap3d_set_page:
 * @self: an #LrgLightmap3D
 * @page: an existing page index
 * @image: (transfer none): the new page image
 *
 * Replaces a page (its GPU texture is re-uploaded on the next
 * lrg_lightmap3d_upload()).  Emits #LrgLightmap3D::changed.
 *
 * Returns: %FALSE when @page does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_set_page (LrgLightmap3D *self,
                                  guint          page,
                                  LrgHdrImage   *image);

/**
 * lrg_lightmap3d_get_page:
 * @self: an #LrgLightmap3D
 * @page: a page index
 *
 * Returns: (transfer none) (nullable): the page image, %NULL when @page
 *   does not exist
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage * lrg_lightmap3d_get_page (LrgLightmap3D *self,
                                       guint          page);

/**
 * lrg_lightmap3d_get_n_pages:
 * @self: an #LrgLightmap3D
 *
 * Returns: the number of pages
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap3d_get_n_pages (LrgLightmap3D *self);

/**
 * lrg_lightmap3d_notify_page_changed:
 * @self: an #LrgLightmap3D
 * @page: a page index
 *
 * Tells the lightmap the pixels of @page were written in place (through
 * lrg_hdr_image_get_data() or lrg_hdr_image_set_pixel()): its GPU
 * texture is re-uploaded on the next lrg_lightmap3d_upload() and
 * #LrgLightmap3D::changed is emitted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap3d_notify_page_changed (LrgLightmap3D *self,
                                         guint          page);

/* --- entries ------------------------------------------------------------ */

/**
 * lrg_lightmap3d_set_entry:
 * @self: an #LrgLightmap3D
 * @guid: the object's node / entity GUID
 * @page: the page its charts are on
 * @scale_offset: (array fixed-size=4): (scale u, scale v, offset u,
 *   offset v) mapping uv2 into the page
 *
 * Adds or replaces the entry of @guid.  Emits #LrgLightmap3D::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap3d_set_entry (LrgLightmap3D *self,
                               const gchar   *guid,
                               guint          page,
                               const gfloat  *scale_offset);

/**
 * lrg_lightmap3d_get_entry:
 * @self: an #LrgLightmap3D
 * @guid: (nullable): an object GUID
 * @out_page: (out) (optional): the entry's page
 * @out_scale_offset: (out caller-allocates) (array fixed-size=4)
 *   (optional): the entry's uv2 scale and offset
 *
 * Returns: %TRUE when @guid has an entry
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_get_entry (LrgLightmap3D *self,
                                   const gchar   *guid,
                                   guint         *out_page,
                                   gfloat        *out_scale_offset);

/**
 * lrg_lightmap3d_remove_entry:
 * @self: an #LrgLightmap3D
 * @guid: an object GUID
 *
 * Emits #LrgLightmap3D::changed when an entry was removed.
 *
 * Returns: %TRUE when @guid had an entry
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_remove_entry (LrgLightmap3D *self,
                                      const gchar   *guid);

/**
 * lrg_lightmap3d_get_n_entries:
 * @self: an #LrgLightmap3D
 *
 * Returns: the number of entries
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap3d_get_n_entries (LrgLightmap3D *self);

/**
 * lrg_lightmap3d_get_guids:
 * @self: an #LrgLightmap3D
 *
 * Returns: (transfer full) (element-type utf8): the GUIDs with an
 *   entry, sorted
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lightmap3d_get_guids (LrgLightmap3D *self);

/* --- CPU sampling ------------------------------------------------------- */

/**
 * lrg_lightmap3d_sample_page:
 * @self: an #LrgLightmap3D
 * @page: a page index
 * @u: atlas u, 0 .. 1 left to right (clamped)
 * @v: atlas v, 0 .. 1 from the first row (clamped)
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the irradiance
 *   / pi (linear), times the intensity
 *
 * Bilinear lookup with texel centres at (i + 0.5) / size and clamped
 * edges: exactly what the GPU sampler returns.
 *
 * Returns: %FALSE (black) when @page does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_sample_page (LrgLightmap3D *self,
                                     guint          page,
                                     gfloat         u,
                                     gfloat         v,
                                     gfloat        *out_rgb);

/**
 * lrg_lightmap3d_sample:
 * @self: an #LrgLightmap3D
 * @guid: an object GUID
 * @uv2: (array fixed-size=2): the object's second texture coordinate
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the baked
 *   irradiance / pi at @uv2 (linear), times the intensity
 *
 * lrg_lightmap3d_sample_page() through the entry of @guid.
 *
 * Returns: %FALSE (black) when @guid has no entry
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_sample (LrgLightmap3D *self,
                                const gchar   *guid,
                                const gfloat  *uv2,
                                gfloat        *out_rgb);

/* --- settings and intensity -------------------------------------------- */

/**
 * lrg_lightmap3d_set_setting:
 * @self: an #LrgLightmap3D
 * @key: a setting name (texels_per_unit, samples, bounces, ...)
 * @value: its value (booleans as 0 / 1)
 *
 * Records a bake setting that produced the lightmap (written to the
 * `settings:` mapping of the index; informational for the runtime).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap3d_set_setting (LrgLightmap3D *self,
                                 const gchar   *key,
                                 gdouble        value);

/**
 * lrg_lightmap3d_get_setting:
 * @self: an #LrgLightmap3D
 * @key: a setting name
 * @out_value: (out) (optional): its value
 *
 * Returns: %TRUE when @key was recorded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_get_setting (LrgLightmap3D *self,
                                     const gchar   *key,
                                     gdouble       *out_value);

/**
 * lrg_lightmap3d_get_setting_keys:
 * @self: an #LrgLightmap3D
 *
 * Returns: (transfer full) (element-type utf8): the recorded setting
 *   names, sorted
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lightmap3d_get_setting_keys (LrgLightmap3D *self);

/**
 * lrg_lightmap3d_set_intensity:
 * @self: an #LrgLightmap3D
 * @intensity: multiplier on every texel (>= 0)
 *
 * Sets #LrgLightmap3D:intensity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap3d_set_intensity (LrgLightmap3D *self,
                                   gfloat         intensity);

/**
 * lrg_lightmap3d_get_intensity:
 * @self: an #LrgLightmap3D
 *
 * Returns: #LrgLightmap3D:intensity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lightmap3d_get_intensity (LrgLightmap3D *self);

/* --- GPU ---------------------------------------------------------------- */

/**
 * lrg_lightmap3d_upload:
 * @self: an #LrgLightmap3D
 * @error: return location for a #GError
 *
 * Uploads the pages that changed since the last upload as RGB32F
 * textures (bilinear, clamped, no mipmaps).  Needs a GL context.
 *
 * Returns: %TRUE when every page has a texture
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap3d_upload (LrgLightmap3D  *self,
                                GError        **error);

/**
 * lrg_lightmap3d_unload_gpu:
 * @self: an #LrgLightmap3D
 *
 * Frees the GPU textures (they are re-made by the next upload).  Call it
 * before closing the window when the lightmap outlives it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap3d_unload_gpu (LrgLightmap3D *self);

/**
 * lrg_lightmap3d_get_page_texture:
 * @self: an #LrgLightmap3D
 * @page: a page index
 *
 * Returns: the GL texture id of @page, 0 when not uploaded (or stale)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap3d_get_page_texture (LrgLightmap3D *self,
                                       guint          page);

G_END_DECLS
