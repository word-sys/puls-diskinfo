/*
 * puls-smart-parser.c
 *
 * Parses smartctl -a -j JSON output into PulsSmartData objects.
 * Handles both ATA/SATA and NVMe JSON schemas.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-smart-parser.h"
#include "puls-utils.h"
#include <json-glib/json-glib.h>
#include <string.h>

static const gchar *
json_object_get_string_safe (JsonObject  *obj,
                             const gchar *member)
{
    if (!json_object_has_member (obj, member))
        return NULL;

    JsonNode *node = json_object_get_member (obj, member);
    if (JSON_NODE_HOLDS_VALUE (node))
        return json_node_get_string (node);

    return NULL;
}

static gint64
json_object_get_int_safe (JsonObject  *obj,
                          const gchar *member,
                          gint64       default_val)
{
    if (!json_object_has_member (obj, member))
        return default_val;

    JsonNode *node = json_object_get_member (obj, member);
    if (JSON_NODE_HOLDS_VALUE (node))
        return json_node_get_int (node);

    return default_val;
}

static gboolean
json_object_get_boolean_safe (JsonObject  *obj,
                              const gchar *member,
                              gboolean     default_val)
{
    if (!json_object_has_member (obj, member))
        return default_val;

    JsonNode *node = json_object_get_member (obj, member);
    if (JSON_NODE_HOLDS_VALUE (node))
        return json_node_get_boolean (node);

    return default_val;
}

static const gchar *
sata_speed_to_mode (const gchar *speed_str)
{
    if (speed_str == NULL)
        return NULL;
    if (strstr (speed_str, "1.5"))
        return "SATA/150";
    if (strstr (speed_str, "3.0"))
        return "SATA/300";
    if (strstr (speed_str, "6.0"))
        return "SATA/600";
    return speed_str;
}

static void
parse_device_info (JsonObject    *root,
                   PulsSmartData *data)
{
    if (json_object_has_member (root, "device")) {
        JsonObject *device = json_object_get_object_member (root, "device");
        const gchar *name = json_object_get_string_safe (device, "name");
        if (name)
            puls_smart_data_set_device_path (data, name);

        const gchar *protocol = json_object_get_string_safe (device, "protocol");
        if (protocol)
            puls_smart_data_set_interface_type (data, protocol);
    }

    const gchar *model = json_object_get_string_safe (root, "model_name");
    if (model)
        puls_smart_data_set_model_name (data, model);

    const gchar *serial = json_object_get_string_safe (root, "serial_number");
    if (serial)
        puls_smart_data_set_serial_number (data, serial);

    const gchar *fw = json_object_get_string_safe (root, "firmware_version");
    if (fw)
        puls_smart_data_set_firmware_version (data, fw);

    if (json_object_has_member (root, "user_capacity")) {
        JsonObject *cap = json_object_get_object_member (root, "user_capacity");
        gint64 bytes = json_object_get_int_safe (cap, "bytes", 0);
        puls_smart_data_set_capacity_bytes (data, (guint64)bytes);
    }

    gint64 lbs = json_object_get_int_safe (root, "logical_block_size", 0);
    if (lbs > 0)
        puls_smart_data_set_logical_sector_size (data, (guint32)lbs);

    gint64 pbs = json_object_get_int_safe (root, "physical_block_size", 0);
    if (pbs > 0)
        puls_smart_data_set_physical_sector_size (data, (guint32)pbs);

    if (json_object_has_member (root, "form_factor")) {
        JsonObject *ff_obj = json_object_get_object_member (root, "form_factor");
        const gchar *ff_name = json_object_get_string_safe (ff_obj, "name");
        if (ff_name == NULL) {
            ff_name = json_object_get_string_safe (ff_obj, "string");
        }
        if (ff_name) {
            puls_smart_data_set_form_factor (data, ff_name);
        }
    }

    gint64 rpm = json_object_get_int_safe (root, "rotation_rate", -1);
    puls_smart_data_set_rotation_rpm (data, (gint)rpm);

    if (json_object_has_member (root, "ata_version")) {
        JsonObject *v_obj = json_object_get_object_member (root, "ata_version");
        const gchar *v_str = json_object_get_string_safe (v_obj, "string");
        if (v_str)
            puls_smart_data_set_standard (data, v_str);
    }

    if (json_object_has_member (root, "nvme_version")) {
        JsonObject *v_obj = json_object_get_object_member (root, "nvme_version");
        const gchar *v_str = json_object_get_string_safe (v_obj, "string");
        if (v_str) {
            g_autofree gchar *fmt = g_strdup_printf ("NVMe %s", v_str);
            puls_smart_data_set_standard (data, fmt);
        }
    }

    if (json_object_has_member (root, "sata_version")) {
        JsonObject *v_obj = json_object_get_object_member (root, "sata_version");
        const gchar *v_str = json_object_get_string_safe (v_obj, "string");
        if (v_str && !puls_smart_data_get_standard (data)) {
            puls_smart_data_set_standard (data, v_str);
        }
    }

    if (json_object_has_member (root, "interface_speed")) {
        JsonObject *speed_obj = json_object_get_object_member (root, "interface_speed");
        const gchar *curr_mode = NULL;
        const gchar *max_mode = NULL;

        if (json_object_has_member (speed_obj, "current")) {
            JsonObject *curr_obj = json_object_get_object_member (speed_obj, "current");
            const gchar *s = json_object_get_string_safe (curr_obj, "string");
            curr_mode = sata_speed_to_mode (s);
            if (s) {
                g_autofree gchar *iface = g_strdup_printf ("SATA %s", s);
                puls_smart_data_set_interface_type (data, iface);
            }
        }
        if (json_object_has_member (speed_obj, "max")) {
            JsonObject *max_obj = json_object_get_object_member (speed_obj, "max");
            const gchar *s = json_object_get_string_safe (max_obj, "string");
            max_mode = sata_speed_to_mode (s);
        }

        if (curr_mode && max_mode) {
            g_autofree gchar *mode = g_strdup_printf ("%s | %s", curr_mode, max_mode);
            puls_smart_data_set_transfer_mode (data, mode);
        } else if (curr_mode) {
            puls_smart_data_set_transfer_mode (data, curr_mode);
        }
    }

    if (json_object_has_member (root, "smart_status")) {
        puls_smart_data_set_smart_enabled (data, TRUE);
    }

    if (json_object_has_member (root, "trim")) {
        JsonObject *trim_obj = json_object_get_object_member (root, "trim");
        gboolean trim_supported = json_object_get_boolean_safe (trim_obj, "supported", FALSE);
        puls_smart_data_set_supports_trim (data, trim_supported);
    }

    if (json_object_has_member (root, "apm")) {
        JsonObject *apm_obj = json_object_get_object_member (root, "apm");
        gboolean apm_supported = json_object_get_boolean_safe (apm_obj, "supported", FALSE);
        puls_smart_data_set_supports_apm (data, apm_supported);
    }

    if (json_object_has_member (root, "aam")) {
        JsonObject *aam_obj = json_object_get_object_member (root, "aam");
        gboolean aam_supported = json_object_get_boolean_safe (aam_obj, "supported", FALSE);
        puls_smart_data_set_supports_aam (data, aam_supported);
    }

    if (json_object_has_member (root, "write_cache")) {
        JsonObject *wc_obj = json_object_get_object_member (root, "write_cache");
        gboolean wc_enabled = json_object_get_boolean_safe (wc_obj, "enabled", FALSE);
        puls_smart_data_set_supports_write_cache (data, wc_enabled);
    } else if (json_object_has_member (root, "wcache")) {
        JsonObject *wc_obj = json_object_get_object_member (root, "wcache");
        gboolean wc_enabled = json_object_get_boolean_safe (wc_obj, "enabled", FALSE);
        puls_smart_data_set_supports_write_cache (data, wc_enabled);
    }

    if (json_object_has_member (root, "sata_ncq")) {
        JsonObject *ncq_obj = json_object_get_object_member (root, "sata_ncq");
        gboolean ncq_supported = json_object_get_boolean_safe (ncq_obj, "supported", FALSE);
        puls_smart_data_set_supports_ncq (data, ncq_supported);
    }

    if (json_object_has_member (root, "sata_devsleep")) {
        JsonObject *ds_obj = json_object_get_object_member (root, "sata_devsleep");
        gboolean ds_supported = json_object_get_boolean_safe (ds_obj, "supported", FALSE);
        puls_smart_data_set_supports_devsleep (data, ds_supported);
    }
}

static void
parse_health_status (JsonObject    *root,
                     PulsSmartData *data)
{
    if (!json_object_has_member (root, "smart_status"))
        return;

    JsonObject *status = json_object_get_object_member (root, "smart_status");
    gboolean passed = json_object_get_boolean_safe (status, "passed", FALSE);

    puls_smart_data_set_health (data, passed ? PULS_HEALTH_GOOD : PULS_HEALTH_BAD);
}

static void
parse_temperature (JsonObject    *root,
                   PulsSmartData *data)
{
    if (!json_object_has_member (root, "temperature"))
        return;

    JsonObject *temp = json_object_get_object_member (root, "temperature");
    gint64 current = json_object_get_int_safe (temp, "current", -1);
    puls_smart_data_set_temperature (data, (gint)current);
}

static void
parse_ata_attributes (JsonObject    *root,
                      PulsSmartData *data)
{
    if (!json_object_has_member (root, "ata_smart_attributes"))
        return;

    JsonObject *smart_attrs = json_object_get_object_member (root, "ata_smart_attributes");
    if (!json_object_has_member (smart_attrs, "table"))
        return;

    JsonArray *table = json_object_get_array_member (smart_attrs, "table");
    guint len = json_array_get_length (table);
    gboolean has_caution = FALSE;

    for (guint i = 0; i < len; i++) {
        JsonObject *entry = json_array_get_object_element (table, i);

        PulsSmartAttribute attr = { 0 };
        attr.id = (guint8)json_object_get_int_safe (entry, "id", 0);
        attr.name = g_strdup (json_object_get_string_safe (entry, "name"));

        attr.current   = (gint)json_object_get_int_safe (entry, "value", 0);
        attr.worst     = (gint)json_object_get_int_safe (entry, "worst", 0);
        attr.threshold = (gint)json_object_get_int_safe (entry, "thresh", 0);

        if (json_object_has_member (entry, "raw")) {
            JsonObject *raw = json_object_get_object_member (entry, "raw");
            attr.raw_value = (guint64)json_object_get_int_safe (raw, "value", 0);
            attr.raw_string = g_strdup (json_object_get_string_safe (raw, "string"));
        }

        if (json_object_has_member (entry, "when_failed")) {
            const gchar *when = json_object_get_string_safe (entry, "when_failed");
            if (when) {
                if (g_strcmp0 (when, "past") == 0)
                    attr.failed_past = TRUE;
                else if (g_strcmp0 (when, "now") == 0 ||
                         g_strcmp0 (when, "FAILING_NOW") == 0)
                    attr.failing_now = TRUE;
            }
        }

        if (attr.threshold > 0 && attr.current > 0) {
            gint margin = attr.current - attr.threshold;
            if (margin <= 10 && margin > 0)
                has_caution = TRUE;
        }

        if (attr.failing_now) {
            puls_smart_data_set_health (data, PULS_HEALTH_BAD);
        }

        puls_smart_data_add_ata_attribute (data, &attr);

        g_free (attr.name);
        g_free (attr.raw_string);
    }

    if (has_caution && puls_smart_data_get_health (data) == PULS_HEALTH_GOOD) {
        puls_smart_data_set_health (data, PULS_HEALTH_CAUTION);
    }
}

static void
parse_power_on_stats (JsonObject    *root,
                      PulsSmartData *data)
{
    if (json_object_has_member (root, "power_on_time")) {
        JsonObject *pot = json_object_get_object_member (root, "power_on_time");
        guint64 hours = (guint64)json_object_get_int_safe (pot, "hours", 0);
        puls_smart_data_set_power_on_hours (data, hours);
    }

    gint64 pcc = json_object_get_int_safe (root, "power_cycle_count", 0);
    puls_smart_data_set_power_cycle_count (data, (guint64)pcc);
}

static void
parse_nvme_health (JsonObject    *root,
                   PulsSmartData *data)
{
    if (!json_object_has_member (root, "nvme_smart_health_information_log"))
        return;

    JsonObject *log = json_object_get_object_member (root,
                          "nvme_smart_health_information_log");

    PulsNvmeHealth health = { 0 };

    health.critical_warning          = (guint8)json_object_get_int_safe (log, "critical_warning", 0);
    health.temperature               = (gint)json_object_get_int_safe (log, "temperature", -1);
    health.available_spare           = (guint8)json_object_get_int_safe (log, "available_spare", 0);
    health.available_spare_threshold = (guint8)json_object_get_int_safe (log, "available_spare_threshold", 0);
    health.percentage_used           = (guint8)json_object_get_int_safe (log, "percentage_used", 0);
    health.data_units_read           = (guint64)json_object_get_int_safe (log, "data_units_read", 0);
    health.data_units_written        = (guint64)json_object_get_int_safe (log, "data_units_written", 0);
    health.host_read_commands        = (guint64)json_object_get_int_safe (log, "host_reads", 0);
    health.host_write_commands       = (guint64)json_object_get_int_safe (log, "host_writes", 0);
    health.controller_busy_time      = (guint64)json_object_get_int_safe (log, "controller_busy_time", 0);
    health.power_cycles              = (guint64)json_object_get_int_safe (log, "power_cycles", 0);
    health.power_on_hours            = (guint64)json_object_get_int_safe (log, "power_on_hours", 0);
    health.unsafe_shutdowns          = (guint64)json_object_get_int_safe (log, "unsafe_shutdowns", 0);
    health.media_errors              = (guint64)json_object_get_int_safe (log, "media_errors", 0);
    health.error_log_entries         = (guint64)json_object_get_int_safe (log, "num_err_log_entries", 0);

    puls_smart_data_set_nvme_health (data, &health);

    if (puls_smart_data_get_temperature (data) < 0 && health.temperature >= 0)
        puls_smart_data_set_temperature (data, health.temperature);

    if (puls_smart_data_get_power_on_hours (data) == 0 && health.power_on_hours > 0)
        puls_smart_data_set_power_on_hours (data, health.power_on_hours);

    if (puls_smart_data_get_power_cycle_count (data) == 0 && health.power_cycles > 0)
        puls_smart_data_set_power_cycle_count (data, health.power_cycles);

    if (health.data_units_written > 0)
        puls_smart_data_set_total_bytes_written (data, health.data_units_written * 512000ULL);

    if (health.data_units_read > 0)
        puls_smart_data_set_total_bytes_read (data, health.data_units_read * 512000ULL);

    if (health.critical_warning != 0) {
        puls_smart_data_set_health (data, PULS_HEALTH_BAD);
    } else if (health.available_spare <= health.available_spare_threshold ||
               health.percentage_used >= 90) {
        if (puls_smart_data_get_health (data) != PULS_HEALTH_BAD)
            puls_smart_data_set_health (data, PULS_HEALTH_CAUTION);
    }
}

static void
parse_self_test_status (JsonObject    *root,
                        PulsSmartData *data)
{
    if (!json_object_has_member (root, "current_testing")) {
        puls_smart_data_set_self_test_in_progress (data, FALSE);
        puls_smart_data_set_self_test_percent (data, 0);
        puls_smart_data_set_self_test_status_str (data, NULL);
        puls_smart_data_set_self_test_type_str (data, NULL);
        return;
    }

    JsonObject *ct = json_object_get_object_member (root, "current_testing");
    puls_smart_data_set_self_test_in_progress (data, TRUE);

    gint64 remaining = json_object_get_int_safe (ct, "remaining_percent", 100);
    gint percent = 100 - (gint)remaining;
    puls_smart_data_set_self_test_percent (data, percent);

    if (json_object_has_member (ct, "status")) {
        JsonObject *status_obj = json_object_get_object_member (ct, "status");
        const gchar *status_str = json_object_get_string_safe (status_obj, "string");
        if (status_str)
            puls_smart_data_set_self_test_status_str (data, status_str);
    }

    if (json_object_has_member (ct, "type")) {
        JsonObject *type_obj = json_object_get_object_member (ct, "type");
        const gchar *type_str = json_object_get_string_safe (type_obj, "string");
        if (type_str)
            puls_smart_data_set_self_test_type_str (data, type_str);
    }
}

PulsSmartData *
puls_smart_parser_parse_json (const gchar *json_str,
                              GError     **error)
{
    g_return_val_if_fail (json_str != NULL, NULL);

    g_autoptr(JsonParser) parser = json_parser_new ();

    if (!json_parser_load_from_data (parser, json_str, -1, error))
        return NULL;

    JsonNode *root_node = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root_node)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "smartctl JSON root is not an object");
        return NULL;
    }

    JsonObject *root = json_node_get_object (root_node);
    PulsSmartData *data = puls_smart_data_new ();

    parse_device_info (root, data);
    parse_health_status (root, data);
    parse_temperature (root, data);
    parse_ata_attributes (root, data);
    parse_power_on_stats (root, data);
    parse_nvme_health (root, data);
    parse_self_test_status (root, data);

    const gchar *device_path = puls_smart_data_get_device_path (data);
    if (device_path)
        puls_smart_data_set_drive_type (data, puls_detect_drive_type (device_path));

    if (puls_smart_data_get_total_bytes_written (data) == 0) {
        GArray *attrs = puls_smart_data_get_ata_attributes (data);
        if (attrs) {
            for (guint i = 0; i < attrs->len; i++) {
                PulsSmartAttribute *attr = &g_array_index (attrs, PulsSmartAttribute, i);
                if (attr->id == 241) {
                    puls_smart_data_set_total_bytes_written (data,
                        attr->raw_value * 512ULL);
                }
                if (attr->id == 242) {
                    puls_smart_data_set_total_bytes_read (data,
                        attr->raw_value * 512ULL);
                }
            }
        }
    }

    return data;
}
