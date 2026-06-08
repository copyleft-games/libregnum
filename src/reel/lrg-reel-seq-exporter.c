/* lrg-reel-seq-exporter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-seq-exporter.h"

struct _LrgReelSeqExporter
{
    LrgReelExporter     parent_instance;

    gchar              *dir;
    gchar              *pattern;
    LrgReelImageFormat  format;
    gint                frame_index;
};

G_DEFINE_FINAL_TYPE (LrgReelSeqExporter, lrg_reel_seq_exporter,
                     LRG_TYPE_REEL_EXPORTER)

/* --------------------------------------------------------------------------
 * vfunc implementations
 * -------------------------------------------------------------------------- */

static gboolean
lrg_reel_seq_exporter_begin (LrgReelExporter *base,
                              gint             width,
                              gint             height,
                              gdouble          fps,
                              GError         **error)
{
    LrgReelSeqExporter *self = LRG_REEL_SEQ_EXPORTER (base);

    (void) width;
    (void) height;
    (void) fps;

    if (g_mkdir_with_parents (self->dir, 0755) == -1)
    {
        g_set_error (error,
                     LRG_REEL_EXPORTER_ERROR,
                     LRG_REEL_EXPORTER_ERROR_OPEN,
                     "failed to create output directory: %s", self->dir);
        return FALSE;
    }

    self->frame_index = 0;
    return TRUE;
}

static gboolean
lrg_reel_seq_exporter_add_frame (LrgReelExporter *base,
                                  GrlImage        *frame,
                                  GError         **error)
{
    LrgReelSeqExporter *self = LRG_REEL_SEQ_EXPORTER (base);
    const gchar *ext;
    g_autofree gchar *base_name = NULL;
    g_autofree gchar *filename = NULL;
    g_autofree gchar *path = NULL;
    gboolean ok;

    switch (self->format)
    {
    case LRG_REEL_IMAGE_FORMAT_JPEG:
        ext = "jpg";
        break;
    case LRG_REEL_IMAGE_FORMAT_PNG:
    default:
        ext = "png";
        break;
    }

    /* The caller supplies the numeric pattern (e.g. "frame_%05d"); using it as
     * a printf format is intentional, so silence -Wformat-nonliteral here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    base_name = g_strdup_printf (self->pattern, self->frame_index);
#pragma GCC diagnostic pop
    filename  = g_strdup_printf ("%s.%s", base_name, ext);
    path      = g_build_filename (self->dir, filename, NULL);

    ok = grl_image_export (frame, path);
    if (!ok)
    {
        g_set_error (error,
                     LRG_REEL_EXPORTER_ERROR,
                     LRG_REEL_EXPORTER_ERROR_WRITE,
                     "failed to write frame to: %s", path);
        return FALSE;
    }

    self->frame_index++;
    return TRUE;
}

static gboolean
lrg_reel_seq_exporter_finish (LrgReelExporter *base,
                               GError         **error)
{
    (void) base;
    (void) error;
    return TRUE;
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_seq_exporter_finalize (GObject *object)
{
    LrgReelSeqExporter *self = LRG_REEL_SEQ_EXPORTER (object);

    g_clear_pointer (&self->dir, g_free);
    g_clear_pointer (&self->pattern, g_free);

    G_OBJECT_CLASS (lrg_reel_seq_exporter_parent_class)->finalize (object);
}

static void
lrg_reel_seq_exporter_class_init (LrgReelSeqExporterClass *klass)
{
    GObjectClass         *object_class   = G_OBJECT_CLASS (klass);
    LrgReelExporterClass *exporter_class = LRG_REEL_EXPORTER_CLASS (klass);

    object_class->finalize = lrg_reel_seq_exporter_finalize;

    exporter_class->begin     = lrg_reel_seq_exporter_begin;
    exporter_class->add_frame = lrg_reel_seq_exporter_add_frame;
    exporter_class->finish    = lrg_reel_seq_exporter_finish;
}

static void
lrg_reel_seq_exporter_init (LrgReelSeqExporter *self)
{
    self->dir         = NULL;
    self->pattern     = NULL;
    self->format      = LRG_REEL_IMAGE_FORMAT_PNG;
    self->frame_index = 0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_seq_exporter_new:
 * @dir: (type filename): the output directory; created with g_mkdir_with_parents()
 *   if it does not yet exist.
 * @pattern: a printf integer format string (without extension), e.g.
 *   %<literal>"frame_%05d"</literal>.  The frame index is substituted at
 *   each call to lrg_reel_exporter_add_frame().
 * @format: the image format to write (%LRG_REEL_IMAGE_FORMAT_PNG or
 *   %LRG_REEL_IMAGE_FORMAT_JPEG).
 *
 * Creates a new #LrgReelSeqExporter.  Each frame is written as a separate
 * image file inside @dir.  The output directory is not created until
 * lrg_reel_exporter_begin() is called.
 *
 * Returns: (transfer full): a new #LrgReelSeqExporter.
 *
 * Since: 1.0
 */
LrgReelSeqExporter *
lrg_reel_seq_exporter_new (const gchar        *dir,
                            const gchar        *pattern,
                            LrgReelImageFormat  format)
{
    LrgReelSeqExporter *self;

    g_return_val_if_fail (dir != NULL, NULL);
    g_return_val_if_fail (pattern != NULL, NULL);

    self = g_object_new (LRG_TYPE_REEL_SEQ_EXPORTER, NULL);
    self->dir     = g_strdup (dir);
    self->pattern = g_strdup (pattern);
    self->format  = format;

    return self;
}

/**
 * lrg_reel_seq_exporter_get_frame_count:
 * @self: an #LrgReelSeqExporter.
 *
 * Returns the number of frames that have been written so far.  This counter
 * is reset to zero each time lrg_reel_exporter_begin() is called.
 *
 * Returns: the number of frames written.
 *
 * Since: 1.0
 */
guint
lrg_reel_seq_exporter_get_frame_count (LrgReelSeqExporter *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SEQ_EXPORTER (self), 0);

    return (guint) self->frame_index;
}
