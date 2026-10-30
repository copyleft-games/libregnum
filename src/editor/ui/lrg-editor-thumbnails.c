/* lrg-editor-thumbnails.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Asset thumbnails in the editor. The headless LrgThumbnailCache does the
 * caching and the CPU work (on its worker thread); this file owns the
 * editor side:
 *
 *  - one cache per project (<project>/.cache/thumbnails, or
 *    $XDG_CACHE_HOME/libregnum/thumbnails without a project);
 *  - the per-asset state the assets panel reads (pending / file ready /
 *    texture uploaded / failed);
 *  - a bounded number of texture uploads per frame, and one GPU (model)
 *    render per frame through editor_render_model_thumbnail();
 *  - invalidation from the assets directory monitor (live reload).
 */

#include "lrg-editor-app-private.h"

#define UPLOADS_PER_FRAME (4)

typedef enum
{
	THUMB_PENDING,
	THUMB_READY,      /* PNG on disk, not uploaded */
	THUMB_UPLOADED,
	THUMB_FAILED
} ThumbState;

typedef struct
{
	ThumbState  state;
	gchar      *file;
	GObject    *tex;   /* GrlTexture */
} EditorThumb;

static void
thumb_free (gpointer data)
{
	EditorThumb *t = data;

	g_free (t->file);
	g_clear_object (&t->tex);
	g_free (t);
}

void
editor_thumbnails_init (LrgEditorApp *self)
{
	self->thumb_entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, thumb_free);
}

void
editor_thumbnails_dispose (LrgEditorApp *self)
{
	if (self->thumbs != NULL)
		g_signal_handlers_disconnect_by_data (self->thumbs, self);
	g_clear_object (&self->thumbs);
	g_clear_pointer (&self->thumbs_dir, g_free);
	g_clear_pointer (&self->thumb_entries, g_hash_table_unref);
}

static void
on_ready (LrgThumbnailCache *cache,
          const gchar       *path,
          const gchar       *thumb,
          gpointer           user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);
	EditorThumb  *t = g_hash_table_lookup (self->thumb_entries, path);

	if (t == NULL)
		return;
	g_free (t->file);
	t->file = g_strdup (thumb);
	g_clear_object (&t->tex);
	t->state = (thumb != NULL) ? THUMB_READY : THUMB_FAILED;
}

static GrlImage *
render_model (const gchar *path,
              guint        size,
              gpointer     user_data)
{
	return editor_render_model_thumbnail (LRG_EDITOR_APP (user_data), path, size);
}

/* The cache for the current project, (re)created when the project
 * changes. */
LrgThumbnailCache *
editor_thumbnails_cache (LrgEditorApp *self)
{
	g_autofree gchar *dir = (self->project_dir != NULL)
	                        ? g_build_filename (self->project_dir, ".cache", "thumbnails", NULL) : NULL;

	if (self->thumbs != NULL && g_strcmp0 (dir, self->thumbs_dir) == 0)
		return self->thumbs;

	if (self->thumbs != NULL)
		g_signal_handlers_disconnect_by_data (self->thumbs, self);
	g_clear_object (&self->thumbs);
	g_hash_table_remove_all (self->thumb_entries);
	g_free (self->thumbs_dir);
	self->thumbs_dir = g_strdup (dir);
	self->thumbs = lrg_thumbnail_cache_new (dir);
	g_signal_connect (self->thumbs, "ready", G_CALLBACK (on_ready), self);
	if (self->window_open && self->render != NULL)
		lrg_thumbnail_cache_set_renderer (self->thumbs, render_model, self, NULL);
	return self->thumbs;
}

void
editor_thumbnails_window_opened (LrgEditorApp *self)
{
	lrg_thumbnail_cache_set_renderer (editor_thumbnails_cache (self), render_model, self, NULL);
}

void
editor_thumbnails_window_closing (LrgEditorApp *self)
{
	GHashTableIter iter;
	gpointer       value;

	if (self->thumbs != NULL)
		lrg_thumbnail_cache_set_renderer (self->thumbs, NULL, NULL, NULL);

	/* Textures die with the GL context; keep the files. */
	g_hash_table_iter_init (&iter, self->thumb_entries);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		EditorThumb *t = value;

		if (t->tex != NULL)
		{
			g_clear_object (&t->tex);
			t->state = THUMB_READY;
		}
	}
}

/* Per frame: one GPU render at most (only with a window), then results. */
void
editor_thumbnails_pump (LrgEditorApp *self)
{
	if (self->thumbs != NULL)
		lrg_thumbnail_cache_process (self->thumbs, (self->window_open && !self->headless) ? 1 : 0);
}

/*
 * The thumbnail texture for @path, or NULL. Requests generation on first
 * use; *pending tells the caller whether one is still coming (draw the
 * type glyph meanwhile).
 */
GObject *
editor_thumbnail_texture (LrgEditorApp *self,
                          const gchar  *path,
                          gboolean     *pending)
{
	LrgThumbnailCache *cache = editor_thumbnails_cache (self);
	EditorThumb       *t = g_hash_table_lookup (self->thumb_entries, path);

	*pending = FALSE;
	if (t == NULL)
	{
		if (!lrg_thumbnail_cache_is_supported (cache, path))
			return NULL;
		t = g_new0 (EditorThumb, 1);
		t->state = lrg_thumbnail_cache_request (cache, path) ? THUMB_PENDING : THUMB_FAILED;
		g_hash_table_insert (self->thumb_entries, g_strdup (path), t);
	}

	if (t->state == THUMB_READY && !self->headless && self->window_open)
	{
		if (self->thumb_upload_frame != self->frames)
		{
			self->thumb_upload_frame = self->frames;
			self->thumb_uploads = 0;
		}
		if (self->thumb_uploads < UPLOADS_PER_FRAME)
		{
			GrlTexture *tex = grl_texture_new_from_file (t->file);

			self->thumb_uploads++;
			if (tex != NULL && grl_texture_is_valid (tex))
			{
				t->tex = G_OBJECT (tex);
				t->state = THUMB_UPLOADED;
			}
			else
			{
				g_clear_object (&tex);
				t->state = THUMB_FAILED;
			}
		}
	}
	*pending = (t->state == THUMB_PENDING || t->state == THUMB_READY);
	return t->tex;
}

/* The assets monitor saw @path change: forget every cached copy. */
void
editor_thumbnails_file_changed (LrgEditorApp *self,
                                const gchar  *path)
{
	g_autofree gchar *abs = g_canonicalize_filename (path, NULL);

	if (self->thumbs != NULL)
		lrg_thumbnail_cache_invalidate (self->thumbs, abs);
	g_hash_table_remove (self->thumb_entries, abs);
	if (g_strcmp0 (abs, path) != 0)
		g_hash_table_remove (self->thumb_entries, path);
	editor_render_forget_asset (self, abs);
}

/* Drops all thumbnails of the current directory listing and regenerates
 * them (Tools > Rebuild Thumbnails). */
void
editor_thumbnails_clear (LrgEditorApp *self)
{
	GHashTableIter iter;
	gpointer       key;

	if (self->thumbs != NULL)
	{
		g_hash_table_iter_init (&iter, self->thumb_entries);
		while (g_hash_table_iter_next (&iter, &key, NULL))
			lrg_thumbnail_cache_invalidate (self->thumbs, key);
	}
	g_hash_table_remove_all (self->thumb_entries);
}
