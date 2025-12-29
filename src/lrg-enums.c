/* lrg-enums.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GType registration for Libregnum enumerations.
 */

#include "config.h"
#include "lrg-enums.h"

/* ==========================================================================
 * Error Quarks
 * ========================================================================== */

/**
 * lrg_engine_error_quark:
 *
 * Gets the error quark for engine errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_engine_error_quark (void)
{
    return g_quark_from_static_string ("lrg-engine-error-quark");
}

/**
 * lrg_data_loader_error_quark:
 *
 * Gets the error quark for data loader errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_data_loader_error_quark (void)
{
    return g_quark_from_static_string ("lrg-data-loader-error-quark");
}

/**
 * lrg_mod_error_quark:
 *
 * Gets the error quark for mod system errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_mod_error_quark (void)
{
    return g_quark_from_static_string ("lrg-mod-error-quark");
}

/**
 * lrg_save_error_quark:
 *
 * Gets the error quark for save system errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_save_error_quark (void)
{
    return g_quark_from_static_string ("lrg-save-error-quark");
}

/**
 * lrg_dialog_error_quark:
 *
 * Gets the error quark for dialog system errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_dialog_error_quark (void)
{
    return g_quark_from_static_string ("lrg-dialog-error-quark");
}

/**
 * lrg_asset_manager_error_quark:
 *
 * Gets the error quark for asset manager errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_asset_manager_error_quark (void)
{
    return g_quark_from_static_string ("lrg-asset-manager-error-quark");
}

/* ==========================================================================
 * Error GTypes
 * ========================================================================== */

GType
lrg_engine_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ENGINE_ERROR_FAILED, "LRG_ENGINE_ERROR_FAILED", "failed" },
            { LRG_ENGINE_ERROR_INIT, "LRG_ENGINE_ERROR_INIT", "init" },
            { LRG_ENGINE_ERROR_STATE, "LRG_ENGINE_ERROR_STATE", "state" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgEngineError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_data_loader_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DATA_LOADER_ERROR_FAILED, "LRG_DATA_LOADER_ERROR_FAILED", "failed" },
            { LRG_DATA_LOADER_ERROR_IO, "LRG_DATA_LOADER_ERROR_IO", "io" },
            { LRG_DATA_LOADER_ERROR_PARSE, "LRG_DATA_LOADER_ERROR_PARSE", "parse" },
            { LRG_DATA_LOADER_ERROR_TYPE, "LRG_DATA_LOADER_ERROR_TYPE", "type" },
            { LRG_DATA_LOADER_ERROR_PROPERTY, "LRG_DATA_LOADER_ERROR_PROPERTY", "property" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDataLoaderError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_mod_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_MOD_ERROR_FAILED, "LRG_MOD_ERROR_FAILED", "failed" },
            { LRG_MOD_ERROR_NOT_FOUND, "LRG_MOD_ERROR_NOT_FOUND", "not-found" },
            { LRG_MOD_ERROR_LOAD_FAILED, "LRG_MOD_ERROR_LOAD_FAILED", "load-failed" },
            { LRG_MOD_ERROR_INVALID_MANIFEST, "LRG_MOD_ERROR_INVALID_MANIFEST", "invalid-manifest" },
            { LRG_MOD_ERROR_MISSING_DEPENDENCY, "LRG_MOD_ERROR_MISSING_DEPENDENCY", "missing-dependency" },
            { LRG_MOD_ERROR_VERSION, "LRG_MOD_ERROR_VERSION", "version" },
            { LRG_MOD_ERROR_CIRCULAR, "LRG_MOD_ERROR_CIRCULAR", "circular" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgModError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_save_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SAVE_ERROR_FAILED, "LRG_SAVE_ERROR_FAILED", "failed" },
            { LRG_SAVE_ERROR_IO, "LRG_SAVE_ERROR_IO", "io" },
            { LRG_SAVE_ERROR_VERSION_MISMATCH, "LRG_SAVE_ERROR_VERSION_MISMATCH", "version-mismatch" },
            { LRG_SAVE_ERROR_CORRUPT, "LRG_SAVE_ERROR_CORRUPT", "corrupt" },
            { LRG_SAVE_ERROR_NOT_FOUND, "LRG_SAVE_ERROR_NOT_FOUND", "not-found" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgSaveError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_dialog_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DIALOG_ERROR_FAILED, "LRG_DIALOG_ERROR_FAILED", "failed" },
            { LRG_DIALOG_ERROR_INVALID_NODE, "LRG_DIALOG_ERROR_INVALID_NODE", "invalid-node" },
            { LRG_DIALOG_ERROR_NO_TREE, "LRG_DIALOG_ERROR_NO_TREE", "no-tree" },
            { LRG_DIALOG_ERROR_CONDITION, "LRG_DIALOG_ERROR_CONDITION", "condition" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDialogError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_asset_manager_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ASSET_MANAGER_ERROR_NOT_FOUND, "LRG_ASSET_MANAGER_ERROR_NOT_FOUND", "not-found" },
            { LRG_ASSET_MANAGER_ERROR_LOAD_FAILED, "LRG_ASSET_MANAGER_ERROR_LOAD_FAILED", "load-failed" },
            { LRG_ASSET_MANAGER_ERROR_INVALID_TYPE, "LRG_ASSET_MANAGER_ERROR_INVALID_TYPE", "invalid-type" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAssetManagerError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Tilemap GTypes
 * ========================================================================== */

GType
lrg_tile_property_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_TILE_PROPERTY_NONE, "LRG_TILE_PROPERTY_NONE", "none" },
            { LRG_TILE_PROPERTY_SOLID, "LRG_TILE_PROPERTY_SOLID", "solid" },
            { LRG_TILE_PROPERTY_ANIMATED, "LRG_TILE_PROPERTY_ANIMATED", "animated" },
            { LRG_TILE_PROPERTY_HAZARD, "LRG_TILE_PROPERTY_HAZARD", "hazard" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgTileProperty"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * State GTypes
 * ========================================================================== */

GType
lrg_engine_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ENGINE_STATE_UNINITIALIZED, "LRG_ENGINE_STATE_UNINITIALIZED", "uninitialized" },
            { LRG_ENGINE_STATE_INITIALIZING, "LRG_ENGINE_STATE_INITIALIZING", "initializing" },
            { LRG_ENGINE_STATE_RUNNING, "LRG_ENGINE_STATE_RUNNING", "running" },
            { LRG_ENGINE_STATE_PAUSED, "LRG_ENGINE_STATE_PAUSED", "paused" },
            { LRG_ENGINE_STATE_SHUTTING_DOWN, "LRG_ENGINE_STATE_SHUTTING_DOWN", "shutting-down" },
            { LRG_ENGINE_STATE_TERMINATED, "LRG_ENGINE_STATE_TERMINATED", "terminated" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgEngineState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Input GTypes
 * ========================================================================== */

GType
lrg_input_binding_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_INPUT_BINDING_KEYBOARD, "LRG_INPUT_BINDING_KEYBOARD", "keyboard" },
            { LRG_INPUT_BINDING_MOUSE_BUTTON, "LRG_INPUT_BINDING_MOUSE_BUTTON", "mouse-button" },
            { LRG_INPUT_BINDING_GAMEPAD_BUTTON, "LRG_INPUT_BINDING_GAMEPAD_BUTTON", "gamepad-button" },
            { LRG_INPUT_BINDING_GAMEPAD_AXIS, "LRG_INPUT_BINDING_GAMEPAD_AXIS", "gamepad-axis" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgInputBindingType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_input_modifiers_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_INPUT_MODIFIER_NONE, "LRG_INPUT_MODIFIER_NONE", "none" },
            { LRG_INPUT_MODIFIER_SHIFT, "LRG_INPUT_MODIFIER_SHIFT", "shift" },
            { LRG_INPUT_MODIFIER_CTRL, "LRG_INPUT_MODIFIER_CTRL", "ctrl" },
            { LRG_INPUT_MODIFIER_ALT, "LRG_INPUT_MODIFIER_ALT", "alt" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgInputModifiers"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Behavior Tree GTypes
 * ========================================================================== */

GType
lrg_bt_status_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BT_STATUS_INVALID, "LRG_BT_STATUS_INVALID", "invalid" },
            { LRG_BT_STATUS_SUCCESS, "LRG_BT_STATUS_SUCCESS", "success" },
            { LRG_BT_STATUS_FAILURE, "LRG_BT_STATUS_FAILURE", "failure" },
            { LRG_BT_STATUS_RUNNING, "LRG_BT_STATUS_RUNNING", "running" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBTStatus"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Quest GTypes
 * ========================================================================== */

GType
lrg_quest_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_QUEST_STATE_AVAILABLE, "LRG_QUEST_STATE_AVAILABLE", "available" },
            { LRG_QUEST_STATE_ACTIVE, "LRG_QUEST_STATE_ACTIVE", "active" },
            { LRG_QUEST_STATE_COMPLETE, "LRG_QUEST_STATE_COMPLETE", "complete" },
            { LRG_QUEST_STATE_FAILED, "LRG_QUEST_STATE_FAILED", "failed" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgQuestState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_quest_objective_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_QUEST_OBJECTIVE_KILL, "LRG_QUEST_OBJECTIVE_KILL", "kill" },
            { LRG_QUEST_OBJECTIVE_COLLECT, "LRG_QUEST_OBJECTIVE_COLLECT", "collect" },
            { LRG_QUEST_OBJECTIVE_INTERACT, "LRG_QUEST_OBJECTIVE_INTERACT", "interact" },
            { LRG_QUEST_OBJECTIVE_REACH, "LRG_QUEST_OBJECTIVE_REACH", "reach" },
            { LRG_QUEST_OBJECTIVE_ESCORT, "LRG_QUEST_OBJECTIVE_ESCORT", "escort" },
            { LRG_QUEST_OBJECTIVE_CUSTOM, "LRG_QUEST_OBJECTIVE_CUSTOM", "custom" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgQuestObjectiveType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Item GTypes
 * ========================================================================== */

GType
lrg_item_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ITEM_TYPE_GENERIC, "LRG_ITEM_TYPE_GENERIC", "generic" },
            { LRG_ITEM_TYPE_WEAPON, "LRG_ITEM_TYPE_WEAPON", "weapon" },
            { LRG_ITEM_TYPE_ARMOR, "LRG_ITEM_TYPE_ARMOR", "armor" },
            { LRG_ITEM_TYPE_CONSUMABLE, "LRG_ITEM_TYPE_CONSUMABLE", "consumable" },
            { LRG_ITEM_TYPE_QUEST, "LRG_ITEM_TYPE_QUEST", "quest" },
            { LRG_ITEM_TYPE_MATERIAL, "LRG_ITEM_TYPE_MATERIAL", "material" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgItemType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Accessibility GTypes
 * ========================================================================== */

GType
lrg_colorblind_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_COLORBLIND_NONE, "LRG_COLORBLIND_NONE", "none" },
            { LRG_COLORBLIND_DEUTERANOPIA, "LRG_COLORBLIND_DEUTERANOPIA", "deuteranopia" },
            { LRG_COLORBLIND_PROTANOPIA, "LRG_COLORBLIND_PROTANOPIA", "protanopia" },
            { LRG_COLORBLIND_TRITANOPIA, "LRG_COLORBLIND_TRITANOPIA", "tritanopia" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgColorblindMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * UI GTypes
 * ========================================================================== */

GType
lrg_text_alignment_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TEXT_ALIGN_LEFT, "LRG_TEXT_ALIGN_LEFT", "left" },
            { LRG_TEXT_ALIGN_CENTER, "LRG_TEXT_ALIGN_CENTER", "center" },
            { LRG_TEXT_ALIGN_RIGHT, "LRG_TEXT_ALIGN_RIGHT", "right" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTextAlignment"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_ui_event_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_UI_EVENT_NONE, "LRG_UI_EVENT_NONE", "none" },
            { LRG_UI_EVENT_MOUSE_MOVE, "LRG_UI_EVENT_MOUSE_MOVE", "mouse-move" },
            { LRG_UI_EVENT_MOUSE_BUTTON_DOWN, "LRG_UI_EVENT_MOUSE_BUTTON_DOWN", "mouse-button-down" },
            { LRG_UI_EVENT_MOUSE_BUTTON_UP, "LRG_UI_EVENT_MOUSE_BUTTON_UP", "mouse-button-up" },
            { LRG_UI_EVENT_KEY_DOWN, "LRG_UI_EVENT_KEY_DOWN", "key-down" },
            { LRG_UI_EVENT_KEY_UP, "LRG_UI_EVENT_KEY_UP", "key-up" },
            { LRG_UI_EVENT_SCROLL, "LRG_UI_EVENT_SCROLL", "scroll" },
            { LRG_UI_EVENT_FOCUS_IN, "LRG_UI_EVENT_FOCUS_IN", "focus-in" },
            { LRG_UI_EVENT_FOCUS_OUT, "LRG_UI_EVENT_FOCUS_OUT", "focus-out" },
            { LRG_UI_EVENT_TEXT_INPUT, "LRG_UI_EVENT_TEXT_INPUT", "text-input" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgUIEventType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_orientation_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ORIENTATION_HORIZONTAL, "LRG_ORIENTATION_HORIZONTAL", "horizontal" },
            { LRG_ORIENTATION_VERTICAL, "LRG_ORIENTATION_VERTICAL", "vertical" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgOrientation"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_image_scale_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_IMAGE_SCALE_MODE_FIT, "LRG_IMAGE_SCALE_MODE_FIT", "fit" },
            { LRG_IMAGE_SCALE_MODE_FILL, "LRG_IMAGE_SCALE_MODE_FILL", "fill" },
            { LRG_IMAGE_SCALE_MODE_STRETCH, "LRG_IMAGE_SCALE_MODE_STRETCH", "stretch" },
            { LRG_IMAGE_SCALE_MODE_TILE, "LRG_IMAGE_SCALE_MODE_TILE", "tile" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgImageScaleMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * I18N Quarks and GTypes
 * ========================================================================== */

/**
 * lrg_i18n_error_quark:
 *
 * Gets the error quark for localization errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_i18n_error_quark (void)
{
    return g_quark_from_static_string ("lrg-i18n-error-quark");
}

GType
lrg_i18n_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_I18N_ERROR_FAILED, "LRG_I18N_ERROR_FAILED", "failed" },
            { LRG_I18N_ERROR_NOT_FOUND, "LRG_I18N_ERROR_NOT_FOUND", "not-found" },
            { LRG_I18N_ERROR_LOCALE_NOT_FOUND, "LRG_I18N_ERROR_LOCALE_NOT_FOUND", "locale-not-found" },
            { LRG_I18N_ERROR_PARSE, "LRG_I18N_ERROR_PARSE", "parse" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgI18nError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_plural_form_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PLURAL_ZERO, "LRG_PLURAL_ZERO", "zero" },
            { LRG_PLURAL_ONE, "LRG_PLURAL_ONE", "one" },
            { LRG_PLURAL_TWO, "LRG_PLURAL_TWO", "two" },
            { LRG_PLURAL_FEW, "LRG_PLURAL_FEW", "few" },
            { LRG_PLURAL_MANY, "LRG_PLURAL_MANY", "many" },
            { LRG_PLURAL_OTHER, "LRG_PLURAL_OTHER", "other" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgPluralForm"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Pathfinding Quarks and GTypes
 * ========================================================================== */

/**
 * lrg_pathfinding_error_quark:
 *
 * Gets the error quark for pathfinding errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_pathfinding_error_quark (void)
{
    return g_quark_from_static_string ("lrg-pathfinding-error-quark");
}

GType
lrg_pathfinding_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PATHFINDING_ERROR_FAILED, "LRG_PATHFINDING_ERROR_FAILED", "failed" },
            { LRG_PATHFINDING_ERROR_NO_PATH, "LRG_PATHFINDING_ERROR_NO_PATH", "no-path" },
            { LRG_PATHFINDING_ERROR_OUT_OF_BOUNDS, "LRG_PATHFINDING_ERROR_OUT_OF_BOUNDS", "out-of-bounds" },
            { LRG_PATHFINDING_ERROR_BLOCKED, "LRG_PATHFINDING_ERROR_BLOCKED", "blocked" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgPathfindingError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_nav_cell_flags_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_NAV_CELL_WALKABLE, "LRG_NAV_CELL_WALKABLE", "walkable" },
            { LRG_NAV_CELL_BLOCKED, "LRG_NAV_CELL_BLOCKED", "blocked" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgNavCellFlags"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_path_smoothing_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PATH_SMOOTHING_NONE, "LRG_PATH_SMOOTHING_NONE", "none" },
            { LRG_PATH_SMOOTHING_SIMPLE, "LRG_PATH_SMOOTHING_SIMPLE", "simple" },
            { LRG_PATH_SMOOTHING_BEZIER, "LRG_PATH_SMOOTHING_BEZIER", "bezier" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgPathSmoothingMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * AI / Behavior Tree GTypes
 * ========================================================================== */

GType
lrg_bt_parallel_policy_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BT_PARALLEL_REQUIRE_ONE, "LRG_BT_PARALLEL_REQUIRE_ONE", "require-one" },
            { LRG_BT_PARALLEL_REQUIRE_ALL, "LRG_BT_PARALLEL_REQUIRE_ALL", "require-all" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBTParallelPolicy"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_blackboard_value_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BLACKBOARD_VALUE_INT, "LRG_BLACKBOARD_VALUE_INT", "int" },
            { LRG_BLACKBOARD_VALUE_FLOAT, "LRG_BLACKBOARD_VALUE_FLOAT", "float" },
            { LRG_BLACKBOARD_VALUE_BOOL, "LRG_BLACKBOARD_VALUE_BOOL", "bool" },
            { LRG_BLACKBOARD_VALUE_STRING, "LRG_BLACKBOARD_VALUE_STRING", "string" },
            { LRG_BLACKBOARD_VALUE_OBJECT, "LRG_BLACKBOARD_VALUE_OBJECT", "object" },
            { LRG_BLACKBOARD_VALUE_VECTOR2, "LRG_BLACKBOARD_VALUE_VECTOR2", "vector2" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBlackboardValueType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Physics GTypes
 * ========================================================================== */

GType
lrg_rigid_body_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_RIGID_BODY_DYNAMIC, "LRG_RIGID_BODY_DYNAMIC", "dynamic" },
            { LRG_RIGID_BODY_KINEMATIC, "LRG_RIGID_BODY_KINEMATIC", "kinematic" },
            { LRG_RIGID_BODY_STATIC, "LRG_RIGID_BODY_STATIC", "static" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgRigidBodyType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_force_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_FORCE_MODE_FORCE, "LRG_FORCE_MODE_FORCE", "force" },
            { LRG_FORCE_MODE_IMPULSE, "LRG_FORCE_MODE_IMPULSE", "impulse" },
            { LRG_FORCE_MODE_ACCELERATION, "LRG_FORCE_MODE_ACCELERATION", "acceleration" },
            { LRG_FORCE_MODE_VELOCITY_CHANGE, "LRG_FORCE_MODE_VELOCITY_CHANGE", "velocity-change" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgForceMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_collision_shape_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_COLLISION_SHAPE_BOX, "LRG_COLLISION_SHAPE_BOX", "box" },
            { LRG_COLLISION_SHAPE_CIRCLE, "LRG_COLLISION_SHAPE_CIRCLE", "circle" },
            { LRG_COLLISION_SHAPE_CAPSULE, "LRG_COLLISION_SHAPE_CAPSULE", "capsule" },
            { LRG_COLLISION_SHAPE_POLYGON, "LRG_COLLISION_SHAPE_POLYGON", "polygon" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgCollisionShape"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Debug Quarks and GTypes
 * ========================================================================== */

/**
 * lrg_debug_error_quark:
 *
 * Gets the error quark for debug system errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_debug_error_quark (void)
{
    return g_quark_from_static_string ("lrg-debug-error-quark");
}

GType
lrg_debug_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DEBUG_ERROR_FAILED, "LRG_DEBUG_ERROR_FAILED", "failed" },
            { LRG_DEBUG_ERROR_COMMAND_NOT_FOUND, "LRG_DEBUG_ERROR_COMMAND_NOT_FOUND", "command-not-found" },
            { LRG_DEBUG_ERROR_INVALID_ARGS, "LRG_DEBUG_ERROR_INVALID_ARGS", "invalid-args" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDebugError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_debug_overlay_flags_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_DEBUG_OVERLAY_NONE, "LRG_DEBUG_OVERLAY_NONE", "none" },
            { LRG_DEBUG_OVERLAY_FPS, "LRG_DEBUG_OVERLAY_FPS", "fps" },
            { LRG_DEBUG_OVERLAY_FRAME_TIME, "LRG_DEBUG_OVERLAY_FRAME_TIME", "frame-time" },
            { LRG_DEBUG_OVERLAY_MEMORY, "LRG_DEBUG_OVERLAY_MEMORY", "memory" },
            { LRG_DEBUG_OVERLAY_ENTITIES, "LRG_DEBUG_OVERLAY_ENTITIES", "entities" },
            { LRG_DEBUG_OVERLAY_PHYSICS, "LRG_DEBUG_OVERLAY_PHYSICS", "physics" },
            { LRG_DEBUG_OVERLAY_COLLIDERS, "LRG_DEBUG_OVERLAY_COLLIDERS", "colliders" },
            { LRG_DEBUG_OVERLAY_PROFILER, "LRG_DEBUG_OVERLAY_PROFILER", "profiler" },
            { LRG_DEBUG_OVERLAY_ALL, "LRG_DEBUG_OVERLAY_ALL", "all" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgDebugOverlayFlags"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_profiler_section_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PROFILER_SECTION_UPDATE, "LRG_PROFILER_SECTION_UPDATE", "update" },
            { LRG_PROFILER_SECTION_PHYSICS, "LRG_PROFILER_SECTION_PHYSICS", "physics" },
            { LRG_PROFILER_SECTION_RENDER, "LRG_PROFILER_SECTION_RENDER", "render" },
            { LRG_PROFILER_SECTION_AI, "LRG_PROFILER_SECTION_AI", "ai" },
            { LRG_PROFILER_SECTION_AUDIO, "LRG_PROFILER_SECTION_AUDIO", "audio" },
            { LRG_PROFILER_SECTION_CUSTOM, "LRG_PROFILER_SECTION_CUSTOM", "custom" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgProfilerSectionType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Mod System GTypes
 * ========================================================================== */

GType
lrg_mod_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_MOD_STATE_UNLOADED, "LRG_MOD_STATE_UNLOADED", "unloaded" },
            { LRG_MOD_STATE_DISCOVERED, "LRG_MOD_STATE_DISCOVERED", "discovered" },
            { LRG_MOD_STATE_LOADING, "LRG_MOD_STATE_LOADING", "loading" },
            { LRG_MOD_STATE_LOADED, "LRG_MOD_STATE_LOADED", "loaded" },
            { LRG_MOD_STATE_FAILED, "LRG_MOD_STATE_FAILED", "failed" },
            { LRG_MOD_STATE_DISABLED, "LRG_MOD_STATE_DISABLED", "disabled" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgModState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_mod_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_MOD_TYPE_DATA, "LRG_MOD_TYPE_DATA", "data" },
            { LRG_MOD_TYPE_SCRIPT, "LRG_MOD_TYPE_SCRIPT", "script" },
            { LRG_MOD_TYPE_NATIVE, "LRG_MOD_TYPE_NATIVE", "native" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgModType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_mod_priority_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_MOD_PRIORITY_LOWEST, "LRG_MOD_PRIORITY_LOWEST", "lowest" },
            { LRG_MOD_PRIORITY_LOW, "LRG_MOD_PRIORITY_LOW", "low" },
            { LRG_MOD_PRIORITY_NORMAL, "LRG_MOD_PRIORITY_NORMAL", "normal" },
            { LRG_MOD_PRIORITY_HIGH, "LRG_MOD_PRIORITY_HIGH", "high" },
            { LRG_MOD_PRIORITY_HIGHEST, "LRG_MOD_PRIORITY_HIGHEST", "highest" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgModPriority"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Equipment System
 * ========================================================================== */

GType
lrg_equipment_slot_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_EQUIPMENT_SLOT_HEAD, "LRG_EQUIPMENT_SLOT_HEAD", "head" },
            { LRG_EQUIPMENT_SLOT_CHEST, "LRG_EQUIPMENT_SLOT_CHEST", "chest" },
            { LRG_EQUIPMENT_SLOT_LEGS, "LRG_EQUIPMENT_SLOT_LEGS", "legs" },
            { LRG_EQUIPMENT_SLOT_FEET, "LRG_EQUIPMENT_SLOT_FEET", "feet" },
            { LRG_EQUIPMENT_SLOT_HANDS, "LRG_EQUIPMENT_SLOT_HANDS", "hands" },
            { LRG_EQUIPMENT_SLOT_WEAPON, "LRG_EQUIPMENT_SLOT_WEAPON", "weapon" },
            { LRG_EQUIPMENT_SLOT_OFFHAND, "LRG_EQUIPMENT_SLOT_OFFHAND", "offhand" },
            { LRG_EQUIPMENT_SLOT_ACCESSORY, "LRG_EQUIPMENT_SLOT_ACCESSORY", "accessory" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgEquipmentSlot"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Networking Quarks and GTypes
 * ========================================================================== */

/**
 * lrg_net_error_quark:
 *
 * Gets the error quark for networking errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_net_error_quark (void)
{
    return g_quark_from_static_string ("lrg-net-error-quark");
}

GType
lrg_net_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_NET_ERROR_FAILED, "LRG_NET_ERROR_FAILED", "failed" },
            { LRG_NET_ERROR_CONNECTION_FAILED, "LRG_NET_ERROR_CONNECTION_FAILED", "connection-failed" },
            { LRG_NET_ERROR_CONNECTION_CLOSED, "LRG_NET_ERROR_CONNECTION_CLOSED", "connection-closed" },
            { LRG_NET_ERROR_MESSAGE_INVALID, "LRG_NET_ERROR_MESSAGE_INVALID", "message-invalid" },
            { LRG_NET_ERROR_TIMEOUT, "LRG_NET_ERROR_TIMEOUT", "timeout" },
            { LRG_NET_ERROR_ALREADY_CONNECTED, "LRG_NET_ERROR_ALREADY_CONNECTED", "already-connected" },
            { LRG_NET_ERROR_NOT_CONNECTED, "LRG_NET_ERROR_NOT_CONNECTED", "not-connected" },
            { LRG_NET_ERROR_SEND_FAILED, "LRG_NET_ERROR_SEND_FAILED", "send-failed" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgNetError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_net_peer_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_NET_PEER_STATE_DISCONNECTED, "LRG_NET_PEER_STATE_DISCONNECTED", "disconnected" },
            { LRG_NET_PEER_STATE_CONNECTING, "LRG_NET_PEER_STATE_CONNECTING", "connecting" },
            { LRG_NET_PEER_STATE_CONNECTED, "LRG_NET_PEER_STATE_CONNECTED", "connected" },
            { LRG_NET_PEER_STATE_DISCONNECTING, "LRG_NET_PEER_STATE_DISCONNECTING", "disconnecting" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgNetPeerState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_net_message_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_NET_MESSAGE_TYPE_HANDSHAKE, "LRG_NET_MESSAGE_TYPE_HANDSHAKE", "handshake" },
            { LRG_NET_MESSAGE_TYPE_DATA, "LRG_NET_MESSAGE_TYPE_DATA", "data" },
            { LRG_NET_MESSAGE_TYPE_PING, "LRG_NET_MESSAGE_TYPE_PING", "ping" },
            { LRG_NET_MESSAGE_TYPE_PONG, "LRG_NET_MESSAGE_TYPE_PONG", "pong" },
            { LRG_NET_MESSAGE_TYPE_DISCONNECT, "LRG_NET_MESSAGE_TYPE_DISCONNECT", "disconnect" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgNetMessageType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Graphics System GTypes
 * ========================================================================== */

GType
lrg_render_layer_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_RENDER_LAYER_BACKGROUND, "LRG_RENDER_LAYER_BACKGROUND", "background" },
            { LRG_RENDER_LAYER_WORLD, "LRG_RENDER_LAYER_WORLD", "world" },
            { LRG_RENDER_LAYER_EFFECTS, "LRG_RENDER_LAYER_EFFECTS", "effects" },
            { LRG_RENDER_LAYER_UI, "LRG_RENDER_LAYER_UI", "ui" },
            { LRG_RENDER_LAYER_DEBUG, "LRG_RENDER_LAYER_DEBUG", "debug" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgRenderLayer"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_projection_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PROJECTION_PERSPECTIVE, "LRG_PROJECTION_PERSPECTIVE", "perspective" },
            { LRG_PROJECTION_ORTHOGRAPHIC, "LRG_PROJECTION_ORTHOGRAPHIC", "orthographic" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgProjectionType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * 3D World GTypes
 * ========================================================================== */

GType
lrg_spawn_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SPAWN_TYPE_PLAYER, "LRG_SPAWN_TYPE_PLAYER", "player" },
            { LRG_SPAWN_TYPE_ENEMY, "LRG_SPAWN_TYPE_ENEMY", "enemy" },
            { LRG_SPAWN_TYPE_NPC, "LRG_SPAWN_TYPE_NPC", "npc" },
            { LRG_SPAWN_TYPE_ITEM, "LRG_SPAWN_TYPE_ITEM", "item" },
            { LRG_SPAWN_TYPE_GENERIC, "LRG_SPAWN_TYPE_GENERIC", "generic" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgSpawnType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_trigger_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TRIGGER_TYPE_ENTER, "LRG_TRIGGER_TYPE_ENTER", "enter" },
            { LRG_TRIGGER_TYPE_EXIT, "LRG_TRIGGER_TYPE_EXIT", "exit" },
            { LRG_TRIGGER_TYPE_INTERACT, "LRG_TRIGGER_TYPE_INTERACT", "interact" },
            { LRG_TRIGGER_TYPE_PROXIMITY, "LRG_TRIGGER_TYPE_PROXIMITY", "proximity" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTriggerType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_octree_node_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_OCTREE_NODE_EMPTY, "LRG_OCTREE_NODE_EMPTY", "empty" },
            { LRG_OCTREE_NODE_LEAF, "LRG_OCTREE_NODE_LEAF", "leaf" },
            { LRG_OCTREE_NODE_BRANCH, "LRG_OCTREE_NODE_BRANCH", "branch" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgOctreeNodeType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Scene System Quarks and GTypes
 * ========================================================================== */

/**
 * lrg_scene_error_quark:
 *
 * Gets the error quark for scene errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_scene_error_quark (void)
{
    return g_quark_from_static_string ("lrg-scene-error-quark");
}

GType
lrg_scene_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SCENE_ERROR_FAILED, "LRG_SCENE_ERROR_FAILED", "failed" },
            { LRG_SCENE_ERROR_IO, "LRG_SCENE_ERROR_IO", "io" },
            { LRG_SCENE_ERROR_PARSE, "LRG_SCENE_ERROR_PARSE", "parse" },
            { LRG_SCENE_ERROR_INVALID_FORMAT, "LRG_SCENE_ERROR_INVALID_FORMAT", "invalid-format" },
            { LRG_SCENE_ERROR_UNKNOWN_PRIMITIVE, "LRG_SCENE_ERROR_UNKNOWN_PRIMITIVE", "unknown-primitive" },
            { LRG_SCENE_ERROR_MISSING_FIELD, "LRG_SCENE_ERROR_MISSING_FIELD", "missing-field" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgSceneError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_primitive_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PRIMITIVE_PLANE, "LRG_PRIMITIVE_PLANE", "plane" },
            { LRG_PRIMITIVE_CUBE, "LRG_PRIMITIVE_CUBE", "cube" },
            { LRG_PRIMITIVE_CIRCLE, "LRG_PRIMITIVE_CIRCLE", "circle" },
            { LRG_PRIMITIVE_UV_SPHERE, "LRG_PRIMITIVE_UV_SPHERE", "uv-sphere" },
            { LRG_PRIMITIVE_ICO_SPHERE, "LRG_PRIMITIVE_ICO_SPHERE", "ico-sphere" },
            { LRG_PRIMITIVE_CYLINDER, "LRG_PRIMITIVE_CYLINDER", "cylinder" },
            { LRG_PRIMITIVE_CONE, "LRG_PRIMITIVE_CONE", "cone" },
            { LRG_PRIMITIVE_TORUS, "LRG_PRIMITIVE_TORUS", "torus" },
            { LRG_PRIMITIVE_GRID, "LRG_PRIMITIVE_GRID", "grid" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgPrimitiveType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_circle_fill_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CIRCLE_FILL_NOTHING, "LRG_CIRCLE_FILL_NOTHING", "nothing" },
            { LRG_CIRCLE_FILL_NGON, "LRG_CIRCLE_FILL_NGON", "ngon" },
            { LRG_CIRCLE_FILL_TRIFAN, "LRG_CIRCLE_FILL_TRIFAN", "trifan" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgCircleFillType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Scripting System Quarks and GTypes
 * ========================================================================== */

/**
 * lrg_scripting_error_quark:
 *
 * Gets the error quark for scripting errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_scripting_error_quark (void)
{
    return g_quark_from_static_string ("lrg-scripting-error-quark");
}

GType
lrg_scripting_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SCRIPTING_ERROR_FAILED, "LRG_SCRIPTING_ERROR_FAILED", "failed" },
            { LRG_SCRIPTING_ERROR_LOAD, "LRG_SCRIPTING_ERROR_LOAD", "load" },
            { LRG_SCRIPTING_ERROR_SYNTAX, "LRG_SCRIPTING_ERROR_SYNTAX", "syntax" },
            { LRG_SCRIPTING_ERROR_RUNTIME, "LRG_SCRIPTING_ERROR_RUNTIME", "runtime" },
            { LRG_SCRIPTING_ERROR_TYPE, "LRG_SCRIPTING_ERROR_TYPE", "type" },
            { LRG_SCRIPTING_ERROR_NOT_FOUND, "LRG_SCRIPTING_ERROR_NOT_FOUND", "not-found" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgScriptingError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_script_access_flags_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_SCRIPT_ACCESS_NONE, "LRG_SCRIPT_ACCESS_NONE", "none" },
            { LRG_SCRIPT_ACCESS_READ, "LRG_SCRIPT_ACCESS_READ", "read" },
            { LRG_SCRIPT_ACCESS_WRITE, "LRG_SCRIPT_ACCESS_WRITE", "write" },
            { LRG_SCRIPT_ACCESS_READWRITE, "LRG_SCRIPT_ACCESS_READWRITE", "readwrite" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgScriptAccessFlags"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}
