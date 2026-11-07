/* lrg-editor-viewport-nav.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportNav: the viewport navigation state machine, ported
 * from the immediate-mode editor_panel_viewport() (src/editor/ui). The old
 * code polled the GUI once per frame; this one is event driven:
 *
 *   old (per frame)                          new (per event)
 *   is_mouse_clicked + start conditions  ->  pointer_press
 *   mouse delta while navigating          ->  pointer_move (dx, dy)
 *   !is_mouse_down (nav_button)           ->  pointer_release
 *   is_key_down (W/A/S/D/Q/E) * dt        ->  key_down/key_up + tick
 *   wheel_x / wheel_y                     ->  scroll
 *
 * The constants (click slop, gizmo geometry, wheel factors, sensitivities)
 * are the old ones, so the camera moves exactly as it did.
 */

#include "config.h"

#include "lrg-editor-viewport-nav.h"
#include "lrg-editor-settings.h"
#include <math.h>
#include <string.h>

#define DEG2RAD   ((gfloat) G_PI / 180.0f)
#define FLY_FAST  (3.0f)

/* ==========================================================================
 * LrgEditorNavScheme
 * ========================================================================== */

GType
lrg_editor_nav_scheme_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_NAV_SCHEME_DEFAULT, "LRG_EDITOR_NAV_SCHEME_DEFAULT", "default" },
			{ LRG_EDITOR_NAV_SCHEME_BLENDER, "LRG_EDITOR_NAV_SCHEME_BLENDER", "blender" },
			{ LRG_EDITOR_NAV_SCHEME_TRACKPAD, "LRG_EDITOR_NAV_SCHEME_TRACKPAD", "trackpad" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorNavScheme"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

struct _LrgEditorViewportNav
{
	GObject             parent_instance;

	LrgEditorViewState *view_state;     /* strong ref */
	GBinding           *bind_scheme;    /* settings bindings (owned by the settings) */
	GBinding           *bind_invert;
	GBinding           *bind_orbit;

	LrgEditorNavScheme  scheme;
	gboolean            invert_y;
	gdouble             orbit_speed;    /* degrees per pixel */

	LrgEditorNavMode    mode;
	GrlMouseButton      nav_button;     /* the button driving navigation */
	gboolean            nav_moved;      /* passed the click slop */
	gint                nav_axis;       /* gizmo end pressed, -1 none */
	gfloat              press_x;
	gfloat              press_y;
	gint                axis_hot;       /* gizmo end hovered, -1 none */

	gboolean            box_armed;      /* left press on empty space */
	gfloat              box_x0;
	gfloat              box_y0;
	gfloat              cur_x;          /* last pointer position */
	gfloat              cur_y;

	GHashTable         *held;           /* GINT_TO_POINTER (GrlKey) set */
	LrgInputModifiers   mods;           /* last modifiers seen */
};

G_DEFINE_FINAL_TYPE (LrgEditorViewportNav, lrg_editor_viewport_nav, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_VIEW_STATE,
	PROP_SCHEME,
	PROP_INVERT_Y,
	PROP_ORBIT_SPEED,
	PROP_MODE,
	PROP_AXIS_HOT,
	N_PROPS
};

enum
{
	SIGNAL_MODE_CHANGED,
	SIGNAL_CAMERA_CHANGED,
	SIGNAL_CLICK,
	SIGNAL_BOX_SELECT,
	SIGNAL_CONTEXT_MENU,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* --- Helpers --------------------------------------------------------------------- */

static LrgEditorViewCamera *
camera (LrgEditorViewportNav *self)
{
	return lrg_editor_view_state_get_camera (self->view_state, -1);
}

/* Mirrors the mode into the view state (ui.state "nav") and notifies. */
static void
set_mode (LrgEditorViewportNav *self,
          LrgEditorNavMode      mode)
{
	if (self->mode == mode)
		return;
	self->mode = mode;
	lrg_editor_view_state_set_nav_mode (self->view_state, mode);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
	g_signal_emit (self, signals[SIGNAL_MODE_CHANGED], 0, mode);
}

static void
set_axis_hot (LrgEditorViewportNav *self,
              gint                  hot)
{
	if (self->axis_hot == hot)
		return;
	self->axis_hot = hot;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AXIS_HOT]);
}

/* Called with the camera state from before a change: emits the change
 * signals only when something actually moved. */
static gboolean
finish_camera_edit (LrgEditorViewportNav      *self,
                    const LrgEditorViewCamera *before)
{
	if (lrg_editor_view_camera_equal (before, camera (self)))
		return FALSE;
	lrg_editor_view_state_camera_changed (self->view_state, -1);
	g_signal_emit (self, signals[SIGNAL_CAMERA_CHANGED], 0);
	return TRUE;
}

/* Vertical drag after the invert-y setting. */
static gfloat
vertical (LrgEditorViewportNav *self,
          gfloat                dy)
{
	return self->invert_y ? -dy : dy;
}

static gfloat
sensitivity (LrgEditorViewportNav *self)
{
	return (gfloat) self->orbit_speed * DEG2RAD;
}

static gboolean
is_fly_key (GrlKey key)
{
	return key == GRL_KEY_W || key == GRL_KEY_A || key == GRL_KEY_S || key == GRL_KEY_D ||
	       key == GRL_KEY_Q || key == GRL_KEY_E;
}

static gboolean
held (LrgEditorViewportNav *self,
      GrlKey                key)
{
	return g_hash_table_contains (self->held, GINT_TO_POINTER ((gint) key));
}

/* --- GObject ------------------------------------------------------------------------ */

static void
lrg_editor_viewport_nav_dispose (GObject *object)
{
	LrgEditorViewportNav *self = LRG_EDITOR_VIEWPORT_NAV (object);

	lrg_editor_viewport_nav_bind_settings (self, NULL);
	g_clear_object (&self->view_state);

	G_OBJECT_CLASS (lrg_editor_viewport_nav_parent_class)->dispose (object);
}

static void
lrg_editor_viewport_nav_finalize (GObject *object)
{
	LrgEditorViewportNav *self = LRG_EDITOR_VIEWPORT_NAV (object);

	g_clear_pointer (&self->held, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_editor_viewport_nav_parent_class)->finalize (object);
}

static void
lrg_editor_viewport_nav_constructed (GObject *object)
{
	LrgEditorViewportNav *self = LRG_EDITOR_VIEWPORT_NAV (object);

	G_OBJECT_CLASS (lrg_editor_viewport_nav_parent_class)->constructed (object);

	/* Without a shared view state, navigate a private one. */
	if (self->view_state == NULL)
		self->view_state = lrg_editor_view_state_new (NULL);
}

static void
lrg_editor_viewport_nav_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
	LrgEditorViewportNav *self = LRG_EDITOR_VIEWPORT_NAV (object);

	switch (prop_id)
	{
	case PROP_VIEW_STATE:  g_value_set_object (value, self->view_state); break;
	case PROP_SCHEME:      g_value_set_enum (value, (gint) self->scheme); break;
	case PROP_INVERT_Y:    g_value_set_boolean (value, self->invert_y); break;
	case PROP_ORBIT_SPEED: g_value_set_double (value, self->orbit_speed); break;
	case PROP_MODE:        g_value_set_enum (value, (gint) self->mode); break;
	case PROP_AXIS_HOT:    g_value_set_int (value, self->axis_hot); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_viewport_nav_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
	LrgEditorViewportNav *self = LRG_EDITOR_VIEWPORT_NAV (object);

	switch (prop_id)
	{
	case PROP_VIEW_STATE:
		g_set_object (&self->view_state, g_value_get_object (value));
		break;
	case PROP_SCHEME:
		lrg_editor_viewport_nav_set_scheme (self, (LrgEditorNavScheme) g_value_get_enum (value));
		break;
	case PROP_INVERT_Y:
		lrg_editor_viewport_nav_set_invert_y (self, g_value_get_boolean (value));
		break;
	case PROP_ORBIT_SPEED:
		lrg_editor_viewport_nav_set_orbit_speed (self, g_value_get_double (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_viewport_nav_class_init (LrgEditorViewportNavClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	GParamFlags   ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS;

	object_class->dispose = lrg_editor_viewport_nav_dispose;
	object_class->finalize = lrg_editor_viewport_nav_finalize;
	object_class->constructed = lrg_editor_viewport_nav_constructed;
	object_class->get_property = lrg_editor_viewport_nav_get_property;
	object_class->set_property = lrg_editor_viewport_nav_set_property;

	/**
	 * LrgEditorViewportNav:view-state:
	 *
	 * The view state whose active camera navigation moves and whose
	 * nav-mode it writes. A private one is created when %NULL.
	 */
	properties[PROP_VIEW_STATE] =
		g_param_spec_object ("view-state", "View State", "View state navigated",
		                     LRG_TYPE_EDITOR_VIEW_STATE,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorViewportNav:scheme:
	 *
	 * The mouse scheme (camera-navigation setting).
	 */
	properties[PROP_SCHEME] =
		g_param_spec_enum ("scheme", "Scheme", "Navigation scheme",
		                   LRG_TYPE_EDITOR_NAV_SCHEME, LRG_EDITOR_NAV_SCHEME_DEFAULT, rw);

	/**
	 * LrgEditorViewportNav:invert-y:
	 *
	 * Inverts vertical look and orbit (camera-invert-y setting).
	 */
	properties[PROP_INVERT_Y] =
		g_param_spec_boolean ("invert-y", "Invert Y", "Invert vertical look", FALSE, rw);

	/**
	 * LrgEditorViewportNav:orbit-speed:
	 *
	 * Look and orbit sensitivity in degrees per pixel
	 * (camera-orbit-speed setting).
	 */
	properties[PROP_ORBIT_SPEED] =
		g_param_spec_double ("orbit-speed", "Orbit Speed", "Degrees per pixel",
		                     0.01, 5.0, 0.35, rw);

	/**
	 * LrgEditorViewportNav:mode:
	 *
	 * The navigation in progress.
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", "Mode", "Navigation in progress",
		                   LRG_TYPE_EDITOR_NAV_MODE, LRG_EDITOR_NAV_MODE_NONE, ro);

	/**
	 * LrgEditorViewportNav:axis-hot:
	 *
	 * The hovered orientation gizmo end (0..5),
	 * %LRG_EDITOR_NAV_AXIS_HOT_DISC over the disc, or -1.
	 */
	properties[PROP_AXIS_HOT] =
		g_param_spec_int ("axis-hot", "Axis Hot", "Hovered orientation gizmo end",
		                  -1, LRG_EDITOR_NAV_AXIS_HOT_DISC, -1, ro);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditorViewportNav::mode-changed:
	 * @self: the navigator
	 * @mode: the new #LrgEditorNavMode
	 *
	 * Navigation started, changed or ended.
	 */
	signals[SIGNAL_MODE_CHANGED] =
		g_signal_new ("mode-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_EDITOR_NAV_MODE);

	/**
	 * LrgEditorViewportNav::camera-changed:
	 * @self: the navigator
	 *
	 * Navigation moved the active camera (the view state also emits
	 * #LrgEditorViewState::camera-changed).
	 */
	signals[SIGNAL_CAMERA_CHANGED] =
		g_signal_new ("camera-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgEditorViewportNav::click:
	 * @self: the navigator
	 * @x: viewport-local x of the release
	 * @y: viewport-local y of the release
	 * @mods: the #LrgInputModifiers held at release
	 *
	 * An armed left press was released without dragging: pick here
	 * (Shift adds, Ctrl toggles, clicking again cycles).
	 */
	signals[SIGNAL_CLICK] =
		g_signal_new ("click", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3,
		              G_TYPE_FLOAT, G_TYPE_FLOAT, LRG_TYPE_INPUT_MODIFIERS);

	/**
	 * LrgEditorViewportNav::box-select:
	 * @self: the navigator
	 * @x0: viewport-local x of the press
	 * @y0: viewport-local y of the press
	 * @x1: viewport-local x of the release
	 * @y1: viewport-local y of the release
	 * @mods: the #LrgInputModifiers held at release (Shift extends)
	 *
	 * A box selection was dragged; feed the corners to
	 * lrg_editor_box_select().
	 */
	signals[SIGNAL_BOX_SELECT] =
		g_signal_new ("box-select", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 5,
		              G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, LRG_TYPE_INPUT_MODIFIERS);

	/**
	 * LrgEditorViewportNav::context-menu:
	 * @self: the navigator
	 * @x: viewport-local x
	 * @y: viewport-local y
	 *
	 * A right click without motion: open the viewport context menu.
	 */
	signals[SIGNAL_CONTEXT_MENU] =
		g_signal_new ("context-menu", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_FLOAT, G_TYPE_FLOAT);
}

static void
lrg_editor_viewport_nav_init (LrgEditorViewportNav *self)
{
	self->scheme = LRG_EDITOR_NAV_SCHEME_DEFAULT;
	self->orbit_speed = 0.35;
	self->mode = LRG_EDITOR_NAV_MODE_NONE;
	self->nav_axis = -1;
	self->axis_hot = -1;
	self->held = g_hash_table_new (g_direct_hash, g_direct_equal);
}

LrgEditorViewportNav *
lrg_editor_viewport_nav_new (LrgEditorViewState *view_state)
{
	g_return_val_if_fail (view_state == NULL || LRG_IS_EDITOR_VIEW_STATE (view_state), NULL);

	return g_object_new (LRG_TYPE_EDITOR_VIEWPORT_NAV, "view-state", view_state, NULL);
}

LrgEditorViewState *
lrg_editor_viewport_nav_get_view_state (LrgEditorViewportNav *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), NULL);

	return self->view_state;
}

LrgEditorViewCamera *
lrg_editor_viewport_nav_get_camera (LrgEditorViewportNav *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), NULL);

	return camera (self);
}

/* --- Settings ---------------------------------------------------------------------- */

static gboolean
scheme_from_setting (GBinding     *binding,
                     const GValue *from,
                     GValue       *to,
                     gpointer      user_data)
{
	const gchar *name = g_value_get_string (from);

	/* Unknown names fall back to the default scheme, as the old
	 * g_strcmp0 checks did. */
	if (g_strcmp0 (name, "blender") == 0)
		g_value_set_enum (to, LRG_EDITOR_NAV_SCHEME_BLENDER);
	else if (g_strcmp0 (name, "trackpad") == 0)
		g_value_set_enum (to, LRG_EDITOR_NAV_SCHEME_TRACKPAD);
	else
		g_value_set_enum (to, LRG_EDITOR_NAV_SCHEME_DEFAULT);
	return TRUE;
}

void
lrg_editor_viewport_nav_bind_settings (LrgEditorViewportNav *self,
                                       LrgEditorSettings    *settings)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));
	g_return_if_fail (settings == NULL || LRG_IS_EDITOR_SETTINGS (settings));

	/* Drop earlier bindings (unbind releases the binding's own ref). */
	if (self->bind_scheme != NULL)
		g_binding_unbind (self->bind_scheme);
	if (self->bind_invert != NULL)
		g_binding_unbind (self->bind_invert);
	if (self->bind_orbit != NULL)
		g_binding_unbind (self->bind_orbit);
	self->bind_scheme = self->bind_invert = self->bind_orbit = NULL;

	if (settings == NULL)
		return;

	self->bind_scheme = g_object_bind_property_full (settings, "camera-navigation", self, "scheme",
	                                                 G_BINDING_SYNC_CREATE, scheme_from_setting,
	                                                 NULL, NULL, NULL);
	self->bind_invert = g_object_bind_property (settings, "camera-invert-y", self, "invert-y",
	                                            G_BINDING_SYNC_CREATE);
	self->bind_orbit = g_object_bind_property (settings, "camera-orbit-speed", self, "orbit-speed",
	                                           G_BINDING_SYNC_CREATE);

	/* A binding dies with its source: track them weakly so a settings
	 * object finalized first leaves NULLs, not dangling pointers. */
	g_object_add_weak_pointer (G_OBJECT (self->bind_scheme), (gpointer *) &self->bind_scheme);
	g_object_add_weak_pointer (G_OBJECT (self->bind_invert), (gpointer *) &self->bind_invert);
	g_object_add_weak_pointer (G_OBJECT (self->bind_orbit), (gpointer *) &self->bind_orbit);
}

LrgEditorNavScheme
lrg_editor_viewport_nav_get_scheme (LrgEditorViewportNav *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), LRG_EDITOR_NAV_SCHEME_DEFAULT);

	return self->scheme;
}

void
lrg_editor_viewport_nav_set_scheme (LrgEditorViewportNav *self,
                                    LrgEditorNavScheme    scheme)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));
	g_return_if_fail (scheme <= LRG_EDITOR_NAV_SCHEME_TRACKPAD);

	if (self->scheme == scheme)
		return;
	self->scheme = scheme;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCHEME]);
}

gboolean
lrg_editor_viewport_nav_get_invert_y (LrgEditorViewportNav *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	return self->invert_y;
}

void
lrg_editor_viewport_nav_set_invert_y (LrgEditorViewportNav *self,
                                      gboolean              invert_y)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));

	invert_y = invert_y ? TRUE : FALSE;
	if (self->invert_y == invert_y)
		return;
	self->invert_y = invert_y;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INVERT_Y]);
}

gdouble
lrg_editor_viewport_nav_get_orbit_speed (LrgEditorViewportNav *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), 0.35);

	return self->orbit_speed;
}

void
lrg_editor_viewport_nav_set_orbit_speed (LrgEditorViewportNav *self,
                                         gdouble               speed)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));

	speed = CLAMP (speed, 0.01, 5.0);
	if (self->orbit_speed == speed)
		return;
	self->orbit_speed = speed;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORBIT_SPEED]);
}

LrgEditorNavMode
lrg_editor_viewport_nav_get_mode (LrgEditorViewportNav *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), LRG_EDITOR_NAV_MODE_NONE);

	return self->mode;
}

/* ==========================================================================
 * Orientation gizmo geometry (bottom left; drag orbits, a click on an
 * axis end looks along it; works with any pointing device)
 * ========================================================================== */

gboolean
lrg_editor_viewport_nav_get_gizmo_center (LrgEditorViewportNav *self,
                                          gfloat               *x,
                                          gfloat               *y)
{
	LrgEditorViewCamera *cam;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);
	g_return_val_if_fail (x != NULL && y != NULL, FALSE);

	cam = camera (self);
	*x = LRG_EDITOR_NAV_GIZMO_MARGIN;
	*y = cam->view.height - LRG_EDITOR_NAV_GIZMO_MARGIN;
	return !cam->view_2d;
}

void
lrg_editor_viewport_nav_get_gizmo_end (LrgEditorViewportNav *self,
                                       gint                  index,
                                       gfloat               *dx,
                                       gfloat               *dy,
                                       gfloat               *depth)
{
	gfloat view[16];
	gint   axis;
	gfloat sign;

	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));
	g_return_if_fail (index >= 0 && index < 6);
	g_return_if_fail (dx != NULL && dy != NULL);

	axis = index % 3;
	sign = (index < 3) ? 1.0f : -1.0f;

	/* Column @axis of the view rotation = that world axis in camera
	 * space. */
	lrg_editor_view_get_view_matrix (&camera (self)->view, view);
	*dx = view[axis * 4 + 0] * sign * LRG_EDITOR_NAV_GIZMO_RADIUS;
	*dy = -view[axis * 4 + 1] * sign * LRG_EDITOR_NAV_GIZMO_RADIUS;
	if (depth != NULL)
		*depth = view[axis * 4 + 2] * sign;
}

gint
lrg_editor_viewport_nav_gizmo_hit (LrgEditorViewportNav *self,
                                   gfloat                x,
                                   gfloat                y)
{
	gfloat cx, cy, best = LRG_EDITOR_NAV_GIZMO_END_HIT;
	gint   hit = -1, i;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), -1);

	if (!lrg_editor_viewport_nav_get_gizmo_center (self, &cx, &cy))
		return -1;

	/* The nearest axis end within its hit radius wins. */
	for (i = 0; i < 6; i++)
	{
		gfloat dx, dy, d;

		lrg_editor_viewport_nav_get_gizmo_end (self, i, &dx, &dy, NULL);
		d = hypotf (x - (cx + dx), y - (cy + dy));
		if (d < best)
		{
			best = d;
			hit = i;
		}
	}
	if (hit >= 0)
		return hit;
	return (hypotf (x - cx, y - cy) <= LRG_EDITOR_NAV_GIZMO_RADIUS + LRG_EDITOR_NAV_GIZMO_END_HIT)
	       ? LRG_EDITOR_NAV_AXIS_HOT_DISC : -1;
}

gint
lrg_editor_viewport_nav_get_axis_hot (LrgEditorViewportNav *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), -1);

	return self->axis_hot;
}

gboolean
lrg_editor_viewport_nav_get_box_rect (LrgEditorViewportNav *self,
                                      gfloat               *x,
                                      gfloat               *y,
                                      gfloat               *width,
                                      gfloat               *height)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	if (self->mode != LRG_EDITOR_NAV_MODE_BOX)
		return FALSE;
	if (x != NULL)
		*x = MIN (self->box_x0, self->cur_x);
	if (y != NULL)
		*y = MIN (self->box_y0, self->cur_y);
	if (width != NULL)
		*width = fabsf (self->cur_x - self->box_x0);
	if (height != NULL)
		*height = fabsf (self->cur_y - self->box_y0);
	return TRUE;
}

/* ==========================================================================
 * Input
 * ========================================================================== */

/* Begins a navigation drag driven by @button. */
static void
start_nav (LrgEditorViewportNav *self,
           LrgEditorNavMode      mode,
           GrlMouseButton        button,
           gfloat                x,
           gfloat                y)
{
	self->nav_button = button;
	self->nav_moved = FALSE;
	self->press_x = x;
	self->press_y = y;
	set_mode (self, mode);
}

gboolean
lrg_editor_viewport_nav_pointer_press (LrgEditorViewportNav *self,
                                       GrlMouseButton        button,
                                       gfloat                x,
                                       gfloat                y,
                                       LrgInputModifiers     mods)
{
	gboolean view_2d;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	self->mods = mods;
	self->cur_x = x;
	self->cur_y = y;

	/* One gesture at a time: presses during navigation or a pending box
	 * selection belong to it. */
	if (self->mode != LRG_EDITOR_NAV_MODE_NONE || self->box_armed)
		return TRUE;

	view_2d = camera (self)->view_2d;

	/* --- Orientation gizmo: a left press on it orbits or aligns ------- */
	if (button == GRL_MOUSE_BUTTON_LEFT)
	{
		gint hit = lrg_editor_viewport_nav_gizmo_hit (self, x, y);

		set_axis_hot (self, hit);
		if (hit != -1)
		{
			self->nav_axis = (hit < 6) ? hit : -1;
			start_nav (self, LRG_EDITOR_NAV_MODE_GIZMO, button, x, y);
			return TRUE;
		}
	}

	/* --- Navigation start ------------------------------------------------ */
	switch (button)
	{
	case GRL_MOUSE_BUTTON_RIGHT:
		/* Fly look (pan in 2D); a click without motion opens the menu. */
		start_nav (self, view_2d ? LRG_EDITOR_NAV_MODE_PAN : LRG_EDITOR_NAV_MODE_FLY, button, x, y);
		return TRUE;

	case GRL_MOUSE_BUTTON_MIDDLE:
		if (self->scheme == LRG_EDITOR_NAV_SCHEME_BLENDER && !view_2d)
			start_nav (self, (mods & LRG_INPUT_MODIFIER_SHIFT) ? LRG_EDITOR_NAV_MODE_PAN
			                 : (mods & LRG_INPUT_MODIFIER_CTRL) ? LRG_EDITOR_NAV_MODE_ZOOM
			                                                    : LRG_EDITOR_NAV_MODE_ORBIT,
			           button, x, y);
		else
			start_nav (self, LRG_EDITOR_NAV_MODE_PAN, button, x, y);
		return TRUE;

	case GRL_MOUSE_BUTTON_LEFT:
		/* Alt + left: orbit (pan in 2D); Alt + Shift + left pans. */
		if (mods & LRG_INPUT_MODIFIER_ALT)
		{
			start_nav (self, (view_2d || (mods & LRG_INPUT_MODIFIER_SHIFT)) ? LRG_EDITOR_NAV_MODE_PAN
			                                                                 : LRG_EDITOR_NAV_MODE_ORBIT,
			           button, x, y);
			return TRUE;
		}
		return FALSE;

	default:
		return FALSE;
	}
}

void
lrg_editor_viewport_nav_press_empty (LrgEditorViewportNav *self,
                                     gfloat                x,
                                     gfloat                y)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));

	if (self->mode != LRG_EDITOR_NAV_MODE_NONE)
		return;
	self->box_armed = TRUE;
	self->box_x0 = self->press_x = x;
	self->box_y0 = self->press_y = y;
	self->cur_x = x;
	self->cur_y = y;
}

gboolean
lrg_editor_viewport_nav_pointer_move (LrgEditorViewportNav *self,
                                      gfloat                x,
                                      gfloat                y,
                                      gfloat                dx,
                                      gfloat                dy,
                                      LrgInputModifiers     mods)
{
	LrgEditorViewCamera *cam;
	LrgEditorViewCamera  before;
	gfloat               sens;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	self->mods = mods;
	self->cur_x = x;
	self->cur_y = y;

	/* --- Idle: track the hovered orientation gizmo end ------------------ */
	if (self->mode == LRG_EDITOR_NAV_MODE_NONE && !self->box_armed)
	{
		set_axis_hot (self, lrg_editor_viewport_nav_gizmo_hit (self, x, y));
		return FALSE;
	}

	/* --- Box selection: becomes a box past the click slop ---------------- */
	if (self->box_armed)
	{
		if (self->mode == LRG_EDITOR_NAV_MODE_NONE &&
		    (fabsf (x - self->box_x0) > LRG_EDITOR_NAV_CLICK_SLOP ||
		     fabsf (y - self->box_y0) > LRG_EDITOR_NAV_CLICK_SLOP))
			set_mode (self, LRG_EDITOR_NAV_MODE_BOX);
		return TRUE;
	}

	/* --- Navigation drags ------------------------------------------------ */
	if (fabsf (x - self->press_x) >= LRG_EDITOR_NAV_CLICK_SLOP ||
	    fabsf (y - self->press_y) >= LRG_EDITOR_NAV_CLICK_SLOP)
		self->nav_moved = TRUE;

	cam = camera (self);
	before = *cam;
	sens = sensitivity (self);
	switch (self->mode)
	{
	case LRG_EDITOR_NAV_MODE_FLY:
		/* Mouse look; the WASD movement happens in tick(). */
		lrg_editor_view_camera_fly (cam, 0.0f, 0.0f, 0.0f, dx * sens, vertical (self, dy) * sens);
		break;

	case LRG_EDITOR_NAV_MODE_ORBIT:
	case LRG_EDITOR_NAV_MODE_GIZMO:
		if (self->nav_moved)
			lrg_editor_view_camera_orbit (cam, -dx * sens, vertical (self, dy) * sens);
		break;

	case LRG_EDITOR_NAV_MODE_PAN:
		lrg_editor_view_camera_pan (cam, dx, dy);
		break;

	case LRG_EDITOR_NAV_MODE_ZOOM:
		if (dy != 0.0f)
			lrg_editor_view_camera_dolly (cam, powf (1.01f, dy));
		break;

	default:
		break;
	}
	finish_camera_edit (self, &before);
	return TRUE;
}

gboolean
lrg_editor_viewport_nav_pointer_release (LrgEditorViewportNav *self,
                                         GrlMouseButton        button,
                                         gfloat                x,
                                         gfloat                y,
                                         LrgInputModifiers     mods)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	self->mods = mods;
	self->cur_x = x;
	self->cur_y = y;

	/* --- Box selection or click ------------------------------------------ */
	if (self->box_armed)
	{
		if (button != GRL_MOUSE_BUTTON_LEFT)
			return TRUE;
		self->box_armed = FALSE;
		if (self->mode == LRG_EDITOR_NAV_MODE_BOX)
		{
			set_mode (self, LRG_EDITOR_NAV_MODE_NONE);
			g_signal_emit (self, signals[SIGNAL_BOX_SELECT], 0, self->box_x0, self->box_y0, x, y, mods);
		}
		else
			g_signal_emit (self, signals[SIGNAL_CLICK], 0, x, y, mods);
		return TRUE;
	}

	if (self->mode == LRG_EDITOR_NAV_MODE_NONE)
		return FALSE;

	/* Other buttons released during navigation stay with it. */
	if (button != self->nav_button)
		return TRUE;

	/* --- End of a navigation drag ----------------------------------------
	 * A right click that never moved opens the menu, a click on a gizmo
	 * axis end looks along it. */
	{
		gboolean         moved = self->nav_moved;
		gint             axis = self->nav_axis;
		LrgEditorNavMode mode = self->mode;

		self->nav_axis = -1;
		set_mode (self, LRG_EDITOR_NAV_MODE_NONE);

		if (!moved && button == GRL_MOUSE_BUTTON_RIGHT)
			g_signal_emit (self, signals[SIGNAL_CONTEXT_MENU], 0, x, y);
		if (!moved && mode == LRG_EDITOR_NAV_MODE_GIZMO && axis >= 0)
		{
			LrgEditorViewCamera before = *camera (self);
			gfloat              sign = (axis < 3) ? 1.0f : -1.0f;
			gint                a = axis % 3;

			lrg_editor_view_camera_align (camera (self), a == 0 ? sign : 0.0f, a == 1 ? sign : 0.0f,
			                              a == 2 ? sign : 0.0f);
			finish_camera_edit (self, &before);
		}
	}
	return TRUE;
}

gboolean
lrg_editor_viewport_nav_scroll (LrgEditorViewportNav *self,
                                gfloat                dx,
                                gfloat                dy,
                                LrgInputModifiers     mods)
{
	LrgEditorViewCamera *cam;
	LrgEditorViewCamera  before;
	gboolean             used = FALSE;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	self->mods = mods;
	if (dx == 0.0f && dy == 0.0f)
		return FALSE;

	/* The wheel tunes the fly speed while flying. */
	if (self->mode == LRG_EDITOR_NAV_MODE_FLY)
	{
		if (dy == 0.0f)
			return FALSE;
		lrg_editor_view_state_set_fly_speed (self->view_state,
		                                     CLAMP (lrg_editor_view_state_get_fly_speed (self->view_state) *
		                                            (gdouble) powf (1.2f, dy), 0.1, 500.0));
		return TRUE;
	}

	/* Zoom; in the trackpad scheme two-finger scroll orbits, Shift pans
	 * and Ctrl zooms. */
	cam = camera (self);
	before = *cam;
	if (self->scheme == LRG_EDITOR_NAV_SCHEME_TRACKPAD && !(mods & LRG_INPUT_MODIFIER_CTRL))
	{
		if ((mods & LRG_INPUT_MODIFIER_SHIFT) || cam->view_2d)
			lrg_editor_view_camera_pan (cam, dx * 12.0f, dy * 12.0f);
		else
			lrg_editor_view_camera_orbit (cam, dx * 4.0f * DEG2RAD, dy * 4.0f * DEG2RAD);
		used = TRUE;
	}
	else if (dy != 0.0f)
	{
		lrg_editor_view_camera_dolly (cam, powf (0.87f, dy));
		used = TRUE;
	}
	finish_camera_edit (self, &before);
	return used;
}

gboolean
lrg_editor_viewport_nav_key_down (LrgEditorViewportNav *self,
                                  GrlKey                key,
                                  LrgInputModifiers     mods)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	self->mods = mods;
	g_hash_table_add (self->held, GINT_TO_POINTER ((gint) key));
	return self->mode == LRG_EDITOR_NAV_MODE_FLY && is_fly_key (key);
}

gboolean
lrg_editor_viewport_nav_key_up (LrgEditorViewportNav *self,
                                GrlKey                key,
                                LrgInputModifiers     mods)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	self->mods = mods;
	g_hash_table_remove (self->held, GINT_TO_POINTER ((gint) key));
	return self->mode == LRG_EDITOR_NAV_MODE_FLY && is_fly_key (key);
}

gboolean
lrg_editor_viewport_nav_is_key_held (LrgEditorViewportNav *self,
                                     GrlKey                key)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	return held (self, key);
}

void
lrg_editor_viewport_nav_cancel (LrgEditorViewportNav *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));

	self->box_armed = FALSE;
	self->nav_axis = -1;
	set_mode (self, LRG_EDITOR_NAV_MODE_NONE);
}

void
lrg_editor_viewport_nav_focus_lost (LrgEditorViewportNav *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self));

	/* Key-ups that happen while unfocused never arrive: forget every held
	 * key so the camera does not keep flying, and drop the gesture. */
	g_hash_table_remove_all (self->held);
	self->mods = LRG_INPUT_MODIFIER_NONE;
	lrg_editor_viewport_nav_cancel (self);
}

gboolean
lrg_editor_viewport_nav_tick (LrgEditorViewportNav *self,
                              gfloat                dt)
{
	LrgEditorViewCamera before;
	gfloat              speed, fwd = 0.0f, right = 0.0f, up = 0.0f;
	gboolean            fast;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_NAV (self), FALSE);

	if (self->mode != LRG_EDITOR_NAV_MODE_FLY || dt <= 0.0f)
		return FALSE;

	/* Held WASD/QE at fly speed, Shift three times faster. */
	fast = held (self, GRL_KEY_LEFT_SHIFT) || held (self, GRL_KEY_RIGHT_SHIFT) ||
	       (self->mods & LRG_INPUT_MODIFIER_SHIFT) != 0;
	speed = (gfloat) lrg_editor_view_state_get_fly_speed (self->view_state) * dt * (fast ? FLY_FAST : 1.0f);
	if (held (self, GRL_KEY_W)) fwd += speed;
	if (held (self, GRL_KEY_S)) fwd -= speed;
	if (held (self, GRL_KEY_D)) right += speed;
	if (held (self, GRL_KEY_A)) right -= speed;
	if (held (self, GRL_KEY_E)) up += speed;
	if (held (self, GRL_KEY_Q)) up -= speed;
	if (fwd == 0.0f && right == 0.0f && up == 0.0f)
		return FALSE;

	before = *camera (self);
	lrg_editor_view_camera_fly (camera (self), fwd, right, up, 0.0f, 0.0f);
	return finish_camera_edit (self, &before);
}
