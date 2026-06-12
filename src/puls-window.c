/*
 * puls-window.c
 *
 * Main application window.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-window.h"
#include "puls-disk-manager.h"
#include "puls-disk-selector.h"
#include "puls-disk-info-view.h"
#include "puls-about-dialog.h"
#include "puls-preferences-window.h"
#include "puls-settings.h"
#include "puls-utils.h"

#ifndef PULS_VERSION
#define PULS_VERSION "1.0.0"
#endif

struct _PulsWindow {
    AdwApplicationWindow parent_instance;

    GtkWidget *header_bar;
    GtkWidget *refresh_button;
    GtkWidget *menu_button;

    GtkWidget *toast_overlay;
    GtkWidget *main_box;
    GtkWidget *disk_selector;
    GtkWidget *info_stack;
    GtkWidget *status_bar;
    GtkWidget *status_label;

    GtkWidget *no_disks_page;

    PulsDiskManager *manager;
    PulsSettings    *settings;

    guint refresh_timer_id;
    gulong settings_changed_id;

    GHashTable *info_views;
};

G_DEFINE_TYPE (PulsWindow, puls_window, ADW_TYPE_APPLICATION_WINDOW)

static void on_disk_selected  (PulsDiskSelector *selector G_GNUC_UNUSED,
                               const gchar      *device_path,
                               PulsWindow       *self);
static void on_disk_added     (PulsDiskManager *manager,
                               const gchar     *device_path,
                               PulsWindow      *self);
static void on_refresh_clicked (GtkButton *button G_GNUC_UNUSED, PulsWindow *self);
static void refresh_current_disk (PulsWindow *self);
static void update_timer (PulsWindow *self);

static void
action_about (GSimpleAction *action G_GNUC_UNUSED,
              GVariant      *parameter G_GNUC_UNUSED,
              gpointer       user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    puls_show_about_dialog (GTK_WINDOW (self));
}

static void
action_refresh (GSimpleAction *action G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    refresh_current_disk (self);
}

static void
action_preferences (GSimpleAction *action G_GNUC_UNUSED,
                    GVariant      *parameter G_GNUC_UNUSED,
                    gpointer       user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    GtkWidget *pref_window = puls_preferences_window_new (GTK_WINDOW (self));
    gtk_window_present (GTK_WINDOW (pref_window));
}

static void
action_export_report (GSimpleAction *action G_GNUC_UNUSED,
                      GVariant      *parameter G_GNUC_UNUSED,
                      gpointer       user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    GString *report = g_string_new ("");
    g_string_append (report, "=========================================\n");
    g_string_append (report, "          PULS DISKINFO REPORT           \n");
    g_string_append (report, "=========================================\n\n");

    GList *devices = puls_disk_manager_get_devices (self->manager);
    for (GList *l = devices; l != NULL; l = l->next) {
        const gchar *path = l->data;
        PulsSmartData *data = puls_disk_manager_get_smart_data (self->manager, path);
        if (!data) continue;

        g_string_append_printf (report, "Drive Path:      %s\n", path);
        g_string_append_printf (report, "Model Name:      %s\n", puls_smart_data_get_model_name (data) ? puls_smart_data_get_model_name (data) : "Unknown");
        g_string_append_printf (report, "Serial Number:   %s\n", puls_smart_data_get_serial_number (data) ? puls_smart_data_get_serial_number (data) : "Unknown");
        g_string_append_printf (report, "Firmware:        %s\n", puls_smart_data_get_firmware_version (data) ? puls_smart_data_get_firmware_version (data) : "Unknown");
        g_string_append_printf (report, "Interface:       %s\n", puls_smart_data_get_interface_type (data) ? puls_smart_data_get_interface_type (data) : "Unknown");

        g_autofree gchar *cap_str = puls_format_bytes_exact (puls_smart_data_get_capacity_bytes (data));
        g_string_append_printf (report, "Capacity:        %s\n", cap_str);
        g_string_append_printf (report, "Power On Hours:  %" G_GUINT64_FORMAT " hours\n", puls_smart_data_get_power_on_hours (data));
        g_string_append_printf (report, "Power Cycles:    %" G_GUINT64_FORMAT " cycles\n", puls_smart_data_get_power_cycle_count (data));
        
        gint temp = puls_smart_data_get_temperature (data);
        if (temp >= 0)
            g_string_append_printf (report, "Temperature:     %d °C\n", temp);
        else
            g_string_append (report, "Temperature:     N/A\n");

        g_string_append_printf (report, "Health Status:   %s\n", puls_health_status_to_string (puls_smart_data_get_health (data)));
        g_string_append (report, "\n-----------------------------------------\n\n");
    }

    g_autofree gchar *report_str = g_string_free (report, FALSE);
    GdkClipboard *clipboard = gtk_widget_get_clipboard (GTK_WIDGET (self));
    gdk_clipboard_set_text (clipboard, report_str);

    adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (self->toast_overlay),
                                 adw_toast_new ("Diagnostic report copied to clipboard."));
}

static void
action_quit (GSimpleAction *action G_GNUC_UNUSED,
             GVariant      *parameter G_GNUC_UNUSED,
             gpointer       user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    gtk_window_destroy (GTK_WINDOW (self));
}

static GMenuModel *
create_app_menu (void)
{
    GMenu *menu = g_menu_new ();
    g_menu_append (menu, "Refresh All", "win.refresh");
    g_menu_append (menu, "Export Report", "win.export-report");
    g_menu_append (menu, "Preferences", "win.preferences");
    g_menu_append (menu, "About PULS DiskInfo", "win.about");
    g_menu_append (menu, "Quit", "win.quit");
    return G_MENU_MODEL (menu);
}

static void
on_refresh_done (GObject      *source G_GNUC_UNUSED,
                 GAsyncResult *result,
                 gpointer      user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    GError *error = NULL;

    g_autoptr(PulsSmartData) data =
        puls_disk_manager_refresh_finish (self->manager, result, &error);

    if (error) {
        g_warning ("Refresh failed: %s", error->message);
        g_clear_error (&error);
    }

    GDateTime *now = g_date_time_new_now_local ();
    g_autofree gchar *time_str = g_date_time_format (now, "%H:%M:%S");
    g_date_time_unref (now);

    g_autofree gchar *status = g_strdup_printf ("Last refreshed: %s │ v%s",
                                                 time_str, PULS_VERSION);
    gtk_label_set_text (GTK_LABEL (self->status_label), status);

    gtk_widget_set_sensitive (self->refresh_button, TRUE);

    if (data) {
        const gchar *dev_path = puls_smart_data_get_device_path (data);
        if (dev_path) {
            GtkWidget *view = g_hash_table_lookup (self->info_views, dev_path);
            if (view)
                puls_disk_info_view_set_data (PULS_DISK_INFO_VIEW (view), data);
        }
    }

    puls_disk_selector_refresh (PULS_DISK_SELECTOR (self->disk_selector));
}

static void
refresh_current_disk (PulsWindow *self)
{
    const gchar *selected = puls_disk_selector_get_selected (
        PULS_DISK_SELECTOR (self->disk_selector));

    if (selected == NULL)
        return;

    gtk_widget_set_sensitive (self->refresh_button, FALSE);
    gtk_label_set_text (GTK_LABEL (self->status_label), "Refreshing…");

    puls_disk_manager_refresh_async (self->manager, selected, NULL,
                                     on_refresh_done, self);
}

static void
on_refresh_clicked (GtkButton *button G_GNUC_UNUSED, PulsWindow *self)
{
    refresh_current_disk (self);
}

static gboolean
on_refresh_timer (gpointer user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    refresh_current_disk (self);
    return G_SOURCE_CONTINUE;
}

static void
update_timer (PulsWindow *self)
{
    if (self->refresh_timer_id > 0) {
        g_source_remove (self->refresh_timer_id);
        self->refresh_timer_id = 0;
    }

    gint seconds = puls_settings_get_polling_interval (self->settings);
    if (seconds > 0) {
        self->refresh_timer_id = g_timeout_add_seconds (seconds, on_refresh_timer, self);
    }
}

static void
on_settings_changed (PulsSettings *settings G_GNUC_UNUSED, gpointer user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    update_timer (self);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init (&iter, self->info_views);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        PulsSmartData *data = puls_disk_manager_get_smart_data (self->manager, (const gchar *)key);
        if (data)
            puls_disk_info_view_set_data (PULS_DISK_INFO_VIEW (value), data);
    }
    puls_disk_selector_refresh (PULS_DISK_SELECTOR (self->disk_selector));
}

static void
on_disk_selected (PulsDiskSelector *selector G_GNUC_UNUSED,
                  const gchar      *device_path,
                  PulsWindow       *self)
{
    GtkWidget *view = g_hash_table_lookup (self->info_views, device_path);
    if (view) {
        gtk_stack_set_visible_child (GTK_STACK (self->info_stack), view);
    }

    refresh_current_disk (self);
}

static void
on_disk_added (PulsDiskManager *manager,
               const gchar     *device_path,
               PulsWindow      *self)
{
    GtkWidget *view = puls_disk_info_view_new ();
    gtk_stack_add_named (GTK_STACK (self->info_stack), view, device_path);

    g_hash_table_insert (self->info_views, g_strdup (device_path), view);

    PulsSmartData *data = puls_disk_manager_get_smart_data (manager, device_path);
    if (data)
        puls_disk_info_view_set_data (PULS_DISK_INFO_VIEW (view), data);

    if (g_hash_table_size (self->info_views) == 1) {
        puls_disk_selector_select (PULS_DISK_SELECTOR (self->disk_selector),
                                   device_path);
        gtk_stack_set_visible_child (GTK_STACK (self->info_stack), view);
    }
}

static void
puls_window_dispose (GObject *object)
{
    PulsWindow *self = PULS_WINDOW (object);

    if (self->refresh_timer_id > 0) {
        g_source_remove (self->refresh_timer_id);
        self->refresh_timer_id = 0;
    }

    if (self->settings_changed_id > 0) {
        g_signal_handler_disconnect (self->settings, self->settings_changed_id);
        self->settings_changed_id = 0;
    }

    g_clear_object (&self->manager);
    g_clear_pointer (&self->info_views, g_hash_table_destroy);

    G_OBJECT_CLASS (puls_window_parent_class)->dispose (object);
}

static void
puls_window_class_init (PulsWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = puls_window_dispose;
}

static void
puls_window_init (PulsWindow *self)
{
    self->info_views = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, NULL);
    self->refresh_timer_id = 0;
    self->settings = puls_settings_get_default ();

    gtk_window_set_title (GTK_WINDOW (self), "PULS DiskInfo");
    gtk_window_set_default_size (GTK_WINDOW (self), 920, 720);

    const GActionEntry actions[] = {
        { "about",         action_about,         NULL, NULL, NULL, {0, 0, 0} },
        { "refresh",       action_refresh,       NULL, NULL, NULL, {0, 0, 0} },
        { "preferences",   action_preferences,   NULL, NULL, NULL, {0, 0, 0} },
        { "export-report", action_export_report, NULL, NULL, NULL, {0, 0, 0} },
        { "quit",          action_quit,          NULL, NULL, NULL, {0, 0, 0} },
    };
    g_action_map_add_action_entries (G_ACTION_MAP (self), actions,
                                    G_N_ELEMENTS (actions), self);

    self->toast_overlay = adw_toast_overlay_new ();
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), self->toast_overlay);

    self->main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    adw_toast_overlay_set_child (ADW_TOAST_OVERLAY (self->toast_overlay), self->main_box);

    self->header_bar = adw_header_bar_new ();
    gtk_box_append (GTK_BOX (self->main_box), self->header_bar);

    self->refresh_button = gtk_button_new_from_icon_name ("view-refresh-symbolic");
    gtk_widget_set_tooltip_text (self->refresh_button, "Refresh SMART data");
    gtk_widget_add_css_class (self->refresh_button, "flat");
    g_signal_connect (self->refresh_button, "clicked",
                      G_CALLBACK (on_refresh_clicked), self);
    adw_header_bar_pack_start (ADW_HEADER_BAR (self->header_bar),
                               self->refresh_button);

    self->menu_button = gtk_menu_button_new ();
    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (self->menu_button),
                                   "open-menu-symbolic");
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->menu_button),
                                    create_app_menu ());
    gtk_widget_add_css_class (self->menu_button, "flat");
    adw_header_bar_pack_end (ADW_HEADER_BAR (self->header_bar),
                             self->menu_button);

    self->manager = puls_disk_manager_new ();
    g_signal_connect (self->manager, "disk-added",
                      G_CALLBACK (on_disk_added), self);

    self->disk_selector = puls_disk_selector_new (self->manager);
    gtk_box_append (GTK_BOX (self->main_box), self->disk_selector);
    g_signal_connect (self->disk_selector, "disk-selected",
                      G_CALLBACK (on_disk_selected), self);

    GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_add_css_class (sep, "selector-separator");
    gtk_box_append (GTK_BOX (self->main_box), sep);

    self->info_stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (self->info_stack),
                                   GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (self->info_stack), 200);
    gtk_widget_set_vexpand (self->info_stack, TRUE);
    gtk_widget_set_hexpand (self->info_stack, TRUE);
    gtk_box_append (GTK_BOX (self->main_box), self->info_stack);

    self->no_disks_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign (self->no_disks_page, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (self->no_disks_page, GTK_ALIGN_CENTER);

    GtkWidget *empty_icon = gtk_image_new_from_icon_name ("drive-harddisk-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (empty_icon), 64);
    gtk_widget_add_css_class (empty_icon, "dim-label");
    gtk_box_append (GTK_BOX (self->no_disks_page), empty_icon);

    GtkWidget *empty_label = gtk_label_new ("Scanning for disks…");
    gtk_widget_add_css_class (empty_label, "dim-label");
    gtk_widget_add_css_class (empty_label, "title-2");
    gtk_box_append (GTK_BOX (self->no_disks_page), empty_label);

    GtkWidget *empty_detail = gtk_label_new (
        "Make sure smartmontools is installed and you have permission to read disk data.");
    gtk_widget_add_css_class (empty_detail, "dim-label");
    gtk_box_append (GTK_BOX (self->no_disks_page), empty_detail);

    gtk_stack_add_named (GTK_STACK (self->info_stack), self->no_disks_page,
                         "empty");
    gtk_stack_set_visible_child_name (GTK_STACK (self->info_stack), "empty");

    GtkWidget *status_sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append (GTK_BOX (self->main_box), status_sep);

    self->status_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class (self->status_bar, "status-bar");
    gtk_widget_set_margin_start (self->status_bar, 12);
    gtk_widget_set_margin_end (self->status_bar, 12);
    gtk_widget_set_margin_top (self->status_bar, 6);
    gtk_widget_set_margin_bottom (self->status_bar, 6);
    gtk_box_append (GTK_BOX (self->main_box), self->status_bar);

    self->status_label = gtk_label_new ("Starting…");
    gtk_widget_add_css_class (self->status_label, "status-text");
    gtk_widget_set_hexpand (self->status_label, TRUE);
    gtk_label_set_xalign (GTK_LABEL (self->status_label), 0.0);
    gtk_box_append (GTK_BOX (self->status_bar), self->status_label);

    g_autofree gchar *ver_label = g_strdup_printf ("v%s", PULS_VERSION);
    GtkWidget *version_label = gtk_label_new (ver_label);
    gtk_widget_add_css_class (version_label, "status-version");
    gtk_box_append (GTK_BOX (self->status_bar), version_label);

    puls_disk_manager_scan (self->manager);

    update_timer (self);
    self->settings_changed_id = g_signal_connect (self->settings, "changed",
                                                   G_CALLBACK (on_settings_changed), self);

    guint count = puls_disk_manager_get_device_count (self->manager);
    g_autofree gchar *init_status = g_strdup_printf (
        "Found %u disk%s │ v%s", count, count == 1 ? "" : "s", PULS_VERSION);
    gtk_label_set_text (GTK_LABEL (self->status_label), init_status);
}

PulsWindow *
puls_window_new (PulsApplication *app)
{
    return g_object_new (PULS_TYPE_WINDOW,
                         "application", app,
                         NULL);
}
