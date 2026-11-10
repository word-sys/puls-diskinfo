/*
 * main.c
 *
 * Entry point for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-application.h"
#include "puls-disk-manager.h"
#include "puls-mock-data.h"
#include "puls-smart-data.h"
#include "puls-utils.h"

#include <locale.h>
#include <adwaita.h>
#include <json-glib/json-glib.h>

#ifndef PULS_VERSION
#define PULS_VERSION "1.1.3"
#endif

static gboolean opt_version = FALSE;
static gboolean opt_headless = FALSE;
static gboolean opt_json = FALSE;
static gboolean opt_mock = FALSE;

static const GOptionEntry cli_entries[] = {
    { "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version, "Show application version", NULL },
    { "headless", 0, 0, G_OPTION_ARG_NONE, &opt_headless, "Run disk scan in headless terminal mode", NULL },
    { "json", 0, 0, G_OPTION_ARG_NONE, &opt_json, "Output scan result as JSON", NULL },
    { "mock-disk", 'm', 0, G_OPTION_ARG_NONE, &opt_mock, "Simulate mock storage devices", NULL },
    { NULL }
};

static int
run_headless_scan (gboolean json_output)
{
    PULS_LOG_DEBUG ("Initializing headless storage scan (mock=%d, json=%d)", opt_mock, json_output);

    g_autoptr(PulsDiskManager) manager = puls_disk_manager_new ();
    puls_disk_manager_scan (manager);

    GList *devices = puls_disk_manager_get_devices (manager);
    guint count = g_list_length (devices);

    PULS_LOG_INFO ("Scan completed: discovered %u storage device(s)", count);

    if (json_output) {
        g_autoptr(JsonBuilder) builder = json_builder_new ();
        json_builder_begin_object (builder);
        json_builder_set_member_name (builder, "version");
        json_builder_add_string_value (builder, PULS_VERSION);
        json_builder_set_member_name (builder, "device_count");
        json_builder_add_int_value (builder, (gint64)count);
        json_builder_set_member_name (builder, "devices");
        json_builder_begin_array (builder);

        for (GList *l = devices; l != NULL; l = l->next) {
            const gchar *path = l->data;
            PulsSmartData *data = puls_disk_manager_get_smart_data (manager, path);
            if (!data)
                continue;

            json_builder_begin_object (builder);
            json_builder_set_member_name (builder, "device_path");
            json_builder_add_string_value (builder, path);
            json_builder_set_member_name (builder, "model");
            json_builder_add_string_value (builder, puls_smart_data_get_model_name (data) ? puls_smart_data_get_model_name (data) : "Unknown");
            json_builder_set_member_name (builder, "serial");
            json_builder_add_string_value (builder, puls_smart_data_get_serial_number (data) ? puls_smart_data_get_serial_number (data) : "Unknown");
            json_builder_set_member_name (builder, "firmware");
            json_builder_add_string_value (builder, puls_smart_data_get_firmware_version (data) ? puls_smart_data_get_firmware_version (data) : "Unknown");
            json_builder_set_member_name (builder, "interface");
            json_builder_add_string_value (builder, puls_smart_data_get_interface_type (data) ? puls_smart_data_get_interface_type (data) : "Unknown");
            json_builder_set_member_name (builder, "transfer_mode");
            json_builder_add_string_value (builder, puls_smart_data_get_transfer_mode (data) ? puls_smart_data_get_transfer_mode (data) : "Unknown");
            json_builder_set_member_name (builder, "capacity_bytes");
            json_builder_add_int_value (builder, (gint64)puls_smart_data_get_capacity_bytes (data));
            json_builder_set_member_name (builder, "temperature_c");
            json_builder_add_int_value (builder, puls_smart_data_get_temperature (data));

            PulsHealthStatus hs = puls_smart_data_get_health (data);
            const gchar *hs_str = (hs == PULS_HEALTH_GOOD) ? "Good" :
                                  (hs == PULS_HEALTH_CAUTION) ? "Caution" :
                                  (hs == PULS_HEALTH_BAD) ? "Bad" : "Unknown";
            json_builder_set_member_name (builder, "health_status");
            json_builder_add_string_value (builder, hs_str);
            json_builder_set_member_name (builder, "health_percent");
            json_builder_add_int_value (builder, puls_smart_data_get_health_percent (data));
            json_builder_set_member_name (builder, "power_on_hours");
            json_builder_add_int_value (builder, (gint64)puls_smart_data_get_power_on_hours (data));
            json_builder_set_member_name (builder, "power_cycles");
            json_builder_add_int_value (builder, (gint64)puls_smart_data_get_power_cycle_count (data));
            json_builder_end_object (builder);
        }

        json_builder_end_array (builder);
        json_builder_end_object (builder);

        g_autoptr(JsonGenerator) gen = json_generator_new ();
        g_autoptr(JsonNode) root = json_builder_get_root (builder);
        json_generator_set_root (gen, root);
        json_generator_set_pretty (gen, TRUE);
        g_autofree gchar *json_text = json_generator_to_data (gen, NULL);
        g_print ("%s\n", json_text);
        return 0;
    }

    g_print ("PULS DiskInfo %s — Storage Health Report\n", PULS_VERSION);
    g_print ("Found %u storage device(s):\n\n", count);

    for (GList *l = devices; l != NULL; l = l->next) {
        const gchar *path = l->data;
        PulsSmartData *data = puls_disk_manager_get_smart_data (manager, path);
        if (!data)
            continue;

        PulsHealthStatus hs = puls_smart_data_get_health (data);
        const gchar *hs_str = (hs == PULS_HEALTH_GOOD) ? "Good" :
                              (hs == PULS_HEALTH_CAUTION) ? "Caution" :
                              (hs == PULS_HEALTH_BAD) ? "Bad" : "Unknown";
        gint temp = puls_smart_data_get_temperature (data);
        g_autofree gchar *temp_str = puls_format_temperature (temp);
        g_autofree gchar *cap_str = puls_format_bytes_exact (puls_smart_data_get_capacity_bytes (data));
        g_autofree gchar *hours_str = puls_format_hours (puls_smart_data_get_power_on_hours (data));

        g_print ("- Device:        %s\n", path);
        g_print ("  Model:         %s\n", puls_smart_data_get_model_name (data) ? puls_smart_data_get_model_name (data) : "Unknown");
        g_print ("  Serial:        %s\n", puls_smart_data_get_serial_number (data) ? puls_smart_data_get_serial_number (data) : "Unknown");
        g_print ("  Firmware:      %s\n", puls_smart_data_get_firmware_version (data) ? puls_smart_data_get_firmware_version (data) : "Unknown");
        const gchar *iface = puls_smart_data_get_interface_type (data);
        const gchar *tmode = puls_smart_data_get_transfer_mode (data);
        if (iface && iface[0] != '\0')
            g_print ("  Interface:     %s\n", iface);
        if (tmode && tmode[0] != '\0')
            g_print ("  Transfer Mode: %s\n", tmode);
        g_print ("  Capacity:      %s\n", cap_str);
        g_print ("  Health Status: %s (%d%%)\n", hs_str, puls_smart_data_get_health_percent (data));
        g_print ("  Temperature:   %s\n", temp_str);
        g_print ("  Power-On Time: %s\n", hours_str);
        g_print ("  Power Cycles:  %" G_GUINT64_FORMAT "\n\n", puls_smart_data_get_power_cycle_count (data));
    }

    return 0;
}

int
main (int argc, char *argv[])
{
    setlocale (LC_ALL, "");
    puls_log_init ();

    g_autoptr(GOptionContext) context = g_option_context_new ("- S.M.A.R.T. disk health monitor and benchmark");
    g_option_context_add_main_entries (context, cli_entries, NULL);
    g_option_context_set_ignore_unknown_options (context, TRUE);

    GError *error = NULL;
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        PULS_LOG_ERROR ("Option parsing failed: %s", error ? error->message : "unknown error");
        g_printerr ("Option parsing failed: %s\n", error ? error->message : "unknown error");
        g_clear_error (&error);
        return 1;
    }

    PULS_LOG_DEBUG ("PULS DiskInfo starting up (version %s)", PULS_VERSION);

    if (opt_mock) {
        puls_set_mock_mode_enabled (TRUE);
    }

    if (opt_version) {
        g_print ("PULS DiskInfo %s\n", PULS_VERSION);
        return 0;
    }

    if (opt_headless || opt_json) {
        return run_headless_scan (opt_json);
    }

    if (!gtk_init_check ()) {
        g_printerr ("Error: Cannot open display. Use --headless for terminal mode or --version.\n");
        return 1;
    }

    adw_init ();

    g_autoptr(PulsApplication) app = puls_application_new ();
    return g_application_run (G_APPLICATION (app), argc, argv);
}
