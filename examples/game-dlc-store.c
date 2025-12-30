/* game-dlc-store.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DLC System Demo
 * Demonstrates: DLC discovery, ownership verification, content gating,
 *               trial support, and store integration.
 *
 * This example shows how to use the DLC system in a game, including:
 *   - Loading and discovering DLCs via LrgModManager
 *   - Querying DLCs by type
 *   - Simulating ownership with LrgDlcOwnershipManifest
 *   - Checking content accessibility (owned vs trial vs locked)
 *   - Opening store pages
 *   - Handling ownership-changed signals
 *
 * Controls:
 *   UP/DOWN    - Navigate DLC list
 *   ENTER      - Toggle ownership (simulate purchase/refund)
 *   SPACE      - Open store page (prints URL)
 *   T          - Toggle trial content view
 *   V          - Verify all DLC ownership
 *   1-6        - Filter by DLC type
 *   0          - Show all DLCs
 *   ESC        - Exit
 */

#include <libregnum.h>
#include <graylib.h>

/* ===== Constants ===== */

#define WINDOW_WIDTH     1024
#define WINDOW_HEIGHT    768
#define LIST_X           40
#define LIST_Y           120
#define LIST_ITEM_HEIGHT 40
#define DETAILS_X        480
#define DETAILS_Y        120
#define FONT_SIZE        20
#define TITLE_SIZE       32

/* ===== Global State ===== */

typedef struct
{
    GrlWindow             *window;
    LrgModManager         *mod_manager;
    LrgDlcOwnershipManifest *ownership;
    GPtrArray             *dlcs;           /* Current filtered list */
    GPtrArray             *all_dlcs;       /* All discovered DLCs */
    gint                   selected_index;
    gboolean               show_trial_content;
    gboolean               filter_all;     /* TRUE = show all, FALSE = filter by filter_type */
    LrgDlcType             filter_type;
    gchar                 *status_message;
    gdouble                status_timer;
} DemoState;

static DemoState g_state = { 0 };

/* ===== Forward Declarations ===== */

static void setup_demo (void);
static void cleanup_demo (void);
static void update_demo (gdouble delta);
static void draw_demo (void);
static void filter_dlcs_all (void);
static void filter_dlcs_by_type (LrgDlcType type);
static void toggle_ownership (LrgDlc *dlc);
static void open_store_page (LrgDlc *dlc);
static void verify_all_ownership (void);
static void set_status (const gchar *message);

static void on_ownership_changed (LrgDlc              *dlc,
                                  LrgDlcOwnershipState state,
                                  gpointer             user_data);

/* ===== Helper Functions ===== */

/**
 * get_ownership_string:
 * @state: the ownership state
 *
 * Returns a display string for the ownership state.
 */
static const gchar *
get_ownership_string (LrgDlcOwnershipState state)
{
    switch (state)
    {
    case LRG_DLC_OWNERSHIP_OWNED:
        return "OWNED";
    case LRG_DLC_OWNERSHIP_NOT_OWNED:
        return "NOT OWNED";
    case LRG_DLC_OWNERSHIP_TRIAL:
        return "TRIAL";
    case LRG_DLC_OWNERSHIP_UNKNOWN:
        return "UNKNOWN";
    case LRG_DLC_OWNERSHIP_ERROR:
        return "ERROR";
    default:
        return "???";
    }
}

/**
 * get_dlc_type_string:
 * @type: the DLC type
 *
 * Returns a display string for the DLC type.
 */
static const gchar *
get_dlc_type_string (LrgDlcType type)
{
    switch (type)
    {
    case LRG_DLC_TYPE_EXPANSION:
        return "Expansion";
    case LRG_DLC_TYPE_COSMETIC:
        return "Cosmetic";
    case LRG_DLC_TYPE_QUEST:
        return "Quest Pack";
    case LRG_DLC_TYPE_ITEM:
        return "Item Pack";
    case LRG_DLC_TYPE_CHARACTER:
        return "Character";
    case LRG_DLC_TYPE_MAP:
        return "Map Pack";
    default:
        return "Unknown";
    }
}

/**
 * get_ownership_color:
 * @state: the ownership state
 *
 * Returns the color to use for displaying the ownership state.
 */
static GrlColor *
get_ownership_color (LrgDlcOwnershipState state)
{
    switch (state)
    {
    case LRG_DLC_OWNERSHIP_OWNED:
        return grl_color_new (100, 255, 100, 255);   /* Green */
    case LRG_DLC_OWNERSHIP_TRIAL:
        return grl_color_new (255, 200, 100, 255);   /* Orange */
    case LRG_DLC_OWNERSHIP_NOT_OWNED:
        return grl_color_new (200, 200, 200, 255);   /* Gray */
    case LRG_DLC_OWNERSHIP_ERROR:
        return grl_color_new (255, 100, 100, 255);   /* Red */
    default:
        return grl_color_new (150, 150, 150, 255);   /* Dim gray */
    }
}

/* ===== Setup and Cleanup ===== */

/**
 * setup_demo:
 *
 * Initializes the demo state, loads DLCs, and sets up ownership checking.
 */
static void
setup_demo (void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dlc_path = NULL;
    guint discovered;
    guint i;

    /* Get mod manager */
    g_state.mod_manager = lrg_mod_manager_get_default ();

    /* Set up search path for DLC discovery */
    dlc_path = g_build_filename (g_get_current_dir (), "data", "dlcs", NULL);
    lrg_mod_manager_add_search_path (g_state.mod_manager, dlc_path);

    /* Discover DLCs */
    discovered = lrg_mod_manager_discover (g_state.mod_manager, &error);
    if (error != NULL)
    {
        g_warning ("Failed to discover DLCs: %s", error->message);
        g_clear_error (&error);
    }

    g_print ("Discovered %u mods/DLCs\n", discovered);

    /* Load all discovered mods */
    if (!lrg_mod_manager_load_all (g_state.mod_manager, &error))
    {
        g_warning ("Failed to load mods: %s", error->message);
        g_clear_error (&error);
    }

    /* Get all DLCs */
    g_state.all_dlcs = lrg_mod_manager_get_dlcs (g_state.mod_manager);
    g_print ("Found %u DLCs\n", g_state.all_dlcs->len);

    /* Create manifest-based ownership checker for simulation */
    g_state.ownership = lrg_dlc_ownership_manifest_new ();

    /* Set up ownership checker for each DLC and connect signals */
    for (i = 0; i < g_state.all_dlcs->len; i++)
    {
        LrgDlc *dlc = g_ptr_array_index (g_state.all_dlcs, i);
        LrgModManifest *manifest = lrg_mod_get_manifest (LRG_MOD (dlc));
        const gchar *dlc_id = lrg_mod_get_id (LRG_MOD (dlc));
        const gchar *dlc_name = lrg_mod_manifest_get_name (manifest);

        /* Set ownership checker */
        lrg_dlc_set_ownership_checker (dlc, LRG_DLC_OWNERSHIP (g_state.ownership));

        /* Register with manifest checker (initially not owned) */
        lrg_dlc_ownership_manifest_set_owned (g_state.ownership, dlc_id, FALSE);

        /* Connect to ownership-changed signal */
        g_signal_connect (dlc, "ownership-changed",
                          G_CALLBACK (on_ownership_changed), NULL);

        g_print ("  - %s (%s)\n", dlc_name ? dlc_name : dlc_id,
                 get_dlc_type_string (lrg_dlc_get_dlc_type (dlc)));
    }

    /* Start with all DLCs */
    g_state.filter_all = TRUE;
    filter_dlcs_all ();

    g_state.selected_index = 0;
    g_state.show_trial_content = FALSE;

    set_status ("DLC Store Demo loaded. Use arrow keys to navigate.");
}

/**
 * cleanup_demo:
 *
 * Cleans up all allocated resources.
 */
static void
cleanup_demo (void)
{
    g_clear_pointer (&g_state.dlcs, g_ptr_array_unref);
    g_clear_pointer (&g_state.all_dlcs, g_ptr_array_unref);
    g_clear_object (&g_state.ownership);
    g_clear_pointer (&g_state.status_message, g_free);
}

/* ===== Filtering ===== */

/**
 * filter_dlcs_all:
 *
 * Shows all DLCs without filtering.
 */
static void
filter_dlcs_all (void)
{
    guint i;

    g_clear_pointer (&g_state.dlcs, g_ptr_array_unref);
    g_state.dlcs = g_ptr_array_new_with_free_func (NULL);

    for (i = 0; i < g_state.all_dlcs->len; i++)
    {
        LrgDlc *dlc = g_ptr_array_index (g_state.all_dlcs, i);
        g_ptr_array_add (g_state.dlcs, dlc);
    }

    g_state.filter_all = TRUE;
    g_state.selected_index = 0;

    set_status ("Showing all DLCs");
}

/**
 * filter_dlcs_by_type:
 * @type: the DLC type to filter by
 *
 * Filters the DLC list by type.
 */
static void
filter_dlcs_by_type (LrgDlcType type)
{
    guint i;

    g_clear_pointer (&g_state.dlcs, g_ptr_array_unref);
    g_state.dlcs = g_ptr_array_new_with_free_func (NULL);

    for (i = 0; i < g_state.all_dlcs->len; i++)
    {
        LrgDlc *dlc = g_ptr_array_index (g_state.all_dlcs, i);

        if (lrg_dlc_get_dlc_type (dlc) == type)
            g_ptr_array_add (g_state.dlcs, dlc);
    }

    g_state.filter_all = FALSE;
    g_state.filter_type = type;
    g_state.selected_index = 0;

    set_status (g_strdup_printf ("Showing %s DLCs", get_dlc_type_string (type)));
}

/* ===== Status Message ===== */

/**
 * set_status:
 * @message: the status message
 *
 * Sets the status message shown at the bottom.
 */
static void
set_status (const gchar *message)
{
    g_free (g_state.status_message);
    g_state.status_message = g_strdup (message);
    g_state.status_timer = 5.0;
}

/* ===== Signal Handlers ===== */

/**
 * on_ownership_changed:
 * @dlc: the DLC whose ownership changed
 * @state: the new ownership state
 * @user_data: unused
 *
 * Called when a DLC's ownership state changes.
 */
static void
on_ownership_changed (LrgDlc              *dlc,
                      LrgDlcOwnershipState state,
                      gpointer             user_data)
{
    LrgModManifest *manifest = lrg_mod_get_manifest (LRG_MOD (dlc));
    const gchar *name = lrg_mod_manifest_get_name (manifest);
    const gchar *state_str = get_ownership_string (state);

    (void)user_data;

    g_print ("Ownership changed: %s -> %s\n", name ? name : "Unknown", state_str);
}

/* ===== Actions ===== */

/**
 * toggle_ownership:
 * @dlc: the DLC to toggle
 *
 * Toggles ownership of a DLC (simulates purchase/refund).
 */
static void
toggle_ownership (LrgDlc *dlc)
{
    LrgModManifest *manifest;
    const gchar *dlc_id;
    const gchar *dlc_name;
    gboolean is_owned;
    g_autoptr(GError) error = NULL;

    manifest = lrg_mod_get_manifest (LRG_MOD (dlc));
    dlc_id = lrg_mod_get_id (LRG_MOD (dlc));
    dlc_name = lrg_mod_manifest_get_name (manifest);
    is_owned = lrg_dlc_is_owned (dlc);

    /* Toggle ownership in manifest */
    lrg_dlc_ownership_manifest_set_owned (g_state.ownership, dlc_id, !is_owned);

    /* Re-verify to update state and emit signal */
    lrg_dlc_verify_ownership (dlc, &error);
    if (error != NULL)
    {
        set_status (g_strdup_printf ("Error: %s", error->message));
        return;
    }

    if (!is_owned)
        set_status (g_strdup_printf ("Purchased: %s", dlc_name ? dlc_name : dlc_id));
    else
        set_status (g_strdup_printf ("Refunded: %s", dlc_name ? dlc_name : dlc_id));
}

/**
 * open_store_page:
 * @dlc: the DLC to open
 *
 * Opens the store page for a DLC.
 */
static void
open_store_page (LrgDlc *dlc)
{
    g_autofree gchar *url = NULL;
    g_autoptr(GError) error = NULL;

    url = lrg_dlc_get_store_url (dlc);
    if (url != NULL)
    {
        g_print ("Store URL: %s\n", url);
        set_status (g_strdup_printf ("Store: %s", url));

        /* Actually open in browser */
        lrg_dlc_open_store_page (dlc, &error);
        if (error != NULL)
            g_print ("Failed to open browser: %s\n", error->message);
    }
    else
    {
        set_status ("No store URL available for this DLC");
    }
}

/**
 * verify_all_ownership:
 *
 * Verifies ownership of all DLCs.
 */
static void
verify_all_ownership (void)
{
    g_autoptr(GError) error = NULL;
    guint owned;

    owned = lrg_mod_manager_verify_all_dlc_ownership (g_state.mod_manager, &error);
    if (error != NULL)
    {
        set_status (g_strdup_printf ("Verification failed: %s", error->message));
        return;
    }

    set_status (g_strdup_printf ("Verified: %u/%u DLCs owned", owned, g_state.all_dlcs->len));
}

/* ===== Update ===== */

/**
 * update_demo:
 * @delta: time since last frame
 *
 * Updates the demo state.
 */
static void
update_demo (gdouble delta)
{
    /* Update status timer */
    if (g_state.status_timer > 0)
        g_state.status_timer -= delta;

    /* Navigation */
    if (grl_input_is_key_pressed (GRL_KEY_UP))
    {
        if (g_state.selected_index > 0)
            g_state.selected_index--;
    }
    if (grl_input_is_key_pressed (GRL_KEY_DOWN))
    {
        if (g_state.selected_index < (gint)g_state.dlcs->len - 1)
            g_state.selected_index++;
    }

    /* Actions on selected DLC */
    if (g_state.dlcs->len > 0)
    {
        LrgDlc *selected = g_ptr_array_index (g_state.dlcs, g_state.selected_index);

        if (grl_input_is_key_pressed (GRL_KEY_ENTER))
            toggle_ownership (selected);

        if (grl_input_is_key_pressed (GRL_KEY_SPACE))
            open_store_page (selected);
    }

    /* Toggle trial content view */
    if (grl_input_is_key_pressed (GRL_KEY_T))
    {
        g_state.show_trial_content = !g_state.show_trial_content;
        if (g_state.show_trial_content)
            set_status ("Showing trial content access");
        else
            set_status ("Showing full content access");
    }

    /* Verify all */
    if (grl_input_is_key_pressed (GRL_KEY_V))
        verify_all_ownership ();

    /* Filter by type */
    if (grl_input_is_key_pressed (GRL_KEY_ZERO))
        filter_dlcs_all ();
    if (grl_input_is_key_pressed (GRL_KEY_ONE))
        filter_dlcs_by_type (LRG_DLC_TYPE_EXPANSION);
    if (grl_input_is_key_pressed (GRL_KEY_TWO))
        filter_dlcs_by_type (LRG_DLC_TYPE_COSMETIC);
    if (grl_input_is_key_pressed (GRL_KEY_THREE))
        filter_dlcs_by_type (LRG_DLC_TYPE_QUEST);
    if (grl_input_is_key_pressed (GRL_KEY_FOUR))
        filter_dlcs_by_type (LRG_DLC_TYPE_ITEM);
    if (grl_input_is_key_pressed (GRL_KEY_FIVE))
        filter_dlcs_by_type (LRG_DLC_TYPE_CHARACTER);
    if (grl_input_is_key_pressed (GRL_KEY_SIX))
        filter_dlcs_by_type (LRG_DLC_TYPE_MAP);
}

/* ===== Drawing ===== */

/**
 * draw_dlc_list:
 *
 * Draws the DLC list on the left side.
 */
static void
draw_dlc_list (void)
{
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) dark = grl_color_new (40, 40, 60, 255);
    g_autoptr(GrlColor) highlight = grl_color_new (60, 60, 100, 255);
    guint i;

    /* Header */
    grl_draw_text ("AVAILABLE DLC", LIST_X, LIST_Y - 40, TITLE_SIZE, white);

    /* List items */
    for (i = 0; i < g_state.dlcs->len; i++)
    {
        LrgDlc *dlc = g_ptr_array_index (g_state.dlcs, i);
        LrgModManifest *manifest = lrg_mod_get_manifest (LRG_MOD (dlc));
        LrgDlcOwnershipState state = lrg_dlc_get_ownership_state (dlc);
        const gchar *name = lrg_mod_manifest_get_name (manifest);
        g_autoptr(GrlColor) state_color = get_ownership_color (state);
        g_autofree gchar *label = NULL;
        gint y;
        gchar marker;

        if (name == NULL)
            name = lrg_mod_get_id (LRG_MOD (dlc));

        y = LIST_Y + (i * LIST_ITEM_HEIGHT);

        /* Selection highlight */
        if ((gint)i == g_state.selected_index)
            grl_draw_rectangle (LIST_X - 10, y - 5, 420, LIST_ITEM_HEIGHT - 2, highlight);
        else if (i % 2 == 0)
            grl_draw_rectangle (LIST_X - 10, y - 5, 420, LIST_ITEM_HEIGHT - 2, dark);

        /* Ownership marker */
        switch (state)
        {
        case LRG_DLC_OWNERSHIP_OWNED:
            marker = '*';
            break;
        case LRG_DLC_OWNERSHIP_TRIAL:
            marker = '~';
            break;
        default:
            marker = ' ';
            break;
        }

        label = g_strdup_printf ("[%c] %s", marker, name);
        grl_draw_text (label, LIST_X, y, FONT_SIZE, white);

        /* Ownership state */
        grl_draw_text (get_ownership_string (state), LIST_X + 300, y, FONT_SIZE, state_color);
    }

    if (g_state.dlcs->len == 0)
    {
        g_autoptr(GrlColor) gray = grl_color_new (150, 150, 150, 255);
        grl_draw_text ("No DLCs found", LIST_X, LIST_Y, FONT_SIZE, gray);
    }
}

/**
 * draw_dlc_details:
 *
 * Draws details for the selected DLC on the right side.
 */
static void
draw_dlc_details (void)
{
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) gray = grl_color_new (180, 180, 180, 255);
    g_autoptr(GrlColor) green = grl_color_new (100, 255, 100, 255);
    g_autoptr(GrlColor) red = grl_color_new (255, 100, 100, 255);
    g_autoptr(GrlColor) yellow = grl_color_new (255, 200, 100, 255);
    LrgDlc *dlc;
    LrgModManifest *manifest;
    LrgDlcType type;
    const gchar *price;
    const gchar *description;
    const gchar *name;
    gint y;

    if (g_state.dlcs->len == 0 || g_state.selected_index < 0)
        return;

    dlc = g_ptr_array_index (g_state.dlcs, g_state.selected_index);
    manifest = lrg_mod_get_manifest (LRG_MOD (dlc));
    type = lrg_dlc_get_dlc_type (dlc);
    price = lrg_dlc_get_price_string (dlc);
    description = lrg_mod_manifest_get_description (manifest);
    name = lrg_mod_manifest_get_name (manifest);
    if (name == NULL)
        name = lrg_mod_get_id (LRG_MOD (dlc));

    /* Header */
    grl_draw_text ("DETAILS", DETAILS_X, LIST_Y - 40, TITLE_SIZE, white);

    y = LIST_Y;

    /* DLC Name */
    grl_draw_text (name, DETAILS_X, y, FONT_SIZE + 4, white);
    y += 35;

    /* Type and Price */
    {
        g_autofree gchar *type_str = g_strdup_printf ("Type: %s", get_dlc_type_string (type));
        grl_draw_text (type_str, DETAILS_X, y, FONT_SIZE, gray);
        y += 25;
    }

    if (price != NULL)
    {
        g_autofree gchar *price_str = g_strdup_printf ("Price: %s", price);
        grl_draw_text (price_str, DETAILS_X, y, FONT_SIZE, green);
        y += 25;
    }

    /* Description */
    y += 10;
    if (description != NULL)
    {
        grl_draw_text (description, DETAILS_X, y, FONT_SIZE - 2, gray);
        y += 50;
    }

    /* Type-specific details */
    y += 10;
    grl_draw_text ("Type Details:", DETAILS_X, y, FONT_SIZE, white);
    y += 25;

    switch (type)
    {
    case LRG_DLC_TYPE_EXPANSION:
        if (LRG_IS_EXPANSION_PACK (dlc))
        {
            LrgExpansionPack *exp = LRG_EXPANSION_PACK (dlc);
            const gchar *campaign = lrg_expansion_pack_get_campaign_name (exp);
            guint level_cap = lrg_expansion_pack_get_level_cap_increase (exp);
            GPtrArray *areas = lrg_expansion_pack_get_new_areas (exp);

            if (campaign != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("Campaign: %s", campaign);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
            if (level_cap > 0)
            {
                g_autofree gchar *str = g_strdup_printf ("+%u Level Cap", level_cap);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
            if (areas != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("%u New Areas", areas->len);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
        }
        break;

    case LRG_DLC_TYPE_QUEST:
        if (LRG_IS_QUEST_PACK (dlc))
        {
            LrgQuestPack *qp = LRG_QUEST_PACK (dlc);
            GPtrArray *quests = lrg_quest_pack_get_quest_ids (qp);
            guint hours = lrg_quest_pack_get_estimated_hours (qp);

            if (quests != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("%u Quests", quests->len);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
            if (hours > 0)
            {
                g_autofree gchar *str = g_strdup_printf ("~%u Hours of Content", hours);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
        }
        break;

    case LRG_DLC_TYPE_CHARACTER:
        if (LRG_IS_CHARACTER_PACK (dlc))
        {
            LrgCharacterPack *cp = LRG_CHARACTER_PACK (dlc);
            gboolean playable = lrg_character_pack_get_is_playable (cp);
            gboolean companion = lrg_character_pack_get_is_companion (cp);

            if (playable)
            {
                grl_draw_text ("Playable Character", DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
            if (companion)
            {
                grl_draw_text ("Companion Character", DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
        }
        break;

    case LRG_DLC_TYPE_MAP:
        if (LRG_IS_MAP_PACK (dlc))
        {
            LrgMapPack *mp = LRG_MAP_PACK (dlc);
            const gchar *biome = lrg_map_pack_get_biome_type (mp);
            GPtrArray *maps = lrg_map_pack_get_map_ids (mp);

            if (biome != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("Biome: %s", biome);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
            if (maps != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("%u Maps", maps->len);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
        }
        break;

    case LRG_DLC_TYPE_ITEM:
        if (LRG_IS_ITEM_PACK (dlc))
        {
            LrgItemPack *ip = LRG_ITEM_PACK (dlc);
            GPtrArray *items = lrg_item_pack_get_item_ids (ip);

            if (items != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("%u Items Included", items->len);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
        }
        break;

    case LRG_DLC_TYPE_COSMETIC:
        if (LRG_IS_COSMETIC_PACK (dlc))
        {
            LrgCosmeticPack *cp = LRG_COSMETIC_PACK (dlc);
            GPtrArray *skins = lrg_cosmetic_pack_get_skin_ids (cp);
            GPtrArray *effects = lrg_cosmetic_pack_get_effect_ids (cp);

            if (skins != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("%u Skins", skins->len);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
            if (effects != NULL)
            {
                g_autofree gchar *str = g_strdup_printf ("%u Effects", effects->len);
                grl_draw_text (str, DETAILS_X, y, FONT_SIZE, gray);
                y += 25;
            }
        }
        break;
    }

    /* Content Access Section */
    y += 20;
    grl_draw_text ("Content Access:", DETAILS_X, y, FONT_SIZE, white);
    y += 25;

    if (lrg_dlc_get_trial_enabled (dlc))
    {
        GPtrArray *trial_ids = lrg_dlc_get_trial_content_ids (dlc);

        grl_draw_text ("Trial Available", DETAILS_X, y, FONT_SIZE, yellow);
        y += 25;

        if (trial_ids != NULL && g_state.show_trial_content)
        {
            guint j;
            for (j = 0; j < trial_ids->len && j < 4; j++)
            {
                const gchar *content_id = g_ptr_array_index (trial_ids, j);
                gboolean accessible = lrg_dlc_is_content_accessible (dlc, content_id);
                g_autofree gchar *str = g_strdup_printf ("  %s %s",
                    accessible ? "[OK]" : "[!!]", content_id);

                grl_draw_text (str, DETAILS_X, y, FONT_SIZE - 2, accessible ? green : red);
                y += 20;
            }
        }
    }
    else
    {
        grl_draw_text ("No Trial Available", DETAILS_X, y, FONT_SIZE, red);
        y += 25;
    }
}

/**
 * draw_controls:
 *
 * Draws the control help at the bottom.
 */
static void
draw_controls (void)
{
    g_autoptr(GrlColor) gray = grl_color_new (150, 150, 150, 255);
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    gint y = WINDOW_HEIGHT - 100;

    grl_draw_text ("Controls:", LIST_X, y, FONT_SIZE, white);
    y += 22;
    grl_draw_text ("UP/DOWN: Navigate | ENTER: Toggle Ownership | SPACE: Store Page", LIST_X, y, FONT_SIZE - 2, gray);
    y += 20;
    grl_draw_text ("T: Trial View | V: Verify All | 0-6: Filter by Type | ESC: Exit", LIST_X, y, FONT_SIZE - 2, gray);

    /* Status message */
    if (g_state.status_timer > 0 && g_state.status_message != NULL)
    {
        g_autoptr(GrlColor) status_color = grl_color_new (100, 200, 255, 255);
        grl_draw_text (g_state.status_message, LIST_X, WINDOW_HEIGHT - 30, FONT_SIZE, status_color);
    }
}

/**
 * draw_header:
 *
 * Draws the header with title and filter info.
 */
static void
draw_header (void)
{
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) cyan = grl_color_new (100, 200, 255, 255);
    g_autofree gchar *filter_str = NULL;

    grl_draw_text ("DLC STORE DEMO", LIST_X, 30, TITLE_SIZE + 8, white);

    if (g_state.filter_all)
        filter_str = g_strdup_printf ("Filter: All (%u DLCs)", g_state.dlcs->len);
    else
        filter_str = g_strdup_printf ("Filter: %s (%u DLCs)",
            get_dlc_type_string (g_state.filter_type), g_state.dlcs->len);

    grl_draw_text (filter_str, LIST_X, 70, FONT_SIZE, cyan);
}

/**
 * draw_demo:
 *
 * Main drawing function.
 */
static void
draw_demo (void)
{
    draw_header ();
    draw_dlc_list ();
    draw_dlc_details ();
    draw_controls ();
}

/* ===== Main ===== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(GrlColor) bg_color = NULL;

    /* Initialize glib type system */
    g_type_ensure (LRG_TYPE_MOD_MANAGER);
    g_type_ensure (LRG_TYPE_DLC);
    g_type_ensure (LRG_TYPE_EXPANSION_PACK);
    g_type_ensure (LRG_TYPE_COSMETIC_PACK);
    g_type_ensure (LRG_TYPE_QUEST_PACK);
    g_type_ensure (LRG_TYPE_ITEM_PACK);
    g_type_ensure (LRG_TYPE_CHARACTER_PACK);
    g_type_ensure (LRG_TYPE_MAP_PACK);
    g_type_ensure (LRG_TYPE_DLC_OWNERSHIP_MANIFEST);

    /* Create window */
    g_state.window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "DLC Store Demo - Libregnum");
    grl_window_set_target_fps (g_state.window, 60);

    /* Setup demo */
    setup_demo ();

    /* Main loop */
    bg_color = grl_color_new (25, 25, 40, 255);

    while (!grl_window_should_close (g_state.window))
    {
        gdouble delta = grl_window_get_frame_time (g_state.window);

        /* Handle exit */
        if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
            break;

        /* Update */
        update_demo (delta);

        /* Draw */
        grl_window_begin_drawing (g_state.window);
        grl_draw_clear_background (bg_color);

        draw_demo ();

        grl_window_end_drawing (g_state.window);
    }

    /* Cleanup */
    cleanup_demo ();
    g_clear_object (&g_state.window);

    return 0;
}
