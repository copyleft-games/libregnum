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
lrg_colorblind_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_COLORBLIND_NONE, "LRG_COLORBLIND_NONE", "none" },
            { LRG_COLORBLIND_DEUTERANOPIA, "LRG_COLORBLIND_DEUTERANOPIA", "deuteranopia" },
            { LRG_COLORBLIND_PROTANOPIA, "LRG_COLORBLIND_PROTANOPIA", "protanopia" },
            { LRG_COLORBLIND_TRITANOPIA, "LRG_COLORBLIND_TRITANOPIA", "tritanopia" },
            { LRG_COLORBLIND_ACHROMATOPSIA, "LRG_COLORBLIND_ACHROMATOPSIA", "achromatopsia" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgColorblindType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_colorblind_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_COLORBLIND_MODE_SIMULATE, "LRG_COLORBLIND_MODE_SIMULATE", "simulate" },
            { LRG_COLORBLIND_MODE_CORRECT, "LRG_COLORBLIND_MODE_CORRECT", "correct" },
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
 * DLC System
 * ========================================================================== */

GQuark
lrg_dlc_error_quark (void)
{
    return g_quark_from_static_string ("lrg-dlc-error-quark");
}

GType
lrg_dlc_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DLC_ERROR_FAILED, "LRG_DLC_ERROR_FAILED", "failed" },
            { LRG_DLC_ERROR_NOT_OWNED, "LRG_DLC_ERROR_NOT_OWNED", "not-owned" },
            { LRG_DLC_ERROR_VERIFICATION_FAILED, "LRG_DLC_ERROR_VERIFICATION_FAILED", "verification-failed" },
            { LRG_DLC_ERROR_INVALID_LICENSE, "LRG_DLC_ERROR_INVALID_LICENSE", "invalid-license" },
            { LRG_DLC_ERROR_STEAM_UNAVAILABLE, "LRG_DLC_ERROR_STEAM_UNAVAILABLE", "steam-unavailable" },
            { LRG_DLC_ERROR_CONTENT_GATED, "LRG_DLC_ERROR_CONTENT_GATED", "content-gated" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDlcError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_dlc_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DLC_TYPE_EXPANSION, "LRG_DLC_TYPE_EXPANSION", "expansion" },
            { LRG_DLC_TYPE_COSMETIC, "LRG_DLC_TYPE_COSMETIC", "cosmetic" },
            { LRG_DLC_TYPE_QUEST, "LRG_DLC_TYPE_QUEST", "quest" },
            { LRG_DLC_TYPE_ITEM, "LRG_DLC_TYPE_ITEM", "item" },
            { LRG_DLC_TYPE_CHARACTER, "LRG_DLC_TYPE_CHARACTER", "character" },
            { LRG_DLC_TYPE_MAP, "LRG_DLC_TYPE_MAP", "map" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDlcType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_dlc_ownership_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DLC_OWNERSHIP_UNKNOWN, "LRG_DLC_OWNERSHIP_UNKNOWN", "unknown" },
            { LRG_DLC_OWNERSHIP_NOT_OWNED, "LRG_DLC_OWNERSHIP_NOT_OWNED", "not-owned" },
            { LRG_DLC_OWNERSHIP_OWNED, "LRG_DLC_OWNERSHIP_OWNED", "owned" },
            { LRG_DLC_OWNERSHIP_TRIAL, "LRG_DLC_OWNERSHIP_TRIAL", "trial" },
            { LRG_DLC_OWNERSHIP_ERROR, "LRG_DLC_OWNERSHIP_ERROR", "error" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDlcOwnershipState"), values);
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

/* ==========================================================================
 * Economy System Quarks and GTypes (Phase 2)
 * ========================================================================== */

/**
 * lrg_economy_error_quark:
 *
 * Gets the error quark for economy errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_economy_error_quark (void)
{
    return g_quark_from_static_string ("lrg-economy-error-quark");
}

GType
lrg_economy_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ECONOMY_ERROR_FAILED, "LRG_ECONOMY_ERROR_FAILED", "failed" },
            { LRG_ECONOMY_ERROR_INSUFFICIENT, "LRG_ECONOMY_ERROR_INSUFFICIENT", "insufficient" },
            { LRG_ECONOMY_ERROR_INVALID_RESOURCE, "LRG_ECONOMY_ERROR_INVALID_RESOURCE", "invalid-resource" },
            { LRG_ECONOMY_ERROR_INVALID_RECIPE, "LRG_ECONOMY_ERROR_INVALID_RECIPE", "invalid-recipe" },
            { LRG_ECONOMY_ERROR_PRODUCTION_FAILED, "LRG_ECONOMY_ERROR_PRODUCTION_FAILED", "production-failed" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgEconomyError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_resource_category_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_RESOURCE_CATEGORY_CURRENCY, "LRG_RESOURCE_CATEGORY_CURRENCY", "currency" },
            { LRG_RESOURCE_CATEGORY_MATERIAL, "LRG_RESOURCE_CATEGORY_MATERIAL", "material" },
            { LRG_RESOURCE_CATEGORY_FOOD, "LRG_RESOURCE_CATEGORY_FOOD", "food" },
            { LRG_RESOURCE_CATEGORY_ENERGY, "LRG_RESOURCE_CATEGORY_ENERGY", "energy" },
            { LRG_RESOURCE_CATEGORY_POPULATION, "LRG_RESOURCE_CATEGORY_POPULATION", "population" },
            { LRG_RESOURCE_CATEGORY_RESEARCH, "LRG_RESOURCE_CATEGORY_RESEARCH", "research" },
            { LRG_RESOURCE_CATEGORY_CUSTOM, "LRG_RESOURCE_CATEGORY_CUSTOM", "custom" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgResourceCategory"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Building System Quarks and GTypes (Phase 2)
 * ========================================================================== */

/**
 * lrg_building_error_quark:
 *
 * Gets the error quark for building errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_building_error_quark (void)
{
    return g_quark_from_static_string ("lrg-building-error-quark");
}

GType
lrg_building_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BUILDING_ERROR_FAILED, "LRG_BUILDING_ERROR_FAILED", "failed" },
            { LRG_BUILDING_ERROR_INVALID_POSITION, "LRG_BUILDING_ERROR_INVALID_POSITION", "invalid-position" },
            { LRG_BUILDING_ERROR_AREA_BLOCKED, "LRG_BUILDING_ERROR_AREA_BLOCKED", "area-blocked" },
            { LRG_BUILDING_ERROR_INVALID_TERRAIN, "LRG_BUILDING_ERROR_INVALID_TERRAIN", "invalid-terrain" },
            { LRG_BUILDING_ERROR_INSUFFICIENT_RESOURCES, "LRG_BUILDING_ERROR_INSUFFICIENT_RESOURCES", "insufficient-resources" },
            { LRG_BUILDING_ERROR_MAX_LEVEL, "LRG_BUILDING_ERROR_MAX_LEVEL", "max-level" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBuildingError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_building_category_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BUILDING_CATEGORY_PRODUCTION, "LRG_BUILDING_CATEGORY_PRODUCTION", "production" },
            { LRG_BUILDING_CATEGORY_RESIDENTIAL, "LRG_BUILDING_CATEGORY_RESIDENTIAL", "residential" },
            { LRG_BUILDING_CATEGORY_COMMERCIAL, "LRG_BUILDING_CATEGORY_COMMERCIAL", "commercial" },
            { LRG_BUILDING_CATEGORY_INFRASTRUCTURE, "LRG_BUILDING_CATEGORY_INFRASTRUCTURE", "infrastructure" },
            { LRG_BUILDING_CATEGORY_DECORATION, "LRG_BUILDING_CATEGORY_DECORATION", "decoration" },
            { LRG_BUILDING_CATEGORY_SPECIAL, "LRG_BUILDING_CATEGORY_SPECIAL", "special" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBuildingCategory"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_rotation_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ROTATION_0, "LRG_ROTATION_0", "0" },
            { LRG_ROTATION_90, "LRG_ROTATION_90", "90" },
            { LRG_ROTATION_180, "LRG_ROTATION_180", "180" },
            { LRG_ROTATION_270, "LRG_ROTATION_270", "270" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgRotation"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_terrain_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_TERRAIN_NONE, "LRG_TERRAIN_NONE", "none" },
            { LRG_TERRAIN_GRASS, "LRG_TERRAIN_GRASS", "grass" },
            { LRG_TERRAIN_DIRT, "LRG_TERRAIN_DIRT", "dirt" },
            { LRG_TERRAIN_SAND, "LRG_TERRAIN_SAND", "sand" },
            { LRG_TERRAIN_WATER, "LRG_TERRAIN_WATER", "water" },
            { LRG_TERRAIN_ROCK, "LRG_TERRAIN_ROCK", "rock" },
            { LRG_TERRAIN_ROAD, "LRG_TERRAIN_ROAD", "road" },
            { LRG_TERRAIN_SNOW, "LRG_TERRAIN_SNOW", "snow" },
            { LRG_TERRAIN_MUD, "LRG_TERRAIN_MUD", "mud" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgTerrainType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Vehicle System Quarks and GTypes (Phase 2)
 * ========================================================================== */

/**
 * lrg_vehicle_error_quark:
 *
 * Gets the error quark for vehicle errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_vehicle_error_quark (void)
{
    return g_quark_from_static_string ("lrg-vehicle-error-quark");
}

GType
lrg_vehicle_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VEHICLE_ERROR_FAILED, "LRG_VEHICLE_ERROR_FAILED", "failed" },
            { LRG_VEHICLE_ERROR_NO_WHEELS, "LRG_VEHICLE_ERROR_NO_WHEELS", "no-wheels" },
            { LRG_VEHICLE_ERROR_INVALID_ROAD, "LRG_VEHICLE_ERROR_INVALID_ROAD", "invalid-road" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVehicleError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_vehicle_camera_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VEHICLE_CAMERA_FOLLOW, "LRG_VEHICLE_CAMERA_FOLLOW", "follow" },
            { LRG_VEHICLE_CAMERA_HOOD, "LRG_VEHICLE_CAMERA_HOOD", "hood" },
            { LRG_VEHICLE_CAMERA_COCKPIT, "LRG_VEHICLE_CAMERA_COCKPIT", "cockpit" },
            { LRG_VEHICLE_CAMERA_FREE, "LRG_VEHICLE_CAMERA_FREE", "free" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVehicleCameraMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_traffic_behavior_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TRAFFIC_BEHAVIOR_CALM, "LRG_TRAFFIC_BEHAVIOR_CALM", "calm" },
            { LRG_TRAFFIC_BEHAVIOR_NORMAL, "LRG_TRAFFIC_BEHAVIOR_NORMAL", "normal" },
            { LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE, "LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE", "aggressive" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTrafficBehavior"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_road_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ROAD_TYPE_HIGHWAY, "LRG_ROAD_TYPE_HIGHWAY", "highway" },
            { LRG_ROAD_TYPE_MAIN, "LRG_ROAD_TYPE_MAIN", "main" },
            { LRG_ROAD_TYPE_STREET, "LRG_ROAD_TYPE_STREET", "street" },
            { LRG_ROAD_TYPE_ALLEY, "LRG_ROAD_TYPE_ALLEY", "alley" },
            { LRG_ROAD_TYPE_DIRT, "LRG_ROAD_TYPE_DIRT", "dirt" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgRoadType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Idle Game System GTypes (Phase 2)
 * ========================================================================== */

GType
lrg_big_number_format_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BIG_NUMBER_FORMAT_SHORT, "LRG_BIG_NUMBER_FORMAT_SHORT", "short" },
            { LRG_BIG_NUMBER_FORMAT_SCIENTIFIC, "LRG_BIG_NUMBER_FORMAT_SCIENTIFIC", "scientific" },
            { LRG_BIG_NUMBER_FORMAT_FULL, "LRG_BIG_NUMBER_FORMAT_FULL", "full" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBigNumberFormat"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_automation_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_AUTOMATION_MODE_CLICK, "LRG_AUTOMATION_MODE_CLICK", "click" },
            { LRG_AUTOMATION_MODE_BUY_ONE, "LRG_AUTOMATION_MODE_BUY_ONE", "buy-one" },
            { LRG_AUTOMATION_MODE_BUY_MAX, "LRG_AUTOMATION_MODE_BUY_MAX", "buy-max" },
            { LRG_AUTOMATION_MODE_UPGRADE, "LRG_AUTOMATION_MODE_UPGRADE", "upgrade" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAutomationMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_milestone_condition_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_MILESTONE_CONDITION_REACH, "LRG_MILESTONE_CONDITION_REACH", "reach" },
            { LRG_MILESTONE_CONDITION_ACCUMULATE, "LRG_MILESTONE_CONDITION_ACCUMULATE", "accumulate" },
            { LRG_MILESTONE_CONDITION_COUNT, "LRG_MILESTONE_CONDITION_COUNT", "count" },
            { LRG_MILESTONE_CONDITION_PRESTIGE, "LRG_MILESTONE_CONDITION_PRESTIGE", "prestige" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgMilestoneCondition"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Particle System Quarks and GTypes (Phase 3)
 * ========================================================================== */

/**
 * lrg_particle_error_quark:
 *
 * Gets the error quark for particle system errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_particle_error_quark (void)
{
    return g_quark_from_static_string ("lrg-particle-error-quark");
}

GType
lrg_particle_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PARTICLE_ERROR_FAILED, "LRG_PARTICLE_ERROR_FAILED", "failed" },
            { LRG_PARTICLE_ERROR_POOL_EXHAUSTED, "LRG_PARTICLE_ERROR_POOL_EXHAUSTED", "pool-exhausted" },
            { LRG_PARTICLE_ERROR_GPU_NOT_AVAILABLE, "LRG_PARTICLE_ERROR_GPU_NOT_AVAILABLE", "gpu-not-available" },
            { LRG_PARTICLE_ERROR_SHADER_COMPILE, "LRG_PARTICLE_ERROR_SHADER_COMPILE", "shader-compile" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgParticleError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_emission_shape_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_EMISSION_SHAPE_POINT, "LRG_EMISSION_SHAPE_POINT", "point" },
            { LRG_EMISSION_SHAPE_CIRCLE, "LRG_EMISSION_SHAPE_CIRCLE", "circle" },
            { LRG_EMISSION_SHAPE_RECTANGLE, "LRG_EMISSION_SHAPE_RECTANGLE", "rectangle" },
            { LRG_EMISSION_SHAPE_CONE, "LRG_EMISSION_SHAPE_CONE", "cone" },
            { LRG_EMISSION_SHAPE_MESH, "LRG_EMISSION_SHAPE_MESH", "mesh" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgEmissionShape"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_particle_render_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PARTICLE_RENDER_BILLBOARD, "LRG_PARTICLE_RENDER_BILLBOARD", "billboard" },
            { LRG_PARTICLE_RENDER_STRETCHED_BILLBOARD, "LRG_PARTICLE_RENDER_STRETCHED_BILLBOARD", "stretched-billboard" },
            { LRG_PARTICLE_RENDER_TRAIL, "LRG_PARTICLE_RENDER_TRAIL", "trail" },
            { LRG_PARTICLE_RENDER_MESH, "LRG_PARTICLE_RENDER_MESH", "mesh" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgParticleRenderMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_particle_backend_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PARTICLE_BACKEND_CPU, "LRG_PARTICLE_BACKEND_CPU", "cpu" },
            { LRG_PARTICLE_BACKEND_GPU, "LRG_PARTICLE_BACKEND_GPU", "gpu" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgParticleBackendType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_particle_blend_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PARTICLE_BLEND_ADDITIVE, "LRG_PARTICLE_BLEND_ADDITIVE", "additive" },
            { LRG_PARTICLE_BLEND_ALPHA, "LRG_PARTICLE_BLEND_ALPHA", "alpha" },
            { LRG_PARTICLE_BLEND_MULTIPLY, "LRG_PARTICLE_BLEND_MULTIPLY", "multiply" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgParticleBlendMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_pool_grow_policy_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_POOL_GROW_NONE, "LRG_POOL_GROW_NONE", "none" },
            { LRG_POOL_GROW_DOUBLE, "LRG_POOL_GROW_DOUBLE", "double" },
            { LRG_POOL_GROW_LINEAR, "LRG_POOL_GROW_LINEAR", "linear" },
            { LRG_POOL_GROW_RECYCLE, "LRG_POOL_GROW_RECYCLE", "recycle" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgPoolGrowPolicy"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Post-Processing System Quarks and GTypes (Phase 3)
 * ========================================================================== */

/**
 * lrg_postprocess_error_quark:
 *
 * Gets the error quark for post-processing errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_postprocess_error_quark (void)
{
    return g_quark_from_static_string ("lrg-postprocess-error-quark");
}

GType
lrg_postprocess_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_POSTPROCESS_ERROR_FAILED, "LRG_POSTPROCESS_ERROR_FAILED", "failed" },
            { LRG_POSTPROCESS_ERROR_SHADER_COMPILE, "LRG_POSTPROCESS_ERROR_SHADER_COMPILE", "shader-compile" },
            { LRG_POSTPROCESS_ERROR_TEXTURE_LOAD, "LRG_POSTPROCESS_ERROR_TEXTURE_LOAD", "texture-load" },
            { LRG_POSTPROCESS_ERROR_FRAMEBUFFER, "LRG_POSTPROCESS_ERROR_FRAMEBUFFER", "framebuffer" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgPostProcessError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_fxaa_quality_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_FXAA_QUALITY_LOW, "LRG_FXAA_QUALITY_LOW", "low" },
            { LRG_FXAA_QUALITY_MEDIUM, "LRG_FXAA_QUALITY_MEDIUM", "medium" },
            { LRG_FXAA_QUALITY_HIGH, "LRG_FXAA_QUALITY_HIGH", "high" },
            { LRG_FXAA_QUALITY_ULTRA, "LRG_FXAA_QUALITY_ULTRA", "ultra" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgFxaaQuality"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_bokeh_shape_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BOKEH_CIRCLE, "LRG_BOKEH_CIRCLE", "circle" },
            { LRG_BOKEH_HEXAGON, "LRG_BOKEH_HEXAGON", "hexagon" },
            { LRG_BOKEH_OCTAGON, "LRG_BOKEH_OCTAGON", "octagon" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBokehShape"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Animation System Quarks and GTypes (Phase 3)
 * ========================================================================== */

/**
 * lrg_animation_error_quark:
 *
 * Gets the error quark for animation errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_animation_error_quark (void)
{
    return g_quark_from_static_string ("lrg-animation-error-quark");
}

GType
lrg_animation_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ANIMATION_ERROR_FAILED, "LRG_ANIMATION_ERROR_FAILED", "failed" },
            { LRG_ANIMATION_ERROR_CLIP_NOT_FOUND, "LRG_ANIMATION_ERROR_CLIP_NOT_FOUND", "clip-not-found" },
            { LRG_ANIMATION_ERROR_STATE_NOT_FOUND, "LRG_ANIMATION_ERROR_STATE_NOT_FOUND", "state-not-found" },
            { LRG_ANIMATION_ERROR_INVALID_TRANSITION, "LRG_ANIMATION_ERROR_INVALID_TRANSITION", "invalid-transition" },
            { LRG_ANIMATION_ERROR_BONE_NOT_FOUND, "LRG_ANIMATION_ERROR_BONE_NOT_FOUND", "bone-not-found" },
            { LRG_ANIMATION_ERROR_IK_FAILED, "LRG_ANIMATION_ERROR_IK_FAILED", "ik-failed" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAnimationError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_animation_loop_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ANIMATION_LOOP_NONE, "LRG_ANIMATION_LOOP_NONE", "none" },
            { LRG_ANIMATION_LOOP_REPEAT, "LRG_ANIMATION_LOOP_REPEAT", "repeat" },
            { LRG_ANIMATION_LOOP_PINGPONG, "LRG_ANIMATION_LOOP_PINGPONG", "pingpong" },
            { LRG_ANIMATION_LOOP_CLAMP_FOREVER, "LRG_ANIMATION_LOOP_CLAMP_FOREVER", "clamp-forever" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAnimationLoopMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_animator_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ANIMATOR_STOPPED, "LRG_ANIMATOR_STOPPED", "stopped" },
            { LRG_ANIMATOR_PLAYING, "LRG_ANIMATOR_PLAYING", "playing" },
            { LRG_ANIMATOR_PAUSED, "LRG_ANIMATOR_PAUSED", "paused" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAnimatorState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_blend_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_BLEND_TYPE_1D, "LRG_BLEND_TYPE_1D", "1d" },
            { LRG_BLEND_TYPE_2D_SIMPLE, "LRG_BLEND_TYPE_2D_SIMPLE", "2d-simple" },
            { LRG_BLEND_TYPE_2D_FREEFORM, "LRG_BLEND_TYPE_2D_FREEFORM", "2d-freeform" },
            { LRG_BLEND_TYPE_DIRECT, "LRG_BLEND_TYPE_DIRECT", "direct" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgBlendType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_animation_parameter_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ANIM_PARAM_FLOAT, "LRG_ANIM_PARAM_FLOAT", "float" },
            { LRG_ANIM_PARAM_INT, "LRG_ANIM_PARAM_INT", "int" },
            { LRG_ANIM_PARAM_BOOL, "LRG_ANIM_PARAM_BOOL", "bool" },
            { LRG_ANIM_PARAM_TRIGGER, "LRG_ANIM_PARAM_TRIGGER", "trigger" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAnimationParameterType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_layer_blend_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_LAYER_BLEND_OVERRIDE, "LRG_LAYER_BLEND_OVERRIDE", "override" },
            { LRG_LAYER_BLEND_ADDITIVE, "LRG_LAYER_BLEND_ADDITIVE", "additive" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgLayerBlendMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_transition_interruption_source_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TRANSITION_INTERRUPT_NONE, "LRG_TRANSITION_INTERRUPT_NONE", "none" },
            { LRG_TRANSITION_INTERRUPT_CURRENT, "LRG_TRANSITION_INTERRUPT_CURRENT", "current" },
            { LRG_TRANSITION_INTERRUPT_NEXT, "LRG_TRANSITION_INTERRUPT_NEXT", "next" },
            { LRG_TRANSITION_INTERRUPT_BOTH, "LRG_TRANSITION_INTERRUPT_BOTH", "both" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTransitionInterruptionSource"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_condition_comparison_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CONDITION_EQUALS, "LRG_CONDITION_EQUALS", "equals" },
            { LRG_CONDITION_NOT_EQUALS, "LRG_CONDITION_NOT_EQUALS", "not-equals" },
            { LRG_CONDITION_GREATER, "LRG_CONDITION_GREATER", "greater" },
            { LRG_CONDITION_LESS, "LRG_CONDITION_LESS", "less" },
            { LRG_CONDITION_GREATER_EQUAL, "LRG_CONDITION_GREATER_EQUAL", "greater-equal" },
            { LRG_CONDITION_LESS_EQUAL, "LRG_CONDITION_LESS_EQUAL", "less-equal" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgConditionComparison"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_ik_solver_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_IK_TYPE_FABRIK, "LRG_IK_TYPE_FABRIK", "fabrik" },
            { LRG_IK_TYPE_CCD, "LRG_IK_TYPE_CCD", "ccd" },
            { LRG_IK_TYPE_TWO_BONE, "LRG_IK_TYPE_TWO_BONE", "two-bone" },
            { LRG_IK_TYPE_LOOK_AT, "LRG_IK_TYPE_LOOK_AT", "look-at" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgIKSolverType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Rich Text System Quarks and GTypes (Phase 3)
 * ========================================================================== */

/**
 * lrg_text_error_quark:
 *
 * Gets the error quark for rich text errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_text_error_quark (void)
{
    return g_quark_from_static_string ("lrg-text-error-quark");
}

GType
lrg_text_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TEXT_ERROR_FAILED, "LRG_TEXT_ERROR_FAILED", "failed" },
            { LRG_TEXT_ERROR_FONT_LOAD, "LRG_TEXT_ERROR_FONT_LOAD", "font-load" },
            { LRG_TEXT_ERROR_INVALID_MARKUP, "LRG_TEXT_ERROR_INVALID_MARKUP", "invalid-markup" },
            { LRG_TEXT_ERROR_SHADER_COMPILE, "LRG_TEXT_ERROR_SHADER_COMPILE", "shader-compile" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTextError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_text_style_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_TEXT_STYLE_NONE, "LRG_TEXT_STYLE_NONE", "none" },
            { LRG_TEXT_STYLE_BOLD, "LRG_TEXT_STYLE_BOLD", "bold" },
            { LRG_TEXT_STYLE_ITALIC, "LRG_TEXT_STYLE_ITALIC", "italic" },
            { LRG_TEXT_STYLE_UNDERLINE, "LRG_TEXT_STYLE_UNDERLINE", "underline" },
            { LRG_TEXT_STYLE_STRIKETHROUGH, "LRG_TEXT_STYLE_STRIKETHROUGH", "strikethrough" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgTextStyle"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_text_effect_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TEXT_EFFECT_NONE, "LRG_TEXT_EFFECT_NONE", "none" },
            { LRG_TEXT_EFFECT_SHAKE, "LRG_TEXT_EFFECT_SHAKE", "shake" },
            { LRG_TEXT_EFFECT_WAVE, "LRG_TEXT_EFFECT_WAVE", "wave" },
            { LRG_TEXT_EFFECT_RAINBOW, "LRG_TEXT_EFFECT_RAINBOW", "rainbow" },
            { LRG_TEXT_EFFECT_TYPEWRITER, "LRG_TEXT_EFFECT_TYPEWRITER", "typewriter" },
            { LRG_TEXT_EFFECT_FADE_IN, "LRG_TEXT_EFFECT_FADE_IN", "fade-in" },
            { LRG_TEXT_EFFECT_PULSE, "LRG_TEXT_EFFECT_PULSE", "pulse" },
            { LRG_TEXT_EFFECT_CUSTOM, "LRG_TEXT_EFFECT_CUSTOM", "custom" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTextEffectType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_text_direction_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TEXT_DIRECTION_LTR, "LRG_TEXT_DIRECTION_LTR", "ltr" },
            { LRG_TEXT_DIRECTION_RTL, "LRG_TEXT_DIRECTION_RTL", "rtl" },
            { LRG_TEXT_DIRECTION_AUTO, "LRG_TEXT_DIRECTION_AUTO", "auto" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTextDirection"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Video Playback Quarks and GTypes (Phase 3)
 * ========================================================================== */

/**
 * lrg_video_error_quark:
 *
 * Gets the error quark for video playback errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_video_error_quark (void)
{
    return g_quark_from_static_string ("lrg-video-error-quark");
}

GType
lrg_video_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VIDEO_ERROR_FAILED, "LRG_VIDEO_ERROR_FAILED", "failed" },
            { LRG_VIDEO_ERROR_NOT_FOUND, "LRG_VIDEO_ERROR_NOT_FOUND", "not-found" },
            { LRG_VIDEO_ERROR_FORMAT, "LRG_VIDEO_ERROR_FORMAT", "format" },
            { LRG_VIDEO_ERROR_CODEC, "LRG_VIDEO_ERROR_CODEC", "codec" },
            { LRG_VIDEO_ERROR_DECODE, "LRG_VIDEO_ERROR_DECODE", "decode" },
            { LRG_VIDEO_ERROR_SEEK, "LRG_VIDEO_ERROR_SEEK", "seek" },
            { LRG_VIDEO_ERROR_AUDIO, "LRG_VIDEO_ERROR_AUDIO", "audio" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVideoError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_video_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VIDEO_STATE_STOPPED, "LRG_VIDEO_STATE_STOPPED", "stopped" },
            { LRG_VIDEO_STATE_LOADING, "LRG_VIDEO_STATE_LOADING", "loading" },
            { LRG_VIDEO_STATE_PLAYING, "LRG_VIDEO_STATE_PLAYING", "playing" },
            { LRG_VIDEO_STATE_PAUSED, "LRG_VIDEO_STATE_PAUSED", "paused" },
            { LRG_VIDEO_STATE_FINISHED, "LRG_VIDEO_STATE_FINISHED", "finished" },
            { LRG_VIDEO_STATE_ERROR, "LRG_VIDEO_STATE_ERROR", "error" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVideoState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_subtitle_position_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SUBTITLE_POSITION_BOTTOM, "LRG_SUBTITLE_POSITION_BOTTOM", "bottom" },
            { LRG_SUBTITLE_POSITION_TOP, "LRG_SUBTITLE_POSITION_TOP", "top" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgSubtitlePosition"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Tutorial System Enums
 * ========================================================================== */

GType
lrg_tutorial_step_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TUTORIAL_STEP_TEXT, "LRG_TUTORIAL_STEP_TEXT", "text" },
            { LRG_TUTORIAL_STEP_HIGHLIGHT, "LRG_TUTORIAL_STEP_HIGHLIGHT", "highlight" },
            { LRG_TUTORIAL_STEP_INPUT, "LRG_TUTORIAL_STEP_INPUT", "input" },
            { LRG_TUTORIAL_STEP_CONDITION, "LRG_TUTORIAL_STEP_CONDITION", "condition" },
            { LRG_TUTORIAL_STEP_DELAY, "LRG_TUTORIAL_STEP_DELAY", "delay" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTutorialStepType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_tutorial_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TUTORIAL_STATE_INACTIVE, "LRG_TUTORIAL_STATE_INACTIVE", "inactive" },
            { LRG_TUTORIAL_STATE_ACTIVE, "LRG_TUTORIAL_STATE_ACTIVE", "active" },
            { LRG_TUTORIAL_STATE_PAUSED, "LRG_TUTORIAL_STATE_PAUSED", "paused" },
            { LRG_TUTORIAL_STATE_COMPLETED, "LRG_TUTORIAL_STATE_COMPLETED", "completed" },
            { LRG_TUTORIAL_STATE_SKIPPED, "LRG_TUTORIAL_STATE_SKIPPED", "skipped" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTutorialState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_highlight_style_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_HIGHLIGHT_STYLE_OUTLINE, "LRG_HIGHLIGHT_STYLE_OUTLINE", "outline" },
            { LRG_HIGHLIGHT_STYLE_GLOW, "LRG_HIGHLIGHT_STYLE_GLOW", "glow" },
            { LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS, "LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS", "darken-others" },
            { LRG_HIGHLIGHT_STYLE_SPOTLIGHT, "LRG_HIGHLIGHT_STYLE_SPOTLIGHT", "spotlight" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgHighlightStyle"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_arrow_direction_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ARROW_DIRECTION_UP, "LRG_ARROW_DIRECTION_UP", "up" },
            { LRG_ARROW_DIRECTION_DOWN, "LRG_ARROW_DIRECTION_DOWN", "down" },
            { LRG_ARROW_DIRECTION_LEFT, "LRG_ARROW_DIRECTION_LEFT", "left" },
            { LRG_ARROW_DIRECTION_RIGHT, "LRG_ARROW_DIRECTION_RIGHT", "right" },
            { LRG_ARROW_DIRECTION_AUTO, "LRG_ARROW_DIRECTION_AUTO", "auto" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgArrowDirection"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_input_device_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_INPUT_DEVICE_KEYBOARD, "LRG_INPUT_DEVICE_KEYBOARD", "keyboard" },
            { LRG_INPUT_DEVICE_MOUSE, "LRG_INPUT_DEVICE_MOUSE", "mouse" },
            { LRG_INPUT_DEVICE_GAMEPAD, "LRG_INPUT_DEVICE_GAMEPAD", "gamepad" },
            { LRG_INPUT_DEVICE_TOUCH, "LRG_INPUT_DEVICE_TOUCH", "touch" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgInputDeviceType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_gamepad_style_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_GAMEPAD_STYLE_XBOX, "LRG_GAMEPAD_STYLE_XBOX", "xbox" },
            { LRG_GAMEPAD_STYLE_PLAYSTATION, "LRG_GAMEPAD_STYLE_PLAYSTATION", "playstation" },
            { LRG_GAMEPAD_STYLE_NINTENDO, "LRG_GAMEPAD_STYLE_NINTENDO", "nintendo" },
            { LRG_GAMEPAD_STYLE_GENERIC, "LRG_GAMEPAD_STYLE_GENERIC", "generic" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgGamepadStyle"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Phase 4: Tween System Enums
 * ========================================================================== */

GType
lrg_easing_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_EASING_LINEAR, "LRG_EASING_LINEAR", "linear" },
            { LRG_EASING_EASE_IN_QUAD, "LRG_EASING_EASE_IN_QUAD", "ease-in-quad" },
            { LRG_EASING_EASE_OUT_QUAD, "LRG_EASING_EASE_OUT_QUAD", "ease-out-quad" },
            { LRG_EASING_EASE_IN_OUT_QUAD, "LRG_EASING_EASE_IN_OUT_QUAD", "ease-in-out-quad" },
            { LRG_EASING_EASE_IN_CUBIC, "LRG_EASING_EASE_IN_CUBIC", "ease-in-cubic" },
            { LRG_EASING_EASE_OUT_CUBIC, "LRG_EASING_EASE_OUT_CUBIC", "ease-out-cubic" },
            { LRG_EASING_EASE_IN_OUT_CUBIC, "LRG_EASING_EASE_IN_OUT_CUBIC", "ease-in-out-cubic" },
            { LRG_EASING_EASE_IN_QUART, "LRG_EASING_EASE_IN_QUART", "ease-in-quart" },
            { LRG_EASING_EASE_OUT_QUART, "LRG_EASING_EASE_OUT_QUART", "ease-out-quart" },
            { LRG_EASING_EASE_IN_OUT_QUART, "LRG_EASING_EASE_IN_OUT_QUART", "ease-in-out-quart" },
            { LRG_EASING_EASE_IN_QUINT, "LRG_EASING_EASE_IN_QUINT", "ease-in-quint" },
            { LRG_EASING_EASE_OUT_QUINT, "LRG_EASING_EASE_OUT_QUINT", "ease-out-quint" },
            { LRG_EASING_EASE_IN_OUT_QUINT, "LRG_EASING_EASE_IN_OUT_QUINT", "ease-in-out-quint" },
            { LRG_EASING_EASE_IN_SINE, "LRG_EASING_EASE_IN_SINE", "ease-in-sine" },
            { LRG_EASING_EASE_OUT_SINE, "LRG_EASING_EASE_OUT_SINE", "ease-out-sine" },
            { LRG_EASING_EASE_IN_OUT_SINE, "LRG_EASING_EASE_IN_OUT_SINE", "ease-in-out-sine" },
            { LRG_EASING_EASE_IN_EXPO, "LRG_EASING_EASE_IN_EXPO", "ease-in-expo" },
            { LRG_EASING_EASE_OUT_EXPO, "LRG_EASING_EASE_OUT_EXPO", "ease-out-expo" },
            { LRG_EASING_EASE_IN_OUT_EXPO, "LRG_EASING_EASE_IN_OUT_EXPO", "ease-in-out-expo" },
            { LRG_EASING_EASE_IN_CIRC, "LRG_EASING_EASE_IN_CIRC", "ease-in-circ" },
            { LRG_EASING_EASE_OUT_CIRC, "LRG_EASING_EASE_OUT_CIRC", "ease-out-circ" },
            { LRG_EASING_EASE_IN_OUT_CIRC, "LRG_EASING_EASE_IN_OUT_CIRC", "ease-in-out-circ" },
            { LRG_EASING_EASE_IN_BACK, "LRG_EASING_EASE_IN_BACK", "ease-in-back" },
            { LRG_EASING_EASE_OUT_BACK, "LRG_EASING_EASE_OUT_BACK", "ease-out-back" },
            { LRG_EASING_EASE_IN_OUT_BACK, "LRG_EASING_EASE_IN_OUT_BACK", "ease-in-out-back" },
            { LRG_EASING_EASE_IN_ELASTIC, "LRG_EASING_EASE_IN_ELASTIC", "ease-in-elastic" },
            { LRG_EASING_EASE_OUT_ELASTIC, "LRG_EASING_EASE_OUT_ELASTIC", "ease-out-elastic" },
            { LRG_EASING_EASE_IN_OUT_ELASTIC, "LRG_EASING_EASE_IN_OUT_ELASTIC", "ease-in-out-elastic" },
            { LRG_EASING_EASE_IN_BOUNCE, "LRG_EASING_EASE_IN_BOUNCE", "ease-in-bounce" },
            { LRG_EASING_EASE_OUT_BOUNCE, "LRG_EASING_EASE_OUT_BOUNCE", "ease-out-bounce" },
            { LRG_EASING_EASE_IN_OUT_BOUNCE, "LRG_EASING_EASE_IN_OUT_BOUNCE", "ease-in-out-bounce" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgEasingType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_tween_loop_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TWEEN_LOOP_RESTART, "LRG_TWEEN_LOOP_RESTART", "restart" },
            { LRG_TWEEN_LOOP_PING_PONG, "LRG_TWEEN_LOOP_PING_PONG", "ping-pong" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTweenLoopMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_tween_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TWEEN_STATE_IDLE, "LRG_TWEEN_STATE_IDLE", "idle" },
            { LRG_TWEEN_STATE_RUNNING, "LRG_TWEEN_STATE_RUNNING", "running" },
            { LRG_TWEEN_STATE_PAUSED, "LRG_TWEEN_STATE_PAUSED", "paused" },
            { LRG_TWEEN_STATE_FINISHED, "LRG_TWEEN_STATE_FINISHED", "finished" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTweenState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Phase 4: Transition System Enums
 * ========================================================================== */

GType
lrg_transition_state_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TRANSITION_STATE_IDLE, "LRG_TRANSITION_STATE_IDLE", "idle" },
            { LRG_TRANSITION_STATE_OUT, "LRG_TRANSITION_STATE_OUT", "out" },
            { LRG_TRANSITION_STATE_HOLD, "LRG_TRANSITION_STATE_HOLD", "hold" },
            { LRG_TRANSITION_STATE_IN, "LRG_TRANSITION_STATE_IN", "in" },
            { LRG_TRANSITION_STATE_COMPLETE, "LRG_TRANSITION_STATE_COMPLETE", "complete" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTransitionState"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_transition_direction_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TRANSITION_DIRECTION_LEFT, "LRG_TRANSITION_DIRECTION_LEFT", "left" },
            { LRG_TRANSITION_DIRECTION_RIGHT, "LRG_TRANSITION_DIRECTION_RIGHT", "right" },
            { LRG_TRANSITION_DIRECTION_UP, "LRG_TRANSITION_DIRECTION_UP", "up" },
            { LRG_TRANSITION_DIRECTION_DOWN, "LRG_TRANSITION_DIRECTION_DOWN", "down" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTransitionDirection"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_slide_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SLIDE_MODE_PUSH, "LRG_SLIDE_MODE_PUSH", "push" },
            { LRG_SLIDE_MODE_COVER, "LRG_SLIDE_MODE_COVER", "cover" },
            { LRG_SLIDE_MODE_REVEAL, "LRG_SLIDE_MODE_REVEAL", "reveal" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgSlideMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_zoom_direction_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ZOOM_DIRECTION_IN, "LRG_ZOOM_DIRECTION_IN", "in" },
            { LRG_ZOOM_DIRECTION_OUT, "LRG_ZOOM_DIRECTION_OUT", "out" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgZoomDirection"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Phase 4: Trigger2D System Enums
 * ========================================================================== */

GType
lrg_trigger2d_shape_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TRIGGER2D_SHAPE_RECTANGLE, "LRG_TRIGGER2D_SHAPE_RECTANGLE", "rectangle" },
            { LRG_TRIGGER2D_SHAPE_CIRCLE, "LRG_TRIGGER2D_SHAPE_CIRCLE", "circle" },
            { LRG_TRIGGER2D_SHAPE_POLYGON, "LRG_TRIGGER2D_SHAPE_POLYGON", "polygon" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTrigger2DShape"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_trigger2d_event_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_TRIGGER2D_EVENT_ENTER, "LRG_TRIGGER2D_EVENT_ENTER", "enter" },
            { LRG_TRIGGER2D_EVENT_STAY, "LRG_TRIGGER2D_EVENT_STAY", "stay" },
            { LRG_TRIGGER2D_EVENT_EXIT, "LRG_TRIGGER2D_EVENT_EXIT", "exit" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgTrigger2DEventType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Phase 4: Atlas System Enums
 * ========================================================================== */

GType
lrg_sprite_sheet_format_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SPRITE_SHEET_FORMAT_GRID, "LRG_SPRITE_SHEET_FORMAT_GRID", "grid" },
            { LRG_SPRITE_SHEET_FORMAT_ASEPRITE, "LRG_SPRITE_SHEET_FORMAT_ASEPRITE", "aseprite" },
            { LRG_SPRITE_SHEET_FORMAT_TEXTUREPACKER, "LRG_SPRITE_SHEET_FORMAT_TEXTUREPACKER", "texturepacker" },
            { LRG_SPRITE_SHEET_FORMAT_LIBREGNUM, "LRG_SPRITE_SHEET_FORMAT_LIBREGNUM", "libregnum" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgSpriteSheetFormat"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_atlas_pack_method_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ATLAS_PACK_METHOD_SHELF, "LRG_ATLAS_PACK_METHOD_SHELF", "shelf" },
            { LRG_ATLAS_PACK_METHOD_MAXRECTS, "LRG_ATLAS_PACK_METHOD_MAXRECTS", "maxrects" },
            { LRG_ATLAS_PACK_METHOD_GUILLOTINE, "LRG_ATLAS_PACK_METHOD_GUILLOTINE", "guillotine" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAtlasPackMethod"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_nine_slice_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_NINE_SLICE_MODE_STRETCH, "LRG_NINE_SLICE_MODE_STRETCH", "stretch" },
            { LRG_NINE_SLICE_MODE_TILE, "LRG_NINE_SLICE_MODE_TILE", "tile" },
            { LRG_NINE_SLICE_MODE_TILE_FIT, "LRG_NINE_SLICE_MODE_TILE_FIT", "tile-fit" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgNineSliceMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Phase 4: Weather System Enums
 * ========================================================================== */

GType
lrg_fog_type_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_FOG_TYPE_UNIFORM, "LRG_FOG_TYPE_UNIFORM", "uniform" },
            { LRG_FOG_TYPE_LINEAR, "LRG_FOG_TYPE_LINEAR", "linear" },
            { LRG_FOG_TYPE_EXPONENTIAL, "LRG_FOG_TYPE_EXPONENTIAL", "exponential" },
            { LRG_FOG_TYPE_HEIGHT, "LRG_FOG_TYPE_HEIGHT", "height" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgFogType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Phase 4: Lighting System Enums
 * ========================================================================== */

GType
lrg_light_falloff_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_LIGHT_FALLOFF_NONE, "LRG_LIGHT_FALLOFF_NONE", "none" },
            { LRG_LIGHT_FALLOFF_LINEAR, "LRG_LIGHT_FALLOFF_LINEAR", "linear" },
            { LRG_LIGHT_FALLOFF_QUADRATIC, "LRG_LIGHT_FALLOFF_QUADRATIC", "quadratic" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgLightFalloff"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_light_blend_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_LIGHT_BLEND_MULTIPLY, "LRG_LIGHT_BLEND_MULTIPLY", "multiply" },
            { LRG_LIGHT_BLEND_ADDITIVE, "LRG_LIGHT_BLEND_ADDITIVE", "additive" },
            { LRG_LIGHT_BLEND_SOFT, "LRG_LIGHT_BLEND_SOFT", "soft" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgLightBlendMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_shadow_method_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SHADOW_METHOD_RAY_CAST, "LRG_SHADOW_METHOD_RAY_CAST", "ray-cast" },
            { LRG_SHADOW_METHOD_GEOMETRY, "LRG_SHADOW_METHOD_GEOMETRY", "geometry" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgShadowMethod"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Analytics Module (Phase 5)
 * ========================================================================== */

/**
 * lrg_analytics_error_quark:
 *
 * Gets the error quark for analytics errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_analytics_error_quark (void)
{
    return g_quark_from_static_string ("lrg-analytics-error-quark");
}

GType
lrg_analytics_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ANALYTICS_ERROR_FAILED, "LRG_ANALYTICS_ERROR_FAILED", "failed" },
            { LRG_ANALYTICS_ERROR_NETWORK, "LRG_ANALYTICS_ERROR_NETWORK", "network" },
            { LRG_ANALYTICS_ERROR_CONSENT, "LRG_ANALYTICS_ERROR_CONSENT", "consent" },
            { LRG_ANALYTICS_ERROR_DISABLED, "LRG_ANALYTICS_ERROR_DISABLED", "disabled" },
            { LRG_ANALYTICS_ERROR_BACKEND, "LRG_ANALYTICS_ERROR_BACKEND", "backend" },
            { LRG_ANALYTICS_ERROR_SERIALIZE, "LRG_ANALYTICS_ERROR_SERIALIZE", "serialize" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAnalyticsError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_analytics_format_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ANALYTICS_FORMAT_JSON, "LRG_ANALYTICS_FORMAT_JSON", "json" },
            { LRG_ANALYTICS_FORMAT_YAML, "LRG_ANALYTICS_FORMAT_YAML", "yaml" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAnalyticsFormat"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Achievement Module (Phase 5)
 * ========================================================================== */

/**
 * lrg_achievement_error_quark:
 *
 * Gets the error quark for achievement errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_achievement_error_quark (void)
{
    return g_quark_from_static_string ("lrg-achievement-error-quark");
}

GType
lrg_achievement_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_ACHIEVEMENT_ERROR_FAILED, "LRG_ACHIEVEMENT_ERROR_FAILED", "failed" },
            { LRG_ACHIEVEMENT_ERROR_NOT_FOUND, "LRG_ACHIEVEMENT_ERROR_NOT_FOUND", "not-found" },
            { LRG_ACHIEVEMENT_ERROR_ALREADY_UNLOCKED, "LRG_ACHIEVEMENT_ERROR_ALREADY_UNLOCKED", "already-unlocked" },
            { LRG_ACHIEVEMENT_ERROR_SAVE, "LRG_ACHIEVEMENT_ERROR_SAVE", "save" },
            { LRG_ACHIEVEMENT_ERROR_LOAD, "LRG_ACHIEVEMENT_ERROR_LOAD", "load" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgAchievementError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_notification_position_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_NOTIFICATION_POSITION_TOP_LEFT, "LRG_NOTIFICATION_POSITION_TOP_LEFT", "top-left" },
            { LRG_NOTIFICATION_POSITION_TOP_CENTER, "LRG_NOTIFICATION_POSITION_TOP_CENTER", "top-center" },
            { LRG_NOTIFICATION_POSITION_TOP_RIGHT, "LRG_NOTIFICATION_POSITION_TOP_RIGHT", "top-right" },
            { LRG_NOTIFICATION_POSITION_BOTTOM_LEFT, "LRG_NOTIFICATION_POSITION_BOTTOM_LEFT", "bottom-left" },
            { LRG_NOTIFICATION_POSITION_BOTTOM_CENTER, "LRG_NOTIFICATION_POSITION_BOTTOM_CENTER", "bottom-center" },
            { LRG_NOTIFICATION_POSITION_BOTTOM_RIGHT, "LRG_NOTIFICATION_POSITION_BOTTOM_RIGHT", "bottom-right" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgNotificationPosition"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Photo Mode Module (Phase 5)
 * ========================================================================== */

/**
 * lrg_photo_mode_error_quark:
 *
 * Gets the error quark for photo mode errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_photo_mode_error_quark (void)
{
    return g_quark_from_static_string ("lrg-photo-mode-error-quark");
}

GType
lrg_photo_mode_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_PHOTO_MODE_ERROR_FAILED, "LRG_PHOTO_MODE_ERROR_FAILED", "failed" },
            { LRG_PHOTO_MODE_ERROR_CAPTURE, "LRG_PHOTO_MODE_ERROR_CAPTURE", "capture" },
            { LRG_PHOTO_MODE_ERROR_SAVE, "LRG_PHOTO_MODE_ERROR_SAVE", "save" },
            { LRG_PHOTO_MODE_ERROR_INVALID_FORMAT, "LRG_PHOTO_MODE_ERROR_INVALID_FORMAT", "invalid-format" },
            { LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE, "LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE", "already-active" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgPhotoModeError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_screenshot_format_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_SCREENSHOT_FORMAT_PNG, "LRG_SCREENSHOT_FORMAT_PNG", "png" },
            { LRG_SCREENSHOT_FORMAT_JPG, "LRG_SCREENSHOT_FORMAT_JPG", "jpg" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgScreenshotFormat"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * Steam Workshop Module (Phase 5)
 * ========================================================================== */

GQuark
lrg_workshop_error_quark (void)
{
    return g_quark_from_static_string ("lrg-workshop-error-quark");
}

GType
lrg_workshop_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_WORKSHOP_ERROR_FAILED, "LRG_WORKSHOP_ERROR_FAILED", "failed" },
            { LRG_WORKSHOP_ERROR_NOT_AVAILABLE, "LRG_WORKSHOP_ERROR_NOT_AVAILABLE", "not-available" },
            { LRG_WORKSHOP_ERROR_QUERY, "LRG_WORKSHOP_ERROR_QUERY", "query" },
            { LRG_WORKSHOP_ERROR_SUBSCRIBE, "LRG_WORKSHOP_ERROR_SUBSCRIBE", "subscribe" },
            { LRG_WORKSHOP_ERROR_DOWNLOAD, "LRG_WORKSHOP_ERROR_DOWNLOAD", "download" },
            { LRG_WORKSHOP_ERROR_UPDATE, "LRG_WORKSHOP_ERROR_UPDATE", "update" },
            { LRG_WORKSHOP_ERROR_CREATE, "LRG_WORKSHOP_ERROR_CREATE", "create" },
            { LRG_WORKSHOP_ERROR_DELETE, "LRG_WORKSHOP_ERROR_DELETE", "delete" },
            { LRG_WORKSHOP_ERROR_BUSY, "LRG_WORKSHOP_ERROR_BUSY", "busy" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgWorkshopError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ============================================================================
 * Demo Module Enums
 * ========================================================================== */

GType
lrg_demo_end_reason_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DEMO_END_REASON_TIME_LIMIT, "LRG_DEMO_END_REASON_TIME_LIMIT", "time-limit" },
            { LRG_DEMO_END_REASON_CONTENT_COMPLETE, "LRG_DEMO_END_REASON_CONTENT_COMPLETE", "content-complete" },
            { LRG_DEMO_END_REASON_MANUAL, "LRG_DEMO_END_REASON_MANUAL", "manual" },
            { LRG_DEMO_END_REASON_UPGRADED, "LRG_DEMO_END_REASON_UPGRADED", "upgraded" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDemoEndReason"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GQuark
lrg_demo_error_quark (void)
{
    return g_quark_from_static_string ("lrg-demo-error-quark");
}

GType
lrg_demo_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_DEMO_ERROR_FAILED, "LRG_DEMO_ERROR_FAILED", "failed" },
            { LRG_DEMO_ERROR_CONTENT_GATED, "LRG_DEMO_ERROR_CONTENT_GATED", "content-gated" },
            { LRG_DEMO_ERROR_TIME_EXPIRED, "LRG_DEMO_ERROR_TIME_EXPIRED", "time-expired" },
            { LRG_DEMO_ERROR_SAVE_LOCKED, "LRG_DEMO_ERROR_SAVE_LOCKED", "save-locked" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgDemoError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ============================================================================
 * VR Module Enums
 * ========================================================================== */

GType
lrg_vr_eye_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VR_EYE_LEFT, "LRG_VR_EYE_LEFT", "left" },
            { LRG_VR_EYE_RIGHT, "LRG_VR_EYE_RIGHT", "right" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVREye"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_vr_hand_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VR_HAND_LEFT, "LRG_VR_HAND_LEFT", "left" },
            { LRG_VR_HAND_RIGHT, "LRG_VR_HAND_RIGHT", "right" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVRHand"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_vr_controller_button_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GFlagsValue values[] = {
            { LRG_VR_CONTROLLER_BUTTON_SYSTEM, "LRG_VR_CONTROLLER_BUTTON_SYSTEM", "system" },
            { LRG_VR_CONTROLLER_BUTTON_MENU, "LRG_VR_CONTROLLER_BUTTON_MENU", "menu" },
            { LRG_VR_CONTROLLER_BUTTON_GRIP, "LRG_VR_CONTROLLER_BUTTON_GRIP", "grip" },
            { LRG_VR_CONTROLLER_BUTTON_TRIGGER, "LRG_VR_CONTROLLER_BUTTON_TRIGGER", "trigger" },
            { LRG_VR_CONTROLLER_BUTTON_TOUCHPAD, "LRG_VR_CONTROLLER_BUTTON_TOUCHPAD", "touchpad" },
            { LRG_VR_CONTROLLER_BUTTON_THUMBSTICK, "LRG_VR_CONTROLLER_BUTTON_THUMBSTICK", "thumbstick" },
            { LRG_VR_CONTROLLER_BUTTON_A, "LRG_VR_CONTROLLER_BUTTON_A", "a" },
            { LRG_VR_CONTROLLER_BUTTON_B, "LRG_VR_CONTROLLER_BUTTON_B", "b" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_flags_register_static (g_intern_static_string ("LrgVRControllerButton"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_vr_turn_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VR_TURN_MODE_SMOOTH, "LRG_VR_TURN_MODE_SMOOTH", "smooth" },
            { LRG_VR_TURN_MODE_SNAP, "LRG_VR_TURN_MODE_SNAP", "snap" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVRTurnMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GType
lrg_vr_locomotion_mode_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VR_LOCOMOTION_SMOOTH, "LRG_VR_LOCOMOTION_SMOOTH", "smooth" },
            { LRG_VR_LOCOMOTION_TELEPORT, "LRG_VR_LOCOMOTION_TELEPORT", "teleport" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVRLocomotionMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

GQuark
lrg_vr_error_quark (void)
{
    return g_quark_from_static_string ("lrg-vr-error-quark");
}

GType
lrg_vr_error_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_VR_ERROR_FAILED, "LRG_VR_ERROR_FAILED", "failed" },
            { LRG_VR_ERROR_NOT_AVAILABLE, "LRG_VR_ERROR_NOT_AVAILABLE", "not-available" },
            { LRG_VR_ERROR_HMD_NOT_FOUND, "LRG_VR_ERROR_HMD_NOT_FOUND", "hmd-not-found" },
            { LRG_VR_ERROR_COMPOSITOR, "LRG_VR_ERROR_COMPOSITOR", "compositor" },
            { LRG_VR_ERROR_TRACKING, "LRG_VR_ERROR_TRACKING", "tracking" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgVRError"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}
