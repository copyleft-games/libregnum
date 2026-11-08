/* lrgeditorctl.c - control running libregnum editors
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * `lrgeditorctl` is to `lrgeditor` what kubectl is to a cluster: verbs
 * (get, describe, create, delete, set, edit, apply, ...) acting on
 * resources (nodes, selection, actions, problems, assets, ...) of a
 * running editor session, with -o yaml|json|table|wide|name output. It
 * talks to the editor's JSON-lines IPC socket and doubles as an MCP
 * server (`lrgeditorctl mcp`) so AI agents can drive the editor.
 */

#include "lrgeditorctl-client.h"
#include "lrgeditorctl-format.h"
#ifdef LRG_EDITORCTL_HAVE_MCP
#include "lrgeditorctl-mcp.h"
#endif
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef LRG_EDITORCTL_VERSION
#define LRG_EDITORCTL_VERSION "0.0.0"
#endif

/* ==========================================================================
 * Options
 * ========================================================================== */

static gchar    *opt_session  = NULL;
static gchar    *opt_output   = NULL;
static gint      opt_timeout  = 10;
static gchar    *opt_parent   = NULL;
static gchar    *opt_kind     = NULL;
static gchar    *opt_primitive = NULL;
static gint      opt_index    = -1;
static gboolean  opt_keep_world = FALSE;
static gboolean  opt_add      = FALSE;
static gboolean  opt_clear    = FALSE;
static gchar    *opt_filename = NULL;
static gboolean  opt_watch    = FALSE;
static gint      opt_tail     = 50;
static gboolean  opt_window   = FALSE;
static gboolean  opt_force    = FALSE;
static gboolean  opt_wait     = FALSE;
static gchar    *opt_level    = NULL;
static gchar    *opt_type     = NULL;
static gchar    *opt_node     = NULL;
static gchar    *opt_target   = NULL;
static gboolean  opt_license  = FALSE;
static gboolean  opt_version  = FALSE;
static gchar   **opt_args     = NULL;
static const gchar *ctl_argv0 = NULL;

static LrgCtlFormat output = LRG_CTL_FORMAT_DEFAULT;

static const GOptionEntry entries[] = {
	{ "session", 's', 0, G_OPTION_ARG_STRING, &opt_session,
	  "Editor to talk to: socket path, PID, or part of the project/level path (default: $LRG_EDITOR_SOCKET, else the newest)", "SESSION" },
	{ "output", 'o', 0, G_OPTION_ARG_STRING, &opt_output,
	  "Output format: yaml, json, table, wide, name", "FORMAT" },
	{ "timeout", 0, 0, G_OPTION_ARG_INT, &opt_timeout, "Seconds to wait for a reply (default 10)", "SEC" },
	{ "parent", 'p', 0, G_OPTION_ARG_STRING, &opt_parent, "Parent node (create, move)", "NODE" },
	{ "kind", 'k', 0, G_OPTION_ARG_STRING, &opt_kind,
	  "Visual kind for create: none, primitive, mesh-asset, sprite, tilemap, light, camera, audio-emitter", "KIND" },
	{ "primitive", 0, 0, G_OPTION_ARG_STRING, &opt_primitive,
	  "Primitive for create: cube, uv-sphere, plane, cylinder, cone, torus, ...", "SHAPE" },
	{ "index", 'i', 0, G_OPTION_ARG_INT, &opt_index, "Position among siblings (create, move)", "N" },
	{ "keep-world", 0, 0, G_OPTION_ARG_NONE, &opt_keep_world, "Keep the world transform when moving", NULL },
	{ "add", 0, 0, G_OPTION_ARG_NONE, &opt_add, "Add to the selection instead of replacing it", NULL },
	{ "clear", 0, 0, G_OPTION_ARG_NONE, &opt_clear, "Clear the selection", NULL },
	{ "filename", 'f', 0, G_OPTION_ARG_FILENAME, &opt_filename, "YAML/JSON file for apply ('-' for stdin)", "FILE" },
	{ "watch", 'w', 0, G_OPTION_ARG_NONE, &opt_watch, "Keep streaming (logs)", NULL },
	{ "tail", 0, 0, G_OPTION_ARG_INT, &opt_tail, "Log records to show first (default 50)", "N" },
	{ "window", 0, 0, G_OPTION_ARG_NONE, &opt_window, "Screenshot the whole window, not just the viewport", NULL },
	{ "force", 0, 0, G_OPTION_ARG_NONE, &opt_force, "Discard unsaved changes (open, new, quit)", NULL },
	{ "wait", 0, 0, G_OPTION_ARG_NONE, &opt_wait, "Wait for a started job to finish (env bake, bake start)", NULL },
	{ "level", 'l', 0, G_OPTION_ARG_STRING, &opt_level,
	  "Toast level for notify: info, success, warning, error (or 0-3)", "LEVEL" },
	{ "type", 't', 0, G_OPTION_ARG_STRING, &opt_type, "Asset type filter for get assets", "TYPE" },
	{ "node", 'n', 0, G_OPTION_ARG_STRING, &opt_node,
	  "Node for physics verbs (GUID or name path; default the primary selection)", "NODE" },
	{ "target", 0, 0, G_OPTION_ARG_STRING, &opt_target, "Joint target node for physics add-joint", "NODE" },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version, "Print the version and exit", NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_args, NULL, "COMMAND [RESOURCE] [ARGS...]" },
	G_OPTION_ENTRY_NULL
};

static const gchar *description =
	"Commands:\n"
	"  sessions                         list running editors\n"
	"  get RESOURCE [NAME...]           nodes, selection, actions, problems, assets, project,\n"
	"                                   info, view, history, methods, state, play, sessions\n"
	"  describe node NAME               everything about a node\n"
	"  create node NAME [key=value...]  --parent, --kind, --primitive, --index\n"
	"  delete node NAME...\n"
	"  set node NAME key=value...       name, location, rotation_degrees, scale, visible, locked\n"
	"  rename node NAME NEW_NAME\n"
	"  move node NAME                   --parent (omit for top level), --index, --keep-world\n"
	"  duplicate node NAME\n"
	"  edit node NAME                   edit as YAML in $VISUAL/$EDITOR, apply on save\n"
	"  apply -f FILE                    create/update nodes from YAML or JSON ('-' = stdin)\n"
	"  select NAME... | select --clear  --add to extend the selection\n"
	"  run ACTION                       run an editor action (see `get actions`)\n"
	"  play [start|pause|step|stop|state]\n"
	"  stop                             stop play-in-editor (restores the level)\n"
	"  game start|stop                  run the game as its own process\n"
	"  undo | redo | save [PATH] | reload | validate\n"
	"  open LEVEL [--force] | new [NAME] [--force]\n"
	"  view [top|front|right|bottom|back|left|persp|all] | view focus NAME | view set key=value...\n"
	"  screenshot FILE.png [--window]\n"
	"  tiles NODE [ACTION key=value...] tilemap grid; ACTION: paint rect fill pick configure\n"
	"  timeline [list|get|add|rm|key|tangent|play|stop|seek|state] ...  keyframe timelines\n"
	"  thumbnail PATH                   generate/print an asset's cached thumbnail\n"
	"  meta get PATH | meta set PATH key=value... [guid=G]  asset .meta sidecar\n"
	"  vcs [PATH]                       git branch and file status (badges)\n"
	"  vcs diff|log|locks|magit | vcs commit MESSAGE\n"
	"  merge BASE OURS THEIRS | merge install [DIR]   .rlevel merge driver\n"
	"  po extract|pseudo TEXT | po import PATH ID | po export PATH ID TEXT\n"
	"  collab apply USER GUID REV PROP VALUE | collab get GUID PROP\n"
	"  collab lock|unlock USER GUID | collab cursor USER X Y Z | collab presence\n"
	"  scenes list | scenes open PATH | scenes active INDEX\n"
	"  gizmo apply OP [axis=N] [value=N] | gizmo pivot MODE | gizmo cursor X Y Z | gizmo origin\n"
	"  prefab overrides|apply|revert|relink|unlink NODE [keys=a,b] | prefab sync [PATH]\n"
	"  code open PATH[:LINE] | code list | code save [PATH]   built-in code editor\n"
	"  inspector list|get|set|reset|state|apply-override|revert-override [NODE] [NAME] [key=value...]\n"
	"  hierarchy reveal|rename NODE [NEW_NAME] | hierarchy filter TEXT | hierarchy state\n"
	"  asset reveal|duplicate|trash PATH | asset rename PATH NAME | asset move PATH DEST | asset orphans\n"
	"  source list | source ls SOURCE [FOLDER] | source search SOURCE PATTERN [limit=N]\n"
	"  source export SOURCE PATH [DEST] | source import|place SOURCE PATH   asset sources (game modules)\n"
	"  profiler [frames=N] [enable=true]  profiler section timings\n"
	"  data list | data open PATH       data editors (input map, locale, sound bank, theme, layouts, ...)\n"
	"  data get [PATH] [AT] | data set [PATH] AT VALUE | data edit [PATH] OP AT [key=value...]\n"
	"  data undo|redo|validate|save|reload|close|reveal [PATH] [AT] [key=value...]  (close --force)\n"
	"  palette [commands|files] [QUERY] open the command palette / quick-open\n"
	"  preferences [PAGE]               open the preferences dialog (e.g. keybindings)\n"
	"  view layout single|split-h|split-v|quad | view render-mode MODE | view overlay NAME on|off\n"
	"  view layers [MASK|LAYER on|off]  a viewport's render-layer mask (viewport=N)\n"
	"                                   MODE: lit unlit wireframe normals lighting-only overdraw uv\n"
	"                                   light-complexity ao reflections\n"
	"  material open|create|variant|get|set|revert|save|preview|assign|list ...  .rmat materials\n"
	"  shader create|compile|diagnostics|uniforms|reload PATH   .rshader user shaders\n"
	"  shadergraph open|create|types|get|add|remove|connect|disconnect|param|input|validate|glsl|save ...\n"
	"  env [get|set KEY VALUE|reset|bake [--wait]|progress|show]  the level environment block\n"
	"  post types|open|create|get|add|remove|move|set|save|preview|look|assign ...  .rpost post stacks\n"
	"  lod get NODE | lod force NODE LEVEL|auto   LOD preview of a mesh node\n"
	"  bake [get|set KEY VALUE|reset|start [--wait]|progress|cancel|save [DIR]|clear|evaluate [X Y Z [NX NY NZ]]\n"
	"       |preview on|off|page N|show]  the level's lighting bake (lightmaps, probe grids)\n"
	"  seq list|open|create|get|set|add-track|add-key|set-key|add-section|remove|bind|play|scrub|save|undo|from-timeline ...\n"
	"                                   .rseq sequences (path=... for one that is not open)\n"
	"  skel2d create|open|get|add-bone|set-bone|remove-bone|add-slot|set-slot|remove-slot|draw-order|attach|detach\n"
	"         |mesh|auto-weights|paint-weights|skin|clip|key|play|mode|pick|save PATH ...  .rskel2d 2D skeletons\n"
	"  anim open|create|get|enter|add-layer|remove-layer|add-state|remove-state|set-state|add-transition\n"
	"       |set-transition|remove-transition|add-param|remove-param|set-param|blend-tree|preview|validate|save PATH ...\n"
	"                                   .ranim animation graphs\n"
	"  bonemap open|create|get|set|auto-fill|rules|models|preview|save PATH ...  .rbonemap retarget maps\n"
	"  clip list|info|preview|morph MODEL ...  glTF clips and morph targets of a model\n"
	"  physics get|add-body [KIND]|remove-body|set-body K=V..|add-shape [KIND] [K=V..]|remove-shape I\n"
	"          |set-shape I K=V..|move-shape I TO|add-joint [KIND] [--target N] [K=V..]|remove-joint I\n"
	"          |set-joint I K=V..|fit [I] [--kind K]|overlay [NAME..|+NAME|-NAME]|state|show  [--node N]\n"
	"                                   the selected node's physics: block (body, shapes, joints)\n"
	"  physics layers [get]|name I NAME|-|collide A B on|off|settings [K=V..]|reset [empty=true]|open\n"
	"                                   project collision layers, collide matrix, world settings\n"
	"  physasset open|create|get|model|add-part|remove-part|set-part|generate|stabilize|exclude|unexclude\n"
	"            |set|select|simulate|save PATH ...  .rphysasset ragdolls\n"
	/* m67e-usage: begin */
	"  nav get|set|new|bake|cancel|save|clear|area-add|area-remove|area-set|area-box-add|area-box-remove|\n"
	"      area-box-set|link-add|link-remove|link-set|select|overlay|agents|debug-agent|show ... [key=value...]\n"
	"                                   navigation (Navigation panel)\n"
	"  bt open|create|get|palette|add-node|remove-node|move-node|set-node|set-position|layout|select|\n"
	"     show|add-key|remove-key|set-subtree|remove-subtree|open-subtree|validate|save|live ... [key=value...]\n"
	"                                   behaviour tree (behaviour tree editor)\n"
	"  fsm open|create|get|enter|up|add-state|remove-state|rename-state|set-initial|add-transition|\n"
	"      set-transition|remove-transition|set-actions|set-position|select|add-key|remove-key|validate|\n"
	"      save|live|send-event ... [key=value...]\n"
	"                                   state machine (state machine editor)\n"
	"  ai show|entities|select|state|set-key|send-event|brains|topic|log ... [key=value...]\n"
	"                                   ai debug (AI panel)\n"
	"  particles open|create|get|kinds|set|add-emitter|remove-emitter|move-emitter|duplicate-emitter|\n"
	"            add-module|remove-module|move-module|select|solo|play|restart|seek|preview|stats|save|\n"
	"            render ... [key=value...]\n"
	"                                   particle effect (particle editor)\n"
	"  weather open|create|get|kinds|set|add-preset|remove-preset|set-preset|add-layer|remove-layer|\n"
	"          move-layer|set-layer|set-param|set-transition|remove-transition|select|preview|save|render ... [key=value...]\n"
	"                                   weather (weather editor)\n"
	"  terrain get|brush|stroke|sample|add-layer|remove-layer|set-layer|add-foliage|remove-foliage|\n"
	"          set-foliage|rebake|save|create|show ... [key=value...]\n"
	"                                   terrain (Terrain panel)\n"
	"  water get|set|set-wave|add-point|set-point|remove-point|save ... [key=value...]\n"
	"                                   water (water editor)\n"
	"  mixer show|open|create|set-default|get|effect-types|add-bus|remove-bus|set-bus|set-send|\n"
	"        remove-send|add-effect|remove-effect|move-effect|set-effect|add-snapshot|remove-snapshot|\n"
	"        set-snapshot|capture-snapshot|set-duck|remove-duck|preview|meters|save ... [key=value...]\n"
	"                                   mixer (mixer editor)\n"
	"  audio get|set|curve|buses ... [key=value...]\n"
	"                                   audio emitter (inspector)\n"
	"  tiles state|grid|brush|autopaint|refresh|cell-at|tile-info ... [key=value...]\n"
	"                                   tile painting (Tiles panel)\n"
	"  tileset create|open|get|set|select|tile|collision|rule-tile|rule|preview|save ... [key=value...]\n"
	"                                   tileset (tileset editor)\n"
	"  sprite create|open|get|slice|frame|clip|clip-frame|event|play|save ... [key=value...]\n"
	"                                   sprite sheet (sprite editor)\n"
	"  debug state|show|breakpoints|set-breakpoint|remove-breakpoint|toggle-breakpoint|clear-breakpoints|\n"
	"        continue|pause|step-over|step-in|step-out|stop|stack|scopes|variables|evaluate|watches|\n"
	"        add-watch|remove-watch|dap-start|dap-stop ... [key=value...]\n"
	"                                   script debugger (Debug panel)\n"
	"  lsp servers|status|diagnostics|complete|hover|definition|references|rename|format|restart ... [key=value...]\n"
	"                                   language server (code panel)\n"
	"  profiler sample|set|capture|save|load|frames|frame|compare|view ... [key=value...]\n"
	"                                   profiler capture (profiler panel)\n"
	"  frame capture|status|calls|call|scrub|texture|save|load|show ... [key=value...]\n"
	"                                   frame debugger (Frame Debugger panel)\n"
	"  script run|languages|tools|run-tool|reload|run-in-editor|editor-running ... [key=value...]\n"
	"                                   tool script (console panel)\n"
	"  vscript create|open|get|node-types|gi-search|add-node|remove-node|set-node|connect|disconnect|\n"
	"          set-variable|add-subgraph|remove-subgraph|add-comment|remove-comment|set-program|select|\n"
	"          validate|run|live|breakpoint|continue|save ... [key=value...]\n"
	"                                   visual script (visual script editor)\n"
	"  memory status|types|resources|snapshot|snapshots|diff|clear-snapshots|history|show ... [key=value...]\n"
	"                                   memory profiler (Memory panel)\n"
	"  network stats|history|enable|reset|show ... [key=value...]\n"
	"                                   network stats (Network panel)\n"
	/* m67e-usage: end */
	"  asset new KIND [path=P] [name=N] [image=I]  a new asset file (bt fsm particles weather mixer tileset\n"
	"                                   sprite water nav-settings terrain ...)\n"
	"  logs [--tail N] [-w]             editor log; -w follows\n"
	"  events [TYPE...]                 stream events (changed, selection, level, saved, log, ...)\n"
	"  call METHOD [key=value...|JSON]  any IPC method\n"
	"  notify MESSAGE [--level LEVEL]   toast in the editor: info, success, warning, error (or 0-3)\n"
	"  quit [--force]\n"
	"  mcp                              serve MCP on stdio (for AI agents)\n"
	"  completion bash                  shell completion script\n"
	"\n"
	"Examples:\n"
	"  lrgeditorctl sessions\n"
	"  lrgeditorctl get nodes\n"
	"  lrgeditorctl get node Props/Crate -o yaml\n"
	"  lrgeditorctl create node Crate --parent Props --primitive cube location=2,0.5,-3\n"
	"  lrgeditorctl set node Props/Crate rotation_degrees=0,45,0 scale=2,2,2\n"
	"  lrgeditorctl edit node Props/Crate\n"
	"  lrgeditorctl get node Props/Crate -o yaml > crate.yaml; lrgeditorctl apply -f crate.yaml\n"
	"  lrgeditorctl run view.frame-all\n"
	"  lrgeditorctl -s mygame screenshot /tmp/view.png --window\n"
	"  lrgeditorctl logs -w\n"
	"  lrgeditorctl tiles Ground paint tile=3 points=[[0,0],[7,0]] brush=2\n"
	"  lrgeditorctl timeline key Intro Props/Crate time=1 channel=location.y value=2 easing=ease-out-bounce\n"
	"  lrgeditorctl thumbnail assets/crate.png\n"
	"  lrgeditorctl meta set assets/crate.png filter=point max-size=512\n"
	"  lrgeditorctl prefab apply Props/Crate keys=transform.location\n"
	"  lrgeditorctl source ls librewow World/Azeroth; lrgeditorctl source import librewow World/tree.m2\n"
	"  lrgeditorctl code open scripts/door.lua:12\n"
	"  lrgeditorctl view layout quad\n"
	"  lrgeditorctl data open data/input.yaml\n"
	"  lrgeditorctl data set /actions/jump/bindings/0/key ENTER; lrgeditorctl data save\n"
	"  lrgeditorctl view mode light-complexity viewport=1\n"
	"  lrgeditorctl view layers 0x5\n"
	"  lrgeditorctl material create materials/steel.rmat shading_model=lit\n"
	"  lrgeditorctl material set materials/steel.rmat metallic 1\n"
	"  lrgeditorctl material assign Props/Crate materials/steel.rmat\n"
	"  lrgeditorctl shader compile shaders/water.rshader\n"
	"  lrgeditorctl shadergraph connect graphs/glow.rshadergraph time out mul a\n"
	"  lrgeditorctl env set fog.density 0.02; lrgeditorctl env bake --wait\n"
	"  lrgeditorctl post add looks/night.rpost vignette; lrgeditorctl post set looks/night.rpost 0 intensity 0.4\n"
	"  lrgeditorctl lod force Props/Statue 2\n"
	"  lrgeditorctl bake set samples 256; lrgeditorctl bake start --wait\n"
	"  lrgeditorctl bake evaluate 0 0.01 0 0 1 0\n"
	"  lrgeditorctl seq create seq/intro.rseq length=8; lrgeditorctl seq add-track transform binding=Props/Crate\n"
	"  lrgeditorctl seq add-key 0 1.5 2 channel=location.y; lrgeditorctl seq scrub 1.5\n"
	"  lrgeditorctl skel2d add-bone rigs/hero.rskel2d arm parent=root length=40\n"
	"  lrgeditorctl anim add-transition anims/hero.ranim idle walk conditions='[{\"parameter\":\"speed\",\"op\":\">\",\"value\":0.1}]'\n"
	"  lrgeditorctl bonemap auto-fill maps/mixamo.rbonemap\n"
	"  lrgeditorctl clip list models/hero.glb\n"
	"  lrgeditorctl physics add-body dynamic --node Props/Crate; lrgeditorctl physics set-body mass=20 --node Props/Crate\n"
	"  lrgeditorctl physics add-joint hinge --target Door/Frame axis=0,1,0 --node Door\n"
	"  lrgeditorctl physics layers name 9 water; lrgeditorctl physics layers collide water debris off\n"
	"  lrgeditorctl physics layers settings gravity=0,-3.7,0 iterations=12\n"
	"  lrgeditorctl physasset create chars/hero.rphysasset model=chars/hero.glb total_mass=80\n"
	"  lrgeditorctl physasset set-part chars/hero.rphysasset shin_l joint=hinge twist_degrees=0,140\n"
	"  lrgeditorctl physasset simulate chars/hero.rphysasset advance=2\n"
	"  lrgeditorctl nav set agent_radius=0.4; lrgeditorctl nav bake; lrgeditorctl nav save\n"
	"  lrgeditorctl bt add-node ai/guard.rbt wait parent=root props='{\"seconds\":2}'\n"
	"  lrgeditorctl fsm add-transition ai/door.rfsm idle open event=use\n"
	"  lrgeditorctl particles add-module fx/sparks.rparticles 0 gravity; lrgeditorctl particles seek fx/sparks.rparticles 1.5\n"
	"  lrgeditorctl weather preview fx/sky.rweather preset=clear transition_to=storm\n"
	"  lrgeditorctl terrain stroke points=[[0,0],[4,0]] tool=raise radius=6 --node Island\n"
	"  lrgeditorctl water points-add 4 0 width=3 --node Lake\n"
	"  lrgeditorctl mixer add-bus footsteps parent=sfx; lrgeditorctl audio set Speaker max_distance=30 bus=sfx\n"
	"  lrgeditorctl tiles autopaint cells=[3,4,4,4] rule_tile=grass --node Ground\n"
	"  lrgeditorctl tileset rule-tile preset grass preset=blob47 first=16 path=tiles/ground.rtileset\n"
	"  lrgeditorctl sprite clip add walk frames=[0,1,2,3] fps=10 path=sprites/hero.rsprite\n"
	"  lrgeditorctl asset new bt path=ai/guard.rbt; lrgeditorctl asset new sprite image=sprites/hero.png\n"
	"  lrgeditorctl notify 'Build finished' --level success\n"
	"  claude mcp add libregnum -- lrgeditorctl mcp\n";

static const gchar *license_text =
	"lrgeditorctl - control running libregnum editors\n"
	"Copyright (C) 2025  Zach Podbielniak\n"
	"\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n"
	"\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gchar *session_socket = NULL;

static JsonNode *
call (const gchar  *method,
      JsonObject   *params,
      GError      **error)
{
	if (session_socket == NULL)
	{
		session_socket = lrg_ctl_resolve_session (opt_session, error);
		if (session_socket == NULL)
			return NULL;
	}
	return lrg_ctl_call (session_socket, method, params, (guint) MAX (opt_timeout, 1) * 1000, error);
}

/* A call whose result only matters for errors. */
static gboolean
call_ok (const gchar  *method,
         JsonObject   *params,
         GError      **error)
{
	g_autoptr(JsonNode) r = call (method, params, error);

	return r != NULL;
}

static gboolean
usage_error (GError      **error,
             const gchar  *format,
             ...) G_GNUC_PRINTF (2, 3);

static gboolean
usage_error (GError      **error,
             const gchar  *format,
             ...)
{
	va_list           ap;
	g_autofree gchar *msg = NULL;

	va_start (ap, format);
	msg = g_strdup_vprintf (format, ap);
	va_end (ap);
	g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_USAGE, msg);
	return FALSE;
}

static JsonObject *
node_param (const gchar *node)
{
	JsonObject *p = json_object_new ();

	json_object_set_string_member (p, "node", node);
	return p;
}

static const gchar *
str_member (JsonObject  *o,
            const gchar *name)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

/* An argument naming a file: absolute when it exists from here (the
 * editor resolves relative paths against its project, not our cwd). */
static gchar *
path_arg (const gchar *arg)
{
	g_autofree gchar *abs = g_canonicalize_filename (arg, NULL);

	return g_file_test (abs, G_FILE_TEST_EXISTS) ? g_steal_pointer (&abs) : g_strdup (arg);
}

/*
 * Calls @method with params built from @args: arguments without '=' fill
 * @keys in order (a key named "path" or "dest" goes through path_arg()),
 * key=value arguments are parsed with lrg_ctl_parse_assignments(). The
 * result prints as YAML unless -o says otherwise. Every verb added for
 * the 0.8.1 methods goes through here, so the editor's params stay the
 * single source of truth (unknown keys pass straight through).
 */
static gboolean
passthrough (const gchar         *method,
             const gchar * const *keys,
             gchar              **args,
             GError             **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(GPtrArray)  assigns = g_ptr_array_new ();
	g_autoptr(JsonNode)   r = NULL;
	guint                 k = 0;
	guint                 i;

	for (i = 0; args != NULL && args[i] != NULL; i++)
	{
		if (strchr (args[i], '=') == NULL && keys != NULL && keys[k] != NULL)
		{
			if (g_strcmp0 (keys[k], "path") == 0 || g_strcmp0 (keys[k], "dest") == 0)
			{
				g_autofree gchar *path = path_arg (args[i]);

				json_object_set_string_member (p, keys[k], path);
			}
			else
				json_object_set_string_member (p, keys[k], args[i]);
			k++;
		}
		else
			g_ptr_array_add (assigns, args[i]);
	}
	g_ptr_array_add (assigns, NULL);
	if (!lrg_ctl_parse_assignments ((gchar * const *) assigns->pdata, p, error))
		return FALSE;
	r = call (method, p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/* "a,b,c" (or one name) -> ["a","b","c"], for keys= arguments */
static void
split_list_member (JsonObject  *p,
                   const gchar *name)
{
	JsonNode *n;

	if (!json_object_has_member (p, name))
		return;
	n = json_object_get_member (p, name);
	if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	{
		g_auto(GStrv) parts = g_strsplit (json_node_get_string (n), ",", -1);
		JsonArray    *arr = json_array_new ();
		guint         i;

		for (i = 0; parts[i] != NULL; i++)
			if (*g_strstrip (parts[i]) != '\0')
				json_array_add_string_element (arr, parts[i]);
		json_object_set_array_member (p, name, arr);
	}
}

/* GUID of a node given by path or GUID (paths change on rename/move). */
static gchar *
node_guid (const gchar  *node,
           GError      **error)
{
	g_autoptr(JsonObject) p = node_param (node);
	g_autoptr(JsonNode)   n = call ("node.get", p, error);

	if (n == NULL || !JSON_NODE_HOLDS_OBJECT (n))
		return NULL;
	return g_strdup (str_member (json_node_get_object (n), "guid"));
}

/* Current path of a node, for reporting after an edit. */
static gchar *
node_path (const gchar *guid)
{
	g_autoptr(JsonObject) p = node_param (guid);
	g_autoptr(JsonNode)   n = call ("node.get", p, NULL);

	if (n == NULL || !JSON_NODE_HOLDS_OBJECT (n))
		return g_strdup (guid);
	return g_strdup (str_member (json_node_get_object (n), "path"));
}

/* Resource names and their aliases. */
static const gchar *
canonical_resource (const gchar *r)
{
	static const gchar *table[][2] = {
		{ "node", "nodes" }, { "no", "nodes" }, { "nodes", "nodes" },
		{ "selection", "selection" }, { "sel", "selection" },
		{ "action", "actions" }, { "actions", "actions" },
		{ "problem", "problems" }, { "problems", "problems" },
		{ "asset", "assets" }, { "assets", "assets" },
		{ "project", "project" }, { "info", "info" }, { "editor", "info" }, { "level", "info" },
		{ "view", "view" }, { "history", "history" }, { "methods", "methods" },
		{ "state", "state" }, { "ui", "state" }, { "play", "play" },
		{ "session", "sessions" }, { "sessions", "sessions" }
	};
	gsize i;

	for (i = 0; r != NULL && i < G_N_ELEMENTS (table); i++)
		if (g_strcmp0 (r, table[i][0]) == 0)
			return table[i][1];
	return NULL;
}

/*
 * notify_level_name:
 * @arg: a --level argument: a toast level name or its number
 *
 * Maps the --level argument of `notify` onto the name ui.notify takes:
 * "info", "success", "warning" or "error" pass through (any case), and
 * 0, 1, 2, 3 select them in that order. Returns %NULL for anything else.
 */
static const gchar *
notify_level_name (const gchar *arg)
{
	static const gchar * const names[] = { "info", "success", "warning", "error" };
	gint64                     n = -1;
	gsize                      i;

	for (i = 0; i < G_N_ELEMENTS (names); i++)
		if (g_ascii_strcasecmp (arg, names[i]) == 0)
			return names[i];
	if (g_ascii_string_to_signed (arg, 10, 0, (gint64) G_N_ELEMENTS (names) - 1, &n, NULL))
		return names[n];
	return NULL;
}

/* node.tree -> flat rows {path, name, guid, kind, visible, locked, children}. */
static void
flatten_tree (JsonArray   *nodes,
              const gchar *prefix,
              JsonArray   *out)
{
	guint i;

	for (i = 0; nodes != NULL && i < json_array_get_length (nodes); i++)
	{
		JsonObject       *n = json_array_get_object_element (nodes, i);
		JsonObject       *row = json_object_new ();
		const gchar      *name = str_member (n, "name");
		g_autofree gchar *path = (prefix != NULL) ? g_strdup_printf ("%s/%s", prefix, name != NULL ? name : "?")
		                                          : g_strdup (name != NULL ? name : "?");
		GList            *members, *l;

		members = json_object_get_members (n);
		for (l = members; l != NULL; l = l->next)
			if (g_strcmp0 (l->data, "nodes") != 0)
				json_object_set_member (row, l->data, json_node_copy (json_object_get_member (n, l->data)));
		g_list_free (members);
		json_object_set_string_member (row, "path", path);
		json_array_add_object_element (out, row);

		if (json_object_has_member (n, "nodes"))
			flatten_tree (json_object_get_array_member (n, "nodes"), path, out);
	}
}

static gchar *
format_time (gint64 usec)
{
	g_autoptr(GDateTime) t = g_date_time_new_from_unix_local_usec (usec);

	return (t != NULL) ? g_date_time_format (t, "%H:%M:%S") : g_strdup ("-");
}

/* ==========================================================================
 * get / describe / sessions
 * ========================================================================== */

static gboolean
cmd_sessions (GError **error)
{
	g_autoptr(GPtrArray) sessions = lrg_ctl_list_sessions ();
	g_autoptr(JsonArray) rows = json_array_new ();
	g_autofree gchar    *current = lrg_ctl_resolve_session (opt_session, NULL);
	static const LrgCtlColumn columns[] = {
		{ "CURRENT", "current", FALSE }, { "PID", "pid", FALSE }, { "PROJECT", "project", FALSE },
		{ "LEVEL", "level", FALSE }, { "SOCKET", "socket", TRUE }
	};
	guint i;

	for (i = 0; i < sessions->len; i++)
	{
		LrgCtlSession *s = g_ptr_array_index (sessions, i);
		JsonObject    *o = json_object_new ();

		json_object_set_string_member (o, "current", g_strcmp0 (s->socket, current) == 0 ? "*" : "");
		json_object_set_int_member (o, "pid", s->pid);
		json_object_set_string_member (o, "project", s->project != NULL ? s->project : "-");
		json_object_set_string_member (o, "level", s->level != NULL ? s->level : "-");
		json_object_set_string_member (o, "socket", s->socket);
		json_array_add_object_element (rows, o);
	}
	if (sessions->len == 0 && output != LRG_CTL_FORMAT_JSON && output != LRG_CTL_FORMAT_YAML)
	{
		g_printerr ("No running editors.\n");
		return TRUE;
	}
	lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "socket");
	return TRUE;
}

static gboolean
cmd_get (gchar   **args,
         GError  **error)
{
	const gchar *res = canonical_resource (args[0]);
	guint        i;

	if (res == NULL)
		return usage_error (error, "Unknown resource '%s' (nodes, selection, actions, problems, assets, project, "
		                    "info, view, history, methods, state, play, sessions)", args[0] != NULL ? args[0] : "");

	if (g_strcmp0 (res, "sessions") == 0)
		return cmd_sessions (error);

	/* get node NAME...: each node as YAML (a list when several). */
	if (g_strcmp0 (res, "nodes") == 0 && args[1] != NULL)
	{
		g_autoptr(JsonArray) all = json_array_new ();

		for (i = 1; args[i] != NULL; i++)
		{
			g_autoptr(JsonObject) p = node_param (args[i]);
			JsonNode             *n = call ("node.get", p, error);

			if (n == NULL)
				return FALSE;
			json_array_add_element (all, n);
		}
		if (output == LRG_CTL_FORMAT_NAME)
		{
			for (i = 0; i < json_array_get_length (all); i++)
				printf ("node/%s\n", str_member (json_array_get_object_element (all, i), "path"));
			return TRUE;
		}
		if (json_array_get_length (all) == 1)
			lrg_ctl_print (json_array_get_element (all, 0), output);
		else
		{
			g_autoptr(JsonNode) list = json_node_new (JSON_NODE_ARRAY);

			json_node_set_array (list, all);
			lrg_ctl_print (list, output);
		}
		return TRUE;
	}

	if (g_strcmp0 (res, "nodes") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "PATH", "path", FALSE }, { "KIND", "kind", FALSE }, { "VISIBLE", "visible", FALSE },
			{ "LOCKED", "locked", FALSE }, { "CHILDREN", "children", FALSE }, { "GUID", "guid", TRUE }
		};
		g_autoptr(JsonNode)  tree = call ("node.tree", NULL, error);
		g_autoptr(JsonArray) rows = json_array_new ();

		if (tree == NULL)
			return FALSE;
		if (output == LRG_CTL_FORMAT_YAML || output == LRG_CTL_FORMAT_JSON)
		{
			lrg_ctl_print (tree, output);
			return TRUE;
		}
		flatten_tree (json_node_get_array (tree), NULL, rows);
		if (output == LRG_CTL_FORMAT_NAME)
		{
			for (i = 0; i < json_array_get_length (rows); i++)
				printf ("node/%s\n", str_member (json_array_get_object_element (rows, i), "path"));
			return TRUE;
		}
		lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}

	if (g_strcmp0 (res, "selection") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "PATH", "path", FALSE }, { "PRIMARY", "primary", FALSE }, { "GUID", "guid", TRUE }
		};
		g_autoptr(JsonNode)  sel = call ("selection.get", NULL, error);
		g_autoptr(JsonArray) rows = json_array_new ();
		JsonArray           *guids;
		const gchar         *primary;

		if (sel == NULL)
			return FALSE;
		guids = json_object_get_array_member (json_node_get_object (sel), "nodes");
		primary = str_member (json_node_get_object (sel), "primary");
		for (i = 0; guids != NULL && i < json_array_get_length (guids); i++)
		{
			const gchar          *guid = json_array_get_string_element (guids, i);
			g_autoptr(JsonObject) p = node_param (guid);
			g_autoptr(JsonNode)   n = call ("node.get", p, NULL);
			JsonObject           *row = json_object_new ();

			json_object_set_string_member (row, "guid", guid);
			json_object_set_string_member (row, "path", n != NULL ? str_member (json_node_get_object (n), "path") : guid);
			json_object_set_string_member (row, "primary", g_strcmp0 (guid, primary) == 0 ? "*" : "");
			json_array_add_object_element (rows, row);
		}
		if (output == LRG_CTL_FORMAT_NAME)
		{
			for (i = 0; i < json_array_get_length (rows); i++)
				printf ("node/%s\n", str_member (json_array_get_object_element (rows, i), "path"));
			return TRUE;
		}
		lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}

	if (g_strcmp0 (res, "actions") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "ID", "id", FALSE }, { "LABEL", "label", FALSE }, { "SHORTCUT", "shortcut", FALSE },
			{ "ENABLED", "enabled", FALSE }, { "CATEGORY", "category", TRUE }, { "CHECKED", "checked", TRUE }
		};
		g_autoptr(JsonNode) list = call ("action.list", NULL, error);

		if (list == NULL)
			return FALSE;
		lrg_ctl_print_table (json_node_get_array (list), columns, G_N_ELEMENTS (columns), output, "id");
		return TRUE;
	}

	if (g_strcmp0 (res, "problems") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "SEVERITY", "severity", FALSE }, { "CODE", "code", FALSE }, { "PATH", "path", FALSE },
			{ "MESSAGE", "message", FALSE }, { "FILE", "file", TRUE }
		};
		g_autoptr(JsonNode) list = call ("level.validate", NULL, error);

		if (list == NULL)
			return FALSE;
		if (json_array_get_length (json_node_get_array (list)) == 0 && output == LRG_CTL_FORMAT_DEFAULT)
		{
			g_printerr ("No problems.\n");
			return TRUE;
		}
		lrg_ctl_print_table (json_node_get_array (list), columns, G_N_ELEMENTS (columns), output, "code");
		return TRUE;
	}

	if (g_strcmp0 (res, "assets") == 0)
	{
		static const LrgCtlColumn columns[] = { { "PATH", "path", FALSE }, { "TYPE", "type", FALSE } };
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   list = NULL;

		if (opt_type != NULL)
			json_object_set_string_member (p, "type", opt_type);
		list = call ("asset.list", p, error);
		if (list == NULL)
			return FALSE;
		lrg_ctl_print_table (json_node_get_array (list), columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}

	if (g_strcmp0 (res, "history") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "#", "index", FALSE }, { "STATE", "state", FALSE }, { "STEP", "step", FALSE }
		};
		g_autoptr(JsonNode)  h = call ("edit.history", NULL, error);
		g_autoptr(JsonArray) rows = json_array_new ();
		JsonArray           *steps;
		gint64               current;

		if (h == NULL)
			return FALSE;
		steps = json_object_get_array_member (json_node_get_object (h), "steps");
		current = json_object_get_int_member (json_node_get_object (h), "current");
		for (i = 0; i < json_array_get_length (steps); i++)
		{
			JsonObject *row = json_object_new ();

			json_object_set_int_member (row, "index", i + 1);
			json_object_set_string_member (row, "state", (gint64) i < current ? "done" : "redo");
			json_object_set_string_member (row, "step", json_array_get_string_element (steps, i));
			json_array_add_object_element (rows, row);
		}
		lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "step");
		return TRUE;
	}

	/* Single objects. */
	{
		static const gchar *methods[][2] = {
			{ "project", "project.info" }, { "info", "editor.info" }, { "view", "view.get" },
			{ "methods", "methods" }, { "state", "ui.state" }, { "play", "play.state" }
		};

		for (i = 0; i < G_N_ELEMENTS (methods); i++)
			if (g_strcmp0 (res, methods[i][0]) == 0)
			{
				g_autoptr(JsonNode) r = call (methods[i][1], NULL, error);

				if (r == NULL)
					return FALSE;
				lrg_ctl_print (r, output);
				return TRUE;
			}
	}
	return usage_error (error, "Cannot get '%s'", args[0]);
}

/* ==========================================================================
 * Node verbs
 * ========================================================================== */

static gboolean
expect_node_resource (gchar       **args,
                      const gchar  *verb,
                      guint         min_names,
                      GError      **error)
{
	guint n = 0;

	if (args[0] == NULL || g_strcmp0 (canonical_resource (args[0]), "nodes") != 0)
		return usage_error (error, "%s works on nodes: lrgeditorctl %s node NAME ...", verb, verb);
	while (args[1 + n] != NULL)
		n++;
	if (n < min_names)
		return usage_error (error, "%s node needs %u argument(s)", verb, min_names);
	return TRUE;
}

static gboolean
cmd_create (gchar  **args,
            GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonObject) set = json_object_new ();
	g_autoptr(JsonNode)   created = NULL;

	if (!expect_node_resource (args, "create", 1, error))
		return FALSE;

	json_object_set_string_member (p, "name", args[1]);
	if (opt_parent != NULL)
		json_object_set_string_member (p, "parent", opt_parent);
	if (opt_kind != NULL)
		json_object_set_string_member (p, "kind", opt_kind);
	if (opt_primitive != NULL)
		json_object_set_string_member (p, "primitive", opt_primitive);
	if (opt_index >= 0)
		json_object_set_int_member (p, "index", opt_index);
	if (!lrg_ctl_parse_assignments (args + 2, set, error))
		return FALSE;

	/*
	 * kind=, primitive=, parent= and index= are node.create parameters
	 * (the same as --kind, --primitive, --parent, --index): node.set does
	 * not know them at the top level and would drop them silently, which
	 * created a plain node for "create node Map kind=tilemap". The flags
	 * win when both are given.
	 */
	{
		static const gchar * const create_keys[] = { "kind", "primitive", "parent", "index", NULL };
		guint                      k;

		for (k = 0; create_keys[k] != NULL; k++)
		{
			if (!json_object_has_member (set, create_keys[k]))
				continue;
			if (!json_object_has_member (p, create_keys[k]))
				json_object_set_member (p, create_keys[k], json_object_dup_member (set, create_keys[k]));
			json_object_remove_member (set, create_keys[k]);
		}
	}

	created = call ("node.create", p, error);
	if (created == NULL)
		return FALSE;
	if (json_object_get_size (set) > 0)
	{
		json_object_set_string_member (set, "node", str_member (json_node_get_object (created), "guid"));
		json_object_set_boolean_member (set, "merge", TRUE);
		json_node_unref (created);
		created = call ("node.set", set, error);
		if (created == NULL)
			return FALSE;
	}
	if (output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_NAME)
		printf ("node/%s created\n", str_member (json_node_get_object (created), "path"));
	else
		lrg_ctl_print (created, output);
	return TRUE;
}

static gboolean
cmd_delete (gchar  **args,
            GError **error)
{
	guint i;

	if (!expect_node_resource (args, "delete", 1, error))
		return FALSE;
	for (i = 1; args[i] != NULL; i++)
	{
		g_autoptr(JsonObject) p = node_param (args[i]);

		if (!call_ok ("node.delete", p, error))
			return FALSE;
		printf ("node/%s deleted\n", args[i]);
	}
	return TRUE;
}

static gboolean
cmd_set (gchar  **args,
         GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;

	if (!expect_node_resource (args, "set", 2, error))
		return FALSE;
	p = node_param (args[1]);
	if (!lrg_ctl_parse_assignments (args + 2, p, error))
		return FALSE;
	r = call ("node.set", p, error);
	if (r == NULL)
		return FALSE;
	if (output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_NAME)
		printf ("node/%s configured\n", str_member (json_node_get_object (r), "path"));
	else
		lrg_ctl_print (r, output);
	return TRUE;
}

static gboolean
cmd_rename (gchar  **args,
            GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autofree gchar     *guid = NULL;
	g_autofree gchar     *path = NULL;

	if (!expect_node_resource (args, "rename", 2, error))
		return FALSE;
	guid = node_guid (args[1], error);
	if (guid == NULL)
		return FALSE;
	p = node_param (guid);
	json_object_set_string_member (p, "name", args[2]);
	if (!call_ok ("node.rename", p, error))
		return FALSE;
	path = node_path (guid);
	printf ("node/%s renamed\n", path);
	return TRUE;
}

static gboolean
cmd_move (gchar  **args,
          GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autofree gchar     *guid = NULL;
	g_autofree gchar     *path = NULL;

	if (!expect_node_resource (args, "move", 1, error))
		return FALSE;
	guid = node_guid (args[1], error);
	if (guid == NULL)
		return FALSE;
	p = node_param (guid);
	if (opt_parent != NULL)
		json_object_set_string_member (p, "parent", opt_parent);
	if (opt_index >= 0)
		json_object_set_int_member (p, "index", opt_index);
	json_object_set_boolean_member (p, "keep_world", opt_keep_world);
	if (!call_ok ("node.move", p, error))
		return FALSE;
	path = node_path (guid);
	printf ("node/%s moved\n", path);
	return TRUE;
}

static gboolean
cmd_duplicate (gchar  **args,
               GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;

	if (!expect_node_resource (args, "duplicate", 1, error))
		return FALSE;
	p = node_param (args[1]);
	r = call ("node.duplicate", p, error);
	if (r == NULL)
		return FALSE;
	if (JSON_NODE_HOLDS_OBJECT (r))
		printf ("node/%s created\n", str_member (json_node_get_object (r), "path"));
	else
		printf ("node/%s duplicated\n", args[1]);
	return TRUE;
}

/* The editable view of a node, for edit and get -o yaml round trips. */
static JsonObject *
editable_node (JsonObject *node)
{
	static const gchar *keep[] = { "name", "location", "scale", "visible", "locked", "visual" };
	JsonObject         *out = json_object_new ();
	gsize               i;

	json_object_set_string_member (out, "node", str_member (node, "guid"));
	for (i = 0; i < G_N_ELEMENTS (keep); i++)
	{
		if (!json_object_has_member (node, keep[i]))
			continue;
		json_object_set_member (out, keep[i], json_node_copy (json_object_get_member (node, keep[i])));

		/* Degrees are friendlier to edit than radians. */
		if (g_strcmp0 (keep[i], "location") == 0 && json_object_has_member (node, "rotation"))
		{
			JsonArray *rad = json_object_get_array_member (node, "rotation");
			JsonArray *deg = json_array_new ();
			guint      k;

			for (k = 0; rad != NULL && k < json_array_get_length (rad); k++)
				json_array_add_double_element (deg, json_array_get_double_element (rad, k) * 180.0 / G_PI);
			json_object_set_array_member (out, "rotation_degrees", deg);
		}
	}
	return out;
}

static gchar *
editor_command (void)
{
	const gchar *e = g_getenv ("VISUAL");

	if (e == NULL || *e == '\0')
		e = g_getenv ("EDITOR");
	return g_strdup ((e != NULL && *e != '\0') ? e : "vi");
}

static gboolean
cmd_edit (gchar  **args,
          GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   node = NULL;
	g_autoptr(JsonNode)   editable = json_node_new (JSON_NODE_OBJECT);
	g_autoptr(GPtrArray)  docs = NULL;
	g_autofree gchar     *yaml = NULL;
	g_autofree gchar     *path = NULL;
	g_autofree gchar     *edited = NULL;
	g_autofree gchar     *before = NULL;
	g_autofree gchar     *cmd = NULL;
	g_auto(GStrv)         words = NULL;
	g_autoptr(GPtrArray)  argv = NULL;
	gint                  fd, status = 0;
	guint                 i;

	if (!expect_node_resource (args, "edit", 1, error))
		return FALSE;
	p = node_param (args[1]);
	node = call ("node.get", p, error);
	if (node == NULL)
		return FALSE;
	json_node_take_object (editable, editable_node (json_node_get_object (node)));
	yaml = lrg_ctl_to_yaml (editable);
	before = g_strdup (yaml);

	fd = g_file_open_tmp ("lrgeditorctl-XXXXXX.yaml", &path, error);
	if (fd < 0)
		return FALSE;
	close (fd);
	{
		g_autofree gchar *text = g_strdup_printf (
			"# Editing node/%s. Lines starting with '#' are ignored.\n"
			"# Save and quit to apply; an empty file or no change does nothing.\n"
			"%s", str_member (json_node_get_object (node), "path"), yaml);

		if (!g_file_set_contents (path, text, -1, error))
			return FALSE;
	}

	/* $VISUAL/$EDITOR may carry arguments ("emacsclient -t"); the editor
	 * shares our terminal (stdin inherited, output not captured). */
	cmd = editor_command ();
	if (!g_shell_parse_argv (cmd, NULL, &words, error))
	{
		g_unlink (path);
		return FALSE;
	}
	argv = g_ptr_array_new ();
	for (i = 0; words[i] != NULL; i++)
		g_ptr_array_add (argv, words[i]);
	g_ptr_array_add (argv, path);
	g_ptr_array_add (argv, NULL);
	if (!g_spawn_sync (NULL, (gchar **) argv->pdata, NULL,
	                   G_SPAWN_SEARCH_PATH | G_SPAWN_CHILD_INHERITS_STDIN, NULL, NULL,
	                   NULL, NULL, &status, error))
	{
		g_unlink (path);
		return FALSE;
	}
	if (!g_file_get_contents (path, &edited, NULL, error))
	{
		g_unlink (path);
		return FALSE;
	}
	g_unlink (path);
	if (!g_spawn_check_wait_status (status, error))
		return FALSE;

	docs = lrg_ctl_parse_documents (edited, error);
	if (docs == NULL)
		return FALSE;
	if (docs->len == 0 || !JSON_NODE_HOLDS_OBJECT (g_ptr_array_index (docs, 0)))
	{
		printf ("Edit cancelled, no changes made.\n");
		return TRUE;
	}
	{
		g_autofree gchar *after = lrg_ctl_to_yaml (g_ptr_array_index (docs, 0));
		JsonObject       *o = json_node_get_object (g_ptr_array_index (docs, 0));
		g_autoptr(JsonNode) r = NULL;

		if (g_strcmp0 (after, before) == 0)
		{
			printf ("Edit cancelled, no changes made.\n");
			return TRUE;
		}
		if (!json_object_has_member (o, "node"))
			json_object_set_string_member (o, "node", str_member (json_node_get_object (node), "guid"));
		r = call ("node.set", o, error);
		if (r == NULL)
			return FALSE;
		printf ("node/%s edited\n", str_member (json_node_get_object (r), "path"));
	}
	return TRUE;
}

/* Makes sure the node at @path exists, creating empty group nodes for
 * any missing segments (like mkdir -p). */
static gboolean
ensure_path (const gchar  *path,
             GError      **error)
{
	g_autoptr(JsonObject) get = node_param (path);
	g_autoptr(JsonNode)   found = call ("node.get", get, NULL);
	g_autoptr(JsonObject) create = NULL;
	g_autoptr(JsonNode)   created = NULL;
	const gchar          *slash;

	if (found != NULL)
		return TRUE;

	slash = strrchr (path, '/');
	create = json_object_new ();
	json_object_set_string_member (create, "name", slash != NULL ? slash + 1 : path);
	if (slash != NULL)
	{
		g_autofree gchar *parent = g_strndup (path, slash - path);

		if (!ensure_path (parent, error))
			return FALSE;
		json_object_set_string_member (create, "parent", parent);
	}
	created = call ("node.create", create, error);
	if (created == NULL)
		return FALSE;
	printf ("node/%s created\n", path);
	return TRUE;
}

/* apply: each document (or array element) is a node spec with "node" (a
 * GUID or path). Existing nodes are updated; missing ones are created
 * under the parent named by the path (missing parents too), then updated. */
static gboolean
apply_one (JsonObject  *spec,
           GError     **error)
{
	const gchar          *target = str_member (spec, "node");
	g_autoptr(JsonObject) get = NULL;
	g_autoptr(JsonNode)   existing = NULL;
	g_autoptr(JsonNode)   r = NULL;
	g_autoptr(GError)     lookup_error = NULL;
	const gchar          *verb = "configured";

	if (target == NULL)
		return usage_error (error, "Each document needs a 'node' (GUID or path)");

	get = node_param (target);
	existing = call ("node.get", get, &lookup_error);
	if (existing == NULL)
	{
		g_autoptr(JsonObject) create = json_object_new ();
		const gchar          *slash = strrchr (target, '/');
		g_autoptr(JsonNode)   created = NULL;
		const gchar          *kind = str_member (spec, "kind");
		const gchar          *primitive = str_member (spec, "primitive");

		if (!g_error_matches (lookup_error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE) ||
		    strstr (lookup_error->message, "not-found") == NULL)
		{
			g_propagate_error (error, g_steal_pointer (&lookup_error));
			return FALSE;
		}
		json_object_set_string_member (create, "name", slash != NULL ? slash + 1 : target);
		if (slash != NULL)
		{
			g_autofree gchar *parent = g_strndup (target, slash - target);

			if (!ensure_path (parent, error))
				return FALSE;
			json_object_set_string_member (create, "parent", parent);
		}
		if (kind != NULL)
			json_object_set_string_member (create, "kind", kind);
		if (primitive != NULL)
			json_object_set_string_member (create, "primitive", primitive);
		created = call ("node.create", create, error);
		if (created == NULL)
			return FALSE;
		json_object_set_string_member (spec, "node", str_member (json_node_get_object (created), "guid"));
		verb = "created";
	}
	json_object_remove_member (spec, "kind");
	json_object_remove_member (spec, "primitive");

	r = call ("node.set", spec, error);
	if (r == NULL)
		return FALSE;
	printf ("node/%s %s\n", str_member (json_node_get_object (r), "path"), verb);
	return TRUE;
}

static gboolean
cmd_apply (GError **error)
{
	g_autofree gchar    *text = NULL;
	g_autoptr(GPtrArray) docs = NULL;
	guint                i, k;

	if (opt_filename == NULL)
		return usage_error (error, "apply needs -f FILE ('-' reads stdin)");
	if (g_strcmp0 (opt_filename, "-") == 0)
	{
		GString *s = g_string_new (NULL);
		gchar    buf[4096];
		gsize    n;

		while ((n = fread (buf, 1, sizeof buf, stdin)) > 0)
			g_string_append_len (s, buf, (gssize) n);
		text = g_string_free (s, FALSE);
	}
	else if (!g_file_get_contents (opt_filename, &text, NULL, error))
		return FALSE;

	docs = lrg_ctl_parse_documents (text, error);
	if (docs == NULL)
		return FALSE;
	for (i = 0; i < docs->len; i++)
	{
		JsonNode *d = g_ptr_array_index (docs, i);

		if (JSON_NODE_HOLDS_OBJECT (d))
		{
			if (!apply_one (json_node_get_object (d), error))
				return FALSE;
		}
		else if (JSON_NODE_HOLDS_ARRAY (d))
		{
			JsonArray *a = json_node_get_array (d);

			for (k = 0; k < json_array_get_length (a); k++)
				if (JSON_NODE_HOLDS_OBJECT (json_array_get_element (a, k)) &&
				    !apply_one (json_array_get_object_element (a, k), error))
					return FALSE;
		}
	}
	return TRUE;
}

static gboolean
cmd_describe (gchar  **args,
              GError **error)
{
	if (args[0] != NULL && g_strcmp0 (canonical_resource (args[0]), "nodes") == 0 && args[1] != NULL)
	{
		g_autoptr(JsonObject) p = node_param (args[1]);
		g_autoptr(JsonNode)   n = NULL;

		json_object_set_boolean_member (p, "recursive", TRUE);
		n = call ("node.get", p, error);
		if (n == NULL)
			return FALSE;
		lrg_ctl_print (n, output == LRG_CTL_FORMAT_JSON ? output : LRG_CTL_FORMAT_YAML);
		return TRUE;
	}
	if (output == LRG_CTL_FORMAT_DEFAULT)
		output = LRG_CTL_FORMAT_YAML;
	return cmd_get (args, error);
}

/* ==========================================================================
 * Editor verbs
 * ========================================================================== */

static gboolean
cmd_select (gchar  **args,
            GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	JsonArray            *nodes = json_array_new ();
	guint                 i;

	if (opt_clear)
	{
		json_array_unref (nodes);
		return call_ok ("selection.clear", NULL, error);
	}
	if (args[0] == NULL)
	{
		json_array_unref (nodes);
		return usage_error (error, "select NAME... (or --clear)");
	}
	for (i = 0; args[i] != NULL; i++)
		json_array_add_string_element (nodes, args[i]);
	json_object_set_array_member (p, "nodes", nodes);
	json_object_set_boolean_member (p, "additive", opt_add);
	return call_ok ("selection.set", p, error);
}

static gboolean view_layers (gchar **args, GError **error);

static gboolean
cmd_view (gchar  **args,
          GError **error)
{
	static const gchar *aligned[][2] = {
		{ "top", "view.top" }, { "front", "view.front" }, { "right", "view.right" },
		{ "bottom", "view.bottom" }, { "back", "view.back" }, { "left", "view.left" },
		{ "persp", "view.perspective" }, { "ortho", "view.perspective" }, { "all", "view.frame-all" }
	};
	gsize i;

	if (args[0] == NULL)
	{
		g_autoptr(JsonNode) v = call ("view.get", NULL, error);

		if (v == NULL)
			return FALSE;
		lrg_ctl_print (v, output);
		return TRUE;
	}
	if (g_strcmp0 (args[0], "focus") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		if (args[1] != NULL)
			json_object_set_string_member (p, "node", args[1]);
		return call_ok ("view.focus", p, error);
	}
	if (g_strcmp0 (args[0], "set") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		if (!lrg_ctl_parse_assignments (args + 1, p, error))
			return FALSE;
		return call_ok ("view.set", p, error);
	}
	if (g_strcmp0 (args[0], "layout") == 0)
	{
		static const gchar * const keys[] = { "layout", NULL };

		return passthrough ("view.layout", keys, args + 1, error);
	}
	if (g_strcmp0 (args[0], "layers") == 0)
		return view_layers (args + 1, error);
	if (g_strcmp0 (args[0], "render-mode") == 0 || g_strcmp0 (args[0], "render_mode") == 0 ||
	    g_strcmp0 (args[0], "mode") == 0)
	{
		static const gchar * const keys[] = { "mode", NULL };

		return passthrough ("view.render_mode", keys, args + 1, error);
	}
	if (g_strcmp0 (args[0], "overlay") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;
		guint                 i;
		guint                 pos = 0;

		/* view overlay [NAME [on|off]] [viewport=N] */
		for (i = 1; args[i] != NULL; i++)
		{
			if (strchr (args[i], '=') != NULL)
			{
				gchar *one[] = { args[i], NULL };

				if (!lrg_ctl_parse_assignments (one, p, error))
					return FALSE;
			}
			else if (pos++ == 0)
				json_object_set_string_member (p, "name", args[i]);
			else
				json_object_set_boolean_member (p, "enabled", g_strcmp0 (args[i], "on") == 0 ||
				                                g_strcmp0 (args[i], "true") == 0 || g_strcmp0 (args[i], "1") == 0);
		}
		r = call ("view.overlay", p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	for (i = 0; i < G_N_ELEMENTS (aligned); i++)
		if (g_strcmp0 (args[0], aligned[i][0]) == 0)
		{
			g_autoptr(JsonObject) p = json_object_new ();

			json_object_set_string_member (p, "id", aligned[i][1]);
			return call_ok ("action.run", p, error);
		}
	return usage_error (error, "view top|front|right|bottom|back|left|persp|all, view focus NAME, view set key=value, "
	                    "view layout NAME, view render-mode MODE, view overlay NAME on|off, view layers [MASK|LAYER on|off]");
}

static gboolean
print_log_record (JsonNode *record)
{
	JsonObject       *o;
	g_autofree gchar *when = NULL;

	if (record == NULL || !JSON_NODE_HOLDS_OBJECT (record))
		return TRUE;
	if (output == LRG_CTL_FORMAT_JSON)
	{
		g_autofree gchar *line = lrg_ctl_to_json (record, FALSE);

		printf ("%s\n", line);
		fflush (stdout);
		return TRUE;
	}
	o = json_node_get_object (record);
	when = format_time (json_object_has_member (o, "time") ? json_object_get_int_member (o, "time") : 0);
	printf ("%s  %-8s %-10s %s\n", when, str_member (o, "level") != NULL ? str_member (o, "level") : "",
	        str_member (o, "domain") != NULL ? str_member (o, "domain") : "",
	        str_member (o, "message") != NULL ? str_member (o, "message") : "");
	fflush (stdout);
	return TRUE;
}

static gboolean
on_log_event (const gchar *event,
              JsonNode    *data,
              gpointer     user_data)
{
	return print_log_record (data);
}

static gboolean
cmd_logs (GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   list = NULL;
	static const gchar   *log_only[] = { "log", NULL };
	guint                 i;

	json_object_set_int_member (p, "count", MAX (opt_tail, 0));
	list = call ("log.tail", p, error);
	if (list == NULL)
		return FALSE;
	for (i = 0; i < json_array_get_length (json_node_get_array (list)); i++)
		print_log_record (json_array_get_element (json_node_get_array (list), i));
	if (!opt_watch)
		return TRUE;
	return lrg_ctl_follow_events (session_socket, log_only, on_log_event, NULL, error);
}

static gboolean
on_event (const gchar *event,
          JsonNode    *data,
          gpointer     user_data)
{
	g_autoptr(JsonNode) wrapped = json_node_new (JSON_NODE_OBJECT);
	JsonObject         *o = json_object_new ();

	json_object_set_string_member (o, "event", event);
	json_object_set_member (o, "data", data != NULL ? json_node_copy (data) : json_node_new (JSON_NODE_NULL));
	json_node_take_object (wrapped, o);

	if (output == LRG_CTL_FORMAT_YAML)
	{
		g_autofree gchar *text = lrg_ctl_to_yaml (wrapped);

		printf ("---\n%s", text);
	}
	else
	{
		g_autofree gchar *line = lrg_ctl_to_json (wrapped, FALSE);

		printf ("%s\n", line);
	}
	fflush (stdout);
	return TRUE;
}

static gboolean
cmd_events (gchar  **args,
            GError **error)
{
	g_autoptr(JsonNode) info = call ("editor.info", NULL, error);

	if (info == NULL)
		return FALSE;
	return lrg_ctl_follow_events (session_socket, (const gchar * const *) args, on_event, NULL, error);
}

static gboolean
cmd_call (gchar  **args,
          GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;

	if (args[0] == NULL)
		return usage_error (error, "call METHOD [key=value...|JSON]");
	if (args[1] != NULL && (args[1][0] == '{'))
	{
		g_autoptr(JsonParser) parser = json_parser_new ();

		if (!json_parser_load_from_data (parser, args[1], -1, error))
			return FALSE;
		if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
			return usage_error (error, "params must be a JSON object");
		p = json_object_ref (json_node_get_object (json_parser_get_root (parser)));
	}
	else
	{
		p = json_object_new ();
		if (!lrg_ctl_parse_assignments (args + 1, p, error))
			return FALSE;
	}
	r = call (args[0], p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output);
	return TRUE;
}

static gboolean
cmd_screenshot (gchar  **args,
                GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autofree gchar     *abs = NULL;
	gint                  tries;

	if (args[0] == NULL)
		return usage_error (error, "screenshot FILE.png [--window]");
	abs = g_canonicalize_filename (args[0], NULL);
	json_object_set_string_member (p, "path", abs);
	json_object_set_boolean_member (p, "window", opt_window);
	g_unlink (abs);
	if (!call_ok ("view.screenshot", p, error))
		return FALSE;
	/* Window captures are written after the editor's next frame. */
	for (tries = 0; tries < 60 && !g_file_test (abs, G_FILE_TEST_EXISTS); tries++)
		g_usleep (50000);
	if (!g_file_test (abs, G_FILE_TEST_EXISTS))
		return usage_error (error, "The editor did not write %s", abs);
	printf ("%s\n", abs);
	return TRUE;
}

static gboolean
simple_method (const gchar  *method,
               JsonObject   *params,
               const gchar  *done,
               GError      **error)
{
	g_autoptr(JsonNode) r = call (method, params, error);

	if (r == NULL)
		return FALSE;
	if (output == LRG_CTL_FORMAT_DEFAULT && done != NULL)
		printf ("%s\n", done);
	else if (output != LRG_CTL_FORMAT_DEFAULT)
		lrg_ctl_print (r, output);
	return TRUE;
}

/*
 * tiles NODE [paint|rect|fill|pick|configure key=value...]
 * Without an action, prints the grid (layer name and rows of tile ids).
 */
/* the M6/M7 tiles.* verbs (defined with the other M6/M7 families) */
static gboolean m67_has_verb (const gchar *family, const gchar *verb);
static gboolean m67_run_named (const gchar *family, gchar **args, GError **error);

static gboolean
cmd_tiles (gchar  **args,
           GError **error)
{
	static const gchar *actions[][2] = {
		{ "paint", "tilemap.paint" }, { "rect", "tilemap.rect" }, { "fill", "tilemap.fill" },
		{ "pick", "tilemap.pick" }, { "configure", "tilemap.configure" }
	};
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;
	const gchar          *method = "tilemap.get";
	gsize                 i;

	if (args[0] == NULL)
		return usage_error (error, "tiles NODE [paint|rect|fill|pick|configure key=value...]");
	/* the 0.9.0 rule-tile and grid verbs: tiles state|grid|brush|autopaint|... */
	if (m67_has_verb ("tiles", args[0]))
		return m67_run_named ("tiles", args, error);
	p = node_param (args[0]);
	if (args[1] != NULL)
	{
		for (i = 0; i < G_N_ELEMENTS (actions); i++)
			if (g_strcmp0 (args[1], actions[i][0]) == 0)
				method = actions[i][1];
		if (g_strcmp0 (method, "tilemap.get") == 0)
			return usage_error (error, "tiles NODE [paint|rect|fill|pick|configure key=value...]");
		if (!lrg_ctl_parse_assignments (args + 2, p, error))
			return FALSE;
	}
	r = call (method, p, error);
	if (r == NULL)
		return FALSE;
	if (output != LRG_CTL_FORMAT_DEFAULT || !JSON_NODE_HOLDS_OBJECT (r))
	{
		lrg_ctl_print (r, output);
		return TRUE;
	}

	/* Default: a readable grid, or the one-line result of an action. */
	if (g_strcmp0 (method, "tilemap.get") == 0 && json_object_has_member (json_node_get_object (r), "layers"))
	{
		JsonArray *layers = json_object_get_array_member (json_node_get_object (r), "layers");
		guint      l, y, x;

		if (json_array_get_length (layers) == 0)
			printf ("(no grid yet)\n");
		for (l = 0; l < json_array_get_length (layers); l++)
		{
			JsonObject *lo = json_array_get_object_element (layers, l);
			JsonArray  *rows = json_object_has_member (lo, "rows") ? json_object_get_array_member (lo, "rows") : NULL;

			printf ("%s (%" G_GINT64_FORMAT " tiles)%s\n", str_member (lo, "name"), json_object_get_int_member (lo, "count"),
			        json_object_get_boolean_member (lo, "visible") ? "" : " hidden");
			for (y = 0; rows != NULL && y < json_array_get_length (rows); y++)
			{
				JsonArray *row = json_array_get_array_element (rows, y);

				printf (" ");
				for (x = 0; x < json_array_get_length (row); x++)
					printf (" %3" G_GINT64_FORMAT, json_array_get_int_element (row, x));
				printf ("\n");
			}
		}
		return TRUE;
	}
	lrg_ctl_print (r, LRG_CTL_FORMAT_YAML);
	return TRUE;
}

/*
 * timeline [list] | get NAME | add NAME [key=value...] | rm NAME |
 * key NAME NODE key=value... | tangent NODE TIME MODE [key=value...] |
 * play [NAME] | stop | seek TIME [NAME] | state
 */
static gboolean
cmd_timeline (gchar  **args,
              GError **error)
{
	const gchar          *what = (args[0] != NULL) ? args[0] : "list";
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;
	const gchar          *method = NULL;

	if (g_strcmp0 (what, "list") == 0)
		method = "timeline.list";
	else if (g_strcmp0 (what, "state") == 0)
		method = "timeline.state";
	else if (g_strcmp0 (what, "stop") == 0)
		method = "timeline.stop";
	else if ((g_strcmp0 (what, "get") == 0 || g_strcmp0 (what, "rm") == 0 || g_strcmp0 (what, "add") == 0) &&
	         args[1] != NULL)
	{
		method = (what[0] == 'g') ? "timeline.get" : (what[0] == 'r') ? "timeline.remove" : "timeline.add";
		json_object_set_string_member (p, "name", args[1]);
		if (!lrg_ctl_parse_assignments (args + 2, p, error))
			return FALSE;
	}
	else if (g_strcmp0 (what, "key") == 0 && args[1] != NULL && args[2] != NULL)
	{
		method = "timeline.key";
		json_object_set_string_member (p, "name", args[1]);
		json_object_set_string_member (p, "node", args[2]);
		if (!lrg_ctl_parse_assignments (args + 3, p, error))
			return FALSE;
	}
	else if (g_strcmp0 (what, "tangent") == 0 && args[1] != NULL && args[2] != NULL && args[3] != NULL)
	{
		/* tangent NODE TIME MODE [name=TL] [channel=C] [slope=S | in=I out=O] */
		method = "timeline.tangent";
		json_object_set_string_member (p, "node", args[1]);
		json_object_set_double_member (p, "time", g_ascii_strtod (args[2], NULL));
		json_object_set_string_member (p, "tangent", args[3]);
		if (!lrg_ctl_parse_assignments (args + 4, p, error))
			return FALSE;
	}
	else if (g_strcmp0 (what, "play") == 0)
	{
		method = "timeline.play";
		if (args[1] != NULL)
			json_object_set_string_member (p, "name", args[1]);
	}
	else if (g_strcmp0 (what, "seek") == 0 && args[1] != NULL)
	{
		method = "timeline.seek";
		json_object_set_double_member (p, "time", g_ascii_strtod (args[1], NULL));
		if (args[2] != NULL)
			json_object_set_string_member (p, "name", args[2]);
	}
	if (method == NULL)
		return usage_error (error, "timeline [list|get NAME|add NAME|rm NAME|key NAME NODE key=value...|"
		                           "tangent NODE TIME MODE [name=TL] [channel=C] [slope=S]|"
		                           "play [NAME]|stop|seek TIME [NAME]|state]");

	r = call (method, p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/* thumbnail PATH: generates (if needed) and prints the cached PNG path. */
static gboolean
cmd_thumbnail (gchar  **args,
               GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;
	g_autofree gchar     *abs = NULL;

	if (args[0] == NULL)
		return usage_error (error, "thumbnail PATH");
	abs = g_canonicalize_filename (args[0], NULL);
	json_object_set_string_member (p, "path", g_file_test (abs, G_FILE_TEST_EXISTS) ? abs : args[0]);
	json_object_set_boolean_member (p, "wait", TRUE);
	r = call ("asset.thumbnail", p, error);
	if (r == NULL)
		return FALSE;
	if (output == LRG_CTL_FORMAT_DEFAULT)
		printf ("%s\n", str_member (json_node_get_object (r), "thumbnail"));
	else
		lrg_ctl_print (r, output);
	return TRUE;
}

/* ==========================================================================
 * 0.8.1 verbs: asset sidecars, git, linked prefabs, panels, code editor
 * ========================================================================== */

/* meta get PATH | meta set PATH key=value... [guid=GUID] */
static gboolean
cmd_meta (gchar  **args,
          GError **error)
{
	static const gchar * const keys[] = { "path", NULL };
	g_autoptr(JsonObject)       p = NULL;
	g_autoptr(JsonObject)       settings = NULL;
	g_autoptr(JsonNode)         r = NULL;
	g_autofree gchar           *path = NULL;
	guint                       i;

	if (g_strcmp0 (args[0], "get") == 0 && args[1] != NULL)
		return passthrough ("asset.meta.get", keys, args + 1, error);
	if (g_strcmp0 (args[0], "set") != 0 || args[1] == NULL)
		return usage_error (error, "meta get PATH | meta set PATH key=value... [guid=GUID]");

	/* Every key=value but guid is an importer setting (null resets it) */
	p = json_object_new ();
	settings = json_object_new ();
	path = path_arg (args[1]);
	json_object_set_string_member (p, "path", path);
	for (i = 2; args[i] != NULL; i++)
	{
		gchar *one[] = { args[i], NULL };

		if (g_str_has_prefix (args[i], "guid="))
			json_object_set_string_member (p, "guid", args[i] + 5);
		else if (!lrg_ctl_parse_assignments (one, settings, error))
			return FALSE;
	}
	if (json_object_get_size (settings) > 0)
		json_object_set_object_member (p, "settings", json_object_ref (settings));
	r = call ("asset.meta.set", p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/* lrgeditor next to this binary, else the one on PATH. */
static gchar *
find_lrgeditor (void)
{
	g_autofree gchar *dir = NULL;
	gchar            *beside;

	if (ctl_argv0 != NULL)
	{
		dir = g_path_get_dirname (ctl_argv0);
		beside = g_build_filename (dir, "lrgeditor", NULL);
		if (g_file_test (beside, G_FILE_TEST_IS_EXECUTABLE))
			return beside;
		g_free (beside);
	}
	return g_find_program_in_path ("lrgeditor");
}

/* Runs lrgeditor with @argv[1...] (argv[0] is replaced). Prints stderr. */
static gboolean
spawn_lrgeditor (gchar  **argv,
                 GError **error)
{
	g_autofree gchar *bin = find_lrgeditor ();
	g_autofree gchar *err_text = NULL;
	gint              status = 0;

	if (bin == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                     "lrgeditor is not installed");
		return FALSE;
	}
	argv[0] = bin;
	if (!g_spawn_sync (NULL, argv, NULL, 0, NULL, NULL, NULL, &err_text, &status, error))
		return FALSE;
	if (err_text != NULL && err_text[0] != '\0')
		fputs (err_text, stderr);
	return g_spawn_check_wait_status (status, error);
}

/* merge BASE OURS THEIRS | merge install [DIR] */
static gboolean
cmd_merge (gchar  **args,
           GError **error)
{
	gchar *argv[6];

	if (g_strcmp0 (args[0], "install") == 0)
	{
		if (args[1] != NULL && args[2] != NULL)
			return usage_error (error, "merge install [DIR]");
		argv[0] = NULL;
		argv[1] = (gchar *) "--merge-install";
		argv[2] = (args[1] != NULL) ? args[1] : (gchar *) ".";
		argv[3] = NULL;
		return spawn_lrgeditor (argv, error);
	}
	if (args[0] == NULL || args[1] == NULL || args[2] == NULL || args[3] != NULL)
		return usage_error (error, "merge BASE OURS THEIRS | merge install [DIR]");
	argv[0] = NULL;
	argv[1] = (gchar *) "--merge";
	argv[2] = args[0];
	argv[3] = args[1];
	argv[4] = args[2];
	argv[5] = NULL;
	return spawn_lrgeditor (argv, error);
}

static gboolean
cmd_po (gchar  **args,
        GError **error)
{
	static const gchar * const text_keys[] = { "text", NULL };
	static const gchar * const import_keys[] = { "path", "id", NULL };
	static const gchar * const export_keys[] = { "path", "id", "text", NULL };

	if (g_strcmp0 (args[0], "extract") == 0)
		return passthrough ("po.extract", text_keys, args + 1, error);
	if (g_strcmp0 (args[0], "pseudo") == 0)
		return passthrough ("po.pseudo", text_keys, args + 1, error);
	if (g_strcmp0 (args[0], "import") == 0)
		return passthrough ("po.import", import_keys, args + 1, error);
	if (g_strcmp0 (args[0], "export") == 0)
		return passthrough ("po.export", export_keys, args + 1, error);
	return usage_error (error, "po extract|pseudo TEXT | po import PATH ID | po export PATH ID TEXT");
}

static gboolean
cmd_collab (gchar  **args,
            GError **error)
{
	static const gchar * const apply_keys[] = { "user", "guid", "revision", "prop", "value", NULL };
	static const gchar * const get_keys[] = { "guid", "prop", NULL };
	static const gchar * const lock_keys[] = { "user", "guid", NULL };
	static const gchar * const cursor_keys[] = { "user", "x", "y", "z", NULL };

	if (g_strcmp0 (args[0], "apply") == 0)
		return passthrough ("collab.apply", apply_keys, args + 1, error);
	if (g_strcmp0 (args[0], "get") == 0)
		return passthrough ("collab.get", get_keys, args + 1, error);
	if (g_strcmp0 (args[0], "lock") == 0)
		return passthrough ("collab.lock", lock_keys, args + 1, error);
	if (g_strcmp0 (args[0], "unlock") == 0)
		return passthrough ("collab.unlock", lock_keys, args + 1, error);
	if (g_strcmp0 (args[0], "cursor") == 0)
		return passthrough ("collab.cursor", cursor_keys, args + 1, error);
	if (g_strcmp0 (args[0], "presence") == 0)
		return passthrough ("collab.presence", NULL, args + 1, error);
	return usage_error (error, "collab apply|get|lock|unlock|cursor|presence ...");
}

static gboolean
cmd_scenes (gchar  **args,
            GError **error)
{
	static const gchar * const path_keys[] = { "path", NULL };
	static const gchar * const index_keys[] = { "index", NULL };

	if (args[0] == NULL || g_strcmp0 (args[0], "list") == 0)
		return passthrough ("scenes.list", NULL, args[0] != NULL ? args + 1 : args, error);
	if (g_strcmp0 (args[0], "open") == 0)
		return passthrough ("scenes.open", path_keys, args + 1, error);
	if (g_strcmp0 (args[0], "active") == 0)
		return passthrough ("scenes.active", index_keys, args + 1, error);
	return usage_error (error, "scenes list | scenes open PATH | scenes active INDEX");
}

static gboolean
cmd_gizmo (gchar  **args,
           GError **error)
{
	static const gchar * const apply_keys[] = { "op", NULL };
	static const gchar * const pivot_keys[] = { "mode", NULL };
	static const gchar * const cursor_keys[] = { "x", "y", "z", NULL };

	if (g_strcmp0 (args[0], "apply") == 0)
		return passthrough ("gizmo.apply", apply_keys, args + 1, error);
	if (g_strcmp0 (args[0], "pivot") == 0)
		return passthrough ("gizmo.pivot", pivot_keys, args + 1, error);
	if (g_strcmp0 (args[0], "cursor") == 0)
		return passthrough ("gizmo.cursor", cursor_keys, args + 1, error);
	if (g_strcmp0 (args[0], "origin") == 0)
		return passthrough ("gizmo.origin", NULL, args + 1, error);
	return usage_error (error, "gizmo apply OP [axis=N] [value=N] | gizmo pivot MODE | gizmo cursor X Y Z | gizmo origin");
}

/* vcs [PATH]: the project's git status, or one path's badge.
 * vcs diff|log|locks|magit | vcs commit MESSAGE */
static gboolean
cmd_vcs (gchar  **args,
         GError **error)
{
	static const gchar * const keys[] = { "path", NULL };
	g_autoptr(JsonObject)       p = json_object_new ();
	g_autoptr(JsonNode)         r = NULL;
	static const LrgCtlColumn   columns[] = {
		{ "STATUS", "status", FALSE }, { "LETTER", "letter", TRUE }, { "PATH", "path", FALSE }
	};

	if (g_strcmp0 (args[0], "diff") == 0)
		return passthrough ("vcs.diff", NULL, args + 1, error);
	if (g_strcmp0 (args[0], "log") == 0)
		return passthrough ("vcs.log", NULL, args + 1, error);
	if (g_strcmp0 (args[0], "locks") == 0)
		return passthrough ("vcs.locks", NULL, args + 1, error);
	if (g_strcmp0 (args[0], "magit") == 0)
		return passthrough ("vcs.magit", NULL, args + 1, error);
	if (g_strcmp0 (args[0], "commit") == 0)
	{
		static const gchar * const message_keys[] = { "message", NULL };
		g_autofree gchar           *msg = NULL;
		gchar                      *one[2];

		if (args[1] == NULL)
			return usage_error (error, "vcs commit MESSAGE");
		msg = g_strjoinv (" ", args + 1);
		one[0] = msg;
		one[1] = NULL;
		return passthrough ("vcs.commit", message_keys, one, error);
	}
	if (args[0] != NULL)
		return passthrough ("vcs.status", keys, args, error);
	r = call ("vcs.status", p, error);
	if (r == NULL)
		return FALSE;
	if (output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_TABLE || output == LRG_CTL_FORMAT_WIDE)
	{
		JsonObject  *o = json_node_get_object (r);
		const gchar *branch = str_member (o, "branch");

		if (!json_object_get_boolean_member_with_default (o, "repository", FALSE))
		{
			printf ("not a git repository\n");
			return TRUE;
		}
		printf ("branch %s\n", branch != NULL ? branch : "?");
		if (json_object_has_member (o, "files"))
			lrg_ctl_print_table (json_object_get_array_member (o, "files"), columns, G_N_ELEMENTS (columns),
			                     output, "path");
		return TRUE;
	}
	lrg_ctl_print (r, output);
	return TRUE;
}

/* prefab overrides|apply|revert|relink|unlink NODE [keys=a,b] [path=P]
 * prefab sync [PATH] */
static gboolean
cmd_prefab (gchar  **args,
            GError **error)
{
	static const gchar * const node_keys[] = { "node", NULL };
	static const gchar * const what[] = { "overrides", "apply", "revert", "relink", "unlink", NULL };
	g_autoptr(JsonObject)       p = NULL;
	g_autoptr(JsonNode)         r = NULL;
	g_autofree gchar           *method = NULL;
	guint                       i;

	if (g_strcmp0 (args[0], "sync") == 0)
	{
		static const gchar * const path_keys[] = { "path", NULL };

		return passthrough ("prefab.sync", path_keys, args + 1, error);
	}
	if (args[0] == NULL || !g_strv_contains (what, args[0]) || args[1] == NULL)
		return usage_error (error, "prefab overrides|apply|revert|relink|unlink NODE [keys=a,b] [path=P] | "
		                    "prefab sync [PATH]");

	/* As passthrough, but keys=a,b becomes a list */
	p = json_object_new ();
	json_object_set_string_member (p, node_keys[0], args[1]);
	for (i = 2; args[i] != NULL; i++)
	{
		gchar *one[] = { args[i], NULL };

		if (g_str_has_prefix (args[i], "path="))
		{
			g_autofree gchar *path = path_arg (args[i] + 5);

			json_object_set_string_member (p, "path", path);
		}
		else if (!lrg_ctl_parse_assignments (one, p, error))
			return FALSE;
	}
	split_list_member (p, "keys");
	method = g_strconcat ("prefab.", args[0], NULL);
	r = call (method, p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/* code open PATH[:LINE] | code list | code save [PATH] */
static gboolean
cmd_code (gchar  **args,
          GError **error)
{
	static const gchar * const keys[] = { "path", NULL };
	static const LrgCtlColumn  columns[] = {
		{ "TAB", "tab", FALSE }, { "DIRTY", "dirty", FALSE }, { "LINE", "line", FALSE },
		{ "LANGUAGE", "language", TRUE }, { "PATH", "path", FALSE }
	};

	if (g_strcmp0 (args[0], "open") == 0 && args[1] != NULL)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;
		g_autofree gchar     *file = g_strdup (args[1]);
		g_autofree gchar     *path = NULL;
		gchar                *colon = strrchr (file, ':');
		gint64                line = 0;

		/* PATH:LINE, unless PATH itself exists with the colon */
		if (colon != NULL && !g_file_test (file, G_FILE_TEST_EXISTS) &&
		    g_ascii_string_to_signed (colon + 1, 10, 1, G_MAXINT, &line, NULL))
			*colon = '\0';
		path = path_arg (file);
		json_object_set_string_member (p, "path", path);
		if (line > 0)
			json_object_set_int_member (p, "line", line);
		if (args[2] != NULL)
		{
			gchar *one[] = { args[2], NULL };

			if (!lrg_ctl_parse_assignments (one, p, error))
				return FALSE;
		}
		r = call ("code.open", p, error);
		if (r == NULL)
			return FALSE;
		if (output == LRG_CTL_FORMAT_DEFAULT)
			printf ("opened %s:%" G_GINT64_FORMAT "\n", str_member (json_node_get_object (r), "path"),
			        json_object_get_int_member (json_node_get_object (r), "line"));
		else
			lrg_ctl_print (r, output);
		return TRUE;
	}
	if (g_strcmp0 (args[0], "list") == 0 || args[0] == NULL)
	{
		g_autoptr(JsonNode) r = call ("code.list", NULL, error);

		if (r == NULL)
			return FALSE;
		if (output == LRG_CTL_FORMAT_YAML || output == LRG_CTL_FORMAT_JSON)
			lrg_ctl_print (r, output);
		else
			lrg_ctl_print_table (json_node_get_array (r), columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}
	if (g_strcmp0 (args[0], "save") == 0)
		return passthrough ("code.save", keys, args + 1, error);
	return usage_error (error, "code open PATH[:LINE] | code list | code save [PATH]");
}

/* inspector list|get|set|reset|state|apply-override|revert-override
 * [NODE] [key=value...]: the inspector panel's view of a node */
static gboolean
cmd_inspector (gchar  **args,
               GError **error)
{
	static const gchar * const what[] = {
		"list", "get", "set", "reset", "state", "apply-override", "revert-override", NULL
	};
	static const gchar * const keys[] = { "node", "name", NULL };
	g_autofree gchar           *method = NULL;

	if (args[0] == NULL || !g_strv_contains (what, args[0]))
		return usage_error (error, "inspector list|get|set|reset|state|apply-override|revert-override [NODE] "
		                    "[NAME] [key=value...]");
	method = g_strconcat ("inspector.", args[0], NULL);
	g_strdelimit (method, "-", '_');
	return passthrough (method, keys, args + 1, error);
}

/* hierarchy reveal|rename NODE [name=NEW] | hierarchy filter TEXT |
 * hierarchy state */
static gboolean
cmd_hierarchy (gchar  **args,
               GError **error)
{
	static const gchar * const node_keys[] = { "node", "name", NULL };
	static const gchar * const text_keys[] = { "text", NULL };

	if ((g_strcmp0 (args[0], "reveal") == 0 || g_strcmp0 (args[0], "rename") == 0) && args[1] != NULL)
	{
		g_autofree gchar *method = g_strconcat ("hierarchy.", args[0], NULL);

		return passthrough (method, node_keys, args + 1, error);
	}
	if (g_strcmp0 (args[0], "filter") == 0)
		return passthrough ("hierarchy.filter", text_keys, args + 1, error);
	if (g_strcmp0 (args[0], "state") == 0 || args[0] == NULL)
		return passthrough ("hierarchy.state", NULL, args[0] != NULL ? args + 1 : args, error);
	return usage_error (error, "hierarchy reveal|rename NODE [NEW_NAME] | hierarchy filter TEXT | hierarchy state");
}

/* asset reveal|duplicate|trash PATH | asset rename PATH NAME |
 * asset move PATH DEST | asset orphans [trash=true] */
static gboolean
cmd_asset (gchar  **args,
           GError **error)
{
	static const gchar * const path_keys[] = { "path", NULL };
	static const gchar * const rename_keys[] = { "path", "name", NULL };
	static const gchar * const move_keys[] = { "path", "dest", NULL };
	const gchar * const        *keys = path_keys;
	g_autofree gchar           *method = NULL;

	if (g_strcmp0 (args[0], "orphans") == 0)
		return passthrough ("asset.orphans", NULL, args + 1, error);
	if (g_strcmp0 (args[0], "new") == 0)
	{
		static const gchar * const new_keys[] = { "kind", NULL };

		return passthrough ("asset.new", new_keys, args + 1, error);
	}
	if (args[0] == NULL || args[1] == NULL)
		return usage_error (error, "asset reveal|duplicate|trash PATH | asset rename PATH NAME | "
		                    "asset move PATH DEST | asset orphans [trash=true]");
	if (g_strcmp0 (args[0], "rename") == 0)
		keys = rename_keys;
	else if (g_strcmp0 (args[0], "move") == 0)
		keys = move_keys;
	else if (g_strcmp0 (args[0], "reveal") != 0 && g_strcmp0 (args[0], "duplicate") != 0 &&
	         g_strcmp0 (args[0], "trash") != 0)
		return usage_error (error, "Unknown asset command '%s'", args[0]);
	method = g_strconcat ("asset.", args[0], NULL);
	return passthrough (method, keys, args + 1, error);
}

/* ==========================================================================
 * 0.8.3 verbs: asset sources
 * ========================================================================== */

/*
 * source list | source ls SOURCE [FOLDER] | source search SOURCE PATTERN
 * [limit=N] | source export SOURCE PATH [DEST] | source import SOURCE
 * PATH | source place SOURCE PATH
 *
 * The source.* methods. Virtual paths are passed as they are (they never
 * name local files); DEST is a local folder, made absolute against the
 * current directory because the editor may run elsewhere. Listings print
 * as tables by default.
 */
static gboolean
cmd_source (gchar  **args,
            GError **error)
{
	static const LrgCtlColumn source_columns[] = {
		{ "ID", "id", FALSE }, { "TITLE", "title", FALSE }, { "SEARCH", "search", FALSE }
	};
	static const LrgCtlColumn entry_columns[] = {
		{ "NAME", "name", FALSE }, { "TYPE", "type", FALSE }, { "SIZE", "size", FALSE },
		{ "PATH", "path", TRUE }, { "DESCRIPTION", "description", TRUE }
	};
	static const gchar * const ls_keys[] = { "folder", NULL };
	static const gchar * const search_keys[] = { "pattern", NULL };
	static const gchar * const path_keys[] = { "path", NULL };
	static const gchar * const export_keys[] = { "path", "dest", NULL };
	const gchar * const        *keys = NULL;
	const gchar                *sub = args[0];
	g_autoptr(JsonObject)       p = json_object_new ();
	g_autoptr(GPtrArray)        assigns = g_ptr_array_new ();
	g_autoptr(JsonNode)         r = NULL;
	g_autofree gchar           *method = NULL;
	gboolean                    table;
	JsonObject                 *o;
	guint                       k = 0;
	guint                       i;

	if (g_strcmp0 (sub, "list") == 0 || sub == NULL)
	{
		r = call ("source.list", NULL, error);
		if (r == NULL)
			return FALSE;
		if ((output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_TABLE || output == LRG_CTL_FORMAT_WIDE) &&
		    JSON_NODE_HOLDS_ARRAY (r))
			lrg_ctl_print_table (json_node_get_array (r), source_columns, G_N_ELEMENTS (source_columns), output,
			                     "id");
		else
			lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}

	if (g_strcmp0 (sub, "ls") == 0)
		keys = ls_keys;
	else if (g_strcmp0 (sub, "search") == 0)
		keys = search_keys;
	else if (g_strcmp0 (sub, "import") == 0 || g_strcmp0 (sub, "place") == 0)
		keys = path_keys;
	else if (g_strcmp0 (sub, "export") == 0)
		keys = export_keys;
	if (keys == NULL || args[1] == NULL ||
	    (keys != ls_keys && (args[2] == NULL || strchr (args[2], '=') != NULL)))
		return usage_error (error, "source list | source ls SOURCE [FOLDER] | source search SOURCE PATTERN "
		                    "[limit=N] | source export SOURCE PATH [DEST] | source import|place SOURCE PATH");

	/* SOURCE, then the positionals in key order, then key=value */
	json_object_set_string_member (p, "source", args[1]);
	for (i = 2; args[i] != NULL; i++)
	{
		if (strchr (args[i], '=') == NULL && keys[k] != NULL)
		{
			if (g_strcmp0 (keys[k], "dest") == 0)
			{
				g_autofree gchar *abs = g_canonicalize_filename (args[i], NULL);

				json_object_set_string_member (p, keys[k], abs);
			}
			else
				json_object_set_string_member (p, keys[k], args[i]);
			k++;
		}
		else
			g_ptr_array_add (assigns, args[i]);
	}
	g_ptr_array_add (assigns, NULL);
	if (!lrg_ctl_parse_assignments ((gchar * const *) assigns->pdata, p, error))
		return FALSE;

	method = g_strconcat ("source.", sub, NULL);
	r = call (method, p, error);
	if (r == NULL)
		return FALSE;

	/* ls / search: a table of the entries unless -o asks otherwise */
	o = JSON_NODE_HOLDS_OBJECT (r) ? json_node_get_object (r) : NULL;
	table = (output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_TABLE || output == LRG_CTL_FORMAT_WIDE);
	if (table && o != NULL && json_object_has_member (o, "entries") &&
	    JSON_NODE_HOLDS_ARRAY (json_object_get_member (o, "entries")))
	{
		lrg_ctl_print_table (json_object_get_array_member (o, "entries"), entry_columns,
		                     G_N_ELEMENTS (entry_columns), output, "path");
		return TRUE;
	}
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/*
 * profiler [sample] [frames=N] [enable=true|false]: section timings;
 * profiler VERB ...: the generated profiler.* verbs (set, capture, ...)
 */
static gboolean
cmd_profiler (gchar  **args,
              GError **error)
{
	static const LrgCtlColumn columns[] = {
		{ "SECTION", "name", FALSE }, { "LAST_MS", "last_ms", FALSE }, { "AVG_MS", "avg_ms", FALSE },
		{ "MAX_MS", "max_ms", FALSE }, { "MIN_MS", "min_ms", TRUE }, { "COUNT", "count", TRUE }
	};
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;
	JsonObject           *o;

	/* profiler set|capture|save|load|frames|frame|compare|view: the 0.9.0 verbs */
	if (m67_has_verb ("profiler", args[0]))
		return m67_run_named ("profiler", args, error);
	if (g_strcmp0 (args[0], "sample") == 0)
		args++;
	if (!lrg_ctl_parse_assignments (args, p, error))
		return FALSE;
	r = call ("profiler.sample", p, error);
	if (r == NULL)
		return FALSE;
	o = JSON_NODE_HOLDS_OBJECT (r) ? json_node_get_object (r) : NULL;
	if ((output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_TABLE || output == LRG_CTL_FORMAT_WIDE) &&
	    o != NULL && json_object_has_member (o, "sections") &&
	    JSON_NODE_HOLDS_ARRAY (json_object_get_member (o, "sections")))
	{
		lrg_ctl_print_table (json_object_get_array_member (o, "sections"), columns, G_N_ELEMENTS (columns), output,
		                     "name");
		return TRUE;
	}
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/*
 * data_is_file_arg:
 * @arg: a positional argument of a `data` command
 *
 * Whether @arg names the data file rather than a YAML path. YAML paths
 * start with '/' (/actions/jump), and so do absolute file names; an
 * argument starting with '/' is a file only when it is a regular file
 * here. Anything else (data/input.yaml, relative to the project) is the
 * file.
 */
static gboolean
data_is_file_arg (const gchar *arg)
{
	if (arg[0] != '/')
		return TRUE;
	return g_file_test (arg, G_FILE_TEST_IS_REGULAR);
}

/*
 * data open PATH | data list | data get|set|edit|reveal [PATH] ... |
 * data undo|redo|validate|save|reload|close [PATH] [key=value...]
 *
 * The data editors (input maps, locales, sound banks, themes, HUD and UI
 * layouts, dialog trees, dungeons) over the data.* methods. The file is
 * optional everywhere but open: without it the focused data editor is
 * meant. The remaining positionals fill the command's keys in order:
 *   get    AT
 *   set    AT VALUE        (VALUE parsed like key=value values: JSON,
 *                           numbers, booleans, null, else a string)
 *   edit   OP AT           (op arguments as key=value)
 *   reveal AT
 * validate exits 1 when the document has errors, like `validate` does
 * for the level; close takes --force.
 */
static gboolean
cmd_data (gchar  **args,
          GError **error)
{
	static const gchar * const plain[] = {
		"open", "undo", "redo", "validate", "save", "reload", "close", NULL
	};
	static const gchar * const get_keys[] = { "at", NULL };
	static const gchar * const set_keys[] = { "at", "value", NULL };
	static const gchar * const edit_keys[] = { "op", "at", NULL };
	static const LrgCtlColumn  columns[] = {
		{ "FORMAT", "format", FALSE }, { "DIRTY", "dirty", FALSE }, { "ISSUES", "issues", FALSE },
		{ "ERRORS", "errors", FALSE }, { "EDITOR", "editor", TRUE }, { "UNDO", "undo_label", TRUE },
		{ "PATH", "path", FALSE }
	};
	const gchar * const       *keys = NULL;
	const gchar               *sub = args[0];
	g_autoptr(JsonObject)      p = json_object_new ();
	g_autoptr(GPtrArray)       assigns = g_ptr_array_new ();
	g_autoptr(JsonNode)        r = NULL;
	g_autofree gchar          *method = NULL;
	gboolean                   have_path = FALSE;
	guint                      k = 0;
	guint                      i;

	if (sub == NULL || g_strcmp0 (sub, "list") == 0)
	{
		r = call ("data.list", NULL, error);
		if (r == NULL)
			return FALSE;
		if (output == LRG_CTL_FORMAT_YAML || output == LRG_CTL_FORMAT_JSON)
			lrg_ctl_print (r, output);
		else
			lrg_ctl_print_table (json_node_get_array (r), columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}
	if (g_strcmp0 (sub, "get") == 0 || g_strcmp0 (sub, "reveal") == 0)
		keys = get_keys;
	else if (g_strcmp0 (sub, "set") == 0)
		keys = set_keys;
	else if (g_strcmp0 (sub, "edit") == 0)
		keys = edit_keys;
	else if (!g_strv_contains (plain, sub))
		return usage_error (error, "data open PATH | data list | data get [PATH] [AT] | data set [PATH] AT VALUE | "
		                    "data edit [PATH] OP AT [key=value...] | data reveal [PATH] AT | "
		                    "data undo|redo|validate|save|reload|close [PATH] [key=value...]");
	if (g_strcmp0 (sub, "open") == 0 && args[1] == NULL)
		return usage_error (error, "data open PATH");

	/* Positionals: the file first (when it looks like one), then the
	 * command's keys in order; key=value arguments pass straight through
	 * (save_as=, force=, index=, yaml=, as=, gesture=, ...). */
	for (i = 1; args[i] != NULL; i++)
	{
		gboolean assign = strchr (args[i], '=') != NULL;

		/* set's VALUE may itself contain '=' ("a=b"): once AT is filled,
		 * the next argument is the value whatever it holds */
		if (keys == set_keys && k == 1 && !json_object_has_member (p, "value"))
			assign = FALSE;
		if (assign)
		{
			g_ptr_array_add (assigns, args[i]);
			continue;
		}
		/* edit's first positional is the op unless it has a dot (op
		 * names never do; file names do) */
		if (!have_path && k == 0 && data_is_file_arg (args[i]) &&
		    (keys != edit_keys || strchr (args[i], '.') != NULL))
		{
			g_autofree gchar *path = path_arg (args[i]);

			json_object_set_string_member (p, "path", path);
			have_path = TRUE;
			continue;
		}
		if (keys == NULL || keys[k] == NULL)
			return usage_error (error, "Unexpected argument '%s' for data %s", args[i], sub);
		if (g_strcmp0 (keys[k], "value") == 0)
			json_object_set_member (p, "value", lrg_ctl_parse_value (args[i]));
		else
			json_object_set_string_member (p, keys[k], args[i]);
		k++;
	}
	g_ptr_array_add (assigns, NULL);
	if (!lrg_ctl_parse_assignments ((gchar * const *) assigns->pdata, p, error))
		return FALSE;
	if (g_strcmp0 (sub, "set") == 0 && !json_object_has_member (p, "value") && !json_object_has_member (p, "yaml"))
		return usage_error (error, "data set [PATH] AT VALUE (or yaml=TEXT)");
	if (g_strcmp0 (sub, "edit") == 0 && !json_object_has_member (p, "op"))
		return usage_error (error, "data edit [PATH] insert|move|add_key|rename_key|remove AT [key=value...]");
	if (g_strcmp0 (sub, "close") == 0 && opt_force)
		json_object_set_boolean_member (p, "force", TRUE);

	method = g_strconcat ("data.", sub, NULL);
	r = call (method, p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);

	/* Exit status 1 when validation finds errors (for scripts and CI). */
	if (g_strcmp0 (sub, "validate") == 0 && JSON_NODE_HOLDS_OBJECT (r) &&
	    json_object_has_member (json_node_get_object (r), "errors") &&
	    json_object_get_int_member (json_node_get_object (r), "errors") > 0)
	{
		g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "the data file has errors");
		return FALSE;
	}
	return TRUE;
}

/* palette [commands|files] [QUERY...]: open the command palette or the
 * quick-open file finder in the editor, pre-filled with QUERY */
static gboolean
cmd_palette (gchar  **args,
             GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;
	guint                 first = 0;

	if (g_strcmp0 (args[0], "commands") == 0 || g_strcmp0 (args[0], "files") == 0)
	{
		json_object_set_string_member (p, "mode", args[0]);
		first = 1;
	}
	if (args[first] != NULL)
	{
		g_autofree gchar *query = g_strjoinv (" ", args + first);

		json_object_set_string_member (p, "query", query);
	}
	r = call ("palette.open", p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/* preferences [PAGE]: open the preferences dialog (on PAGE) */
static gboolean
cmd_preferences (gchar  **args,
                 GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;

	if (args[0] != NULL)
		json_object_set_string_member (p, "page", args[0]);
	r = call ("preferences.open", p, error);
	if (r == NULL)
		return FALSE;
	if (output == LRG_CTL_FORMAT_DEFAULT && JSON_NODE_HOLDS_VALUE (r) &&
	    json_node_get_value_type (r) == G_TYPE_STRING)
		printf ("preferences/%s opened\n", json_node_get_string (r));
	else
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/* ==========================================================================
 * 0.9.0 rendering verbs: material, shader, shadergraph, env, post, lod
 * ========================================================================== */

/*
 * One sub-command of a verb family: "material get PATH" calls
 * material.get with {path}. @keys names the positional parameters in
 * order; a leading '@' marks a file path (sent through path_arg()), a
 * leading '~' a typed value (parsed like an assignment value: JSON,
 * numbers, booleans, null, number vectors; "on" / "off" are booleans).
 * Anything else is sent as a string. key=value arguments pass through.
 */
typedef struct
{
	const gchar *verb;
	const gchar *method;
	const gchar *keys[8];
} CtlVerb;

/* A typed positional value: on/off as booleans, else lrg_ctl_parse_value(). */
static JsonNode *
typed_value (const gchar *text)
{
	JsonNode *node;

	if (g_strcmp0 (text, "on") == 0 || g_strcmp0 (text, "off") == 0)
	{
		node = json_node_new (JSON_NODE_VALUE);
		json_node_set_boolean (node, text[1] == 'n');
		return node;
	}
	return lrg_ctl_parse_value (text);
}

/* The usage line of a family: "family verb KEY KEY | ...". */
static gchar *
family_usage (const gchar   *family,
              const CtlVerb *table,
              gsize          n)
{
	GString *s = g_string_new (NULL);
	gsize    i;
	guint    k;

	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append (s, " | ");
		g_string_append_printf (s, "%s %s", family, table[i].verb);
		for (k = 0; table[i].keys[k] != NULL; k++)
		{
			const gchar      *key = table[i].keys[k];
			g_autofree gchar *upper = NULL;

			if (*key == '@' || *key == '~')
				key++;
			upper = g_ascii_strup (key, -1);
			g_string_append_printf (s, " %s", upper);
		}
	}
	g_string_append (s, " [key=value...]");
	return g_string_free (s, FALSE);
}

/* Builds the params of @row from @args (positionals, then key=value). */
static JsonObject *
family_params (const CtlVerb  *row,
               gchar         **args,
               GError        **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	guint                 k = 0;
	guint                 i;

	for (i = 0; args != NULL && args[i] != NULL; i++)
	{
		const gchar *key = row->keys[k];

		if (strchr (args[i], '=') == NULL && key != NULL)
		{
			if (*key == '@')
			{
				g_autofree gchar *path = path_arg (args[i]);

				json_object_set_string_member (p, key + 1, path);
			}
			else if (*key == '~')
				json_object_set_member (p, key + 1, typed_value (args[i]));
			else
				json_object_set_string_member (p, key, args[i]);
			k++;
		}
		else
		{
			gchar *one[] = { args[i], NULL };

			if (!lrg_ctl_parse_assignments (one, p, error))
				return NULL;
		}
	}
	return g_steal_pointer (&p);
}

/* Runs "family VERB ..." from @table; the result prints as YAML by default. */
static gboolean
run_family (const gchar   *family,
            const CtlVerb *table,
            gsize          n,
            gchar        **args,
            GError       **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;
	gsize                 i;

	for (i = 0; args[0] != NULL && i < n; i++)
	{
		if (g_strcmp0 (args[0], table[i].verb) != 0)
			continue;
		p = family_params (&table[i], args + 1, error);
		if (p == NULL)
			return FALSE;
		r = call (table[i].method, p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	{
		g_autofree gchar *usage = family_usage (family, table, n);

		return usage_error (error, "%s", usage);
	}
}

/* material open|create|variant|get|set|revert|save|preview|assign|list */
static gboolean
cmd_material (gchar  **args,
              GError **error)
{
	static const CtlVerb table[] = {
		{ "open", "material.open", { "@path", NULL } },
		{ "create", "material.create", { "@path", NULL } },
		{ "variant", "material.create_variant", { "@path", "@dest", NULL } },
		{ "get", "material.get", { "@path", NULL } },
		{ "set", "material.set", { "@path", "key", "~value", NULL } },
		{ "revert", "material.revert", { "@path", "key", NULL } },
		{ "save", "material.save", { "@path", NULL } },
		{ "preview", "material.preview", { "@path", "shape", NULL } },
		{ "assign", "material.assign", { "node", "@path", NULL } },
		{ "list", "material.list", { NULL } },
	};

	return run_family ("material", table, G_N_ELEMENTS (table), args, error);
}

/* shader create|compile|diagnostics|uniforms|reload PATH */
static gboolean
cmd_shader (gchar  **args,
            GError **error)
{
	static const CtlVerb table[] = {
		{ "create", "shader.create", { "@path", NULL } },
		{ "compile", "shader.compile", { "@path", NULL } },
		{ "diagnostics", "shader.diagnostics", { "@path", NULL } },
		{ "uniforms", "shader.uniforms", { "@path", NULL } },
		{ "reload", "shader.reload", { "@path", NULL } },
	};

	return run_family ("shader", table, G_N_ELEMENTS (table), args, error);
}

/* shadergraph open|create|types|get|add|remove|connect|disconnect|param|input|validate|glsl|save */
static gboolean
cmd_shadergraph (gchar  **args,
                 GError **error)
{
	static const CtlVerb table[] = {
		{ "open", "shadergraph.open", { "@path", NULL } },
		{ "create", "shadergraph.create", { "@path", NULL } },
		{ "types", "shadergraph.node_types", { NULL } },
		{ "get", "shadergraph.get", { "@path", NULL } },
		{ "add", "shadergraph.add_node", { "@path", "type", NULL } },
		{ "remove", "shadergraph.remove_node", { "@path", "id", NULL } },
		{ "connect", "shadergraph.connect", { "@path", "from", "from_port", "to", "to_port", NULL } },
		{ "disconnect", "shadergraph.disconnect", { "@path", "to", "to_port", NULL } },
		{ "param", "shadergraph.set_param", { "@path", "id", "key", "~value", NULL } },
		{ "input", "shadergraph.set_input", { "@path", "id", "port", "~value", NULL } },
		{ "validate", "shadergraph.validate", { "@path", NULL } },
		{ "glsl", "shadergraph.glsl", { "@path", NULL } },
		{ "save", "shadergraph.save", { "@path", NULL } },
	};

	return run_family ("shadergraph", table, G_N_ELEMENTS (table), args, error);
}

/*
 * env bake --wait: start the bake, then poll environment.progress until
 * it is no longer baking (the bake runs over the editor's next frames).
 * Exits 1 when the editor reports an error.
 */
static gboolean
env_bake_wait (GError **error)
{
	g_autoptr(JsonNode) started = NULL;
	gint64              deadline;

	started = call ("environment.bake", NULL, error);
	if (started == NULL)
		return FALSE;
	/* A bake under software GL can take a while: wait up to ten minutes
	 * (or --timeout seconds when that is longer) */
	deadline = g_get_monotonic_time () + (gint64) MAX (opt_timeout, 600) * G_USEC_PER_SEC;
	while (g_get_monotonic_time () < deadline)
	{
		g_autoptr(JsonNode) r = call ("environment.progress", NULL, error);
		JsonObject         *o;

		if (r == NULL)
			return FALSE;
		o = JSON_NODE_HOLDS_OBJECT (r) ? json_node_get_object (r) : NULL;
		if (o == NULL || !json_object_get_boolean_member_with_default (o, "baking", FALSE))
		{
			const gchar *err = str_member (o, "error");

			if (err != NULL)
			{
				g_set_error (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "bake failed: %s", err);
				return FALSE;
			}
			lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
			return TRUE;
		}
		g_usleep (100 * 1000);
	}
	g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "timed out waiting for the bake");
	return FALSE;
}

/* env get|set KEY VALUE|reset|bake [--wait]|progress|show */
static gboolean
cmd_env (gchar  **args,
         GError **error)
{
	static const CtlVerb table[] = {
		{ "get", "environment.get", { NULL } },
		{ "set", "environment.set", { "key", "~value", NULL } },
		{ "reset", "environment.reset", { NULL } },
		{ "bake", "environment.bake", { NULL } },
		{ "progress", "environment.progress", { NULL } },
		{ "show", "environment.show", { NULL } },
	};

	if (args[0] == NULL)
	{
		gchar *get[] = { (gchar *) "get", NULL };

		return run_family ("env", table, G_N_ELEMENTS (table), get, error);
	}
	if (g_strcmp0 (args[0], "bake") == 0 && opt_wait)
		return env_bake_wait (error);
	return run_family ("env", table, G_N_ELEMENTS (table), args, error);
}

/* post types|open|create|get|add|remove|move|set|save|preview|look|assign */
static gboolean
cmd_post (gchar  **args,
          GError **error)
{
	static const CtlVerb table[] = {
		{ "types", "post.effect_types", { NULL } },
		{ "open", "post.open", { "@path", NULL } },
		{ "create", "post.create", { "@path", NULL } },
		{ "get", "post.get", { "@path", NULL } },
		{ "add", "post.add", { "@path", "type", NULL } },
		{ "remove", "post.remove", { "@path", "~index", NULL } },
		{ "move", "post.move", { "@path", "~from", "~to", NULL } },
		{ "set", "post.set", { "@path", "~index", "key", "~value", NULL } },
		{ "save", "post.save", { "@path", NULL } },
		{ "preview", "post.preview", { "@path", "~enabled", NULL } },
		{ "look", "post.save_look", { "@path", NULL } },
		{ "assign", "post.assign", { "@path", NULL } },
	};

	/* post preview on|off: no path (the open editor's stack, or off) */
	if (g_strcmp0 (args[0], "preview") == 0 &&
	    (g_strcmp0 (args[1], "on") == 0 || g_strcmp0 (args[1], "off") == 0))
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;

		json_object_set_boolean_member (p, "enabled", g_strcmp0 (args[1], "on") == 0);
		r = call ("post.preview", p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	return run_family ("post", table, G_N_ELEMENTS (table), args, error);
}

/* lod get NODE | lod force NODE LEVEL|auto */
static gboolean
cmd_lod (gchar  **args,
         GError **error)
{
	static const CtlVerb table[] = {
		{ "get", "lod.get", { "node", NULL } },
		{ "force", "lod.force", { "node", "~level", NULL } },
	};

	/* "auto" is -1 (a bare -1 would be read as an option) */
	if (g_strcmp0 (args[0], "force") == 0 && args[1] != NULL && g_strcmp0 (args[2], "auto") == 0)
	{
		gchar *fixed[] = { args[0], args[1], (gchar *) "-1", NULL };
		guint  i;

		for (i = 3; args[i] != NULL; i++)
			if (strchr (args[i], '=') != NULL)
				return usage_error (error, "lod force NODE auto takes no other arguments");
		return run_family ("lod", table, G_N_ELEMENTS (table), fixed, error);
	}
	return run_family ("lod", table, G_N_ELEMENTS (table), args, error);
}

/* ==========================================================================
 * 0.9.0 M3/M4 editor verbs: bake, seq, skel2d, anim, bonemap, clip
 * ========================================================================== */

/*
 * bake start --wait: start the lighting bake, then poll bake.progress
 * until it stops.  Exits 1 when the bake failed or was cancelled.
 */
static gboolean
bake_start_wait (gchar  **args,
                 GError **error)
{
	static const CtlVerb start = { "start", "bake.start", { NULL } };
	g_autoptr(JsonObject) p = family_params (&start, args, error);
	g_autoptr(JsonNode)   started = NULL;
	gint64                deadline;

	if (p == NULL)
		return FALSE;
	started = call ("bake.start", p, error);
	if (started == NULL)
		return FALSE;
	/* the baker runs on the CPU: large levels take minutes */
	deadline = g_get_monotonic_time () + (gint64) MAX (opt_timeout, 3600) * G_USEC_PER_SEC;
	while (g_get_monotonic_time () < deadline)
	{
		g_autoptr(JsonNode) r = call ("bake.progress", NULL, error);
		JsonObject         *o;

		if (r == NULL)
			return FALSE;
		o = JSON_NODE_HOLDS_OBJECT (r) ? json_node_get_object (r) : NULL;
		if (o == NULL || !json_object_get_boolean_member_with_default (o, "baking", FALSE))
		{
			const gchar *err = str_member (o, "error");
			const gchar *phase = str_member (o, "phase");

			if (err != NULL)
			{
				g_set_error (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "bake failed: %s", err);
				return FALSE;
			}
			if (g_strcmp0 (phase, "cancelled") == 0)
			{
				g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "bake cancelled");
				return FALSE;
			}
			{
				g_autoptr(JsonNode) state = call ("bake.get", NULL, error);

				if (state == NULL)
					return FALSE;
				lrg_ctl_print (state, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
			}
			return TRUE;
		}
		g_usleep (200 * 1000);
	}
	g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "timed out waiting for the bake");
	return FALSE;
}

/* "X Y Z" positionals as a JSON array member @key of @p. */
static gboolean
xyz_member (JsonObject   *p,
            const gchar  *key,
            gchar       **args,
            GError      **error)
{
	JsonArray *a = json_array_new ();
	guint      k;

	for (k = 0; k < 3; k++)
	{
		gchar   *end = NULL;
		gdouble  v;

		if (args[k] == NULL)
		{
			json_array_unref (a);
			return usage_error (error, "%s needs three numbers", key);
		}
		v = g_ascii_strtod (args[k], &end);
		if (end == args[k] || *end != '\0')
		{
			json_array_unref (a);
			return usage_error (error, "%s: '%s' is not a number", key, args[k]);
		}
		json_array_add_double_element (a, v);
	}
	json_object_set_array_member (p, key, a);
	return TRUE;
}

/*
 * bake [get] | set KEY VALUE | reset | start [--wait] [save=false] |
 * progress | cancel | save [DIR] | clear | evaluate [X Y Z [NX NY NZ]] |
 * preview on|off | page N | show
 */
static gboolean
cmd_bake (gchar  **args,
          GError **error)
{
	static const CtlVerb table[] = {
		{ "get", "bake.get", { NULL } },
		{ "set", "bake.set", { "key", "~value", NULL } },
		{ "start", "bake.start", { NULL } },
		{ "progress", "bake.progress", { NULL } },
		{ "cancel", "bake.cancel", { NULL } },
		{ "save", "bake.save", { "@dir", NULL } },
		{ "clear", "bake.clear", { NULL } },
		{ "evaluate", "bake.evaluate", { NULL } },
		{ "preview", "bake.preview", { "~show", NULL } },
		{ "page", "bake.preview", { "~page", NULL } },
		{ "show", "bake.show", { NULL } },
		{ "reset", "bake.set", { NULL } },
	};

	if (args[0] == NULL)
	{
		gchar *get[] = { (gchar *) "get", NULL };

		return run_family ("bake", table, G_N_ELEMENTS (table), get, error);
	}
	if (g_strcmp0 (args[0], "start") == 0 && opt_wait)
		return bake_start_wait (args + 1, error);
	if (g_strcmp0 (args[0], "reset") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;

		json_object_set_boolean_member (p, "reset", TRUE);
		r = call ("bake.set", p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	if (g_strcmp0 (args[0], "evaluate") == 0 && args[1] != NULL && strchr (args[1], '=') == NULL)
	{
		/* evaluate X Y Z [NX NY NZ]: a point (and normal); without
		 * numbers the surface under the viewport centre */
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;

		if (!xyz_member (p, "position", args + 1, error))
			return FALSE;
		if (args[4] != NULL && !xyz_member (p, "normal", args + 4, error))
			return FALSE;
		r = call ("bake.evaluate", p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	return run_family ("bake", table, G_N_ELEMENTS (table), args, error);
}

/*
 * seq list | open PATH | create PATH | get | set | add-track KIND |
 * add-key TRACK TIME [VALUE] | set-key TRACK | add-section TRACK START |
 * remove | bind NODE | play | scrub TIME | save | undo [redo=true] |
 * from-timeline TL PATH
 * (path=... picks a sequence other than the open one)
 */
static gboolean
cmd_seq (gchar  **args,
         GError **error)
{
	static const CtlVerb table[] = {
		{ "list", "sequence.list", { NULL } },
		{ "open", "sequence.open", { "@path", NULL } },
		{ "create", "sequence.create", { "@path", NULL } },
		{ "get", "sequence.get", { NULL } },
		{ "set", "sequence.set", { NULL } },
		{ "add-track", "sequence.add_track", { "kind", NULL } },
		{ "add-key", "sequence.add_key", { "~track", "~time", "~value", NULL } },
		{ "set-key", "sequence.set_key", { "~track", NULL } },
		{ "add-section", "sequence.add_section", { "~track", "~start", NULL } },
		{ "remove", "sequence.remove", { NULL } },
		{ "bind", "sequence.bind", { "node", NULL } },
		{ "play", "sequence.play", { NULL } },
		{ "scrub", "sequence.scrub", { "~time", NULL } },
		{ "save", "sequence.save", { NULL } },
		{ "undo", "sequence.undo", { NULL } },
		{ "from-timeline", "sequence.from_timeline", { "timeline", "@path", NULL } },
	};

	if (args[0] == NULL)
	{
		gchar *list[] = { (gchar *) "list", NULL };

		return run_family ("seq", table, G_N_ELEMENTS (table), list, error);
	}
	return run_family ("seq", table, G_N_ELEMENTS (table), args, error);
}

/* skel2d VERB PATH ...: the .rskel2d editor (every verb names the file) */
static gboolean
cmd_skel2d (gchar  **args,
            GError **error)
{
	static const CtlVerb table[] = {
		{ "create", "skel2d.create", { "@path", NULL } },
		{ "open", "skel2d.open", { "@path", NULL } },
		{ "get", "skel2d.get", { "@path", NULL } },
		{ "add-bone", "skel2d.add_bone", { "@path", "name", NULL } },
		{ "set-bone", "skel2d.set_bone", { "@path", "bone", NULL } },
		{ "remove-bone", "skel2d.remove_bone", { "@path", "bone", NULL } },
		{ "add-slot", "skel2d.add_slot", { "@path", "name", "bone", NULL } },
		{ "set-slot", "skel2d.set_slot", { "@path", "slot", NULL } },
		{ "remove-slot", "skel2d.remove_slot", { "@path", "slot", NULL } },
		{ "draw-order", "skel2d.draw_order", { "@path", "~order", NULL } },
		{ "attach", "skel2d.attach", { "@path", "slot", "name", NULL } },
		{ "detach", "skel2d.detach", { "@path", "slot", "name", NULL } },
		{ "mesh", "skel2d.mesh", { "@path", "slot", "name", NULL } },
		{ "auto-weights", "skel2d.auto_weights", { "@path", "slot", "name", NULL } },
		{ "paint-weights", "skel2d.paint_weights", { "@path", "slot", "name", "bone", "~x", "~y", NULL } },
		{ "skin", "skel2d.skin", { "@path", "name", "op", NULL } },
		{ "clip", "skel2d.clip", { "@path", "name", "op", NULL } },
		{ "key", "skel2d.key", { "@path", "target", "channel", "~time", NULL } },
		{ "play", "skel2d.play", { "@path", NULL } },
		{ "mode", "skel2d.mode", { "@path", "mode", NULL } },
		{ "pick", "skel2d.pick", { "@path", "~x", "~y", NULL } },
		{ "save", "skel2d.save", { "@path", NULL } },
	};

	return run_family ("skel2d", table, G_N_ELEMENTS (table), args, error);
}

/* anim VERB PATH ...: the .ranim animation graph editor */
static gboolean
cmd_anim (gchar  **args,
          GError **error)
{
	static const CtlVerb table[] = {
		{ "open", "anim.open", { "@path", NULL } },
		{ "create", "anim.create", { "@path", NULL } },
		{ "get", "anim.get", { "@path", NULL } },
		{ "enter", "anim.enter", { "@path", NULL } },
		{ "add-layer", "anim.add_layer", { "@path", "name", NULL } },
		{ "remove-layer", "anim.remove_layer", { "@path", "name", NULL } },
		{ "add-state", "anim.add_state", { "@path", "name", NULL } },
		{ "remove-state", "anim.remove_state", { "@path", "name", NULL } },
		{ "set-state", "anim.set_state", { "@path", "name", NULL } },
		{ "add-transition", "anim.add_transition", { "@path", "from", "to", NULL } },
		{ "set-transition", "anim.set_transition", { "@path", "from", "to", NULL } },
		{ "remove-transition", "anim.remove_transition", { "@path", "from", "to", NULL } },
		{ "add-param", "anim.add_parameter", { "@path", "name", "type", NULL } },
		{ "remove-param", "anim.remove_parameter", { "@path", "name", NULL } },
		{ "set-param", "anim.set_parameter", { "@path", "name", "~value", NULL } },
		{ "blend-tree", "anim.set_blend_tree", { "@path", "state", NULL } },
		{ "preview", "anim.preview", { "@path", NULL } },
		{ "validate", "anim.validate", { "@path", NULL } },
		{ "save", "anim.save", { "@path", NULL } },
	};

	return run_family ("anim", table, G_N_ELEMENTS (table), args, error);
}

/* bonemap VERB PATH ...: the .rbonemap retarget map editor */
static gboolean
cmd_bonemap (gchar  **args,
             GError **error)
{
	static const CtlVerb table[] = {
		{ "open", "bonemap.open", { "@path", NULL } },
		{ "create", "bonemap.create", { "@path", NULL } },
		{ "get", "bonemap.get", { "@path", NULL } },
		{ "set", "bonemap.set", { "@path", "source", "~target", NULL } },
		{ "auto-fill", "bonemap.auto_fill", { "@path", NULL } },
		{ "rules", "bonemap.set_rules", { "@path", NULL } },
		{ "models", "bonemap.set_models", { "@path", NULL } },
		{ "preview", "bonemap.preview", { "@path", NULL } },
		{ "save", "bonemap.save", { "@path", NULL } },
	};

	return run_family ("bonemap", table, G_N_ELEMENTS (table), args, error);
}

/* clip list|info|preview|morph MODEL ...: glTF clips and morph targets */
static gboolean
cmd_clip (gchar  **args,
          GError **error)
{
	static const CtlVerb table[] = {
		{ "list", "clip.list", { "@model", NULL } },
		{ "info", "clip.info", { "@model", "clip", NULL } },
		{ "preview", "clip.preview", { "@model", "clip", NULL } },
		{ "morph", "clip.set_morph", { "@model", "target", "~weight", NULL } },
	};

	return run_family ("clip", table, G_N_ELEMENTS (table), args, error);
}

/* ==========================================================================
 * 0.9.0 M5 physics verbs: physics (node physics blocks, project layers)
 * and physasset (.rphysasset ragdolls)
 * ========================================================================== */

/*
 * values_call:
 * Calls @method with params from @args: arguments without '=' fill @keys
 * in order (as in family_params: '@' a path, '~' a typed value);
 * key=value arguments whose key is in @top go to the top level, every
 * other one into a "values" object (the field = value maps of
 * physics.set_* and physasset.set_part). --node fills "node" when
 * @with_node and node= was not given.
 */
static gboolean
values_call (const gchar         *method,
             const gchar * const *keys,
             const gchar * const *top,
             gboolean             with_node,
             gchar              **args,
             JsonObject          *extra,
             GError             **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonObject) values = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;
	guint                 k = 0;
	guint                 i;

	for (i = 0; args != NULL && args[i] != NULL; i++)
	{
		const gchar *eq = strchr (args[i], '=');

		if (eq == NULL || eq == args[i])
		{
			const gchar *key = keys != NULL ? keys[k] : NULL;

			if (key == NULL)
				return usage_error (error, "%s: unexpected argument '%s'", method, args[i]);
			if (*key == '@')
			{
				g_autofree gchar *path = path_arg (args[i]);

				json_object_set_string_member (p, key + 1, path);
			}
			else if (*key == '~')
				json_object_set_member (p, key + 1, typed_value (args[i]));
			else
				json_object_set_string_member (p, key, args[i]);
			k++;
		}
		else
		{
			g_autofree gchar *key = g_strndup (args[i], eq - args[i]);

			if (top != NULL && g_strv_contains (top, key))
				json_object_set_member (p, key, lrg_ctl_parse_value (eq + 1));
			else
				json_object_set_member (values, key, lrg_ctl_parse_value (eq + 1));
		}
	}
	if (json_object_get_size (values) > 0)
		json_object_set_object_member (p, "values", json_object_ref (values));
	if (with_node && opt_node != NULL && !json_object_has_member (p, "node"))
		json_object_set_string_member (p, "node", opt_node);
	if (extra != NULL)
	{
		GList *members = json_object_get_members (extra);
		GList *l;

		for (l = members; l != NULL; l = l->next)
			if (!json_object_has_member (p, l->data))
				json_object_set_member (p, l->data, json_object_dup_member (extra, l->data));
		g_list_free (members);
	}
	r = call (method, p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/*
 * physics overlay [NAME..] [+NAME] [-NAME] [viewport=N]: no names reads
 * the flags; bare names set them ("all", "none" too), +NAME / -NAME turn
 * one on or off.
 */
static gboolean
physics_overlay (gchar  **args,
                 GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;
	JsonArray            *flags = json_array_new ();
	JsonArray            *on = json_array_new ();
	JsonArray            *off = json_array_new ();
	guint                 i;

	for (i = 0; args != NULL && args[i] != NULL; i++)
	{
		if (strchr (args[i], '=') != NULL)
		{
			gchar *one[] = { args[i], NULL };

			if (!lrg_ctl_parse_assignments (one, p, error))
			{
				json_array_unref (flags);
				json_array_unref (on);
				json_array_unref (off);
				return FALSE;
			}
		}
		else if (args[i][0] == '+')
			json_array_add_string_element (on, args[i] + 1);
		else if (args[i][0] == '-' && args[i][1] != '\0')
			json_array_add_string_element (off, args[i] + 1);
		else
			json_array_add_string_element (flags, args[i]);
	}
	if (json_array_get_length (flags) > 0)
		json_object_set_array_member (p, "flags", flags);
	else
		json_array_unref (flags);
	if (json_array_get_length (on) > 0)
		json_object_set_array_member (p, "enable", on);
	else
		json_array_unref (on);
	if (json_array_get_length (off) > 0)
		json_object_set_array_member (p, "disable", off);
	else
		json_array_unref (off);
	r = call ("physics.overlay", p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

/*
 * physics layers [get] | name I NAME|- | collide A B on|off |
 * settings [K=V..] | reset [empty=true] | open
 */
static gboolean
physics_layers (gchar  **args,
                GError **error)
{
	static const CtlVerb table[] = {
		{ "get", "physics.layers.get", { NULL } },
		{ "name", "physics.layers.set_name", { "~index", "name", NULL } },
		{ "collide", "physics.layers.set_collides", { "a", "b", "~collides", NULL } },
		{ "settings", "physics.layers.settings", { NULL } },
		{ "reset", "physics.layers.reset", { NULL } },
		{ "open", "physics.layers.open", { NULL } },
	};

	if (args[0] == NULL)
	{
		gchar *get[] = { (gchar *) "get", NULL };

		return run_family ("physics layers", table, G_N_ELEMENTS (table), get, error);
	}
	/* "name I -" (or no name) clears the slot */
	if (g_strcmp0 (args[0], "name") == 0 && args[1] != NULL && (args[2] == NULL || g_strcmp0 (args[2], "-") == 0))
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;

		json_object_set_member (p, "index", typed_value (args[1]));
		json_object_set_null_member (p, "name");
		r = call ("physics.layers.set_name", p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	/* layer names that look like numbers are indices */
	if (g_strcmp0 (args[0], "collide") == 0 && args[1] != NULL && args[2] != NULL && args[3] != NULL)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;
		guint                 k;

		for (k = 0; k < 2; k++)
		{
			gchar *end = NULL;
			gint64 v = g_ascii_strtoll (args[1 + k], &end, 10);

			if (end != args[1 + k] && *end == '\0')
				json_object_set_int_member (p, k == 0 ? "a" : "b", v);
			else
				json_object_set_string_member (p, k == 0 ? "a" : "b", args[1 + k]);
		}
		json_object_set_member (p, "collides", typed_value (args[3]));
		r = call ("physics.layers.set_collides", p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	return run_family ("physics layers", table, G_N_ELEMENTS (table), args, error);
}

/* physics VERB ... [--node N]: the node physics: block (see the usage) */
static gboolean
cmd_physics (gchar  **args,
             GError **error)
{
	static const gchar * const no_keys[] = { NULL };
	static const gchar * const kind_key[] = { "kind", NULL };
	static const gchar * const index_key[] = { "~index", NULL };
	static const gchar * const move_keys[] = { "~index", "~to", NULL };
	static const gchar * const node_top[] = { "node", NULL };
	static const gchar * const body_top[] = { "node", "kind", "shape", NULL };
	static const gchar * const shape_top[] = { "node", "kind", "fit", NULL };
	static const gchar * const joint_top[] = { "node", "kind", "target", NULL };
	static const gchar * const index_top[] = { "node", "index", NULL };
	static const gchar * const fit_top[] = { "node", "index", "kind", NULL };
	static const gchar * const move_top[] = { "node", "index", "to", NULL };
	const gchar               *verb = args[0];
	g_autoptr(JsonObject)      extra = json_object_new ();

	if (verb == NULL || g_strcmp0 (verb, "get") == 0)
		return values_call ("physics.get", no_keys, node_top, TRUE, verb != NULL ? args + 1 : args, NULL, error);
	if (g_strcmp0 (verb, "layers") == 0)
		return physics_layers (args + 1, error);
	if (g_strcmp0 (verb, "overlay") == 0)
		return physics_overlay (args + 1, error);
	if (opt_kind != NULL)
		json_object_set_string_member (extra, "kind", opt_kind);
	if (g_strcmp0 (verb, "state") == 0)
		return values_call ("physics.state", no_keys, node_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "show") == 0)
		return values_call ("physics.show", no_keys, NULL, FALSE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "add-body") == 0)
		return values_call ("physics.add_body", kind_key, body_top, TRUE, args + 1, extra, error);
	if (g_strcmp0 (verb, "remove-body") == 0)
		return values_call ("physics.remove_body", no_keys, node_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "set-body") == 0)
		return values_call ("physics.set_body", no_keys, node_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "add-shape") == 0)
		return values_call ("physics.add_shape", kind_key, shape_top, TRUE, args + 1, extra, error);
	if (g_strcmp0 (verb, "remove-shape") == 0)
		return values_call ("physics.remove_shape", index_key, index_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "set-shape") == 0)
		return values_call ("physics.set_shape", index_key, index_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "move-shape") == 0)
		return values_call ("physics.move_shape", move_keys, move_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "add-joint") == 0)
	{
		if (opt_target != NULL)
			json_object_set_string_member (extra, "target", opt_target);
		return values_call ("physics.add_joint", kind_key, joint_top, TRUE, args + 1, extra, error);
	}
	if (g_strcmp0 (verb, "remove-joint") == 0)
		return values_call ("physics.remove_joint", index_key, index_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "set-joint") == 0)
		return values_call ("physics.set_joint", index_key, index_top, TRUE, args + 1, NULL, error);
	if (g_strcmp0 (verb, "fit") == 0)
		return values_call ("physics.fit", index_key, fit_top, TRUE, args + 1, extra, error);
	return usage_error (error, "physics get|add-body|remove-body|set-body|add-shape|remove-shape|set-shape|move-shape"
	                           "|add-joint|remove-joint|set-joint|fit|overlay|state|show|layers ...");
}

/* physasset VERB PATH ...: the .rphysasset editor (every verb names the file) */
static gboolean
cmd_physasset (gchar  **args,
               GError **error)
{
	static const CtlVerb table[] = {
		{ "open", "physasset.open", { "@path", NULL } },
		{ "create", "physasset.create", { "@path", NULL } },
		{ "get", "physasset.get", { "@path", NULL } },
		{ "model", "physasset.set_model", { "@path", "model", NULL } },
		{ "remove-part", "physasset.remove_part", { "@path", "part", NULL } },
		{ "generate", "physasset.generate", { "@path", NULL } },
		{ "stabilize", "physasset.stabilize", { "@path", NULL } },
		{ "exclude", "physasset.exclude", { "@path", "a", "b", NULL } },
		{ "set", "physasset.set", { "@path", NULL } },
		{ "select", "physasset.select", { "@path", "part", NULL } },
		{ "simulate", "physasset.simulate", { "@path", NULL } },
		{ "save", "physasset.save", { "@path", NULL } },
	};
	static const gchar * const add_keys[] = { "@path", "bone", NULL };
	static const gchar * const set_keys[] = { "@path", "part", NULL };
	static const gchar * const path_top[] = { "path", NULL };

	if (args[0] != NULL && g_strcmp0 (args[0], "add-part") == 0)
		return values_call ("physasset.add_part", add_keys, path_top, FALSE, args + 1, NULL, error);
	if (args[0] != NULL && g_strcmp0 (args[0], "set-part") == 0)
		return values_call ("physasset.set_part", set_keys, path_top, FALSE, args + 1, NULL, error);
	if (args[0] != NULL && g_strcmp0 (args[0], "unexclude") == 0)
	{
		static const gchar * const excl_keys[] = { "@path", "a", "b", NULL };
		g_autoptr(JsonObject)      extra = json_object_new ();

		json_object_set_boolean_member (extra, "remove", TRUE);
		return values_call ("physasset.exclude", excl_keys, NULL, FALSE, args + 1, extra, error);
	}
	return run_family ("physasset", table, G_N_ELEMENTS (table), args, error);
}

/* ==========================================================================
 * 0.9.0 M6/M7 verbs: nav, bt, fsm, ai, particles, weather, terrain, water,
 * mixer, audio, tiles (rule tiles and grids), tileset and sprite
 *
 * One table row per IPC method (rows generated from the method lists the
 * tools publish; keep them in sync with docs/editor/ipc-protocol.org).
 * The verb is the method name after the family with '.' and '_' turned
 * into '-' (nav.area.box.add -> "nav area-box-add").
 * ========================================================================== */

/*
 * M67Verb:
 * @verb: the CLI verb
 * @method: the IPC method
 * @keys: positional parameters in order ('@' a file path, '~' a typed
 *   value, as in #CtlVerb)
 * @wrap: %NULL, or the object member that key=value arguments go into
 *   (nav.set "values", audio.set "params"), except the keys in @top
 * @top: key=value keys that stay top-level when @wrap is set
 * @node: --node fills "node" when node= was not given
 */
typedef struct
{
	const gchar *verb;
	const gchar *method;
	const gchar *keys[6];
	const gchar *wrap;
	const gchar *top[4];
	gboolean     node;
} M67Verb;

/* m67e-generated: begin */
/* nav get|set|new|bake|cancel|save|clear|area-add|area-remove|area-set|area-box-add|area-box-remove|area-box-set|link-add|link-remove|link-set|select|overlay|agents|debug-agent|show */
static const M67Verb m67_nav_verbs[] = {
	{ "get", "nav.get", { NULL }, NULL, { NULL }, FALSE },
	{ "set", "nav.set", { NULL }, "values", { "key", "value", "values", NULL }, FALSE },
	{ "new", "nav.new", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "bake", "nav.bake", { NULL }, NULL, { NULL }, FALSE },
	{ "cancel", "nav.cancel", { NULL }, NULL, { NULL }, FALSE },
	{ "save", "nav.save", { NULL }, NULL, { NULL }, FALSE },
	{ "clear", "nav.clear", { NULL }, NULL, { NULL }, FALSE },
	{ "area-add", "nav.area.add", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "area-remove", "nav.area.remove", { "~area", NULL }, NULL, { NULL }, FALSE },
	{ "area-set", "nav.area.set", { "~area", NULL }, NULL, { NULL }, FALSE },
	{ "area-box-add", "nav.area.box.add", { "~area", NULL }, NULL, { NULL }, FALSE },
	{ "area-box-remove", "nav.area.box.remove", { "~area", "~box", NULL }, NULL, { NULL }, FALSE },
	{ "area-box-set", "nav.area.box.set", { "~area", "~box", NULL }, NULL, { NULL }, FALSE },
	{ "link-add", "nav.link.add", { NULL }, NULL, { NULL }, FALSE },
	{ "link-remove", "nav.link.remove", { "~index", NULL }, NULL, { NULL }, FALSE },
	{ "link-set", "nav.link.set", { "~index", NULL }, NULL, { NULL }, FALSE },
	{ "select", "nav.select", { NULL }, NULL, { NULL }, FALSE },
	{ "overlay", "nav.overlay", { NULL }, NULL, { NULL }, FALSE },
	{ "agents", "nav.agents", { NULL }, NULL, { NULL }, FALSE },
	{ "debug-agent", "nav.debug_agent", { NULL }, NULL, { NULL }, TRUE },
	{ "show", "nav.show", { NULL }, NULL, { NULL }, FALSE },
};

/* bt open|create|get|palette|add-node|remove-node|move-node|set-node|set-position|layout|select|show|add-key|remove-key|set-subtree|remove-subtree|open-subtree|validate|save|live */
static const M67Verb m67_bt_verbs[] = {
	{ "open", "bt.open", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "create", "bt.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "bt.get", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "palette", "bt.palette", { NULL }, NULL, { NULL }, FALSE },
	{ "add-node", "bt.add_node", { "@path", "type", NULL }, NULL, { NULL }, FALSE },
	{ "remove-node", "bt.remove_node", { "@path", "id", NULL }, NULL, { NULL }, FALSE },
	{ "move-node", "bt.move_node", { "@path", "id", "parent", NULL }, NULL, { NULL }, FALSE },
	{ "set-node", "bt.set_node", { "@path", "id", NULL }, NULL, { NULL }, FALSE },
	{ "set-position", "bt.set_position", { "@path", "id", "~x", "~y", NULL }, NULL, { NULL }, FALSE },
	{ "layout", "bt.layout", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "select", "bt.select", { "@path", "~id", NULL }, NULL, { NULL }, FALSE },
	{ "show", "bt.show", { "@path", "~subtree", NULL }, NULL, { NULL }, FALSE },
	{ "add-key", "bt.add_key", { "@path", "name", "type", NULL }, NULL, { NULL }, FALSE },
	{ "remove-key", "bt.remove_key", { "@path", "name", NULL }, NULL, { NULL }, FALSE },
	{ "set-subtree", "bt.set_subtree", { "@path", "name", NULL }, NULL, { NULL }, FALSE },
	{ "remove-subtree", "bt.remove_subtree", { "@path", "name", NULL }, NULL, { NULL }, FALSE },
	{ "open-subtree", "bt.open_subtree", { "@path", "id", NULL }, NULL, { NULL }, FALSE },
	{ "validate", "bt.validate", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "save", "bt.save", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "live", "bt.live", { "@path", NULL }, NULL, { NULL }, FALSE },
};

/* fsm open|create|get|enter|up|add-state|remove-state|rename-state|set-initial|add-transition|set-transition|remove-transition|set-actions|set-position|select|add-key|remove-key|validate|save|live|send-event */
static const M67Verb m67_fsm_verbs[] = {
	{ "open", "fsm.open", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "create", "fsm.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "fsm.get", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "enter", "fsm.enter", { "@path", "state", NULL }, NULL, { NULL }, FALSE },
	{ "up", "fsm.up", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "add-state", "fsm.add_state", { "@path", "name", NULL }, NULL, { NULL }, FALSE },
	{ "remove-state", "fsm.remove_state", { "@path", "state", NULL }, NULL, { NULL }, FALSE },
	{ "rename-state", "fsm.rename_state", { "@path", "state", "name", NULL }, NULL, { NULL }, FALSE },
	{ "set-initial", "fsm.set_initial", { "@path", "state", NULL }, NULL, { NULL }, FALSE },
	{ "add-transition", "fsm.add_transition", { "@path", "from", "to", NULL }, NULL, { NULL }, FALSE },
	{ "set-transition", "fsm.set_transition", { "@path", "from", "~index", NULL }, NULL, { NULL }, FALSE },
	{ "remove-transition", "fsm.remove_transition", { "@path", "from", "~index", NULL }, NULL, { NULL }, FALSE },
	{ "set-actions", "fsm.set_actions", { "@path", "state", "phase", NULL }, NULL, { NULL }, FALSE },
	{ "set-position", "fsm.set_position", { "@path", "state", "~x", "~y", NULL }, NULL, { NULL }, FALSE },
	{ "select", "fsm.select", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "add-key", "fsm.add_key", { "@path", "name", "type", NULL }, NULL, { NULL }, FALSE },
	{ "remove-key", "fsm.remove_key", { "@path", "name", NULL }, NULL, { NULL }, FALSE },
	{ "validate", "fsm.validate", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "save", "fsm.save", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "live", "fsm.live", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "send-event", "fsm.send_event", { "@path", "event", NULL }, NULL, { NULL }, FALSE },
};

/* ai show|entities|select|state|set-key|send-event|brains|topic|log */
static const M67Verb m67_ai_verbs[] = {
	{ "show", "ai.show", { NULL }, NULL, { NULL }, FALSE },
	{ "entities", "ai.entities", { NULL }, NULL, { NULL }, FALSE },
	{ "select", "ai.select", { "~entity", NULL }, NULL, { NULL }, FALSE },
	{ "state", "ai.state", { NULL }, NULL, { NULL }, FALSE },
	{ "set-key", "ai.set_key", { "key", "~value", NULL }, NULL, { NULL }, FALSE },
	{ "send-event", "ai.send_event", { "event", NULL }, NULL, { NULL }, FALSE },
	{ "brains", "ai.brains", { NULL }, NULL, { NULL }, FALSE },
	{ "topic", "ai.topic", { "brain", "topic", NULL }, NULL, { NULL }, FALSE },
	{ "log", "ai.log", { NULL }, NULL, { NULL }, FALSE },
};

/* particles open|create|get|kinds|set|add-emitter|remove-emitter|move-emitter|duplicate-emitter|add-module|remove-module|move-module|select|solo|play|restart|seek|preview|stats|save|render */
static const M67Verb m67_particles_verbs[] = {
	{ "open", "particles.open", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "create", "particles.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "particles.get", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "kinds", "particles.kinds", { NULL }, NULL, { NULL }, FALSE },
	{ "set", "particles.set", { "@path", "key", "~value", NULL }, NULL, { NULL }, FALSE },
	{ "add-emitter", "particles.add_emitter", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "remove-emitter", "particles.remove_emitter", { "@path", "~emitter", NULL }, NULL, { NULL }, FALSE },
	{ "move-emitter", "particles.move_emitter", { "@path", "~from", "~to", NULL }, NULL, { NULL }, FALSE },
	{ "duplicate-emitter", "particles.duplicate_emitter", { "@path", "~emitter", NULL }, NULL, { NULL }, FALSE },
	{ "add-module", "particles.add_module", { "@path", "~emitter", "kind", NULL }, NULL, { NULL }, FALSE },
	{ "remove-module", "particles.remove_module", { "@path", "~emitter", "~module", NULL }, NULL, { NULL }, FALSE },
	{ "move-module", "particles.move_module", { "@path", "~emitter", "~from", "~to", NULL }, NULL, { NULL }, FALSE },
	{ "select", "particles.select", { "@path", "~emitter", NULL }, NULL, { NULL }, FALSE },
	{ "solo", "particles.solo", { "@path", "~emitter", NULL }, NULL, { NULL }, FALSE },
	{ "play", "particles.play", { "@path", "~playing", NULL }, NULL, { NULL }, FALSE },
	{ "restart", "particles.restart", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "seek", "particles.seek", { "@path", "~time", NULL }, NULL, { NULL }, FALSE },
	{ "preview", "particles.preview", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "stats", "particles.stats", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "save", "particles.save", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "render", "particles.render", { "@path", "file", NULL }, NULL, { NULL }, FALSE },
};

/* weather open|create|get|kinds|set|add-preset|remove-preset|set-preset|add-layer|remove-layer|move-layer|set-layer|set-param|set-transition|remove-transition|select|preview|save|render */
static const M67Verb m67_weather_verbs[] = {
	{ "open", "weather.open", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "create", "weather.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "weather.get", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "kinds", "weather.kinds", { NULL }, NULL, { NULL }, FALSE },
	{ "set", "weather.set", { "@path", "key", "~value", NULL }, NULL, { NULL }, FALSE },
	{ "add-preset", "weather.add_preset", { "@path", "id", NULL }, NULL, { NULL }, FALSE },
	{ "remove-preset", "weather.remove_preset", { "@path", "~preset", NULL }, NULL, { NULL }, FALSE },
	{ "set-preset", "weather.set_preset", { "@path", "~preset", NULL }, NULL, { NULL }, FALSE },
	{ "add-layer", "weather.add_layer", { "@path", "~preset", "kind", NULL }, NULL, { NULL }, FALSE },
	{ "remove-layer", "weather.remove_layer", { "@path", "~preset", "~layer", NULL }, NULL, { NULL }, FALSE },
	{ "move-layer", "weather.move_layer", { "@path", "~preset", "~from", "~to", NULL }, NULL, { NULL }, FALSE },
	{ "set-layer", "weather.set_layer", { "@path", "~preset", "~layer", NULL }, NULL, { NULL }, FALSE },
	{ "set-param", "weather.set_param", { "@path", "~preset", "~layer", "name", "~value", NULL }, NULL, { NULL }, FALSE },
	{ "set-transition", "weather.set_transition", { "@path", "from", "to", "~duration", NULL }, NULL, { NULL }, FALSE },
	{ "remove-transition", "weather.remove_transition", { "@path", "from", "to", NULL }, NULL, { NULL }, FALSE },
	{ "select", "weather.select", { "@path", "~preset", NULL }, NULL, { NULL }, FALSE },
	{ "preview", "weather.preview", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "save", "weather.save", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "render", "weather.render", { "@path", "file", NULL }, NULL, { NULL }, FALSE },
};

/* terrain get|brush|stroke|sample|add-layer|remove-layer|set-layer|add-foliage|remove-foliage|set-foliage|rebake|save|create|show */
static const M67Verb m67_terrain_verbs[] = {
	{ "get", "terrain.get", { NULL }, NULL, { NULL }, TRUE },
	{ "brush", "terrain.brush", { NULL }, NULL, { NULL }, FALSE },
	{ "stroke", "terrain.stroke", { NULL }, NULL, { NULL }, TRUE },
	{ "sample", "terrain.sample", { "~x", "~z", NULL }, NULL, { NULL }, TRUE },
	{ "add-layer", "terrain.add_layer", { NULL }, NULL, { NULL }, TRUE },
	{ "remove-layer", "terrain.remove_layer", { "~index", NULL }, NULL, { NULL }, TRUE },
	{ "set-layer", "terrain.set_layer", { "~index", NULL }, NULL, { NULL }, TRUE },
	{ "add-foliage", "terrain.add_foliage", { NULL }, NULL, { NULL }, TRUE },
	{ "remove-foliage", "terrain.remove_foliage", { "~index", NULL }, NULL, { NULL }, TRUE },
	{ "set-foliage", "terrain.set_foliage", { "~index", NULL }, NULL, { NULL }, TRUE },
	{ "rebake", "terrain.rebake", { NULL }, NULL, { NULL }, TRUE },
	{ "save", "terrain.save", { NULL }, NULL, { NULL }, TRUE },
	{ "create", "terrain.create", { NULL }, NULL, { NULL }, FALSE },
	{ "show", "terrain.show", { NULL }, NULL, { NULL }, FALSE },
};

/* water get|set|set-wave|add-point|set-point|remove-point|save */
static const M67Verb m67_water_verbs[] = {
	{ "get", "water.get", { NULL }, NULL, { NULL }, TRUE },
	{ "set", "water.set", { NULL }, NULL, { NULL }, TRUE },
	{ "set-wave", "water.set_wave", { NULL }, NULL, { NULL }, TRUE },
	{ "add-point", "water.add_point", { "~x", "~z", NULL }, NULL, { NULL }, TRUE },
	{ "set-point", "water.set_point", { "~index", NULL }, NULL, { NULL }, TRUE },
	{ "remove-point", "water.remove_point", { "~index", NULL }, NULL, { NULL }, TRUE },
	{ "save", "water.save", { NULL }, NULL, { NULL }, TRUE },
};

/* mixer show|open|create|set-default|get|effect-types|add-bus|remove-bus|set-bus|set-send|remove-send|add-effect|remove-effect|move-effect|set-effect|add-snapshot|remove-snapshot|set-snapshot|capture-snapshot|set-duck|remove-duck|preview|meters|save */
static const M67Verb m67_mixer_verbs[] = {
	{ "show", "mixer.show", { NULL }, NULL, { NULL }, FALSE },
	{ "open", "mixer.open", { NULL }, NULL, { NULL }, FALSE },
	{ "create", "mixer.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "set-default", "mixer.set_default", { "~path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "mixer.get", { NULL }, NULL, { NULL }, FALSE },
	{ "effect-types", "mixer.effect_types", { NULL }, NULL, { NULL }, FALSE },
	{ "add-bus", "mixer.add_bus", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "remove-bus", "mixer.remove_bus", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "set-bus", "mixer.set_bus", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "set-send", "mixer.set_send", { "source", "target", "~level", NULL }, NULL, { NULL }, FALSE },
	{ "remove-send", "mixer.remove_send", { "source", "target", NULL }, NULL, { NULL }, FALSE },
	{ "add-effect", "mixer.add_effect", { "bus", "type", NULL }, NULL, { NULL }, FALSE },
	{ "remove-effect", "mixer.remove_effect", { "bus", "id", NULL }, NULL, { NULL }, FALSE },
	{ "move-effect", "mixer.move_effect", { "bus", "id", "~index", NULL }, NULL, { NULL }, FALSE },
	{ "set-effect", "mixer.set_effect", { "bus", "id", NULL }, NULL, { NULL }, FALSE },
	{ "add-snapshot", "mixer.add_snapshot", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "remove-snapshot", "mixer.remove_snapshot", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "set-snapshot", "mixer.set_snapshot", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "capture-snapshot", "mixer.capture_snapshot", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "set-duck", "mixer.set_duck", { "trigger", "target", NULL }, NULL, { NULL }, FALSE },
	{ "remove-duck", "mixer.remove_duck", { "trigger", "target", NULL }, NULL, { NULL }, FALSE },
	{ "preview", "mixer.preview", { NULL }, NULL, { NULL }, FALSE },
	{ "meters", "mixer.meters", { NULL }, NULL, { NULL }, FALSE },
	{ "save", "mixer.save", { NULL }, NULL, { NULL }, FALSE },
};

/* audio get|set|curve|buses */
static const M67Verb m67_audio_verbs[] = {
	{ "get", "audio.get", { "node", NULL }, NULL, { NULL }, TRUE },
	{ "set", "audio.set", { "node", NULL }, "params", { "node", "params", NULL }, TRUE },
	{ "curve", "audio.curve", { "node", NULL }, NULL, { NULL }, TRUE },
	{ "buses", "audio.buses", { NULL }, NULL, { NULL }, FALSE },
};

/* tiles state|grid|brush|autopaint|refresh|cell-at|tile-info */
static const M67Verb m67_tiles_verbs[] = {
	{ "state", "tiles.get", { NULL }, NULL, { NULL }, TRUE },
	{ "grid", "tiles.grid", { NULL }, NULL, { NULL }, TRUE },
	{ "brush", "tiles.brush", { NULL }, NULL, { NULL }, FALSE },
	{ "autopaint", "tiles.paint", { NULL }, NULL, { NULL }, TRUE },
	{ "refresh", "tiles.refresh", { NULL }, NULL, { NULL }, TRUE },
	{ "cell-at", "tiles.cell_at", { NULL }, NULL, { NULL }, TRUE },
	{ "tile-info", "tiles.tile_info", { "~tile", NULL }, NULL, { NULL }, TRUE },
};

/* tileset create|open|get|set|select|tile|collision|rule-tile|rule|preview|save */
static const M67Verb m67_tileset_verbs[] = {
	{ "create", "tileset.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "open", "tileset.open", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "tileset.get", { NULL }, NULL, { NULL }, FALSE },
	{ "set", "tileset.set", { NULL }, NULL, { NULL }, FALSE },
	{ "select", "tileset.select", { NULL }, NULL, { NULL }, FALSE },
	{ "tile", "tileset.tile", { "~tile", NULL }, NULL, { NULL }, FALSE },
	{ "collision", "tileset.collision", { "~tile", "op", NULL }, NULL, { NULL }, FALSE },
	{ "rule-tile", "tileset.rule_tile", { "op", "name", NULL }, NULL, { NULL }, FALSE },
	{ "rule", "tileset.rule", { "rule_tile", "op", NULL }, NULL, { NULL }, FALSE },
	{ "preview", "tileset.preview", { "rule_tile", NULL }, NULL, { NULL }, FALSE },
	{ "save", "tileset.save", { NULL }, NULL, { NULL }, FALSE },
};

/* sprite create|open|get|slice|frame|clip|clip-frame|event|play|save */
static const M67Verb m67_sprite_verbs[] = {
	{ "create", "sprite.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "open", "sprite.open", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "sprite.get", { NULL }, NULL, { NULL }, FALSE },
	{ "slice", "sprite.slice", { "mode", NULL }, NULL, { NULL }, FALSE },
	{ "frame", "sprite.frame", { "op", NULL }, NULL, { NULL }, FALSE },
	{ "clip", "sprite.clip", { "op", "name", NULL }, NULL, { NULL }, FALSE },
	{ "clip-frame", "sprite.clip_frame", { "clip", "op", NULL }, NULL, { NULL }, FALSE },
	{ "event", "sprite.event", { "clip", "op", "~frame", NULL }, NULL, { NULL }, FALSE },
	{ "play", "sprite.play", { NULL }, NULL, { NULL }, FALSE },
	{ "save", "sprite.save", { NULL }, NULL, { NULL }, FALSE },
};

/* debug state|show|breakpoints|set-breakpoint|remove-breakpoint|toggle-breakpoint|clear-breakpoints|continue|pause|step-over|step-in|step-out|stop|stack|scopes|variables|evaluate|watches|add-watch|remove-watch|dap-start|dap-stop */
static const M67Verb m67_debug_verbs[] = {
	{ "state", "debug.state", { NULL }, NULL, { NULL }, FALSE },
	{ "show", "debug.show", { NULL }, NULL, { NULL }, FALSE },
	{ "breakpoints", "debug.breakpoints", { NULL }, NULL, { NULL }, FALSE },
	{ "set-breakpoint", "debug.set_breakpoint", { "@path", "~line", NULL }, NULL, { NULL }, FALSE },
	{ "remove-breakpoint", "debug.remove_breakpoint", { "@path", "~line", NULL }, NULL, { NULL }, FALSE },
	{ "toggle-breakpoint", "debug.toggle_breakpoint", { "~line", NULL }, NULL, { NULL }, FALSE },
	{ "clear-breakpoints", "debug.clear_breakpoints", { NULL }, NULL, { NULL }, FALSE },
	{ "continue", "debug.continue", { NULL }, NULL, { NULL }, FALSE },
	{ "pause", "debug.pause", { NULL }, NULL, { NULL }, FALSE },
	{ "step-over", "debug.step_over", { NULL }, NULL, { NULL }, FALSE },
	{ "step-in", "debug.step_in", { NULL }, NULL, { NULL }, FALSE },
	{ "step-out", "debug.step_out", { NULL }, NULL, { NULL }, FALSE },
	{ "stop", "debug.stop", { NULL }, NULL, { NULL }, FALSE },
	{ "stack", "debug.stack", { NULL }, NULL, { NULL }, FALSE },
	{ "scopes", "debug.scopes", { NULL }, NULL, { NULL }, FALSE },
	{ "variables", "debug.variables", { "~reference", NULL }, NULL, { NULL }, FALSE },
	{ "evaluate", "debug.evaluate", { "expression", NULL }, NULL, { NULL }, FALSE },
	{ "watches", "debug.watches", { NULL }, NULL, { NULL }, FALSE },
	{ "add-watch", "debug.add_watch", { "expression", NULL }, NULL, { NULL }, FALSE },
	{ "remove-watch", "debug.remove_watch", { "expression", NULL }, NULL, { NULL }, FALSE },
	{ "dap-start", "debug.dap_start", { NULL }, NULL, { NULL }, FALSE },
	{ "dap-stop", "debug.dap_stop", { NULL }, NULL, { NULL }, FALSE },
};

/* lsp servers|status|diagnostics|complete|hover|definition|references|rename|format|restart */
static const M67Verb m67_lsp_verbs[] = {
	{ "servers", "lsp.servers", { NULL }, NULL, { NULL }, FALSE },
	{ "status", "lsp.status", { NULL }, NULL, { NULL }, FALSE },
	{ "diagnostics", "lsp.diagnostics", { NULL }, NULL, { NULL }, FALSE },
	{ "complete", "lsp.complete", { NULL }, NULL, { NULL }, FALSE },
	{ "hover", "lsp.hover", { NULL }, NULL, { NULL }, FALSE },
	{ "definition", "lsp.definition", { NULL }, NULL, { NULL }, FALSE },
	{ "references", "lsp.references", { NULL }, NULL, { NULL }, FALSE },
	{ "rename", "lsp.rename", { "new_name", NULL }, NULL, { NULL }, FALSE },
	{ "format", "lsp.format", { NULL }, NULL, { NULL }, FALSE },
	{ "restart", "lsp.restart", { NULL }, NULL, { NULL }, FALSE },
};

/* profiler set|capture|save|load|frames|frame|compare|view */
static const M67Verb m67_profiler_verbs[] = {
	{ "set", "profiler.set", { NULL }, NULL, { NULL }, FALSE },
	{ "capture", "profiler.capture", { NULL }, NULL, { NULL }, FALSE },
	{ "save", "profiler.save", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "load", "profiler.load", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "frames", "profiler.frames", { NULL }, NULL, { NULL }, FALSE },
	{ "frame", "profiler.frame", { NULL }, NULL, { NULL }, FALSE },
	{ "compare", "profiler.compare", { NULL }, NULL, { NULL }, FALSE },
	{ "view", "profiler.view", { NULL }, NULL, { NULL }, FALSE },
};

/* frame capture|status|calls|call|scrub|texture|save|load|show */
static const M67Verb m67_frame_verbs[] = {
	{ "capture", "frame.capture", { NULL }, NULL, { NULL }, FALSE },
	{ "status", "frame.status", { NULL }, NULL, { NULL }, FALSE },
	{ "calls", "frame.calls", { NULL }, NULL, { NULL }, FALSE },
	{ "call", "frame.call", { "~index", NULL }, NULL, { NULL }, FALSE },
	{ "scrub", "frame.scrub", { "~index", NULL }, NULL, { NULL }, FALSE },
	{ "texture", "frame.texture", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "save", "frame.save", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "load", "frame.load", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "show", "frame.show", { NULL }, NULL, { NULL }, FALSE },
};

/* script run|languages|tools|run-tool|reload|run-in-editor|editor-running */
static const M67Verb m67_script_verbs[] = {
	{ "run", "script.run", { "code", NULL }, NULL, { NULL }, FALSE },
	{ "languages", "script.languages", { NULL }, NULL, { NULL }, FALSE },
	{ "tools", "script.tools", { NULL }, NULL, { NULL }, FALSE },
	{ "run-tool", "script.run_tool", { "id", NULL }, NULL, { NULL }, FALSE },
	{ "reload", "script.reload", { NULL }, NULL, { NULL }, FALSE },
	{ "run-in-editor", "script.run_in_editor", { "node", NULL }, NULL, { NULL }, TRUE },
	{ "editor-running", "script.editor_running", { NULL }, NULL, { NULL }, FALSE },
};

/* vscript create|open|get|node-types|gi-search|add-node|remove-node|set-node|connect|disconnect|set-variable|add-subgraph|remove-subgraph|add-comment|remove-comment|set-program|select|validate|run|live|breakpoint|continue|save */
static const M67Verb m67_vscript_verbs[] = {
	{ "create", "vscript.create", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "open", "vscript.open", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "get", "vscript.get", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "node-types", "vscript.node_types", { "query", NULL }, NULL, { NULL }, FALSE },
	{ "gi-search", "vscript.gi_search", { "query", NULL }, NULL, { NULL }, FALSE },
	{ "add-node", "vscript.add_node", { "type", NULL }, NULL, { NULL }, FALSE },
	{ "remove-node", "vscript.remove_node", { "id", NULL }, NULL, { NULL }, FALSE },
	{ "set-node", "vscript.set_node", { "id", NULL }, NULL, { NULL }, FALSE },
	{ "connect", "vscript.connect", { "from", "to", NULL }, NULL, { NULL }, FALSE },
	{ "disconnect", "vscript.disconnect", { "from", "to", NULL }, NULL, { NULL }, FALSE },
	{ "set-variable", "vscript.set_variable", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "add-subgraph", "vscript.add_subgraph", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "remove-subgraph", "vscript.remove_subgraph", { "name", NULL }, NULL, { NULL }, FALSE },
	{ "add-comment", "vscript.add_comment", { "text", NULL }, NULL, { NULL }, FALSE },
	{ "remove-comment", "vscript.remove_comment", { "id", NULL }, NULL, { NULL }, FALSE },
	{ "set-program", "vscript.set_program", { NULL }, NULL, { NULL }, FALSE },
	{ "select", "vscript.select", { NULL }, NULL, { NULL }, FALSE },
	{ "validate", "vscript.validate", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "run", "vscript.run", { NULL }, NULL, { NULL }, FALSE },
	{ "live", "vscript.live", { "@path", NULL }, NULL, { NULL }, FALSE },
	{ "breakpoint", "vscript.breakpoint", { "node", NULL }, NULL, { NULL }, TRUE },
	{ "continue", "vscript.continue", { NULL }, NULL, { NULL }, FALSE },
	{ "save", "vscript.save", { "@path", NULL }, NULL, { NULL }, FALSE },
};

/* memory status|types|resources|snapshot|snapshots|diff|clear-snapshots|history|show */
static const M67Verb m67_memory_verbs[] = {
	{ "status", "memory.status", { NULL }, NULL, { NULL }, FALSE },
	{ "types", "memory.types", { NULL }, NULL, { NULL }, FALSE },
	{ "resources", "memory.resources", { NULL }, NULL, { NULL }, FALSE },
	{ "snapshot", "memory.snapshot", { NULL }, NULL, { NULL }, FALSE },
	{ "snapshots", "memory.snapshots", { NULL }, NULL, { NULL }, FALSE },
	{ "diff", "memory.diff", { "from", NULL }, NULL, { NULL }, FALSE },
	{ "clear-snapshots", "memory.clear_snapshots", { NULL }, NULL, { NULL }, FALSE },
	{ "history", "memory.history", { NULL }, NULL, { NULL }, FALSE },
	{ "show", "memory.show", { NULL }, NULL, { NULL }, FALSE },
};

/* network stats|history|enable|reset|show */
static const M67Verb m67_network_verbs[] = {
	{ "stats", "network.stats", { NULL }, NULL, { NULL }, FALSE },
	{ "history", "network.history", { "metric", NULL }, NULL, { NULL }, FALSE },
	{ "enable", "network.enable", { NULL }, NULL, { NULL }, FALSE },
	{ "reset", "network.reset", { NULL }, NULL, { NULL }, FALSE },
	{ "show", "network.show", { NULL }, NULL, { NULL }, FALSE },
};

/* the M6/M7/M8 families: lrgeditorctl FAMILY VERB ... */
static const struct
{
	const gchar   *family;
	const M67Verb *verbs;
	gsize          n_verbs;
} m67_families[] = {
	{ "nav", m67_nav_verbs, G_N_ELEMENTS (m67_nav_verbs) },
	{ "bt", m67_bt_verbs, G_N_ELEMENTS (m67_bt_verbs) },
	{ "fsm", m67_fsm_verbs, G_N_ELEMENTS (m67_fsm_verbs) },
	{ "ai", m67_ai_verbs, G_N_ELEMENTS (m67_ai_verbs) },
	{ "particles", m67_particles_verbs, G_N_ELEMENTS (m67_particles_verbs) },
	{ "weather", m67_weather_verbs, G_N_ELEMENTS (m67_weather_verbs) },
	{ "terrain", m67_terrain_verbs, G_N_ELEMENTS (m67_terrain_verbs) },
	{ "water", m67_water_verbs, G_N_ELEMENTS (m67_water_verbs) },
	{ "mixer", m67_mixer_verbs, G_N_ELEMENTS (m67_mixer_verbs) },
	{ "audio", m67_audio_verbs, G_N_ELEMENTS (m67_audio_verbs) },
	{ "tiles", m67_tiles_verbs, G_N_ELEMENTS (m67_tiles_verbs) },
	{ "tileset", m67_tileset_verbs, G_N_ELEMENTS (m67_tileset_verbs) },
	{ "sprite", m67_sprite_verbs, G_N_ELEMENTS (m67_sprite_verbs) },
	{ "debug", m67_debug_verbs, G_N_ELEMENTS (m67_debug_verbs) },
	{ "lsp", m67_lsp_verbs, G_N_ELEMENTS (m67_lsp_verbs) },
	{ "profiler", m67_profiler_verbs, G_N_ELEMENTS (m67_profiler_verbs) },
	{ "frame", m67_frame_verbs, G_N_ELEMENTS (m67_frame_verbs) },
	{ "script", m67_script_verbs, G_N_ELEMENTS (m67_script_verbs) },
	{ "vscript", m67_vscript_verbs, G_N_ELEMENTS (m67_vscript_verbs) },
	{ "memory", m67_memory_verbs, G_N_ELEMENTS (m67_memory_verbs) },
	{ "network", m67_network_verbs, G_N_ELEMENTS (m67_network_verbs) },
};
/* m67e-generated: end */

/* The usage of one M6/M7 family: "family verb KEY | ...". */
static gchar *
m67_usage (const gchar   *family,
           const M67Verb *table,
           gsize          n)
{
	GString *s = g_string_new (NULL);
	gsize    i;
	guint    k;

	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append (s, " | ");
		g_string_append_printf (s, "%s %s", family, table[i].verb);
		for (k = 0; table[i].keys[k] != NULL; k++)
		{
			const gchar      *key = table[i].keys[k];
			g_autofree gchar *upper = NULL;

			if (*key == '@' || *key == '~')
				key++;
			upper = g_ascii_strup (key, -1);
			g_string_append_printf (s, " %s", upper);
		}
	}
	g_string_append (s, " [key=value...]");
	return g_string_free (s, FALSE);
}

/*
 * m67_params:
 * Builds the params of @row from @args: arguments without '=' fill the
 * positional keys, key=value arguments go to the top level (parsed like
 * every other verb's assignments) or, with @row->wrap, into that member.
 */
static JsonObject *
m67_params (const M67Verb  *row,
            gchar         **args,
            GError        **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonObject) wrapped = json_object_new ();
	guint                 k = 0;
	guint                 i;

	for (i = 0; args != NULL && args[i] != NULL; i++)
	{
		const gchar *eq = strchr (args[i], '=');

		if (eq == NULL || eq == args[i])
		{
			const gchar *key = row->keys[k];

			if (key == NULL)
			{
				usage_error (error, "%s: unexpected argument '%s'", row->method, args[i]);
				return NULL;
			}
			if (*key == '@')
			{
				g_autofree gchar *path = path_arg (args[i]);

				json_object_set_string_member (p, key + 1, path);
			}
			else if (*key == '~')
				json_object_set_member (p, key + 1, typed_value (args[i]));
			else
				json_object_set_string_member (p, key, args[i]);
			k++;
		}
		else if (row->wrap != NULL)
		{
			g_autofree gchar *key = g_strndup (args[i], eq - args[i]);

			if (g_strv_contains (row->top, key))
				json_object_set_member (p, key, lrg_ctl_parse_value (eq + 1));
			else
				json_object_set_member (wrapped, key, lrg_ctl_parse_value (eq + 1));
		}
		else
		{
			gchar *one[] = { args[i], NULL };

			if (!lrg_ctl_parse_assignments (one, p, error))
				return NULL;
		}
	}
	if (row->wrap != NULL && json_object_get_size (wrapped) > 0)
		json_object_set_object_member (p, row->wrap, json_object_ref (wrapped));
	if (row->node && opt_node != NULL && !json_object_has_member (p, "node"))
		json_object_set_string_member (p, "node", opt_node);
	return g_steal_pointer (&p);
}

/*
 * m67_run:
 * Runs "FAMILY VERB ..." of an M6/M7 family; a bare FAMILY runs its
 * "get" (or "state") verb. The result prints as YAML by default.
 */
static gboolean
m67_run (const gchar   *family,
         const M67Verb *table,
         gsize          n,
         gchar        **args,
         GError       **error)
{
	static gchar         *none[] = { NULL };
	const gchar          *verb = args[0];
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;
	gsize                 i;

	for (i = 0; verb == NULL && i < n; i++)
	{
		if (g_strcmp0 (table[i].verb, "get") == 0 || g_strcmp0 (table[i].verb, "state") == 0)
			verb = table[i].verb;
	}
	for (i = 0; verb != NULL && i < n; i++)
	{
		if (g_strcmp0 (verb, table[i].verb) != 0)
			continue;
		p = m67_params (&table[i], args[0] != NULL ? args + 1 : none, error);
		if (p == NULL)
			return FALSE;
		r = call (table[i].method, p, error);
		if (r == NULL)
			return FALSE;
		lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
		return TRUE;
	}
	{
		g_autofree gchar *usage = m67_usage (family, table, n);

		return usage_error (error, "%s", usage);
	}
}

/* The M6/M7 family named @family, or NULL. */
static gboolean
m67_lookup (const gchar    *family,
            const M67Verb **table,
            gsize          *n)
{
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (m67_families); i++)
	{
		if (g_strcmp0 (family, m67_families[i].family) == 0)
		{
			*table = m67_families[i].verbs;
			*n = m67_families[i].n_verbs;
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * m67_has_verb:
 * @family: a generated family ("tiles", "code", "profiler", ...)
 * @verb: (nullable): the first argument after the family
 *
 * Whether @verb is one of @family's generated verbs. The hand-written
 * commands that share a family with a generated table (tiles NODE ...,
 * code open|list|save, profiler [key=value]) ask this first and hand the
 * generated verbs over to m67_run_named().
 */
static gboolean
m67_has_verb (const gchar *family,
              const gchar *verb)
{
	const M67Verb *table = NULL;
	gsize          n = 0;
	gsize          i;

	if (verb == NULL || !m67_lookup (family, &table, &n))
		return FALSE;
	for (i = 0; i < n; i++)
	{
		if (g_strcmp0 (verb, table[i].verb) == 0)
			return TRUE;
	}
	return FALSE;
}

/* FAMILY VERB ...: runs a verb of a generated family by its name */
static gboolean
m67_run_named (const gchar  *family,
               gchar       **args,
               GError      **error)
{
	const M67Verb *table = NULL;
	gsize          n = 0;

	if (!m67_lookup (family, &table, &n))
		return usage_error (error, "%s: unknown command family", family);
	return m67_run (family, table, n, args, error);
}

/*
 * view layers [MASK | LAYER on|off] [viewport=N]: a viewport's render
 * layer cull mask. MASK is a number (0x... hex too), "all" or "none";
 * LAYER is an index 0..31 or a layer name.
 */
static gboolean
view_layers (gchar  **args,
             GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   r = NULL;
	const gchar          *pos[2] = { NULL, NULL };
	guint                 n = 0;
	guint                 i;

	for (i = 0; args[i] != NULL; i++)
	{
		if (strchr (args[i], '=') != NULL)
		{
			gchar *one[] = { args[i], NULL };

			if (!lrg_ctl_parse_assignments (one, p, error))
				return FALSE;
		}
		else if (n < 2)
			pos[n++] = args[i];
		else
			return usage_error (error, "view layers [MASK | LAYER on|off] [viewport=N]");
	}
	if (n == 1)
	{
		guint64 mask = 0;

		if (g_strcmp0 (pos[0], "all") == 0)
			mask = 0xFFFFFFFFu;
		else if (g_strcmp0 (pos[0], "none") == 0)
			mask = 0;
		else if (!g_ascii_string_to_unsigned (pos[0], g_str_has_prefix (pos[0], "0x") ? 16 : 10,
		                                      0, 0xFFFFFFFFu, &mask, NULL) &&
		         !(g_str_has_prefix (pos[0], "0x") &&
		           g_ascii_string_to_unsigned (pos[0] + 2, 16, 0, 0xFFFFFFFFu, &mask, NULL)))
			return usage_error (error, "view layers: '%s' is not a mask (a number, 0x hex, all or none)", pos[0]);
		json_object_set_int_member (p, "mask", (gint64) mask);
	}
	else if (n == 2)
	{
		guint64 layer = 0;

		if (g_ascii_string_to_unsigned (pos[0], 10, 0, 31, &layer, NULL))
			json_object_set_int_member (p, "layer", (gint64) layer);
		else
			json_object_set_string_member (p, "layer", pos[0]);
		json_object_set_boolean_member (p, "enabled", g_strcmp0 (pos[1], "on") == 0 ||
		                                g_strcmp0 (pos[1], "true") == 0 || g_strcmp0 (pos[1], "1") == 0);
	}
	r = call ("view.layers", p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output == LRG_CTL_FORMAT_DEFAULT ? LRG_CTL_FORMAT_YAML : output);
	return TRUE;
}

#ifdef LRG_EDITORCTL_HAVE_MCP
static gboolean
cmd_mcp (GError **error)
{
	g_autoptr(McpServer)         server = NULL;
	g_autoptr(McpStdioTransport) transport = NULL;

	/* Each tool call resolves the session again, so the bridge survives
	 * editor restarts and can start before any editor is running. */
	server = lrg_ctl_mcp_server_new (lrg_ctl_call_selected, opt_session);
	transport = mcp_stdio_transport_new ();
	mcp_server_set_transport (server, MCP_TRANSPORT (transport));
	if (mcp_server_run (server, error))
		return TRUE;
	/* The client closing stdin is how an MCP session normally ends. */
	if (error != NULL && (g_error_matches (*error, MCP_ERROR, MCP_ERROR_CONNECTION_CLOSED) ||
	                      g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)))
	{
		g_clear_error (error);
		return TRUE;
	}
	return FALSE;
}
#endif

static const gchar *completion_script =
	"# bash completion for lrgeditorctl\n"
	"_lrgeditorctl () {\n"
	"\tlocal cur prev verb\n"
	"\tcur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
	"\tprev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
	"\tverb=\"${COMP_WORDS[1]}\"\n"
	"\tcase \"${prev}\" in\n"
	"\t\t-o|--output) mapfile -t COMPREPLY < <(compgen -W 'yaml json table wide name' -- \"${cur}\"); return ;;\n"
	"\t\t-l|--level) mapfile -t COMPREPLY < <(compgen -W 'info success warning error' -- \"${cur}\"); return ;;\n"
	"\t\t-f|--filename) mapfile -t COMPREPLY < <(compgen -f -- \"${cur}\"); return ;;\n"
	"\t\t-p|--parent) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\"); return ;;\n"
	"\tesac\n"
	"\tif [[ ${COMP_CWORD} -eq 1 ]]\n"
	"\tthen\n"
	"\t\tmapfile -t COMPREPLY < <(compgen -W 'sessions get describe create delete set rename move duplicate edit apply select run play stop game undo redo save reload validate open new view screenshot logs events call notify quit mcp completion version tiles timeline thumbnail meta vcs prefab code inspector hierarchy asset source profiler data palette preferences material shader shadergraph env post lod bake seq skel2d anim bonemap clip physics physasset nav bt fsm ai particles weather terrain water mixer audio tileset sprite debug lsp frame script vscript memory network merge po collab scenes gizmo' -- \"${cur}\")\n"
	"\t\treturn\n"
	"\tfi\n"
	"\tcase \"${verb}\" in\n"
	"\t\tget|describe)\n"
	"\t\t\tif [[ ${COMP_CWORD} -eq 2 ]]\n"
	"\t\t\tthen\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W 'nodes node selection actions problems assets project info view history methods state play sessions' -- \"${cur}\")\n"
	"\t\t\telse\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\")\n"
	"\t\t\tfi ;;\n"
	"\t\tcreate|delete|set|rename|move|duplicate|edit)\n"
	"\t\t\tif [[ ${COMP_CWORD} -eq 2 ]]\n"
	"\t\t\tthen\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W 'node' -- \"${cur}\")\n"
	"\t\t\telse\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\")\n"
	"\t\t\tfi ;;\n"
	"\t\tselect|events) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\") ;;\n"
	"\t\trun) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get actions -o name 2>/dev/null)\" -- \"${cur}\") ;;\n"
	"\t\tplay) mapfile -t COMPREPLY < <(compgen -W 'start pause step stop state' -- \"${cur}\") ;;\n"
	"\t\tgame) mapfile -t COMPREPLY < <(compgen -W 'start stop' -- \"${cur}\") ;;\n"
	"\t\tview)\n"
	"\t\t\tif [[ ${COMP_CWORD} -eq 3 && ( ${prev} == render-mode || ${prev} == mode ) ]]\n"
	"\t\t\tthen\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W 'lit unlit wireframe normals lighting-only overdraw uv light-complexity ao reflections' -- \"${cur}\")\n"
	"\t\t\telse\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W 'top front right bottom back left persp all focus set layout render-mode mode overlay layers' -- \"${cur}\")\n"
	"\t\t\tfi ;;\n"
	"\t\tmaterial) mapfile -t COMPREPLY < <(compgen -W 'open create variant get set revert save preview assign list' -- \"${cur}\") ;;\n"
	"\t\tshader) mapfile -t COMPREPLY < <(compgen -W 'create compile diagnostics uniforms reload' -- \"${cur}\") ;;\n"
	"\t\tshadergraph) mapfile -t COMPREPLY < <(compgen -W 'open create types get add remove connect disconnect param input validate glsl save' -- \"${cur}\") ;;\n"
	"\t\tenv) mapfile -t COMPREPLY < <(compgen -W 'get set reset bake progress show' -- \"${cur}\") ;;\n"
	"\t\tpost) mapfile -t COMPREPLY < <(compgen -W 'types open create get add remove move set save preview look assign' -- \"${cur}\") ;;\n"
	"\t\tlod) mapfile -t COMPREPLY < <(compgen -W 'get force' -- \"${cur}\") ;;\n"
	"\t\tbake) mapfile -t COMPREPLY < <(compgen -W 'get set reset start progress cancel save clear evaluate preview page show' -- \"${cur}\") ;;\n"
	"\t\tseq|sequence) mapfile -t COMPREPLY < <(compgen -W 'list open create get set add-track add-key set-key add-section remove bind play scrub save undo from-timeline' -- \"${cur}\") ;;\n"
	"\t\tskel2d) mapfile -t COMPREPLY < <(compgen -W 'create open get add-bone set-bone remove-bone add-slot set-slot remove-slot draw-order attach detach mesh auto-weights paint-weights skin clip key play mode pick save' -- \"${cur}\") ;;\n"
	"\t\tanim) mapfile -t COMPREPLY < <(compgen -W 'open create get enter add-layer remove-layer add-state remove-state set-state add-transition set-transition remove-transition add-param remove-param set-param blend-tree preview validate save' -- \"${cur}\") ;;\n"
	"\t\tbonemap) mapfile -t COMPREPLY < <(compgen -W 'open create get set auto-fill rules models preview save' -- \"${cur}\") ;;\n"
	"\t\tclip) mapfile -t COMPREPLY < <(compgen -W 'list info preview morph' -- \"${cur}\") ;;\n"
	"\t\tphysics) mapfile -t COMPREPLY < <(compgen -W 'get add-body remove-body set-body add-shape remove-shape set-shape move-shape add-joint remove-joint set-joint fit overlay state show layers' -- \"${cur}\") ;;\n"
	"\t\tphysasset) mapfile -t COMPREPLY < <(compgen -W 'open create get model add-part remove-part set-part generate stabilize exclude unexclude set select simulate save' -- \"${cur}\") ;;\n"
	/* m67e-completion: begin */
	"\t\tnav) mapfile -t COMPREPLY < <(compgen -W 'get set new bake cancel save clear area-add area-remove area-set area-box-add area-box-remove area-box-set link-add link-remove link-set select overlay agents debug-agent show' -- \"${cur}\") ;;\n"
	"\t\tbt) mapfile -t COMPREPLY < <(compgen -W 'open create get palette add-node remove-node move-node set-node set-position layout select show add-key remove-key set-subtree remove-subtree open-subtree validate save live' -- \"${cur}\") ;;\n"
	"\t\tfsm) mapfile -t COMPREPLY < <(compgen -W 'open create get enter up add-state remove-state rename-state set-initial add-transition set-transition remove-transition set-actions set-position select add-key remove-key validate save live send-event' -- \"${cur}\") ;;\n"
	"\t\tai) mapfile -t COMPREPLY < <(compgen -W 'show entities select state set-key send-event brains topic log' -- \"${cur}\") ;;\n"
	"\t\tparticles) mapfile -t COMPREPLY < <(compgen -W 'open create get kinds set add-emitter remove-emitter move-emitter duplicate-emitter add-module remove-module move-module select solo play restart seek preview stats save render' -- \"${cur}\") ;;\n"
	"\t\tweather) mapfile -t COMPREPLY < <(compgen -W 'open create get kinds set add-preset remove-preset set-preset add-layer remove-layer move-layer set-layer set-param set-transition remove-transition select preview save render' -- \"${cur}\") ;;\n"
	"\t\tterrain) mapfile -t COMPREPLY < <(compgen -W 'get brush stroke sample add-layer remove-layer set-layer add-foliage remove-foliage set-foliage rebake save create show' -- \"${cur}\") ;;\n"
	"\t\twater) mapfile -t COMPREPLY < <(compgen -W 'get set set-wave add-point set-point remove-point save' -- \"${cur}\") ;;\n"
	"\t\tmixer) mapfile -t COMPREPLY < <(compgen -W 'show open create set-default get effect-types add-bus remove-bus set-bus set-send remove-send add-effect remove-effect move-effect set-effect add-snapshot remove-snapshot set-snapshot capture-snapshot set-duck remove-duck preview meters save' -- \"${cur}\") ;;\n"
	"\t\taudio) mapfile -t COMPREPLY < <(compgen -W 'get set curve buses' -- \"${cur}\") ;;\n"
	"\t\ttiles) mapfile -t COMPREPLY < <(compgen -W 'state grid brush autopaint refresh cell-at tile-info' -- \"${cur}\") ;;\n"
	"\t\ttileset) mapfile -t COMPREPLY < <(compgen -W 'create open get set select tile collision rule-tile rule preview save' -- \"${cur}\") ;;\n"
	"\t\tsprite) mapfile -t COMPREPLY < <(compgen -W 'create open get slice frame clip clip-frame event play save' -- \"${cur}\") ;;\n"
	"\t\tdebug) mapfile -t COMPREPLY < <(compgen -W 'state show breakpoints set-breakpoint remove-breakpoint toggle-breakpoint clear-breakpoints continue pause step-over step-in step-out stop stack scopes variables evaluate watches add-watch remove-watch dap-start dap-stop' -- \"${cur}\") ;;\n"
	"\t\tlsp) mapfile -t COMPREPLY < <(compgen -W 'servers status diagnostics complete hover definition references rename format restart' -- \"${cur}\") ;;\n"
	"\t\tprofiler) mapfile -t COMPREPLY < <(compgen -W 'sample set capture save load frames frame compare view' -- \"${cur}\") ;;\n"
	"\t\tframe) mapfile -t COMPREPLY < <(compgen -W 'capture status calls call scrub texture save load show' -- \"${cur}\") ;;\n"
	"\t\tscript) mapfile -t COMPREPLY < <(compgen -W 'run languages tools run-tool reload run-in-editor editor-running' -- \"${cur}\") ;;\n"
	"\t\tvscript) mapfile -t COMPREPLY < <(compgen -W 'create open get node-types gi-search add-node remove-node set-node connect disconnect set-variable add-subgraph remove-subgraph add-comment remove-comment set-program select validate run live breakpoint continue save' -- \"${cur}\") ;;\n"
	"\t\tmemory) mapfile -t COMPREPLY < <(compgen -W 'status types resources snapshot snapshots diff clear-snapshots history show' -- \"${cur}\") ;;\n"
	"\t\tnetwork) mapfile -t COMPREPLY < <(compgen -W 'stats history enable reset show' -- \"${cur}\") ;;\n"
	"\t\tmerge) mapfile -t COMPREPLY < <(compgen -W 'install' -- \"${cur}\") ;;\n"
	"\t\tpo) mapfile -t COMPREPLY < <(compgen -W 'extract pseudo import export' -- \"${cur}\") ;;\n"
	"\t\tcollab) mapfile -t COMPREPLY < <(compgen -W 'apply get lock unlock cursor presence' -- \"${cur}\") ;;\n"
	"\t\tscenes) mapfile -t COMPREPLY < <(compgen -W 'list open active' -- \"${cur}\") ;;\n"
	"\t\tgizmo) mapfile -t COMPREPLY < <(compgen -W 'apply pivot cursor origin' -- \"${cur}\") ;;\n"
	"\t\tvcs|git) mapfile -t COMPREPLY < <(compgen -W 'diff log commit locks magit' -- \"${cur}\") ;;\n"
	/* m67e-completion: end */
	"\t\tmeta) mapfile -t COMPREPLY < <(compgen -W 'get set' -- \"${cur}\") ;;\n"
	"\t\tprefab) mapfile -t COMPREPLY < <(compgen -W 'overrides apply revert sync relink unlink' -- \"${cur}\") ;;\n"
	"\t\tcode) mapfile -t COMPREPLY < <(compgen -W 'open list save' -- \"${cur}\") ;;\n"
	"\t\tinspector) mapfile -t COMPREPLY < <(compgen -W 'list get set reset state apply-override revert-override' -- \"${cur}\") ;;\n"
	"\t\thierarchy) mapfile -t COMPREPLY < <(compgen -W 'reveal rename filter state' -- \"${cur}\") ;;\n"
	"\t\tasset) mapfile -t COMPREPLY < <(compgen -W 'reveal rename move duplicate trash orphans new' -- \"${cur}\") ;;\n"
	"\t\tsource|sources) mapfile -t COMPREPLY < <(compgen -W 'list ls search export import place' -- \"${cur}\") ;;\n"
	"\t\tdata) mapfile -t COMPREPLY < <(compgen -W 'list open get set edit undo redo validate save reload close reveal' -- \"${cur}\") ;;\n"
	"\t\tpalette) mapfile -t COMPREPLY < <(compgen -W 'commands files' -- \"${cur}\") ;;\n"
	"\t\tcall) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get methods -o json 2>/dev/null | tr -d '[]\\\",')\" -- \"${cur}\") ;;\n"
	"\t\tcompletion) mapfile -t COMPREPLY < <(compgen -W 'bash' -- \"${cur}\") ;;\n"
	"\tesac\n"
	"}\n"
	"complete -o default -F _lrgeditorctl lrgeditorctl\n";

/* ==========================================================================
 * Main
 * ========================================================================== */

static gboolean
dispatch (gchar  **argv,
          GError **error)
{
	const gchar  *verb = argv[0];
	gchar       **rest = argv + 1;

	if (g_strcmp0 (verb, "sessions") == 0)
		return cmd_sessions (error);
	if (g_strcmp0 (verb, "tiles") == 0)
		return cmd_tiles (rest, error);
	if (g_strcmp0 (verb, "timeline") == 0)
		return cmd_timeline (rest, error);
	if (g_strcmp0 (verb, "thumbnail") == 0)
		return cmd_thumbnail (rest, error);
	if (g_strcmp0 (verb, "meta") == 0)
		return cmd_meta (rest, error);
	if (g_strcmp0 (verb, "vcs") == 0 || g_strcmp0 (verb, "git") == 0)
		return cmd_vcs (rest, error);
	if (g_strcmp0 (verb, "merge") == 0)
		return cmd_merge (rest, error);
	if (g_strcmp0 (verb, "po") == 0)
		return cmd_po (rest, error);
	if (g_strcmp0 (verb, "collab") == 0)
		return cmd_collab (rest, error);
	if (g_strcmp0 (verb, "scenes") == 0)
		return cmd_scenes (rest, error);
	if (g_strcmp0 (verb, "gizmo") == 0)
		return cmd_gizmo (rest, error);
	if (g_strcmp0 (verb, "prefab") == 0)
		return cmd_prefab (rest, error);
	if (g_strcmp0 (verb, "code") == 0)
		return cmd_code (rest, error);
	if (g_strcmp0 (verb, "inspector") == 0)
		return cmd_inspector (rest, error);
	if (g_strcmp0 (verb, "hierarchy") == 0)
		return cmd_hierarchy (rest, error);
	if (g_strcmp0 (verb, "asset") == 0)
		return cmd_asset (rest, error);
	if (g_strcmp0 (verb, "source") == 0 || g_strcmp0 (verb, "sources") == 0)
		return cmd_source (rest, error);
	if (g_strcmp0 (verb, "profiler") == 0)
		return cmd_profiler (rest, error);
	if (g_strcmp0 (verb, "data") == 0)
		return cmd_data (rest, error);
	if (g_strcmp0 (verb, "palette") == 0)
		return cmd_palette (rest, error);
	if (g_strcmp0 (verb, "preferences") == 0 || g_strcmp0 (verb, "prefs") == 0)
		return cmd_preferences (rest, error);
	if (g_strcmp0 (verb, "material") == 0 || g_strcmp0 (verb, "mat") == 0)
		return cmd_material (rest, error);
	if (g_strcmp0 (verb, "shader") == 0)
		return cmd_shader (rest, error);
	if (g_strcmp0 (verb, "shadergraph") == 0 || g_strcmp0 (verb, "sg") == 0)
		return cmd_shadergraph (rest, error);
	if (g_strcmp0 (verb, "env") == 0 || g_strcmp0 (verb, "environment") == 0)
		return cmd_env (rest, error);
	if (g_strcmp0 (verb, "post") == 0)
		return cmd_post (rest, error);
	if (g_strcmp0 (verb, "lod") == 0)
		return cmd_lod (rest, error);
	if (g_strcmp0 (verb, "bake") == 0)
		return cmd_bake (rest, error);
	if (g_strcmp0 (verb, "seq") == 0 || g_strcmp0 (verb, "sequence") == 0)
		return cmd_seq (rest, error);
	if (g_strcmp0 (verb, "skel2d") == 0)
		return cmd_skel2d (rest, error);
	if (g_strcmp0 (verb, "anim") == 0)
		return cmd_anim (rest, error);
	if (g_strcmp0 (verb, "bonemap") == 0)
		return cmd_bonemap (rest, error);
	if (g_strcmp0 (verb, "clip") == 0)
		return cmd_clip (rest, error);
	if (g_strcmp0 (verb, "physics") == 0)
		return cmd_physics (rest, error);
	if (g_strcmp0 (verb, "physasset") == 0)
		return cmd_physasset (rest, error);
	{
		const M67Verb *table = NULL;
		gsize          n = 0;

		/* nav bt fsm ai particles weather terrain water mixer audio tileset sprite */
		if (g_strcmp0 (verb, "tiles") != 0 && m67_lookup (verb, &table, &n))
			return m67_run (verb, table, n, rest, error);
	}
	if (g_strcmp0 (verb, "get") == 0)
		return cmd_get (rest, error);
	if (g_strcmp0 (verb, "describe") == 0)
		return cmd_describe (rest, error);
	if (g_strcmp0 (verb, "create") == 0)
		return cmd_create (rest, error);
	if (g_strcmp0 (verb, "delete") == 0 || g_strcmp0 (verb, "rm") == 0)
		return cmd_delete (rest, error);
	if (g_strcmp0 (verb, "set") == 0)
		return cmd_set (rest, error);
	if (g_strcmp0 (verb, "rename") == 0)
		return cmd_rename (rest, error);
	if (g_strcmp0 (verb, "move") == 0 || g_strcmp0 (verb, "mv") == 0)
		return cmd_move (rest, error);
	if (g_strcmp0 (verb, "duplicate") == 0 || g_strcmp0 (verb, "dup") == 0)
		return cmd_duplicate (rest, error);
	if (g_strcmp0 (verb, "edit") == 0)
		return cmd_edit (rest, error);
	if (g_strcmp0 (verb, "apply") == 0)
		return cmd_apply (error);
	if (g_strcmp0 (verb, "select") == 0)
		return cmd_select (rest, error);
	if (g_strcmp0 (verb, "run") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		if (rest[0] == NULL)
		{
			gchar *actions[] = { (gchar *) "actions", NULL };

			return cmd_get (actions, error);
		}
		json_object_set_string_member (p, "id", rest[0]);
		return simple_method ("action.run", p, NULL, error);
	}
	if (g_strcmp0 (verb, "play") == 0)
	{
		const gchar      *what = (rest[0] != NULL) ? rest[0] : "start";
		g_autofree gchar *method = g_strconcat ("play.", what, NULL);

		if (g_strcmp0 (what, "start") != 0 && g_strcmp0 (what, "pause") != 0 && g_strcmp0 (what, "step") != 0 &&
		    g_strcmp0 (what, "stop") != 0 && g_strcmp0 (what, "state") != 0)
			return usage_error (error, "play start|pause|step|stop|state");
		if (output == LRG_CTL_FORMAT_DEFAULT)
			output = LRG_CTL_FORMAT_YAML;
		return simple_method (method, NULL, NULL, error);
	}
	if (g_strcmp0 (verb, "stop") == 0)
		return simple_method ("play.stop", NULL, "stopped", error);
	if (g_strcmp0 (verb, "game") == 0)
	{
		if (g_strcmp0 (rest[0], "start") == 0)
			return simple_method ("run.start", NULL, "game started", error);
		if (g_strcmp0 (rest[0], "stop") == 0)
			return simple_method ("run.stop", NULL, "game stopped", error);
		return usage_error (error, "game start|stop");
	}
	if (g_strcmp0 (verb, "undo") == 0)
		return simple_method ("edit.undo", NULL, "undone", error);
	if (g_strcmp0 (verb, "redo") == 0)
		return simple_method ("edit.redo", NULL, "redone", error);
	if (g_strcmp0 (verb, "save") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;

		if (rest[0] != NULL)
		{
			g_autofree gchar *abs = g_canonicalize_filename (rest[0], NULL);

			json_object_set_string_member (p, "path", abs);
		}
		r = call ("level.save", p, error);
		if (r == NULL)
			return FALSE;
		printf ("saved %s\n", (JSON_NODE_HOLDS_VALUE (r) && json_node_get_value_type (r) == G_TYPE_STRING)
		                      ? json_node_get_string (r) : "");
		return TRUE;
	}
	if (g_strcmp0 (verb, "reload") == 0)
		return simple_method ("level.reload", NULL, "reloaded", error);
	if (g_strcmp0 (verb, "validate") == 0)
	{
		gchar             *problems[] = { (gchar *) "problems", NULL };
		g_autoptr(JsonNode) list = NULL;
		guint               i;

		if (!cmd_get (problems, error))
			return FALSE;
		/* Exit status 1 when there are errors (for scripts and CI). */
		list = call ("level.validate", NULL, error);
		for (i = 0; list != NULL && i < json_array_get_length (json_node_get_array (list)); i++)
			if (g_strcmp0 (str_member (json_array_get_object_element (json_node_get_array (list), i), "severity"),
			               "error") == 0)
			{
				g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "the level has errors");
				return FALSE;
			}
		return list != NULL;
	}
	if (g_strcmp0 (verb, "open") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autofree gchar     *abs = NULL;

		if (rest[0] == NULL)
			return usage_error (error, "open LEVEL [--force]");
		abs = g_canonicalize_filename (rest[0], NULL);
		json_object_set_string_member (p, "path", g_file_test (abs, G_FILE_TEST_EXISTS) ? abs : rest[0]);
		json_object_set_boolean_member (p, "force", opt_force);
		return simple_method ("level.open", p, "opened", error);
	}
	if (g_strcmp0 (verb, "new") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   info = NULL;

		info = call ("editor.info", NULL, error);
		if (info == NULL)
			return FALSE;
		if (json_object_get_boolean_member (json_node_get_object (info), "dirty") && !opt_force)
			return usage_error (error, "The level has unsaved changes; save it or pass --force");
		if (rest[0] != NULL)
			json_object_set_string_member (p, "name", rest[0]);
		return simple_method ("level.new", p, "new level", error);
	}
	if (g_strcmp0 (verb, "view") == 0)
		return cmd_view (rest, error);
	if (g_strcmp0 (verb, "screenshot") == 0)
		return cmd_screenshot (rest, error);
	if (g_strcmp0 (verb, "logs") == 0 || g_strcmp0 (verb, "log") == 0)
		return cmd_logs (error);
	if (g_strcmp0 (verb, "events") == 0 || g_strcmp0 (verb, "watch") == 0)
		return cmd_events (rest, error);
	if (g_strcmp0 (verb, "call") == 0)
		return cmd_call (rest, error);
	if (g_strcmp0 (verb, "notify") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autofree gchar     *msg = NULL;

		const gchar          *level = NULL;

		if (rest[0] == NULL)
			return usage_error (error, "notify MESSAGE [--level info|success|warning|error]");
		if (opt_level != NULL && (level = notify_level_name (opt_level)) == NULL)
			return usage_error (error, "Unknown toast level '%s' (info, success, warning, error or 0-3)",
			                    opt_level);
		msg = g_strjoinv (" ", rest);
		json_object_set_string_member (p, "message", msg);
		/* ui.notify takes the level by name; the app still accepts 0-3
		 * from older clients, but the name is the documented form */
		json_object_set_string_member (p, "level", level != NULL ? level : "info");
		return simple_method ("ui.notify", p, NULL, error);
	}
	if (g_strcmp0 (verb, "quit") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		json_object_set_boolean_member (p, "force", opt_force);
		return simple_method ("editor.quit", p, "quitting", error);
	}
	if (g_strcmp0 (verb, "mcp") == 0)
	{
#ifdef LRG_EDITORCTL_HAVE_MCP
		return cmd_mcp (error);
#else
		return usage_error (error, "lrgeditorctl was built without MCP support (needs mcp-glib, libsoup-3.0, libdex-1)");
#endif
	}
	if (g_strcmp0 (verb, "completion") == 0)
	{
		if (g_strcmp0 (rest[0], "bash") != 0)
			return usage_error (error, "completion bash");
		fputs (completion_script, stdout);
		return TRUE;
	}
	if (g_strcmp0 (verb, "version") == 0)
	{
		g_autoptr(JsonNode) hello = NULL;

		printf ("lrgeditorctl %s\n", LRG_EDITORCTL_VERSION);
		hello = call ("hello", NULL, NULL);
		if (hello != NULL && JSON_NODE_HOLDS_OBJECT (hello))
			printf ("editor protocol %" G_GINT64_FORMAT " (pid %" G_GINT64_FORMAT ")\n",
			        json_object_get_int_member (json_node_get_object (hello), "protocol"),
			        json_object_get_int_member (json_node_get_object (hello), "pid"));
		return TRUE;
	}
	return usage_error (error, "Unknown command '%s' (see lrgeditorctl --help)", verb);
}

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	gboolean                  ok;

	ctl_argv0 = argv[0];
	ctx = g_option_context_new ("- control running libregnum editors");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx, description);
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("lrgeditorctl: %s\n", error->message);
		return 2;
	}

	if (opt_license)
	{
		fputs (license_text, stdout);
		return 0;
	}
	if (opt_version)
	{
		printf ("lrgeditorctl %s\n", LRG_EDITORCTL_VERSION);
		return 0;
	}
	if (opt_output != NULL && !lrg_ctl_parse_format (opt_output, &output))
	{
		g_printerr ("lrgeditorctl: unknown output format '%s' (yaml, json, table, wide, name)\n", opt_output);
		return 2;
	}
	if (opt_args == NULL || opt_args[0] == NULL)
	{
		g_autofree gchar *help = g_option_context_get_help (ctx, TRUE, NULL);

		fputs (help, stderr);
		return 2;
	}

	ok = dispatch (opt_args, &error);
	if (!ok)
	{
		g_printerr ("lrgeditorctl: %s\n", error != NULL ? error->message : "failed");
		return g_error_matches (error, LRG_CTL_ERROR, LRG_CTL_ERROR_USAGE) ? 2 : 1;
	}
	g_free (session_socket);
	return 0;
}
