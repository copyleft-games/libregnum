/* lrg-editor-ai-debug-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAiDebugPanel: the docked "AI" panel of lrgeditor.
 *
 * While the level plays in the editor the panel lists every entity with an
 * #LrgAiComponent (its tree, machine, current node and state), and for the
 * selected one shows the live blackboard (editable: a value typed into a
 * row is written into the running blackboard), the tree's active path, the
 * machine's active states and a field to send events to the machine. Below
 * it the decisions inspector shows every #LrgDecisionBrain the embedding
 * program registered with lrg_editor_ai_debug_panel_watch_brain(): the
 * brain's totals (questions asked, answered, from the cache, failed,
 * tokens, mean latency), one row per topic (busy, pending seconds, last
 * latency, counts, last error), the selected topic's last request JSON and
 * latest answer, and a log of the brain's `asked`, `answered` and `failed`
 * signals.
 *
 * The panel also owns the "focus": the live entity the behaviour tree and
 * state-machine editors follow while playing (the one picked here, else
 * the primary selection's live entity, else the first entity running the
 * edited file). The ai.* IPC methods are registered from this file.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ai/lrg-decision-brain.h"
#include "../../world3d/components/lrg-ai-component.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_AI_DEBUG_PANEL_ID:
 *
 * The dock panel id of the AI panel ("ai").
 */
#define LRG_EDITOR_AI_DEBUG_PANEL_ID "ai"

#define LRG_TYPE_EDITOR_AI_DEBUG_PANEL (lrg_editor_ai_debug_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorAiDebugPanel, lrg_editor_ai_debug_panel, LRG, EDITOR_AI_DEBUG_PANEL, LrgContainer)

/**
 * lrg_editor_ai_debug_panel_new:
 * @app: the editor application
 *
 * Creates the panel and registers the ai.* IPC methods and the ai.*
 * actions of @app. The window docks it with the id
 * %LRG_EDITOR_AI_DEBUG_PANEL_ID; embedders may dock it themselves with
 * lrg_editor_app_add_panel().
 *
 * Returns: (transfer full): a new #LrgEditorAiDebugPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorAiDebugPanel * lrg_editor_ai_debug_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_ai_debug_panel_find:
 * @app: the editor application
 *
 * Returns: (transfer none) (nullable): the AI panel of @app, if one was
 *   created
 */
LRG_AVAILABLE_IN_ALL
LrgEditorAiDebugPanel * lrg_editor_ai_debug_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_ai_debug_panel_install:
 * @app: the editor application
 *
 * Registers the ai.* IPC methods on @app's server (once). The panel's
 * constructor and the behaviour tree and state-machine editors call it,
 * so the methods exist even when the panel is not docked.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_ai_debug_panel_install (LrgEditorApp *app);

/* --- live entities -------------------------------------------------------- */

/**
 * lrg_editor_ai_debug_panel_list_live:
 * @app: the editor application
 *
 * Every #LrgAiComponent of the play world (3D entities in hierarchy
 * order, or the components of the 2D game objects in world order). Empty
 * while the level is not playing.
 *
 * Returns: (transfer container) (element-type LrgAiComponent): the
 *   components
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_ai_debug_panel_list_live (LrgEditorApp *app);

/**
 * lrg_editor_ai_debug_panel_dup_entity_id:
 * @app: the editor application
 * @component: a live #LrgAiComponent
 *
 * The id the ai.* methods use for @component's owner: the GUID of the
 * level node the entity was made from, else the entity's name, else
 * "#N" (its index in lrg_editor_ai_debug_panel_list_live()).
 *
 * Returns: (transfer full) (nullable): the id, %NULL for a component
 *   without an owner
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_ai_debug_panel_dup_entity_id (LrgEditorApp   *app,
                                                 LrgAiComponent *component);

/**
 * lrg_editor_ai_debug_panel_lookup:
 * @app: the editor application
 * @entity: (nullable): an entity id (see
 *   lrg_editor_ai_debug_panel_dup_entity_id()), a node name, or %NULL for
 *   the focused entity
 *
 * Returns: (transfer none) (nullable): the live component
 */
LRG_AVAILABLE_IN_ALL
LrgAiComponent * lrg_editor_ai_debug_panel_lookup (LrgEditorApp *app,
                                                   const gchar  *entity);

/**
 * lrg_editor_ai_debug_panel_set_focus:
 * @app: the editor application
 * @entity: (nullable): an entity id, or %NULL to follow the selection
 *
 * Picks the live entity the AI tools follow (the panel's list selection;
 * ai.select over IPC). The pick is kept by id across play sessions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_ai_debug_panel_set_focus (LrgEditorApp *app,
                                          const gchar  *entity);

/**
 * lrg_editor_ai_debug_panel_get_focus:
 * @app: the editor application
 *
 * Returns: (transfer none) (nullable): the picked entity id, %NULL when
 *   the tools follow the selection
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_ai_debug_panel_get_focus (LrgEditorApp *app);

/**
 * lrg_editor_ai_debug_panel_get_focus_component:
 * @app: the editor application
 *
 * The component the tools follow while playing: the picked entity's, else
 * the one on the live entity of the primary selection, else %NULL.
 *
 * Returns: (transfer none) (nullable): the component
 */
LRG_AVAILABLE_IN_ALL
LrgAiComponent * lrg_editor_ai_debug_panel_get_focus_component (LrgEditorApp *app);

/**
 * lrg_editor_ai_debug_panel_find_for_file:
 * @app: the editor application
 * @path: a `.rbt` or `.rfsm` file (absolute or relative to the project)
 * @machine: %TRUE to match #LrgAiComponent:machine-path, %FALSE for
 *   #LrgAiComponent:tree-path
 *
 * The live component a tree or machine editor shows: the focused one when
 * it runs @path, else the first live component running @path. Component
 * paths are compared after resolving them against the project directory
 * (and, for relative ones, by suffix).
 *
 * Returns: (transfer none) (nullable): the component
 */
LRG_AVAILABLE_IN_ALL
LrgAiComponent * lrg_editor_ai_debug_panel_find_for_file (LrgEditorApp *app,
                                                          const gchar  *path,
                                                          gboolean      machine);

/* --- decisions ------------------------------------------------------------ */

/**
 * lrg_editor_ai_debug_panel_watch_brain:
 * @app: the editor application
 * @name: a name for the brain in the inspector (unique; an existing
 *   brain of that name is replaced)
 * @brain: the brain
 *
 * Shows @brain in the decisions inspector and logs its #LrgDecisionBrain::asked,
 * #LrgDecisionBrain::answered and #LrgDecisionBrain::failed signals. The app
 * keeps a reference until lrg_editor_ai_debug_panel_unwatch_brain().
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_ai_debug_panel_watch_brain (LrgEditorApp     *app,
                                            const gchar      *name,
                                            LrgDecisionBrain *brain);

/**
 * lrg_editor_ai_debug_panel_unwatch_brain:
 * @app: the editor application
 * @name: a brain name given to lrg_editor_ai_debug_panel_watch_brain()
 *
 * Returns: %TRUE if the brain was watched
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ai_debug_panel_unwatch_brain (LrgEditorApp *app,
                                                  const gchar  *name);

/**
 * lrg_editor_ai_debug_panel_dup_brain_names:
 * @app: the editor application
 *
 * Returns: (transfer full) (array zero-terminated=1): the watched brains'
 *   names, in the order they were added
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_editor_ai_debug_panel_dup_brain_names (LrgEditorApp *app);

/**
 * lrg_editor_ai_debug_panel_get_brain:
 * @app: the editor application
 * @name: a brain name
 *
 * Returns: (transfer none) (nullable): the watched brain
 */
LRG_AVAILABLE_IN_ALL
LrgDecisionBrain * lrg_editor_ai_debug_panel_get_brain (LrgEditorApp *app,
                                                        const gchar  *name);

/**
 * lrg_editor_ai_debug_panel_get_log_length:
 * @app: the editor application
 *
 * Returns: the number of decision log entries kept (the newest 200)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_ai_debug_panel_get_log_length (LrgEditorApp *app);

/**
 * lrg_editor_ai_debug_panel_dup_log_entry:
 * @app: the editor application
 * @index: 0 for the oldest kept entry
 *
 * Returns: (transfer full) (nullable): the entry as one line,
 *   "BRAIN KIND TOPIC: TEXT", %NULL when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_ai_debug_panel_dup_log_entry (LrgEditorApp *app,
                                                 guint         index);

/* --- the panel ------------------------------------------------------------ */

/**
 * lrg_editor_ai_debug_panel_get_selected_topic:
 * @self: an #LrgEditorAiDebugPanel
 *
 * Returns: (transfer none) (nullable): the decision topic whose request
 *   the inspector shows
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_ai_debug_panel_get_selected_topic (LrgEditorAiDebugPanel *self);

/**
 * lrg_editor_ai_debug_panel_select_topic:
 * @self: an #LrgEditorAiDebugPanel
 * @brain: (nullable): a watched brain's name, %NULL for the first
 * @topic: (nullable): a topic, %NULL to clear
 *
 * Shows @topic's request and answer below the topic rows.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_ai_debug_panel_select_topic (LrgEditorAiDebugPanel *self,
                                             const gchar           *brain,
                                             const gchar           *topic);

/**
 * lrg_editor_ai_debug_panel_describe:
 * @self: an #LrgEditorAiDebugPanel
 *
 * What the panel shows, as text: one line per entity, the selected
 * entity's blackboard and the brains (tests, screenshots in reports).
 *
 * Returns: (transfer full): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_ai_debug_panel_describe (LrgEditorAiDebugPanel *self);

G_END_DECLS

/*
 * Helpers shared by the AI editors (lrg-editor-bt-editor.c,
 * lrg-editor-fsm-editor.c and this panel); not part of the public API.
 */
#ifdef LRG_EDITOR_PANEL_INTERNAL
#include <json-glib/json-glib.h>
#include "../lrg-editor-document.h"
#include "../../ui/lrg-theme.h"
#include "../../ui/lrg-node-graph.h"
#include "data/lrg-editor-data-editor.h"

G_GNUC_INTERNAL gchar *      editor_ai_yaml_str      (const gchar *s);
G_GNUC_INTERNAL gchar *      editor_ai_num_str       (gdouble v, gboolean keep_point);
G_GNUC_INTERNAL gchar *      editor_ai_error_path    (const gchar *message);
G_GNUC_INTERNAL JsonNode *   editor_ai_doc_to_json   (LrgEditorDocument *doc, const gchar *path);
G_GNUC_INTERNAL gchar *      editor_ai_json_to_yaml  (JsonNode *node);
G_GNUC_INTERNAL gchar *      editor_ai_flow_yaml     (LrgEditorDocument *doc, const gchar *path);
G_GNUC_INTERNAL gchar *      editor_ai_text_to_yaml  (const gchar *text, const gchar *type);
G_GNUC_INTERNAL const gchar * editor_ai_param_string (JsonObject *params, const gchar *name);
G_GNUC_INTERNAL const gchar * editor_ai_require_string (JsonObject *params, const gchar *name, GError **error);
G_GNUC_INTERNAL gboolean     editor_ai_param_number  (JsonObject *params, const gchar *name, gdouble *out);
G_GNUC_INTERNAL gboolean     editor_ai_param_is_null (JsonObject *params, const gchar *name);
G_GNUC_INTERNAL void         editor_ai_fail          (GError **error, const GError *cause);
G_GNUC_INTERNAL void         editor_ai_fail_msg      (GError **error, gint code, const gchar *message);
G_GNUC_INTERNAL JsonNode *   editor_ai_object_node   (JsonObject *object);
G_GNUC_INTERNAL JsonNode *   editor_ai_issues_result (LrgEditorDataEditor *editor);
G_GNUC_INTERNAL gboolean     editor_ai_create_file   (LrgEditorApp *app, const gchar *path, const gchar *text,
                                                      gchar **out_abs, GError **error);
G_GNUC_INTERNAL LrgWidget *  editor_ai_card_body_new (const gchar *text, LrgThemeColor role, gfloat width,
                                                      guint lines);
G_GNUC_INTERNAL gchar *      editor_ai_dup_display_name (LrgEditorApp *app, LrgAiComponent *component);
G_GNUC_INTERNAL void         editor_ai_card_body_set (LrgWidget *body, const gchar *text, LrgThemeColor role);
G_GNUC_INTERNAL LrgWidget *  editor_ai_heading       (const gchar *text);
G_GNUC_INTERNAL LrgWidget *  editor_ai_button        (const gchar *name, const gchar *text, GCallback callback,
                                                      gpointer user_data);
G_GNUC_INTERNAL void         editor_ai_add           (LrgWidget *box, LrgWidget *child);
G_GNUC_INTERNAL void         editor_ai_frame_canvas  (LrgNodeGraph *canvas);
#endif
