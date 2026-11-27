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
#include "../helper/puls-helper-protocol.h"
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

    /* Negotiate / verify smartctl JSON schema version (1.x supported) */
    if (json_object_has_member (root, "json_format_version")) {
        JsonNode *vnode = json_object_get_member (root, "json_format_version");
        if (JSON_NODE_HOLDS_ARRAY (vnode)) {
            JsonArray *varr = json_node_get_array (vnode);
            if (varr != NULL && json_array_get_length (varr) >= 2) {
                JsonNode *e0 = json_array_get_element (varr, 0);
                JsonNode *e1 = json_array_get_element (varr, 1);
                if (JSON_NODE_HOLDS_VALUE (e0) && JSON_NODE_HOLDS_VALUE (e1)) {
                    gint64 vmaj = json_node_get_int (e0);
                    gint64 vmin = json_node_get_int (e1);
                    if (vmaj != 1) {
                        PULS_LOG_WARN ("Unexpected smartctl JSON schema version %" G_GINT64_FORMAT ".%" G_GINT64_FORMAT " (expected 1.x); attempting best-effort parse",
                                       vmaj, vmin);
                    }
                }
            }
        }
    }

    PulsSmartData *data = puls_smart_data_new ();

    parse_device_info (root, data);
    parse_health_status (root, data);
    parse_temperature (root, data);
    parse_ata_attributes (root, data);
    parse_power_on_stats (root, data);
    parse_nvme_health (root, data);
    parse_self_test_status (root, data);

    PulsPowerState pstate = puls_parse_smartctl_standby_json (json_str);
    if (pstate != PULS_POWER_STATE_UNKNOWN)
        puls_smart_data_set_power_state (data, pstate);

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

    {
        GArray *attrs = puls_smart_data_get_ata_attributes (data);
        if (attrs && attrs->len > 0) {
            gint min_headroom = 100;
            gboolean any_threshold = FALSE;
            for (guint i = 0; i < attrs->len; i++) {
                PulsSmartAttribute *attr = &g_array_index (attrs, PulsSmartAttribute, i);
                if (attr->threshold > 0 && attr->current > 0) {
                    gint range = 100 - attr->threshold;
                    if (range <= 0) range = 1;
                    gint headroom = (attr->current - attr->threshold) * 100 / range;
                    headroom = CLAMP (headroom, 0, 100);
                    if (headroom < min_headroom)
                        min_headroom = headroom;
                    any_threshold = TRUE;
                }
            }
            if (any_threshold)
                puls_smart_data_set_health_percent (data, min_headroom);
        }

        PulsNvmeHealth *nvme = puls_smart_data_get_nvme_health (data);
        if (nvme) {
            gint pct = 100 - (gint)nvme->percentage_used;
            pct = CLAMP (pct, 0, 100);
            puls_smart_data_set_health_percent (data, pct);
        }
    }

    gint health_pct = puls_smart_data_get_health_percent (data);
    guint64 hours_used = puls_smart_data_get_power_on_hours (data);
    if (health_pct > 0 && health_pct < 100) {
        gint percentage_used = 100 - health_pct;
        gint days_left = -1;

        if (hours_used > 2000 && percentage_used > 0) {
            guint64 total_hours_est = hours_used * 100 / percentage_used;
            guint64 hours_left = (total_hours_est > hours_used) ? (total_hours_est - hours_used) : 0;
            days_left = (gint)(hours_left / 24);
            gint min_expected_days = health_pct * 30;
            if (days_left < min_expected_days) {
                days_left = min_expected_days;
            }
        } else {
            days_left = (gint)(health_pct * 36.5);
        }
        puls_smart_data_set_estimated_lifetime_days (data, days_left);
    } else if (health_pct == 100) {
        puls_smart_data_set_estimated_lifetime_days (data, 3650);
    }


    {
        /* buffer_size_kb from "cache" key */
        if (json_object_has_member (root, "ata_cache")) {
            JsonObject *cache_obj = json_object_get_object_member (root, "ata_cache");
            gint64 cache_kb = json_object_get_int_safe (cache_obj, "size", 0);
            if (cache_kb > 0)
                puls_smart_data_set_buffer_size_kb (data, (guint32)cache_kb);
        }
        if (puls_smart_data_get_buffer_size_kb (data) == 0 &&
            json_object_has_member (root, "cache")) {
            JsonObject *cache_obj = json_object_get_object_member (root, "cache");
            gint64 cache_kb = json_object_get_int_safe (cache_obj, "size", 0);
            if (cache_kb > 0)
                puls_smart_data_set_buffer_size_kb (data, (guint32)cache_kb);
        }

        if (json_object_has_member (root, "apm")) {
            JsonObject *apm_obj = json_object_get_object_member (root, "apm");
            gint64 apm_val = json_object_get_int_safe (apm_obj, "level", -1);
            if (apm_val >= 0)
                puls_smart_data_set_apm_level (data, (gint)apm_val);
        }

        if (json_object_has_member (root, "aam")) {
            JsonObject *aam_obj = json_object_get_object_member (root, "aam");
            gint64 aam_val = json_object_get_int_safe (aam_obj, "level", -1);
            if (aam_val >= 0)
                puls_smart_data_set_aam_level (data, (gint)aam_val);
        }

        GArray *attrs = puls_smart_data_get_ata_attributes (data);
        if (attrs) {
            for (guint i = 0; i < attrs->len; i++) {
                PulsSmartAttribute *attr = &g_array_index (attrs, PulsSmartAttribute, i);
                if (attr->id == 3 && attr->raw_value > 0) {
                    puls_smart_data_set_spin_up_time_ms (data, (gint)(attr->raw_value & 0xFFFF));
                    break;
                }
            }
        }

        if (json_object_has_member (root, "ata_smart_error_log")) {
            JsonObject *elog = json_object_get_object_member (root, "ata_smart_error_log");
            if (json_object_has_member (elog, "summary")) {
                JsonObject *summary = json_object_get_object_member (elog, "summary");
                gint64 count = json_object_get_int_safe (summary, "count", 0);
                if (count > 0)
                    puls_smart_data_set_error_count_total (data, (guint64)count);
            }
        }
    }

    return data;
}

static gboolean
smart_device_type_supported_by_caps (PulsSmartDeviceType     dev_type,
                                     const PulsSmartctlCaps *caps)
{
    if (caps == NULL || !caps->available)
        return TRUE;

    switch (dev_type) {
    case PULS_SMART_DEVTYPE_AUTO:
        return TRUE;
    case PULS_SMART_DEVTYPE_SAT:
        return caps->supports_sat;
    case PULS_SMART_DEVTYPE_SNT_ASMEDIA:
        return caps->supports_sntasmedia;
    case PULS_SMART_DEVTYPE_SNT_JMICRON:
        return caps->supports_sntjmicron;
    case PULS_SMART_DEVTYPE_SNT_REALTEK:
        return caps->supports_sntrealtek;
    default:
        return FALSE;
    }
}

static gboolean
usb_hint_contains (const gchar *lower_hint, const gchar *needle)
{
    return (lower_hint != NULL && needle != NULL && strstr (lower_hint, needle) != NULL);
}

/**
 * puls_smart_device_type_to_flag:
 * @dev_type: Device type enum value.
 *
 * Maps @dev_type to the whitelisted `smartctl -d` argument string, or %NULL for
 * %PULS_SMART_DEVTYPE_AUTO.
 *
 * Returns: (nullable): Static device-type flag string or %NULL.
 */
const gchar *
puls_smart_device_type_to_flag (PulsSmartDeviceType dev_type)
{
    switch (dev_type) {
    case PULS_SMART_DEVTYPE_SAT:
        return PULS_HELPER_DEVTYPE_SAT;
    case PULS_SMART_DEVTYPE_SNT_ASMEDIA:
        return PULS_HELPER_DEVTYPE_SNT_ASMEDIA;
    case PULS_SMART_DEVTYPE_SNT_JMICRON:
        return PULS_HELPER_DEVTYPE_SNT_JMICRON;
    case PULS_SMART_DEVTYPE_SNT_REALTEK:
        return PULS_HELPER_DEVTYPE_SNT_REALTEK;
    case PULS_SMART_DEVTYPE_AUTO:
    default:
        return NULL;
    }
}

/**
 * puls_smart_device_type_from_usb_ids:
 * @vid: (nullable): 4-digit hex USB vendor ID.
 * @pid: (nullable): 4-digit hex USB product ID.
 * @bridge_hint: (nullable): Human-readable chipset or USB product hint string.
 *
 * Infers the most likely `smartctl -d` passthrough type for a USB bridge.
 *
 * Returns: Predicted #PulsSmartDeviceType, or %PULS_SMART_DEVTYPE_AUTO when no
 *   specific bridge preference is known.
 */
PulsSmartDeviceType
puls_smart_device_type_from_usb_ids (const gchar *vid,
                                     const gchar *pid,
                                     const gchar *bridge_hint)
{
    g_autofree gchar *lower_vid = vid ? g_ascii_strdown (vid, -1) : NULL;
    g_autofree gchar *lower_pid = pid ? g_ascii_strdown (pid, -1) : NULL;
    g_autofree gchar *lower_hint = bridge_hint ? g_ascii_strdown (bridge_hint, -1) : NULL;

    gboolean hint_nvme = usb_hint_contains (lower_hint, "nvme");
    gboolean hint_sata = usb_hint_contains (lower_hint, "sata") ||
                         usb_hint_contains (lower_hint, "hdd") ||
                         usb_hint_contains (lower_hint, "ata");

    /* ASMedia bridges (174c): SATA vs NVMe families */
    if (g_strcmp0 (lower_vid, "174c") == 0 || usb_hint_contains (lower_hint, "asmedia")) {
        if (g_strcmp0 (lower_pid, "55aa") == 0 ||
            g_strcmp0 (lower_pid, "1153") == 0 ||
            g_strcmp0 (lower_pid, "1051") == 0 ||
            g_strcmp0 (lower_pid, "1053") == 0) {
            return PULS_SMART_DEVTYPE_SAT;
        }
        if (g_strcmp0 (lower_pid, "2362") == 0 ||
            g_strcmp0 (lower_pid, "2364") == 0 ||
            g_strcmp0 (lower_pid, "2464") == 0 ||
            hint_nvme) {
            return PULS_SMART_DEVTYPE_SNT_ASMEDIA;
        }
        if (hint_sata && !hint_nvme)
            return PULS_SMART_DEVTYPE_SAT;
        return PULS_SMART_DEVTYPE_SNT_ASMEDIA;
    }

    /* Realtek bridges (0bda): RTL9210 / RTL9201 NVMe or USB-SATA */
    if (g_strcmp0 (lower_vid, "0bda") == 0 || usb_hint_contains (lower_hint, "realtek")) {
        if (g_strcmp0 (lower_pid, "9210") == 0 ||
            g_strcmp0 (lower_pid, "9201") == 0 ||
            hint_nvme) {
            return PULS_SMART_DEVTYPE_SNT_REALTEK;
        }
        if (hint_sata && !hint_nvme)
            return PULS_SMART_DEVTYPE_SAT;
        return PULS_SMART_DEVTYPE_SNT_REALTEK;
    }

    /* JMicron bridges (152d): JMS567/578/580 SATA vs JMS583 NVMe */
    if (g_strcmp0 (lower_vid, "152d") == 0 || usb_hint_contains (lower_hint, "jmicron")) {
        if (g_strcmp0 (lower_pid, "0567") == 0 ||
            g_strcmp0 (lower_pid, "0578") == 0 ||
            g_strcmp0 (lower_pid, "0580") == 0) {
            return PULS_SMART_DEVTYPE_SAT;
        }
        if (g_strcmp0 (lower_pid, "0583") == 0 || hint_nvme)
            return PULS_SMART_DEVTYPE_SNT_JMICRON;
        if (hint_sata && !hint_nvme)
            return PULS_SMART_DEVTYPE_SAT;
        return PULS_SMART_DEVTYPE_SNT_JMICRON;
    }

    /* Genesys Logic (05e3) / VIA Labs (2109) / generic SATA USB bridges */
    if (g_strcmp0 (lower_vid, "05e3") == 0 ||
        g_strcmp0 (lower_vid, "2109") == 0 ||
        (hint_sata && !hint_nvme)) {
        return PULS_SMART_DEVTYPE_SAT;
    }

    return PULS_SMART_DEVTYPE_AUTO;
}

static void
append_probe_candidate (PulsSmartDeviceType     candidate,
                        const PulsSmartctlCaps *caps,
                        PulsSmartDeviceType    *out_order,
                        guint                   max_len,
                        guint                  *io_count)
{
    if (*io_count >= max_len)
        return;

    if (!smart_device_type_supported_by_caps (candidate, caps))
        return;

    for (guint i = 0; i < *io_count; i++) {
        if (out_order[i] == candidate)
            return;
    }

    out_order[*io_count] = candidate;
    (*io_count)++;
}

/**
 * puls_smart_build_usb_probe_order:
 * @bridge_info: (nullable): USB bridge identity parsed from sysfs.
 * @caps: (nullable): Installed `smartctl` feature flags for gating `-d` types.
 * @out_order: (out caller-allocates): Output array receiving ordered candidates.
 * @max_len: Capacity of @out_order (typically %PULS_SMART_USB_PROBE_MAX_STEPS).
 *
 * Builds the deduplicated probe sequence starting with default auto-detection
 * (`-a`), followed by any prioritized chipset flag inferred from @bridge_info,
 * then the standard USB bridge fallbacks (`sat`, `sntasmedia`, `sntjmicron`,
 * `sntrealtek`), filtered to flags supported by @caps.
 *
 * Returns: Number of probe steps written into @out_order.
 */
guint
puls_smart_build_usb_probe_order (const PulsUsbBridgeInfo *bridge_info,
                                  const PulsSmartctlCaps  *caps,
                                  PulsSmartDeviceType     *out_order,
                                  guint                    max_len)
{
    g_return_val_if_fail (out_order != NULL, 0);
    g_return_val_if_fail (max_len > 0, 0);

    static const PulsSmartDeviceType fallback_seq[] = {
        PULS_SMART_DEVTYPE_SAT,
        PULS_SMART_DEVTYPE_SNT_ASMEDIA,
        PULS_SMART_DEVTYPE_SNT_JMICRON,
        PULS_SMART_DEVTYPE_SNT_REALTEK,
    };

    guint count = 0;
    append_probe_candidate (PULS_SMART_DEVTYPE_AUTO, caps, out_order, max_len, &count);

    if (bridge_info != NULL) {
        const gchar *hint = bridge_info->bridge_chip ? bridge_info->bridge_chip : bridge_info->product;
        PulsSmartDeviceType preferred = puls_smart_device_type_from_usb_ids (
            bridge_info->vendor_id, bridge_info->product_id, hint);
        if (preferred != PULS_SMART_DEVTYPE_AUTO)
            append_probe_candidate (preferred, caps, out_order, max_len, &count);
    }

    for (guint i = 0; i < G_N_ELEMENTS (fallback_seq); i++)
        append_probe_candidate (fallback_seq[i], caps, out_order, max_len, &count);

    return count;
}

/**
 * puls_smart_json_has_device_data:
 * @json_str: (nullable): Raw `smartctl -j` JSON output.
 *
 * Checks whether @json_str is valid JSON containing either a valid standby/sleep
 * power report or SMART device telemetry (`smart_status`, ATA attributes, NVMe
 * health log, or SCSI cycle counters), rather than an unknown-bridge error stub.
 *
 * Returns: %TRUE if @json_str holds usable drive telemetry.
 */
gboolean
puls_smart_json_has_device_data (const gchar *json_str)
{
    if (json_str == NULL || *json_str == '\0')
        return FALSE;

    if (puls_power_state_is_sleeping (puls_parse_smartctl_standby_json (json_str)))
        return TRUE;

    g_autoptr(JsonParser) parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json_str, -1, NULL))
        return FALSE;

    JsonNode *root_node = json_parser_get_root (parser);
    if (root_node == NULL || !JSON_NODE_HOLDS_OBJECT (root_node))
        return FALSE;

    JsonObject *root = json_node_get_object (root_node);
    if (json_object_has_member (root, "smart_status") ||
        json_object_has_member (root, "ata_smart_attributes") ||
        json_object_has_member (root, "nvme_smart_health_information_log") ||
        json_object_has_member (root, "scsi_start_stop_cycle_counter")) {
        return TRUE;
    }

    return FALSE;
}

/**
 * puls_smart_resolve_usb_bridge_json:
 * @device_path: Block or controller device node path.
 * @bridge_info: (nullable): Sysfs USB bridge details for prioritizing `-d` flags.
 * @caps: (nullable): Installed `smartctl` capabilities for flag gating.
 * @initial_auto_json: (nullable): Output from an already-completed default `-a`
 *   probe, to avoid running default `AUTO` twice when the caller already tried it.
 * @probe_fn: Callback executing `smartctl` with a chosen #PulsSmartDeviceType.
 * @user_data: Opaque pointer forwarded to @probe_fn.
 * @out_resolved_type: (out) (optional): Winning #PulsSmartDeviceType.
 * @error: (out) (optional): Last error if all probes fail without usable JSON.
 *
 * Executes the ordered USB bridge `-d` fallback sequence until a probe returns
 * usable SMART JSON. If no candidate yields complete SMART telemetry, returns
 * the best non-empty raw JSON fallback (e.g. @initial_auto_json) so callers can
 * still report model/error status.
 *
 * Returns: (transfer full) (nullable): Raw SMART JSON string.
 */
gchar *
puls_smart_resolve_usb_bridge_json (const gchar             *device_path,
                                    const PulsUsbBridgeInfo *bridge_info,
                                    const PulsSmartctlCaps  *caps,
                                    const gchar             *initial_auto_json,
                                    PulsSmartProbeFunc       probe_fn,
                                    gpointer                 user_data,
                                    PulsSmartDeviceType     *out_resolved_type,
                                    GError                 **error)
{
    g_return_val_if_fail (device_path != NULL, NULL);
    g_return_val_if_fail (probe_fn != NULL, NULL);

    if (out_resolved_type != NULL)
        *out_resolved_type = PULS_SMART_DEVTYPE_AUTO;

    if (puls_smart_json_has_device_data (initial_auto_json))
        return g_strdup (initial_auto_json);

    PulsSmartDeviceType order[PULS_SMART_USB_PROBE_MAX_STEPS];
    guint n_steps = puls_smart_build_usb_probe_order (bridge_info, caps, order, G_N_ELEMENTS (order));

    g_autofree gchar *fallback_raw = (initial_auto_json && *initial_auto_json != '\0')
                                     ? g_strdup (initial_auto_json) : NULL;
    GError *last_err = NULL;

    for (guint i = 0; i < n_steps; i++) {
        PulsSmartDeviceType dev_type = order[i];

        if (dev_type == PULS_SMART_DEVTYPE_AUTO && initial_auto_json != NULL)
            continue;

        g_clear_error (&last_err);
        gchar *candidate_json = probe_fn (device_path, dev_type, user_data, &last_err);

        if (puls_smart_json_has_device_data (candidate_json)) {
            g_clear_error (&last_err);
            if (out_resolved_type != NULL)
                *out_resolved_type = dev_type;
            return candidate_json;
        }

        if (fallback_raw == NULL && candidate_json != NULL && *candidate_json != '\0')
            fallback_raw = g_steal_pointer (&candidate_json);
        else
            g_free (candidate_json);
    }

    if (fallback_raw != NULL) {
        g_clear_error (&last_err);
        return g_steal_pointer (&fallback_raw);
    }

    if (last_err != NULL)
        g_propagate_error (error, last_err);

    return NULL;
}
