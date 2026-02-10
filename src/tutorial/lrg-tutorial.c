/* lrg-tutorial.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tutorial sequence definition.
 */

#include <gio/gio.h>

#include "lrg-tutorial.h"

#include <yaml-glib.h>

struct _LrgTutorial
{
    GObject parent_instance;

    gchar           *id;
    gchar           *name;
    gchar           *description;
    LrgTutorialState state;

    gboolean repeatable;
    gboolean skippable;

    GPtrArray  *steps;          /* LrgTutorialStep* array */
    GHashTable *steps_by_id;    /* id -> LrgTutorialStep* */

    /* Runtime state */
    guint   current_step_index;
    gfloat  step_elapsed_time;

    /* Condition callback */
    LrgTutorialConditionFunc condition_callback;
    gpointer                 condition_user_data;
    GDestroyNotify           condition_destroy;
};

G_DEFINE_FINAL_TYPE (LrgTutorial, lrg_tutorial, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_STATE,
    PROP_STEP_COUNT,
    PROP_CURRENT_STEP_INDEX,
    PROP_PROGRESS,
    PROP_REPEATABLE,
    PROP_SKIPPABLE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STARTED,
    SIGNAL_STEP_CHANGED,
    SIGNAL_COMPLETED,
    SIGNAL_SKIPPED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_tutorial_finalize (GObject *object)
{
    LrgTutorial *self = LRG_TUTORIAL (object);

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->description, g_free);
    g_clear_pointer (&self->steps, g_ptr_array_unref);
    g_clear_pointer (&self->steps_by_id, g_hash_table_unref);

    if (self->condition_destroy != NULL && self->condition_user_data != NULL)
    {
        self->condition_destroy (self->condition_user_data);
    }

    G_OBJECT_CLASS (lrg_tutorial_parent_class)->finalize (object);
}

static void
lrg_tutorial_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgTutorial *self = LRG_TUTORIAL (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;
    case PROP_STATE:
        g_value_set_int (value, self->state);
        break;
    case PROP_STEP_COUNT:
        g_value_set_uint (value, self->steps->len);
        break;
    case PROP_CURRENT_STEP_INDEX:
        g_value_set_uint (value, self->current_step_index);
        break;
    case PROP_PROGRESS:
        g_value_set_float (value, lrg_tutorial_get_progress (self));
        break;
    case PROP_REPEATABLE:
        g_value_set_boolean (value, self->repeatable);
        break;
    case PROP_SKIPPABLE:
        g_value_set_boolean (value, self->skippable);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tutorial_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgTutorial *self = LRG_TUTORIAL (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_clear_pointer (&self->id, g_free);
        self->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_clear_pointer (&self->name, g_free);
        self->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_clear_pointer (&self->description, g_free);
        self->description = g_value_dup_string (value);
        break;
    case PROP_REPEATABLE:
        self->repeatable = g_value_get_boolean (value);
        break;
    case PROP_SKIPPABLE:
        self->skippable = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tutorial_class_init (LrgTutorialClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_tutorial_finalize;
    object_class->get_property = lrg_tutorial_get_property;
    object_class->set_property = lrg_tutorial_set_property;

    /**
     * LrgTutorial:id:
     *
     * Unique identifier for the tutorial.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:name:
     *
     * Display name of the tutorial.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:description:
     *
     * Description of the tutorial.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:state:
     *
     * Current state of the tutorial.
     *
     * Since: 1.0
     */
    properties[PROP_STATE] =
        g_param_spec_int ("state", NULL, NULL,
                          LRG_TUTORIAL_STATE_INACTIVE,
                          LRG_TUTORIAL_STATE_SKIPPED,
                          LRG_TUTORIAL_STATE_INACTIVE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:step-count:
     *
     * Total number of steps.
     *
     * Since: 1.0
     */
    properties[PROP_STEP_COUNT] =
        g_param_spec_uint ("step-count", NULL, NULL,
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:current-step-index:
     *
     * Index of the current step.
     *
     * Since: 1.0
     */
    properties[PROP_CURRENT_STEP_INDEX] =
        g_param_spec_uint ("current-step-index", NULL, NULL,
                           0, G_MAXUINT, G_MAXUINT,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:progress:
     *
     * Tutorial progress (0.0 to 1.0).
     *
     * Since: 1.0
     */
    properties[PROP_PROGRESS] =
        g_param_spec_float ("progress", NULL, NULL,
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:repeatable:
     *
     * Whether the tutorial can be replayed.
     *
     * Since: 1.0
     */
    properties[PROP_REPEATABLE] =
        g_param_spec_boolean ("repeatable", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorial:skippable:
     *
     * Whether the entire tutorial can be skipped.
     *
     * Since: 1.0
     */
    properties[PROP_SKIPPABLE] =
        g_param_spec_boolean ("skippable", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTutorial::started:
     * @self: The tutorial
     *
     * Emitted when the tutorial starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_STARTED] =
        g_signal_new ("started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTutorial::step-changed:
     * @self: The tutorial
     * @step_index: The new step index
     * @step: The new step
     *
     * Emitted when the current step changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_STEP_CHANGED] =
        g_signal_new ("step-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT,
                      LRG_TYPE_TUTORIAL_STEP);

    /**
     * LrgTutorial::completed:
     * @self: The tutorial
     *
     * Emitted when the tutorial completes successfully.
     *
     * Since: 1.0
     */
    signals[SIGNAL_COMPLETED] =
        g_signal_new ("completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTutorial::skipped:
     * @self: The tutorial
     *
     * Emitted when the tutorial is skipped.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SKIPPED] =
        g_signal_new ("skipped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_tutorial_init (LrgTutorial *self)
{
    self->state = LRG_TUTORIAL_STATE_INACTIVE;
    self->repeatable = FALSE;
    self->skippable = TRUE;
    self->current_step_index = G_MAXUINT;

    self->steps = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_tutorial_step_free);
    self->steps_by_id = g_hash_table_new (g_str_hash, g_str_equal);
}

/**
 * lrg_tutorial_new:
 * @id: Unique identifier for the tutorial
 * @name: Display name of the tutorial
 *
 * Creates a new tutorial.
 *
 * Returns: (transfer full): A new #LrgTutorial
 *
 * Since: 1.0
 */
LrgTutorial *
lrg_tutorial_new (const gchar *id,
                  const gchar *name)
{
    return g_object_new (LRG_TYPE_TUTORIAL,
                         "id", id,
                         "name", name,
                         NULL);
}

/**
 * lrg_tutorial_new_from_file:
 * @path: Path to the tutorial definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a tutorial by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgTutorial or %NULL on error
 *
 * Since: 1.0
 */
LrgTutorial *
lrg_tutorial_new_from_file (const gchar  *path,
                            GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlNode *root;
    YamlMapping *mapping;
    const gchar *id;
    const gchar *name;
    const gchar *description;
    LrgTutorial *self;

    g_return_val_if_fail (path != NULL, NULL);

    /* Parse the YAML file */
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, error))
        return NULL;

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Empty tutorial file: %s", path);
        return NULL;
    }

    mapping = yaml_node_get_mapping (root);
    if (mapping == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Tutorial root must be a mapping: %s", path);
        return NULL;
    }

    /* Get required fields */
    id = yaml_mapping_get_string_member (mapping, "id");
    if (id == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Tutorial missing 'id' field: %s", path);
        return NULL;
    }

    name = yaml_mapping_get_string_member (mapping, "name");
    if (name == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Tutorial missing 'name' field: %s", path);
        return NULL;
    }

    /* Create tutorial */
    self = lrg_tutorial_new (id, name);

    /* Optional description */
    description = yaml_mapping_get_string_member (mapping, "description");
    if (description != NULL)
        lrg_tutorial_set_description (self, description);

    /* Optional flags */
    if (yaml_mapping_has_member (mapping, "repeatable"))
        lrg_tutorial_set_repeatable (self, yaml_mapping_get_boolean_member (mapping, "repeatable"));

    if (yaml_mapping_has_member (mapping, "skippable"))
        lrg_tutorial_set_skippable (self, yaml_mapping_get_boolean_member (mapping, "skippable"));

    /* Load steps */
    if (yaml_mapping_has_member (mapping, "steps"))
    {
        YamlSequence *steps_seq;
        guint i;
        guint n_steps;

        steps_seq = yaml_mapping_get_sequence_member (mapping, "steps");
        if (steps_seq != NULL)
        {
            n_steps = yaml_sequence_get_length (steps_seq);
            for (i = 0; i < n_steps; i++)
            {
                YamlMapping *step_map;
                const gchar *type_str;
                const gchar *step_id;
                LrgTutorialStep *step;

                step_map = yaml_sequence_get_mapping_element (steps_seq, i);
                if (step_map == NULL)
                    continue;

                type_str = yaml_mapping_get_string_member (step_map, "type");
                if (type_str == NULL)
                    continue;

                /* Create step based on type */
                step = NULL;
                if (g_strcmp0 (type_str, "text") == 0)
                {
                    const gchar *text;
                    const gchar *speaker;

                    text = yaml_mapping_get_string_member (step_map, "text");
                    speaker = yaml_mapping_get_string_member (step_map, "speaker");
                    step = lrg_tutorial_step_new_text (text, speaker);
                }
                else if (g_strcmp0 (type_str, "highlight") == 0)
                {
                    const gchar *target;
                    const gchar *style_str;
                    LrgHighlightStyle style;

                    target = yaml_mapping_get_string_member (step_map, "target_id");
                    style = LRG_HIGHLIGHT_STYLE_OUTLINE;

                    style_str = yaml_mapping_get_string_member (step_map, "highlight_style");
                    if (style_str != NULL)
                    {
                        if (g_strcmp0 (style_str, "glow") == 0)
                            style = LRG_HIGHLIGHT_STYLE_GLOW;
                        else if (g_strcmp0 (style_str, "darken_others") == 0)
                            style = LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS;
                        else if (g_strcmp0 (style_str, "spotlight") == 0)
                            style = LRG_HIGHLIGHT_STYLE_SPOTLIGHT;
                    }

                    step = lrg_tutorial_step_new_highlight (target, style);
                }
                else if (g_strcmp0 (type_str, "input") == 0)
                {
                    const gchar *action;
                    gboolean prompt;

                    action = yaml_mapping_get_string_member (step_map, "action_name");
                    prompt = FALSE;
                    if (yaml_mapping_has_member (step_map, "show_prompt"))
                        prompt = yaml_mapping_get_boolean_member (step_map, "show_prompt");

                    step = lrg_tutorial_step_new_input (action, prompt);
                }
                else if (g_strcmp0 (type_str, "condition") == 0)
                {
                    const gchar *cond_id;

                    cond_id = yaml_mapping_get_string_member (step_map, "condition_id");
                    step = lrg_tutorial_step_new_condition (cond_id);
                }
                else if (g_strcmp0 (type_str, "delay") == 0)
                {
                    gfloat dur;

                    dur = (gfloat) yaml_mapping_get_double_member (step_map, "duration");
                    step = lrg_tutorial_step_new_delay (dur);
                }

                if (step == NULL)
                    continue;

                /* Set common properties */
                step_id = yaml_mapping_get_string_member (step_map, "id");
                if (step_id != NULL)
                    lrg_tutorial_step_set_id (step, step_id);

                if (yaml_mapping_has_member (step_map, "can_skip"))
                    lrg_tutorial_step_set_can_skip (step, yaml_mapping_get_boolean_member (step_map, "can_skip"));

                if (yaml_mapping_has_member (step_map, "blocks_input"))
                    lrg_tutorial_step_set_blocks_input (step, yaml_mapping_get_boolean_member (step_map, "blocks_input"));

                if (yaml_mapping_has_member (step_map, "auto_advance"))
                    lrg_tutorial_step_set_auto_advance (step, yaml_mapping_get_boolean_member (step_map, "auto_advance"));

                if (yaml_mapping_has_member (step_map, "position_x") &&
                    yaml_mapping_has_member (step_map, "position_y"))
                {
                    lrg_tutorial_step_set_position (step,
                        (gfloat) yaml_mapping_get_double_member (step_map, "position_x"),
                        (gfloat) yaml_mapping_get_double_member (step_map, "position_y"));
                }

                if (yaml_mapping_has_member (step_map, "arrow_direction"))
                {
                    const gchar *arrow_str;

                    arrow_str = yaml_mapping_get_string_member (step_map, "arrow_direction");
                    if (arrow_str != NULL)
                    {
                        if (g_strcmp0 (arrow_str, "up") == 0)
                            lrg_tutorial_step_set_arrow_direction (step, LRG_ARROW_DIRECTION_UP);
                        else if (g_strcmp0 (arrow_str, "down") == 0)
                            lrg_tutorial_step_set_arrow_direction (step, LRG_ARROW_DIRECTION_DOWN);
                        else if (g_strcmp0 (arrow_str, "left") == 0)
                            lrg_tutorial_step_set_arrow_direction (step, LRG_ARROW_DIRECTION_LEFT);
                        else if (g_strcmp0 (arrow_str, "right") == 0)
                            lrg_tutorial_step_set_arrow_direction (step, LRG_ARROW_DIRECTION_RIGHT);
                        else
                            lrg_tutorial_step_set_arrow_direction (step, LRG_ARROW_DIRECTION_AUTO);
                    }
                }

                lrg_tutorial_add_step (self, step);
                lrg_tutorial_step_free (step);
            }
        }
    }

    return self;
}

/**
 * lrg_tutorial_get_id:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial ID.
 *
 * Returns: (transfer none): The tutorial ID
 *
 * Since: 1.0
 */
const gchar *
lrg_tutorial_get_id (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), NULL);
    return self->id;
}

/**
 * lrg_tutorial_get_name:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial display name.
 *
 * Returns: (transfer none): The tutorial name
 *
 * Since: 1.0
 */
const gchar *
lrg_tutorial_get_name (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), NULL);
    return self->name;
}

/**
 * lrg_tutorial_get_description:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial description.
 *
 * Returns: (transfer none) (nullable): The description
 *
 * Since: 1.0
 */
const gchar *
lrg_tutorial_get_description (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), NULL);
    return self->description;
}

/**
 * lrg_tutorial_set_description:
 * @self: A #LrgTutorial
 * @description: The description
 *
 * Sets the tutorial description.
 *
 * Since: 1.0
 */
void
lrg_tutorial_set_description (LrgTutorial *self,
                              const gchar *description)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    g_clear_pointer (&self->description, g_free);
    self->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
}

/**
 * lrg_tutorial_get_state:
 * @self: A #LrgTutorial
 *
 * Gets the current state of the tutorial.
 *
 * Returns: The tutorial state
 *
 * Since: 1.0
 */
LrgTutorialState
lrg_tutorial_get_state (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), LRG_TUTORIAL_STATE_INACTIVE);
    return self->state;
}

/**
 * lrg_tutorial_is_repeatable:
 * @self: A #LrgTutorial
 *
 * Gets whether the tutorial can be replayed.
 *
 * Returns: %TRUE if repeatable
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_is_repeatable (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), FALSE);
    return self->repeatable;
}

/**
 * lrg_tutorial_set_repeatable:
 * @self: A #LrgTutorial
 * @repeatable: Whether the tutorial is repeatable
 *
 * Sets whether the tutorial can be replayed.
 *
 * Since: 1.0
 */
void
lrg_tutorial_set_repeatable (LrgTutorial *self,
                             gboolean     repeatable)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    self->repeatable = repeatable;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REPEATABLE]);
}

/**
 * lrg_tutorial_is_skippable:
 * @self: A #LrgTutorial
 *
 * Gets whether the entire tutorial can be skipped.
 *
 * Returns: %TRUE if skippable
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_is_skippable (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), TRUE);
    return self->skippable;
}

/**
 * lrg_tutorial_set_skippable:
 * @self: A #LrgTutorial
 * @skippable: Whether the tutorial is skippable
 *
 * Sets whether the entire tutorial can be skipped.
 *
 * Since: 1.0
 */
void
lrg_tutorial_set_skippable (LrgTutorial *self,
                            gboolean     skippable)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    self->skippable = skippable;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKIPPABLE]);
}

/**
 * lrg_tutorial_get_step_count:
 * @self: A #LrgTutorial
 *
 * Gets the total number of steps.
 *
 * Returns: The step count
 *
 * Since: 1.0
 */
guint
lrg_tutorial_get_step_count (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), 0);
    return self->steps->len;
}

/**
 * lrg_tutorial_get_step:
 * @self: A #LrgTutorial
 * @index: Step index (0-based)
 *
 * Gets a step by index.
 *
 * Returns: (transfer none) (nullable): The step, or %NULL if out of bounds
 *
 * Since: 1.0
 */
LrgTutorialStep *
lrg_tutorial_get_step (LrgTutorial *self,
                       guint        index)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), NULL);

    if (index >= self->steps->len)
        return NULL;

    return g_ptr_array_index (self->steps, index);
}

/**
 * lrg_tutorial_get_step_by_id:
 * @self: A #LrgTutorial
 * @id: Step ID
 *
 * Gets a step by ID.
 *
 * Returns: (transfer none) (nullable): The step, or %NULL if not found
 *
 * Since: 1.0
 */
LrgTutorialStep *
lrg_tutorial_get_step_by_id (LrgTutorial *self,
                             const gchar *id)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->steps_by_id, id);
}

/**
 * lrg_tutorial_add_step:
 * @self: A #LrgTutorial
 * @step: The step to add (copied)
 *
 * Adds a step to the end of the tutorial.
 *
 * Returns: The index of the added step
 *
 * Since: 1.0
 */
guint
lrg_tutorial_add_step (LrgTutorial           *self,
                       const LrgTutorialStep *step)
{
    LrgTutorialStep *copy;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_TUTORIAL (self), 0);
    g_return_val_if_fail (step != NULL, 0);

    copy = lrg_tutorial_step_copy (step);
    g_ptr_array_add (self->steps, copy);

    id = lrg_tutorial_step_get_id (copy);
    if (id != NULL)
    {
        g_hash_table_insert (self->steps_by_id, (gpointer) id, copy);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STEP_COUNT]);

    return self->steps->len - 1;
}

/**
 * lrg_tutorial_insert_step:
 * @self: A #LrgTutorial
 * @index: Position to insert at
 * @step: The step to insert (copied)
 *
 * Inserts a step at the specified position.
 *
 * Since: 1.0
 */
void
lrg_tutorial_insert_step (LrgTutorial           *self,
                          guint                  index,
                          const LrgTutorialStep *step)
{
    LrgTutorialStep *copy;
    const gchar *id;

    g_return_if_fail (LRG_IS_TUTORIAL (self));
    g_return_if_fail (step != NULL);

    copy = lrg_tutorial_step_copy (step);
    g_ptr_array_insert (self->steps, (gint)index, copy);

    id = lrg_tutorial_step_get_id (copy);
    if (id != NULL)
    {
        g_hash_table_insert (self->steps_by_id, (gpointer) id, copy);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STEP_COUNT]);
}

/**
 * lrg_tutorial_remove_step:
 * @self: A #LrgTutorial
 * @index: Index of step to remove
 *
 * Removes a step by index.
 *
 * Returns: %TRUE if the step was removed
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_remove_step (LrgTutorial *self,
                          guint        index)
{
    LrgTutorialStep *step;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_TUTORIAL (self), FALSE);

    if (index >= self->steps->len)
        return FALSE;

    step = g_ptr_array_index (self->steps, index);
    id = lrg_tutorial_step_get_id (step);

    if (id != NULL)
    {
        g_hash_table_remove (self->steps_by_id, id);
    }

    g_ptr_array_remove_index (self->steps, index);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STEP_COUNT]);

    return TRUE;
}

/**
 * lrg_tutorial_clear_steps:
 * @self: A #LrgTutorial
 *
 * Removes all steps from the tutorial.
 *
 * Since: 1.0
 */
void
lrg_tutorial_clear_steps (LrgTutorial *self)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    g_ptr_array_set_size (self->steps, 0);
    g_hash_table_remove_all (self->steps_by_id);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STEP_COUNT]);
}

/**
 * lrg_tutorial_get_current_step_index:
 * @self: A #LrgTutorial
 *
 * Gets the current step index.
 *
 * Returns: The current step index, or G_MAXUINT if not active
 *
 * Since: 1.0
 */
guint
lrg_tutorial_get_current_step_index (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), G_MAXUINT);
    return self->current_step_index;
}

/**
 * lrg_tutorial_get_current_step:
 * @self: A #LrgTutorial
 *
 * Gets the current step.
 *
 * Returns: (transfer none) (nullable): The current step, or %NULL if not active
 *
 * Since: 1.0
 */
LrgTutorialStep *
lrg_tutorial_get_current_step (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), NULL);

    if (self->current_step_index >= self->steps->len)
        return NULL;

    return g_ptr_array_index (self->steps, self->current_step_index);
}

/**
 * lrg_tutorial_get_progress:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial progress as a fraction.
 *
 * Returns: Progress from 0.0 to 1.0
 *
 * Since: 1.0
 */
gfloat
lrg_tutorial_get_progress (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), 0.0f);

    if (self->steps->len == 0)
        return 0.0f;

    if (self->state == LRG_TUTORIAL_STATE_COMPLETED ||
        self->state == LRG_TUTORIAL_STATE_SKIPPED)
        return 1.0f;

    if (self->state == LRG_TUTORIAL_STATE_INACTIVE)
        return 0.0f;

    return (gfloat)self->current_step_index / (gfloat)self->steps->len;
}

/**
 * lrg_tutorial_start:
 * @self: A #LrgTutorial
 *
 * Starts the tutorial from the beginning.
 *
 * Returns: %TRUE if the tutorial was started
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_start (LrgTutorial *self)
{
    LrgTutorialStep *step;

    g_return_val_if_fail (LRG_IS_TUTORIAL (self), FALSE);

    if (self->steps->len == 0)
        return FALSE;

    if (self->state == LRG_TUTORIAL_STATE_COMPLETED ||
        self->state == LRG_TUTORIAL_STATE_SKIPPED)
    {
        if (!self->repeatable)
            return FALSE;
    }

    self->state = LRG_TUTORIAL_STATE_ACTIVE;
    self->current_step_index = 0;
    self->step_elapsed_time = 0.0f;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STEP_INDEX]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

    g_signal_emit (self, signals[SIGNAL_STARTED], 0);

    step = lrg_tutorial_get_current_step (self);
    if (step != NULL)
    {
        g_signal_emit (self, signals[SIGNAL_STEP_CHANGED], 0,
                       self->current_step_index, step);
    }

    return TRUE;
}

/**
 * lrg_tutorial_pause:
 * @self: A #LrgTutorial
 *
 * Pauses the tutorial.
 *
 * Since: 1.0
 */
void
lrg_tutorial_pause (LrgTutorial *self)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    if (self->state != LRG_TUTORIAL_STATE_ACTIVE)
        return;

    self->state = LRG_TUTORIAL_STATE_PAUSED;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

/**
 * lrg_tutorial_resume:
 * @self: A #LrgTutorial
 *
 * Resumes a paused tutorial.
 *
 * Since: 1.0
 */
void
lrg_tutorial_resume (LrgTutorial *self)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    if (self->state != LRG_TUTORIAL_STATE_PAUSED)
        return;

    self->state = LRG_TUTORIAL_STATE_ACTIVE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

/**
 * lrg_tutorial_skip:
 * @self: A #LrgTutorial
 *
 * Skips the entire tutorial.
 *
 * Returns: %TRUE if the tutorial was skipped
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_skip (LrgTutorial *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL (self), FALSE);

    if (!self->skippable)
        return FALSE;

    if (self->state == LRG_TUTORIAL_STATE_INACTIVE ||
        self->state == LRG_TUTORIAL_STATE_COMPLETED ||
        self->state == LRG_TUTORIAL_STATE_SKIPPED)
        return FALSE;

    self->state = LRG_TUTORIAL_STATE_SKIPPED;
    self->current_step_index = G_MAXUINT;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STEP_INDEX]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

    g_signal_emit (self, signals[SIGNAL_SKIPPED], 0);

    return TRUE;
}

/**
 * lrg_tutorial_advance:
 * @self: A #LrgTutorial
 *
 * Advances to the next step.
 *
 * Returns: %TRUE if advanced, %FALSE if at end or not active
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_advance (LrgTutorial *self)
{
    LrgTutorialStep *step;

    g_return_val_if_fail (LRG_IS_TUTORIAL (self), FALSE);

    if (self->state != LRG_TUTORIAL_STATE_ACTIVE)
        return FALSE;

    self->current_step_index++;
    self->step_elapsed_time = 0.0f;

    if (self->current_step_index >= self->steps->len)
    {
        /* Tutorial completed */
        self->state = LRG_TUTORIAL_STATE_COMPLETED;
        self->current_step_index = G_MAXUINT;

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STEP_INDEX]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

        g_signal_emit (self, signals[SIGNAL_COMPLETED], 0);

        return FALSE;
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STEP_INDEX]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

    step = lrg_tutorial_get_current_step (self);
    if (step != NULL)
    {
        g_signal_emit (self, signals[SIGNAL_STEP_CHANGED], 0,
                       self->current_step_index, step);
    }

    return TRUE;
}

/**
 * lrg_tutorial_go_to_step:
 * @self: A #LrgTutorial
 * @index: Step index to go to
 *
 * Jumps to a specific step.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_go_to_step (LrgTutorial *self,
                         guint        index)
{
    LrgTutorialStep *step;

    g_return_val_if_fail (LRG_IS_TUTORIAL (self), FALSE);

    if (index >= self->steps->len)
        return FALSE;

    if (self->state != LRG_TUTORIAL_STATE_ACTIVE &&
        self->state != LRG_TUTORIAL_STATE_PAUSED)
        return FALSE;

    self->current_step_index = index;
    self->step_elapsed_time = 0.0f;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STEP_INDEX]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

    step = lrg_tutorial_get_current_step (self);
    if (step != NULL)
    {
        g_signal_emit (self, signals[SIGNAL_STEP_CHANGED], 0,
                       self->current_step_index, step);
    }

    return TRUE;
}

/**
 * lrg_tutorial_reset:
 * @self: A #LrgTutorial
 *
 * Resets the tutorial to inactive state.
 *
 * Since: 1.0
 */
void
lrg_tutorial_reset (LrgTutorial *self)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    self->state = LRG_TUTORIAL_STATE_INACTIVE;
    self->current_step_index = G_MAXUINT;
    self->step_elapsed_time = 0.0f;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STEP_INDEX]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
}

/**
 * lrg_tutorial_update:
 * @self: A #LrgTutorial
 * @delta_time: Time since last update in seconds
 *
 * Updates the tutorial state. Handles delay steps and auto-advancing.
 *
 * Since: 1.0
 */
void
lrg_tutorial_update (LrgTutorial *self,
                     gfloat       delta_time)
{
    LrgTutorialStep *step;
    LrgTutorialStepType step_type;

    g_return_if_fail (LRG_IS_TUTORIAL (self));

    if (self->state != LRG_TUTORIAL_STATE_ACTIVE)
        return;

    step = lrg_tutorial_get_current_step (self);
    if (step == NULL)
        return;

    self->step_elapsed_time += delta_time;
    step_type = lrg_tutorial_step_get_step_type (step);

    switch (step_type)
    {
    case LRG_TUTORIAL_STEP_DELAY:
        {
            gfloat duration = lrg_tutorial_step_get_duration (step);
            if (self->step_elapsed_time >= duration)
            {
                lrg_tutorial_advance (self);
            }
        }
        break;

    case LRG_TUTORIAL_STEP_CONDITION:
        {
            const gchar *condition_id = lrg_tutorial_step_get_condition_id (step);
            if (condition_id != NULL && self->condition_callback != NULL)
            {
                gboolean met = self->condition_callback (condition_id,
                                                         self->condition_user_data);
                if (met && lrg_tutorial_step_get_auto_advance (step))
                {
                    lrg_tutorial_advance (self);
                }
            }
        }
        break;

    default:
        break;
    }
}

/**
 * lrg_tutorial_set_condition_callback:
 * @self: A #LrgTutorial
 * @callback: (scope notified) (nullable): The callback function
 * @user_data: User data to pass to the callback
 * @destroy: (nullable): Destroy notify for user_data
 *
 * Sets the callback for checking conditions.
 *
 * Since: 1.0
 */
void
lrg_tutorial_set_condition_callback (LrgTutorial              *self,
                                     LrgTutorialConditionFunc  callback,
                                     gpointer                  user_data,
                                     GDestroyNotify            destroy)
{
    g_return_if_fail (LRG_IS_TUTORIAL (self));

    if (self->condition_destroy != NULL && self->condition_user_data != NULL)
    {
        self->condition_destroy (self->condition_user_data);
    }

    self->condition_callback = callback;
    self->condition_user_data = user_data;
    self->condition_destroy = destroy;
}

/**
 * lrg_tutorial_save_to_file:
 * @self: A #LrgTutorial
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the tutorial definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_save_to_file (LrgTutorial  *self,
                           const gchar  *path,
                           GError      **error)
{
    g_autoptr(YamlBuilder) builder = NULL;
    g_autoptr(YamlGenerator) generator = NULL;
    YamlDocument *doc;
    gchar *yaml_str;
    gboolean ret;
    guint i;

    g_return_val_if_fail (LRG_IS_TUTORIAL (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    builder = yaml_builder_new ();

    /* Build root mapping */
    yaml_builder_begin_mapping (builder);

    yaml_builder_set_member_name (builder, "id");
    yaml_builder_add_string_value (builder, self->id != NULL ? self->id : "");

    yaml_builder_set_member_name (builder, "name");
    yaml_builder_add_string_value (builder, self->name != NULL ? self->name : "");

    if (self->description != NULL)
    {
        yaml_builder_set_member_name (builder, "description");
        yaml_builder_add_string_value (builder, self->description);
    }

    yaml_builder_set_member_name (builder, "repeatable");
    yaml_builder_add_boolean_value (builder, self->repeatable);

    yaml_builder_set_member_name (builder, "skippable");
    yaml_builder_add_boolean_value (builder, self->skippable);

    /* Steps sequence */
    yaml_builder_set_member_name (builder, "steps");
    yaml_builder_begin_sequence (builder);

    for (i = 0; i < self->steps->len; i++)
    {
        LrgTutorialStep *step;
        LrgTutorialStepType step_type;
        const gchar *step_id;
        gfloat pos_x, pos_y;

        step = (LrgTutorialStep *) g_ptr_array_index (self->steps, i);
        step_type = lrg_tutorial_step_get_step_type (step);

        yaml_builder_begin_mapping (builder);

        /* Step ID */
        step_id = lrg_tutorial_step_get_id (step);
        if (step_id != NULL)
        {
            yaml_builder_set_member_name (builder, "id");
            yaml_builder_add_string_value (builder, step_id);
        }

        /* Type and type-specific data */
        yaml_builder_set_member_name (builder, "type");
        switch (step_type)
        {
        case LRG_TUTORIAL_STEP_TEXT:
            yaml_builder_add_string_value (builder, "text");

            if (lrg_tutorial_step_get_text (step) != NULL)
            {
                yaml_builder_set_member_name (builder, "text");
                yaml_builder_add_string_value (builder, lrg_tutorial_step_get_text (step));
            }

            if (lrg_tutorial_step_get_speaker (step) != NULL)
            {
                yaml_builder_set_member_name (builder, "speaker");
                yaml_builder_add_string_value (builder, lrg_tutorial_step_get_speaker (step));
            }
            break;

        case LRG_TUTORIAL_STEP_HIGHLIGHT:
            yaml_builder_add_string_value (builder, "highlight");

            if (lrg_tutorial_step_get_target_id (step) != NULL)
            {
                yaml_builder_set_member_name (builder, "target_id");
                yaml_builder_add_string_value (builder, lrg_tutorial_step_get_target_id (step));
            }

            yaml_builder_set_member_name (builder, "highlight_style");
            switch (lrg_tutorial_step_get_highlight_style (step))
            {
            case LRG_HIGHLIGHT_STYLE_GLOW:
                yaml_builder_add_string_value (builder, "glow");
                break;
            case LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS:
                yaml_builder_add_string_value (builder, "darken_others");
                break;
            case LRG_HIGHLIGHT_STYLE_SPOTLIGHT:
                yaml_builder_add_string_value (builder, "spotlight");
                break;
            default:
                yaml_builder_add_string_value (builder, "outline");
                break;
            }
            break;

        case LRG_TUTORIAL_STEP_INPUT:
            yaml_builder_add_string_value (builder, "input");

            if (lrg_tutorial_step_get_action_name (step) != NULL)
            {
                yaml_builder_set_member_name (builder, "action_name");
                yaml_builder_add_string_value (builder, lrg_tutorial_step_get_action_name (step));
            }

            yaml_builder_set_member_name (builder, "show_prompt");
            yaml_builder_add_boolean_value (builder, lrg_tutorial_step_get_show_prompt (step));
            break;

        case LRG_TUTORIAL_STEP_CONDITION:
            yaml_builder_add_string_value (builder, "condition");

            if (lrg_tutorial_step_get_condition_id (step) != NULL)
            {
                yaml_builder_set_member_name (builder, "condition_id");
                yaml_builder_add_string_value (builder, lrg_tutorial_step_get_condition_id (step));
            }
            break;

        case LRG_TUTORIAL_STEP_DELAY:
            yaml_builder_add_string_value (builder, "delay");

            yaml_builder_set_member_name (builder, "duration");
            yaml_builder_add_double_value (builder, (gdouble) lrg_tutorial_step_get_duration (step));
            break;
        }

        /* Common properties */
        yaml_builder_set_member_name (builder, "can_skip");
        yaml_builder_add_boolean_value (builder, lrg_tutorial_step_get_can_skip (step));

        yaml_builder_set_member_name (builder, "blocks_input");
        yaml_builder_add_boolean_value (builder, lrg_tutorial_step_get_blocks_input (step));

        yaml_builder_set_member_name (builder, "auto_advance");
        yaml_builder_add_boolean_value (builder, lrg_tutorial_step_get_auto_advance (step));

        lrg_tutorial_step_get_position (step, &pos_x, &pos_y);
        yaml_builder_set_member_name (builder, "position_x");
        yaml_builder_add_double_value (builder, (gdouble) pos_x);

        yaml_builder_set_member_name (builder, "position_y");
        yaml_builder_add_double_value (builder, (gdouble) pos_y);

        /* Arrow direction */
        yaml_builder_set_member_name (builder, "arrow_direction");
        switch (lrg_tutorial_step_get_arrow_direction (step))
        {
        case LRG_ARROW_DIRECTION_UP:
            yaml_builder_add_string_value (builder, "up");
            break;
        case LRG_ARROW_DIRECTION_DOWN:
            yaml_builder_add_string_value (builder, "down");
            break;
        case LRG_ARROW_DIRECTION_LEFT:
            yaml_builder_add_string_value (builder, "left");
            break;
        case LRG_ARROW_DIRECTION_RIGHT:
            yaml_builder_add_string_value (builder, "right");
            break;
        default:
            yaml_builder_add_string_value (builder, "auto");
            break;
        }

        yaml_builder_end_mapping (builder);  /* step */
    }

    yaml_builder_end_sequence (builder);  /* steps */
    yaml_builder_end_mapping (builder);  /* root */

    doc = yaml_builder_get_document (builder);
    if (doc == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Failed to build YAML document for tutorial");
        return FALSE;
    }

    generator = yaml_generator_new ();
    yaml_generator_set_document (generator, doc);
    yaml_str = yaml_generator_to_data (generator, NULL, error);

    if (yaml_str == NULL)
        return FALSE;

    ret = g_file_set_contents (path, yaml_str, -1, error);
    g_free (yaml_str);

    return ret;
}
