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
#include "puls-alert-manager.h"
#include "puls-i18n.h"
#include "puls-disk-info-view.h"

#ifndef PULS_VERSION
#define PULS_VERSION "1.1.2"
#endif

struct _PulsWindow {
    AdwApplicationWindow parent_instance;

    GtkWidget *header_bar;
    GtkWidget *refresh_button;
    GtkWidget *lang_button;
    GtkWidget *menu_button;

    GtkWidget *toast_overlay;
    GtkWidget *main_box;
    GtkWidget *disk_selector;
    GtkWidget *info_stack;
    GtkWidget *status_bar;
    GtkWidget *status_label;

    GtkWidget *no_disks_page;
    GtkWidget *empty_label;
    GtkWidget *empty_detail;

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
static void puls_window_apply_lang (PulsWindow *self);

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

static gchar *
generate_report_html (PulsWindow *self)
{
    GString *html = g_string_new ("");
    g_string_append (html,
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n<head><meta charset=\"UTF-8\">\n"
        "<title>PULS DiskInfo Report</title>\n"
        "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:2rem}\n"
        "h1{color:#7fc8f8}h2{color:#aef;border-bottom:1px solid #444;padding-bottom:.3rem}\n"
        "table{border-collapse:collapse;width:100%;margin-bottom:1.5rem}td,th{padding:.4rem .8rem;text-align:left}\n"
        "th{background:#1e2a38;color:#7fc8f8}tr:nth-child(even){background:#1a1a2e}\n"
        ".ok{color:#4ade80}.warn{color:#fbbf24}.bad{color:#f87171}\n"
        "</style>\n</head>\n<body>\n"
        "<h1>PULS DiskInfo v" PULS_VERSION " Report</h1>\n");

    GDateTime *now = g_date_time_new_now_local ();
    g_autofree gchar *ts = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");
    g_date_time_unref (now);
    g_string_append_printf (html, "<p>Generated: %s</p>\n", ts);

    GList *devices = puls_disk_manager_get_devices (self->manager);
    for (GList *l = devices; l != NULL; l = l->next) {
        const gchar *path = l->data;
        PulsSmartData *data = puls_disk_manager_get_smart_data (self->manager, path);
        if (!data) continue;

        const gchar *model  = puls_smart_data_get_model_name (data);
        const gchar *serial = puls_smart_data_get_serial_number (data);
        const gchar *fw     = puls_smart_data_get_firmware_version (data);
        const gchar *iface  = puls_smart_data_get_interface_type (data);
        PulsHealthStatus hs = puls_smart_data_get_health (data);
        gint temp           = puls_smart_data_get_temperature (data);
        gint health_pct     = puls_smart_data_get_health_percent (data);
        gint lifetime_days  = puls_smart_data_get_estimated_lifetime_days (data);
        g_autofree gchar *cap_str = puls_format_bytes_exact (puls_smart_data_get_capacity_bytes (data));

        const gchar *status_class = "ok";
        const gchar *status_text  = puls_health_status_to_string (hs);
        if (hs == PULS_HEALTH_CAUTION) status_class = "warn";
        else if (hs == PULS_HEALTH_BAD) status_class = "bad";

        g_string_append_printf (html, "<h2>%s (%s)</h2>\n",
                                model ? model : "Unknown Drive", path);
        g_string_append (html, "<table><tr><th>Field</th><th>Value</th></tr>\n");

        g_string_append_printf (html, "<tr><td>Serial Number</td><td>%s</td></tr>\n",
                                serial ? serial : "—");
        g_string_append_printf (html, "<tr><td>Firmware</td><td>%s</td></tr>\n",
                                fw ? fw : "—");
        g_string_append_printf (html, "<tr><td>Interface</td><td>%s</td></tr>\n",
                                iface ? iface : "—");
        g_string_append_printf (html, "<tr><td>Capacity</td><td>%s</td></tr>\n", cap_str);
        g_string_append_printf (html,
            "<tr><td>Health</td><td class=\"%s\">%s",
            status_class, status_text);
        if (health_pct >= 0)
            g_string_append_printf (html, " (%d%%)", health_pct);
        g_string_append (html, "</td></tr>\n");

        if (lifetime_days >= 0) {
            if (lifetime_days >= 365)
                g_string_append_printf (html,
                    "<tr><td>Est. Lifetime Remaining</td><td>%.1f yr</td></tr>\n",
                    (double)lifetime_days / 365.25);
            else
                g_string_append_printf (html,
                    "<tr><td>Est. Lifetime Remaining</td><td>%d day%s</td></tr>\n",
                    lifetime_days, lifetime_days == 1 ? "" : "s");
        }

        if (temp >= 0) {
            g_autofree gchar *ts2 = g_strdup_printf ("%d °C", temp);
            g_string_append_printf (html, "<tr><td>Temperature</td><td>%s</td></tr>\n", ts2);
        } else {
            g_string_append (html, "<tr><td>Temperature</td><td>N/A</td></tr>\n");
        }
        g_string_append_printf (html,
            "<tr><td>Power On Hours</td><td>%" G_GUINT64_FORMAT "</td></tr>\n",
            puls_smart_data_get_power_on_hours (data));
        g_string_append_printf (html,
            "<tr><td>Power Cycles</td><td>%" G_GUINT64_FORMAT "</td></tr>\n",
            puls_smart_data_get_power_cycle_count (data));

        g_autofree gchar *wear_str = NULL;
        PulsNvmeHealth *nvme = puls_smart_data_get_nvme_health (data);
        if (nvme) {
            wear_str = g_strdup_printf ("%d%% remaining life (%d%% used)", 100 - nvme->percentage_used, nvme->percentage_used);
        } else {
            GArray *attrs = puls_smart_data_get_ata_attributes (data);
            if (attrs) {
                for (guint i = 0; i < attrs->len; i++) {
                    PulsSmartAttribute *attr = &g_array_index (attrs, PulsSmartAttribute, i);
                    if (attr->id == 231 || attr->id == 202 || attr->id == 233) {
                        wear_str = g_strdup_printf ("%d%% remaining life (%d%% used)", attr->current, 100 - attr->current);
                        break;
                    }
                }
            }
        }
        if (wear_str) {
            g_string_append_printf (html, "<tr><td>Wear Level</td><td>%s</td></tr>\n", wear_str);
        }

        guint64 unsafe_shutdowns = 0;
        gboolean has_unsafe = FALSE;
        if (nvme) {
            unsafe_shutdowns = nvme->unsafe_shutdowns;
            has_unsafe = TRUE;
        } else {
            GArray *attrs = puls_smart_data_get_ata_attributes (data);
            if (attrs) {
                for (guint i = 0; i < attrs->len; i++) {
                    PulsSmartAttribute *attr = &g_array_index (attrs, PulsSmartAttribute, i);
                    if (attr->id == 174 || attr->id == 192) {
                        unsafe_shutdowns = attr->raw_value;
                        has_unsafe = TRUE;
                        break;
                    }
                }
            }
        }
        if (has_unsafe) {
            g_string_append_printf (html, "<tr><td>Unsafe Shutdowns</td><td>%" G_GUINT64_FORMAT "</td></tr>\n", unsafe_shutdowns);
        }

        guint32 bufsz = puls_smart_data_get_buffer_size_kb (data);
        if (bufsz > 0) {
            g_string_append_printf (html, "<tr><td>Buffer Size</td><td>%u KB</td></tr>\n", bufsz);
        }

        gint apm = puls_smart_data_get_apm_level (data);
        if (apm >= 0) {
            g_string_append_printf (html, "<tr><td>APM Level</td><td>%d</td></tr>\n", apm);
        }

        gint aam = puls_smart_data_get_aam_level (data);
        if (aam >= 0) {
            g_string_append_printf (html, "<tr><td>AAM Level</td><td>%d</td></tr>\n", aam);
        }

        gint spin = puls_smart_data_get_spin_up_time_ms (data);
        if (spin >= 0) {
            g_string_append_printf (html, "<tr><td>Spin-Up Time</td><td>%d ms</td></tr>\n", spin);
        }

        guint64 errs = puls_smart_data_get_error_count_total (data);
        g_string_append_printf (html, "<tr><td>Error Count</td><td>%" G_GUINT64_FORMAT "</td></tr>\n", errs);

        GList *parts = puls_get_disk_partitions (path);
        if (parts) {
            g_string_append (html, "<tr><td>Partition Usage</td><td>");
            for (GList *pl = parts; pl != NULL; pl = pl->next) {
                PulsPartitionInfo *pinfo = pl->data;
                if (pinfo->total_bytes > 0) {
                    guint64 used = pinfo->total_bytes - pinfo->available_bytes;
                    double pct = (double)used / pinfo->total_bytes * 100.0;
                    g_autofree gchar *u_str = puls_format_bytes (used);
                    g_autofree gchar *t_str = puls_format_bytes (pinfo->total_bytes);
                    g_string_append_printf (html, "<div>%s mounted at %s (%s): %s / %s (%.1f%%)</div>",
                        pinfo->device_path, pinfo->mount_point, pinfo->fs_type, u_str, t_str, pct);
                } else {
                    g_string_append_printf (html, "<div>%s mounted at %s (%s)</div>",
                        pinfo->device_path, pinfo->mount_point, pinfo->fs_type);
                }
            }
            g_string_append (html, "</td></tr>\n");
            g_list_free_full (parts, (GDestroyNotify)puls_partition_info_free);
        }

        g_string_append (html, "</table>\n");

        GArray *attrs = puls_smart_data_get_ata_attributes (data);
        if (attrs && attrs->len > 0) {
            g_string_append (html,
                "<h3>S.M.A.R.T. Attributes</h3>\n"
                "<table><tr><th>ID</th><th>Name</th><th>Current</th>"
                "<th>Worst</th><th>Threshold</th><th>Raw</th><th>Status</th></tr>\n");
            for (guint i = 0; i < attrs->len; i++) {
                PulsSmartAttribute *a = &g_array_index (attrs, PulsSmartAttribute, i);
                const gchar *row_class = a->failing_now ? "bad" : (a->failed_past ? "warn" : "ok");
                g_string_append_printf (html,
                    "<tr><td>0x%02x</td><td>%s</td><td class=\"%s\">%d</td>"
                    "<td>%d</td><td>%d</td><td>%s</td><td class=\"%s\">%s</td></tr>\n",
                    a->id, a->name ? a->name : "—",
                    row_class, a->current, a->worst, a->threshold,
                    a->raw_string ? a->raw_string : "0",
                    row_class, a->failing_now ? "FAIL" : (a->failed_past ? "Past" : "OK"));
            }
            g_string_append (html, "</table>\n");
        }
    }
    g_list_free (devices);

    g_string_append (html, "</body>\n</html>\n");
    return g_string_free (html, FALSE);
}

static void
on_save_report_response (GtkNativeDialog *dialog,
                         gint             response_id,
                         gpointer         user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
        if (file) {
            g_autofree gchar *path = g_file_get_path (file);
            if (path) {
                GError *error = NULL;
                gchar *report_html = generate_report_html (self);
                if (g_file_set_contents (path, report_html, -1, &error)) {
                    adw_toast_overlay_add_toast (
                        ADW_TOAST_OVERLAY (self->toast_overlay),
                        adw_toast_new (_(PULS_STR_REPORT_SAVED_OK)));
                } else {
                    g_autofree gchar *err_msg = g_strdup_printf (_(PULS_STR_REPORT_SAVE_FAILED), error->message);
                    adw_toast_overlay_add_toast (
                        ADW_TOAST_OVERLAY (self->toast_overlay),
                        adw_toast_new (err_msg));
                    g_clear_error (&error);
                }
                g_free (report_html);
            }
            g_object_unref (file);
        }
    }
    g_object_unref (dialog);
}

static void
action_export_report (GSimpleAction *action G_GNUC_UNUSED,
                      GVariant      *parameter G_GNUC_UNUSED,
                      gpointer       user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);

    GtkFileChooserNative *native = gtk_file_chooser_native_new (
        _(PULS_STR_REPORT_SAVE_TITLE),
        GTK_WINDOW (self),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _(PULS_STR_REPORT_SAVE_BTN),
        _(PULS_STR_REPORT_CANCEL_BTN)
    );

    gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (native), "puls-diskinfo-report.html");

    GtkFileFilter *filter = gtk_file_filter_new ();
    gtk_file_filter_add_pattern (filter, "*.html");
    gtk_file_filter_set_name (filter, "HTML Files (*.html)");
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), filter);

    g_signal_connect (native, "response", G_CALLBACK (on_save_report_response), self);
    gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}


static void
action_view_alert_log (GSimpleAction *action G_GNUC_UNUSED,
                       GVariant      *parameter G_GNUC_UNUSED,
                       gpointer       user_data)
{
    PulsWindow *self = PULS_WINDOW (user_data);
    PulsAlertManager *alert_mgr = puls_alert_manager_get_default ();
    GPtrArray *log = puls_alert_manager_get_log (alert_mgr);

    GString *msg = g_string_new ("");
    if (log == NULL || log->len == 0) {
        g_string_append (msg, _(PULS_STR_ALERT_LOG_EMPTY));
    } else {
        for (guint i = 0; i < log->len; i++) {
            PulsAlertEntry *e = g_ptr_array_index (log, i);
            g_autofree gchar *ts_str = g_date_time_format (e->timestamp, "%H:%M:%S");
            g_string_append_printf (msg, "[%s] %s: %s\n",
                                    ts_str,
                                    e->device_path ? e->device_path : "?",
                                    e->message ? e->message : "");
        }
    }

    GtkWidget *dialog = gtk_message_dialog_new (
        GTK_WINDOW (self),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s", _(PULS_STR_ALERT_LOG_TITLE)
    );
    gtk_message_dialog_format_secondary_text (
        GTK_MESSAGE_DIALOG (dialog), "%s", msg->str);
    g_signal_connect (dialog, "response",
                      G_CALLBACK (gtk_window_destroy), NULL);
    gtk_window_present (GTK_WINDOW (dialog));
    g_string_free (msg, TRUE);
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
    g_menu_append (menu, _(PULS_STR_MENU_REFRESH_ALL),     "win.refresh");
    g_menu_append (menu, _(PULS_STR_MENU_EXPORT_REPORT),   "win.export-report");
    g_menu_append (menu, _(PULS_STR_MENU_ALERT_LOG),       "win.alert-log");
    g_menu_append (menu, _(PULS_STR_MENU_PREFERENCES),     "win.preferences");
    g_menu_append (menu, _(PULS_STR_MENU_ABOUT),           "win.about");
    g_menu_append (menu, _(PULS_STR_MENU_QUIT),            "win.quit");
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

    g_autofree gchar *status = g_strdup_printf (_(PULS_STR_STATUS_LAST_REFRESHED), time_str);
    gtk_label_set_text (GTK_LABEL (self->status_label), status);

    gtk_widget_set_sensitive (self->refresh_button, TRUE);

    if (data) {
        const gchar *dev_path = puls_smart_data_get_device_path (data);
        if (dev_path) {
            GtkWidget *view = g_hash_table_lookup (self->info_views, dev_path);
            if (view)
                puls_disk_info_view_set_data (PULS_DISK_INFO_VIEW (view), data);

            PulsAlertManager *alert_mgr = puls_alert_manager_get_default ();
            GPtrArray *new_alerts = puls_alert_manager_check (alert_mgr, data);
            if (new_alerts && new_alerts->len > 0) {
                for (guint i = 0; i < new_alerts->len; i++) {
                    PulsAlertEntry *e = g_ptr_array_index (new_alerts, i);
                    adw_toast_overlay_add_toast (
                        ADW_TOAST_OVERLAY (self->toast_overlay),
                        adw_toast_new (e->message ? e->message : "Drive alert!"));
                }
                g_ptr_array_free (new_alerts, FALSE);
            }
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
    gtk_label_set_text (GTK_LABEL (self->status_label), _(PULS_STR_STATUS_REFRESHING));

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
on_lang_button_clicked (GtkButton *btn G_GNUC_UNUSED, PulsWindow *self)
{
    PulsLang new_lang = (puls_i18n_get_lang () == PULS_LANG_EN) ? PULS_LANG_TR : PULS_LANG_EN;
    puls_i18n_set_lang (new_lang);
    puls_settings_set_language (self->settings, (gint)new_lang);
    puls_window_apply_lang (self);
}

static void
puls_window_apply_lang (PulsWindow *self)
{
    gtk_button_set_label (GTK_BUTTON (self->lang_button), _(PULS_STR_LANG_BUTTON_LABEL));
    gtk_widget_set_tooltip_text (self->refresh_button, _(PULS_STR_REFRESH_TOOLTIP));

    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->menu_button),
                                    create_app_menu ());

    gtk_label_set_text (GTK_LABEL (self->empty_label),  _(PULS_STR_SCANNING_FOR_DISKS));
    gtk_label_set_text (GTK_LABEL (self->empty_detail), _(PULS_STR_SMARTMONTOOLS_HINT));

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init (&iter, self->info_views);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        puls_disk_info_view_apply_lang (PULS_DISK_INFO_VIEW (value));
    }
}

static void
puls_window_init (PulsWindow *self)
{
    self->info_views = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, NULL);
    self->refresh_timer_id = 0;
    self->settings = puls_settings_get_default ();

    gint saved_lang = puls_settings_get_language (self->settings);
    puls_i18n_set_lang (saved_lang == 1 ? PULS_LANG_TR : PULS_LANG_EN);

    gtk_window_set_title (GTK_WINDOW (self), "PULS DiskInfo");
    gtk_window_set_default_size (GTK_WINDOW (self), 920, 720);

    const GActionEntry actions[] = {
        { "about",         action_about,           NULL, NULL, NULL, {0, 0, 0} },
        { "refresh",       action_refresh,         NULL, NULL, NULL, {0, 0, 0} },
        { "preferences",   action_preferences,     NULL, NULL, NULL, {0, 0, 0} },
        { "export-report", action_export_report,   NULL, NULL, NULL, {0, 0, 0} },
        { "alert-log",     action_view_alert_log,  NULL, NULL, NULL, {0, 0, 0} },
        { "quit",          action_quit,            NULL, NULL, NULL, {0, 0, 0} },
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
    gtk_widget_set_tooltip_text (self->refresh_button, _(PULS_STR_REFRESH_TOOLTIP));
    gtk_widget_add_css_class (self->refresh_button, "flat");
    g_signal_connect (self->refresh_button, "clicked",
                      G_CALLBACK (on_refresh_clicked), self);
    adw_header_bar_pack_start (ADW_HEADER_BAR (self->header_bar),
                               self->refresh_button);

    self->lang_button = gtk_button_new_with_label (_(PULS_STR_LANG_BUTTON_LABEL));
    gtk_widget_add_css_class (self->lang_button, "flat");
    gtk_widget_add_css_class (self->lang_button, "lang-toggle");
    gtk_widget_set_tooltip_text (self->lang_button, "Switch language / Dil değiştir");
    g_signal_connect (self->lang_button, "clicked",
                      G_CALLBACK (on_lang_button_clicked), self);
    adw_header_bar_pack_start (ADW_HEADER_BAR (self->header_bar),
                               self->lang_button);

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

    self->empty_label = gtk_label_new (_(PULS_STR_SCANNING_FOR_DISKS));
    gtk_widget_add_css_class (self->empty_label, "dim-label");
    gtk_widget_add_css_class (self->empty_label, "title-2");
    gtk_box_append (GTK_BOX (self->no_disks_page), self->empty_label);

    self->empty_detail = gtk_label_new (_(PULS_STR_SMARTMONTOOLS_HINT));
    gtk_widget_add_css_class (self->empty_detail, "dim-label");
    gtk_box_append (GTK_BOX (self->no_disks_page), self->empty_detail);

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

    self->status_label = gtk_label_new (_(PULS_STR_STATUS_STARTING));
    gtk_widget_add_css_class (self->status_label, "status-text");
    gtk_widget_set_hexpand (self->status_label, TRUE);
    gtk_label_set_xalign (GTK_LABEL (self->status_label), 0.0);
    gtk_box_append (GTK_BOX (self->status_bar), self->status_label);

    puls_disk_manager_scan (self->manager);

    update_timer (self);
    self->settings_changed_id = g_signal_connect (self->settings, "changed",
                                                   G_CALLBACK (on_settings_changed), self);

    guint count = puls_disk_manager_get_device_count (self->manager);
    const gchar *fmt = (count == 1) ? _(PULS_STR_STATUS_FOUND_DISK) : _(PULS_STR_STATUS_FOUND_DISKS);
    g_autofree gchar *init_status = g_strdup_printf (fmt, count);
    gtk_label_set_text (GTK_LABEL (self->status_label), init_status);
}

PulsWindow *
puls_window_new (PulsApplication *app)
{
    return g_object_new (PULS_TYPE_WINDOW,
                         "application", app,
                         NULL);
}

