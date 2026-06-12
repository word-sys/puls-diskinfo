/*
 * puls-settings.c
 *
 * Settings management implementation. Uses JSON file storage in ~/.config.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-settings.h"
#include <json-glib/json-glib.h>
#include <stdlib.h>
#include <string.h>

struct _PulsSettings {
    GObject parent_instance;

    gint     polling_interval;
    gboolean use_fahrenheit;
    gint     caution_temp;
    gint     theme_preference;
};

G_DEFINE_TYPE (PulsSettings, puls_settings, G_TYPE_OBJECT)

static PulsSettings *default_settings = NULL;
static guint settings_changed_signal = 0;

static void
puls_settings_finalize (GObject *object)
{
    G_OBJECT_CLASS (puls_settings_parent_class)->finalize (object);
}

static void
puls_settings_class_init (PulsSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = puls_settings_finalize;

    settings_changed_signal =
        g_signal_new ("changed",
                       G_TYPE_FROM_CLASS (klass),
                       G_SIGNAL_RUN_LAST,
                       0, NULL, NULL, NULL,
                       G_TYPE_NONE, 0);
}

static gchar *
get_config_path (void)
{
    const gchar *config_dir = g_get_user_config_dir ();
    return g_build_filename (config_dir, "puls-diskinfo", "settings.json", NULL);
}

static void
puls_settings_load (PulsSettings *self)
{
    g_autofree gchar *path = get_config_path ();
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
        return;

    g_autoptr(JsonParser) parser = json_parser_new ();
    GError *error = NULL;

    if (!json_parser_load_from_file (parser, path, &error)) {
        g_warning ("Failed to load settings: %s", error->message);
        g_clear_error (&error);
        return;
    }

    JsonNode *root_node = json_parser_get_root (parser);
    if (root_node && JSON_NODE_HOLDS_OBJECT (root_node)) {
        JsonObject *root = json_node_get_object (root_node);

        if (json_object_has_member (root, "polling_interval"))
            self->polling_interval = json_object_get_int_member (root, "polling_interval");

        if (json_object_has_member (root, "use_fahrenheit"))
            self->use_fahrenheit = json_object_get_boolean_member (root, "use_fahrenheit");

        if (json_object_has_member (root, "caution_temp"))
            self->caution_temp = json_object_get_int_member (root, "caution_temp");

        if (json_object_has_member (root, "theme_preference"))
            self->theme_preference = json_object_get_int_member (root, "theme_preference");
    }
}

static void
puls_settings_init (PulsSettings *self)
{
    self->polling_interval = 60;
    self->use_fahrenheit = FALSE;
    self->caution_temp = 60;
    self->theme_preference = 0;

    puls_settings_load (self);
}

PulsSettings *
puls_settings_get_default (void)
{
    if (default_settings == NULL) {
        default_settings = g_object_new (PULS_TYPE_SETTINGS, NULL);
        g_object_add_weak_pointer (G_OBJECT (default_settings), (gpointer *)&default_settings);
    }
    return default_settings;
}

gint
puls_settings_get_polling_interval (PulsSettings *self)
{
    g_return_val_if_fail (PULS_IS_SETTINGS (self), 60);
    return self->polling_interval;
}

void
puls_settings_set_polling_interval (PulsSettings *self, gint val)
{
    g_return_if_fail (PULS_IS_SETTINGS (self));
    if (self->polling_interval != val) {
        self->polling_interval = val;
        puls_settings_save (self);
        g_signal_emit (self, settings_changed_signal, 0);
    }
}

gboolean
puls_settings_get_use_fahrenheit (PulsSettings *self)
{
    g_return_val_if_fail (PULS_IS_SETTINGS (self), FALSE);
    return self->use_fahrenheit;
}

void
puls_settings_set_use_fahrenheit (PulsSettings *self, gboolean val)
{
    g_return_if_fail (PULS_IS_SETTINGS (self));
    if (self->use_fahrenheit != val) {
        self->use_fahrenheit = val;
        puls_settings_save (self);
        g_signal_emit (self, settings_changed_signal, 0);
    }
}

gint
puls_settings_get_caution_temp (PulsSettings *self)
{
    g_return_val_if_fail (PULS_IS_SETTINGS (self), 60);
    return self->caution_temp;
}

void
puls_settings_set_caution_temp (PulsSettings *self, gint val)
{
    g_return_if_fail (PULS_IS_SETTINGS (self));
    if (self->caution_temp != val) {
        self->caution_temp = val;
        puls_settings_save (self);
        g_signal_emit (self, settings_changed_signal, 0);
    }
}

gint
puls_settings_get_theme_preference (PulsSettings *self)
{
    g_return_val_if_fail (PULS_IS_SETTINGS (self), 0);
    return self->theme_preference;
}

void
puls_settings_set_theme_preference (PulsSettings *self, gint val)
{
    g_return_if_fail (PULS_IS_SETTINGS (self));
    if (self->theme_preference != val) {
        self->theme_preference = val;
        puls_settings_save (self);
        g_signal_emit (self, settings_changed_signal, 0);
    }
}

void
puls_settings_save (PulsSettings *self)
{
    g_return_if_fail (PULS_IS_SETTINGS (self));

    g_autofree gchar *path = get_config_path ();
    g_autofree gchar *dir = g_path_get_dirname (path);

    g_mkdir_with_parents (dir, 0700);

    g_autoptr(JsonBuilder) builder = json_builder_new ();
    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "polling_interval");
    json_builder_add_int_value (builder, self->polling_interval);

    json_builder_set_member_name (builder, "use_fahrenheit");
    json_builder_add_boolean_value (builder, self->use_fahrenheit);

    json_builder_set_member_name (builder, "caution_temp");
    json_builder_add_int_value (builder, self->caution_temp);

    json_builder_set_member_name (builder, "theme_preference");
    json_builder_add_int_value (builder, self->theme_preference);

    json_builder_end_object (builder);

    g_autoptr(JsonGenerator) gen = json_generator_new ();
    g_autoptr(JsonNode) node = json_builder_get_root (builder);
    json_generator_set_root (gen, node);
    json_generator_set_pretty (gen, TRUE);

    GError *error = NULL;
    if (!json_generator_to_file (gen, path, &error)) {
        g_warning ("Failed to save settings: %s", error->message);
        g_clear_error (&error);
    }
}
