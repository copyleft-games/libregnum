/* lrg-editor-host.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Embedding contract for the editor runtime, plus a concrete software host.
 */

#include "lrg-editor-host.h"

G_DEFINE_INTERFACE (LrgEditorHost, lrg_editor_host, G_TYPE_OBJECT)

static void
lrg_editor_host_default_init (LrgEditorHostInterface *iface)
{
}

/* ==========================================================================
 * Interface method wrappers
 * ========================================================================== */

LrgEngine *
lrg_editor_host_get_engine (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_val_if_fail (LRG_IS_EDITOR_HOST (self), NULL);

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	return (iface->get_engine != NULL) ? iface->get_engine (self) : NULL;
}

void
lrg_editor_host_get_render_size (LrgEditorHost *self,
                                 gint          *width,
                                 gint          *height)
{
	LrgEditorHostInterface *iface;

	if (width != NULL)
		*width = 0;
	if (height != NULL)
		*height = 0;

	g_return_if_fail (LRG_IS_EDITOR_HOST (self));

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	if (iface->get_render_size != NULL)
		iface->get_render_size (self, width, height);
}

void
lrg_editor_host_begin_frame (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_if_fail (LRG_IS_EDITOR_HOST (self));

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	if (iface->begin_frame != NULL)
		iface->begin_frame (self);
}

void
lrg_editor_host_clear (LrgEditorHost *self,
                       GrlColor      *color)
{
	LrgEditorHostInterface *iface;

	g_return_if_fail (LRG_IS_EDITOR_HOST (self));

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	if (iface->clear != NULL)
		iface->clear (self, color);
}

void
lrg_editor_host_end_frame (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_if_fail (LRG_IS_EDITOR_HOST (self));

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	if (iface->end_frame != NULL)
		iface->end_frame (self);
}

gdouble
lrg_editor_host_get_frame_delta (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_val_if_fail (LRG_IS_EDITOR_HOST (self), 0.0);

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	return (iface->get_frame_delta != NULL) ? iface->get_frame_delta (self) : 0.0;
}

LrgInputSoftware *
lrg_editor_host_get_input_source (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_val_if_fail (LRG_IS_EDITOR_HOST (self), NULL);

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	return (iface->get_input_source != NULL) ? iface->get_input_source (self) : NULL;
}

LrgCamera *
lrg_editor_host_get_viewport_camera (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_val_if_fail (LRG_IS_EDITOR_HOST (self), NULL);

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	return (iface->get_viewport_camera != NULL) ? iface->get_viewport_camera (self) : NULL;
}

gboolean
lrg_editor_host_is_2d_mode (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_val_if_fail (LRG_IS_EDITOR_HOST (self), FALSE);

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	return (iface->is_2d_mode != NULL) ? iface->is_2d_mode (self) : FALSE;
}

void
lrg_editor_host_request_redraw (LrgEditorHost *self)
{
	LrgEditorHostInterface *iface;

	g_return_if_fail (LRG_IS_EDITOR_HOST (self));

	iface = LRG_EDITOR_HOST_GET_IFACE (self);
	if (iface->request_redraw != NULL)
		iface->request_redraw (self);
}

/* ==========================================================================
 * LrgEditorSoftwareHost
 * ========================================================================== */

struct _LrgEditorSoftwareHost
{
	GObject parent_instance;

	LrgEngine        *engine;     /* borrowed (ref held) */
	LrgCamera        *camera;     /* borrowed (ref held) */
	LrgInputSoftware *input;      /* borrowed (ref held) */
	gint              width;
	gint              height;
	gdouble           delta;
	gboolean          is_2d;
	guint             redraw_count;
};

static LrgEngine *
sh_get_engine (LrgEditorHost *host)
{
	return LRG_EDITOR_SOFTWARE_HOST (host)->engine;
}

static void
sh_get_render_size (LrgEditorHost *host,
                    gint          *width,
                    gint          *height)
{
	LrgEditorSoftwareHost *self = LRG_EDITOR_SOFTWARE_HOST (host);

	if (width != NULL)
		*width = self->width;
	if (height != NULL)
		*height = self->height;
}

static void
sh_begin_frame (LrgEditorHost *host)
{
}

static void
sh_clear (LrgEditorHost *host,
          GrlColor      *color)
{
}

static void
sh_end_frame (LrgEditorHost *host)
{
}

static gdouble
sh_get_frame_delta (LrgEditorHost *host)
{
	return LRG_EDITOR_SOFTWARE_HOST (host)->delta;
}

static LrgInputSoftware *
sh_get_input_source (LrgEditorHost *host)
{
	return LRG_EDITOR_SOFTWARE_HOST (host)->input;
}

static LrgCamera *
sh_get_viewport_camera (LrgEditorHost *host)
{
	return LRG_EDITOR_SOFTWARE_HOST (host)->camera;
}

static gboolean
sh_is_2d_mode (LrgEditorHost *host)
{
	return LRG_EDITOR_SOFTWARE_HOST (host)->is_2d;
}

static void
sh_request_redraw (LrgEditorHost *host)
{
	LRG_EDITOR_SOFTWARE_HOST (host)->redraw_count++;
}

static void
lrg_editor_software_host_iface_init (LrgEditorHostInterface *iface)
{
	iface->get_engine          = sh_get_engine;
	iface->get_render_size     = sh_get_render_size;
	iface->begin_frame         = sh_begin_frame;
	iface->clear               = sh_clear;
	iface->end_frame           = sh_end_frame;
	iface->get_frame_delta     = sh_get_frame_delta;
	iface->get_input_source    = sh_get_input_source;
	iface->get_viewport_camera = sh_get_viewport_camera;
	iface->is_2d_mode          = sh_is_2d_mode;
	iface->request_redraw      = sh_request_redraw;
}

G_DEFINE_TYPE_WITH_CODE (LrgEditorSoftwareHost, lrg_editor_software_host, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_EDITOR_HOST,
                                                lrg_editor_software_host_iface_init))

static void
lrg_editor_software_host_finalize (GObject *object)
{
	LrgEditorSoftwareHost *self = LRG_EDITOR_SOFTWARE_HOST (object);

	g_clear_object (&self->engine);
	g_clear_object (&self->camera);
	g_clear_object (&self->input);

	G_OBJECT_CLASS (lrg_editor_software_host_parent_class)->finalize (object);
}

static void
lrg_editor_software_host_class_init (LrgEditorSoftwareHostClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_software_host_finalize;
}

static void
lrg_editor_software_host_init (LrgEditorSoftwareHost *self)
{
	self->width  = 800;
	self->height = 600;
	self->delta  = 0.0;
	self->is_2d  = FALSE;
}

LrgEditorSoftwareHost *
lrg_editor_software_host_new (LrgEngine *engine)
{
	LrgEditorSoftwareHost *self = g_object_new (LRG_TYPE_EDITOR_SOFTWARE_HOST, NULL);

	if (engine != NULL)
		self->engine = g_object_ref (engine);

	return self;
}

void
lrg_editor_software_host_set_render_size (LrgEditorSoftwareHost *self,
                                          gint                   width,
                                          gint                   height)
{
	g_return_if_fail (LRG_IS_EDITOR_SOFTWARE_HOST (self));

	self->width = width;
	self->height = height;
}

void
lrg_editor_software_host_set_frame_delta (LrgEditorSoftwareHost *self,
                                          gdouble                delta)
{
	g_return_if_fail (LRG_IS_EDITOR_SOFTWARE_HOST (self));

	self->delta = delta;
}

void
lrg_editor_software_host_set_viewport_camera (LrgEditorSoftwareHost *self,
                                              LrgCamera             *camera)
{
	g_return_if_fail (LRG_IS_EDITOR_SOFTWARE_HOST (self));

	g_set_object (&self->camera, camera);
}

void
lrg_editor_software_host_set_input_source (LrgEditorSoftwareHost *self,
                                           LrgInputSoftware      *input)
{
	g_return_if_fail (LRG_IS_EDITOR_SOFTWARE_HOST (self));

	g_set_object (&self->input, input);
}

void
lrg_editor_software_host_set_2d_mode (LrgEditorSoftwareHost *self,
                                      gboolean               is_2d)
{
	g_return_if_fail (LRG_IS_EDITOR_SOFTWARE_HOST (self));

	self->is_2d = is_2d;
}

guint
lrg_editor_software_host_get_redraw_count (LrgEditorSoftwareHost *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOFTWARE_HOST (self), 0);

	return self->redraw_count;
}
