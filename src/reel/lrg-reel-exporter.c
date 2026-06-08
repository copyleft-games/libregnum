/* lrg-reel-exporter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-exporter.h"

G_DEFINE_ABSTRACT_TYPE (LrgReelExporter, lrg_reel_exporter, G_TYPE_OBJECT)

GQuark
lrg_reel_exporter_error_quark (void)
{
    return g_quark_from_static_string ("lrg-reel-exporter-error-quark");
}

static gboolean
lrg_reel_exporter_real_begin (LrgReelExporter *self,
                              gint             width,
                              gint             height,
                              gdouble          fps,
                              GError         **error)
{
    g_set_error_literal (error, LRG_REEL_EXPORTER_ERROR,
                         LRG_REEL_EXPORTER_ERROR_UNSUPPORTED,
                         "exporter does not implement begin()");
    return FALSE;
}

static gboolean
lrg_reel_exporter_real_add_frame (LrgReelExporter *self,
                                  GrlImage        *frame,
                                  GError         **error)
{
    g_set_error_literal (error, LRG_REEL_EXPORTER_ERROR,
                         LRG_REEL_EXPORTER_ERROR_UNSUPPORTED,
                         "exporter does not implement add_frame()");
    return FALSE;
}

static gboolean
lrg_reel_exporter_real_finish (LrgReelExporter *self,
                               GError         **error)
{
    g_set_error_literal (error, LRG_REEL_EXPORTER_ERROR,
                         LRG_REEL_EXPORTER_ERROR_UNSUPPORTED,
                         "exporter does not implement finish()");
    return FALSE;
}

static void
lrg_reel_exporter_class_init (LrgReelExporterClass *klass)
{
    klass->begin = lrg_reel_exporter_real_begin;
    klass->add_frame = lrg_reel_exporter_real_add_frame;
    klass->finish = lrg_reel_exporter_real_finish;
}

static void
lrg_reel_exporter_init (LrgReelExporter *self)
{
}

gboolean
lrg_reel_exporter_begin (LrgReelExporter *self,
                         gint             width,
                         gint             height,
                         gdouble          fps,
                         GError         **error)
{
    g_return_val_if_fail (LRG_IS_REEL_EXPORTER (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return LRG_REEL_EXPORTER_GET_CLASS (self)->begin (self, width, height, fps, error);
}

gboolean
lrg_reel_exporter_add_frame (LrgReelExporter *self,
                             GrlImage        *frame,
                             GError         **error)
{
    g_return_val_if_fail (LRG_IS_REEL_EXPORTER (self), FALSE);
    g_return_val_if_fail (GRL_IS_IMAGE (frame), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return LRG_REEL_EXPORTER_GET_CLASS (self)->add_frame (self, frame, error);
}

gboolean
lrg_reel_exporter_finish (LrgReelExporter *self,
                          GError         **error)
{
    g_return_val_if_fail (LRG_IS_REEL_EXPORTER (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return LRG_REEL_EXPORTER_GET_CLASS (self)->finish (self, error);
}
