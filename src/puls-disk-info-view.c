/*
 * puls-disk-info-view.c
 *
 * Per-disk detail panel — shows all disk information sections.
 *
 * Copyright (C) 2024 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-disk-info-view.h"
#include "puls-health-indicator.h"
#include "puls-temperature-widget.h"
#include "puls-smart-table.h"
#include "puls-settings.h"
#include "puls-utils.h"

struct _PulsDiskInfoView {
    GtkWidget parent_instance;

    GtkWidget *scrolled;
    GtkWidget *content_box;

    GtkWidget *identity_frame;
    GtkWidget *model_label;
    GtkWidget *serial_label;
    GtkWidget *firmware_label;
    GtkWidget *interface_label;
    GtkWidget *capacity_label;
    GtkWidget *rotation_label;
    GtkWidget *features_label;

    GtkWidget *health_frame;
    GtkWidget *health_indicator;
    GtkWidget *temperature_widget;
    GtkWidget *power_hours_label;
    GtkWidget *power_cycles_label;
    GtkWidget *total_written_label;
    GtkWidget *total_read_label;

    GtkWidget *partitions_frame;
    GtkWidget *partitions_box;

    GtkWidget *diag_frame;
    GtkWidget *diag_box;
    GtkWidget *short_test_btn;
    GtkWidget *long_test_btn;
    GtkWidget *abort_test_btn;
    GtkWidget *test_status_label;
    GtkWidget *test_progress_bar;

    GtkWidget *smart_frame;
    GtkWidget *smart_table;

    GtkWidget *nvme_frame;
    GtkWidget *nvme_grid;
    GtkWidget *nvme_labels[14];

    gchar     *current_device;
};

G_DEFINE_TYPE (PulsDiskInfoView, puls_disk_info_view, GTK_TYPE_WIDGET)

static void
add_info_row_to_grid (GtkWidget   *grid,
                      gint         col_left,
                      gint         row,
                      const gchar *label_text,
                      GtkWidget  **val_label_out)
{
    GtkWidget *label = gtk_label_new (label_text);
    gtk_widget_add_css_class (label, "info-key");
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, col_left, row, 1, 1);

    GtkWidget *value = gtk_label_new ("—");
    gtk_widget_add_css_class (value, "info-value");
    gtk_label_set_xalign (GTK_LABEL (value), 0.0);
    gtk_label_set_selectable (GTK_LABEL (value), TRUE);
    gtk_label_set_ellipsize (GTK_LABEL (value), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand (value, TRUE);
    gtk_grid_attach (GTK_GRID (grid), value, col_left + 1, row, 1, 1);

    if (val_label_out)
        *val_label_out = value;
}

static GtkWidget *
create_info_row (GtkWidget   *grid,
                 gint         row,
                 const gchar *label_text)
{
    GtkWidget *val = NULL;
    add_info_row_to_grid (grid, 0, row, label_text, &val);
    return val;
}

static GtkWidget *
create_section_frame (const gchar *title)
{
    GtkWidget *frame = gtk_frame_new (NULL);
    gtk_widget_add_css_class (frame, "card");
    gtk_widget_set_margin_bottom (frame, 12);

    GtkWidget *title_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title_label = gtk_label_new (title);
    gtk_widget_add_css_class (title_label, "section-title");
    gtk_widget_set_margin_start (title_label, 16);
    gtk_widget_set_margin_top (title_label, 12);
    gtk_label_set_xalign (GTK_LABEL (title_label), 0.0);
    gtk_box_append (GTK_BOX (title_box), title_label);

    gtk_frame_set_label_widget (GTK_FRAME (frame), title_box);

    return frame;
}

static void
on_action_done (GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
    PulsDiskInfoView *self = PULS_DISK_INFO_VIEW (user_data);
    GError *error = NULL;
    g_autofree gchar *output = puls_run_smartctl_action_finish (result, &error);

    if (error) {
        g_warning ("SMART test action failed: %s", error->message);
        gtk_label_set_text (GTK_LABEL (self->test_status_label), error->message);
        g_clear_error (&error);
    } else {
        gtk_label_set_text (GTK_LABEL (self->test_status_label), "Test command accepted. Refresh to view updates.");
    }
}

static void
on_short_test_clicked (GtkButton *btn G_GNUC_UNUSED, PulsDiskInfoView *self)
{
    if (self->current_device == NULL)
        return;
    gtk_label_set_text (GTK_LABEL (self->test_status_label), "Starting Short Self-Test...");
    puls_run_smartctl_action_async (self->current_device, "short", NULL, on_action_done, self);
}

static void
on_long_test_clicked (GtkButton *btn G_GNUC_UNUSED, PulsDiskInfoView *self)
{
    if (self->current_device == NULL)
        return;
    gtk_label_set_text (GTK_LABEL (self->test_status_label), "Starting Long Self-Test...");
    puls_run_smartctl_action_async (self->current_device, "long", NULL, on_action_done, self);
}

static void
on_abort_test_clicked (GtkButton *btn G_GNUC_UNUSED, PulsDiskInfoView *self)
{
    if (self->current_device == NULL)
        return;
    gtk_label_set_text (GTK_LABEL (self->test_status_label), "Aborting active test...");
    puls_run_smartctl_action_async (self->current_device, "abort", NULL, on_action_done, self);
}

static void
puls_disk_info_view_finalize (GObject *object)
{
    PulsDiskInfoView *self = PULS_DISK_INFO_VIEW (object);
    g_free (self->current_device);
    G_OBJECT_CLASS (puls_disk_info_view_parent_class)->finalize (object);
}

static void
puls_disk_info_view_dispose (GObject *object)
{
    PulsDiskInfoView *self = PULS_DISK_INFO_VIEW (object);
    g_clear_pointer (&self->scrolled, gtk_widget_unparent);
    G_OBJECT_CLASS (puls_disk_info_view_parent_class)->dispose (object);
}

static void
puls_disk_info_view_class_init (PulsDiskInfoViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = puls_disk_info_view_dispose;
    object_class->finalize = puls_disk_info_view_finalize;

    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name (widget_class, "disk-info-view");
}

static void
puls_disk_info_view_init (PulsDiskInfoView *self)
{
    self->current_device = NULL;

    self->scrolled = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_parent (self->scrolled, GTK_WIDGET (self));

    self->content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class (self->content_box, "info-content");
    gtk_widget_set_margin_start (self->content_box, 16);
    gtk_widget_set_margin_end (self->content_box, 16);
    gtk_widget_set_margin_top (self->content_box, 12);
    gtk_widget_set_margin_bottom (self->content_box, 12);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scrolled),
                                  self->content_box);

    GtkWidget *top_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append (GTK_BOX (self->content_box), top_row);

    self->health_frame = create_section_frame ("Health & Temp");
    gtk_widget_set_size_request (self->health_frame, 220, -1);
    gtk_box_append (GTK_BOX (top_row), self->health_frame);

    GtkWidget *health_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start (health_box, 12);
    gtk_widget_set_margin_end (health_box, 12);
    gtk_widget_set_margin_top (health_box, 8);
    gtk_widget_set_margin_bottom (health_box, 8);
    gtk_frame_set_child (GTK_FRAME (self->health_frame), health_box);

    self->health_indicator = puls_health_indicator_new ();
    gtk_widget_set_size_request (self->health_indicator, -1, 70);
    gtk_box_append (GTK_BOX (health_box), self->health_indicator);

    self->temperature_widget = puls_temperature_widget_new ();
    gtk_box_append (GTK_BOX (health_box), self->temperature_widget);

    self->identity_frame = create_section_frame ("Drive Information");
    gtk_widget_set_hexpand (self->identity_frame, TRUE);
    gtk_box_append (GTK_BOX (top_row), self->identity_frame);

    GtkWidget *id_grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (id_grid), 6);
    gtk_grid_set_column_spacing (GTK_GRID (id_grid), 16);
    gtk_widget_set_margin_start (id_grid, 12);
    gtk_widget_set_margin_end (id_grid, 12);
    gtk_widget_set_margin_top (id_grid, 8);
    gtk_widget_set_margin_bottom (id_grid, 8);
    gtk_frame_set_child (GTK_FRAME (self->identity_frame), id_grid);

    GtkWidget *model_key = gtk_label_new ("Model:");
    gtk_widget_add_css_class (model_key, "info-key");
    gtk_label_set_xalign (GTK_LABEL (model_key), 0.0);
    gtk_grid_attach (GTK_GRID (id_grid), model_key, 0, 0, 1, 1);

    self->model_label = gtk_label_new ("—");
    gtk_widget_add_css_class (self->model_label, "info-value");
    gtk_label_set_xalign (GTK_LABEL (self->model_label), 0.0);
    gtk_label_set_selectable (GTK_LABEL (self->model_label), TRUE);
    gtk_label_set_ellipsize (GTK_LABEL (self->model_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand (self->model_label, TRUE);
    gtk_grid_attach (GTK_GRID (id_grid), self->model_label, 1, 0, 3, 1);

    add_info_row_to_grid (id_grid, 0, 1, "Firmware:", &self->firmware_label);
    add_info_row_to_grid (id_grid, 2, 1, "Serial Number:", &self->serial_label);
    add_info_row_to_grid (id_grid, 0, 2, "Interface:", &self->interface_label);
    add_info_row_to_grid (id_grid, 2, 2, "Capacity:", &self->capacity_label);
    add_info_row_to_grid (id_grid, 0, 3, "Type:", &self->rotation_label);
    add_info_row_to_grid (id_grid, 2, 3, "Features:", &self->features_label);
    add_info_row_to_grid (id_grid, 0, 4, "Power On Hours:", &self->power_hours_label);
    add_info_row_to_grid (id_grid, 2, 4, "Power Cycles:", &self->power_cycles_label);
    add_info_row_to_grid (id_grid, 0, 5, "Total Reads:", &self->total_read_label);
    add_info_row_to_grid (id_grid, 2, 5, "Total Writes:", &self->total_written_label);

    self->partitions_frame = create_section_frame ("Mount Points & Partitions");
    gtk_box_append (GTK_BOX (self->content_box), self->partitions_frame);

    self->partitions_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start (self->partitions_box, 16);
    gtk_widget_set_margin_end (self->partitions_box, 16);
    gtk_widget_set_margin_top (self->partitions_box, 12);
    gtk_widget_set_margin_bottom (self->partitions_box, 12);
    gtk_frame_set_child (GTK_FRAME (self->partitions_frame), self->partitions_box);

    self->diag_frame = create_section_frame ("Diagnostics & Self-Tests");
    gtk_box_append (GTK_BOX (self->content_box), self->diag_frame);

    self->diag_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (self->diag_box, 16);
    gtk_widget_set_margin_end (self->diag_box, 16);
    gtk_widget_set_margin_top (self->diag_box, 12);
    gtk_widget_set_margin_bottom (self->diag_box, 12);
    gtk_frame_set_child (GTK_FRAME (self->diag_frame), self->diag_box);

    GtkWidget *btn_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (self->diag_box), btn_row);

    self->short_test_btn = gtk_button_new_with_label ("Run Short Test");
    gtk_widget_add_css_class (self->short_test_btn, "suggested-action");
    g_signal_connect (self->short_test_btn, "clicked", G_CALLBACK (on_short_test_clicked), self);
    gtk_box_append (GTK_BOX (btn_row), self->short_test_btn);

    self->long_test_btn = gtk_button_new_with_label ("Run Long Test");
    g_signal_connect (self->long_test_btn, "clicked", G_CALLBACK (on_long_test_clicked), self);
    gtk_box_append (GTK_BOX (btn_row), self->long_test_btn);

    self->abort_test_btn = gtk_button_new_with_label ("Abort Active Test");
    gtk_widget_add_css_class (self->abort_test_btn, "destructive-action");
    g_signal_connect (self->abort_test_btn, "clicked", G_CALLBACK (on_abort_test_clicked), self);
    gtk_box_append (GTK_BOX (btn_row), self->abort_test_btn);

    self->test_status_label = gtk_label_new ("No diagnostic test currently running.");
    gtk_widget_add_css_class (self->test_status_label, "dim-label");
    gtk_label_set_xalign (GTK_LABEL (self->test_status_label), 0.0);
    gtk_box_append (GTK_BOX (self->diag_box), self->test_status_label);

    self->test_progress_bar = gtk_progress_bar_new ();
    gtk_widget_set_visible (self->test_progress_bar, FALSE);
    gtk_box_append (GTK_BOX (self->diag_box), self->test_progress_bar);

    self->smart_frame = create_section_frame ("S.M.A.R.T. Attributes");
    gtk_box_append (GTK_BOX (self->content_box), self->smart_frame);

    GtkWidget *smart_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start (smart_box, 8);
    gtk_widget_set_margin_end (smart_box, 8);
    gtk_widget_set_margin_top (smart_box, 8);
    gtk_widget_set_margin_bottom (smart_box, 8);
    gtk_frame_set_child (GTK_FRAME (self->smart_frame), smart_box);

    self->smart_table = puls_smart_table_new ();
    gtk_widget_set_vexpand (self->smart_table, FALSE);
    gtk_box_append (GTK_BOX (smart_box), self->smart_table);

    self->nvme_frame = create_section_frame ("NVMe Health Information");
    gtk_box_append (GTK_BOX (self->content_box), self->nvme_frame);
    gtk_widget_set_visible (self->nvme_frame, FALSE);

    self->nvme_grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (self->nvme_grid), 6);
    gtk_grid_set_column_spacing (GTK_GRID (self->nvme_grid), 16);
    gtk_widget_set_margin_start (self->nvme_grid, 16);
    gtk_widget_set_margin_end (self->nvme_grid, 16);
    gtk_widget_set_margin_top (self->nvme_grid, 12);
    gtk_widget_set_margin_bottom (self->nvme_grid, 12);
    gtk_frame_set_child (GTK_FRAME (self->nvme_frame), self->nvme_grid);

    const gchar *nvme_fields[] = {
        "Critical Warning:",
        "Available Spare:",
        "Available Spare Threshold:",
        "Percentage Used:",
        "Data Units Read:",
        "Data Units Written:",
        "Host Read Commands:",
        "Host Write Commands:",
        "Controller Busy Time:",
        "Power Cycles:",
        "Power On Hours:",
        "Unsafe Shutdowns:",
        "Media Errors:",
        "Error Log Entries:"
    };

    for (gint i = 0; i < 14; i++) {
        self->nvme_labels[i] = create_info_row (self->nvme_grid, i, nvme_fields[i]);
    }
}

GtkWidget *
puls_disk_info_view_new (void)
{
    return g_object_new (PULS_TYPE_DISK_INFO_VIEW, NULL);
}

void
puls_disk_info_view_set_data (PulsDiskInfoView *self,
                              PulsSmartData    *data)
{
    g_return_if_fail (PULS_IS_DISK_INFO_VIEW (self));

    if (data == NULL)
        return;

    g_free (self->current_device);
    self->current_device = g_strdup (puls_smart_data_get_device_path (data));

    const gchar *model = puls_smart_data_get_model_name (data);
    gtk_label_set_text (GTK_LABEL (self->model_label),
                        model ? model : "Unknown");

    const gchar *serial = puls_smart_data_get_serial_number (data);
    gtk_label_set_text (GTK_LABEL (self->serial_label),
                        serial ? serial : "N/A");

    const gchar *fw = puls_smart_data_get_firmware_version (data);
    gtk_label_set_text (GTK_LABEL (self->firmware_label),
                        fw ? fw : "N/A");

    const gchar *iface = puls_smart_data_get_interface_type (data);
    gtk_label_set_text (GTK_LABEL (self->interface_label),
                        iface ? iface : "N/A");

    guint64 cap = puls_smart_data_get_capacity_bytes (data);
    if (cap > 0) {
        g_autofree gchar *cap_str = puls_format_bytes_exact (cap);
        gtk_label_set_text (GTK_LABEL (self->capacity_label), cap_str);
    } else {
        gtk_label_set_text (GTK_LABEL (self->capacity_label), "N/A");
    }

    gint rpm = puls_smart_data_get_rotation_rpm (data);
    PulsDriveType dtype = puls_smart_data_get_drive_type (data);
    g_autofree gchar *type_str = NULL;
    if (rpm == 0)
        type_str = g_strdup_printf ("%s (SSD)", puls_drive_type_to_string (dtype));
    else if (rpm > 0)
        type_str = g_strdup_printf ("%s (%d RPM)", puls_drive_type_to_string (dtype), rpm);
    else
        type_str = g_strdup (puls_drive_type_to_string (dtype));
    gtk_label_set_text (GTK_LABEL (self->rotation_label), type_str);

    gboolean trim = puls_smart_data_get_supports_trim (data);
    gboolean ncq  = puls_smart_data_get_supports_ncq (data);
    gboolean apm  = puls_smart_data_get_supports_apm (data);
    g_autofree gchar *features = g_strdup_printf ("TRIM %s   NCQ %s   APM %s",
        trim ? "✓" : "✗", ncq ? "✓" : "✗", apm ? "✓" : "✗");
    gtk_label_set_text (GTK_LABEL (self->features_label), features);

    PulsHealthStatus health = puls_smart_data_get_health (data);
    gint temp = puls_smart_data_get_temperature (data);

    PulsSettings *settings = puls_settings_get_default ();
    gint caution_limit = puls_settings_get_caution_temp (settings);
    if (temp >= caution_limit && health == PULS_HEALTH_GOOD) {
        health = PULS_HEALTH_CAUTION;
    }

    puls_health_indicator_set_status (
        PULS_HEALTH_INDICATOR (self->health_indicator), health);

    puls_temperature_widget_set_temperature (
        PULS_TEMPERATURE_WIDGET (self->temperature_widget), temp);

    guint64 hours = puls_smart_data_get_power_on_hours (data);
    if (hours > 0) {
        g_autofree gchar *hours_str = puls_format_hours (hours);
        g_autofree gchar *num_str = puls_format_number (hours);
        g_autofree gchar *final = g_strdup_printf ("%s (%s h)", hours_str, num_str);
        gtk_label_set_text (GTK_LABEL (self->power_hours_label), final);
    } else {
        gtk_label_set_text (GTK_LABEL (self->power_hours_label), "N/A");
    }

    guint64 cycles = puls_smart_data_get_power_cycle_count (data);
    if (cycles > 0) {
        g_autofree gchar *cycles_str = puls_format_number (cycles);
        gtk_label_set_text (GTK_LABEL (self->power_cycles_label), cycles_str);
    } else {
        gtk_label_set_text (GTK_LABEL (self->power_cycles_label), "N/A");
    }

    guint64 written = puls_smart_data_get_total_bytes_written (data);
    if (written > 0) {
        g_autofree gchar *w_str = puls_format_bytes (written);
        gtk_label_set_text (GTK_LABEL (self->total_written_label), w_str);
    } else {
        gtk_label_set_text (GTK_LABEL (self->total_written_label), "N/A");
    }

    guint64 read_bytes = puls_smart_data_get_total_bytes_read (data);
    if (read_bytes > 0) {
        g_autofree gchar *r_str = puls_format_bytes (read_bytes);
        gtk_label_set_text (GTK_LABEL (self->total_read_label), r_str);
    } else {
        gtk_label_set_text (GTK_LABEL (self->total_read_label), "N/A");
    }

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child (self->partitions_box)) != NULL) {
        gtk_box_remove (GTK_BOX (self->partitions_box), child);
    }

    GList *parts = puls_get_disk_partitions (self->current_device);
    if (parts == NULL) {
        GtkWidget *no_parts = gtk_label_new ("No active mount points found for this disk.");
        gtk_widget_add_css_class (no_parts, "dim-label");
        gtk_label_set_xalign (GTK_LABEL (no_parts), 0.0);
        gtk_box_append (GTK_BOX (self->partitions_box), no_parts);
    } else {
        for (GList *l = parts; l != NULL; l = l->next) {
            PulsPartitionInfo *pinfo = l->data;
            GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

            GtkWidget *dev_lbl = gtk_label_new (pinfo->device_path);
            gtk_widget_add_css_class (dev_lbl, "bold");
            gtk_box_append (GTK_BOX (row), dev_lbl);

            g_autofree gchar *mp_str = g_strdup_printf ("mounted at %s (%s)", pinfo->mount_point, pinfo->fs_type);
            GtkWidget *mp_lbl = gtk_label_new (mp_str);
            gtk_widget_add_css_class (mp_lbl, "dim-label");
            gtk_widget_set_hexpand (mp_lbl, TRUE);
            gtk_label_set_xalign (GTK_LABEL (mp_lbl), 0.0);
            gtk_box_append (GTK_BOX (row), mp_lbl);

            if (pinfo->total_bytes > 0) {
                guint64 used = pinfo->total_bytes - pinfo->available_bytes;
                gdouble pct = (gdouble)used / pinfo->total_bytes;

                g_autofree gchar *used_str = puls_format_bytes (used);
                g_autofree gchar *total_str = puls_format_bytes (pinfo->total_bytes);
                g_autofree gchar *usage_str = g_strdup_printf ("%s / %s (%.1f%%)", used_str, total_str, pct * 100.0);

                GtkWidget *use_lbl = gtk_label_new (usage_str);
                gtk_box_append (GTK_BOX (row), use_lbl);

                GtkWidget *pb = gtk_progress_bar_new ();
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pb), pct);
                gtk_widget_set_size_request (pb, 80, -1);
                gtk_widget_set_valign (pb, GTK_ALIGN_CENTER);
                gtk_box_append (GTK_BOX (row), pb);
            } else {
                GtkWidget *use_lbl = gtk_label_new ("Unknown size");
                gtk_box_append (GTK_BOX (row), use_lbl);
            }

            gtk_box_append (GTK_BOX (self->partitions_box), row);
        }
        g_list_free_full (parts, (GDestroyNotify)puls_partition_info_free);
    }

    gboolean testing = puls_smart_data_get_self_test_in_progress (data);
    if (testing) {
        gint test_pct = puls_smart_data_get_self_test_percent (data);
        const gchar *status_txt = puls_smart_data_get_self_test_status_str (data);
        const gchar *type_txt = puls_smart_data_get_self_test_type_str (data);

        g_autofree gchar *stat_msg = g_strdup_printf ("%s in progress: %s (%d%% completed)",
            type_txt ? type_txt : "Self-Test",
            status_txt ? status_txt : "running",
            test_pct);
        gtk_label_set_text (GTK_LABEL (self->test_status_label), stat_msg);

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->test_progress_bar), (gdouble)test_pct / 100.0);
        gtk_widget_set_visible (self->test_progress_bar, TRUE);

        gtk_widget_set_sensitive (self->short_test_btn, FALSE);
        gtk_widget_set_sensitive (self->long_test_btn, FALSE);
        gtk_widget_set_sensitive (self->abort_test_btn, TRUE);
    } else {
        gtk_label_set_text (GTK_LABEL (self->test_status_label), "No diagnostic test currently running.");
        gtk_widget_set_visible (self->test_progress_bar, FALSE);

        gtk_widget_set_sensitive (self->short_test_btn, TRUE);
        gtk_widget_set_sensitive (self->long_test_btn, TRUE);
        gtk_widget_set_sensitive (self->abort_test_btn, FALSE);
    }

    puls_smart_table_set_data (PULS_SMART_TABLE (self->smart_table), data);

    PulsNvmeHealth *nvme = puls_smart_data_get_nvme_health (data);
    if (nvme) {
        gtk_widget_set_visible (self->nvme_frame, TRUE);

        g_autofree gchar *cw = g_strdup_printf ("0x%02X%s", nvme->critical_warning,
            nvme->critical_warning == 0 ? " (None)" : " (WARNING)");
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[0]), cw);

        g_autofree gchar *as = g_strdup_printf ("%d%%", nvme->available_spare);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[1]), as);

        g_autofree gchar *ast = g_strdup_printf ("%d%%", nvme->available_spare_threshold);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[2]), ast);

        g_autofree gchar *pu = g_strdup_printf ("%d%%", nvme->percentage_used);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[3]), pu);

        g_autofree gchar *dur = puls_format_number (nvme->data_units_read);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[4]), dur);

        g_autofree gchar *duw = puls_format_number (nvme->data_units_written);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[5]), duw);

        g_autofree gchar *hrc = puls_format_number (nvme->host_read_commands);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[6]), hrc);

        g_autofree gchar *hwc = puls_format_number (nvme->host_write_commands);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[7]), hwc);

        g_autofree gchar *cbt_num = puls_format_number (nvme->controller_busy_time);
        g_autofree gchar *cbt_full = g_strdup_printf ("%s min", cbt_num);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[8]), cbt_full);

        g_autofree gchar *pc = puls_format_number (nvme->power_cycles);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[9]), pc);

        g_autofree gchar *poh_str = puls_format_hours (nvme->power_on_hours);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[10]), poh_str);

        g_autofree gchar *us = puls_format_number (nvme->unsafe_shutdowns);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[11]), us);

        g_autofree gchar *me = puls_format_number (nvme->media_errors);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[12]), me);

        g_autofree gchar *ele = puls_format_number (nvme->error_log_entries);
        gtk_label_set_text (GTK_LABEL (self->nvme_labels[13]), ele);
    } else {
        gtk_widget_set_visible (self->nvme_frame, FALSE);
    }
}
