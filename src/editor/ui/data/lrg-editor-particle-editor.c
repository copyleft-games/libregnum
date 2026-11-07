/* lrg-editor-particle-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorParticleEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-particle-editor.h"

struct _LrgEditorParticleEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorParticleEditor, lrg_editor_particle_editor, G_TYPE_OBJECT)

static void
lrg_editor_particle_editor_class_init (LrgEditorParticleEditorClass *klass)
{
}

static void
lrg_editor_particle_editor_init (LrgEditorParticleEditor *self)
{
}

LrgEditorParticleEditor *
lrg_editor_particle_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_PARTICLE_EDITOR, NULL);
}
