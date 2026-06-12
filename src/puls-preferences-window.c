/*
 * puls-preferences-window.c
 *
 * Preferences window implementation for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-preferences-window.h"
#include "puls-settings.h"

struct _PulsPreferencesWindow {
    AdwPreferencesWindow parent_instance;

    AdwComboRow *theme_row;
    AdwComboRow *polling_row;
    GtkWidget   *fahrenheit_switch;
    GtkWidget   *caution_spin;

    PulsSettings *settings;
    gulong        changed_handler_id;
    gboolean      updating_widgets;
};

G_DEFINE_TYPE (PulsPreferencesWindow, puls_preferences_window, ADW_TYPE_PREFERENCES_WINDOW)

static void
on_theme_changed (GObject *object G_GNUC_UNUSED, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
    PulsPreferencesWindow *self = PULS_PREFERENCES_WINDOW (user_data);
    if (self->updating_widgets)
        return;

    guint selected = adw_combo_row_get_selected (self->theme_row);
    puls_settings_set_theme_preference (self->settings, (gint)selected);

    AdwStyleManager *sm = adw_style_manager_get_default ();
    if (selected == 1)
        adw_style_manager_set_color_scheme (sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
    else if (selected == 2)
        adw_style_manager_set_color_scheme (sm, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme (sm, ADW_COLOR_SCHEME_DEFAULT);
}

static void
on_polling_changed (GObject *object G_GNUC_UNUSED, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
    PulsPreferencesWindow *self = PULS_PREFERENCES_WINDOW (user_data);
    if (self->updating_widgets)
        return;

    guint selected = adw_combo_row_get_selected (self->polling_row);
    gint seconds = 60;
    switch (selected) {
    case 0: seconds = 0;   break;
    case 1: seconds = 10;  break;
    case 2: seconds = 30;  break;
    case 3: seconds = 60;  break;
    case 4: seconds = 300; break;
    }

    puls_settings_set_polling_interval (self->settings, seconds);
}

static void
on_fahrenheit_changed (GObject *object G_GNUC_UNUSED, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
    PulsPreferencesWindow *self = PULS_PREFERENCES_WINDOW (user_data);
    if (self->updating_widgets)
        return;

    gboolean active = gtk_switch_get_active (GTK_SWITCH (self->fahrenheit_switch));
    puls_settings_set_use_fahrenheit (self->settings, active);
}

static void
on_caution_changed (GtkSpinButton *spin, gpointer user_data)
{
    PulsPreferencesWindow *self = PULS_PREFERENCES_WINDOW (user_data);
    if (self->updating_widgets)
        return;

    gdouble val = gtk_spin_button_get_value (spin);
    puls_settings_set_caution_temp (self->settings, (gint)val);
}

static void
update_widgets (PulsPreferencesWindow *self)
{
    self->updating_widgets = TRUE;

    gint theme = puls_settings_get_theme_preference (self->settings);
    adw_combo_row_set_selected (self->theme_row, (guint)theme);

    gint seconds = puls_settings_get_polling_interval (self->settings);
    guint poll_idx = 3;
    switch (seconds) {
    case 0:   poll_idx = 0; break;
    case 10:  poll_idx = 1; break;
    case 30:  poll_idx = 2; break;
    case 60:  poll_idx = 3; break;
    case 300: poll_idx = 4; break;
    }
    adw_combo_row_set_selected (self->polling_row, poll_idx);

    gboolean fahr = puls_settings_get_use_fahrenheit (self->settings);
    gtk_switch_set_active (GTK_SWITCH (self->fahrenheit_switch), fahr);

    gint caution = puls_settings_get_caution_temp (self->settings);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->caution_spin), (gdouble)caution);

    self->updating_widgets = FALSE;
}

static void
on_settings_changed (PulsSettings *settings G_GNUC_UNUSED, gpointer user_data)
{
    PulsPreferencesWindow *self = PULS_PREFERENCES_WINDOW (user_data);
    update_widgets (self);
}

static void
puls_preferences_window_dispose (GObject *object)
{
    PulsPreferencesWindow *self = PULS_PREFERENCES_WINDOW (object);

    if (self->changed_handler_id > 0) {
        g_signal_handler_disconnect (self->settings, self->changed_handler_id);
        self->changed_handler_id = 0;
    }

    G_OBJECT_CLASS (puls_preferences_window_parent_class)->dispose (object);
}

static void
puls_preferences_window_class_init (PulsPreferencesWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = puls_preferences_window_dispose;
}

static void
puls_preferences_window_init (PulsPreferencesWindow *self)
{
    self->settings = puls_settings_get_default ();
    self->updating_widgets = FALSE;

    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
    adw_preferences_window_add (ADW_PREFERENCES_WINDOW (self), page);

    AdwPreferencesGroup *general_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
    adw_preferences_group_set_title (general_group, "General Settings");
    adw_preferences_page_add (page, general_group);

    self->theme_row = ADW_COMBO_ROW (adw_combo_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->theme_row), "Interface Theme");
    adw_preferences_group_add (general_group, GTK_WIDGET (self->theme_row));

    g_auto(GStrv) themes = g_new0 (gchar*, 4);
    themes[0] = g_strdup ("System Default");
    themes[1] = g_strdup ("Always Light");
    themes[2] = g_strdup ("Always Dark");
    themes[3] = NULL;
    g_autoptr(GtkStringList) theme_list = gtk_string_list_new ((const gchar * const *)themes);
    adw_combo_row_set_model (self->theme_row, G_LIST_MODEL (theme_list));
    g_signal_connect (self->theme_row, "notify::selected", G_CALLBACK (on_theme_changed), self);

    self->polling_row = ADW_COMBO_ROW (adw_combo_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->polling_row), "Auto Refresh Rate");
    adw_preferences_group_add (general_group, GTK_WIDGET (self->polling_row));

    g_auto(GStrv) intervals = g_new0 (gchar*, 6);
    intervals[0] = g_strdup ("Disabled (Manual)");
    intervals[1] = g_strdup ("10 seconds");
    intervals[2] = g_strdup ("30 seconds");
    intervals[3] = g_strdup ("1 minute");
    intervals[4] = g_strdup ("5 minutes");
    intervals[5] = NULL;
    g_autoptr(GtkStringList) interval_list = gtk_string_list_new ((const gchar * const *)intervals);
    adw_combo_row_set_model (self->polling_row, G_LIST_MODEL (interval_list));
    g_signal_connect (self->polling_row, "notify::selected", G_CALLBACK (on_polling_changed), self);

    AdwPreferencesGroup *temp_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
    adw_preferences_group_set_title (temp_group, "Temperature &amp; Thresholds");
    adw_preferences_page_add (page, temp_group);

    AdwActionRow *temp_row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (temp_row), "Use Fahrenheit");
    adw_action_row_set_subtitle (temp_row, "Display drive temperature in Fahrenheit (°F)");
    adw_preferences_group_add (temp_group, GTK_WIDGET (temp_row));

    self->fahrenheit_switch = gtk_switch_new ();
    gtk_widget_set_valign (self->fahrenheit_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix (temp_row, self->fahrenheit_switch);
    g_signal_connect (self->fahrenheit_switch, "notify::active", G_CALLBACK (on_fahrenheit_changed), self);

    AdwActionRow *caution_row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (caution_row), "Caution Threshold (°C)");
    adw_action_row_set_subtitle (caution_row, "Trigger caution warning above this temperature");
    adw_preferences_group_add (temp_group, GTK_WIDGET (caution_row));

    GtkAdjustment *adj = gtk_adjustment_new (60.0, 30.0, 90.0, 1.0, 5.0, 0.0);
    self->caution_spin = gtk_spin_button_new (adj, 1.0, 0);
    gtk_widget_set_valign (self->caution_spin, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix (caution_row, self->caution_spin);
    g_signal_connect (self->caution_spin, "value-changed", G_CALLBACK (on_caution_changed), self);

    update_widgets (self);

    self->changed_handler_id = g_signal_connect (self->settings, "changed", G_CALLBACK (on_settings_changed), self);
}

GtkWidget *
puls_preferences_window_new (GtkWindow *parent)
{
    return g_object_new (PULS_TYPE_PREFERENCES_WINDOW,
                         "transient-for", parent,
                         "modal", TRUE,
                         "title", "Preferences",
                         "default-width", 450,
                         "default-height", 400,
                         NULL);
}
