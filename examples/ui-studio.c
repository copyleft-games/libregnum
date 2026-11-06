/* ui-studio.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A "flashy tool app" built purely on libregnum's UI: a menu bar over a
 * dock area holding a live 3D viewport, a node graph, a block editor, a
 * timeline whose keyframes animate the scene while it plays, a script
 * editor and a console. The window is described by
 * examples/data/ui/studio.yaml (the block palette and sample program by
 * studio-blocks.yaml) and built with LrgUILoader; when the document is
 * missing or fails to load the same window is built in C
 * (build_ui_in_c()), so the program always runs. Either way the C side
 * then wires the scene, the animation tracks, the console input, the
 * actions, the shortcuts and the block interpreter.
 *
 * Controls:
 *   Ctrl+Q          quit                 Space     play / pause (runs the
 *   Ctrl+S          save the dock layout           "when play pressed" scripts)
 *   Ctrl+Shift+S    save as... (dialog)  Ctrl+O    open a layout (dialog)
 *   F5              run the script       Ctrl+N    reset layout, clear console
 *   Drag the scene  orbit the camera     Wheel     zoom
 *   Drag tabs       re-dock / float panels (see LrgDockArea)
 *   Double-click a block script: run it (one block per tick, highlighted)
 *
 * The layout is saved as YAML to $XDG_CACHE_HOME/libregnum/ui-studio-layout.yaml
 * and restored at start-up (unless --default-layout). File > Open and
 * Save As use LrgFileDialog (in-app, or the desktop's portal chooser
 * with --native-dialogs); a save is announced with an LrgNotification
 * (a desktop notification, or a toast when there is no notification
 * server or with --in-app-notifications). View > Canvas Look switches
 * the node graph and the timeline between the five LrgCanvasLook presets.
 *
 * Run with --help for options.
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#define WINDOW_WIDTH   (1280)
#define WINDOW_HEIGHT  (800)
#define STUDIO_FILE    "studio.yaml"
#define BLOCKS_FILE    "studio-blocks.yaml"
#define LAYOUT_FILE    "ui-studio-layout.yaml"
#define BLOCK_STEP     (0.3)    /* seconds the interpreter spends per block */
#define BOUNCE_TIME    (0.7f)   /* seconds a "bounce" block's hop lasts */

static const gchar *license_text =
	"ui-studio - a docked tool window built on the libregnum UI\n"
	"Copyright (C) 2026  Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* The script language, the same definition studio.yaml carries under
 * languages:, registered by the C fallback */
static const gchar *script_language =
	"name: studio-script\n"
	"base: code\n"
	"extensions: [\"*.sscript\"]\n"
	"line-comment: \"#\"\n"
	"strings: \"\\\"'\"\n"
	"keywords: [on, when, do, end, let, if, else, emit]\n"
	"builtins: [spawn, play, stop, wait, log]\n"
	"constants: [true, false, none]\n";

static const gchar *script_text =
	"# Runs when the scene starts\n"
	"on start do\n"
	"  let door = spawn(\"door\", 4, 2)\n"
	"  play(\"intro\")\n"
	"  when door.open do\n"
	"    emit(\"level-complete\")\n"
	"  end\n"
	"end\n";

/* Motion presets offered in Theme > Motion, by LrgMotion nick */
static const gchar *motion_nicks[] = { "smooth", "snappy", "bouncy", "wobbly", "none" };

/* View > Canvas Look entries: label and LrgCanvasLook nick */
static const struct { const gchar *label; const gchar *nick; } canvas_looks[] = {
	{ "_Auto (theme)", "auto" },
	{ "_Modern",       "modern" },
	{ "_Blueprint",    "blueprint" },
	{ "Ble_nder",      "blender" },
	{ "_Neon",         "neon" }
};

/* Hues of the "set color" block's dropdown choices */
static const struct { const gchar *name; gfloat hue; } block_colors[] = {
	{ "red",    355.0f },
	{ "green",  135.0f },
	{ "blue",   215.0f },
	{ "purple", 275.0f },
	{ "gold",    45.0f }
};

/* A "repeat" block being run: the block and the iterations still to go */
typedef struct
{
	LrgBlock *loop;
	gint      left;
} BlockLoop;

/* ==========================================================================
 * Application state
 * ========================================================================== */

typedef struct
{
	LrgCanvas            *canvas;
	LrgWidget            *root;          /* the "studio" vbox */
	LrgUILoader          *loader;
	gboolean              from_yaml;

	/* Widgets by their studio.yaml ids (owned by the tree) */
	LrgMenuBar           *menubar;
	LrgDockArea          *dock;
	LrgViewport          *scene;
	LrgNodeGraph         *graph;
	LrgTextEditor        *script;
	LrgTimelineView      *timeline;
	LrgTextEditor        *console;
	LrgTextEditor        *input;         /* added under the console in C */
	LrgBlockEditor       *blocks;
	LrgLabel             *status;
	LrgLabel             *clock;
	LrgToastQueue        *toasts;        /* overlay; in-app notification fallback */

	GSimpleActionGroup   *actions;       /* "app" on the canvas */
	gchar                *default_layout;
	gchar                *layout_path;
	gboolean              native_dialogs;
	gboolean              quit;
	guint                 added_nodes;

	/* The block interpreter: the next block to run, the repeat loops
	 * it is inside (innermost last) and the time left on this step */
	gboolean              running;
	LrgBlock             *pc;
	GArray               *loops;         /* BlockLoop */
	gdouble               run_delay;
	guint                 run_steps;

	/* What the blocks do to the scene, on top of the timeline curves */
	gfloat                block_spin;
	gfloat                block_spin_target;
	gfloat                bounce_height;
	gfloat                bounce_time;
	gfloat                orbit_left;    /* radians of camera swing still to go */
	gboolean              block_tinted;
	gfloat                block_hue;

	/* Scene */
	GrlCamera3D          *camera;
	gfloat                yaw;
	gfloat                pitch;
	gfloat                distance;
	Model                 cube;
	gboolean              cube_loaded;
	gboolean              grid;

	/* Animation tracks the scene samples */
	LrgTimelineViewTrack *track_spin;
	LrgTimelineViewTrack *track_hue;
	LrgTimelineViewTrack *track_bounce;
	LrgTimelineViewTrack *track_camera;
} Studio;

static void console_print (Studio *app, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

/* console_print: one line at the end of the console log */
static void
console_print (Studio      *app,
               const gchar *format,
               ...)
{
	g_autofree gchar *line = NULL;
	g_autofree gchar *text = NULL;
	va_list           args;

	va_start (args, format);
	line = g_strdup_vprintf (format, args);
	va_end (args);
	if (app->console == NULL)
		return;
	text = lrg_text_editor_get_text (app->console);
	lrg_text_editor_append (app->console, (text[0] == '\0' || g_str_has_suffix (text, "\n")) ? "" : "\n");
	lrg_text_editor_append (app->console, line);
}

static void
set_status (Studio      *app,
            const gchar *text)
{
	if (app->status != NULL)
		lrg_label_set_text (app->status, text);
}

/* ==========================================================================
 * The 3D scene
 * ========================================================================== */

/* load_shaded_cube: a unit cube with per-face grey vertex colours, so a
 * plain tint reads as a lit solid with raylib's default shader */
static Model
load_shaded_cube (void)
{
	Mesh src = GenMeshCube (1.0f, 1.0f, 1.0f);
	Mesh mesh = { 0 };
	gint i;

	mesh.vertexCount = src.vertexCount;
	mesh.triangleCount = src.triangleCount;
	mesh.vertices = MemAlloc ((guint) src.vertexCount * 3 * sizeof (gfloat));
	mesh.normals = MemAlloc ((guint) src.vertexCount * 3 * sizeof (gfloat));
	mesh.texcoords = MemAlloc ((guint) src.vertexCount * 2 * sizeof (gfloat));
	mesh.colors = MemAlloc ((guint) src.vertexCount * 4);
	mesh.indices = MemAlloc ((guint) src.triangleCount * 3 * sizeof (gushort));
	memcpy (mesh.vertices, src.vertices, (gsize) src.vertexCount * 3 * sizeof (gfloat));
	memcpy (mesh.normals, src.normals, (gsize) src.vertexCount * 3 * sizeof (gfloat));
	memcpy (mesh.texcoords, src.texcoords, (gsize) src.vertexCount * 2 * sizeof (gfloat));
	memcpy (mesh.indices, src.indices, (gsize) src.triangleCount * 3 * sizeof (gushort));

	/* Ambient + diffuse from a light above, front and right */
	for (i = 0; i < src.vertexCount; i++)
	{
		gfloat d = MAX (0.0f, src.normals[i * 3] * 0.40f + src.normals[i * 3 + 1] * 0.80f +
		                      src.normals[i * 3 + 2] * 0.45f);
		guint8 v = (guint8) CLAMP (95.0f + 160.0f * d, 0.0f, 255.0f);

		mesh.colors[i * 4 + 0] = v;
		mesh.colors[i * 4 + 1] = v;
		mesh.colors[i * 4 + 2] = v;
		mesh.colors[i * 4 + 3] = 255;
	}
	UnloadMesh (src);
	UploadMesh (&mesh, false);
	return LoadModelFromMesh (mesh);
}

/* sample: a track's value at the playhead, or @fallback without one */
static gfloat
sample (Studio               *app,
        LrgTimelineViewTrack *track,
        gfloat                fallback)
{
	if (app->timeline == NULL || track == NULL)
		return fallback;
	return (gfloat) lrg_timeline_view_evaluate (app->timeline, track, lrg_timeline_view_get_time (app->timeline));
}

/* orbit_apply: the camera on a sphere around the scene, shifted along X
 * by the "Camera / Position X" curve */
static void
orbit_apply (Studio *app)
{
	gfloat cp = cosf (app->pitch);
	gfloat shift = sample (app, app->track_camera, 0.0f) * 0.25f;

	grl_camera3d_set_target_xyz (app->camera, shift, 0.8f, 0.0f);
	grl_camera3d_set_position_xyz (app->camera,
	                               shift + app->distance * cp * sinf (app->yaw),
	                               0.8f + app->distance * sinf (app->pitch),
	                               app->distance * cp * cosf (app->yaw));
}

/* block_bounce: the height of a "bounce" block's hop, a half sine over
 * BOUNCE_TIME seconds */
static gfloat
block_bounce (Studio *app)
{
	if (app->bounce_time >= BOUNCE_TIME)
		return 0.0f;
	return app->bounce_height * sinf ((gfloat) G_PI * app->bounce_time / BOUNCE_TIME);
}

/* draw_scene: the viewport's render function, run from the canvas step
 * between BeginTextureMode() and EndTextureMode() on the viewport's own
 * texture. Every animated value comes from a timeline curve sampled at
 * the playhead, so scrubbing the ruler scrubs the scene */
static void
draw_scene (LrgViewport      *viewport,
            GrlRenderTexture *target,
            gfloat            width,
            gfloat            height,
            gpointer          user_data)
{
	Studio  *app = user_data;
	gfloat   spin = sample (app, app->track_spin, 0.0f) + app->block_spin;
	gfloat   hue = app->block_tinted ? app->block_hue : sample (app, app->track_hue, 260.0f);
	gfloat   bounce = sample (app, app->track_bounce, 0.0f) + block_bounce (app);
	Color    main_tint = ColorFromHSV (hue, 0.55f, 1.0f);
	Vector3  axis = { 0.0f, 1.0f, 0.0f };
	Vector3  tilt = { 0.35f, 1.0f, 0.15f };
	guint    i;

	orbit_apply (app);
	grl_camera3d_begin (app->camera);
	if (app->grid)
		DrawGrid (20, 0.5f);
	if (app->cube_loaded)
	{
		Vector3 pos = { 0.0f, 0.75f + bounce * 1.2f, 0.0f };
		Vector3 scale = { 1.5f, 1.5f, 1.5f };

		DrawModelEx (app->cube, pos, tilt, spin, scale, main_tint);

		/* Satellites: complementary hues, counter-rotating */
		for (i = 0; i < 5; i++)
		{
			gfloat  a = spin * 0.0174533f * 0.5f + (gfloat) i * (gfloat) G_PI * 0.4f;
			Vector3 p = { cosf (a) * 3.0f, 0.4f + 0.3f * sinf (a * 3.0f), sinf (a) * 3.0f };
			Vector3 s = { 0.5f, 0.5f, 0.5f };
			Color   c = ColorFromHSV (fmodf (hue + 150.0f + (gfloat) i * 25.0f, 360.0f), 0.6f, 1.0f);

			DrawModelEx (app->cube, p, axis, -spin * 2.0f, s, c);
		}
	}
	DrawCircle3D ((Vector3){ 0.0f, 0.01f, 0.0f }, 1.4f + bounce, (Vector3){ 1.0f, 0.0f, 0.0f }, 90.0f,
	              ColorAlpha (main_tint, 0.8f));
	grl_camera3d_end (app->camera);
}

static void
on_scene_move (LrgViewport *viewport,
               gfloat       u,
               gfloat       v,
               gfloat       dx,
               gfloat       dy,
               gpointer     user_data)
{
	Studio *app = user_data;

	if (!lrg_viewport_get_dragging (viewport))
		return;
	app->yaw -= dx * 0.01f;
	app->pitch = CLAMP (app->pitch + dy * 0.01f, -0.1f, 1.4f);
}

static void
on_scene_scroll (LrgViewport *viewport,
                 gfloat       u,
                 gfloat       v,
                 gfloat       scroll_x,
                 gfloat       scroll_y,
                 gpointer     user_data)
{
	Studio *app = user_data;

	app->distance = CLAMP (app->distance * (1.0f - scroll_y * 0.1f), 4.0f, 24.0f);
}

/* ==========================================================================
 * The C fallback: the same window as studio.yaml, built by hand
 * ========================================================================== */

static void
menu_item (GMenu       *menu,
           const gchar *label,
           const gchar *action,
           GVariant    *target,
           const gchar *icon)
{
	g_autoptr(GMenuItem) item = g_menu_item_new (label, NULL);

	g_menu_item_set_action_and_target_value (item, action, target);
	if (icon != NULL)
		g_menu_item_set_attribute (item, "icon", "s", icon);
	g_menu_append_item (menu, item);
}

/* build_menus_in_c: the "main" and "graph-context" menus of studio.yaml.
 * Accelerators are not set on items: the menus show the shortcut bound
 * to each action on the canvas */
static GMenuModel *
build_menus_in_c (GMenuModel **graph_context)
{
	GMenu            *main_menu = g_menu_new ();
	GMenu            *context = g_menu_new ();
	g_autoptr(GMenu)  file = g_menu_new ();
	g_autoptr(GMenu)  file_a = g_menu_new ();
	g_autoptr(GMenu)  file_b = g_menu_new ();
	g_autoptr(GMenu)  file_c = g_menu_new ();
	g_autoptr(GMenu)  recent = g_menu_new ();
	g_autoptr(GMenu)  edit = g_menu_new ();
	g_autoptr(GMenu)  edit_a = g_menu_new ();
	g_autoptr(GMenu)  edit_b = g_menu_new ();
	g_autoptr(GMenu)  view = g_menu_new ();
	g_autoptr(GMenu)  panels = g_menu_new ();
	g_autoptr(GMenu)  looks = g_menu_new ();
	g_autoptr(GMenu)  help = g_menu_new ();
	g_autoptr(GMenu)  add = g_menu_new ();
	guint             i;

	menu_item (file_a, "_New", "app.new", NULL, "plus");
	menu_item (file_a, "_Open...", "app.open", NULL, "folder");
	menu_item (recent, "forest.rlevel", "app.open-recent", g_variant_new_string ("forest.rlevel"), NULL);
	menu_item (recent, "cave.rlevel", "app.open-recent", g_variant_new_string ("cave.rlevel"), NULL);
	g_menu_append_submenu (file_a, "Open _Recent", G_MENU_MODEL (recent));
	g_menu_append_section (file, NULL, G_MENU_MODEL (file_a));
	menu_item (file_b, "_Save", "app.save", NULL, "download");
	menu_item (file_b, "Save _As...", "app.save-as", NULL, NULL);
	g_menu_append_section (file, NULL, G_MENU_MODEL (file_b));
	menu_item (file_c, "_Quit", "app.quit", NULL, "close");
	g_menu_append_section (file, NULL, G_MENU_MODEL (file_c));

	menu_item (edit_a, "_Undo", "app.undo", NULL, NULL);
	menu_item (edit_a, "_Redo", "app.redo", NULL, NULL);
	g_menu_append_section (edit, NULL, G_MENU_MODEL (edit_a));
	menu_item (edit_b, "Cu_t", "app.cut", NULL, NULL);
	menu_item (edit_b, "_Copy", "app.copy", NULL, NULL);
	menu_item (edit_b, "_Paste", "app.paste", NULL, NULL);
	g_menu_append_section (edit, NULL, G_MENU_MODEL (edit_b));

	menu_item (view, "Show _Grid", "app.toggle-grid", NULL, NULL);
	menu_item (view, "_Reset Layout", "app.reset-layout", NULL, "refresh");
	/* A radio submenu: app.canvas-look has a string state */
	for (i = 0; i < G_N_ELEMENTS (canvas_looks); i++)
		menu_item (looks, canvas_looks[i].label, "app.canvas-look", g_variant_new_string (canvas_looks[i].nick), NULL);
	g_menu_append_submenu (view, "Canvas _Look", G_MENU_MODEL (looks));
	menu_item (panels, "Scene", "app.show-panel", g_variant_new_string ("scene-panel"), "eye");
	menu_item (panels, "Graph", "app.show-panel", g_variant_new_string ("graph-panel"), NULL);
	menu_item (panels, "Script", "app.show-panel", g_variant_new_string ("script-panel"), "edit");
	menu_item (panels, "Blocks", "app.show-panel", g_variant_new_string ("blocks-panel"), "play");
	menu_item (panels, "Timeline", "app.show-panel", g_variant_new_string ("timeline-panel"), "clock");
	menu_item (panels, "Console", "app.show-panel", g_variant_new_string ("console-panel"), NULL);
	g_menu_append_section (view, "Panels", G_MENU_MODEL (panels));

	menu_item (help, "_About", "app.about", NULL, "info");

	g_menu_append_submenu (main_menu, "_File", G_MENU_MODEL (file));
	g_menu_append_submenu (main_menu, "_Edit", G_MENU_MODEL (edit));
	g_menu_append_submenu (main_menu, "_View", G_MENU_MODEL (view));
	g_menu_append_submenu (main_menu, "_Help", G_MENU_MODEL (help));

	menu_item (add, "Texture", "app.add-node", g_variant_new_string ("texture"), NULL);
	menu_item (add, "Mix", "app.add-node", g_variant_new_string ("mix"), NULL);
	menu_item (add, "Output", "app.add-node", g_variant_new_string ("output"), NULL);
	g_menu_append_submenu (context, "Add Node", G_MENU_MODEL (add));
	menu_item (context, "Frame All", "app.frame-all", NULL, "search");

	*graph_context = G_MENU_MODEL (context);
	return G_MENU_MODEL (main_menu);
}

static LrgDockPanel *
dock_panel (LrgDockArea     *dock,
            const gchar     *id,
            const gchar     *title,
            const gchar     *icon,
            LrgWidget       *child,
            LrgDockPanel    *relative,
            LrgDockPosition  position)
{
	LrgDockPanel *panel = lrg_dock_panel_new (id, title);

	lrg_widget_set_name (LRG_WIDGET (panel), id);
	if (icon != NULL)
		lrg_dock_panel_set_icon (panel, icon);
	lrg_dock_panel_set_child (panel, child);
	lrg_dock_area_add_panel (dock, panel, relative, position);
	g_object_unref (panel);
	return panel;
}

/* blocks_attach: a new block of @kind after @after (or into the first
 * mouth of @after with @into_mouth), with one slot value set */
static LrgBlock *
blocks_attach (LrgBlockEditor *editor,
               LrgBlock       *after,
               gboolean        into_mouth,
               const gchar    *kind,
               const gchar    *slot,
               const gchar    *value)
{
	LrgBlock *block = lrg_block_editor_create_block (editor, kind, 0.0f, 0.0f);

	if (slot != NULL)
		lrg_block_set_value_by_name (block, slot, value);
	lrg_block_editor_attach (editor, block, after,
	                         into_mouth ? LRG_BLOCK_PLACE_MOUTH : LRG_BLOCK_PLACE_NEXT, 0);
	return block;
}

/* build_blocks_in_c: the palette and sample program of studio-blocks.yaml,
 * through the C API (keep the two in step) */
static LrgBlockEditor *
build_blocks_in_c (void)
{
	static const struct { const gchar *id; LrgBlockShape shape; const gchar *category; const gchar *label; } kinds[] = {
		{ "when-play", LRG_BLOCK_SHAPE_HAT,       "events",  "when play pressed" },
		{ "spin",      LRG_BLOCK_SHAPE_STATEMENT, "motion",  "spin {deg:number=90} degrees" },
		{ "bounce",    LRG_BLOCK_SHAPE_STATEMENT, "motion",  "bounce {height:number=1}" },
		{ "orbit",     LRG_BLOCK_SHAPE_STATEMENT, "motion",  "orbit camera {deg:number=45}" },
		{ "repeat",    LRG_BLOCK_SHAPE_C,         "control", "repeat {times:number=4}" },
		{ "wait",      LRG_BLOCK_SHAPE_STATEMENT, "control", "wait {s:number=1} s" },
		{ "stop",      LRG_BLOCK_SHAPE_CAP,       "control", "stop" },
		{ "set-color", LRG_BLOCK_SHAPE_STATEMENT, "looks",   "set color {c:dropdown=red|green|blue|purple|gold}" },
		{ "say",       LRG_BLOCK_SHAPE_STATEMENT, "looks",   "say {msg:text=Hello}" }
	};
	LrgBlockEditor *editor = lrg_block_editor_new ();
	LrgBlock       *b;
	LrgBlock       *loop;
	guint           i;

	/* The palette: categories appear in the order kinds first use them */
	for (i = 0; i < G_N_ELEMENTS (kinds); i++)
	{
		g_autoptr(GError) error = NULL;

		if (lrg_block_editor_define_kind (editor, kinds[i].id, kinds[i].shape, kinds[i].category,
		                                  kinds[i].label, &error) == NULL)
			g_printerr ("ui-studio: block kind %s: %s\n", kinds[i].id, error->message);
	}

	/* The sample program, one script under a "when play pressed" hat */
	b = lrg_block_editor_create_block (editor, "when-play", 24.0f, 24.0f);
	b = blocks_attach (editor, b, FALSE, "set-color", "c", "blue");
	b = blocks_attach (editor, b, FALSE, "say", "msg", "Spinning up");
	loop = blocks_attach (editor, b, FALSE, "repeat", "times", "4");
	b = blocks_attach (editor, loop, TRUE, "spin", "deg", "90");
	blocks_attach (editor, b, FALSE, "wait", "s", "0.4");
	b = blocks_attach (editor, loop, FALSE, "set-color", "c", "gold");
	b = blocks_attach (editor, b, FALSE, "bounce", "height", "1");
	b = blocks_attach (editor, b, FALSE, "orbit", "deg", "60");
	blocks_attach (editor, b, FALSE, "say", "msg", "Done!");
	lrg_block_editor_set_scroll (editor, -16.0f, -16.0f);
	lrg_block_editor_clear_history (editor);   /* the sample is not an undo step */
	return editor;
}

/* build_ui_in_c: everything studio.yaml declares, in the same places
 * and with the same ids, so the wiring below works on either */
static LrgWidget *
build_ui_in_c (Studio *app)
{
	LrgVBox              *root = lrg_vbox_new ();
	g_autoptr(GMenuModel) main_menu = NULL;
	g_autoptr(GMenuModel) graph_context = NULL;
	g_autoptr(LrgSyntax)  language = NULL;
	g_autoptr(GError)     error = NULL;
	LrgDockPanel         *scene;
	LrgDockPanel         *graph;
	LrgDockPanel         *script;
	LrgDockPanel         *timeline;
	LrgTimelineViewTrack *camera;
	LrgTimelineViewTrack *track;
	LrgGraphNode         *n;
	GrlColor              c;

	lrg_widget_set_name (LRG_WIDGET (root), "studio");
	lrg_container_set_padding (LRG_CONTAINER (root), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (root), 0.0f);

	main_menu = build_menus_in_c (&graph_context);
	app->menubar = lrg_menu_bar_new_from_model (main_menu);
	lrg_widget_set_name (LRG_WIDGET (app->menubar), "menubar");
	lrg_container_add_child (LRG_CONTAINER (root), LRG_WIDGET (app->menubar));
	g_object_unref (app->menubar);

	app->dock = lrg_dock_area_new ();
	lrg_widget_set_name (LRG_WIDGET (app->dock), "dock");
	lrg_widget_set_fill_weight (LRG_WIDGET (app->dock), 1.0f);
	lrg_dock_area_set_divider_size (app->dock, 6.0f);
	lrg_container_add_child (LRG_CONTAINER (root), LRG_WIDGET (app->dock));
	g_object_unref (app->dock);

	/* Scene: a 640 x 360 render letterboxed into the panel */
	app->scene = lrg_viewport_new ();
	lrg_widget_set_name (LRG_WIDGET (app->scene), "scene");
	lrg_viewport_set_fit (app->scene, LRG_VIEWPORT_FIT_CONTAIN);
	lrg_viewport_set_fixed_resolution (app->scene, 640, 360);
	lrg_color_parse ("#11111b", &c);
	lrg_viewport_set_clear_color (app->scene, &c);
	scene = dock_panel (app->dock, "scene-panel", "Scene", "eye", LRG_WIDGET (app->scene), NULL,
	                    LRG_DOCK_POSITION_CENTER);
	lrg_dock_panel_set_closable (scene, FALSE);
	g_object_unref (app->scene);

	/* Graph: texture -> mix -> output, with a context menu */
	app->graph = lrg_node_graph_new ();
	lrg_widget_set_name (LRG_WIDGET (app->graph), "graph");
	lrg_node_graph_set_grid_style (app->graph, LRG_NODE_GRAPH_GRID_DOTS);
	lrg_node_graph_set_show_minimap (app->graph, TRUE);
	lrg_node_graph_set_animate_links (app->graph, TRUE);
	n = lrg_node_graph_add_node (app->graph, "tex", "Texture", 0.0f, 0.0f);
	lrg_graph_node_set_category (n, "texture");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "uv", "vec2");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "color", "vec4");
	n = lrg_node_graph_add_node (app->graph, "mix", "Mix", 260.0f, 60.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "a", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "b", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "out", "vec4");
	n = lrg_node_graph_add_node (app->graph, "out", "Output", 520.0f, 40.0f);
	lrg_color_parse ("#f38ba8", &c);
	lrg_graph_node_set_accent (n, &c);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "albedo", "vec4");
	lrg_node_graph_connect_by_name (app->graph, "tex", "color", "mix", "a", NULL);
	lrg_node_graph_connect_by_name (app->graph, "mix", "out", "out", "albedo", NULL);
	lrg_widget_set_context_menu (LRG_WIDGET (app->graph), graph_context);
	graph = dock_panel (app->dock, "graph-panel", "Graph", NULL, LRG_WIDGET (app->graph), scene,
	                    LRG_DOCK_POSITION_RIGHT);
	g_object_unref (app->graph);

	/* Script: the toy studio-script language */
	language = lrg_syntax_new_from_yaml (script_language, -1, &error);
	if (language != NULL)
		lrg_syntax_register (language);
	else
		g_printerr ("ui-studio: script language: %s\n", error->message);
	app->script = lrg_text_editor_new ();
	lrg_widget_set_name (LRG_WIDGET (app->script), "script");
	lrg_text_editor_set_language (app->script, "studio-script");
	lrg_text_editor_set_show_line_numbers (app->script, TRUE);
	lrg_text_editor_set_text (app->script, script_text);
	script = dock_panel (app->dock, "script-panel", "Script", "edit", LRG_WIDGET (app->script), graph,
	                     LRG_DOCK_POSITION_CENTER);
	g_object_unref (app->script);

	/* Blocks: the palette and program of studio-blocks.yaml, in C */
	app->blocks = build_blocks_in_c ();
	lrg_widget_set_name (LRG_WIDGET (app->blocks), "blocks");
	lrg_block_editor_set_palette_width (app->blocks, 170.0f);
	dock_panel (app->dock, "blocks-panel", "Blocks", "play", LRG_WIDGET (app->blocks), script,
	            LRG_DOCK_POSITION_CENTER);
	g_object_unref (app->blocks);

	/* Timeline: camera curve, audio clips, an event, three markers */
	app->timeline = lrg_timeline_view_new ();
	lrg_widget_set_name (LRG_WIDGET (app->timeline), "timeline");
	lrg_timeline_view_set_duration (app->timeline, 12.0);
	lrg_timeline_view_set_fps (app->timeline, 30.0);
	lrg_timeline_view_set_loop (app->timeline, TRUE);
	lrg_timeline_view_set_time_format (app->timeline, LRG_TIMELINE_VIEW_TIME_FORMAT_TIMECODE);
	camera = lrg_timeline_view_add_track (app->timeline, "Camera", NULL);
	lrg_color_parse ("#89b4fa", &c);
	lrg_timeline_view_track_set_color (camera, &c);
	track = lrg_timeline_view_add_track (app->timeline, "Position X", camera);
	lrg_timeline_view_track_set_curve_lane (track, TRUE);
	lrg_timeline_view_track_set_value_range (track, -10.0, 10.0);
	lrg_timeline_view_add_keyframe (app->timeline, track, 0.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, track, 4.0, 6.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, track, 8.0, -4.0, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_add_keyframe (app->timeline, track, 12.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_STEP);
	track = lrg_timeline_view_add_track (app->timeline, "Audio", NULL);
	lrg_color_parse ("#a6e3a1", &c);
	lrg_timeline_view_track_set_color (track, &c);
	lrg_timeline_view_add_clip (app->timeline, track, 0.0, 4.5, "Intro");
	lrg_color_parse ("#f9e2af", &c);
	lrg_timeline_view_set_clip_color (app->timeline,
	                                  lrg_timeline_view_add_clip (app->timeline, track, 5.0, 6.0, "Theme"), &c);
	track = lrg_timeline_view_add_track (app->timeline, "Events", NULL);
	lrg_timeline_view_add_clip (app->timeline, track, 2.0, 1.0, "Door");
	lrg_timeline_view_add_marker (app->timeline, 0.0, "Start");
	lrg_timeline_view_add_marker (app->timeline, 6.0, "Drop");
	lrg_timeline_view_add_marker (app->timeline, 11.5, "End");
	timeline = dock_panel (app->dock, "timeline-panel", "Timeline", "clock", LRG_WIDGET (app->timeline), NULL,
	                       LRG_DOCK_POSITION_BOTTOM);
	g_object_unref (app->timeline);

	/* Console: a read-only log */
	app->console = lrg_text_editor_new ();
	lrg_widget_set_name (LRG_WIDGET (app->console), "console");
	lrg_text_editor_set_read_only (app->console, TRUE);
	lrg_text_editor_set_text (app->console, "[info] built in C (studio.yaml not used)\n[info] 3 nodes, 2 links");
	dock_panel (app->dock, "console-panel", "Console", NULL, LRG_WIDGET (app->console), timeline,
	            LRG_DOCK_POSITION_CENTER);
	g_object_unref (app->console);

	/* The shortcuts studio.yaml declares under shortcuts: */
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl>n", "app.new", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl>o", "app.open", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl>s", "app.save", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl>q", "app.quit", NULL);
	lrg_canvas_add_shortcut (app->canvas, "space", "app.play", NULL);

	return LRG_WIDGET (root);
}

/* ==========================================================================
 * Loading studio.yaml
 * ========================================================================== */

/* find_studio_file: next to the sources, from the repository root,
 * examples/ or the build directory */
static gchar *
find_studio_file (void)
{
	static const gchar *candidates[] = {
		"data/ui/" STUDIO_FILE,
		"examples/data/ui/" STUDIO_FILE,
		"../../../data/ui/" STUDIO_FILE
	};
	g_autofree gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
	guint             i;

	for (i = 0; i < G_N_ELEMENTS (candidates); i++)
		if (g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
			return g_canonicalize_filename (candidates[i], NULL);
	if (exe != NULL)
	{
		g_autofree gchar *dir = g_path_get_dirname (exe);
		g_autofree gchar *path = g_build_filename (dir, "..", "..", "..", "data", "ui", STUDIO_FILE, NULL);

		if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
			return g_canonicalize_filename (path, NULL);
	}
	return NULL;
}

/* lookup: a widget by id from the loader, checked against its type */
static gpointer
lookup (Studio      *app,
        const gchar *id,
        GType        type)
{
	LrgWidget *widget = lrg_ui_loader_get_widget (app->loader, id);

	if (widget == NULL || !g_type_is_a (G_OBJECT_TYPE (widget), type))
	{
		g_printerr ("ui-studio: studio.yaml has no %s '%s'\n", g_type_name (type), id);
		return NULL;
	}
	return widget;
}

/* build_ui_from_yaml: load the document and pick the widgets out by id.
 * Returns NULL (and the caller builds in C) when the file is missing,
 * does not load, or lacks one of the ids the wiring needs */
static LrgWidget *
build_ui_from_yaml (Studio *app)
{
	g_autofree gchar  *path = find_studio_file ();
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgWidget) root = NULL;

	if (path == NULL)
		return NULL;
	app->loader = lrg_ui_loader_new ();
	root = lrg_ui_loader_load_from_file (app->loader, path, &error);
	if (root == NULL)
	{
		g_printerr ("ui-studio: %s: %s; building the window in C\n", path, error->message);
		return NULL;
	}

	app->menubar = lookup (app, "menubar", LRG_TYPE_MENU_BAR);
	app->dock = lookup (app, "dock", LRG_TYPE_DOCK_AREA);
	app->scene = lookup (app, "scene", LRG_TYPE_VIEWPORT);
	app->graph = lookup (app, "graph", LRG_TYPE_NODE_GRAPH);
	app->script = lookup (app, "script", LRG_TYPE_TEXT_EDITOR);
	app->timeline = lookup (app, "timeline", LRG_TYPE_TIMELINE_VIEW);
	app->console = lookup (app, "console", LRG_TYPE_TEXT_EDITOR);
	app->blocks = lookup (app, "blocks", LRG_TYPE_BLOCK_EDITOR);
	if (app->menubar == NULL || app->dock == NULL || app->scene == NULL || app->graph == NULL ||
	    app->script == NULL || app->timeline == NULL || app->console == NULL || app->blocks == NULL ||
	    !LRG_IS_CONTAINER (root))
	{
		g_printerr ("ui-studio: %s is incomplete; building the window in C\n", path);
		app->menubar = NULL;
		app->dock = NULL;
		app->scene = NULL;
		app->graph = NULL;
		app->script = NULL;
		app->timeline = NULL;
		app->console = NULL;
		app->blocks = NULL;
		return NULL;
	}
	app->from_yaml = TRUE;
	return g_steal_pointer (&root);
}

/* ==========================================================================
 * Actions ("app" on the canvas: replaces the built-in app group, so it
 * implements quit itself)
 * ========================================================================== */

static LrgDockPanel *
find_panel (Studio      *app,
            const gchar *id)
{
	LrgDockPanel *panel = lrg_dock_area_get_panel (app->dock, id);
	LrgWidget    *widget;

	if (panel != NULL)
		return panel;
	widget = app->loader != NULL ? lrg_ui_loader_get_widget (app->loader, id) : NULL;
	return (widget != NULL && LRG_IS_DOCK_PANEL (widget)) ? LRG_DOCK_PANEL (widget) : NULL;
}

/* notify_saved: announce a save with a desktop notification. Its button
 * is a detailed action on the canvas ("app.show-panel" with the console's
 * id), so the same action group that serves the menus answers it. With
 * no notification server (or --in-app-notifications) it becomes a toast
 * in the studio's queue, with the same button. */
static void
notify_saved (Studio      *app,
              const gchar *path)
{
	g_autoptr(LrgNotification) note = lrg_notification_new ("Layout saved");
	g_autoptr(GError)          error = NULL;
	const gchar               *home = g_get_home_dir ();
	g_autofree gchar          *shown = NULL;

	/* "~/..." keeps the body short enough for a toast */
	if (home != NULL && g_str_has_prefix (path, home) && path[strlen (home)] == G_DIR_SEPARATOR)
		shown = g_strconcat ("~", path + strlen (home), NULL);
	else
		shown = g_strdup (path);
	lrg_notification_set_body (note, shown);
	lrg_notification_set_icon_name (note, "document-save");
	lrg_notification_set_timeout (note, 6000);
	lrg_notification_add_button (note, "Show in console", "app.show-panel::console-panel");
	lrg_notification_set_toast_queue (note, app->toasts);
	if (!lrg_notification_show (note, LRG_WIDGET (app->canvas), &error))
		console_print (app, "[warn] notification: %s", error->message);
	/* note may go now: it keeps itself alive while shown */
}

/* save_layout_to: the dock arrangement as YAML at @path; with @graph also
 * the node graph, next to it as NAME-graph.yaml */
static gboolean
save_layout_to (Studio      *app,
                const gchar *path,
                gboolean     graph)
{
	g_autofree gchar  *yaml = lrg_dock_area_save_layout (app->dock);
	g_autofree gchar  *dir = g_path_get_dirname (path);
	g_autoptr(GError)  error = NULL;

	g_mkdir_with_parents (dir, 0755);
	if (!g_file_set_contents (path, yaml, -1, &error))
	{
		console_print (app, "[error] save layout: %s", error->message);
		return FALSE;
	}
	console_print (app, "[info] layout saved to %s", path);
	set_status (app, "Layout saved");

	if (graph)
	{
		g_autofree gchar *base = g_path_get_basename (path);
		g_autofree gchar *stem = g_str_has_suffix (base, ".yaml") ? g_strndup (base, strlen (base) - 5)
		                                                          : g_strdup (base);
		g_autofree gchar *name = g_strconcat (stem, "-graph.yaml", NULL);
		g_autofree gchar *graph_path = g_build_filename (dir, name, NULL);
		g_autofree gchar *text = lrg_node_graph_to_yaml (app->graph, NULL, NULL);

		if (text != NULL && g_file_set_contents (graph_path, text, -1, NULL))
			console_print (app, "[info] graph saved to %s", graph_path);
	}
	notify_saved (app, path);
	return TRUE;
}

/* load_layout: rearrange the existing panels from a saved layout */
static gboolean
load_layout (Studio      *app,
             const gchar *yaml)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_dock_area_load_layout (app->dock, yaml, &error))
	{
		console_print (app, "[error] load layout: %s", error->message);
		return FALSE;
	}
	return TRUE;
}

/* load_layout_file: read and apply a layout file */
static gboolean
load_layout_file (Studio      *app,
                  const gchar *path)
{
	g_autofree gchar  *yaml = NULL;
	g_autoptr(GError)  error = NULL;

	if (!g_file_get_contents (path, &yaml, NULL, &error))
	{
		console_print (app, "[warn] %s (Ctrl+S saves a layout)", error->message);
		return FALSE;
	}
	if (!load_layout (app, yaml))
		return FALSE;
	console_print (app, "[info] layout loaded from %s", path);
	set_status (app, "Layout loaded");
	return TRUE;
}

/* layout_dialog: an LrgFileDialog over the layout folder showing only
 * YAML files. In-app by default (a modal sheet on the canvas); with
 * --native-dialogs the XDG portal's chooser is tried first. */
static LrgFileDialog *
layout_dialog (Studio      *app,
               const gchar *title)
{
	LrgFileDialog            *dialog = lrg_file_dialog_new ();
	g_autoptr(LrgFileFilter)  layouts = lrg_file_filter_new ("Layouts (*.yaml)");
	g_autoptr(LrgFileFilter)  all = lrg_file_filter_new ("All files");
	g_autoptr(GListStore)     filters = g_list_store_new (LRG_TYPE_FILE_FILTER);
	g_autofree gchar         *dir_path = g_path_get_dirname (app->layout_path);
	g_autoptr(GFile)          dir = g_file_new_for_path (dir_path);

	/* The folder must exist for the browser to list it */
	g_mkdir_with_parents (dir_path, 0755);
	lrg_file_filter_add_suffix (layouts, "yaml");
	lrg_file_filter_add_suffix (layouts, "yml");
	lrg_file_filter_add_pattern (all, "*");
	g_list_store_append (filters, layouts);
	g_list_store_append (filters, all);

	lrg_file_dialog_set_title (dialog, title);
	lrg_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
	lrg_file_dialog_set_default_filter (dialog, layouts);
	lrg_file_dialog_set_initial_folder (dialog, dir);
	lrg_file_dialog_add_place (dialog, "Layouts", dir, LRG_ICON_STAR);
	lrg_file_dialog_set_prefer_native (dialog, app->native_dialogs);
	return dialog;
}

/* dialog_failed: report a file dialog error; backing out is silent */
static void
dialog_failed (Studio *app,
               GError *error)
{
	if (!g_error_matches (error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_DISMISSED))
		console_print (app, "[error] file dialog: %s", error->message);
	else
		set_status (app, "Cancelled");
}

static void
on_open_chosen (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
	Studio            *app = user_data;
	g_autoptr(GError)  error = NULL;
	g_autoptr(GFile)   file = lrg_file_dialog_open_finish (LRG_FILE_DIALOG (source), result, &error);
	g_autofree gchar  *path = NULL;

	if (file == NULL)
	{
		dialog_failed (app, error);
		return;
	}
	path = g_file_get_path (file);
	if (path != NULL)
		load_layout_file (app, path);
}

static void
on_save_chosen (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
	Studio            *app = user_data;
	g_autoptr(GError)  error = NULL;
	g_autoptr(GFile)   file = lrg_file_dialog_save_finish (LRG_FILE_DIALOG (source), result, &error);
	g_autofree gchar  *path = NULL;

	if (file == NULL)
	{
		dialog_failed (app, error);
		return;
	}
	path = g_file_get_path (file);
	if (path != NULL)
		save_layout_to (app, path, TRUE);
}

static void
on_new (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;

	load_layout (app, app->default_layout);
	lrg_text_editor_set_text (app->console, "[info] new session");
	lrg_timeline_view_set_time (app->timeline, 0.0);
	set_status (app, "New session");
}

/* on_open: File > Open..., a layout picked in a file dialog. The dialog
 * object can go right away: the operation holds its own reference. */
static void
on_open (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio                  *app = user_data;
	g_autoptr(LrgFileDialog) dialog = layout_dialog (app, "Open Layout");

	lrg_file_dialog_open (dialog, LRG_WIDGET (app->canvas), NULL, on_open_chosen, app);
}

/* on_open_recent: the File > Open Recent entries carry the file name as
 * their target. The studio has no level loader, so it reports the pick in
 * the console and status bar, which is where a real tool would hook in. */
static void
on_open_recent (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio      *app = user_data;
	const gchar *name;

	name = g_variant_get_string (parameter, NULL);
	console_print (app, "[info] open recent: %s", name);
	set_status (app, name);
}

/* on_save: File > Save, straight to the default layout file */
static void
on_save (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;

	save_layout_to (app, app->layout_path, FALSE);
}

/* on_save_as: File > Save As..., the layout (and the graph next to it)
 * wherever the save dialog says; it asks before replacing a file */
static void
on_save_as (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio                  *app = user_data;
	g_autoptr(LrgFileDialog) dialog = layout_dialog (app, "Save Layout As");

	lrg_file_dialog_set_initial_name (dialog, LAYOUT_FILE);
	lrg_file_dialog_save (dialog, LRG_WIDGET (app->canvas), NULL, on_save_chosen, app);
}

static void
on_quit (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;

	app->quit = TRUE;
}

static void
on_undo (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	lrg_text_editor_undo (((Studio *) user_data)->script);
}

static void
on_redo (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	lrg_text_editor_redo (((Studio *) user_data)->script);
}

static void
on_cut (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	lrg_text_editor_cut_clipboard (((Studio *) user_data)->script);
}

static void
on_copy (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	lrg_text_editor_copy_clipboard (((Studio *) user_data)->script);
}

static void
on_paste (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	lrg_text_editor_paste_clipboard (((Studio *) user_data)->script);
}

/* on_toggle_grid: a check item (boolean state) */
static void
on_toggle_grid (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;

	app->grid = !app->grid;
	g_simple_action_set_state (action, g_variant_new_boolean (app->grid));
}

static void
on_reset_layout (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;

	if (load_layout (app, app->default_layout))
		set_status (app, "Layout reset");
}

static void
on_show_panel (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio       *app = user_data;
	LrgDockPanel *panel = find_panel (app, g_variant_get_string (parameter, NULL));

	if (panel != NULL)
		lrg_dock_area_present_panel (app->dock, panel);
}

static void
on_about (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	console_print (user_data, "[about] ui-studio: libregnum's UI as a tool app (AGPL-3.0-or-later)");
}

static void
on_frame_all (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	lrg_node_graph_frame_all (((Studio *) user_data)->graph, TRUE);
}

/* on_play: toggle playback; the timeline advances the playhead itself
 * on the canvas clock */
static void blocks_run_hats (Studio *app);

static void
on_play (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio  *app = user_data;
	gboolean play = !lrg_timeline_view_get_playing (app->timeline);

	lrg_timeline_view_set_playing (app->timeline, play);
	if (play)
		blocks_run_hats (app);
}

/* on_canvas_look: View > Canvas Look, a radio (string state) over the
 * LrgCanvasLook nicks, applied to the node graph and the timeline */
static void
on_canvas_look (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio                *app = user_data;
	const gchar           *nick = g_variant_get_string (parameter, NULL);
	g_autoptr(GEnumClass)  klass = g_type_class_ref (LRG_TYPE_CANVAS_LOOK);
	GEnumValue            *value = g_enum_get_value_by_nick (klass, nick);

	if (value == NULL)
	{
		console_print (app, "[warn] unknown canvas look '%s' (auto, modern, blueprint, blender, neon)", nick);
		return;
	}
	lrg_node_graph_set_look (app->graph, (LrgCanvasLook) value->value);
	lrg_timeline_view_set_look (app->timeline, (LrgCanvasLook) value->value);
	g_simple_action_set_state (action, parameter);
	console_print (app, "[look] %s", nick);
}

/* on_add_node: a node of the chosen kind at the centre of the view */
static void
on_add_node (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio           *app = user_data;
	const gchar      *kind = g_variant_get_string (parameter, NULL);
	g_autofree gchar *id = g_strdup_printf ("%s-%u", kind, ++app->added_nodes);
	LrgGraphNode     *n;
	gfloat            cx;
	gfloat            cy;

	lrg_node_graph_get_center (app->graph, &cx, &cy);
	cx += 30.0f * (gfloat) (app->added_nodes % 6);
	cy += 30.0f * (gfloat) (app->added_nodes % 6);
	if (g_strcmp0 (kind, "texture") == 0)
	{
		n = lrg_node_graph_add_node (app->graph, id, "Texture", cx, cy);
		lrg_graph_node_set_category (n, "texture");
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "uv", "vec2");
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "color", "vec4");
	}
	else if (g_strcmp0 (kind, "mix") == 0)
	{
		n = lrg_node_graph_add_node (app->graph, id, "Mix", cx, cy);
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "a", "vec4");
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "b", "vec4");
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "out", "vec4");
	}
	else
	{
		n = lrg_node_graph_add_node (app->graph, id, "Output", cx, cy);
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "albedo", "vec4");
	}
	console_print (app, "[graph] added %s", id);
}

/* on_theme: Theme > preset, a radio; the canvas gets the preset through
 * the built-in ui.theme action */
static void
on_theme (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;

	if (lrg_widget_activate_action (LRG_WIDGET (app->canvas), "ui.theme",
	                                g_variant_new_string (g_variant_get_string (parameter, NULL))))
	{
		g_simple_action_set_state (action, parameter);
		console_print (app, "[theme] %s", g_variant_get_string (parameter, NULL));
	}
}

/* on_motion: Theme > Motion, a radio; through the built-in ui.motion */
static void
on_motion (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;

	if (lrg_widget_activate_action (LRG_WIDGET (app->canvas), "ui.motion",
	                                g_variant_new_string (g_variant_get_string (parameter, NULL))))
		g_simple_action_set_state (action, parameter);
}

/* on_run: "run" the script: count its statements and play */
static void
on_run (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	Studio *app = user_data;
	guint   lines = lrg_text_editor_get_line_count (app->script);
	guint   statements = 0;
	guint   i;

	for (i = 0; i < lines; i++)
	{
		g_autofree gchar *line = g_strstrip (g_strdup (lrg_text_editor_get_line (app->script, i)));

		if (line[0] != '\0' && line[0] != '#')
			statements++;
	}
	console_print (app, "[run] %u lines, %u statements; playing from the start", lines, statements);
	lrg_timeline_view_set_time (app->timeline, 0.0);
	lrg_timeline_view_set_playing (app->timeline, TRUE);
}

/* ==========================================================================
 * The block interpreter
 *
 * Runs one block per tick from the main loop (studio_update()), so a
 * script with waits and loops never blocks a frame. The state is a
 * program counter (the next block to run) and a stack of the "repeat"
 * blocks it is inside with their remaining iterations. The block being
 * run glows (lrg_block_set_running()). Any edit of the program stops the
 * run, so the block pointers held here never outlive their blocks.
 * ========================================================================== */

/* blocks_light: move the execution glow to @block (NULL: off) */
static void
blocks_light (Studio   *app,
              LrgBlock *block)
{
	lrg_block_editor_clear_running (app->blocks);
	if (block != NULL)
		lrg_block_set_running (block, TRUE);
}

static void
blocks_finish (Studio      *app,
               const gchar *why)
{
	if (!app->running)
		return;
	app->running = FALSE;
	app->pc = NULL;
	g_array_set_size (app->loops, 0);
	blocks_light (app, NULL);
	console_print (app, "[blocks] %s after %u blocks", why, app->run_steps);
	set_status (app, "Script finished");
}

/* blocks_run: start running the script under @top (stops a running one) */
static void
blocks_run (Studio   *app,
            LrgBlock *top)
{
	blocks_finish (app, "interrupted");
	app->running = TRUE;
	app->pc = top;
	app->run_delay = 0.0;
	app->run_steps = 0;
	console_print (app, "[blocks] running '%s'", lrg_block_kind_get_label (lrg_block_get_kind (top)));
	set_status (app, "Running blocks...");
}

/* blocks_run_hats: run the first "when play pressed" script (Space) */
static void
blocks_run_hats (Studio *app)
{
	guint i;

	for (i = 0; i < lrg_block_editor_get_n_scripts (app->blocks); i++)
	{
		LrgBlock *top = lrg_block_editor_get_nth_script (app->blocks, i);

		if (g_strcmp0 (lrg_block_get_kind_id (top), "when-play") == 0)
		{
			blocks_run (app, top);
			return;
		}
	}
}

/* blocks_hue: the hue of a "set color" choice */
static gfloat
blocks_hue (const gchar *name)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (block_colors); i++)
		if (g_strcmp0 (block_colors[i].name, name) == 0)
			return block_colors[i].hue;
	return 260.0f;
}

/* blocks_step: run one block. Returns the seconds it takes (the time
 * until the next step), or a negative value when the run ended. */
static gdouble
blocks_step (Studio *app)
{
	LrgBlock    *block;
	const gchar *id;
	gdouble      cost = BLOCK_STEP;

	/* Fell off the end of a stack: finish, or go round the innermost
	 * loop again, or leave it and carry on below it */
	while (app->pc == NULL)
	{
		BlockLoop *frame;

		if (app->loops->len == 0)
		{
			blocks_finish (app, "finished");
			return -1.0;
		}
		frame = &g_array_index (app->loops, BlockLoop, app->loops->len - 1);
		if (--frame->left > 0)
			app->pc = lrg_block_get_mouth (frame->loop, 0);
		else
		{
			app->pc = lrg_block_get_next (frame->loop);
			g_array_set_size (app->loops, app->loops->len - 1);
		}
	}

	/* Run the block at the program counter */
	block = app->pc;
	id = lrg_block_get_kind_id (block);
	app->pc = lrg_block_get_next (block);
	app->run_steps++;
	blocks_light (app, block);

	if (g_strcmp0 (id, "when-play") == 0)
		cost = BLOCK_STEP * 0.5;
	else if (g_strcmp0 (id, "spin") == 0)
		app->block_spin_target += (gfloat) lrg_block_get_number (block, "deg");
	else if (g_strcmp0 (id, "bounce") == 0)
	{
		app->bounce_height = (gfloat) lrg_block_get_number (block, "height");
		app->bounce_time = 0.0f;
		cost = MAX (cost, (gdouble) BOUNCE_TIME);
	}
	else if (g_strcmp0 (id, "orbit") == 0)
		app->orbit_left += (gfloat) lrg_block_get_number (block, "deg") * ((gfloat) G_PI / 180.0f);
	else if (g_strcmp0 (id, "repeat") == 0)
	{
		gint      times = (gint) lrg_block_get_number (block, "times");
		LrgBlock *mouth = lrg_block_get_mouth (block, 0);

		/* An empty or zero-times loop is skipped; otherwise enter it */
		if (times > 0 && mouth != NULL)
		{
			BlockLoop frame = { block, MIN (times, 10000) };

			g_array_append_val (app->loops, frame);
			app->pc = mouth;
		}
		cost = BLOCK_STEP * 0.5;
	}
	else if (g_strcmp0 (id, "wait") == 0)
		cost = CLAMP (lrg_block_get_number (block, "s"), 0.0, 60.0);
	else if (g_strcmp0 (id, "set-color") == 0)
	{
		app->block_tinted = TRUE;
		app->block_hue = blocks_hue (lrg_block_get_value_by_name (block, "c"));
	}
	else if (g_strcmp0 (id, "say") == 0)
		console_print (app, "[blocks] say: %s", lrg_block_get_value_by_name (block, "msg"));
	else if (g_strcmp0 (id, "stop") == 0)
	{
		blocks_finish (app, "stopped by a stop block");
		return -1.0;
	}
	return cost;
}

/* on_block_activated: double-click / Enter on a script runs it */
static void
on_block_activated (LrgBlockEditor *editor,
                    LrgBlock       *top,
                    gpointer        user_data)
{
	blocks_run (user_data, top);
}

/* on_blocks_changed: an edit invalidates the running program */
static void
on_blocks_changed (LrgBlockEditor *editor,
                   gpointer        user_data)
{
	blocks_finish (user_data, "stopped (the program was edited)");
}

/* studio_update: per-frame animation of what the blocks set in motion,
 * then the interpreter's clock */
static void
studio_update (Studio *app,
               gfloat  dt)
{
	gfloat swing;

	app->block_spin += (app->block_spin_target - app->block_spin) * MIN (1.0f, dt * 8.0f);
	if (app->bounce_time < BOUNCE_TIME)
		app->bounce_time += dt;
	swing = app->orbit_left * MIN (1.0f, dt * 5.0f);
	app->yaw += swing;
	app->orbit_left -= swing;

	if (!app->running)
		return;
	app->run_delay -= dt;
	while (app->running && app->run_delay <= 0.0)
	{
		gdouble cost = blocks_step (app);

		if (cost < 0.0)
			break;
		app->run_delay += cost;
	}
}

/* ==========================================================================
 * Console commands
 * ========================================================================== */

static void
run_command (Studio      *app,
             const gchar *command)
{
	g_auto(GStrv)  argv = g_strsplit (command, " ", 2);
	const gchar   *verb = argv[0];
	const gchar   *arg = argv[1] != NULL ? g_strstrip (argv[1]) : NULL;
	GActionGroup  *group = G_ACTION_GROUP (app->actions);

	console_print (app, "> %s", command);
	if (g_strcmp0 (verb, "help") == 0)
	{
		console_print (app, "play | pause | seek SECONDS | rate X     timeline playback");
		console_print (app, "theme NAME | motion NICK                 look and feel");
		console_print (app, "save | load | reset                      dock layout (default file)");
		console_print (app, "open | save-as                           file dialogs");
		console_print (app, "panel ID | node KIND | frame             panels and graph");
		console_print (app, "look NICK                                canvas look (auto, modern, blueprint, ...)");
		console_print (app, "blocks | stop                            run / stop the block script");
		console_print (app, "run | clear | quit");
	}
	else if (g_strcmp0 (verb, "play") == 0)
		lrg_timeline_view_set_playing (app->timeline, TRUE);
	else if (g_strcmp0 (verb, "pause") == 0)
		lrg_timeline_view_set_playing (app->timeline, FALSE);
	else if (g_strcmp0 (verb, "seek") == 0 && arg != NULL)
		lrg_timeline_view_set_time (app->timeline, g_ascii_strtod (arg, NULL));
	else if (g_strcmp0 (verb, "rate") == 0 && arg != NULL)
		lrg_timeline_view_set_rate (app->timeline, g_ascii_strtod (arg, NULL));
	else if (g_strcmp0 (verb, "theme") == 0 && arg != NULL)
		g_action_group_activate_action (group, "theme", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "motion") == 0 && arg != NULL)
		g_action_group_activate_action (group, "motion", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "save") == 0)
		g_action_group_activate_action (group, "save", NULL);
	else if (g_strcmp0 (verb, "load") == 0)
		load_layout_file (app, app->layout_path);
	else if (g_strcmp0 (verb, "open") == 0)
		g_action_group_activate_action (group, "open", NULL);
	else if (g_strcmp0 (verb, "save-as") == 0)
		g_action_group_activate_action (group, "save-as", NULL);
	else if (g_strcmp0 (verb, "look") == 0 && arg != NULL)
		g_action_group_activate_action (group, "canvas-look", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "blocks") == 0)
		blocks_run_hats (app);
	else if (g_strcmp0 (verb, "stop") == 0)
		blocks_finish (app, "stopped");
	else if (g_strcmp0 (verb, "reset") == 0)
		g_action_group_activate_action (group, "reset-layout", NULL);
	else if (g_strcmp0 (verb, "panel") == 0 && arg != NULL)
		g_action_group_activate_action (group, "show-panel", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "node") == 0 && arg != NULL)
		g_action_group_activate_action (group, "add-node", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "frame") == 0)
		g_action_group_activate_action (group, "frame-all", NULL);
	else if (g_strcmp0 (verb, "run") == 0)
		g_action_group_activate_action (group, "run", NULL);
	else if (g_strcmp0 (verb, "clear") == 0)
		lrg_text_editor_set_text (app->console, "");
	else if (g_strcmp0 (verb, "quit") == 0)
		g_action_group_activate_action (group, "quit", NULL);
	else
		console_print (app, "unknown command '%s' (try help)", verb);
}

static void
on_console_submit (LrgTextEditor *input,
                   gpointer       user_data)
{
	g_autofree gchar *text = lrg_text_editor_get_text (input);

	g_strstrip (text);
	if (text[0] != '\0')
		run_command (user_data, text);
	lrg_text_editor_set_text (input, "");
}

/* ==========================================================================
 * Wiring: the same for the YAML and the C window
 * ========================================================================== */

/* add_theme_menu: insert Theme (presets + motion) before Help. The bar's
 * model is copied item by item into a new GMenu, so this works whatever
 * GMenuModel implementation the loader produced */
static void
add_theme_menu (Studio *app)
{
	GMenuModel          *old = lrg_menu_bar_get_model (app->menubar);
	g_autoptr(GMenu)     bar = g_menu_new ();
	g_autoptr(GMenu)     theme = g_menu_new ();
	g_autoptr(GMenu)     presets = g_menu_new ();
	g_autoptr(GMenu)     motion = g_menu_new ();
	const gchar * const *names = lrg_theme_list_presets ();
	gint                 n = old != NULL ? g_menu_model_get_n_items (old) : 0;
	gint                 i;

	for (i = 0; names[i] != NULL; i++)
		menu_item (presets, names[i], "app.theme", g_variant_new_string (names[i]), NULL);
	g_menu_append_section (theme, "Preset", G_MENU_MODEL (presets));
	for (i = 0; i < (gint) G_N_ELEMENTS (motion_nicks); i++)
	{
		g_autofree gchar *label = g_strdup (motion_nicks[i]);

		label[0] = g_ascii_toupper (label[0]);
		menu_item (motion, label, "app.motion", g_variant_new_string (motion_nicks[i]), NULL);
	}
	g_menu_append_section (theme, "Motion", G_MENU_MODEL (motion));

	for (i = 0; i < n; i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new_from_model (old, i);

		if (i == n - 1)
			g_menu_append_submenu (bar, "_Theme", G_MENU_MODEL (theme));
		g_menu_append_item (bar, item);
	}
	if (n == 0)
		g_menu_append_submenu (bar, "_Theme", G_MENU_MODEL (theme));
	lrg_menu_bar_set_model (app->menubar, G_MENU_MODEL (bar));
}

static void
install_actions (Studio      *app,
                 const gchar *theme)
{
	const GActionEntry entries[] = {
		{ "new",          on_new,          NULL, NULL,       NULL, { 0 } },
		{ "open",         on_open,         NULL, NULL,       NULL, { 0 } },
		{ "open-recent",  on_open_recent,  "s",  NULL,       NULL, { 0 } },
		{ "save",         on_save,         NULL, NULL,       NULL, { 0 } },
		{ "save-as",      on_save_as,      NULL, NULL,       NULL, { 0 } },
		{ "quit",         on_quit,         NULL, NULL,       NULL, { 0 } },
		{ "undo",         on_undo,         NULL, NULL,       NULL, { 0 } },
		{ "redo",         on_redo,         NULL, NULL,       NULL, { 0 } },
		{ "cut",          on_cut,          NULL, NULL,       NULL, { 0 } },
		{ "copy",         on_copy,         NULL, NULL,       NULL, { 0 } },
		{ "paste",        on_paste,        NULL, NULL,       NULL, { 0 } },
		{ "toggle-grid",  on_toggle_grid,  NULL, "true",     NULL, { 0 } },
		{ "reset-layout", on_reset_layout, NULL, NULL,       NULL, { 0 } },
		{ "show-panel",   on_show_panel,   "s",  NULL,       NULL, { 0 } },
		{ "about",        on_about,        NULL, NULL,       NULL, { 0 } },
		{ "frame-all",    on_frame_all,    NULL, NULL,       NULL, { 0 } },
		{ "play",         on_play,         NULL, NULL,       NULL, { 0 } },
		{ "add-node",     on_add_node,     "s",  NULL,       NULL, { 0 } },
		{ "theme",        on_theme,        "s",  "'midnight'", NULL, { 0 } },
		{ "motion",       on_motion,       "s",  "'smooth'", NULL, { 0 } },
		{ "run",          on_run,          NULL, NULL,       NULL, { 0 } },
		{ "canvas-look",  on_canvas_look,  "s",  "'auto'",   NULL, { 0 } }
	};

	app->actions = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (app->actions), entries, G_N_ELEMENTS (entries), app);
	g_simple_action_set_state (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (app->actions), "theme")),
	                           g_variant_new_string (theme));
	lrg_widget_insert_action_group (LRG_WIDGET (app->canvas), "app", G_ACTION_GROUP (app->actions));

	/* Shortcuts beyond the document's own */
	lrg_canvas_add_shortcut (app->canvas, "F5", "app.run", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl><Shift>s", "app.save-as", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl>z", "app.undo", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl><Shift>z", "app.redo", NULL);
	lrg_canvas_add_shortcut (app->canvas, "Home", "app.frame-all", NULL);
}

/* add_animation_tracks: a "Cube" group whose curves the scene samples:
 * spin (degrees), hue (degrees) and bounce (height) */
static void
add_animation_tracks (Studio *app)
{
	LrgTimelineViewTrack *group = lrg_timeline_view_add_track (app->timeline, "Cube", NULL);
	GrlColor              c;
	guint                 i;

	lrg_color_parse ("#cba6f7", &c);
	lrg_timeline_view_track_set_color (group, &c);

	app->track_spin = lrg_timeline_view_add_track (app->timeline, "Spin", group);
	lrg_timeline_view_track_set_curve_lane (app->track_spin, TRUE);
	lrg_timeline_view_track_set_curve_height (app->track_spin, 34.0f);
	lrg_timeline_view_track_set_value_range (app->track_spin, 0.0, 720.0);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_spin, 0.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_spin, 6.0, 360.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_spin, 12.0, 720.0, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);

	app->track_hue = lrg_timeline_view_add_track (app->timeline, "Hue", group);
	lrg_color_parse ("#fab387", &c);
	lrg_timeline_view_track_set_color (app->track_hue, &c);
	lrg_timeline_view_track_set_curve_lane (app->track_hue, TRUE);
	lrg_timeline_view_track_set_curve_height (app->track_hue, 34.0f);
	lrg_timeline_view_track_set_value_range (app->track_hue, 0.0, 360.0);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_hue, 0.0, 260.0, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_hue, 6.0, 330.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_hue, 12.0, 260.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);

	app->track_bounce = lrg_timeline_view_add_track (app->timeline, "Bounce", group);
	lrg_color_parse ("#a6e3a1", &c);
	lrg_timeline_view_track_set_color (app->track_bounce, &c);
	lrg_timeline_view_track_set_curve_lane (app->track_bounce, TRUE);
	lrg_timeline_view_track_set_curve_height (app->track_bounce, 34.0f);
	lrg_timeline_view_track_set_value_range (app->track_bounce, 0.0, 1.0);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_bounce, 0.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_bounce, 2.0, 1.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_bounce, 3.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_bounce, 6.0, 0.6, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_bounce, 7.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_bounce, 10.0, 0.8, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_bounce, 12.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);

	/* The camera curve still drives the scene; fold its group away so
	 * the cube's curves are in view */
	/* Compact rows so the clips and all three curves fit the panel */
	for (i = 0; i < lrg_timeline_view_get_n_tracks (app->timeline); i++)
		lrg_timeline_view_track_set_height (lrg_timeline_view_get_track (app->timeline, i), 28.0f);

	app->track_camera = lrg_timeline_view_find_track (app->timeline, "Position X");
	if (app->track_camera != NULL && lrg_timeline_view_track_get_parent (app->track_camera) != NULL)
		lrg_timeline_view_track_set_collapsed (lrg_timeline_view_track_get_parent (app->track_camera), TRUE);
}

/* add_console_input: put a submit-on-enter input under the console log.
 * The panel's child becomes an anchor layout: the log fills it minus a
 * strip at the bottom, the input stretches along the bottom edge. (A
 * vbox would not do: it never shrinks the log below its natural 12 rows,
 * which would push the input out of a short panel.) */
static void
add_console_input (Studio *app)
{
	LrgWidget       *log = LRG_WIDGET (app->console);
	LrgWidget       *parent = LRG_WIDGET (lrg_widget_get_parent (log));
	LrgAnchorLayout *box = lrg_anchor_layout_new ();

	app->input = lrg_text_editor_new ();
	lrg_text_editor_set_submit_on_enter (app->input, TRUE);
	lrg_text_editor_set_placeholder (app->input, "Command, e.g. help");
	lrg_text_editor_set_highlight_current_line (app->input, FALSE);
	lrg_widget_set_min_size (LRG_WIDGET (app->input), 0.0f, 38.0f);
	lrg_widget_set_max_size (LRG_WIDGET (app->input), 0.0f, 38.0f);
	g_signal_connect (app->input, "submit", G_CALLBACK (on_console_submit), app);

	lrg_container_set_padding (LRG_CONTAINER (box), 0.0f);
	lrg_text_editor_set_max_lines (app->console, 1000);
	lrg_text_editor_set_wrap (app->console, TRUE);
	lrg_text_editor_set_highlight_current_line (app->console, FALSE);

	/* Move the log from the panel into the box, keeping it alive */
	g_object_ref (log);
	if (parent != NULL && LRG_IS_DOCK_PANEL (parent))
		lrg_dock_panel_set_child (LRG_DOCK_PANEL (parent), LRG_WIDGET (box));
	else if (parent != NULL && LRG_IS_CONTAINER (parent))
		lrg_container_remove_child (LRG_CONTAINER (parent), log);
	lrg_anchor_layout_add (box, log, LRG_ANCHOR_PRESET_FILL, 0.0f, 0.0f);
	lrg_anchor_layout_set_child_offsets (box, log, 0.0f, 0.0f, 0.0f, 44.0f);
	lrg_anchor_layout_add (box, LRG_WIDGET (app->input), LRG_ANCHOR_PRESET_BOTTOM_STRETCH, 0.0f, 0.0f);
	g_object_unref (app->input);
	g_object_unref (log);
	g_object_unref (box);
}

/* add_status_bar: status text on the left, the playhead time on the right */
static void
add_status_bar (Studio *app)
{
	LrgPanel *bar = lrg_panel_new ();
	LrgHBox  *row = lrg_hbox_new ();

	lrg_container_set_padding (LRG_CONTAINER (bar), 6.0f);
	lrg_panel_set_fill_children (bar, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (row), 0.0f);
	app->status = lrg_label_new (app->from_yaml ? "Loaded studio.yaml" : "Built in C");
	lrg_widget_add_class (LRG_WIDGET (app->status), "muted");
	lrg_widget_set_fill_weight (LRG_WIDGET (app->status), 1.0f);
	app->clock = lrg_label_new ("");
	lrg_widget_add_class (LRG_WIDGET (app->clock), "muted");
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (app->status));
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (app->clock));
	lrg_container_add_child (LRG_CONTAINER (bar), LRG_WIDGET (row));
	lrg_container_add_child (LRG_CONTAINER (app->root), LRG_WIDGET (bar));
	g_object_unref (app->status);
	g_object_unref (app->clock);
	g_object_unref (row);
	g_object_unref (bar);
}

static void
on_layout_changed (LrgDockArea *dock,
                   gpointer     user_data)
{
	set_status (user_data, "Layout changed (Ctrl+S saves it)");
}

static void
wire (Studio      *app,
      const gchar *theme,
      gboolean     restore)
{
	g_autofree gchar *saved = NULL;

	install_actions (app, theme);
	add_theme_menu (app);
	add_animation_tracks (app);
	add_console_input (app);
	add_status_bar (app);

	/* The scene: render function, orbit and zoom */
	app->camera = grl_camera3d_new ();
	grl_camera3d_set_fovy (app->camera, 45.0f);
	app->yaw = 0.6f;
	app->pitch = 0.42f;
	app->distance = 9.0f;
	app->grid = TRUE;
	if (IsWindowReady ())
	{
		app->cube = load_shaded_cube ();
		app->cube_loaded = TRUE;
	}
	lrg_viewport_set_render_func (app->scene, draw_scene, app, NULL);
	lrg_viewport_set_continuous (app->scene, TRUE);
	g_signal_connect (app->scene, "pointer-move", G_CALLBACK (on_scene_move), app);
	g_signal_connect (app->scene, "pointer-scroll", G_CALLBACK (on_scene_scroll), app);

	/* Proportions: scene wide, graph beside it, timeline a third high */
	lrg_dock_area_set_panel_share (app->dock, find_panel (app, "timeline-panel"), 0.44f);
	lrg_dock_area_set_panel_share (app->dock, find_panel (app, "scene-panel"), 0.56f);
	lrg_dock_area_present_panel (app->dock, find_panel (app, "graph-panel"));
	lrg_dock_area_present_panel (app->dock, find_panel (app, "timeline-panel"));
	g_signal_connect (app->dock, "layout-changed", G_CALLBACK (on_layout_changed), app);

	/* Blocks: double-click a script to run it; edits stop a run */
	app->loops = g_array_new (FALSE, FALSE, sizeof (BlockLoop));
	g_signal_connect (app->blocks, "block-activated", G_CALLBACK (on_block_activated), app);
	g_signal_connect (app->blocks, "changed", G_CALLBACK (on_blocks_changed), app);

	/* Toasts in the overlay layer (above dialogs and their veil): the
	 * in-app fallback for notifications, also the process default */
	app->toasts = lrg_toast_queue_new ();
	lrg_canvas_add_overlay (app->canvas, LRG_WIDGET (app->toasts));
	lrg_notification_set_default_toast_queue (app->toasts);

	/* The layout as built is the "reset" layout; a saved one is restored */
	app->default_layout = lrg_dock_area_save_layout (app->dock);
	if (restore && g_file_get_contents (app->layout_path, &saved, NULL, NULL) && load_layout (app, saved))
		console_print (app, "[info] restored the layout saved in %s", app->layout_path);

	lrg_timeline_view_set_playing (app->timeline, TRUE);
	console_print (app, "[info] window %s; type 'help' below", app->from_yaml ? "from studio.yaml" : "built in C");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GrlWindow)      window = NULL;
	g_autoptr(LrgTheme)       preset = NULL;
	g_autofree gchar         *opt_theme = NULL;
	g_autofree gchar         *opt_screenshot = NULL;
	gint                      opt_frames = 90;
	gboolean                  opt_license = FALSE;
	gboolean                  opt_c_only = FALSE;
	gboolean                  opt_default_layout = FALSE;
	gboolean                  opt_native = FALSE;
	gboolean                  opt_in_app = FALSE;
	g_autofree gchar         *opt_look = NULL;
	gint                      opt_menu = 0;
	g_auto(GStrv)             opt_exec = NULL;
	Studio                    app = { 0 };
	LrgWidget                *root;
	gint                      frame = 0;

	GOptionEntry entries[] = {
		{ "theme", 't', 0, G_OPTION_ARG_STRING, &opt_theme,
		  "Theme preset (midnight, daylight, catppuccin-mocha, catppuccin-latte, high-contrast)", "NAME" },
		{ "c-only", 0, 0, G_OPTION_ARG_NONE, &opt_c_only, "Build the window in C even if studio.yaml exists", NULL },
		{ "default-layout", 0, 0, G_OPTION_ARG_NONE, &opt_default_layout,
		  "Do not restore the layout saved with Ctrl+S", NULL },
		{ "look", 'l', 0, G_OPTION_ARG_STRING, &opt_look,
		  "Canvas look of the graph and timeline (auto, modern, blueprint, blender, neon)", "NICK" },
		{ "native-dialogs", 0, 0, G_OPTION_ARG_NONE, &opt_native,
		  "Open and Save As try the desktop's file chooser (XDG portal) first", NULL },
		{ "in-app-notifications", 0, 0, G_OPTION_ARG_NONE, &opt_in_app,
		  "Show notifications as in-app toasts, never on the desktop", NULL },
		{ "menu", 0, 0, G_OPTION_ARG_INT, &opt_menu, "Open menu N of the menu bar (1 = File) on frame 10", "N" },
		{ "exec", 'e', 0, G_OPTION_ARG_STRING_ARRAY, &opt_exec,
		  "Run a console command at start-up (repeatable)", "COMMAND" },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit (implies --default-layout)", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames, "Frames to run before --screenshot (default 90)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- a docked tool window built on the libregnum UI");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  ui-studio\n"
		"  ui-studio --theme catppuccin-latte\n"
		"  ui-studio --c-only                   (ignore examples/data/ui/studio.yaml)\n"
		"  ui-studio --default-layout           (do not restore the saved dock layout)\n"
		"  ui-studio --theme daylight --screenshot studio.png --frames 120\n"
		"  ui-studio --menu 4 --screenshot theme-menu.png\n"
		"  ui-studio --exec 'theme catppuccin-latte' --exec 'seek 6'\n"
		"  ui-studio --look blueprint             (also View > Canvas Look)\n"
		"  ui-studio --exec 'panel blocks-panel' --exec blocks   (run the block script)\n"
		"  ui-studio --native-dialogs             (File > Open uses the desktop chooser)\n"
		"  ui-studio --in-app-notifications --exec save   (the save toast)\n"
		"\n"
		"Keys: Ctrl+Q quit, Space play/pause (and run the blocks), Ctrl+S save layout,\n"
		"Ctrl+Shift+S save as, Ctrl+O open a layout, Ctrl+N reset, F5 run the script,\n"
		"Home frames the graph. Double-click a block script to run it. Console: type 'help'.\n"
		"The layout file is $XDG_CACHE_HOME/libregnum/" LAYOUT_FILE ".\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("ui-studio: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
	window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "libregnum UI studio");
	grl_window_set_target_fps (window, 60);

	/* The start-up theme is the default; the Theme menu then puts presets
	 * on the canvas through ui.theme */
	if (opt_theme == NULL)
		opt_theme = g_strdup ("midnight");
	preset = lrg_theme_new_preset (opt_theme);
	if (preset == NULL)
	{
		g_printerr ("ui-studio: unknown theme '%s'\n", opt_theme);
		g_free (opt_theme);
		opt_theme = g_strdup ("midnight");
		preset = lrg_theme_new_preset (opt_theme);
	}
	lrg_theme_set_default (preset);

	app.canvas = lrg_canvas_new ();
	lrg_canvas_set_paint_background (app.canvas, TRUE);
	app.native_dialogs = opt_native;
	if (opt_in_app)
		lrg_notification_set_force_in_app (TRUE);
	app.layout_path = g_build_filename (g_get_user_cache_dir (), "libregnum", LAYOUT_FILE, NULL);

	/* The window: studio.yaml if it loads, else the same thing in C */
	root = opt_c_only ? NULL : build_ui_from_yaml (&app);
	if (root == NULL)
		root = build_ui_in_c (&app);
	app.root = root;
	lrg_container_add_child (LRG_CONTAINER (app.canvas), root);
	g_object_unref (root);
	wire (&app, opt_theme, !opt_default_layout && opt_screenshot == NULL);
	if (opt_look != NULL)
		g_action_group_activate_action (G_ACTION_GROUP (app.actions), "canvas-look", g_variant_new_string (opt_look));

	while (!grl_window_should_close (window) && !app.quit)
	{
		gfloat            w = (gfloat) GetScreenWidth ();
		gfloat            h = (gfloat) GetScreenHeight ();
		g_autofree gchar *clock = NULL;

		lrg_widget_set_size (LRG_WIDGET (app.canvas), w, h);
		lrg_widget_set_size (app.root, w, h);
		lrg_widget_set_size (LRG_WIDGET (app.toasts), w, h);

		/* GIO work: file dialog folder listings, notification D-Bus
		 * replies and portal responses complete on the main context */
		g_main_context_iteration (NULL, FALSE);

		/* Input, then the canvas clock: springs, timeline playback and
		 * the viewport's render into its texture; then the blocks */
		lrg_canvas_handle_input (app.canvas);
		studio_update (&app, GetFrameTime ());
		if (opt_menu > 0 && frame == 10)
			lrg_menu_bar_open_menu (app.menubar, (guint) (opt_menu - 1), TRUE);
		if (frame == 2)
			lrg_node_graph_frame_all (app.graph, FALSE);
		if (frame == 5 && opt_exec != NULL)
		{
			guint i;

			for (i = 0; opt_exec[i] != NULL; i++)
				run_command (&app, opt_exec[i]);
		}

		clock = lrg_timeline_view_format_time (app.timeline, lrg_timeline_view_get_time (app.timeline));
		lrg_label_set_text (app.clock, clock);

		grl_window_begin_drawing (window);
		lrg_canvas_render (app.canvas);
		if (opt_screenshot != NULL && frame >= opt_frames)
		{
			Image shot = LoadImageFromScreen ();

			ExportImage (shot, opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	if (app.cube_loaded)
		UnloadModel (app.cube);
	g_clear_object (&app.camera);
	g_clear_object (&app.actions);
	g_clear_object (&app.toasts);
	g_clear_object (&app.canvas);
	g_clear_object (&app.loader);
	g_clear_pointer (&app.loops, g_array_unref);
	g_free (app.default_layout);
	g_free (app.layout_path);
	return 0;
}
