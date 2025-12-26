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

G_END_DECLS
