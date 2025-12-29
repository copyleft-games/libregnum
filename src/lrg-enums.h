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

G_END_DECLS
