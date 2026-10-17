/* lrg-font-atlas.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFontAtlas keeps the codepoints it was asked for in insertion
 * order and bakes them all with lrg_text_baker_bake_atlas() whenever
 * the set grew (lazily, on the next read).  Only codepoints the font
 * has and that measure wider than zero are admitted, so the baker's
 * region array lines up one to one with the codepoint list.  The
 * texture is a cache of the image, uploaded on demand.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEXT

#include "lrg-font-atlas.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <string.h>

/* free faces looked for when fontconfig is not available, best first */
static const gchar * const known_fonts[] = {
    "DejaVuSans.ttf",
    "NotoSans-Regular.ttf",
    "NotoSans[wght].ttf",
    "LiberationSans-Regular.ttf",
    "FreeSans.ttf",
    "FreeSans.otf",
    "Cantarell-Regular.otf",
    "DejaVuSansMono.ttf",
    "LiberationMono-Regular.ttf",
    NULL
};

/* how deep the font directory scan descends */
#define LRG_FONT_ATLAS_SCAN_DEPTH (4)

struct _LrgFontAtlas
{
    GObject        parent_instance;

    LrgTextBaker  *baker;
    gfloat         px_size;
    guint8         threshold;
    gfloat         ascent;
    gfloat         line_height;

    GArray        *codepoints;  /* gunichar, insertion order */
    GHashTable    *index;       /* codepoint -> index + 1 */
    GrlRectangle  *rects;       /* of the last build, codepoints->len long */
    guint          built;       /* glyphs in the last build */
    GrlImage      *image;       /* of the last build */
    GrlTexture    *texture;     /* upload of image, nullable */
    gboolean       texture_stale;
};

G_DEFINE_FINAL_TYPE (LrgFontAtlas, lrg_font_atlas, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-font-atlas-error-quark, lrg_font_atlas_error)

static void
lrg_font_atlas_finalize (GObject *object)
{
    LrgFontAtlas *self = LRG_FONT_ATLAS (object);

    g_clear_object (&self->texture);
    g_clear_object (&self->image);
    g_clear_pointer (&self->rects, g_free);
    g_clear_pointer (&self->codepoints, g_array_unref);
    g_clear_pointer (&self->index, g_hash_table_unref);
    g_clear_object (&self->baker);

    G_OBJECT_CLASS (lrg_font_atlas_parent_class)->finalize (object);
}

static void
lrg_font_atlas_class_init (LrgFontAtlasClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_font_atlas_finalize;
}

static void
lrg_font_atlas_init (LrgFontAtlas *self)
{
    self->codepoints = g_array_new (FALSE, FALSE, sizeof (gunichar));
    self->index = g_hash_table_new (g_direct_hash, g_direct_equal);
}

LrgFontAtlas *
lrg_font_atlas_new_for_baker (LrgTextBaker *baker,
                               gfloat        px_size)
{
    LrgFontAtlas *self;
    gfloat ascent;
    gfloat descent;
    gfloat gap;

    g_return_val_if_fail (LRG_IS_TEXT_BAKER (baker), NULL);
    g_return_val_if_fail (px_size > 0.0f, NULL);

    self = g_object_new (LRG_TYPE_FONT_ATLAS, NULL);
    self->baker = g_object_ref (baker);
    self->px_size = px_size;

    /* the baker draws each glyph with its em box top at the cell top */
    grl_image_font_get_v_metrics (lrg_text_baker_get_font (baker), px_size,
                                  &ascent, &descent, &gap);
    self->ascent = ascent;
    self->line_height = ascent - descent + gap;
    return self;
}

LrgFontAtlas *
lrg_font_atlas_new_from_file (const gchar  *font_path,
                               gfloat        px_size,
                               GError      **error)
{
    g_autoptr(LrgTextBaker) baker = NULL;

    g_return_val_if_fail (font_path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (px_size <= 0.0f)
    {
        g_set_error (error, LRG_FONT_ATLAS_ERROR, LRG_FONT_ATLAS_ERROR_INVALID_SIZE,
                     "Glyph atlas pixel size must be positive (got %g)", (gdouble) px_size);
        return NULL;
    }
    baker = lrg_text_baker_new_from_file (font_path, error);
    if (baker == NULL)
        return NULL;
    return lrg_font_atlas_new_for_baker (baker, px_size);
}

/* --- system font lookup --- */

/* every character of @needed (spaces and controls aside) is in @font */
static gboolean
font_covers (GrlImageFont *font,
             const gchar  *needed)
{
    const gchar *p;

    if (needed == NULL)
        return TRUE;
    for (p = needed; *p != '\0'; p = g_utf8_next_char (p))
    {
        gunichar c;

        c = g_utf8_get_char_validated (p, -1);
        if (c == (gunichar) -1 || c == (gunichar) -2)
            return FALSE;
        if (g_unichar_isspace (c) || g_unichar_iscntrl (c))
            continue;
        if (!grl_image_font_has_glyph (font, c))
            return FALSE;
    }
    return TRUE;
}

/* @path loads as a font and covers @needed */
static gboolean
font_file_usable (const gchar *path,
                  const gchar *needed)
{
    g_autoptr(GrlImageFont) font = NULL;
    const gchar *ext;

    ext = strrchr (path, '.');
    if (ext == NULL || (g_ascii_strcasecmp (ext, ".ttf") != 0 &&
                        g_ascii_strcasecmp (ext, ".otf") != 0))
        return FALSE;
    font = grl_image_font_new_from_file (path, NULL);
    if (font == NULL)
        return FALSE;
    return font_covers (font, needed);
}

/* fc-match's best face for @family covering @needed, or NULL */
static gchar *
find_with_fontconfig (const gchar *family,
                      const gchar *needed)
{
    g_autoptr(GSubprocess) proc = NULL;
    g_autoptr(GString) pattern = NULL;
    g_autofree gchar *out = NULL;
    const gchar *p;

    pattern = g_string_new (family != NULL ? family : "sans");
    if (needed != NULL && needed[0] != '\0')
    {
        gboolean first = TRUE;

        g_string_append (pattern, ":charset=");
        for (p = needed; *p != '\0'; p = g_utf8_next_char (p))
        {
            gunichar c;

            c = g_utf8_get_char_validated (p, -1);
            if (c == (gunichar) -1 || c == (gunichar) -2)
                return NULL;
            if (g_unichar_isspace (c) || g_unichar_iscntrl (c))
                continue;
            g_string_append_printf (pattern, "%s%x", first ? "" : " ", (guint) c);
            first = FALSE;
        }
    }

    proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                             NULL, "fc-match", "--format=%{file}", pattern->str, NULL);
    if (proc == NULL)
        return NULL;
    if (!g_subprocess_communicate_utf8 (proc, NULL, NULL, &out, NULL, NULL))
        return NULL;
    if (!g_subprocess_get_successful (proc) || out == NULL || out[0] == '\0')
        return NULL;
    g_strchomp (out);
    if (!font_file_usable (out, needed))
        return NULL;
    return g_steal_pointer (&out);
}

/* collects known font files below @dir into @found (basename -> path) */
static void
scan_dir (const gchar *dir,
          guint        depth,
          GHashTable  *found)
{
    g_autoptr(GDir) d = NULL;
    const gchar *name;

    if (depth > LRG_FONT_ATLAS_SCAN_DEPTH)
        return;
    d = g_dir_open (dir, 0, NULL);
    if (d == NULL)
        return;
    while ((name = g_dir_read_name (d)) != NULL)
    {
        g_autofree gchar *path = g_build_filename (dir, name, NULL);

        if (g_file_test (path, G_FILE_TEST_IS_DIR))
            scan_dir (path, depth + 1, found);
        else if (g_strv_contains (known_fonts, name) &&
                 !g_hash_table_contains (found, name))
            g_hash_table_insert (found, g_strdup (name), g_steal_pointer (&path));
    }
}

gchar *
lrg_font_atlas_find_system_font (const gchar *family,
                                  const gchar *needed)
{
    g_autoptr(GHashTable) found = NULL;
    g_autofree gchar *home_fonts = NULL;
    g_autofree gchar *data_fonts = NULL;
    const gchar * const *dirs;
    gchar *path;
    guint i;

    if (needed != NULL && !g_utf8_validate (needed, -1, NULL))
        return NULL;

    path = find_with_fontconfig (family, needed);
    if (path != NULL)
        return path;

    found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    data_fonts = g_build_filename (g_get_user_data_dir (), "fonts", NULL);
    home_fonts = g_build_filename (g_get_home_dir (), ".fonts", NULL);
    scan_dir (data_fonts, 0, found);
    scan_dir (home_fonts, 0, found);
    dirs = g_get_system_data_dirs ();
    for (i = 0; dirs[i] != NULL; i++)
    {
        g_autofree gchar *dir = g_build_filename (dirs[i], "fonts", NULL);

        scan_dir (dir, 0, found);
    }

    for (i = 0; known_fonts[i] != NULL; i++)
    {
        const gchar *candidate;

        candidate = (const gchar *) g_hash_table_lookup (found, known_fonts[i]);
        if (candidate != NULL && font_file_usable (candidate, needed))
            return g_strdup (candidate);
    }
    return NULL;
}

/* --- properties --- */

gfloat
lrg_font_atlas_get_pixel_size (LrgFontAtlas *self)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), 0.0f);
    return self->px_size;
}

void
lrg_font_atlas_set_alpha_threshold (LrgFontAtlas *self,
                                     guint8         threshold)
{
    g_return_if_fail (LRG_IS_FONT_ATLAS (self));

    if (self->threshold == threshold)
        return;
    self->threshold = threshold;
    /* force a rebuild */
    self->built = 0;
}

guint8
lrg_font_atlas_get_alpha_threshold (LrgFontAtlas *self)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), 0);
    return self->threshold;
}

gboolean
lrg_font_atlas_font_has_glyph (LrgFontAtlas *self,
                                gunichar       codepoint)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), FALSE);
    return grl_image_font_has_glyph (lrg_text_baker_get_font (self->baker), codepoint);
}

/* --- building --- */

guint
lrg_font_atlas_add_text (LrgFontAtlas *self,
                          const gchar   *text)
{
    const gchar *p;
    guint added;

    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), 0);

    if (text == NULL)
        return 0;

    added = 0;
    p = text;
    while (*p != '\0')
    {
        gunichar c;
        gchar buf[8];
        gint n;
        gint width;
        gint height;

        c = g_utf8_get_char_validated (p, -1);
        if (c == (gunichar) -1 || c == (gunichar) -2)
        {
            p++;
            continue;
        }
        p = g_utf8_next_char (p);
        if (g_unichar_isspace (c) || g_unichar_iscntrl (c) ||
            g_hash_table_contains (self->index, GUINT_TO_POINTER (c)))
            continue;
        if (!grl_image_font_has_glyph (lrg_text_baker_get_font (self->baker), c))
            continue;

        /* the baker drops zero-width glyphs; so do we, to stay aligned */
        n = g_unichar_to_utf8 (c, buf);
        buf[n] = '\0';
        if (!lrg_text_baker_measure (self->baker, buf, self->px_size, &width, &height) ||
            width <= 0 || height <= 0)
            continue;

        g_array_append_val (self->codepoints, c);
        g_hash_table_insert (self->index, GUINT_TO_POINTER (c),
                             GUINT_TO_POINTER (self->codepoints->len));
        added++;
    }
    return added;
}

/* bakes every codepoint when the set changed since the last build */
static gboolean
ensure_built (LrgFontAtlas *self)
{
    g_autoptr(GString) text = NULL;
    g_autoptr(GrlColor) white = NULL;
    GrlRectangle *rects = NULL;
    GrlImage *image;
    guint n = 0;
    guint i;

    if (self->codepoints->len == 0)
        return FALSE;
    if (self->built == self->codepoints->len && self->image != NULL)
        return TRUE;

    text = g_string_new (NULL);
    for (i = 0; i < self->codepoints->len; i++)
        g_string_append_unichar (text, g_array_index (self->codepoints, gunichar, i));
    white = grl_color_new (255, 255, 255, 255);
    image = lrg_text_baker_bake_atlas (self->baker, text->str, self->px_size, white, 1,
                                       &rects, &n);
    if (image == NULL || n != self->codepoints->len)
    {
        /* should not happen: every admitted glyph measured non-empty */
        lrg_warning (LRG_LOG_DOMAIN_TEXT, "glyph atlas: baked %u of %u glyphs",
                     n, self->codepoints->len);
        g_clear_object (&image);
        g_free (rects);
        return FALSE;
    }

    if (self->threshold > 0)
    {
        guint8 *px;
        gsize size;
        gsize j;

        px = grl_image_get_pixels (image, &size);
        for (j = 0; px != NULL && j + 3 < size; j += 4)
        {
            px[j] = 255;
            px[j + 1] = 255;
            px[j + 2] = 255;
            px[j + 3] = px[j + 3] >= self->threshold ? 255 : 0;
        }
    }

    g_clear_object (&self->image);
    g_free (self->rects);
    self->image = image;
    self->rects = rects;
    self->built = n;
    self->texture_stale = TRUE;
    return TRUE;
}

gboolean
lrg_font_atlas_has_glyph (LrgFontAtlas *self,
                           gunichar       codepoint)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), FALSE);
    return g_hash_table_contains (self->index, GUINT_TO_POINTER (codepoint));
}

gboolean
lrg_font_atlas_lookup (LrgFontAtlas *self,
                        gunichar       codepoint,
                        GrlRectangle  *out_rect)
{
    guint idx;

    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), FALSE);
    g_return_val_if_fail (out_rect != NULL, FALSE);

    idx = GPOINTER_TO_UINT (g_hash_table_lookup (self->index, GUINT_TO_POINTER (codepoint)));
    if (idx == 0 || !ensure_built (self))
        return FALSE;
    *out_rect = self->rects[idx - 1];
    return TRUE;
}

guint
lrg_font_atlas_get_glyph_count (LrgFontAtlas *self)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), 0);
    return self->codepoints->len;
}

gfloat
lrg_font_atlas_get_line_height (LrgFontAtlas *self)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), 0.0f);
    return self->line_height;
}

gfloat
lrg_font_atlas_get_ascent (LrgFontAtlas *self)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), 0.0f);
    return self->ascent;
}

GrlImage *
lrg_font_atlas_get_image (LrgFontAtlas *self)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), NULL);

    if (!ensure_built (self))
        return NULL;
    return self->image;
}

GrlTexture *
lrg_font_atlas_get_texture (LrgFontAtlas *self)
{
    g_return_val_if_fail (LRG_IS_FONT_ATLAS (self), NULL);

    if (!ensure_built (self))
        return NULL;
    if (self->texture == NULL || self->texture_stale)
    {
        g_clear_object (&self->texture);
        self->texture = grl_texture_new_from_image (self->image);
        if (self->texture != NULL)
            grl_texture_set_filter (self->texture, GRL_TEXTURE_FILTER_POINT);
        self->texture_stale = FALSE;
    }
    return self->texture;
}

void
lrg_font_atlas_release_texture (LrgFontAtlas *self)
{
    g_return_if_fail (LRG_IS_FONT_ATLAS (self));
    g_clear_object (&self->texture);
}
