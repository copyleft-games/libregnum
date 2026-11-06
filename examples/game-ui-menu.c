/* game-ui-menu.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Game-style UI over a live scene: an animated title menu, a settings
 * card, an in-game HUD with achievement toasts and a modal pause menu.
 * Everything is drivable with a gamepad (D-pad/stick to move, A to
 * activate, B/Start to go back) as well as mouse and keyboard.
 *
 * The UI renders at a fixed 1280x720 virtual resolution into a render
 * texture that is letterboxed into the window; a canvas pointer
 * transform maps window pixels back to UI units so hit-testing matches.
 * The look comes from examples/data/themes/game-menu.yaml.
 *
 * Run with --help for options.
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <math.h>

#define VIRTUAL_W (1280)
#define VIRTUAL_H (720)

static const gchar *license_text =
	"game-ui-menu - libregnum game UI example\n"
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

typedef enum
{
	SCREEN_MAIN,
	SCREEN_SETTINGS,
	SCREEN_PLAYING
} Screen;

typedef struct
{
	LrgCanvas      *canvas;
	LrgOverlay     *root;
	LrgVBox        *main_menu;
	LrgPanel       *settings;
	LrgVBox        *hud;
	LrgLabel       *score;
	LrgPanel       *pause;
	LrgToastQueue  *toasts;
	LrgButton      *play;
	LrgButton      *settings_back;
	LrgButton      *resume;
	Screen          screen;
	Screen          settings_return;
	gboolean        paused;
	gboolean        quit;
	gdouble         play_time;
	guint           achievements;

	/* letterbox: virtual -> window */
	gfloat          scale;
	gfloat          offset_x;
	gfloat          offset_y;
} Game;

/* ==========================================================================
 * Pointer transform: window pixels -> virtual UI units
 * ========================================================================== */

static void
window_to_virtual (gfloat   *x,
                   gfloat   *y,
                   gpointer  user_data)
{
	Game *game = user_data;

	*x = (*x - game->offset_x) / game->scale;
	*y = (*y - game->offset_y) / game->scale;
}

/* ==========================================================================
 * The live scene behind the UI (a parallax dusk landscape)
 * ========================================================================== */

static void
draw_scene (gdouble t)
{
	gint i;

	/* Sky gradient */
	DrawRectangleGradientV (0, 0, VIRTUAL_W, VIRTUAL_H,
	                        (Color){ 22, 14, 40, 255 }, (Color){ 120, 40, 70, 255 });

	/* Sun */
	DrawCircleGradient ((Vector2){ VIRTUAL_W / 2.0f, 470.0f }, 210.0f, (Color){ 255, 150, 90, 255 },
	                    (Color){ 255, 90, 120, 0 });

	/* Stars twinkling */
	for (i = 0; i < 90; i++)
	{
		gfloat x = (gfloat) ((i * 137) % VIRTUAL_W);
		gfloat y = (gfloat) ((i * 71) % 380);
		gfloat a = 0.4f + 0.4f * sinf ((gfloat) t * 1.5f + (gfloat) i);

		DrawCircleV ((Vector2){ x, y }, 1.4f, Fade (WHITE, a));
	}

	/* Three hill layers scrolling at different speeds */
	for (i = 0; i < 3; i++)
	{
		gfloat  speed = 12.0f + 18.0f * (gfloat) i;
		gfloat  base = 470.0f + 70.0f * (gfloat) i;
		Color   col = i == 0 ? (Color){ 70, 30, 80, 255 }
		            : i == 1 ? (Color){ 45, 20, 58, 255 } : (Color){ 25, 12, 36, 255 };
		gint    x;

		for (x = 0; x < VIRTUAL_W; x += 8)
		{
			gfloat h = 40.0f * sinf (((gfloat) x + (gfloat) t * speed) * (0.006f + 0.003f * (gfloat) i))
			           + 25.0f * sinf (((gfloat) x + (gfloat) t * speed) * 0.017f);

			DrawRectangle (x, (gint) (base - h), 8, VIRTUAL_H, col);
		}
	}
}

/* ==========================================================================
 * Screens
 * ========================================================================== */

static void
show_screen (Game   *game,
             Screen  screen)
{
	game->screen = screen;
	lrg_widget_set_visible (LRG_WIDGET (game->main_menu), screen == SCREEN_MAIN);
	lrg_widget_set_visible (LRG_WIDGET (game->settings), screen == SCREEN_SETTINGS);
	lrg_widget_set_visible (LRG_WIDGET (game->hud), screen == SCREEN_PLAYING);

	/* Land focus on something so the gamepad works immediately */
	if (screen == SCREEN_MAIN)
		lrg_canvas_set_focused_widget (game->canvas, LRG_WIDGET (game->play));
	else if (screen == SCREEN_SETTINGS)
		lrg_canvas_set_focused_widget (game->canvas, LRG_WIDGET (game->settings_back));
	else
		lrg_canvas_set_focused_widget (game->canvas, NULL);
}

static void
toast (Game         *game,
       const gchar  *title,
       const gchar  *message,
       LrgToastKind  kind)
{
	g_autoptr(LrgToast) t = lrg_toast_new (title, message);

	lrg_toast_set_kind (t, kind);
	lrg_toast_queue_show (game->toasts, t);
}

static void
open_pause (Game *game)
{
	if (game->paused)
		return;
	game->paused = TRUE;
	lrg_widget_set_appear (LRG_WIDGET (game->pause), LRG_APPEAR_DROP, 0.0f);
	lrg_canvas_show_popup (game->canvas, LRG_WIDGET (game->pause), NULL,
	                       LRG_POPUP_PLACEMENT_CENTER, TRUE);
	lrg_canvas_set_focused_widget (game->canvas, LRG_WIDGET (game->resume));
}

static void
close_pause (Game *game)
{
	if (!game->paused)
		return;
	game->paused = FALSE;
	lrg_canvas_dismiss_popup (game->canvas, LRG_WIDGET (game->pause));
}

/* ==========================================================================
 * Signal handlers
 * ========================================================================== */

static void
on_play (LrgButton *b,
         gpointer   user_data)
{
	Game *game = user_data;

	game->play_time = 0.0;
	show_screen (game, SCREEN_PLAYING);
	toast (game, "Achievement unlocked", "First Steps: start a new run.", LRG_TOAST_KIND_SUCCESS);
}

static void
on_settings (LrgButton *b,
             gpointer   user_data)
{
	Game *game = user_data;

	game->settings_return = game->screen;
	close_pause (game);
	show_screen (game, SCREEN_SETTINGS);
}

static void
on_settings_back (LrgButton *b,
                  gpointer   user_data)
{
	Game *game = user_data;

	show_screen (game, game->settings_return == SCREEN_PLAYING ? SCREEN_PLAYING : SCREEN_MAIN);
}

static void
on_quit (LrgButton *b,
         gpointer   user_data)
{
	((Game *) user_data)->quit = TRUE;
}

static void
on_resume (LrgButton *b,
           gpointer   user_data)
{
	close_pause (user_data);
}

static void
on_to_menu (LrgButton *b,
            gpointer   user_data)
{
	Game *game = user_data;

	close_pause (game);
	show_screen (game, SCREEN_MAIN);
}

/* Escape / gamepad B with nothing else to close */
static void
on_cancel (LrgCanvas *canvas,
           gpointer   user_data)
{
	Game *game = user_data;

	if (game->screen == SCREEN_PLAYING)
		open_pause (game);
	else if (game->screen == SCREEN_SETTINGS)
		on_settings_back (NULL, game);
}

/* The pause popup closed by Escape (canvas dismissed it) */
static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	Game *game = user_data;

	if (popup == LRG_WIDGET (game->pause))
		game->paused = FALSE;
}

/* ==========================================================================
 * Building
 * ========================================================================== */

static void
add (gpointer container,
     gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (container), LRG_WIDGET (child));
	g_object_unref (child);
}

static LrgButton *
menu_button (const gchar *text,
             gboolean     primary,
             GCallback    cb,
             gpointer     data)
{
	LrgButton *b = lrg_button_new (text);

	if (primary)
		lrg_widget_add_class (LRG_WIDGET (b), "primary");
	lrg_widget_set_min_size (LRG_WIDGET (b), 300.0f, 0.0f);
	lrg_widget_set_halign (LRG_WIDGET (b), LRG_ALIGN_CENTER);
	g_signal_connect (b, "clicked", cb, data);
	return b;
}

/* labelled_row: "Label .......... control" */
static void
labelled_row (LrgContainer *parent,
              const gchar  *text,
              LrgWidget    *control)
{
	LrgHBox  *row = lrg_hbox_new ();
	LrgLabel *label = lrg_label_new (text);

	lrg_container_set_spacing (LRG_CONTAINER (row), 16.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (label), 1.0f);
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	lrg_widget_set_min_size (control, 260.0f, 0.0f);
	lrg_widget_set_valign (control, LRG_ALIGN_CENTER);
	add (row, label);
	add (row, control);
	add (parent, row);
}

static void
build_ui (Game *game)
{
	LrgLabel    *title;
	LrgLabel    *subtitle;
	LrgVBox     *box;
	LrgComboBox *res;
	LrgSlider   *slider;
	LrgSpinBox  *fov;
	guint        i;

	game->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (game->canvas), VIRTUAL_W, VIRTUAL_H);
	g_signal_connect (game->canvas, "cancel", G_CALLBACK (on_cancel), game);
	g_signal_connect (game->canvas, "popup-dismissed", G_CALLBACK (on_popup_dismissed), game);

	game->root = lrg_overlay_new ();
	lrg_widget_set_pointer_transparent (LRG_WIDGET (game->root), TRUE);
	lrg_widget_set_size (LRG_WIDGET (game->root), VIRTUAL_W, VIRTUAL_H);
	lrg_container_add_child (LRG_CONTAINER (game->canvas), LRG_WIDGET (game->root));

	/* Main menu: the title drops in, buttons rise one after another */
	game->main_menu = lrg_vbox_new ();
	lrg_widget_set_pointer_transparent (LRG_WIDGET (game->main_menu), TRUE);
	lrg_container_set_spacing (LRG_CONTAINER (game->main_menu), 14.0f);
	lrg_widget_set_halign (LRG_WIDGET (game->main_menu), LRG_ALIGN_CENTER);
	lrg_widget_set_valign (LRG_WIDGET (game->main_menu), LRG_ALIGN_CENTER);
	title = lrg_label_new ("DUSKBOUND");
	lrg_widget_add_class (LRG_WIDGET (title), "title");
	lrg_widget_set_halign (LRG_WIDGET (title), LRG_ALIGN_CENTER);
	lrg_widget_set_appear (LRG_WIDGET (title), LRG_APPEAR_DROP, 0.0f);
	add (game->main_menu, title);
	subtitle = lrg_label_new ("a libregnum UI demo");
	lrg_widget_add_class (LRG_WIDGET (subtitle), "muted");
	lrg_widget_set_halign (LRG_WIDGET (subtitle), LRG_ALIGN_CENTER);
	lrg_widget_set_margin_all (LRG_WIDGET (subtitle), 0.0f);
	lrg_widget_set_appear (LRG_WIDGET (subtitle), LRG_APPEAR_FADE, 0.25f);
	add (game->main_menu, subtitle);
	{
		LrgButton *buttons[3];

		game->play = menu_button ("Play", TRUE, G_CALLBACK (on_play), game);
		buttons[0] = game->play;
		buttons[1] = menu_button ("Settings", FALSE, G_CALLBACK (on_settings), game);
		buttons[2] = menu_button ("Quit", FALSE, G_CALLBACK (on_quit), game);
		for (i = 0; i < 3; i++)
		{
			lrg_widget_set_appear (LRG_WIDGET (buttons[i]), LRG_APPEAR_FLY_UP, 0.35f + 0.07f * (gfloat) i);
			lrg_container_add_child (LRG_CONTAINER (game->main_menu), LRG_WIDGET (buttons[i]));
			if (i > 0)
				g_object_unref (buttons[i]);
		}
	}
	lrg_container_add_child (LRG_CONTAINER (game->root), LRG_WIDGET (game->main_menu));

	/* Settings card */
	game->settings = lrg_panel_new ();
	lrg_widget_add_class (LRG_WIDGET (game->settings), "card");
	lrg_panel_set_fill_children (game->settings, TRUE);
	lrg_panel_set_elevation (game->settings, 3);
	lrg_container_set_padding (LRG_CONTAINER (game->settings), 32.0f);
	lrg_widget_set_min_size (LRG_WIDGET (game->settings), 640.0f, 0.0f);
	lrg_widget_set_halign (LRG_WIDGET (game->settings), LRG_ALIGN_CENTER);
	lrg_widget_set_valign (LRG_WIDGET (game->settings), LRG_ALIGN_CENTER);
	lrg_widget_set_appear (LRG_WIDGET (game->settings), LRG_APPEAR_POP, 0.0f);
	box = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (box), 18.0f);
	{
		LrgLabel *h = lrg_label_new ("Settings");

		lrg_widget_add_class (LRG_WIDGET (h), "heading");
		add (box, h);
	}
	res = lrg_combo_box_new ();
	lrg_combo_box_append (res, "1280 x 720");
	lrg_combo_box_append (res, "1920 x 1080");
	lrg_combo_box_append (res, "2560 x 1440");
	lrg_combo_box_append (res, "3840 x 2160");
	lrg_combo_box_set_selected (res, 1);
	labelled_row (LRG_CONTAINER (box), "Resolution", LRG_WIDGET (res));
	slider = lrg_slider_new_with_range (0.0, 100.0, 5.0);
	lrg_slider_set_value (slider, 80.0);
	labelled_row (LRG_CONTAINER (box), "Music volume", LRG_WIDGET (slider));
	slider = lrg_slider_new_with_range (0.0, 100.0, 5.0);
	lrg_slider_set_value (slider, 55.0);
	labelled_row (LRG_CONTAINER (box), "Effects volume", LRG_WIDGET (slider));
	fov = lrg_spin_box_new_with_range (60.0, 120.0, 5.0);
	lrg_spin_box_set_value (fov, 90.0);
	labelled_row (LRG_CONTAINER (box), "Field of view", LRG_WIDGET (fov));
	labelled_row (LRG_CONTAINER (box), "Screen shake",
	              LRG_WIDGET (lrg_checkbox_new_with_style ("", LRG_CHECKBOX_STYLE_SWITCH)));
	labelled_row (LRG_CONTAINER (box), "Subtitles",
	              LRG_WIDGET (lrg_checkbox_new_with_style ("", LRG_CHECKBOX_STYLE_SWITCH)));
	game->settings_back = menu_button ("Back", TRUE, G_CALLBACK (on_settings_back), game);
	lrg_widget_set_halign (LRG_WIDGET (game->settings_back), LRG_ALIGN_END);
	lrg_widget_set_min_size (LRG_WIDGET (game->settings_back), 160.0f, 0.0f);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (game->settings_back));
	add (game->settings, box);
	lrg_container_add_child (LRG_CONTAINER (game->root), LRG_WIDGET (game->settings));

	/* HUD */
	game->hud = lrg_vbox_new ();
	lrg_widget_set_pointer_transparent (LRG_WIDGET (game->hud), TRUE);
	lrg_widget_set_halign (LRG_WIDGET (game->hud), LRG_ALIGN_START);
	lrg_widget_set_valign (LRG_WIDGET (game->hud), LRG_ALIGN_START);
	lrg_widget_set_margin_all (LRG_WIDGET (game->hud), 24.0f);
	game->score = lrg_label_new ("Distance 0 m");
	lrg_widget_add_class (LRG_WIDGET (game->score), "heading");
	lrg_widget_set_appear (LRG_WIDGET (game->score), LRG_APPEAR_FLY_RIGHT, 0.0f);
	lrg_container_add_child (LRG_CONTAINER (game->hud), LRG_WIDGET (game->score));
	{
		LrgLabel *hint = lrg_label_new ("Esc / Start: pause");

		lrg_widget_add_class (LRG_WIDGET (hint), "muted");
		add (game->hud, hint);
	}
	lrg_container_add_child (LRG_CONTAINER (game->root), LRG_WIDGET (game->hud));

	/* Pause menu: a modal popup (veil, trapped focus) */
	game->pause = lrg_panel_new ();
	lrg_widget_add_class (LRG_WIDGET (game->pause), "card");
	lrg_panel_set_fill_children (game->pause, TRUE);
	lrg_panel_set_elevation (game->pause, 3);
	lrg_container_set_padding (LRG_CONTAINER (game->pause), 28.0f);
	box = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (box), 12.0f);
	{
		LrgLabel *h = lrg_label_new ("Paused");

		lrg_widget_add_class (LRG_WIDGET (h), "heading");
		lrg_widget_set_halign (LRG_WIDGET (h), LRG_ALIGN_CENTER);
		add (box, h);
	}
	game->resume = menu_button ("Resume", TRUE, G_CALLBACK (on_resume), game);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (game->resume));
	add (box, menu_button ("Settings", FALSE, G_CALLBACK (on_settings), game));
	add (box, menu_button ("Quit to menu", FALSE, G_CALLBACK (on_to_menu), game));
	add (game->pause, box);

	/* Toasts in the top-right corner, above everything */
	game->toasts = lrg_toast_queue_new ();
	lrg_toast_queue_set_toast_valign (game->toasts, LRG_ALIGN_START);
	lrg_widget_set_size (LRG_WIDGET (game->toasts), VIRTUAL_W, VIRTUAL_H);
	lrg_canvas_add_overlay (game->canvas, LRG_WIDGET (game->toasts));
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
	g_autoptr(LrgTheme)       theme = NULL;
	g_autofree gchar         *opt_theme_file = NULL;
	g_autofree gchar         *opt_screen = NULL;
	g_autofree gchar         *opt_screenshot = NULL;
	gint                      opt_frames = 120;
	gboolean                  opt_license = FALSE;
	Game                      game = { 0 };
	RenderTexture2D           target;
	gint                      frame = 0;

	GOptionEntry entries[] = {
		{ "theme-file", 't', 0, G_OPTION_ARG_FILENAME, &opt_theme_file,
		  "Theme YAML (default: data/themes/game-menu.yaml)", "PATH" },
		{ "screen", 0, 0, G_OPTION_ARG_STRING, &opt_screen,
		  "Start on a screen: main, settings, playing, pause", "NAME" },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames, "Frames before --screenshot (default 120)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- game menus over a live scene");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Gamepad: D-pad/stick move focus, A activates, B/Start goes back or pauses.\n\n"
		"Examples:\n"
		"  game-ui-menu\n"
		"  game-ui-menu --screen settings\n"
		"  game-ui-menu --theme-file my-theme.yaml\n"
		"  game-ui-menu --screen pause --screenshot pause.png\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("game-ui-menu: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
	window = grl_window_new (VIRTUAL_W, VIRTUAL_H, "Duskbound - libregnum game UI");
	grl_window_set_target_fps (window, 60);
	target = LoadRenderTexture (VIRTUAL_W, VIRTUAL_H);
	SetTextureFilter (target.texture, TEXTURE_FILTER_BILINEAR);

	/* Theme from YAML, falling back to a preset if it can't be read */
	theme = lrg_theme_new_from_file (opt_theme_file != NULL ? opt_theme_file
	                                                        : "data/themes/game-menu.yaml", &error);
	if (theme == NULL)
	{
		g_printerr ("game-ui-menu: %s; using the midnight preset\n", error->message);
		g_clear_error (&error);
		theme = lrg_theme_new_preset ("midnight");
	}
	lrg_theme_set_default (theme);

	build_ui (&game);
	lrg_canvas_set_default_pointer_transform (window_to_virtual, &game);

	show_screen (&game, SCREEN_MAIN);
	if (g_strcmp0 (opt_screen, "settings") == 0)
		on_settings (NULL, &game);
	else if (g_strcmp0 (opt_screen, "playing") == 0 || g_strcmp0 (opt_screen, "pause") == 0)
		on_play (NULL, &game);

	while (!grl_window_should_close (window) && !game.quit)
	{
		gfloat ww = (gfloat) GetScreenWidth ();
		gfloat wh = (gfloat) GetScreenHeight ();
		gfloat dt = GetFrameTime ();

		/* Letterbox the virtual screen into the window */
		game.scale = MIN (ww / (gfloat) VIRTUAL_W, wh / (gfloat) VIRTUAL_H);
		game.offset_x = (ww - (gfloat) VIRTUAL_W * game.scale) * 0.5f;
		game.offset_y = (wh - (gfloat) VIRTUAL_H * game.scale) * 0.5f;

		/* Start on a gamepad also pauses */
		if (game.screen == SCREEN_PLAYING && IsGamepadAvailable (0) &&
		    IsGamepadButtonPressed (0, GAMEPAD_BUTTON_MIDDLE_RIGHT))
			open_pause (&game);

		lrg_canvas_handle_input (game.canvas);

		/* A tiny "game": distance and timed achievements */
		if (game.screen == SCREEN_PLAYING && !game.paused)
		{
			g_autofree gchar *text = NULL;

			game.play_time += dt;
			text = g_strdup_printf ("Distance %d m", (gint) (game.play_time * 12.0));
			lrg_label_set_text (game.score, text);
			if (game.play_time > 6.0 * (game.achievements + 1))
			{
				game.achievements++;
				toast (&game, "Achievement unlocked",
				       game.achievements % 2 ? "Wanderer: travel 70 m." : "Night owl: keep going.",
				       LRG_TOAST_KIND_INFO);
			}
		}
		if (g_strcmp0 (opt_screen, "pause") == 0 && frame == 30)
			open_pause (&game);

		/* Scene + UI into the virtual target */
		BeginTextureMode (target);
		draw_scene (GetTime ());
		lrg_canvas_render (game.canvas);
		EndTextureMode ();

		grl_window_begin_drawing (window);
		ClearBackground (BLACK);
		DrawTexturePro (target.texture,
		                (Rectangle){ 0.0f, 0.0f, (gfloat) VIRTUAL_W, -(gfloat) VIRTUAL_H },
		                (Rectangle){ game.offset_x, game.offset_y,
		                             (gfloat) VIRTUAL_W * game.scale, (gfloat) VIRTUAL_H * game.scale },
		                (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);

		if (opt_screenshot != NULL && frame >= opt_frames)
		{
			Image shot = LoadImageFromTexture (target.texture);

			ImageFlipVertical (&shot);
			ExportImage (shot, opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	lrg_canvas_set_default_pointer_transform (NULL, NULL);
	g_clear_object (&game.pause);
	g_clear_object (&game.play);
	g_clear_object (&game.settings_back);
	g_clear_object (&game.resume);
	g_clear_object (&game.score);
	g_clear_object (&game.hud);
	g_clear_object (&game.settings);
	g_clear_object (&game.main_menu);
	g_clear_object (&game.root);
	g_clear_object (&game.toasts);
	g_clear_object (&game.canvas);
	UnloadRenderTexture (target);
	return 0;
}
