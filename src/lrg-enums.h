/* lrg-enums.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Enumerations for Libregnum with GType registration.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Error Domains
 * ========================================================================== */

/**
 * LRG_ENGINE_ERROR:
 *
 * Error domain for engine errors.
 */
#define LRG_ENGINE_ERROR (lrg_engine_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_engine_error_quark (void);

/**
 * LrgEngineError:
 * @LRG_ENGINE_ERROR_FAILED: Generic failure
 * @LRG_ENGINE_ERROR_INIT: Initialization error
 * @LRG_ENGINE_ERROR_STATE: Invalid state error
 *
 * Error codes for the engine.
 */
typedef enum
{
    LRG_ENGINE_ERROR_FAILED,
    LRG_ENGINE_ERROR_INIT,
    LRG_ENGINE_ERROR_STATE
} LrgEngineError;

LRG_AVAILABLE_IN_ALL
GType lrg_engine_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ENGINE_ERROR (lrg_engine_error_get_type ())

/**
 * LRG_DATA_LOADER_ERROR:
 *
 * Error domain for data loader errors.
 */
#define LRG_DATA_LOADER_ERROR (lrg_data_loader_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_data_loader_error_quark (void);

/**
 * LrgDataLoaderError:
 * @LRG_DATA_LOADER_ERROR_FAILED: Generic failure
 * @LRG_DATA_LOADER_ERROR_IO: I/O error (file not found, etc.)
 * @LRG_DATA_LOADER_ERROR_PARSE: YAML parsing error
 * @LRG_DATA_LOADER_ERROR_TYPE: Type mismatch or unknown type
 * @LRG_DATA_LOADER_ERROR_PROPERTY: Property error (missing required, invalid value)
 *
 * Error codes for the data loader.
 */
typedef enum
{
    LRG_DATA_LOADER_ERROR_FAILED,
    LRG_DATA_LOADER_ERROR_IO,
    LRG_DATA_LOADER_ERROR_PARSE,
    LRG_DATA_LOADER_ERROR_TYPE,
    LRG_DATA_LOADER_ERROR_PROPERTY
} LrgDataLoaderError;

LRG_AVAILABLE_IN_ALL
GType lrg_data_loader_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DATA_LOADER_ERROR (lrg_data_loader_error_get_type ())

/**
 * LRG_MOD_ERROR:
 *
 * Error domain for mod system errors.
 */
#define LRG_MOD_ERROR (lrg_mod_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_mod_error_quark (void);

/**
 * LRG_ASSET_MANAGER_ERROR:
 *
 * Error domain for asset manager errors.
 */
#define LRG_ASSET_MANAGER_ERROR (lrg_asset_manager_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_asset_manager_error_quark (void);

/**
 * LrgAssetManagerError:
 * @LRG_ASSET_MANAGER_ERROR_NOT_FOUND: Asset file not found in any search path
 * @LRG_ASSET_MANAGER_ERROR_LOAD_FAILED: Asset loading failed
 * @LRG_ASSET_MANAGER_ERROR_INVALID_TYPE: Wrong asset type requested
 *
 * Error codes for the asset manager.
 */
typedef enum
{
    LRG_ASSET_MANAGER_ERROR_NOT_FOUND,
    LRG_ASSET_MANAGER_ERROR_LOAD_FAILED,
    LRG_ASSET_MANAGER_ERROR_INVALID_TYPE
} LrgAssetManagerError;

LRG_AVAILABLE_IN_ALL
GType lrg_asset_manager_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ASSET_MANAGER_ERROR (lrg_asset_manager_error_get_type ())

/**
 * LrgModError:
 * @LRG_MOD_ERROR_FAILED: Generic failure
 * @LRG_MOD_ERROR_NOT_FOUND: Mod not found
 * @LRG_MOD_ERROR_LOAD_FAILED: Failed to load mod
 * @LRG_MOD_ERROR_INVALID_MANIFEST: Invalid manifest format
 * @LRG_MOD_ERROR_MISSING_DEPENDENCY: Required dependency not found
 * @LRG_MOD_ERROR_VERSION: Version constraint not satisfied
 * @LRG_MOD_ERROR_CIRCULAR: Circular dependency detected
 *
 * Error codes for the mod system.
 */
typedef enum
{
    LRG_MOD_ERROR_FAILED,
    LRG_MOD_ERROR_NOT_FOUND,
    LRG_MOD_ERROR_LOAD_FAILED,
    LRG_MOD_ERROR_INVALID_MANIFEST,
    LRG_MOD_ERROR_MISSING_DEPENDENCY,
    LRG_MOD_ERROR_VERSION,
    LRG_MOD_ERROR_CIRCULAR
} LrgModError;

LRG_AVAILABLE_IN_ALL
GType lrg_mod_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MOD_ERROR (lrg_mod_error_get_type ())

/* ==========================================================================
 * Engine State
 * ========================================================================== */

/**
 * LrgEngineState:
 * @LRG_ENGINE_STATE_UNINITIALIZED: Engine not yet initialized
 * @LRG_ENGINE_STATE_INITIALIZING: Engine is starting up
 * @LRG_ENGINE_STATE_RUNNING: Engine is running normally
 * @LRG_ENGINE_STATE_PAUSED: Engine is paused
 * @LRG_ENGINE_STATE_SHUTTING_DOWN: Engine is shutting down
 * @LRG_ENGINE_STATE_TERMINATED: Engine has terminated
 *
 * The state of the engine.
 */
typedef enum
{
    LRG_ENGINE_STATE_UNINITIALIZED,
    LRG_ENGINE_STATE_INITIALIZING,
    LRG_ENGINE_STATE_RUNNING,
    LRG_ENGINE_STATE_PAUSED,
    LRG_ENGINE_STATE_SHUTTING_DOWN,
    LRG_ENGINE_STATE_TERMINATED
} LrgEngineState;

LRG_AVAILABLE_IN_ALL
GType lrg_engine_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ENGINE_STATE (lrg_engine_state_get_type ())

/* ==========================================================================
 * Input Types
 * ========================================================================== */

/**
 * LrgInputBindingType:
 * @LRG_INPUT_BINDING_KEYBOARD: Keyboard key binding
 * @LRG_INPUT_BINDING_MOUSE_BUTTON: Mouse button binding
 * @LRG_INPUT_BINDING_GAMEPAD_BUTTON: Gamepad button binding
 * @LRG_INPUT_BINDING_GAMEPAD_AXIS: Gamepad axis binding
 *
 * Type of input binding.
 */
typedef enum
{
    LRG_INPUT_BINDING_KEYBOARD,
    LRG_INPUT_BINDING_MOUSE_BUTTON,
    LRG_INPUT_BINDING_GAMEPAD_BUTTON,
    LRG_INPUT_BINDING_GAMEPAD_AXIS
} LrgInputBindingType;

LRG_AVAILABLE_IN_ALL
GType lrg_input_binding_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_INPUT_BINDING_TYPE (lrg_input_binding_type_get_type ())

/**
 * LrgInputModifiers:
 * @LRG_INPUT_MODIFIER_NONE: No modifier
 * @LRG_INPUT_MODIFIER_SHIFT: Shift key
 * @LRG_INPUT_MODIFIER_CTRL: Control key
 * @LRG_INPUT_MODIFIER_ALT: Alt key
 *
 * Input modifier flags.
 */
typedef enum /*< flags >*/
{
    LRG_INPUT_MODIFIER_NONE  = 0,
    LRG_INPUT_MODIFIER_SHIFT = 1 << 0,
    LRG_INPUT_MODIFIER_CTRL  = 1 << 1,
    LRG_INPUT_MODIFIER_ALT   = 1 << 2
} LrgInputModifiers;

LRG_AVAILABLE_IN_ALL
GType lrg_input_modifiers_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_INPUT_MODIFIERS (lrg_input_modifiers_get_type ())

/**
 * LrgGamepadType:
 * @LRG_GAMEPAD_TYPE_UNKNOWN: Unknown or undetected controller
 * @LRG_GAMEPAD_TYPE_XBOX: Xbox controller (360, One, Series X|S)
 * @LRG_GAMEPAD_TYPE_PLAYSTATION: PlayStation controller (DS4, DualSense)
 * @LRG_GAMEPAD_TYPE_SWITCH: Nintendo Switch controller (Pro, Joy-Con)
 * @LRG_GAMEPAD_TYPE_STEAM_DECK: Steam Deck controller
 * @LRG_GAMEPAD_TYPE_GENERIC: Generic/unrecognized controller (uses Xbox names)
 *
 * The type of gamepad controller connected. Used for displaying
 * controller-specific button names in UI prompts.
 */
typedef enum
{
    LRG_GAMEPAD_TYPE_UNKNOWN,
    LRG_GAMEPAD_TYPE_XBOX,
    LRG_GAMEPAD_TYPE_PLAYSTATION,
    LRG_GAMEPAD_TYPE_SWITCH,
    LRG_GAMEPAD_TYPE_STEAM_DECK,
    LRG_GAMEPAD_TYPE_GENERIC
} LrgGamepadType;

LRG_AVAILABLE_IN_ALL
GType lrg_gamepad_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_GAMEPAD_TYPE (lrg_gamepad_type_get_type ())

/* ==========================================================================
 * Dialog System
 * ========================================================================== */

/**
 * LRG_DIALOG_ERROR:
 *
 * Error domain for dialog system errors.
 */
#define LRG_DIALOG_ERROR (lrg_dialog_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_dialog_error_quark (void);

/**
 * LrgDialogError:
 * @LRG_DIALOG_ERROR_FAILED: Generic failure
 * @LRG_DIALOG_ERROR_INVALID_NODE: Invalid or missing node reference
 * @LRG_DIALOG_ERROR_NO_TREE: No dialog tree set
 * @LRG_DIALOG_ERROR_CONDITION: Condition evaluation failed
 *
 * Error codes for the dialog system.
 */
typedef enum
{
    LRG_DIALOG_ERROR_FAILED,
    LRG_DIALOG_ERROR_INVALID_NODE,
    LRG_DIALOG_ERROR_NO_TREE,
    LRG_DIALOG_ERROR_CONDITION
} LrgDialogError;

LRG_AVAILABLE_IN_ALL
GType lrg_dialog_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DIALOG_ERROR (lrg_dialog_error_get_type ())

/* ==========================================================================
 * Behavior Tree
 * ========================================================================== */

/**
 * LrgBTStatus:
 * @LRG_BT_STATUS_INVALID: Node has not been ticked yet
 * @LRG_BT_STATUS_SUCCESS: Node succeeded
 * @LRG_BT_STATUS_FAILURE: Node failed
 * @LRG_BT_STATUS_RUNNING: Node is still running
 *
 * Status returned by behavior tree nodes.
 */
typedef enum
{
    LRG_BT_STATUS_INVALID,
    LRG_BT_STATUS_SUCCESS,
    LRG_BT_STATUS_FAILURE,
    LRG_BT_STATUS_RUNNING
} LrgBTStatus;

LRG_AVAILABLE_IN_ALL
GType lrg_bt_status_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BT_STATUS (lrg_bt_status_get_type ())

/* ==========================================================================
 * Quest System
 * ========================================================================== */

/**
 * LrgQuestState:
 * @LRG_QUEST_STATE_AVAILABLE: Quest can be started
 * @LRG_QUEST_STATE_ACTIVE: Quest is in progress
 * @LRG_QUEST_STATE_COMPLETE: Quest completed successfully
 * @LRG_QUEST_STATE_FAILED: Quest failed
 *
 * State of a quest.
 */
typedef enum
{
    LRG_QUEST_STATE_AVAILABLE,
    LRG_QUEST_STATE_ACTIVE,
    LRG_QUEST_STATE_COMPLETE,
    LRG_QUEST_STATE_FAILED
} LrgQuestState;

LRG_AVAILABLE_IN_ALL
GType lrg_quest_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_QUEST_STATE (lrg_quest_state_get_type ())

/**
 * LrgQuestObjectiveType:
 * @LRG_QUEST_OBJECTIVE_KILL: Kill a target
 * @LRG_QUEST_OBJECTIVE_COLLECT: Collect items
 * @LRG_QUEST_OBJECTIVE_INTERACT: Interact with something
 * @LRG_QUEST_OBJECTIVE_REACH: Reach a location
 * @LRG_QUEST_OBJECTIVE_ESCORT: Escort an NPC
 * @LRG_QUEST_OBJECTIVE_CUSTOM: Custom objective type
 *
 * Type of quest objective.
 */
typedef enum
{
    LRG_QUEST_OBJECTIVE_KILL,
    LRG_QUEST_OBJECTIVE_COLLECT,
    LRG_QUEST_OBJECTIVE_INTERACT,
    LRG_QUEST_OBJECTIVE_REACH,
    LRG_QUEST_OBJECTIVE_ESCORT,
    LRG_QUEST_OBJECTIVE_CUSTOM
} LrgQuestObjectiveType;

LRG_AVAILABLE_IN_ALL
GType lrg_quest_objective_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_QUEST_OBJECTIVE_TYPE (lrg_quest_objective_type_get_type ())

/* ==========================================================================
 * Tilemap System
 * ========================================================================== */

/**
 * LrgTileProperty:
 * @LRG_TILE_PROPERTY_NONE: No special properties
 * @LRG_TILE_PROPERTY_SOLID: Tile blocks movement
 * @LRG_TILE_PROPERTY_ANIMATED: Tile has animation frames
 * @LRG_TILE_PROPERTY_HAZARD: Tile causes damage
 *
 * Flags describing tile properties for collision and effects.
 */
typedef enum /*< flags >*/
{
    LRG_TILE_PROPERTY_NONE     = 0,
    LRG_TILE_PROPERTY_SOLID    = 1 << 0,
    LRG_TILE_PROPERTY_ANIMATED = 1 << 1,
    LRG_TILE_PROPERTY_HAZARD   = 1 << 2
} LrgTileProperty;

LRG_AVAILABLE_IN_ALL
GType lrg_tile_property_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILE_PROPERTY (lrg_tile_property_get_type ())

/* ==========================================================================
 * Save System
 * ========================================================================== */

/**
 * LRG_SAVE_ERROR:
 *
 * Error domain for save system errors.
 */
#define LRG_SAVE_ERROR (lrg_save_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_save_error_quark (void);

/**
 * LrgSaveError:
 * @LRG_SAVE_ERROR_FAILED: Generic save operation failure
 * @LRG_SAVE_ERROR_IO: I/O error (file not found, permission denied, etc.)
 * @LRG_SAVE_ERROR_VERSION_MISMATCH: Save file version incompatible
 * @LRG_SAVE_ERROR_CORRUPT: Save file is corrupted
 * @LRG_SAVE_ERROR_NOT_FOUND: Save slot not found
 *
 * Error codes for the save system.
 */
typedef enum
{
    LRG_SAVE_ERROR_FAILED,
    LRG_SAVE_ERROR_IO,
    LRG_SAVE_ERROR_VERSION_MISMATCH,
    LRG_SAVE_ERROR_CORRUPT,
    LRG_SAVE_ERROR_NOT_FOUND
} LrgSaveError;

LRG_AVAILABLE_IN_ALL
GType lrg_save_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SAVE_ERROR (lrg_save_error_get_type ())

/* ==========================================================================
 * Item System
 * ========================================================================== */

/**
 * LrgItemType:
 * @LRG_ITEM_TYPE_GENERIC: Generic item
 * @LRG_ITEM_TYPE_WEAPON: Weapon
 * @LRG_ITEM_TYPE_ARMOR: Armor
 * @LRG_ITEM_TYPE_CONSUMABLE: Consumable item
 * @LRG_ITEM_TYPE_QUEST: Quest item
 * @LRG_ITEM_TYPE_MATERIAL: Crafting material
 *
 * Type of item.
 */
typedef enum
{
    LRG_ITEM_TYPE_GENERIC,
    LRG_ITEM_TYPE_WEAPON,
    LRG_ITEM_TYPE_ARMOR,
    LRG_ITEM_TYPE_CONSUMABLE,
    LRG_ITEM_TYPE_QUEST,
    LRG_ITEM_TYPE_MATERIAL
} LrgItemType;

LRG_AVAILABLE_IN_ALL
GType lrg_item_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ITEM_TYPE (lrg_item_type_get_type ())

/* ==========================================================================
 * Accessibility Types
 * ========================================================================== */

/**
 * LrgColorblindType:
 * @LRG_COLORBLIND_NONE: No colorblind filter
 * @LRG_COLORBLIND_DEUTERANOPIA: Red-green (deutan) colorblindness
 * @LRG_COLORBLIND_PROTANOPIA: Red-green (protan) colorblindness
 * @LRG_COLORBLIND_TRITANOPIA: Blue-yellow colorblindness
 * @LRG_COLORBLIND_ACHROMATOPSIA: Total color blindness
 *
 * Types of colorblindness for accessibility filters.
 */
typedef enum
{
    LRG_COLORBLIND_NONE = 0,
    LRG_COLORBLIND_DEUTERANOPIA,
    LRG_COLORBLIND_PROTANOPIA,
    LRG_COLORBLIND_TRITANOPIA,
    LRG_COLORBLIND_ACHROMATOPSIA
} LrgColorblindType;

LRG_AVAILABLE_IN_ALL
GType lrg_colorblind_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COLORBLIND_TYPE (lrg_colorblind_type_get_type ())

/**
 * LrgColorblindMode:
 * @LRG_COLORBLIND_MODE_SIMULATE: Simulate what colorblind users see
 * @LRG_COLORBLIND_MODE_CORRECT: Correct colors to improve visibility
 *
 * Colorblind filter operating modes.
 */
typedef enum
{
    LRG_COLORBLIND_MODE_SIMULATE = 0,
    LRG_COLORBLIND_MODE_CORRECT
} LrgColorblindMode;

LRG_AVAILABLE_IN_ALL
GType lrg_colorblind_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COLORBLIND_MODE (lrg_colorblind_mode_get_type ())

/* ==========================================================================
 * UI Types
 * ========================================================================== */

/**
 * LrgTextAlignment:
 * @LRG_TEXT_ALIGN_LEFT: Left-aligned text
 * @LRG_TEXT_ALIGN_CENTER: Center-aligned text
 * @LRG_TEXT_ALIGN_RIGHT: Right-aligned text
 *
 * Text alignment for UI labels.
 */
typedef enum
{
    LRG_TEXT_ALIGN_LEFT   = 0,
    LRG_TEXT_ALIGN_CENTER = 1,
    LRG_TEXT_ALIGN_RIGHT  = 2
} LrgTextAlignment;

LRG_AVAILABLE_IN_ALL
GType lrg_text_alignment_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TEXT_ALIGNMENT (lrg_text_alignment_get_type ())

/**
 * LrgUIEventType:
 * @LRG_UI_EVENT_NONE: No event
 * @LRG_UI_EVENT_MOUSE_MOVE: Mouse moved
 * @LRG_UI_EVENT_MOUSE_BUTTON_DOWN: Mouse button pressed
 * @LRG_UI_EVENT_MOUSE_BUTTON_UP: Mouse button released
 * @LRG_UI_EVENT_KEY_DOWN: Key pressed
 * @LRG_UI_EVENT_KEY_UP: Key released
 * @LRG_UI_EVENT_SCROLL: Mouse scroll wheel
 * @LRG_UI_EVENT_FOCUS_IN: Widget gained focus
 * @LRG_UI_EVENT_FOCUS_OUT: Widget lost focus
 * @LRG_UI_EVENT_TEXT_INPUT: Text character input
 *
 * Type of UI event.
 */
typedef enum
{
    LRG_UI_EVENT_NONE              = 0,
    LRG_UI_EVENT_MOUSE_MOVE        = 1,
    LRG_UI_EVENT_MOUSE_BUTTON_DOWN = 2,
    LRG_UI_EVENT_MOUSE_BUTTON_UP   = 3,
    LRG_UI_EVENT_KEY_DOWN          = 4,
    LRG_UI_EVENT_KEY_UP            = 5,
    LRG_UI_EVENT_SCROLL            = 6,
    LRG_UI_EVENT_FOCUS_IN          = 7,
    LRG_UI_EVENT_FOCUS_OUT         = 8,
    LRG_UI_EVENT_TEXT_INPUT        = 9
} LrgUIEventType;

LRG_AVAILABLE_IN_ALL
GType lrg_ui_event_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_UI_EVENT_TYPE (lrg_ui_event_type_get_type ())

/**
 * LrgOrientation:
 * @LRG_ORIENTATION_HORIZONTAL: Horizontal orientation
 * @LRG_ORIENTATION_VERTICAL: Vertical orientation
 *
 * Widget orientation for sliders, progress bars, etc.
 */
typedef enum
{
    LRG_ORIENTATION_HORIZONTAL,
    LRG_ORIENTATION_VERTICAL
} LrgOrientation;

LRG_AVAILABLE_IN_ALL
GType lrg_orientation_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ORIENTATION (lrg_orientation_get_type ())

/**
 * LrgImageScaleMode:
 * @LRG_IMAGE_SCALE_MODE_FIT: Scale to fit, maintain aspect ratio
 * @LRG_IMAGE_SCALE_MODE_FILL: Scale to fill, crop if needed
 * @LRG_IMAGE_SCALE_MODE_STRETCH: Stretch to exact size
 * @LRG_IMAGE_SCALE_MODE_TILE: Tile texture to fill area
 *
 * Scaling mode for image widgets.
 */
typedef enum
{
    LRG_IMAGE_SCALE_MODE_FIT,
    LRG_IMAGE_SCALE_MODE_FILL,
    LRG_IMAGE_SCALE_MODE_STRETCH,
    LRG_IMAGE_SCALE_MODE_TILE
} LrgImageScaleMode;

LRG_AVAILABLE_IN_ALL
GType lrg_image_scale_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_IMAGE_SCALE_MODE (lrg_image_scale_mode_get_type ())

/* ==========================================================================
 * Localization (I18N)
 * ========================================================================== */

/**
 * LRG_I18N_ERROR:
 *
 * Error domain for localization errors.
 */
#define LRG_I18N_ERROR (lrg_i18n_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_i18n_error_quark (void);

/**
 * LrgI18nError:
 * @LRG_I18N_ERROR_FAILED: Generic failure
 * @LRG_I18N_ERROR_NOT_FOUND: String key not found
 * @LRG_I18N_ERROR_LOCALE_NOT_FOUND: Locale not found
 * @LRG_I18N_ERROR_PARSE: Locale file parsing error
 *
 * Error codes for the localization system.
 */
typedef enum
{
    LRG_I18N_ERROR_FAILED,
    LRG_I18N_ERROR_NOT_FOUND,
    LRG_I18N_ERROR_LOCALE_NOT_FOUND,
    LRG_I18N_ERROR_PARSE
} LrgI18nError;

LRG_AVAILABLE_IN_ALL
GType lrg_i18n_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_I18N_ERROR (lrg_i18n_error_get_type ())

/**
 * LrgPluralForm:
 * @LRG_PLURAL_ZERO: Zero quantity
 * @LRG_PLURAL_ONE: Singular (one)
 * @LRG_PLURAL_TWO: Dual (two)
 * @LRG_PLURAL_FEW: Few (language-specific)
 * @LRG_PLURAL_MANY: Many (language-specific)
 * @LRG_PLURAL_OTHER: Other/default form
 *
 * Plural forms for internationalization (CLDR rules).
 */
typedef enum
{
    LRG_PLURAL_ZERO,
    LRG_PLURAL_ONE,
    LRG_PLURAL_TWO,
    LRG_PLURAL_FEW,
    LRG_PLURAL_MANY,
    LRG_PLURAL_OTHER
} LrgPluralForm;

LRG_AVAILABLE_IN_ALL
GType lrg_plural_form_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PLURAL_FORM (lrg_plural_form_get_type ())

/* ==========================================================================
 * Pathfinding
 * ========================================================================== */

/**
 * LRG_PATHFINDING_ERROR:
 *
 * Error domain for pathfinding errors.
 */
#define LRG_PATHFINDING_ERROR (lrg_pathfinding_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_pathfinding_error_quark (void);

/**
 * LrgPathfindingError:
 * @LRG_PATHFINDING_ERROR_FAILED: Generic failure
 * @LRG_PATHFINDING_ERROR_NO_PATH: No valid path found
 * @LRG_PATHFINDING_ERROR_OUT_OF_BOUNDS: Coordinates out of bounds
 * @LRG_PATHFINDING_ERROR_BLOCKED: Start or end cell is blocked
 * @LRG_PATHFINDING_ERROR_NO_GRID: No navigation grid set
 * @LRG_PATHFINDING_ERROR_INVALID_START: Invalid start position
 * @LRG_PATHFINDING_ERROR_INVALID_GOAL: Invalid goal position
 *
 * Error codes for the pathfinding system.
 */
typedef enum
{
    LRG_PATHFINDING_ERROR_FAILED,
    LRG_PATHFINDING_ERROR_NO_PATH,
    LRG_PATHFINDING_ERROR_OUT_OF_BOUNDS,
    LRG_PATHFINDING_ERROR_BLOCKED,
    LRG_PATHFINDING_ERROR_NO_GRID,
    LRG_PATHFINDING_ERROR_INVALID_START,
    LRG_PATHFINDING_ERROR_INVALID_GOAL
} LrgPathfindingError;

LRG_AVAILABLE_IN_ALL
GType lrg_pathfinding_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PATHFINDING_ERROR (lrg_pathfinding_error_get_type ())

/**
 * LrgNavCellFlags:
 * @LRG_NAV_CELL_NONE: No flags set (default walkable)
 * @LRG_NAV_CELL_WALKABLE: Cell is explicitly walkable
 * @LRG_NAV_CELL_BLOCKED: Cell is blocked/impassable
 *
 * Flags describing navigation cell properties.
 */
typedef enum /*< flags >*/
{
    LRG_NAV_CELL_NONE     = 0,
    LRG_NAV_CELL_WALKABLE = 1 << 0,
    LRG_NAV_CELL_BLOCKED  = 1 << 1
} LrgNavCellFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_nav_cell_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NAV_CELL_FLAGS (lrg_nav_cell_flags_get_type ())

/**
 * LrgPathSmoothingMode:
 * @LRG_PATH_SMOOTHING_NONE: No smoothing (raw waypoints)
 * @LRG_PATH_SMOOTHING_SIMPLE: Simple line-of-sight smoothing
 * @LRG_PATH_SMOOTHING_BEZIER: Bezier curve smoothing
 *
 * Path smoothing algorithms.
 */
typedef enum
{
    LRG_PATH_SMOOTHING_NONE,
    LRG_PATH_SMOOTHING_SIMPLE,
    LRG_PATH_SMOOTHING_BEZIER
} LrgPathSmoothingMode;

LRG_AVAILABLE_IN_ALL
GType lrg_path_smoothing_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PATH_SMOOTHING_MODE (lrg_path_smoothing_mode_get_type ())

/* ==========================================================================
 * AI / Behavior Trees (Extended)
 * ========================================================================== */

/**
 * LrgBTParallelPolicy:
 * @LRG_BT_PARALLEL_REQUIRE_ONE: Succeed when any child succeeds
 * @LRG_BT_PARALLEL_REQUIRE_ALL: Succeed only when all children succeed
 *
 * Success policy for parallel composite nodes.
 */
typedef enum
{
    LRG_BT_PARALLEL_REQUIRE_ONE,
    LRG_BT_PARALLEL_REQUIRE_ALL
} LrgBTParallelPolicy;

LRG_AVAILABLE_IN_ALL
GType lrg_bt_parallel_policy_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BT_PARALLEL_POLICY (lrg_bt_parallel_policy_get_type ())

/**
 * LrgBlackboardValueType:
 * @LRG_BLACKBOARD_VALUE_INT: Integer value
 * @LRG_BLACKBOARD_VALUE_FLOAT: Floating point value
 * @LRG_BLACKBOARD_VALUE_BOOL: Boolean value
 * @LRG_BLACKBOARD_VALUE_STRING: String value
 * @LRG_BLACKBOARD_VALUE_OBJECT: GObject pointer
 * @LRG_BLACKBOARD_VALUE_VECTOR2: 2D vector (x, y)
 *
 * Types of values stored in a behavior tree blackboard.
 */
typedef enum
{
    LRG_BLACKBOARD_VALUE_INT,
    LRG_BLACKBOARD_VALUE_FLOAT,
    LRG_BLACKBOARD_VALUE_BOOL,
    LRG_BLACKBOARD_VALUE_STRING,
    LRG_BLACKBOARD_VALUE_OBJECT,
    LRG_BLACKBOARD_VALUE_VECTOR2
} LrgBlackboardValueType;

LRG_AVAILABLE_IN_ALL
GType lrg_blackboard_value_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLACKBOARD_VALUE_TYPE (lrg_blackboard_value_type_get_type ())

/* ==========================================================================
 * Physics
 * ========================================================================== */

/**
 * LrgRigidBodyType:
 * @LRG_RIGID_BODY_DYNAMIC: Fully simulated body (affected by forces and gravity)
 * @LRG_RIGID_BODY_KINEMATIC: Moved by code, affects dynamic bodies
 * @LRG_RIGID_BODY_STATIC: Immovable, affects dynamic bodies
 *
 * Type of rigid body simulation.
 */
typedef enum
{
    LRG_RIGID_BODY_DYNAMIC,
    LRG_RIGID_BODY_KINEMATIC,
    LRG_RIGID_BODY_STATIC
} LrgRigidBodyType;

LRG_AVAILABLE_IN_ALL
GType lrg_rigid_body_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RIGID_BODY_TYPE (lrg_rigid_body_type_get_type ())

/**
 * LrgForceMode:
 * @LRG_FORCE_MODE_FORCE: Apply as continuous force (affected by mass)
 * @LRG_FORCE_MODE_IMPULSE: Apply as instant impulse (affected by mass)
 * @LRG_FORCE_MODE_ACCELERATION: Apply as acceleration (ignores mass)
 * @LRG_FORCE_MODE_VELOCITY_CHANGE: Apply as direct velocity change (ignores mass)
 *
 * Mode of force application to rigid bodies.
 */
typedef enum
{
    LRG_FORCE_MODE_FORCE,
    LRG_FORCE_MODE_IMPULSE,
    LRG_FORCE_MODE_ACCELERATION,
    LRG_FORCE_MODE_VELOCITY_CHANGE
} LrgForceMode;

LRG_AVAILABLE_IN_ALL
GType lrg_force_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FORCE_MODE (lrg_force_mode_get_type ())

/**
 * LrgCollisionShape:
 * @LRG_COLLISION_SHAPE_BOX: Axis-aligned bounding box
 * @LRG_COLLISION_SHAPE_CIRCLE: Circle/sphere collider
 * @LRG_COLLISION_SHAPE_CAPSULE: Capsule (cylinder with hemispherical ends)
 * @LRG_COLLISION_SHAPE_POLYGON: Convex polygon
 *
 * Shape type for collision detection.
 */
typedef enum
{
    LRG_COLLISION_SHAPE_BOX,
    LRG_COLLISION_SHAPE_CIRCLE,
    LRG_COLLISION_SHAPE_CAPSULE,
    LRG_COLLISION_SHAPE_POLYGON
} LrgCollisionShape;

LRG_AVAILABLE_IN_ALL
GType lrg_collision_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COLLISION_SHAPE (lrg_collision_shape_get_type ())

/* ==========================================================================
 * Debug System
 * ========================================================================== */

/**
 * LRG_DEBUG_ERROR:
 *
 * Error domain for debug system errors.
 */
#define LRG_DEBUG_ERROR (lrg_debug_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_debug_error_quark (void);

/**
 * LrgDebugError:
 * @LRG_DEBUG_ERROR_FAILED: Generic failure
 * @LRG_DEBUG_ERROR_COMMAND_NOT_FOUND: Unknown console command
 * @LRG_DEBUG_ERROR_INVALID_ARGS: Invalid command arguments
 *
 * Error codes for the debug system.
 */
typedef enum
{
    LRG_DEBUG_ERROR_FAILED,
    LRG_DEBUG_ERROR_COMMAND_NOT_FOUND,
    LRG_DEBUG_ERROR_INVALID_ARGS
} LrgDebugError;

LRG_AVAILABLE_IN_ALL
GType lrg_debug_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DEBUG_ERROR (lrg_debug_error_get_type ())

/**
 * LrgDebugOverlayFlags:
 * @LRG_DEBUG_OVERLAY_NONE: No debug info displayed
 * @LRG_DEBUG_OVERLAY_FPS: Show frames per second
 * @LRG_DEBUG_OVERLAY_FRAME_TIME: Show frame time in milliseconds
 * @LRG_DEBUG_OVERLAY_MEMORY: Show memory usage
 * @LRG_DEBUG_OVERLAY_ENTITIES: Show entity count
 * @LRG_DEBUG_OVERLAY_PHYSICS: Show physics debug info
 * @LRG_DEBUG_OVERLAY_COLLIDERS: Show collider wireframes
 * @LRG_DEBUG_OVERLAY_PROFILER: Show profiler timings
 * @LRG_DEBUG_OVERLAY_CUSTOM: Show custom debug lines
 * @LRG_DEBUG_OVERLAY_ALL: Show all debug info
 *
 * Flags for controlling what the debug overlay displays.
 */
typedef enum /*< flags >*/
{
    LRG_DEBUG_OVERLAY_NONE      = 0,
    LRG_DEBUG_OVERLAY_FPS       = 1 << 0,
    LRG_DEBUG_OVERLAY_FRAME_TIME = 1 << 1,
    LRG_DEBUG_OVERLAY_MEMORY    = 1 << 2,
    LRG_DEBUG_OVERLAY_ENTITIES  = 1 << 3,
    LRG_DEBUG_OVERLAY_PHYSICS   = 1 << 4,
    LRG_DEBUG_OVERLAY_COLLIDERS = 1 << 5,
    LRG_DEBUG_OVERLAY_PROFILER  = 1 << 6,
    LRG_DEBUG_OVERLAY_CUSTOM    = 1 << 7,
    LRG_DEBUG_OVERLAY_ALL       = 0xFF
} LrgDebugOverlayFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_debug_overlay_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DEBUG_OVERLAY_FLAGS (lrg_debug_overlay_flags_get_type ())

/**
 * LrgProfilerSectionType:
 * @LRG_PROFILER_SECTION_UPDATE: Game logic update
 * @LRG_PROFILER_SECTION_PHYSICS: Physics simulation
 * @LRG_PROFILER_SECTION_RENDER: Rendering
 * @LRG_PROFILER_SECTION_AI: AI/Behavior tree processing
 * @LRG_PROFILER_SECTION_AUDIO: Audio processing
 * @LRG_PROFILER_SECTION_CUSTOM: Custom user-defined section
 *
 * Built-in profiler section types.
 */
typedef enum
{
    LRG_PROFILER_SECTION_UPDATE,
    LRG_PROFILER_SECTION_PHYSICS,
    LRG_PROFILER_SECTION_RENDER,
    LRG_PROFILER_SECTION_AI,
    LRG_PROFILER_SECTION_AUDIO,
    LRG_PROFILER_SECTION_CUSTOM
} LrgProfilerSectionType;

LRG_AVAILABLE_IN_ALL
GType lrg_profiler_section_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PROFILER_SECTION_TYPE (lrg_profiler_section_type_get_type ())

/* ==========================================================================
 * Mod System (Extended)
 * ========================================================================== */

/**
 * LrgModState:
 * @LRG_MOD_STATE_UNLOADED: Mod is not loaded
 * @LRG_MOD_STATE_DISCOVERED: Mod manifest found but not loaded
 * @LRG_MOD_STATE_LOADING: Mod is being loaded
 * @LRG_MOD_STATE_LOADED: Mod is loaded and active
 * @LRG_MOD_STATE_FAILED: Mod failed to load
 * @LRG_MOD_STATE_DISABLED: Mod is disabled by user
 *
 * State of a mod.
 */
typedef enum
{
    LRG_MOD_STATE_UNLOADED,
    LRG_MOD_STATE_DISCOVERED,
    LRG_MOD_STATE_LOADING,
    LRG_MOD_STATE_LOADED,
    LRG_MOD_STATE_FAILED,
    LRG_MOD_STATE_DISABLED
} LrgModState;

LRG_AVAILABLE_IN_ALL
GType lrg_mod_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MOD_STATE (lrg_mod_state_get_type ())

/**
 * LrgModType:
 * @LRG_MOD_TYPE_DATA: Data-only mod (assets, configs, translations)
 * @LRG_MOD_TYPE_SCRIPT: Script mod (Lua or other scripting)
 * @LRG_MOD_TYPE_NATIVE: Native plugin (shared library)
 *
 * Type of mod.
 */
typedef enum
{
    LRG_MOD_TYPE_DATA,
    LRG_MOD_TYPE_SCRIPT,
    LRG_MOD_TYPE_NATIVE
} LrgModType;

LRG_AVAILABLE_IN_ALL
GType lrg_mod_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MOD_TYPE (lrg_mod_type_get_type ())

/**
 * LrgModPriority:
 * @LRG_MOD_PRIORITY_LOWEST: Lowest priority, loaded first
 * @LRG_MOD_PRIORITY_LOW: Low priority
 * @LRG_MOD_PRIORITY_NORMAL: Normal priority (default)
 * @LRG_MOD_PRIORITY_HIGH: High priority
 * @LRG_MOD_PRIORITY_HIGHEST: Highest priority, loaded last
 *
 * Load priority for mods (higher priority overrides lower).
 */
typedef enum
{
    LRG_MOD_PRIORITY_LOWEST  = -100,
    LRG_MOD_PRIORITY_LOW     = -50,
    LRG_MOD_PRIORITY_NORMAL  = 0,
    LRG_MOD_PRIORITY_HIGH    = 50,
    LRG_MOD_PRIORITY_HIGHEST = 100
} LrgModPriority;

LRG_AVAILABLE_IN_ALL
GType lrg_mod_priority_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MOD_PRIORITY (lrg_mod_priority_get_type ())

/* ==========================================================================
 * DLC System
 * ========================================================================== */

/**
 * LRG_DLC_ERROR:
 *
 * Error domain for DLC errors.
 */
#define LRG_DLC_ERROR (lrg_dlc_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_dlc_error_quark (void);

/**
 * LrgDlcError:
 * @LRG_DLC_ERROR_FAILED: Generic failure
 * @LRG_DLC_ERROR_NOT_OWNED: DLC is not owned by the user
 * @LRG_DLC_ERROR_VERIFICATION_FAILED: Ownership verification failed
 * @LRG_DLC_ERROR_INVALID_LICENSE: License file is invalid or corrupted
 * @LRG_DLC_ERROR_STEAM_UNAVAILABLE: Steam client is not available
 * @LRG_DLC_ERROR_CONTENT_GATED: Content is gated (trial mode)
 *
 * Error codes for the DLC system.
 */
typedef enum
{
    LRG_DLC_ERROR_FAILED,
    LRG_DLC_ERROR_NOT_OWNED,
    LRG_DLC_ERROR_VERIFICATION_FAILED,
    LRG_DLC_ERROR_INVALID_LICENSE,
    LRG_DLC_ERROR_STEAM_UNAVAILABLE,
    LRG_DLC_ERROR_CONTENT_GATED
} LrgDlcError;

LRG_AVAILABLE_IN_ALL
GType lrg_dlc_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DLC_ERROR (lrg_dlc_error_get_type ())

/**
 * LrgDlcType:
 * @LRG_DLC_TYPE_EXPANSION: Full expansion pack with new areas and content
 * @LRG_DLC_TYPE_COSMETIC: Cosmetic items (skins, effects, visual changes)
 * @LRG_DLC_TYPE_QUEST: Quest pack with new storylines
 * @LRG_DLC_TYPE_ITEM: Item pack with new equipment/items
 * @LRG_DLC_TYPE_CHARACTER: Character pack (playable characters or companions)
 * @LRG_DLC_TYPE_MAP: Map pack with new levels/areas
 *
 * Type of DLC content.
 */
typedef enum
{
    LRG_DLC_TYPE_EXPANSION,
    LRG_DLC_TYPE_COSMETIC,
    LRG_DLC_TYPE_QUEST,
    LRG_DLC_TYPE_ITEM,
    LRG_DLC_TYPE_CHARACTER,
    LRG_DLC_TYPE_MAP
} LrgDlcType;

LRG_AVAILABLE_IN_ALL
GType lrg_dlc_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DLC_TYPE (lrg_dlc_type_get_type ())

/**
 * LrgDlcOwnershipState:
 * @LRG_DLC_OWNERSHIP_UNKNOWN: Ownership not yet verified
 * @LRG_DLC_OWNERSHIP_NOT_OWNED: User does not own the DLC
 * @LRG_DLC_OWNERSHIP_OWNED: User owns the DLC
 * @LRG_DLC_OWNERSHIP_TRIAL: User has trial access to the DLC
 * @LRG_DLC_OWNERSHIP_ERROR: Error occurred during verification
 *
 * Ownership state of a DLC.
 */
typedef enum
{
    LRG_DLC_OWNERSHIP_UNKNOWN,
    LRG_DLC_OWNERSHIP_NOT_OWNED,
    LRG_DLC_OWNERSHIP_OWNED,
    LRG_DLC_OWNERSHIP_TRIAL,
    LRG_DLC_OWNERSHIP_ERROR
} LrgDlcOwnershipState;

LRG_AVAILABLE_IN_ALL
GType lrg_dlc_ownership_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DLC_OWNERSHIP_STATE (lrg_dlc_ownership_state_get_type ())

/* ==========================================================================
 * Equipment System
 * ========================================================================== */

/**
 * LrgEquipmentSlot:
 * @LRG_EQUIPMENT_SLOT_HEAD: Head slot (helmet, hat)
 * @LRG_EQUIPMENT_SLOT_CHEST: Chest slot (armor, shirt)
 * @LRG_EQUIPMENT_SLOT_LEGS: Legs slot (pants, leggings)
 * @LRG_EQUIPMENT_SLOT_FEET: Feet slot (boots, shoes)
 * @LRG_EQUIPMENT_SLOT_HANDS: Hands slot (gloves, gauntlets)
 * @LRG_EQUIPMENT_SLOT_WEAPON: Main weapon slot
 * @LRG_EQUIPMENT_SLOT_OFFHAND: Off-hand slot (shield, secondary weapon)
 * @LRG_EQUIPMENT_SLOT_ACCESSORY: Accessory slot (ring, amulet)
 *
 * Equipment slot types.
 */
typedef enum
{
    LRG_EQUIPMENT_SLOT_HEAD,
    LRG_EQUIPMENT_SLOT_CHEST,
    LRG_EQUIPMENT_SLOT_LEGS,
    LRG_EQUIPMENT_SLOT_FEET,
    LRG_EQUIPMENT_SLOT_HANDS,
    LRG_EQUIPMENT_SLOT_WEAPON,
    LRG_EQUIPMENT_SLOT_OFFHAND,
    LRG_EQUIPMENT_SLOT_ACCESSORY
} LrgEquipmentSlot;

LRG_AVAILABLE_IN_ALL
GType lrg_equipment_slot_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EQUIPMENT_SLOT (lrg_equipment_slot_get_type ())

/* ==========================================================================
 * Networking System
 * ========================================================================== */

/**
 * LRG_NET_ERROR:
 *
 * Error domain for networking errors.
 */
#define LRG_NET_ERROR (lrg_net_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_net_error_quark (void);

/**
 * LrgNetError:
 * @LRG_NET_ERROR_FAILED: Generic failure
 * @LRG_NET_ERROR_CONNECTION_FAILED: Connection attempt failed
 * @LRG_NET_ERROR_CONNECTION_CLOSED: Connection was closed
 * @LRG_NET_ERROR_MESSAGE_INVALID: Invalid or malformed message
 * @LRG_NET_ERROR_TIMEOUT: Operation timed out
 * @LRG_NET_ERROR_ALREADY_CONNECTED: Already connected
 * @LRG_NET_ERROR_NOT_CONNECTED: Not connected
 * @LRG_NET_ERROR_SEND_FAILED: Failed to send message
 *
 * Error codes for the networking system.
 */
typedef enum
{
    LRG_NET_ERROR_FAILED,
    LRG_NET_ERROR_CONNECTION_FAILED,
    LRG_NET_ERROR_CONNECTION_CLOSED,
    LRG_NET_ERROR_MESSAGE_INVALID,
    LRG_NET_ERROR_TIMEOUT,
    LRG_NET_ERROR_ALREADY_CONNECTED,
    LRG_NET_ERROR_NOT_CONNECTED,
    LRG_NET_ERROR_SEND_FAILED
} LrgNetError;

LRG_AVAILABLE_IN_ALL
GType lrg_net_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NET_ERROR (lrg_net_error_get_type ())

/**
 * LrgNetPeerState:
 * @LRG_NET_PEER_STATE_DISCONNECTED: Peer is not connected
 * @LRG_NET_PEER_STATE_CONNECTING: Peer is connecting
 * @LRG_NET_PEER_STATE_CONNECTED: Peer is connected
 * @LRG_NET_PEER_STATE_DISCONNECTING: Peer is disconnecting
 *
 * State of a network peer.
 */
typedef enum
{
    LRG_NET_PEER_STATE_DISCONNECTED,
    LRG_NET_PEER_STATE_CONNECTING,
    LRG_NET_PEER_STATE_CONNECTED,
    LRG_NET_PEER_STATE_DISCONNECTING
} LrgNetPeerState;

LRG_AVAILABLE_IN_ALL
GType lrg_net_peer_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NET_PEER_STATE (lrg_net_peer_state_get_type ())

/**
 * LrgNetMessageType:
 * @LRG_NET_MESSAGE_TYPE_HANDSHAKE: Initial handshake message
 * @LRG_NET_MESSAGE_TYPE_DATA: Regular data message
 * @LRG_NET_MESSAGE_TYPE_PING: Ping message (latency check)
 * @LRG_NET_MESSAGE_TYPE_PONG: Pong response message
 * @LRG_NET_MESSAGE_TYPE_DISCONNECT: Disconnect notification
 *
 * Type of network message.
 */
typedef enum
{
    LRG_NET_MESSAGE_TYPE_HANDSHAKE,
    LRG_NET_MESSAGE_TYPE_DATA,
    LRG_NET_MESSAGE_TYPE_PING,
    LRG_NET_MESSAGE_TYPE_PONG,
    LRG_NET_MESSAGE_TYPE_DISCONNECT
} LrgNetMessageType;

LRG_AVAILABLE_IN_ALL
GType lrg_net_message_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NET_MESSAGE_TYPE (lrg_net_message_type_get_type ())

/* ==========================================================================
 * Graphics System
 * ========================================================================== */

/**
 * LrgRenderLayer:
 * @LRG_RENDER_LAYER_BACKGROUND: Sky, far background elements
 * @LRG_RENDER_LAYER_WORLD: Main 3D/2D world content
 * @LRG_RENDER_LAYER_EFFECTS: Particles, visual effects
 * @LRG_RENDER_LAYER_UI: 2D UI overlay
 * @LRG_RENDER_LAYER_DEBUG: Debug overlays and information
 *
 * Render layers for organizing draw order.
 */
typedef enum
{
    LRG_RENDER_LAYER_BACKGROUND,
    LRG_RENDER_LAYER_WORLD,
    LRG_RENDER_LAYER_EFFECTS,
    LRG_RENDER_LAYER_UI,
    LRG_RENDER_LAYER_DEBUG
} LrgRenderLayer;

LRG_AVAILABLE_IN_ALL
GType lrg_render_layer_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RENDER_LAYER (lrg_render_layer_get_type ())

/**
 * LrgProjectionType:
 * @LRG_PROJECTION_PERSPECTIVE: Perspective projection (3D depth)
 * @LRG_PROJECTION_ORTHOGRAPHIC: Orthographic projection (no depth distortion)
 *
 * Camera projection types.
 */
typedef enum
{
    LRG_PROJECTION_PERSPECTIVE,
    LRG_PROJECTION_ORTHOGRAPHIC
} LrgProjectionType;

LRG_AVAILABLE_IN_ALL
GType lrg_projection_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PROJECTION_TYPE (lrg_projection_type_get_type ())

/* ==========================================================================
 * 3D World System
 * ========================================================================== */

/**
 * LrgSpawnType:
 * @LRG_SPAWN_TYPE_PLAYER: Player spawn point
 * @LRG_SPAWN_TYPE_ENEMY: Enemy spawn point
 * @LRG_SPAWN_TYPE_NPC: NPC spawn point
 * @LRG_SPAWN_TYPE_ITEM: Item spawn point
 * @LRG_SPAWN_TYPE_GENERIC: Generic spawn point
 *
 * Type of spawn point in a 3D level.
 */
typedef enum
{
    LRG_SPAWN_TYPE_PLAYER,
    LRG_SPAWN_TYPE_ENEMY,
    LRG_SPAWN_TYPE_NPC,
    LRG_SPAWN_TYPE_ITEM,
    LRG_SPAWN_TYPE_GENERIC
} LrgSpawnType;

LRG_AVAILABLE_IN_ALL
GType lrg_spawn_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SPAWN_TYPE (lrg_spawn_type_get_type ())

/**
 * LrgTriggerType:
 * @LRG_TRIGGER_TYPE_ENTER: Triggered when entity enters volume
 * @LRG_TRIGGER_TYPE_EXIT: Triggered when entity exits volume
 * @LRG_TRIGGER_TYPE_INTERACT: Triggered on player interaction
 * @LRG_TRIGGER_TYPE_PROXIMITY: Triggered when entity is within range
 *
 * Type of trigger event in a 3D level.
 */
typedef enum
{
    LRG_TRIGGER_TYPE_ENTER,
    LRG_TRIGGER_TYPE_EXIT,
    LRG_TRIGGER_TYPE_INTERACT,
    LRG_TRIGGER_TYPE_PROXIMITY
} LrgTriggerType;

LRG_AVAILABLE_IN_ALL
GType lrg_trigger_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRIGGER_TYPE (lrg_trigger_type_get_type ())

/**
 * LrgOctreeNodeType:
 * @LRG_OCTREE_NODE_EMPTY: Node contains no objects
 * @LRG_OCTREE_NODE_LEAF: Node is a leaf with objects
 * @LRG_OCTREE_NODE_BRANCH: Node is a branch with children
 *
 * Type of octree node.
 */
typedef enum
{
    LRG_OCTREE_NODE_EMPTY,
    LRG_OCTREE_NODE_LEAF,
    LRG_OCTREE_NODE_BRANCH
} LrgOctreeNodeType;

LRG_AVAILABLE_IN_ALL
GType lrg_octree_node_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_OCTREE_NODE_TYPE (lrg_octree_node_type_get_type ())

/* ==========================================================================
 * Scene System
 * ========================================================================== */

/**
 * LRG_SCENE_ERROR:
 *
 * Error domain for scene system errors.
 */
#define LRG_SCENE_ERROR (lrg_scene_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_scene_error_quark (void);

/**
 * LrgSceneError:
 * @LRG_SCENE_ERROR_FAILED: Generic failure
 * @LRG_SCENE_ERROR_IO: I/O error (file not found, permission denied)
 * @LRG_SCENE_ERROR_PARSE: YAML parsing error
 * @LRG_SCENE_ERROR_INVALID_FORMAT: Invalid scene file format
 * @LRG_SCENE_ERROR_UNKNOWN_PRIMITIVE: Unknown primitive type
 * @LRG_SCENE_ERROR_MISSING_FIELD: Required field missing
 *
 * Error codes for the scene system.
 */
typedef enum
{
    LRG_SCENE_ERROR_FAILED,
    LRG_SCENE_ERROR_IO,
    LRG_SCENE_ERROR_PARSE,
    LRG_SCENE_ERROR_INVALID_FORMAT,
    LRG_SCENE_ERROR_UNKNOWN_PRIMITIVE,
    LRG_SCENE_ERROR_MISSING_FIELD
} LrgSceneError;

LRG_AVAILABLE_IN_ALL
GType lrg_scene_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCENE_ERROR (lrg_scene_error_get_type ())

/**
 * LrgPrimitiveType:
 * @LRG_PRIMITIVE_PLANE: Flat plane (size parameter)
 * @LRG_PRIMITIVE_CUBE: Box/cube (width, height, depth)
 * @LRG_PRIMITIVE_CIRCLE: 2D circle in 3D space (vertices, radius, fill_type)
 * @LRG_PRIMITIVE_UV_SPHERE: UV sphere (segments, rings, radius)
 * @LRG_PRIMITIVE_ICO_SPHERE: Icosphere (subdivisions, radius)
 * @LRG_PRIMITIVE_CYLINDER: Cylinder (vertices, radius, depth, cap_ends)
 * @LRG_PRIMITIVE_CONE: Cone (vertices, radius1, radius2, depth)
 * @LRG_PRIMITIVE_TORUS: Torus (major_segments, minor_segments, major_radius, minor_radius)
 * @LRG_PRIMITIVE_GRID: Grid plane (x_subdivisions, y_subdivisions, size)
 * @LRG_PRIMITIVE_MESH: Custom mesh from vertex/face data (mesh_data parameter)
 * @LRG_PRIMITIVE_RECTANGLE_2D: 2D rectangle (width, height, filled)
 * @LRG_PRIMITIVE_CIRCLE_2D: 2D circle (radius, filled)
 *
 * Primitive types for scene objects.
 * Includes 3D primitives (Blender-compatible) and 2D shapes.
 */
typedef enum
{
    LRG_PRIMITIVE_PLANE,
    LRG_PRIMITIVE_CUBE,
    LRG_PRIMITIVE_CIRCLE,
    LRG_PRIMITIVE_UV_SPHERE,
    LRG_PRIMITIVE_ICO_SPHERE,
    LRG_PRIMITIVE_CYLINDER,
    LRG_PRIMITIVE_CONE,
    LRG_PRIMITIVE_TORUS,
    LRG_PRIMITIVE_GRID,
    LRG_PRIMITIVE_MESH,
    LRG_PRIMITIVE_RECTANGLE_2D,
    LRG_PRIMITIVE_CIRCLE_2D
} LrgPrimitiveType;

LRG_AVAILABLE_IN_ALL
GType lrg_primitive_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PRIMITIVE_TYPE (lrg_primitive_type_get_type ())

/**
 * LrgCircleFillType:
 * @LRG_CIRCLE_FILL_NOTHING: No fill (outline only)
 * @LRG_CIRCLE_FILL_NGON: N-gon fill
 * @LRG_CIRCLE_FILL_TRIFAN: Triangle fan fill
 *
 * Fill types for circle primitives.
 * Corresponds to Blender's circle primitive fill options.
 */
typedef enum
{
    LRG_CIRCLE_FILL_NOTHING,
    LRG_CIRCLE_FILL_NGON,
    LRG_CIRCLE_FILL_TRIFAN
} LrgCircleFillType;

LRG_AVAILABLE_IN_ALL
GType lrg_circle_fill_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CIRCLE_FILL_TYPE (lrg_circle_fill_type_get_type ())

/* ==========================================================================
 * Scripting System
 * ========================================================================== */

/**
 * LRG_SCRIPTING_ERROR:
 *
 * Error domain for scripting system errors.
 */
#define LRG_SCRIPTING_ERROR (lrg_scripting_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_scripting_error_quark (void);

/**
 * LrgScriptingError:
 * @LRG_SCRIPTING_ERROR_FAILED: Generic failure
 * @LRG_SCRIPTING_ERROR_LOAD: Script loading failed
 * @LRG_SCRIPTING_ERROR_SYNTAX: Script syntax error
 * @LRG_SCRIPTING_ERROR_RUNTIME: Runtime error during script execution
 * @LRG_SCRIPTING_ERROR_TYPE: Type conversion or mismatch error
 * @LRG_SCRIPTING_ERROR_NOT_FOUND: Function or variable not found
 * @LRG_SCRIPTING_ERROR_GI_FAILED: GObject Introspection operation failed
 * @LRG_SCRIPTING_ERROR_TYPELIB_NOT_FOUND: Typelib not found
 *
 * Error codes for the scripting system.
 */
typedef enum
{
    LRG_SCRIPTING_ERROR_FAILED,
    LRG_SCRIPTING_ERROR_LOAD,
    LRG_SCRIPTING_ERROR_SYNTAX,
    LRG_SCRIPTING_ERROR_RUNTIME,
    LRG_SCRIPTING_ERROR_TYPE,
    LRG_SCRIPTING_ERROR_NOT_FOUND,
    LRG_SCRIPTING_ERROR_GI_FAILED,
    LRG_SCRIPTING_ERROR_TYPELIB_NOT_FOUND
} LrgScriptingError;

LRG_AVAILABLE_IN_ALL
GType lrg_scripting_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCRIPTING_ERROR (lrg_scripting_error_get_type ())

/**
 * LrgScriptAccessFlags:
 * @LRG_SCRIPT_ACCESS_NONE: No script access (property hidden)
 * @LRG_SCRIPT_ACCESS_READ: Property is readable from scripts
 * @LRG_SCRIPT_ACCESS_WRITE: Property is writable from scripts
 * @LRG_SCRIPT_ACCESS_READWRITE: Full read/write access from scripts
 *
 * Flags controlling script access to GObject properties.
 *
 * Objects implementing #LrgScriptable can return these flags from
 * their get_property_access() implementation to control what scripts
 * can do with each property.
 */
typedef enum /*< flags >*/
{
    LRG_SCRIPT_ACCESS_NONE      = 0,
    LRG_SCRIPT_ACCESS_READ      = 1 << 0,
    LRG_SCRIPT_ACCESS_WRITE     = 1 << 1,
    LRG_SCRIPT_ACCESS_READWRITE = (LRG_SCRIPT_ACCESS_READ | LRG_SCRIPT_ACCESS_WRITE)
} LrgScriptAccessFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_script_access_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCRIPT_ACCESS_FLAGS (lrg_script_access_flags_get_type ())

/* ==========================================================================
 * Economy System (Phase 2)
 * ========================================================================== */

/**
 * LRG_ECONOMY_ERROR:
 *
 * Error domain for economy system errors.
 *
 * Since: 1.0
 */
#define LRG_ECONOMY_ERROR (lrg_economy_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_economy_error_quark (void);

/**
 * LrgEconomyError:
 * @LRG_ECONOMY_ERROR_FAILED: Generic failure
 * @LRG_ECONOMY_ERROR_INSUFFICIENT: Insufficient resources
 * @LRG_ECONOMY_ERROR_INVALID_RESOURCE: Invalid resource reference
 * @LRG_ECONOMY_ERROR_INVALID_RECIPE: Invalid recipe reference
 * @LRG_ECONOMY_ERROR_PRODUCTION_FAILED: Production failed
 *
 * Error codes for the economy system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ECONOMY_ERROR_FAILED,
    LRG_ECONOMY_ERROR_INSUFFICIENT,
    LRG_ECONOMY_ERROR_INVALID_RESOURCE,
    LRG_ECONOMY_ERROR_INVALID_RECIPE,
    LRG_ECONOMY_ERROR_PRODUCTION_FAILED
} LrgEconomyError;

LRG_AVAILABLE_IN_ALL
GType lrg_economy_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ECONOMY_ERROR (lrg_economy_error_get_type ())

/**
 * LrgResourceCategory:
 * @LRG_RESOURCE_CATEGORY_CURRENCY: Monetary currency (gold, coins, credits)
 * @LRG_RESOURCE_CATEGORY_MATERIAL: Raw or crafting materials
 * @LRG_RESOURCE_CATEGORY_FOOD: Food and consumables
 * @LRG_RESOURCE_CATEGORY_ENERGY: Energy, fuel, power
 * @LRG_RESOURCE_CATEGORY_POPULATION: Population, workers, units
 * @LRG_RESOURCE_CATEGORY_RESEARCH: Research points, science
 * @LRG_RESOURCE_CATEGORY_CUSTOM: Custom user-defined category
 *
 * Categories for resource types in the economy system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_RESOURCE_CATEGORY_CURRENCY,
    LRG_RESOURCE_CATEGORY_MATERIAL,
    LRG_RESOURCE_CATEGORY_FOOD,
    LRG_RESOURCE_CATEGORY_ENERGY,
    LRG_RESOURCE_CATEGORY_POPULATION,
    LRG_RESOURCE_CATEGORY_RESEARCH,
    LRG_RESOURCE_CATEGORY_CUSTOM
} LrgResourceCategory;

LRG_AVAILABLE_IN_ALL
GType lrg_resource_category_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RESOURCE_CATEGORY (lrg_resource_category_get_type ())

/* ==========================================================================
 * Building System (Phase 2)
 * ========================================================================== */

/**
 * LRG_BUILDING_ERROR:
 *
 * Error domain for building system errors.
 *
 * Since: 1.0
 */
#define LRG_BUILDING_ERROR (lrg_building_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_building_error_quark (void);

/**
 * LrgBuildingError:
 * @LRG_BUILDING_ERROR_FAILED: Generic failure
 * @LRG_BUILDING_ERROR_INVALID_POSITION: Invalid placement position
 * @LRG_BUILDING_ERROR_AREA_BLOCKED: Placement area is blocked
 * @LRG_BUILDING_ERROR_INVALID_TERRAIN: Terrain type not allowed
 * @LRG_BUILDING_ERROR_INSUFFICIENT_RESOURCES: Not enough resources to build
 * @LRG_BUILDING_ERROR_MAX_LEVEL: Building is already at maximum level
 *
 * Error codes for the building system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_BUILDING_ERROR_FAILED,
    LRG_BUILDING_ERROR_INVALID_POSITION,
    LRG_BUILDING_ERROR_AREA_BLOCKED,
    LRG_BUILDING_ERROR_INVALID_TERRAIN,
    LRG_BUILDING_ERROR_INSUFFICIENT_RESOURCES,
    LRG_BUILDING_ERROR_MAX_LEVEL
} LrgBuildingError;

LRG_AVAILABLE_IN_ALL
GType lrg_building_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BUILDING_ERROR (lrg_building_error_get_type ())

/**
 * LrgBuildingCategory:
 * @LRG_BUILDING_CATEGORY_PRODUCTION: Production buildings (factories, farms)
 * @LRG_BUILDING_CATEGORY_RESIDENTIAL: Residential buildings (houses, apartments)
 * @LRG_BUILDING_CATEGORY_COMMERCIAL: Commercial buildings (shops, markets)
 * @LRG_BUILDING_CATEGORY_INFRASTRUCTURE: Infrastructure (roads, power, water)
 * @LRG_BUILDING_CATEGORY_DECORATION: Decorative buildings (parks, statues)
 * @LRG_BUILDING_CATEGORY_SPECIAL: Special buildings (unique, story-related)
 *
 * Categories for building types.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_BUILDING_CATEGORY_PRODUCTION,
    LRG_BUILDING_CATEGORY_RESIDENTIAL,
    LRG_BUILDING_CATEGORY_COMMERCIAL,
    LRG_BUILDING_CATEGORY_INFRASTRUCTURE,
    LRG_BUILDING_CATEGORY_DECORATION,
    LRG_BUILDING_CATEGORY_SPECIAL
} LrgBuildingCategory;

LRG_AVAILABLE_IN_ALL
GType lrg_building_category_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BUILDING_CATEGORY (lrg_building_category_get_type ())

/**
 * LrgRotation:
 * @LRG_ROTATION_0: No rotation (0 degrees)
 * @LRG_ROTATION_90: 90 degrees clockwise
 * @LRG_ROTATION_180: 180 degrees
 * @LRG_ROTATION_270: 270 degrees clockwise (90 counter-clockwise)
 *
 * Rotation angles for building placement.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ROTATION_0   = 0,
    LRG_ROTATION_90  = 90,
    LRG_ROTATION_180 = 180,
    LRG_ROTATION_270 = 270
} LrgRotation;

LRG_AVAILABLE_IN_ALL
GType lrg_rotation_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ROTATION (lrg_rotation_get_type ())

/**
 * LrgTerrainType:
 * @LRG_TERRAIN_NONE: No terrain (void)
 * @LRG_TERRAIN_GRASS: Grass terrain
 * @LRG_TERRAIN_DIRT: Dirt/earth terrain
 * @LRG_TERRAIN_SAND: Sand terrain
 * @LRG_TERRAIN_WATER: Water terrain
 * @LRG_TERRAIN_ROCK: Rock/stone terrain
 * @LRG_TERRAIN_ROAD: Road/paved terrain
 * @LRG_TERRAIN_SNOW: Snow terrain
 * @LRG_TERRAIN_MUD: Mud terrain
 * @LRG_TERRAIN_ANY: All terrain types
 *
 * Terrain types for building placement validation.
 * These are flags and can be combined.
 *
 * Since: 1.0
 */
typedef enum /*< flags >*/
{
    LRG_TERRAIN_NONE  = 0,
    LRG_TERRAIN_GRASS = 1 << 0,
    LRG_TERRAIN_DIRT  = 1 << 1,
    LRG_TERRAIN_SAND  = 1 << 2,
    LRG_TERRAIN_WATER = 1 << 3,
    LRG_TERRAIN_ROCK  = 1 << 4,
    LRG_TERRAIN_ROAD  = 1 << 5,
    LRG_TERRAIN_SNOW  = 1 << 6,
    LRG_TERRAIN_MUD   = 1 << 7,
    LRG_TERRAIN_ANY   = 0xFF
} LrgTerrainType;

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TERRAIN_TYPE (lrg_terrain_type_get_type ())

/* ==========================================================================
 * Vehicle System (Phase 2)
 * ========================================================================== */

/**
 * LRG_VEHICLE_ERROR:
 *
 * Error domain for vehicle system errors.
 *
 * Since: 1.0
 */
#define LRG_VEHICLE_ERROR (lrg_vehicle_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_vehicle_error_quark (void);

/**
 * LrgVehicleError:
 * @LRG_VEHICLE_ERROR_FAILED: Generic failure
 * @LRG_VEHICLE_ERROR_NO_WHEELS: Vehicle has no wheels attached
 * @LRG_VEHICLE_ERROR_INVALID_ROAD: Invalid road reference
 *
 * Error codes for the vehicle system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VEHICLE_ERROR_FAILED,
    LRG_VEHICLE_ERROR_NO_WHEELS,
    LRG_VEHICLE_ERROR_INVALID_ROAD
} LrgVehicleError;

LRG_AVAILABLE_IN_ALL
GType lrg_vehicle_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VEHICLE_ERROR (lrg_vehicle_error_get_type ())

/**
 * LrgVehicleCameraMode:
 * @LRG_VEHICLE_CAMERA_FOLLOW: Third-person follow camera
 * @LRG_VEHICLE_CAMERA_HOOD: Hood/bonnet camera
 * @LRG_VEHICLE_CAMERA_COCKPIT: First-person cockpit view
 * @LRG_VEHICLE_CAMERA_FREE: Free look around vehicle
 *
 * Camera modes for vehicle cameras.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VEHICLE_CAMERA_FOLLOW,
    LRG_VEHICLE_CAMERA_HOOD,
    LRG_VEHICLE_CAMERA_COCKPIT,
    LRG_VEHICLE_CAMERA_FREE
} LrgVehicleCameraMode;

LRG_AVAILABLE_IN_ALL
GType lrg_vehicle_camera_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VEHICLE_CAMERA_MODE (lrg_vehicle_camera_mode_get_type ())

/**
 * LrgTrafficBehavior:
 * @LRG_TRAFFIC_BEHAVIOR_CALM: Slow, cautious driving
 * @LRG_TRAFFIC_BEHAVIOR_NORMAL: Normal traffic behavior
 * @LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE: Fast, aggressive driving
 *
 * Driving behavior for AI traffic agents.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TRAFFIC_BEHAVIOR_CALM,
    LRG_TRAFFIC_BEHAVIOR_NORMAL,
    LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE
} LrgTrafficBehavior;

LRG_AVAILABLE_IN_ALL
GType lrg_traffic_behavior_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRAFFIC_BEHAVIOR (lrg_traffic_behavior_get_type ())

/**
 * LrgRoadType:
 * @LRG_ROAD_TYPE_HIGHWAY: High-speed highway
 * @LRG_ROAD_TYPE_MAIN: Main road/avenue
 * @LRG_ROAD_TYPE_STREET: Regular street
 * @LRG_ROAD_TYPE_ALLEY: Narrow alley
 * @LRG_ROAD_TYPE_DIRT: Unpaved dirt road
 *
 * Types of roads in a road network.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ROAD_TYPE_HIGHWAY,
    LRG_ROAD_TYPE_MAIN,
    LRG_ROAD_TYPE_STREET,
    LRG_ROAD_TYPE_ALLEY,
    LRG_ROAD_TYPE_DIRT
} LrgRoadType;

LRG_AVAILABLE_IN_ALL
GType lrg_road_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ROAD_TYPE (lrg_road_type_get_type ())

/* ==========================================================================
 * Idle Game System (Phase 2)
 * ========================================================================== */

/**
 * LrgBigNumberFormat:
 * @LRG_BIG_NUMBER_FORMAT_SHORT: Short format (1.5M, 2.3B, 4.7T)
 * @LRG_BIG_NUMBER_FORMAT_SCIENTIFIC: Scientific notation (1.5e6)
 * @LRG_BIG_NUMBER_FORMAT_FULL: Full number with separators (1,500,000)
 *
 * Formatting options for big numbers.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_BIG_NUMBER_FORMAT_SHORT,
    LRG_BIG_NUMBER_FORMAT_SCIENTIFIC,
    LRG_BIG_NUMBER_FORMAT_FULL
} LrgBigNumberFormat;

LRG_AVAILABLE_IN_ALL
GType lrg_big_number_format_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BIG_NUMBER_FORMAT (lrg_big_number_format_get_type ())

/**
 * LrgAutomationMode:
 * @LRG_AUTOMATION_MODE_CLICK: Auto-click (trigger action)
 * @LRG_AUTOMATION_MODE_BUY_ONE: Auto-buy one unit
 * @LRG_AUTOMATION_MODE_BUY_MAX: Auto-buy maximum affordable
 * @LRG_AUTOMATION_MODE_UPGRADE: Auto-upgrade when available
 *
 * Modes for idle game automation.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_AUTOMATION_MODE_CLICK,
    LRG_AUTOMATION_MODE_BUY_ONE,
    LRG_AUTOMATION_MODE_BUY_MAX,
    LRG_AUTOMATION_MODE_UPGRADE
} LrgAutomationMode;

LRG_AVAILABLE_IN_ALL
GType lrg_automation_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_AUTOMATION_MODE (lrg_automation_mode_get_type ())

/**
 * LrgMilestoneCondition:
 * @LRG_MILESTONE_CONDITION_REACH: Reach a target value
 * @LRG_MILESTONE_CONDITION_ACCUMULATE: Accumulate total over time
 * @LRG_MILESTONE_CONDITION_COUNT: Count occurrences
 * @LRG_MILESTONE_CONDITION_PRESTIGE: Prestige a certain number of times
 *
 * Conditions for milestone achievement.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_MILESTONE_CONDITION_REACH,
    LRG_MILESTONE_CONDITION_ACCUMULATE,
    LRG_MILESTONE_CONDITION_COUNT,
    LRG_MILESTONE_CONDITION_PRESTIGE
} LrgMilestoneCondition;

LRG_AVAILABLE_IN_ALL
GType lrg_milestone_condition_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MILESTONE_CONDITION (lrg_milestone_condition_get_type ())

/* ==========================================================================
 * Particle System (Phase 3)
 * ========================================================================== */

/**
 * LRG_PARTICLE_ERROR:
 *
 * Error domain for particle system errors.
 *
 * Since: 1.0
 */
#define LRG_PARTICLE_ERROR (lrg_particle_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_particle_error_quark (void);

/**
 * LrgParticleError:
 * @LRG_PARTICLE_ERROR_FAILED: Generic failure
 * @LRG_PARTICLE_ERROR_POOL_EXHAUSTED: Particle pool is exhausted
 * @LRG_PARTICLE_ERROR_GPU_NOT_AVAILABLE: GPU compute not available
 * @LRG_PARTICLE_ERROR_SHADER_COMPILE: Shader compilation failed
 *
 * Error codes for the particle system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_PARTICLE_ERROR_FAILED,
    LRG_PARTICLE_ERROR_POOL_EXHAUSTED,
    LRG_PARTICLE_ERROR_GPU_NOT_AVAILABLE,
    LRG_PARTICLE_ERROR_SHADER_COMPILE
} LrgParticleError;

LRG_AVAILABLE_IN_ALL
GType lrg_particle_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_ERROR (lrg_particle_error_get_type ())

/**
 * LrgEmissionShape:
 * @LRG_EMISSION_SHAPE_POINT: Emit from a single point
 * @LRG_EMISSION_SHAPE_CIRCLE: Emit from a circle (2D) or sphere surface
 * @LRG_EMISSION_SHAPE_RECTANGLE: Emit from a rectangular area
 * @LRG_EMISSION_SHAPE_CONE: Emit in a cone direction
 * @LRG_EMISSION_SHAPE_MESH: Emit from mesh surface vertices
 *
 * Emission shape types for particle emitters.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_EMISSION_SHAPE_POINT,
    LRG_EMISSION_SHAPE_CIRCLE,
    LRG_EMISSION_SHAPE_RECTANGLE,
    LRG_EMISSION_SHAPE_CONE,
    LRG_EMISSION_SHAPE_MESH
} LrgEmissionShape;

LRG_AVAILABLE_IN_ALL
GType lrg_emission_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EMISSION_SHAPE (lrg_emission_shape_get_type ())

/**
 * LrgParticleRenderMode:
 * @LRG_PARTICLE_RENDER_BILLBOARD: Camera-facing billboard
 * @LRG_PARTICLE_RENDER_STRETCHED_BILLBOARD: Stretched along velocity
 * @LRG_PARTICLE_RENDER_TRAIL: Trail/ribbon rendering
 * @LRG_PARTICLE_RENDER_MESH: Mesh instance rendering
 *
 * Render modes for particles.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_PARTICLE_RENDER_BILLBOARD,
    LRG_PARTICLE_RENDER_STRETCHED_BILLBOARD,
    LRG_PARTICLE_RENDER_TRAIL,
    LRG_PARTICLE_RENDER_MESH
} LrgParticleRenderMode;

LRG_AVAILABLE_IN_ALL
GType lrg_particle_render_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_RENDER_MODE (lrg_particle_render_mode_get_type ())

/**
 * LrgParticleBackendType:
 * @LRG_PARTICLE_BACKEND_CPU: CPU-based simulation
 * @LRG_PARTICLE_BACKEND_GPU: GPU compute shader simulation
 *
 * Backend types for particle simulation.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_PARTICLE_BACKEND_CPU,
    LRG_PARTICLE_BACKEND_GPU
} LrgParticleBackendType;

LRG_AVAILABLE_IN_ALL
GType lrg_particle_backend_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_BACKEND_TYPE (lrg_particle_backend_type_get_type ())

/**
 * LrgParticleBlendMode:
 * @LRG_PARTICLE_BLEND_ADDITIVE: Additive blending (fire, glow)
 * @LRG_PARTICLE_BLEND_ALPHA: Standard alpha blending
 * @LRG_PARTICLE_BLEND_MULTIPLY: Multiply blending (shadows)
 *
 * Blend modes for particle rendering.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_PARTICLE_BLEND_ADDITIVE,
    LRG_PARTICLE_BLEND_ALPHA,
    LRG_PARTICLE_BLEND_MULTIPLY
} LrgParticleBlendMode;

LRG_AVAILABLE_IN_ALL
GType lrg_particle_blend_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_BLEND_MODE (lrg_particle_blend_mode_get_type ())

/**
 * LrgPoolGrowPolicy:
 * @LRG_POOL_GROW_NONE: Pool does not grow; fails when exhausted
 * @LRG_POOL_GROW_DOUBLE: Double capacity when exhausted
 * @LRG_POOL_GROW_LINEAR: Add fixed increment when exhausted
 * @LRG_POOL_GROW_RECYCLE: Recycle oldest alive item when exhausted
 *
 * Growth policy for object pools.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_POOL_GROW_NONE,
    LRG_POOL_GROW_DOUBLE,
    LRG_POOL_GROW_LINEAR,
    LRG_POOL_GROW_RECYCLE
} LrgPoolGrowPolicy;

LRG_AVAILABLE_IN_ALL
GType lrg_pool_grow_policy_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_POOL_GROW_POLICY (lrg_pool_grow_policy_get_type ())

/* ==========================================================================
 * Post-Processing System (Phase 3)
 * ========================================================================== */

/**
 * LRG_POSTPROCESS_ERROR:
 *
 * Error domain for post-processing system errors.
 *
 * Since: 1.0
 */
#define LRG_POSTPROCESS_ERROR (lrg_postprocess_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_postprocess_error_quark (void);

/**
 * LrgPostProcessError:
 * @LRG_POSTPROCESS_ERROR_FAILED: Generic failure
 * @LRG_POSTPROCESS_ERROR_SHADER_COMPILE: Shader compilation failed
 * @LRG_POSTPROCESS_ERROR_TEXTURE_LOAD: Texture loading failed (e.g., LUT)
 * @LRG_POSTPROCESS_ERROR_FRAMEBUFFER: Framebuffer creation failed
 *
 * Error codes for the post-processing system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_POSTPROCESS_ERROR_FAILED,
    LRG_POSTPROCESS_ERROR_SHADER_COMPILE,
    LRG_POSTPROCESS_ERROR_TEXTURE_LOAD,
    LRG_POSTPROCESS_ERROR_FRAMEBUFFER
} LrgPostProcessError;

LRG_AVAILABLE_IN_ALL
GType lrg_postprocess_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_POSTPROCESS_ERROR (lrg_postprocess_error_get_type ())

/**
 * LrgFxaaQuality:
 * @LRG_FXAA_QUALITY_LOW: Low quality (fastest)
 * @LRG_FXAA_QUALITY_MEDIUM: Medium quality
 * @LRG_FXAA_QUALITY_HIGH: High quality
 * @LRG_FXAA_QUALITY_ULTRA: Ultra quality (slowest)
 *
 * Quality presets for FXAA anti-aliasing.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_FXAA_QUALITY_LOW,
    LRG_FXAA_QUALITY_MEDIUM,
    LRG_FXAA_QUALITY_HIGH,
    LRG_FXAA_QUALITY_ULTRA
} LrgFxaaQuality;

LRG_AVAILABLE_IN_ALL
GType lrg_fxaa_quality_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FXAA_QUALITY (lrg_fxaa_quality_get_type ())

/**
 * LrgBokehShape:
 * @LRG_BOKEH_CIRCLE: Circular bokeh
 * @LRG_BOKEH_HEXAGON: Hexagonal bokeh
 * @LRG_BOKEH_OCTAGON: Octagonal bokeh
 *
 * Bokeh shapes for depth of field effect.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_BOKEH_CIRCLE,
    LRG_BOKEH_HEXAGON,
    LRG_BOKEH_OCTAGON
} LrgBokehShape;

LRG_AVAILABLE_IN_ALL
GType lrg_bokeh_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BOKEH_SHAPE (lrg_bokeh_shape_get_type ())

/* ==========================================================================
 * Animation System (Phase 3)
 * ========================================================================== */

/**
 * LRG_ANIMATION_ERROR:
 *
 * Error domain for animation system errors.
 *
 * Since: 1.0
 */
#define LRG_ANIMATION_ERROR (lrg_animation_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_animation_error_quark (void);

/**
 * LrgAnimationError:
 * @LRG_ANIMATION_ERROR_FAILED: Generic failure
 * @LRG_ANIMATION_ERROR_CLIP_NOT_FOUND: Animation clip not found
 * @LRG_ANIMATION_ERROR_STATE_NOT_FOUND: State not found in state machine
 * @LRG_ANIMATION_ERROR_INVALID_TRANSITION: Invalid transition definition
 * @LRG_ANIMATION_ERROR_BONE_NOT_FOUND: Bone not found in skeleton
 * @LRG_ANIMATION_ERROR_IK_FAILED: Inverse kinematics solve failed
 *
 * Error codes for the animation system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ANIMATION_ERROR_FAILED,
    LRG_ANIMATION_ERROR_CLIP_NOT_FOUND,
    LRG_ANIMATION_ERROR_STATE_NOT_FOUND,
    LRG_ANIMATION_ERROR_INVALID_TRANSITION,
    LRG_ANIMATION_ERROR_BONE_NOT_FOUND,
    LRG_ANIMATION_ERROR_IK_FAILED
} LrgAnimationError;

LRG_AVAILABLE_IN_ALL
GType lrg_animation_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIMATION_ERROR (lrg_animation_error_get_type ())

/**
 * LrgAnimationLoopMode:
 * @LRG_ANIMATION_LOOP_NONE: Play once and stop
 * @LRG_ANIMATION_LOOP_REPEAT: Loop from start when finished
 * @LRG_ANIMATION_LOOP_PINGPONG: Play forward then reverse
 * @LRG_ANIMATION_LOOP_CLAMP_FOREVER: Stop at last frame
 *
 * Animation clip loop modes.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ANIMATION_LOOP_NONE,
    LRG_ANIMATION_LOOP_REPEAT,
    LRG_ANIMATION_LOOP_PINGPONG,
    LRG_ANIMATION_LOOP_CLAMP_FOREVER
} LrgAnimationLoopMode;

LRG_AVAILABLE_IN_ALL
GType lrg_animation_loop_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIMATION_LOOP_MODE (lrg_animation_loop_mode_get_type ())

/**
 * LrgAnimatorState:
 * @LRG_ANIMATOR_STOPPED: Not playing
 * @LRG_ANIMATOR_PLAYING: Playing animation
 * @LRG_ANIMATOR_PAUSED: Playback paused
 *
 * Animator playback states.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ANIMATOR_STOPPED,
    LRG_ANIMATOR_PLAYING,
    LRG_ANIMATOR_PAUSED
} LrgAnimatorState;

LRG_AVAILABLE_IN_ALL
GType lrg_animator_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIMATOR_STATE (lrg_animator_state_get_type ())

/**
 * LrgBlendType:
 * @LRG_BLEND_TYPE_1D: 1D threshold-based blending
 * @LRG_BLEND_TYPE_2D_SIMPLE: Simple 2D directional blending
 * @LRG_BLEND_TYPE_2D_FREEFORM: Freeform 2D blending with cartesian positions
 * @LRG_BLEND_TYPE_DIRECT: Direct blend with explicit weights
 *
 * Blend tree types for animation blending.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_BLEND_TYPE_1D,
    LRG_BLEND_TYPE_2D_SIMPLE,
    LRG_BLEND_TYPE_2D_FREEFORM,
    LRG_BLEND_TYPE_DIRECT
} LrgBlendType;

LRG_AVAILABLE_IN_ALL
GType lrg_blend_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLEND_TYPE (lrg_blend_type_get_type ())

/**
 * LrgAnimationParameterType:
 * @LRG_ANIM_PARAM_FLOAT: Floating point parameter
 * @LRG_ANIM_PARAM_INT: Integer parameter
 * @LRG_ANIM_PARAM_BOOL: Boolean parameter
 * @LRG_ANIM_PARAM_TRIGGER: One-shot trigger (auto-resets)
 *
 * Types of animation state machine parameters.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ANIM_PARAM_FLOAT,
    LRG_ANIM_PARAM_INT,
    LRG_ANIM_PARAM_BOOL,
    LRG_ANIM_PARAM_TRIGGER
} LrgAnimationParameterType;

LRG_AVAILABLE_IN_ALL
GType lrg_animation_parameter_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIMATION_PARAMETER_TYPE (lrg_animation_parameter_type_get_type ())

/**
 * LrgLayerBlendMode:
 * @LRG_LAYER_BLEND_OVERRIDE: Override lower layers
 * @LRG_LAYER_BLEND_ADDITIVE: Add to lower layers
 *
 * Blending modes for animation layers.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_LAYER_BLEND_OVERRIDE,
    LRG_LAYER_BLEND_ADDITIVE
} LrgLayerBlendMode;

LRG_AVAILABLE_IN_ALL
GType lrg_layer_blend_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LAYER_BLEND_MODE (lrg_layer_blend_mode_get_type ())

/**
 * LrgTransitionInterruptionSource:
 * @LRG_TRANSITION_INTERRUPT_NONE: Cannot be interrupted
 * @LRG_TRANSITION_INTERRUPT_CURRENT: Current state can interrupt
 * @LRG_TRANSITION_INTERRUPT_NEXT: Next state can interrupt
 * @LRG_TRANSITION_INTERRUPT_BOTH: Both states can interrupt
 *
 * Sources that can interrupt a transition.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TRANSITION_INTERRUPT_NONE,
    LRG_TRANSITION_INTERRUPT_CURRENT,
    LRG_TRANSITION_INTERRUPT_NEXT,
    LRG_TRANSITION_INTERRUPT_BOTH
} LrgTransitionInterruptionSource;

LRG_AVAILABLE_IN_ALL
GType lrg_transition_interruption_source_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRANSITION_INTERRUPTION_SOURCE (lrg_transition_interruption_source_get_type ())

/**
 * LrgConditionComparison:
 * @LRG_CONDITION_EQUALS: Equal to value
 * @LRG_CONDITION_NOT_EQUALS: Not equal to value
 * @LRG_CONDITION_GREATER: Greater than value
 * @LRG_CONDITION_LESS: Less than value
 * @LRG_CONDITION_GREATER_EQUAL: Greater than or equal
 * @LRG_CONDITION_LESS_EQUAL: Less than or equal
 *
 * Comparison operators for transition conditions.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CONDITION_EQUALS,
    LRG_CONDITION_NOT_EQUALS,
    LRG_CONDITION_GREATER,
    LRG_CONDITION_LESS,
    LRG_CONDITION_GREATER_EQUAL,
    LRG_CONDITION_LESS_EQUAL
} LrgConditionComparison;

LRG_AVAILABLE_IN_ALL
GType lrg_condition_comparison_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CONDITION_COMPARISON (lrg_condition_comparison_get_type ())

/**
 * LrgIKSolverType:
 * @LRG_IK_TYPE_FABRIK: Forward And Backward Reaching IK
 * @LRG_IK_TYPE_CCD: Cyclic Coordinate Descent
 * @LRG_IK_TYPE_TWO_BONE: Analytic two-bone solver
 * @LRG_IK_TYPE_LOOK_AT: Look-at rotation solver
 *
 * Inverse kinematics solver types.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_IK_TYPE_FABRIK,
    LRG_IK_TYPE_CCD,
    LRG_IK_TYPE_TWO_BONE,
    LRG_IK_TYPE_LOOK_AT
} LrgIKSolverType;

LRG_AVAILABLE_IN_ALL
GType lrg_ik_solver_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_IK_SOLVER_TYPE (lrg_ik_solver_type_get_type ())

/* ==========================================================================
 * Rich Text System (Phase 3)
 * ========================================================================== */

/**
 * LRG_TEXT_ERROR:
 *
 * Error domain for rich text system errors.
 *
 * Since: 1.0
 */
#define LRG_TEXT_ERROR (lrg_text_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_text_error_quark (void);

/**
 * LrgTextError:
 * @LRG_TEXT_ERROR_FAILED: Generic failure
 * @LRG_TEXT_ERROR_FONT_LOAD: Font loading failed
 * @LRG_TEXT_ERROR_INVALID_MARKUP: Invalid markup syntax
 * @LRG_TEXT_ERROR_SHADER_COMPILE: SDF shader compilation failed
 *
 * Error codes for the rich text system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TEXT_ERROR_FAILED,
    LRG_TEXT_ERROR_FONT_LOAD,
    LRG_TEXT_ERROR_INVALID_MARKUP,
    LRG_TEXT_ERROR_SHADER_COMPILE
} LrgTextError;

LRG_AVAILABLE_IN_ALL
GType lrg_text_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TEXT_ERROR (lrg_text_error_get_type ())

/**
 * LrgTextStyle:
 * @LRG_TEXT_STYLE_NONE: No style
 * @LRG_TEXT_STYLE_BOLD: Bold text
 * @LRG_TEXT_STYLE_ITALIC: Italic text
 * @LRG_TEXT_STYLE_UNDERLINE: Underlined text
 * @LRG_TEXT_STYLE_STRIKETHROUGH: Strikethrough text
 *
 * Text style flags for rich text formatting.
 *
 * Since: 1.0
 */
typedef enum /*< flags >*/
{
    LRG_TEXT_STYLE_NONE          = 0,
    LRG_TEXT_STYLE_BOLD          = 1 << 0,
    LRG_TEXT_STYLE_ITALIC        = 1 << 1,
    LRG_TEXT_STYLE_UNDERLINE     = 1 << 2,
    LRG_TEXT_STYLE_STRIKETHROUGH = 1 << 3
} LrgTextStyle;

LRG_AVAILABLE_IN_ALL
GType lrg_text_style_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TEXT_STYLE (lrg_text_style_get_type ())

/**
 * LrgTextEffectType:
 * @LRG_TEXT_EFFECT_NONE: No effect
 * @LRG_TEXT_EFFECT_SHAKE: Random position jitter
 * @LRG_TEXT_EFFECT_WAVE: Sine wave vertical motion
 * @LRG_TEXT_EFFECT_RAINBOW: Cycling rainbow colors
 * @LRG_TEXT_EFFECT_TYPEWRITER: Progressive character reveal
 * @LRG_TEXT_EFFECT_FADE_IN: Alpha fade in
 * @LRG_TEXT_EFFECT_PULSE: Scale pulsing
 * @LRG_TEXT_EFFECT_CUSTOM: User-defined effect
 *
 * Types of animated text effects.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TEXT_EFFECT_NONE,
    LRG_TEXT_EFFECT_SHAKE,
    LRG_TEXT_EFFECT_WAVE,
    LRG_TEXT_EFFECT_RAINBOW,
    LRG_TEXT_EFFECT_TYPEWRITER,
    LRG_TEXT_EFFECT_FADE_IN,
    LRG_TEXT_EFFECT_PULSE,
    LRG_TEXT_EFFECT_CUSTOM
} LrgTextEffectType;

LRG_AVAILABLE_IN_ALL
GType lrg_text_effect_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TEXT_EFFECT_TYPE (lrg_text_effect_type_get_type ())

/**
 * LrgTextDirection:
 * @LRG_TEXT_DIRECTION_LTR: Left-to-right text
 * @LRG_TEXT_DIRECTION_RTL: Right-to-left text
 * @LRG_TEXT_DIRECTION_AUTO: Auto-detect from content
 *
 * Text direction for bidirectional text support.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TEXT_DIRECTION_LTR,
    LRG_TEXT_DIRECTION_RTL,
    LRG_TEXT_DIRECTION_AUTO
} LrgTextDirection;

LRG_AVAILABLE_IN_ALL
GType lrg_text_direction_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TEXT_DIRECTION (lrg_text_direction_get_type ())

/* ==========================================================================
 * Video Playback System (Phase 3)
 * ========================================================================== */

/**
 * LRG_VIDEO_ERROR:
 *
 * Error domain for video playback errors.
 *
 * Since: 1.0
 */
#define LRG_VIDEO_ERROR (lrg_video_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_video_error_quark (void);

/**
 * LrgVideoError:
 * @LRG_VIDEO_ERROR_NONE: No error
 * @LRG_VIDEO_ERROR_FAILED: Generic failure
 * @LRG_VIDEO_ERROR_NOT_FOUND: Video file not found
 * @LRG_VIDEO_ERROR_FORMAT: Unsupported format
 * @LRG_VIDEO_ERROR_CODEC: Codec not available
 * @LRG_VIDEO_ERROR_DECODE: Decode error
 * @LRG_VIDEO_ERROR_SEEK: Seek operation failed
 * @LRG_VIDEO_ERROR_AUDIO: Audio stream error
 *
 * Error codes for the video playback system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VIDEO_ERROR_NONE,
    LRG_VIDEO_ERROR_FAILED,
    LRG_VIDEO_ERROR_NOT_FOUND,
    LRG_VIDEO_ERROR_FORMAT,
    LRG_VIDEO_ERROR_CODEC,
    LRG_VIDEO_ERROR_DECODE,
    LRG_VIDEO_ERROR_SEEK,
    LRG_VIDEO_ERROR_AUDIO
} LrgVideoError;

LRG_AVAILABLE_IN_ALL
GType lrg_video_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VIDEO_ERROR (lrg_video_error_get_type ())

/**
 * LrgVideoState:
 * @LRG_VIDEO_STATE_STOPPED: Not playing, position at 0
 * @LRG_VIDEO_STATE_LOADING: File is being opened
 * @LRG_VIDEO_STATE_PLAYING: Actively playing
 * @LRG_VIDEO_STATE_PAUSED: Paused at current position
 * @LRG_VIDEO_STATE_FINISHED: Reached end of video
 * @LRG_VIDEO_STATE_ERROR: Error occurred
 *
 * Video player states.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VIDEO_STATE_STOPPED,
    LRG_VIDEO_STATE_LOADING,
    LRG_VIDEO_STATE_PLAYING,
    LRG_VIDEO_STATE_PAUSED,
    LRG_VIDEO_STATE_FINISHED,
    LRG_VIDEO_STATE_ERROR
} LrgVideoState;

LRG_AVAILABLE_IN_ALL
GType lrg_video_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VIDEO_STATE (lrg_video_state_get_type ())

/**
 * LrgSubtitlePosition:
 * @LRG_SUBTITLE_POSITION_BOTTOM: Subtitles at bottom of video
 * @LRG_SUBTITLE_POSITION_TOP: Subtitles at top of video
 *
 * Subtitle positioning.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_SUBTITLE_POSITION_BOTTOM,
    LRG_SUBTITLE_POSITION_TOP
} LrgSubtitlePosition;

LRG_AVAILABLE_IN_ALL
GType lrg_subtitle_position_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SUBTITLE_POSITION (lrg_subtitle_position_get_type ())

/* ==========================================================================
 * Tween Module (Phase 4)
 * ========================================================================== */

/**
 * LrgEasingType:
 * @LRG_EASING_LINEAR: Linear interpolation (no easing)
 * @LRG_EASING_EASE_IN_QUAD: Quadratic ease-in
 * @LRG_EASING_EASE_OUT_QUAD: Quadratic ease-out
 * @LRG_EASING_EASE_IN_OUT_QUAD: Quadratic ease-in-out
 * @LRG_EASING_EASE_IN_CUBIC: Cubic ease-in
 * @LRG_EASING_EASE_OUT_CUBIC: Cubic ease-out
 * @LRG_EASING_EASE_IN_OUT_CUBIC: Cubic ease-in-out
 * @LRG_EASING_EASE_IN_QUART: Quartic ease-in
 * @LRG_EASING_EASE_OUT_QUART: Quartic ease-out
 * @LRG_EASING_EASE_IN_OUT_QUART: Quartic ease-in-out
 * @LRG_EASING_EASE_IN_QUINT: Quintic ease-in
 * @LRG_EASING_EASE_OUT_QUINT: Quintic ease-out
 * @LRG_EASING_EASE_IN_OUT_QUINT: Quintic ease-in-out
 * @LRG_EASING_EASE_IN_SINE: Sinusoidal ease-in
 * @LRG_EASING_EASE_OUT_SINE: Sinusoidal ease-out
 * @LRG_EASING_EASE_IN_OUT_SINE: Sinusoidal ease-in-out
 * @LRG_EASING_EASE_IN_EXPO: Exponential ease-in
 * @LRG_EASING_EASE_OUT_EXPO: Exponential ease-out
 * @LRG_EASING_EASE_IN_OUT_EXPO: Exponential ease-in-out
 * @LRG_EASING_EASE_IN_CIRC: Circular ease-in
 * @LRG_EASING_EASE_OUT_CIRC: Circular ease-out
 * @LRG_EASING_EASE_IN_OUT_CIRC: Circular ease-in-out
 * @LRG_EASING_EASE_IN_BACK: Back ease-in (overshoots)
 * @LRG_EASING_EASE_OUT_BACK: Back ease-out (overshoots)
 * @LRG_EASING_EASE_IN_OUT_BACK: Back ease-in-out (overshoots)
 * @LRG_EASING_EASE_IN_ELASTIC: Elastic ease-in
 * @LRG_EASING_EASE_OUT_ELASTIC: Elastic ease-out
 * @LRG_EASING_EASE_IN_OUT_ELASTIC: Elastic ease-in-out
 * @LRG_EASING_EASE_IN_BOUNCE: Bounce ease-in
 * @LRG_EASING_EASE_OUT_BOUNCE: Bounce ease-out
 * @LRG_EASING_EASE_IN_OUT_BOUNCE: Bounce ease-in-out
 *
 * Easing function types for animation interpolation.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_EASING_LINEAR,
    LRG_EASING_EASE_IN_QUAD,
    LRG_EASING_EASE_OUT_QUAD,
    LRG_EASING_EASE_IN_OUT_QUAD,
    LRG_EASING_EASE_IN_CUBIC,
    LRG_EASING_EASE_OUT_CUBIC,
    LRG_EASING_EASE_IN_OUT_CUBIC,
    LRG_EASING_EASE_IN_QUART,
    LRG_EASING_EASE_OUT_QUART,
    LRG_EASING_EASE_IN_OUT_QUART,
    LRG_EASING_EASE_IN_QUINT,
    LRG_EASING_EASE_OUT_QUINT,
    LRG_EASING_EASE_IN_OUT_QUINT,
    LRG_EASING_EASE_IN_SINE,
    LRG_EASING_EASE_OUT_SINE,
    LRG_EASING_EASE_IN_OUT_SINE,
    LRG_EASING_EASE_IN_EXPO,
    LRG_EASING_EASE_OUT_EXPO,
    LRG_EASING_EASE_IN_OUT_EXPO,
    LRG_EASING_EASE_IN_CIRC,
    LRG_EASING_EASE_OUT_CIRC,
    LRG_EASING_EASE_IN_OUT_CIRC,
    LRG_EASING_EASE_IN_BACK,
    LRG_EASING_EASE_OUT_BACK,
    LRG_EASING_EASE_IN_OUT_BACK,
    LRG_EASING_EASE_IN_ELASTIC,
    LRG_EASING_EASE_OUT_ELASTIC,
    LRG_EASING_EASE_IN_OUT_ELASTIC,
    LRG_EASING_EASE_IN_BOUNCE,
    LRG_EASING_EASE_OUT_BOUNCE,
    LRG_EASING_EASE_IN_OUT_BOUNCE
} LrgEasingType;

LRG_AVAILABLE_IN_ALL
GType lrg_easing_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EASING_TYPE (lrg_easing_type_get_type ())

/**
 * LrgTweenLoopMode:
 * @LRG_TWEEN_LOOP_RESTART: Jump back to start after completing
 * @LRG_TWEEN_LOOP_PING_PONG: Reverse direction at each end
 *
 * Loop behavior for tweens.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TWEEN_LOOP_RESTART,
    LRG_TWEEN_LOOP_PING_PONG
} LrgTweenLoopMode;

LRG_AVAILABLE_IN_ALL
GType lrg_tween_loop_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TWEEN_LOOP_MODE (lrg_tween_loop_mode_get_type ())

/**
 * LrgTweenState:
 * @LRG_TWEEN_STATE_IDLE: Tween not started
 * @LRG_TWEEN_STATE_RUNNING: Tween is actively playing
 * @LRG_TWEEN_STATE_PAUSED: Tween is paused
 * @LRG_TWEEN_STATE_FINISHED: Tween has completed
 *
 * Current state of a tween.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TWEEN_STATE_IDLE,
    LRG_TWEEN_STATE_RUNNING,
    LRG_TWEEN_STATE_PAUSED,
    LRG_TWEEN_STATE_FINISHED
} LrgTweenState;

LRG_AVAILABLE_IN_ALL
GType lrg_tween_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TWEEN_STATE (lrg_tween_state_get_type ())

/* ==========================================================================
 * Scene Transition Enumerations (Phase 4)
 * ========================================================================== */

/**
 * LrgTransitionState:
 * @LRG_TRANSITION_STATE_IDLE: Not currently transitioning
 * @LRG_TRANSITION_STATE_OUT: Transitioning out from current scene
 * @LRG_TRANSITION_STATE_HOLD: Holding at midpoint (scene switch happens here)
 * @LRG_TRANSITION_STATE_IN: Transitioning in to new scene
 * @LRG_TRANSITION_STATE_COMPLETE: Transition has completed
 *
 * States for scene transitions.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TRANSITION_STATE_IDLE,
    LRG_TRANSITION_STATE_OUT,
    LRG_TRANSITION_STATE_HOLD,
    LRG_TRANSITION_STATE_IN,
    LRG_TRANSITION_STATE_COMPLETE
} LrgTransitionState;

LRG_AVAILABLE_IN_ALL
GType lrg_transition_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRANSITION_STATE (lrg_transition_state_get_type ())

/**
 * LrgTransitionDirection:
 * @LRG_TRANSITION_DIRECTION_LEFT: Left direction
 * @LRG_TRANSITION_DIRECTION_RIGHT: Right direction
 * @LRG_TRANSITION_DIRECTION_UP: Up direction
 * @LRG_TRANSITION_DIRECTION_DOWN: Down direction
 *
 * Direction for directional transitions (wipe, slide).
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TRANSITION_DIRECTION_LEFT,
    LRG_TRANSITION_DIRECTION_RIGHT,
    LRG_TRANSITION_DIRECTION_UP,
    LRG_TRANSITION_DIRECTION_DOWN
} LrgTransitionDirection;

LRG_AVAILABLE_IN_ALL
GType lrg_transition_direction_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRANSITION_DIRECTION (lrg_transition_direction_get_type ())

/**
 * LrgSlideMode:
 * @LRG_SLIDE_MODE_PUSH: New scene pushes old scene
 * @LRG_SLIDE_MODE_COVER: New scene slides over old scene
 * @LRG_SLIDE_MODE_REVEAL: Old scene slides away revealing new scene
 *
 * Slide modes for slide transitions.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_SLIDE_MODE_PUSH,
    LRG_SLIDE_MODE_COVER,
    LRG_SLIDE_MODE_REVEAL
} LrgSlideMode;

LRG_AVAILABLE_IN_ALL
GType lrg_slide_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SLIDE_MODE (lrg_slide_mode_get_type ())

/**
 * LrgZoomDirection:
 * @LRG_ZOOM_DIRECTION_IN: Zoom in effect
 * @LRG_ZOOM_DIRECTION_OUT: Zoom out effect
 *
 * Zoom direction for zoom transitions.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ZOOM_DIRECTION_IN,
    LRG_ZOOM_DIRECTION_OUT
} LrgZoomDirection;

LRG_AVAILABLE_IN_ALL
GType lrg_zoom_direction_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ZOOM_DIRECTION (lrg_zoom_direction_get_type ())

/* ==========================================================================
 * 2D Trigger System Enumerations (Phase 4)
 * ========================================================================== */

/**
 * LrgTrigger2DShape:
 * @LRG_TRIGGER2D_SHAPE_RECTANGLE: Rectangular trigger zone
 * @LRG_TRIGGER2D_SHAPE_CIRCLE: Circular trigger zone
 * @LRG_TRIGGER2D_SHAPE_POLYGON: Arbitrary polygon trigger zone
 *
 * Shape types for 2D triggers.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TRIGGER2D_SHAPE_RECTANGLE,
    LRG_TRIGGER2D_SHAPE_CIRCLE,
    LRG_TRIGGER2D_SHAPE_POLYGON
} LrgTrigger2DShape;

LRG_AVAILABLE_IN_ALL
GType lrg_trigger2d_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRIGGER2D_SHAPE (lrg_trigger2d_shape_get_type ())

/**
 * LrgTrigger2DEventType:
 * @LRG_TRIGGER2D_EVENT_ENTER: Entity entered the trigger zone
 * @LRG_TRIGGER2D_EVENT_STAY: Entity remains inside the trigger zone
 * @LRG_TRIGGER2D_EVENT_EXIT: Entity exited the trigger zone
 *
 * Event types for 2D trigger interactions.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TRIGGER2D_EVENT_ENTER,
    LRG_TRIGGER2D_EVENT_STAY,
    LRG_TRIGGER2D_EVENT_EXIT
} LrgTrigger2DEventType;

LRG_AVAILABLE_IN_ALL
GType lrg_trigger2d_event_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRIGGER2D_EVENT_TYPE (lrg_trigger2d_event_type_get_type ())

/* ==========================================================================
 * Texture Atlas / Sprite Sheet Enums
 * ========================================================================== */

/**
 * LrgSpriteSheetFormat:
 * @LRG_SPRITE_SHEET_FORMAT_GRID: Regular grid layout (columns x rows)
 * @LRG_SPRITE_SHEET_FORMAT_ASEPRITE: Aseprite JSON format
 * @LRG_SPRITE_SHEET_FORMAT_TEXTUREPACKER: TexturePacker JSON format
 * @LRG_SPRITE_SHEET_FORMAT_LIBREGNUM: Native Libregnum YAML format
 *
 * Supported sprite sheet data formats.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_SPRITE_SHEET_FORMAT_GRID,
    LRG_SPRITE_SHEET_FORMAT_ASEPRITE,
    LRG_SPRITE_SHEET_FORMAT_TEXTUREPACKER,
    LRG_SPRITE_SHEET_FORMAT_LIBREGNUM
} LrgSpriteSheetFormat;

LRG_AVAILABLE_IN_ALL
GType lrg_sprite_sheet_format_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SPRITE_SHEET_FORMAT (lrg_sprite_sheet_format_get_type ())

/**
 * LrgAtlasPackMethod:
 * @LRG_ATLAS_PACK_METHOD_SHELF: Shelf/row-based packing
 * @LRG_ATLAS_PACK_METHOD_MAXRECTS: MaxRects algorithm
 * @LRG_ATLAS_PACK_METHOD_GUILLOTINE: Guillotine algorithm
 *
 * Packing algorithms for atlas generation.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ATLAS_PACK_METHOD_SHELF,
    LRG_ATLAS_PACK_METHOD_MAXRECTS,
    LRG_ATLAS_PACK_METHOD_GUILLOTINE
} LrgAtlasPackMethod;

LRG_AVAILABLE_IN_ALL
GType lrg_atlas_pack_method_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ATLAS_PACK_METHOD (lrg_atlas_pack_method_get_type ())

/**
 * LrgNineSliceMode:
 * @LRG_NINE_SLICE_MODE_STRETCH: Stretch center and edges
 * @LRG_NINE_SLICE_MODE_TILE: Tile center and edges
 * @LRG_NINE_SLICE_MODE_TILE_FIT: Tile and fit to size (clipping if needed)
 *
 * Rendering modes for nine-slice sprites.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_NINE_SLICE_MODE_STRETCH,
    LRG_NINE_SLICE_MODE_TILE,
    LRG_NINE_SLICE_MODE_TILE_FIT
} LrgNineSliceMode;

LRG_AVAILABLE_IN_ALL
GType lrg_nine_slice_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NINE_SLICE_MODE (lrg_nine_slice_mode_get_type ())

/* ==========================================================================
 * Tutorial System Enums (Phase 4)
 * ========================================================================== */

/**
 * LrgTutorialStepType:
 * @LRG_TUTORIAL_STEP_TEXT: Display text/dialog
 * @LRG_TUTORIAL_STEP_HIGHLIGHT: Highlight a UI element
 * @LRG_TUTORIAL_STEP_INPUT: Wait for specific input
 * @LRG_TUTORIAL_STEP_CONDITION: Wait for condition to be met
 * @LRG_TUTORIAL_STEP_DELAY: Wait for a time delay
 *
 * Types of tutorial steps.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TUTORIAL_STEP_TEXT,
    LRG_TUTORIAL_STEP_HIGHLIGHT,
    LRG_TUTORIAL_STEP_INPUT,
    LRG_TUTORIAL_STEP_CONDITION,
    LRG_TUTORIAL_STEP_DELAY
} LrgTutorialStepType;

LRG_AVAILABLE_IN_ALL
GType lrg_tutorial_step_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TUTORIAL_STEP_TYPE (lrg_tutorial_step_type_get_type ())

/**
 * LrgTutorialState:
 * @LRG_TUTORIAL_STATE_INACTIVE: Tutorial not started
 * @LRG_TUTORIAL_STATE_ACTIVE: Tutorial is running
 * @LRG_TUTORIAL_STATE_PAUSED: Tutorial is paused
 * @LRG_TUTORIAL_STATE_COMPLETED: Tutorial finished successfully
 * @LRG_TUTORIAL_STATE_SKIPPED: Tutorial was skipped by user
 *
 * State of a tutorial.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TUTORIAL_STATE_INACTIVE,
    LRG_TUTORIAL_STATE_ACTIVE,
    LRG_TUTORIAL_STATE_PAUSED,
    LRG_TUTORIAL_STATE_COMPLETED,
    LRG_TUTORIAL_STATE_SKIPPED
} LrgTutorialState;

LRG_AVAILABLE_IN_ALL
GType lrg_tutorial_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TUTORIAL_STATE (lrg_tutorial_state_get_type ())

/**
 * LrgHighlightStyle:
 * @LRG_HIGHLIGHT_STYLE_OUTLINE: Draw outline around target
 * @LRG_HIGHLIGHT_STYLE_GLOW: Glowing effect around target
 * @LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS: Darken everything except target
 * @LRG_HIGHLIGHT_STYLE_SPOTLIGHT: Spotlight effect on target
 *
 * Visual styles for highlighting tutorial targets.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_HIGHLIGHT_STYLE_OUTLINE,
    LRG_HIGHLIGHT_STYLE_GLOW,
    LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS,
    LRG_HIGHLIGHT_STYLE_SPOTLIGHT
} LrgHighlightStyle;

LRG_AVAILABLE_IN_ALL
GType lrg_highlight_style_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_HIGHLIGHT_STYLE (lrg_highlight_style_get_type ())

/**
 * LrgArrowDirection:
 * @LRG_ARROW_DIRECTION_UP: Arrow points up
 * @LRG_ARROW_DIRECTION_DOWN: Arrow points down
 * @LRG_ARROW_DIRECTION_LEFT: Arrow points left
 * @LRG_ARROW_DIRECTION_RIGHT: Arrow points right
 * @LRG_ARROW_DIRECTION_AUTO: Automatically determine best direction
 *
 * Direction for tooltip arrows.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ARROW_DIRECTION_UP,
    LRG_ARROW_DIRECTION_DOWN,
    LRG_ARROW_DIRECTION_LEFT,
    LRG_ARROW_DIRECTION_RIGHT,
    LRG_ARROW_DIRECTION_AUTO
} LrgArrowDirection;

LRG_AVAILABLE_IN_ALL
GType lrg_arrow_direction_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ARROW_DIRECTION (lrg_arrow_direction_get_type ())

/**
 * LrgInputDeviceType:
 * @LRG_INPUT_DEVICE_KEYBOARD: Keyboard input device
 * @LRG_INPUT_DEVICE_MOUSE: Mouse input device
 * @LRG_INPUT_DEVICE_GAMEPAD: Gamepad/controller input device
 * @LRG_INPUT_DEVICE_TOUCH: Touch input device
 *
 * Types of input devices for input prompt display.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_INPUT_DEVICE_KEYBOARD,
    LRG_INPUT_DEVICE_MOUSE,
    LRG_INPUT_DEVICE_GAMEPAD,
    LRG_INPUT_DEVICE_TOUCH
} LrgInputDeviceType;

LRG_AVAILABLE_IN_ALL
GType lrg_input_device_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_INPUT_DEVICE_TYPE (lrg_input_device_type_get_type ())

/**
 * LrgGamepadStyle:
 * @LRG_GAMEPAD_STYLE_XBOX: Xbox-style button labels (A, B, X, Y)
 * @LRG_GAMEPAD_STYLE_PLAYSTATION: PlayStation-style labels (Cross, Circle, Square, Triangle)
 * @LRG_GAMEPAD_STYLE_NINTENDO: Nintendo-style labels (A, B, X, Y - different layout)
 * @LRG_GAMEPAD_STYLE_GENERIC: Generic gamepad labels (1, 2, 3, 4)
 *
 * Gamepad button label styles for input prompts.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_GAMEPAD_STYLE_XBOX,
    LRG_GAMEPAD_STYLE_PLAYSTATION,
    LRG_GAMEPAD_STYLE_NINTENDO,
    LRG_GAMEPAD_STYLE_GENERIC
} LrgGamepadStyle;

LRG_AVAILABLE_IN_ALL
GType lrg_gamepad_style_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_GAMEPAD_STYLE (lrg_gamepad_style_get_type ())

/*
 * Weather Module Enums
 */

/**
 * LrgFogType:
 * @LRG_FOG_TYPE_UNIFORM: Uniform fog density
 * @LRG_FOG_TYPE_LINEAR: Linear fog based on distance
 * @LRG_FOG_TYPE_EXPONENTIAL: Exponential fog falloff
 * @LRG_FOG_TYPE_HEIGHT: Height-based fog
 *
 * Types of fog rendering.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_FOG_TYPE_UNIFORM,
    LRG_FOG_TYPE_LINEAR,
    LRG_FOG_TYPE_EXPONENTIAL,
    LRG_FOG_TYPE_HEIGHT
} LrgFogType;

LRG_AVAILABLE_IN_ALL
GType lrg_fog_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FOG_TYPE (lrg_fog_type_get_type ())

/*
 * Lighting Module Enums
 */

/**
 * LrgLightFalloff:
 * @LRG_LIGHT_FALLOFF_NONE: No falloff (constant intensity)
 * @LRG_LIGHT_FALLOFF_LINEAR: Linear falloff with distance
 * @LRG_LIGHT_FALLOFF_QUADRATIC: Quadratic (realistic) falloff
 *
 * Light intensity falloff types.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_LIGHT_FALLOFF_NONE,
    LRG_LIGHT_FALLOFF_LINEAR,
    LRG_LIGHT_FALLOFF_QUADRATIC
} LrgLightFalloff;

LRG_AVAILABLE_IN_ALL
GType lrg_light_falloff_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LIGHT_FALLOFF (lrg_light_falloff_get_type ())

/**
 * LrgLightBlendMode:
 * @LRG_LIGHT_BLEND_MULTIPLY: Multiply with scene color
 * @LRG_LIGHT_BLEND_ADDITIVE: Add to scene color
 * @LRG_LIGHT_BLEND_SOFT: Soft light blending
 *
 * Light blending modes.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_LIGHT_BLEND_MULTIPLY,
    LRG_LIGHT_BLEND_ADDITIVE,
    LRG_LIGHT_BLEND_SOFT
} LrgLightBlendMode;

LRG_AVAILABLE_IN_ALL
GType lrg_light_blend_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LIGHT_BLEND_MODE (lrg_light_blend_mode_get_type ())

/**
 * LrgShadowMethod:
 * @LRG_SHADOW_METHOD_RAY_CAST: Ray-casting shadows (CPU)
 * @LRG_SHADOW_METHOD_GEOMETRY: Shadow volume geometry (GPU)
 *
 * Shadow calculation methods.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_SHADOW_METHOD_RAY_CAST,
    LRG_SHADOW_METHOD_GEOMETRY
} LrgShadowMethod;

LRG_AVAILABLE_IN_ALL
GType lrg_shadow_method_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADOW_METHOD (lrg_shadow_method_get_type ())

/* ==========================================================================
 * Analytics Module (Phase 5)
 * ========================================================================== */

/**
 * LRG_ANALYTICS_ERROR:
 *
 * Error domain for analytics errors.
 *
 * Since: 1.0
 */
#define LRG_ANALYTICS_ERROR (lrg_analytics_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_analytics_error_quark (void);

/**
 * LrgAnalyticsError:
 * @LRG_ANALYTICS_ERROR_FAILED: Generic failure
 * @LRG_ANALYTICS_ERROR_NETWORK: Network error (connection failed, timeout)
 * @LRG_ANALYTICS_ERROR_CONSENT: Consent not granted
 * @LRG_ANALYTICS_ERROR_DISABLED: Analytics disabled
 * @LRG_ANALYTICS_ERROR_BACKEND: Backend error
 * @LRG_ANALYTICS_ERROR_SERIALIZE: Serialization error
 *
 * Error codes for the analytics system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ANALYTICS_ERROR_FAILED,
    LRG_ANALYTICS_ERROR_NETWORK,
    LRG_ANALYTICS_ERROR_CONSENT,
    LRG_ANALYTICS_ERROR_DISABLED,
    LRG_ANALYTICS_ERROR_BACKEND,
    LRG_ANALYTICS_ERROR_SERIALIZE
} LrgAnalyticsError;

LRG_AVAILABLE_IN_ALL
GType lrg_analytics_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANALYTICS_ERROR (lrg_analytics_error_get_type ())

/**
 * LrgAnalyticsFormat:
 * @LRG_ANALYTICS_FORMAT_JSON: JSON payload format
 * @LRG_ANALYTICS_FORMAT_YAML: YAML payload format
 *
 * Payload formats for analytics events.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ANALYTICS_FORMAT_JSON,
    LRG_ANALYTICS_FORMAT_YAML
} LrgAnalyticsFormat;

LRG_AVAILABLE_IN_ALL
GType lrg_analytics_format_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANALYTICS_FORMAT (lrg_analytics_format_get_type ())

/* ==========================================================================
 * Achievement Module (Phase 5)
 * ========================================================================== */

/**
 * LRG_ACHIEVEMENT_ERROR:
 *
 * Error domain for achievement errors.
 *
 * Since: 1.0
 */
#define LRG_ACHIEVEMENT_ERROR (lrg_achievement_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_achievement_error_quark (void);

/**
 * LrgAchievementError:
 * @LRG_ACHIEVEMENT_ERROR_FAILED: Generic failure
 * @LRG_ACHIEVEMENT_ERROR_NOT_FOUND: Achievement not found
 * @LRG_ACHIEVEMENT_ERROR_ALREADY_UNLOCKED: Achievement already unlocked
 * @LRG_ACHIEVEMENT_ERROR_SAVE: Save operation failed
 * @LRG_ACHIEVEMENT_ERROR_LOAD: Load operation failed
 *
 * Error codes for the achievement system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ACHIEVEMENT_ERROR_FAILED,
    LRG_ACHIEVEMENT_ERROR_NOT_FOUND,
    LRG_ACHIEVEMENT_ERROR_ALREADY_UNLOCKED,
    LRG_ACHIEVEMENT_ERROR_SAVE,
    LRG_ACHIEVEMENT_ERROR_LOAD
} LrgAchievementError;

LRG_AVAILABLE_IN_ALL
GType lrg_achievement_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ACHIEVEMENT_ERROR (lrg_achievement_error_get_type ())

/**
 * LrgNotificationPosition:
 * @LRG_NOTIFICATION_POSITION_TOP_LEFT: Top-left corner
 * @LRG_NOTIFICATION_POSITION_TOP_CENTER: Top center
 * @LRG_NOTIFICATION_POSITION_TOP_RIGHT: Top-right corner
 * @LRG_NOTIFICATION_POSITION_BOTTOM_LEFT: Bottom-left corner
 * @LRG_NOTIFICATION_POSITION_BOTTOM_CENTER: Bottom center
 * @LRG_NOTIFICATION_POSITION_BOTTOM_RIGHT: Bottom-right corner
 *
 * Screen position for notifications.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_NOTIFICATION_POSITION_TOP_LEFT,
    LRG_NOTIFICATION_POSITION_TOP_CENTER,
    LRG_NOTIFICATION_POSITION_TOP_RIGHT,
    LRG_NOTIFICATION_POSITION_BOTTOM_LEFT,
    LRG_NOTIFICATION_POSITION_BOTTOM_CENTER,
    LRG_NOTIFICATION_POSITION_BOTTOM_RIGHT
} LrgNotificationPosition;

LRG_AVAILABLE_IN_ALL
GType lrg_notification_position_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NOTIFICATION_POSITION (lrg_notification_position_get_type ())

/* ==========================================================================
 * Photo Mode Module (Phase 5)
 * ========================================================================== */

/**
 * LRG_PHOTO_MODE_ERROR:
 *
 * Error domain for photo mode errors.
 *
 * Since: 1.0
 */
#define LRG_PHOTO_MODE_ERROR (lrg_photo_mode_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_photo_mode_error_quark (void);

/**
 * LrgPhotoModeError:
 * @LRG_PHOTO_MODE_ERROR_FAILED: Generic failure
 * @LRG_PHOTO_MODE_ERROR_CAPTURE: Screenshot capture failed
 * @LRG_PHOTO_MODE_ERROR_SAVE: Screenshot save failed
 * @LRG_PHOTO_MODE_ERROR_INVALID_FORMAT: Invalid image format
 * @LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE: Photo mode already active
 *
 * Error codes for the photo mode system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_PHOTO_MODE_ERROR_FAILED,
    LRG_PHOTO_MODE_ERROR_CAPTURE,
    LRG_PHOTO_MODE_ERROR_SAVE,
    LRG_PHOTO_MODE_ERROR_INVALID_FORMAT,
    LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE
} LrgPhotoModeError;

LRG_AVAILABLE_IN_ALL
GType lrg_photo_mode_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PHOTO_MODE_ERROR (lrg_photo_mode_error_get_type ())

/**
 * LrgScreenshotFormat:
 * @LRG_SCREENSHOT_FORMAT_PNG: Save as PNG (lossless)
 * @LRG_SCREENSHOT_FORMAT_JPG: Save as JPG (lossy)
 *
 * Image format for screenshots.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_SCREENSHOT_FORMAT_PNG,
    LRG_SCREENSHOT_FORMAT_JPG
} LrgScreenshotFormat;

LRG_AVAILABLE_IN_ALL
GType lrg_screenshot_format_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCREENSHOT_FORMAT (lrg_screenshot_format_get_type ())

/* ==========================================================================
 * Steam Workshop Module (Phase 5)
 * ========================================================================== */

/**
 * LRG_WORKSHOP_ERROR:
 *
 * Error domain for Workshop errors.
 *
 * Since: 1.0
 */
#define LRG_WORKSHOP_ERROR (lrg_workshop_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_workshop_error_quark (void);

/**
 * LrgWorkshopError:
 * @LRG_WORKSHOP_ERROR_FAILED: Generic failure
 * @LRG_WORKSHOP_ERROR_NOT_AVAILABLE: Workshop not available
 * @LRG_WORKSHOP_ERROR_QUERY: Query failed
 * @LRG_WORKSHOP_ERROR_SUBSCRIBE: Subscription failed
 * @LRG_WORKSHOP_ERROR_DOWNLOAD: Download failed
 * @LRG_WORKSHOP_ERROR_UPDATE: Update failed
 * @LRG_WORKSHOP_ERROR_CREATE: Item creation failed
 * @LRG_WORKSHOP_ERROR_DELETE: Item deletion failed
 * @LRG_WORKSHOP_ERROR_BUSY: Operation already in progress
 *
 * Error codes for the Workshop system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_WORKSHOP_ERROR_FAILED,
    LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
    LRG_WORKSHOP_ERROR_QUERY,
    LRG_WORKSHOP_ERROR_SUBSCRIBE,
    LRG_WORKSHOP_ERROR_DOWNLOAD,
    LRG_WORKSHOP_ERROR_UPDATE,
    LRG_WORKSHOP_ERROR_CREATE,
    LRG_WORKSHOP_ERROR_DELETE,
    LRG_WORKSHOP_ERROR_BUSY
} LrgWorkshopError;

LRG_AVAILABLE_IN_ALL
GType lrg_workshop_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WORKSHOP_ERROR (lrg_workshop_error_get_type ())

/* ============================================================================
 * Demo Module Enums
 * ========================================================================== */

/**
 * LrgDemoEndReason:
 * @LRG_DEMO_END_REASON_TIME_LIMIT: Demo time limit reached
 * @LRG_DEMO_END_REASON_CONTENT_COMPLETE: Demo content completed
 * @LRG_DEMO_END_REASON_MANUAL: User manually ended demo
 * @LRG_DEMO_END_REASON_UPGRADED: User upgraded to full version
 *
 * Reasons why a demo session ended.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_DEMO_END_REASON_TIME_LIMIT,
    LRG_DEMO_END_REASON_CONTENT_COMPLETE,
    LRG_DEMO_END_REASON_MANUAL,
    LRG_DEMO_END_REASON_UPGRADED
} LrgDemoEndReason;

LRG_AVAILABLE_IN_ALL
GType lrg_demo_end_reason_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DEMO_END_REASON (lrg_demo_end_reason_get_type ())

/**
 * LrgDemoError:
 * @LRG_DEMO_ERROR_FAILED: Generic demo error
 * @LRG_DEMO_ERROR_CONTENT_GATED: Content is gated in demo mode
 * @LRG_DEMO_ERROR_TIME_EXPIRED: Demo time has expired
 * @LRG_DEMO_ERROR_SAVE_LOCKED: Demo saves cannot be used in full version
 *
 * Error codes for the Demo system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_DEMO_ERROR_FAILED,
    LRG_DEMO_ERROR_CONTENT_GATED,
    LRG_DEMO_ERROR_TIME_EXPIRED,
    LRG_DEMO_ERROR_SAVE_LOCKED
} LrgDemoError;

LRG_AVAILABLE_IN_ALL
GType lrg_demo_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DEMO_ERROR (lrg_demo_error_get_type ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_demo_error_quark (void);
#define LRG_DEMO_ERROR (lrg_demo_error_quark ())

/* ============================================================================
 * VR Module Enums
 * ========================================================================== */

/**
 * LrgVREye:
 * @LRG_VR_EYE_LEFT: Left eye
 * @LRG_VR_EYE_RIGHT: Right eye
 *
 * VR eye identifiers for stereo rendering.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VR_EYE_LEFT,
    LRG_VR_EYE_RIGHT
} LrgVREye;

LRG_AVAILABLE_IN_ALL
GType lrg_vr_eye_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VR_EYE (lrg_vr_eye_get_type ())

/**
 * LrgVRHand:
 * @LRG_VR_HAND_LEFT: Left hand controller
 * @LRG_VR_HAND_RIGHT: Right hand controller
 *
 * VR hand identifiers for motion controllers.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VR_HAND_LEFT,
    LRG_VR_HAND_RIGHT
} LrgVRHand;

LRG_AVAILABLE_IN_ALL
GType lrg_vr_hand_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VR_HAND (lrg_vr_hand_get_type ())

/**
 * LrgVRControllerButton:
 * @LRG_VR_CONTROLLER_BUTTON_SYSTEM: System/dashboard button
 * @LRG_VR_CONTROLLER_BUTTON_MENU: Menu button
 * @LRG_VR_CONTROLLER_BUTTON_GRIP: Grip button
 * @LRG_VR_CONTROLLER_BUTTON_TRIGGER: Trigger button
 * @LRG_VR_CONTROLLER_BUTTON_TOUCHPAD: Touchpad click
 * @LRG_VR_CONTROLLER_BUTTON_THUMBSTICK: Thumbstick click
 * @LRG_VR_CONTROLLER_BUTTON_A: A button (where present)
 * @LRG_VR_CONTROLLER_BUTTON_B: B button (where present)
 *
 * VR controller button flags.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VR_CONTROLLER_BUTTON_SYSTEM     = (1 << 0),
    LRG_VR_CONTROLLER_BUTTON_MENU       = (1 << 1),
    LRG_VR_CONTROLLER_BUTTON_GRIP       = (1 << 2),
    LRG_VR_CONTROLLER_BUTTON_TRIGGER    = (1 << 3),
    LRG_VR_CONTROLLER_BUTTON_TOUCHPAD   = (1 << 4),
    LRG_VR_CONTROLLER_BUTTON_THUMBSTICK = (1 << 5),
    LRG_VR_CONTROLLER_BUTTON_A          = (1 << 6),
    LRG_VR_CONTROLLER_BUTTON_B          = (1 << 7)
} LrgVRControllerButton;

LRG_AVAILABLE_IN_ALL
GType lrg_vr_controller_button_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VR_CONTROLLER_BUTTON (lrg_vr_controller_button_get_type ())

/**
 * LrgVRTurnMode:
 * @LRG_VR_TURN_MODE_SMOOTH: Smooth turning
 * @LRG_VR_TURN_MODE_SNAP: Snap turning (comfort)
 *
 * VR turning modes for comfort.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VR_TURN_MODE_SMOOTH,
    LRG_VR_TURN_MODE_SNAP
} LrgVRTurnMode;

LRG_AVAILABLE_IN_ALL
GType lrg_vr_turn_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VR_TURN_MODE (lrg_vr_turn_mode_get_type ())

/**
 * LrgVRLocomotionMode:
 * @LRG_VR_LOCOMOTION_SMOOTH: Smooth locomotion
 * @LRG_VR_LOCOMOTION_TELEPORT: Teleport locomotion (comfort)
 *
 * VR locomotion modes for comfort.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VR_LOCOMOTION_SMOOTH,
    LRG_VR_LOCOMOTION_TELEPORT
} LrgVRLocomotionMode;

LRG_AVAILABLE_IN_ALL
GType lrg_vr_locomotion_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VR_LOCOMOTION_MODE (lrg_vr_locomotion_mode_get_type ())

/**
 * LrgVRError:
 * @LRG_VR_ERROR_FAILED: Generic VR error
 * @LRG_VR_ERROR_NOT_AVAILABLE: VR runtime not available
 * @LRG_VR_ERROR_HMD_NOT_FOUND: HMD not detected
 * @LRG_VR_ERROR_COMPOSITOR: Compositor error
 * @LRG_VR_ERROR_TRACKING: Tracking error
 *
 * Error codes for the VR system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VR_ERROR_FAILED,
    LRG_VR_ERROR_NOT_AVAILABLE,
    LRG_VR_ERROR_HMD_NOT_FOUND,
    LRG_VR_ERROR_COMPOSITOR,
    LRG_VR_ERROR_TRACKING
} LrgVRError;

LRG_AVAILABLE_IN_ALL
GType lrg_vr_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VR_ERROR (lrg_vr_error_get_type ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_vr_error_quark (void);
#define LRG_VR_ERROR (lrg_vr_error_quark ())

/* ==========================================================================
 * Deckbuilder Module
 * ========================================================================== */

/**
 * LRG_DECKBUILDER_ERROR:
 *
 * Error domain for deckbuilder errors.
 */
#define LRG_DECKBUILDER_ERROR (lrg_deckbuilder_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_deckbuilder_error_quark (void);

/**
 * LrgDeckbuilderError:
 * @LRG_DECKBUILDER_ERROR_FAILED: Generic failure
 * @LRG_DECKBUILDER_ERROR_INSUFFICIENT_ENERGY: Not enough energy to play card
 * @LRG_DECKBUILDER_ERROR_CARD_UNPLAYABLE: Card cannot be played (condition not met)
 * @LRG_DECKBUILDER_ERROR_INVALID_TARGET: Invalid target for card effect
 * @LRG_DECKBUILDER_ERROR_COMBAT_NOT_ACTIVE: Combat not in active phase
 * @LRG_DECKBUILDER_ERROR_DECK_EMPTY: No cards in deck
 * @LRG_DECKBUILDER_ERROR_HAND_FULL: Hand is at maximum capacity
 * @LRG_DECKBUILDER_ERROR_INVALID_ZONE: Invalid card zone operation
 * @LRG_DECKBUILDER_ERROR_DECK_TOO_SMALL: Deck has fewer cards than minimum
 * @LRG_DECKBUILDER_ERROR_DECK_TOO_LARGE: Deck has more cards than maximum
 * @LRG_DECKBUILDER_ERROR_CARD_NOT_ALLOWED: Card type not allowed in deck
 * @LRG_DECKBUILDER_ERROR_CARD_BANNED: Card is banned from deck
 * @LRG_DECKBUILDER_ERROR_CARD_LIMIT_EXCEEDED: Too many copies of card
 * @LRG_DECKBUILDER_ERROR_EXECUTOR_NOT_FOUND: Effect executor not registered
 *
 * Error codes for the deckbuilder system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_DECKBUILDER_ERROR_FAILED,
    LRG_DECKBUILDER_ERROR_INSUFFICIENT_ENERGY,
    LRG_DECKBUILDER_ERROR_CARD_UNPLAYABLE,
    LRG_DECKBUILDER_ERROR_INVALID_TARGET,
    LRG_DECKBUILDER_ERROR_COMBAT_NOT_ACTIVE,
    LRG_DECKBUILDER_ERROR_DECK_EMPTY,
    LRG_DECKBUILDER_ERROR_HAND_FULL,
    LRG_DECKBUILDER_ERROR_INVALID_ZONE,
    LRG_DECKBUILDER_ERROR_DECK_TOO_SMALL,
    LRG_DECKBUILDER_ERROR_DECK_TOO_LARGE,
    LRG_DECKBUILDER_ERROR_CARD_NOT_ALLOWED,
    LRG_DECKBUILDER_ERROR_CARD_BANNED,
    LRG_DECKBUILDER_ERROR_CARD_LIMIT_EXCEEDED,
    LRG_DECKBUILDER_ERROR_EXECUTOR_NOT_FOUND
} LrgDeckbuilderError;

LRG_AVAILABLE_IN_ALL
GType lrg_deckbuilder_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DECKBUILDER_ERROR (lrg_deckbuilder_error_get_type ())

/**
 * LrgCardType:
 * @LRG_CARD_TYPE_ATTACK: Attack card - deals damage
 * @LRG_CARD_TYPE_SKILL: Skill card - provides utility/defense
 * @LRG_CARD_TYPE_POWER: Power card - persistent effect for combat
 * @LRG_CARD_TYPE_STATUS: Status card - negative card added to deck
 * @LRG_CARD_TYPE_CURSE: Curse card - permanent negative card
 *
 * The type of a card, determining its behavior and visual style.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_TYPE_ATTACK,
    LRG_CARD_TYPE_SKILL,
    LRG_CARD_TYPE_POWER,
    LRG_CARD_TYPE_STATUS,
    LRG_CARD_TYPE_CURSE
} LrgCardType;

LRG_AVAILABLE_IN_ALL
GType lrg_card_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_TYPE (lrg_card_type_get_type ())

/**
 * LrgCardRarity:
 * @LRG_CARD_RARITY_STARTER: Starter deck card
 * @LRG_CARD_RARITY_COMMON: Common card (higher drop rate)
 * @LRG_CARD_RARITY_UNCOMMON: Uncommon card (medium drop rate)
 * @LRG_CARD_RARITY_RARE: Rare card (low drop rate)
 * @LRG_CARD_RARITY_SPECIAL: Special card (not in normal pools)
 *
 * The rarity of a card, affecting drop rates and visual styling.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_RARITY_STARTER,
    LRG_CARD_RARITY_COMMON,
    LRG_CARD_RARITY_UNCOMMON,
    LRG_CARD_RARITY_RARE,
    LRG_CARD_RARITY_SPECIAL
} LrgCardRarity;

LRG_AVAILABLE_IN_ALL
GType lrg_card_rarity_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_RARITY (lrg_card_rarity_get_type ())

/**
 * LrgCardTargetType:
 * @LRG_CARD_TARGET_NONE: No target required
 * @LRG_CARD_TARGET_SELF: Targets self (player)
 * @LRG_CARD_TARGET_SINGLE_ENEMY: Requires selecting one enemy
 * @LRG_CARD_TARGET_ALL_ENEMIES: Affects all enemies
 * @LRG_CARD_TARGET_RANDOM_ENEMY: Affects a random enemy
 *
 * Target type for card effects.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_TARGET_NONE,
    LRG_CARD_TARGET_SELF,
    LRG_CARD_TARGET_SINGLE_ENEMY,
    LRG_CARD_TARGET_ALL_ENEMIES,
    LRG_CARD_TARGET_RANDOM_ENEMY
} LrgCardTargetType;

LRG_AVAILABLE_IN_ALL
GType lrg_card_target_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_TARGET_TYPE (lrg_card_target_type_get_type ())

/**
 * LrgPilePosition:
 * @LRG_PILE_POSITION_TOP: Top of the pile (drawn first)
 * @LRG_PILE_POSITION_BOTTOM: Bottom of the pile (drawn last)
 * @LRG_PILE_POSITION_RANDOM: Random position in pile
 *
 * Position for adding cards to a pile.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_PILE_POSITION_TOP,
    LRG_PILE_POSITION_BOTTOM,
    LRG_PILE_POSITION_RANDOM
} LrgPilePosition;

LRG_AVAILABLE_IN_ALL
GType lrg_pile_position_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PILE_POSITION (lrg_pile_position_get_type ())

/**
 * LrgCardZone:
 * @LRG_ZONE_DRAW: Draw pile
 * @LRG_ZONE_HAND: Player's hand
 * @LRG_ZONE_DISCARD: Discard pile
 * @LRG_ZONE_EXHAUST: Exhaust pile (removed for combat)
 * @LRG_ZONE_PLAYED: Currently being played
 * @LRG_ZONE_LIMBO: Temporarily removed (e.g., during scry)
 *
 * Zone where a card currently resides.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ZONE_DRAW,
    LRG_ZONE_HAND,
    LRG_ZONE_DISCARD,
    LRG_ZONE_EXHAUST,
    LRG_ZONE_PLAYED,
    LRG_ZONE_LIMBO
} LrgCardZone;

LRG_AVAILABLE_IN_ALL
GType lrg_card_zone_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_ZONE (lrg_card_zone_get_type ())

/**
 * LrgCardUpgradeTier:
 * @LRG_CARD_UPGRADE_TIER_BASE: Base unupgraded card
 * @LRG_CARD_UPGRADE_TIER_PLUS: First upgrade (+)
 * @LRG_CARD_UPGRADE_TIER_PLUS_PLUS: Second upgrade (++)
 * @LRG_CARD_UPGRADE_TIER_ULTIMATE: Maximum upgrade level
 *
 * Upgrade tier for card instances.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_UPGRADE_TIER_BASE,
    LRG_CARD_UPGRADE_TIER_PLUS,
    LRG_CARD_UPGRADE_TIER_PLUS_PLUS,
    LRG_CARD_UPGRADE_TIER_ULTIMATE
} LrgCardUpgradeTier;

LRG_AVAILABLE_IN_ALL
GType lrg_card_upgrade_tier_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_UPGRADE_TIER (lrg_card_upgrade_tier_get_type ())

/**
 * LrgCardKeyword:
 * @LRG_CARD_KEYWORD_NONE: No keywords
 * @LRG_CARD_KEYWORD_INNATE: Drawn first at start of combat
 * @LRG_CARD_KEYWORD_RETAIN: Not discarded at end of turn
 * @LRG_CARD_KEYWORD_EXHAUST: Removed from deck when played
 * @LRG_CARD_KEYWORD_ETHEREAL: Exhausted if in hand at end of turn
 * @LRG_CARD_KEYWORD_UNPLAYABLE: Cannot be played
 * @LRG_CARD_KEYWORD_X_COST: Cost equals remaining energy
 * @LRG_CARD_KEYWORD_FRAGILE: Destroyed when discarded
 * @LRG_CARD_KEYWORD_FLEETING: Exhausted at end of turn if not played
 *
 * Keywords that modify card behavior (flags).
 *
 * Since: 1.0
 */
typedef enum /*< flags >*/
{
    LRG_CARD_KEYWORD_NONE       = 0,
    LRG_CARD_KEYWORD_INNATE     = 1 << 0,
    LRG_CARD_KEYWORD_RETAIN     = 1 << 1,
    LRG_CARD_KEYWORD_EXHAUST    = 1 << 2,
    LRG_CARD_KEYWORD_ETHEREAL   = 1 << 3,
    LRG_CARD_KEYWORD_UNPLAYABLE = 1 << 4,
    LRG_CARD_KEYWORD_X_COST     = 1 << 5,
    LRG_CARD_KEYWORD_FRAGILE    = 1 << 6,
    LRG_CARD_KEYWORD_FLEETING   = 1 << 7
} LrgCardKeyword;

LRG_AVAILABLE_IN_ALL
GType lrg_card_keyword_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_KEYWORD (lrg_card_keyword_get_type ())

/**
 * LrgEffectFlags:
 * @LRG_EFFECT_FLAG_NONE: No special flags
 * @LRG_EFFECT_FLAG_UNBLOCKABLE: Cannot be blocked
 * @LRG_EFFECT_FLAG_PIERCING: Partially ignores block
 * @LRG_EFFECT_FLAG_TRUE_DAMAGE: Ignores all modifiers
 * @LRG_EFFECT_FLAG_HP_LOSS: Direct HP loss (ignores block)
 * @LRG_EFFECT_FLAG_LIFESTEAL: Heals attacker for damage dealt
 * @LRG_EFFECT_FLAG_AOE: Affects all targets
 * @LRG_EFFECT_FLAG_DELAYED: Effect is delayed to end of turn
 *
 * Flags modifying how effects are applied.
 *
 * Since: 1.0
 */
typedef enum /*< flags >*/
{
    LRG_EFFECT_FLAG_NONE        = 0,
    LRG_EFFECT_FLAG_UNBLOCKABLE = 1 << 0,
    LRG_EFFECT_FLAG_PIERCING    = 1 << 1,
    LRG_EFFECT_FLAG_TRUE_DAMAGE = 1 << 2,
    LRG_EFFECT_FLAG_HP_LOSS     = 1 << 3,
    LRG_EFFECT_FLAG_LIFESTEAL   = 1 << 4,
    LRG_EFFECT_FLAG_AOE         = 1 << 5,
    LRG_EFFECT_FLAG_DELAYED     = 1 << 6
} LrgEffectFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_effect_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EFFECT_FLAGS (lrg_effect_flags_get_type ())

/**
 * LrgCombatPhase:
 * @LRG_COMBAT_PHASE_SETUP: Combat initialization
 * @LRG_COMBAT_PHASE_PLAYER_START: Player turn start
 * @LRG_COMBAT_PHASE_PLAYER_PLAY: Player can play cards
 * @LRG_COMBAT_PHASE_PLAYER_END: Player turn end
 * @LRG_COMBAT_PHASE_ENEMY_TURN: Enemies executing intents
 * @LRG_COMBAT_PHASE_FINISHED: Combat has ended
 *
 * Current phase of combat.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_COMBAT_PHASE_SETUP,
    LRG_COMBAT_PHASE_PLAYER_START,
    LRG_COMBAT_PHASE_PLAYER_PLAY,
    LRG_COMBAT_PHASE_PLAYER_END,
    LRG_COMBAT_PHASE_ENEMY_TURN,
    LRG_COMBAT_PHASE_FINISHED
} LrgCombatPhase;

LRG_AVAILABLE_IN_ALL
GType lrg_combat_phase_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COMBAT_PHASE (lrg_combat_phase_get_type ())

/**
 * LrgIntentType:
 * @LRG_INTENT_UNKNOWN: Unknown intent
 * @LRG_INTENT_ATTACK: Will deal damage
 * @LRG_INTENT_DEFEND: Will gain block
 * @LRG_INTENT_BUFF: Will buff self
 * @LRG_INTENT_DEBUFF: Will debuff player
 * @LRG_INTENT_ATTACK_BUFF: Will attack and buff
 * @LRG_INTENT_ATTACK_DEBUFF: Will attack and debuff
 * @LRG_INTENT_STRONG_DEBUFF: Powerful debuff
 * @LRG_INTENT_ESCAPE: Will flee combat
 * @LRG_INTENT_SLEEP: Sleeping/inactive
 * @LRG_INTENT_STUN: Stunned/skipping turn
 *
 * Enemy intent types for visual display.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_INTENT_UNKNOWN,
    LRG_INTENT_ATTACK,
    LRG_INTENT_DEFEND,
    LRG_INTENT_BUFF,
    LRG_INTENT_DEBUFF,
    LRG_INTENT_ATTACK_BUFF,
    LRG_INTENT_ATTACK_DEBUFF,
    LRG_INTENT_STRONG_DEBUFF,
    LRG_INTENT_ESCAPE,
    LRG_INTENT_SLEEP,
    LRG_INTENT_STUN
} LrgIntentType;

LRG_AVAILABLE_IN_ALL
GType lrg_intent_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_INTENT_TYPE (lrg_intent_type_get_type ())

/**
 * LrgStatusType:
 * @LRG_STATUS_TYPE_BUFF: Positive effect
 * @LRG_STATUS_TYPE_DEBUFF: Negative effect
 * @LRG_STATUS_TYPE_NEUTRAL: Neutral effect
 *
 * Category of a status effect.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_STATUS_TYPE_BUFF,
    LRG_STATUS_TYPE_DEBUFF,
    LRG_STATUS_TYPE_NEUTRAL
} LrgStatusType;

LRG_AVAILABLE_IN_ALL
GType lrg_status_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_STATUS_TYPE (lrg_status_type_get_type ())

/**
 * LrgStatusDuration:
 * @LRG_STATUS_DURATION_COMBAT: Lasts entire combat
 * @LRG_STATUS_DURATION_TURN: Counts down each turn
 * @LRG_STATUS_DURATION_ATTACK: Counts down on attack
 * @LRG_STATUS_DURATION_PERMANENT: Never expires
 *
 * How a status effect's duration is tracked.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_STATUS_DURATION_COMBAT,
    LRG_STATUS_DURATION_TURN,
    LRG_STATUS_DURATION_ATTACK,
    LRG_STATUS_DURATION_PERMANENT
} LrgStatusDuration;

LRG_AVAILABLE_IN_ALL
GType lrg_status_duration_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_STATUS_DURATION (lrg_status_duration_get_type ())

/**
 * LrgRelicRarity:
 * @LRG_RELIC_RARITY_STARTER: Starting character relic
 * @LRG_RELIC_RARITY_COMMON: Common relic
 * @LRG_RELIC_RARITY_UNCOMMON: Uncommon relic
 * @LRG_RELIC_RARITY_RARE: Rare relic
 * @LRG_RELIC_RARITY_BOSS: Boss reward relic
 * @LRG_RELIC_RARITY_EVENT: Event-only relic
 * @LRG_RELIC_RARITY_SHOP: Shop-only relic
 * @LRG_RELIC_RARITY_SPECIAL: Special/unique relic
 *
 * Rarity tier for relics.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_RELIC_RARITY_STARTER,
    LRG_RELIC_RARITY_COMMON,
    LRG_RELIC_RARITY_UNCOMMON,
    LRG_RELIC_RARITY_RARE,
    LRG_RELIC_RARITY_BOSS,
    LRG_RELIC_RARITY_EVENT,
    LRG_RELIC_RARITY_SHOP,
    LRG_RELIC_RARITY_SPECIAL
} LrgRelicRarity;

LRG_AVAILABLE_IN_ALL
GType lrg_relic_rarity_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RELIC_RARITY (lrg_relic_rarity_get_type ())

/**
 * LrgMapNodeType:
 * @LRG_MAP_NODE_COMBAT: Normal combat encounter
 * @LRG_MAP_NODE_ELITE: Elite combat encounter
 * @LRG_MAP_NODE_BOSS: Boss combat encounter
 * @LRG_MAP_NODE_EVENT: Random event
 * @LRG_MAP_NODE_SHOP: Shop
 * @LRG_MAP_NODE_REST: Rest site (heal/upgrade)
 * @LRG_MAP_NODE_TREASURE: Treasure room
 * @LRG_MAP_NODE_MYSTERY: Unknown/mystery node
 *
 * Type of node on the run map.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_MAP_NODE_COMBAT,
    LRG_MAP_NODE_ELITE,
    LRG_MAP_NODE_BOSS,
    LRG_MAP_NODE_EVENT,
    LRG_MAP_NODE_SHOP,
    LRG_MAP_NODE_REST,
    LRG_MAP_NODE_TREASURE,
    LRG_MAP_NODE_MYSTERY
} LrgMapNodeType;

LRG_AVAILABLE_IN_ALL
GType lrg_map_node_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MAP_NODE_TYPE (lrg_map_node_type_get_type ())

/**
 * LrgCardEventType:
 * @LRG_CARD_EVENT_COMBAT_START: Combat has started
 * @LRG_CARD_EVENT_COMBAT_END: Combat has ended
 * @LRG_CARD_EVENT_TURN_START: Turn has started
 * @LRG_CARD_EVENT_TURN_END: Turn has ended
 * @LRG_CARD_EVENT_CARD_DRAWN: A card was drawn
 * @LRG_CARD_EVENT_CARD_PLAYED: A card was played
 * @LRG_CARD_EVENT_CARD_DISCARDED: A card was discarded
 * @LRG_CARD_EVENT_CARD_EXHAUSTED: A card was exhausted
 * @LRG_CARD_EVENT_DAMAGE_DEALT: Damage was dealt
 * @LRG_CARD_EVENT_DAMAGE_RECEIVED: Damage was received
 * @LRG_CARD_EVENT_BLOCK_GAINED: Block was gained
 * @LRG_CARD_EVENT_HEAL: Healing occurred
 * @LRG_CARD_EVENT_STATUS_APPLIED: Status effect applied
 * @LRG_CARD_EVENT_STATUS_REMOVED: Status effect removed
 * @LRG_CARD_EVENT_ENERGY_GAINED: Energy was gained
 * @LRG_CARD_EVENT_SHUFFLE: Deck was shuffled
 * @LRG_CARD_EVENT_ENEMY_DIED: An enemy died
 * @LRG_CARD_EVENT_RELIC_TRIGGERED: A relic triggered
 *
 * Types of events in the deckbuilder event bus.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_EVENT_COMBAT_START,
    LRG_CARD_EVENT_COMBAT_END,
    LRG_CARD_EVENT_TURN_START,
    LRG_CARD_EVENT_TURN_END,
    LRG_CARD_EVENT_CARD_DRAWN,
    LRG_CARD_EVENT_CARD_PLAYED,
    LRG_CARD_EVENT_CARD_DISCARDED,
    LRG_CARD_EVENT_CARD_EXHAUSTED,
    LRG_CARD_EVENT_DAMAGE_DEALT,
    LRG_CARD_EVENT_DAMAGE_RECEIVED,
    LRG_CARD_EVENT_BLOCK_GAINED,
    LRG_CARD_EVENT_HEAL,
    LRG_CARD_EVENT_STATUS_APPLIED,
    LRG_CARD_EVENT_STATUS_REMOVED,
    LRG_CARD_EVENT_ENERGY_GAINED,
    LRG_CARD_EVENT_SHUFFLE,
    LRG_CARD_EVENT_ENEMY_DIED,
    LRG_CARD_EVENT_RELIC_TRIGGERED
} LrgCardEventType;

LRG_AVAILABLE_IN_ALL
GType lrg_card_event_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_EVENT_TYPE (lrg_card_event_type_get_type ())

/**
 * LrgHandType:
 * @LRG_HAND_TYPE_NONE: No valid hand
 * @LRG_HAND_TYPE_HIGH_CARD: High card
 * @LRG_HAND_TYPE_PAIR: One pair
 * @LRG_HAND_TYPE_TWO_PAIR: Two pair
 * @LRG_HAND_TYPE_THREE_OF_A_KIND: Three of a kind
 * @LRG_HAND_TYPE_STRAIGHT: Straight
 * @LRG_HAND_TYPE_FLUSH: Flush
 * @LRG_HAND_TYPE_FULL_HOUSE: Full house
 * @LRG_HAND_TYPE_FOUR_OF_A_KIND: Four of a kind
 * @LRG_HAND_TYPE_STRAIGHT_FLUSH: Straight flush
 * @LRG_HAND_TYPE_ROYAL_FLUSH: Royal flush
 * @LRG_HAND_TYPE_FIVE_OF_A_KIND: Five of a kind (requires wild)
 * @LRG_HAND_TYPE_FLUSH_HOUSE: Flush full house (Balatro special)
 * @LRG_HAND_TYPE_FLUSH_FIVE: Flush five of a kind (Balatro special)
 *
 * Poker hand types for scoring deckbuilders (Balatro-style).
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_HAND_TYPE_NONE,
    LRG_HAND_TYPE_HIGH_CARD,
    LRG_HAND_TYPE_PAIR,
    LRG_HAND_TYPE_TWO_PAIR,
    LRG_HAND_TYPE_THREE_OF_A_KIND,
    LRG_HAND_TYPE_STRAIGHT,
    LRG_HAND_TYPE_FLUSH,
    LRG_HAND_TYPE_FULL_HOUSE,
    LRG_HAND_TYPE_FOUR_OF_A_KIND,
    LRG_HAND_TYPE_STRAIGHT_FLUSH,
    LRG_HAND_TYPE_ROYAL_FLUSH,
    LRG_HAND_TYPE_FIVE_OF_A_KIND,
    LRG_HAND_TYPE_FLUSH_HOUSE,
    LRG_HAND_TYPE_FLUSH_FIVE
} LrgHandType;

LRG_AVAILABLE_IN_ALL
GType lrg_hand_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_HAND_TYPE (lrg_hand_type_get_type ())

/**
 * LrgCardSuit:
 * @LRG_CARD_SUIT_NONE: No suit (for non-poker cards)
 * @LRG_CARD_SUIT_SPADES: Spades
 * @LRG_CARD_SUIT_HEARTS: Hearts
 * @LRG_CARD_SUIT_DIAMONDS: Diamonds
 * @LRG_CARD_SUIT_CLUBS: Clubs
 *
 * Playing card suits for scoring deckbuilders.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_SUIT_NONE,
    LRG_CARD_SUIT_SPADES,
    LRG_CARD_SUIT_HEARTS,
    LRG_CARD_SUIT_DIAMONDS,
    LRG_CARD_SUIT_CLUBS
} LrgCardSuit;

LRG_AVAILABLE_IN_ALL
GType lrg_card_suit_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_SUIT (lrg_card_suit_get_type ())

/**
 * LrgCardRank:
 * @LRG_CARD_RANK_NONE: No rank
 * @LRG_CARD_RANK_ACE: Ace (value 1 or 14)
 * @LRG_CARD_RANK_TWO: Two
 * @LRG_CARD_RANK_THREE: Three
 * @LRG_CARD_RANK_FOUR: Four
 * @LRG_CARD_RANK_FIVE: Five
 * @LRG_CARD_RANK_SIX: Six
 * @LRG_CARD_RANK_SEVEN: Seven
 * @LRG_CARD_RANK_EIGHT: Eight
 * @LRG_CARD_RANK_NINE: Nine
 * @LRG_CARD_RANK_TEN: Ten
 * @LRG_CARD_RANK_JACK: Jack
 * @LRG_CARD_RANK_QUEEN: Queen
 * @LRG_CARD_RANK_KING: King
 *
 * Playing card ranks for scoring deckbuilders.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_RANK_NONE,
    LRG_CARD_RANK_ACE,
    LRG_CARD_RANK_TWO,
    LRG_CARD_RANK_THREE,
    LRG_CARD_RANK_FOUR,
    LRG_CARD_RANK_FIVE,
    LRG_CARD_RANK_SIX,
    LRG_CARD_RANK_SEVEN,
    LRG_CARD_RANK_EIGHT,
    LRG_CARD_RANK_NINE,
    LRG_CARD_RANK_TEN,
    LRG_CARD_RANK_JACK,
    LRG_CARD_RANK_QUEEN,
    LRG_CARD_RANK_KING
} LrgCardRank;

LRG_AVAILABLE_IN_ALL
GType lrg_card_rank_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_RANK (lrg_card_rank_get_type ())

/**
 * LrgEnemyType:
 * @LRG_ENEMY_TYPE_NORMAL: Normal enemy
 * @LRG_ENEMY_TYPE_ELITE: Elite enemy (stronger, better rewards)
 * @LRG_ENEMY_TYPE_BOSS: Boss enemy (end of act)
 * @LRG_ENEMY_TYPE_MINION: Minion (summoned by other enemies)
 *
 * Classification of enemy types.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ENEMY_TYPE_NORMAL,
    LRG_ENEMY_TYPE_ELITE,
    LRG_ENEMY_TYPE_BOSS,
    LRG_ENEMY_TYPE_MINION
} LrgEnemyType;

LRG_AVAILABLE_IN_ALL
GType lrg_enemy_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ENEMY_TYPE (lrg_enemy_type_get_type ())

/**
 * LrgCombatResult:
 * @LRG_COMBAT_RESULT_IN_PROGRESS: Combat is ongoing
 * @LRG_COMBAT_RESULT_VICTORY: Player won
 * @LRG_COMBAT_RESULT_DEFEAT: Player lost
 * @LRG_COMBAT_RESULT_ESCAPE: Combat ended by escape
 *
 * Result of a combat encounter.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_COMBAT_RESULT_IN_PROGRESS,
    LRG_COMBAT_RESULT_VICTORY,
    LRG_COMBAT_RESULT_DEFEAT,
    LRG_COMBAT_RESULT_ESCAPE
} LrgCombatResult;

LRG_AVAILABLE_IN_ALL
GType lrg_combat_result_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COMBAT_RESULT (lrg_combat_result_get_type ())

/**
 * LrgRunState:
 * @LRG_RUN_STATE_NOT_STARTED: Run has not started yet
 * @LRG_RUN_STATE_MAP: Player is on the map selecting a node
 * @LRG_RUN_STATE_COMBAT: Player is in combat
 * @LRG_RUN_STATE_EVENT: Player is in an event
 * @LRG_RUN_STATE_SHOP: Player is in the shop
 * @LRG_RUN_STATE_REST: Player is at a rest site
 * @LRG_RUN_STATE_TREASURE: Player is at a treasure room
 * @LRG_RUN_STATE_CARD_REWARD: Player is selecting a card reward
 * @LRG_RUN_STATE_BOSS_RELIC: Player is selecting a boss relic
 * @LRG_RUN_STATE_VICTORY: Run completed successfully
 * @LRG_RUN_STATE_DEFEAT: Player was defeated
 *
 * Current state of a deckbuilder run.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_RUN_STATE_NOT_STARTED,
    LRG_RUN_STATE_MAP,
    LRG_RUN_STATE_COMBAT,
    LRG_RUN_STATE_EVENT,
    LRG_RUN_STATE_SHOP,
    LRG_RUN_STATE_REST,
    LRG_RUN_STATE_TREASURE,
    LRG_RUN_STATE_CARD_REWARD,
    LRG_RUN_STATE_BOSS_RELIC,
    LRG_RUN_STATE_VICTORY,
    LRG_RUN_STATE_DEFEAT
} LrgRunState;

LRG_AVAILABLE_IN_ALL
GType lrg_run_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RUN_STATE (lrg_run_state_get_type ())

/**
 * LrgCardEnhancement:
 * @LRG_CARD_ENHANCEMENT_NONE: No enhancement
 * @LRG_CARD_ENHANCEMENT_BONUS: +30 chips
 * @LRG_CARD_ENHANCEMENT_MULT: +4 mult
 * @LRG_CARD_ENHANCEMENT_WILD: Counts as all suits
 * @LRG_CARD_ENHANCEMENT_GLASS: x2 mult, may break
 * @LRG_CARD_ENHANCEMENT_STEEL: x1.5 mult when held
 * @LRG_CARD_ENHANCEMENT_STONE: +50 chips, no rank/suit
 * @LRG_CARD_ENHANCEMENT_GOLD: +3 money when held at end of round
 * @LRG_CARD_ENHANCEMENT_LUCKY: 1 in 5 chance for +20 mult or money
 *
 * Enhancement types for scoring cards (Balatro-style).
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_ENHANCEMENT_NONE,
    LRG_CARD_ENHANCEMENT_BONUS,
    LRG_CARD_ENHANCEMENT_MULT,
    LRG_CARD_ENHANCEMENT_WILD,
    LRG_CARD_ENHANCEMENT_GLASS,
    LRG_CARD_ENHANCEMENT_STEEL,
    LRG_CARD_ENHANCEMENT_STONE,
    LRG_CARD_ENHANCEMENT_GOLD,
    LRG_CARD_ENHANCEMENT_LUCKY
} LrgCardEnhancement;

LRG_AVAILABLE_IN_ALL
GType lrg_card_enhancement_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_ENHANCEMENT (lrg_card_enhancement_get_type ())

/**
 * LrgCardSeal:
 * @LRG_CARD_SEAL_NONE: No seal
 * @LRG_CARD_SEAL_GOLD: Creates money when played
 * @LRG_CARD_SEAL_RED: Retriggered
 * @LRG_CARD_SEAL_BLUE: Creates a Planet card when held at end of round
 * @LRG_CARD_SEAL_PURPLE: Creates a Tarot card when discarded
 *
 * Seal types for scoring cards (Balatro-style).
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_CARD_SEAL_NONE,
    LRG_CARD_SEAL_GOLD,
    LRG_CARD_SEAL_RED,
    LRG_CARD_SEAL_BLUE,
    LRG_CARD_SEAL_PURPLE
} LrgCardSeal;

LRG_AVAILABLE_IN_ALL
GType lrg_card_seal_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CARD_SEAL (lrg_card_seal_get_type ())

/**
 * LrgJokerRarity:
 * @LRG_JOKER_RARITY_COMMON: Common joker
 * @LRG_JOKER_RARITY_UNCOMMON: Uncommon joker
 * @LRG_JOKER_RARITY_RARE: Rare joker
 * @LRG_JOKER_RARITY_LEGENDARY: Legendary joker
 *
 * Rarity levels for jokers (Balatro-style).
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_JOKER_RARITY_COMMON,
    LRG_JOKER_RARITY_UNCOMMON,
    LRG_JOKER_RARITY_RARE,
    LRG_JOKER_RARITY_LEGENDARY
} LrgJokerRarity;

LRG_AVAILABLE_IN_ALL
GType lrg_joker_rarity_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_JOKER_RARITY (lrg_joker_rarity_get_type ())

/**
 * LrgJokerEdition:
 * @LRG_JOKER_EDITION_BASE: No special edition
 * @LRG_JOKER_EDITION_FOIL: +50 chips
 * @LRG_JOKER_EDITION_HOLOGRAPHIC: +10 mult
 * @LRG_JOKER_EDITION_POLYCHROME: x1.5 mult
 * @LRG_JOKER_EDITION_NEGATIVE: +1 joker slot
 *
 * Edition types for jokers (Balatro-style).
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_JOKER_EDITION_BASE,
    LRG_JOKER_EDITION_FOIL,
    LRG_JOKER_EDITION_HOLOGRAPHIC,
    LRG_JOKER_EDITION_POLYCHROME,
    LRG_JOKER_EDITION_NEGATIVE
} LrgJokerEdition;

LRG_AVAILABLE_IN_ALL
GType lrg_joker_edition_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_JOKER_EDITION (lrg_joker_edition_get_type ())

/**
 * LrgScoringPhase:
 * @LRG_SCORING_PHASE_SETUP: Setting up the round
 * @LRG_SCORING_PHASE_DRAW: Drawing cards
 * @LRG_SCORING_PHASE_SELECT: Selecting cards to play
 * @LRG_SCORING_PHASE_SCORE: Scoring the selected cards
 * @LRG_SCORING_PHASE_DISCARD: Discarding cards
 * @LRG_SCORING_PHASE_SHOP: Shopping between rounds
 * @LRG_SCORING_PHASE_FINISHED: Round/game finished
 *
 * Phases in a scoring deckbuilder round.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_SCORING_PHASE_SETUP,
    LRG_SCORING_PHASE_DRAW,
    LRG_SCORING_PHASE_SELECT,
    LRG_SCORING_PHASE_SCORE,
    LRG_SCORING_PHASE_DISCARD,
    LRG_SCORING_PHASE_SHOP,
    LRG_SCORING_PHASE_FINISHED
} LrgScoringPhase;

LRG_AVAILABLE_IN_ALL
GType lrg_scoring_phase_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCORING_PHASE (lrg_scoring_phase_get_type ())

/* --- Phase 7: Meta-Progression --- */

/**
 * LrgAscensionModifier:
 * @LRG_ASCENSION_MODIFIER_NONE: No modifiers
 * @LRG_ASCENSION_MODIFIER_ENEMY_HP: Enemies have increased HP
 * @LRG_ASCENSION_MODIFIER_ENEMY_DAMAGE: Enemies deal more damage
 * @LRG_ASCENSION_MODIFIER_LESS_GOLD: Start with less gold
 * @LRG_ASCENSION_MODIFIER_LESS_HEALING: Healing is less effective
 * @LRG_ASCENSION_MODIFIER_HARDER_ELITES: Elite enemies are stronger
 * @LRG_ASCENSION_MODIFIER_HARDER_BOSSES: Boss enemies are stronger
 * @LRG_ASCENSION_MODIFIER_CURSES: Start with curse cards
 * @LRG_ASCENSION_MODIFIER_LESS_POTIONS: Fewer potion slots
 * @LRG_ASCENSION_MODIFIER_DECK_RESTRICTIONS: Deck building restrictions
 * @LRG_ASCENSION_MODIFIER_TIME_LIMIT: Time limits on turns/decisions
 *
 * Flags for ascension challenge modifiers.
 *
 * Since: 1.0
 */
typedef enum /*< flags >*/
{
    LRG_ASCENSION_MODIFIER_NONE             = 0,
    LRG_ASCENSION_MODIFIER_ENEMY_HP         = 1 << 0,
    LRG_ASCENSION_MODIFIER_ENEMY_DAMAGE     = 1 << 1,
    LRG_ASCENSION_MODIFIER_LESS_GOLD        = 1 << 2,
    LRG_ASCENSION_MODIFIER_LESS_HEALING     = 1 << 3,
    LRG_ASCENSION_MODIFIER_HARDER_ELITES    = 1 << 4,
    LRG_ASCENSION_MODIFIER_HARDER_BOSSES    = 1 << 5,
    LRG_ASCENSION_MODIFIER_CURSES           = 1 << 6,
    LRG_ASCENSION_MODIFIER_LESS_POTIONS     = 1 << 7,
    LRG_ASCENSION_MODIFIER_DECK_RESTRICTIONS = 1 << 8,
    LRG_ASCENSION_MODIFIER_TIME_LIMIT       = 1 << 9
} LrgAscensionModifier;

LRG_AVAILABLE_IN_ALL
GType lrg_ascension_modifier_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ASCENSION_MODIFIER (lrg_ascension_modifier_get_type ())

/**
 * LrgUnlockType:
 * @LRG_UNLOCK_TYPE_CHARACTER: Unlock a playable character
 * @LRG_UNLOCK_TYPE_CARD: Unlock a card for card pools
 * @LRG_UNLOCK_TYPE_RELIC: Unlock a relic for relic pools
 * @LRG_UNLOCK_TYPE_POTION: Unlock a potion type
 * @LRG_UNLOCK_TYPE_JOKER: Unlock a joker for scoring mode
 * @LRG_UNLOCK_TYPE_ASCENSION: Unlock next ascension level
 * @LRG_UNLOCK_TYPE_COSMETIC: Unlock a cosmetic item
 * @LRG_UNLOCK_TYPE_CHALLENGE: Unlock a challenge mode
 *
 * Types of unlockable content.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_UNLOCK_TYPE_CHARACTER,
    LRG_UNLOCK_TYPE_CARD,
    LRG_UNLOCK_TYPE_RELIC,
    LRG_UNLOCK_TYPE_POTION,
    LRG_UNLOCK_TYPE_JOKER,
    LRG_UNLOCK_TYPE_ASCENSION,
    LRG_UNLOCK_TYPE_COSMETIC,
    LRG_UNLOCK_TYPE_CHALLENGE
} LrgUnlockType;

LRG_AVAILABLE_IN_ALL
GType lrg_unlock_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_UNLOCK_TYPE (lrg_unlock_type_get_type ())

/**
 * LrgUnlockStatus:
 * @LRG_UNLOCK_STATUS_LOCKED: Content is locked
 * @LRG_UNLOCK_STATUS_UNLOCKED: Content is unlocked
 * @LRG_UNLOCK_STATUS_NEW: Content is newly unlocked (not yet seen)
 *
 * Status of unlockable content.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_UNLOCK_STATUS_LOCKED,
    LRG_UNLOCK_STATUS_UNLOCKED,
    LRG_UNLOCK_STATUS_NEW
} LrgUnlockStatus;

LRG_AVAILABLE_IN_ALL
GType lrg_unlock_status_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_UNLOCK_STATUS (lrg_unlock_status_get_type ())

G_END_DECLS
