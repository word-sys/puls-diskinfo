/*
 * puls-alert-manager.c
 *
 * Alert manager implementation.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-alert-manager.h"
#include "puls-settings.h"

struct _PulsAlertManager {
    GObject parent_instance;

    GPtrArray *log;          /* All PulsAlertEntry* ever fired this session */
    gint       temp_threshold;
    gint       health_threshold;

    /* Per-device de-dup: track last-seen temp/health to avoid toast spam */
    GHashTable *last_temp;    /* device_path → gint* */
    GHashTable *last_health;  /* device_path → gint* */
};

G_DEFINE_TYPE (PulsAlertManager, puls_alert_manager, G_TYPE_OBJECT)

static PulsAlertManager *default_manager = NULL;

static void
alert_entry_free (PulsAlertEntry *e)
{
    if (!e) return;
    g_free (e->message);
    g_free (e->device_path);
    if (e->timestamp) g_date_time_unref (e->timestamp);
    g_free (e);
}

static void
puls_alert_manager_finalize (GObject *object)
{
    PulsAlertManager *self = PULS_ALERT_MANAGER (object);
    g_ptr_array_free (self->log, TRUE);
    g_hash_table_destroy (self->last_temp);
    g_hash_table_destroy (self->last_health);
    G_OBJECT_CLASS (puls_alert_manager_parent_class)->finalize (object);
}

static void
puls_alert_manager_class_init (PulsAlertManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = puls_alert_manager_finalize;
}

static void
puls_alert_manager_init (PulsAlertManager *self)
{
    self->log = g_ptr_array_new_with_free_func ((GDestroyNotify)alert_entry_free);
    self->last_temp   = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    self->last_health = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    PulsSettings *settings = puls_settings_get_default ();
    self->temp_threshold   = puls_settings_get_alert_temp_threshold   (settings);
    self->health_threshold = puls_settings_get_alert_health_threshold (settings);
}

PulsAlertManager *
puls_alert_manager_get_default (void)
{
    if (default_manager == NULL) {
        default_manager = g_object_new (PULS_TYPE_ALERT_MANAGER, NULL);
        g_object_add_weak_pointer (G_OBJECT (default_manager),
                                   (gpointer *)&default_manager);
    }
    return default_manager;
}

void
puls_alert_manager_set_temp_threshold (PulsAlertManager *self, gint celsius)
{
    g_return_if_fail (PULS_IS_ALERT_MANAGER (self));
    self->temp_threshold = celsius;
}

void
puls_alert_manager_set_health_threshold (PulsAlertManager *self, gint percent)
{
    g_return_if_fail (PULS_IS_ALERT_MANAGER (self));
    self->health_threshold = percent;
}

GPtrArray *
puls_alert_manager_get_log (PulsAlertManager *self)
{
    g_return_val_if_fail (PULS_IS_ALERT_MANAGER (self), NULL);
    return self->log;
}

static void
append_alert (PulsAlertManager *self,
              GPtrArray        *new_alerts,
              const gchar      *device_path,
              const gchar      *message)
{
    PulsAlertEntry *e = g_new0 (PulsAlertEntry, 1);
    e->message     = g_strdup (message);
    e->device_path = g_strdup (device_path);
    e->timestamp   = g_date_time_new_now_local ();
    g_ptr_array_add (self->log, e);
    /* new_alerts holds a borrowed pointer into self->log's last element */
    g_ptr_array_add (new_alerts, e);
}

GPtrArray *
puls_alert_manager_check (PulsAlertManager *self,
                           PulsSmartData    *data)
{
    g_return_val_if_fail (PULS_IS_ALERT_MANAGER (self), NULL);
    g_return_val_if_fail (data != NULL, NULL);

    /* Re-read thresholds each time in case settings changed */
    PulsSettings *settings = puls_settings_get_default ();
    if (!puls_settings_get_alerts_enabled (settings))
        return NULL;
    self->temp_threshold   = puls_settings_get_alert_temp_threshold   (settings);
    self->health_threshold = puls_settings_get_alert_health_threshold (settings);

    GPtrArray *new_alerts = g_ptr_array_new (); /* borrowed ptrs, do NOT free entries */
    const gchar *dev = puls_smart_data_get_device_path (data);
    if (dev == NULL) dev = "unknown";

    /* ── Temperature alert ──────────────────────────────────── */
    gint temp = puls_smart_data_get_temperature (data);
    if (temp >= 0 && self->temp_threshold > 0) {
        gint *last_t = g_hash_table_lookup (self->last_temp, dev);
        gboolean was_ok = (last_t == NULL || *last_t < self->temp_threshold);
        if (temp >= self->temp_threshold && was_ok) {
            g_autofree gchar *msg = g_strdup_printf (
                "[WARNING] %s: Temperature %d °C exceeds threshold (%d °C)",
                dev, temp, self->temp_threshold);
            append_alert (self, new_alerts, dev, msg);
        }
        gint *copy = g_new (gint, 1);
        *copy = temp;
        g_hash_table_insert (self->last_temp, g_strdup (dev), copy);
    }

    /* ── Health % alert ─────────────────────────────────────── */
    gint health_pct = puls_smart_data_get_health_percent (data);
    if (health_pct >= 0 && self->health_threshold > 0) {
        gint *last_h = g_hash_table_lookup (self->last_health, dev);
        gboolean was_ok = (last_h == NULL || *last_h >= self->health_threshold);
        if (health_pct < self->health_threshold && was_ok) {
            g_autofree gchar *msg = g_strdup_printf (
                "[WARNING] %s: Drive health %d%% is below threshold (%d%%)",
                dev, health_pct, self->health_threshold);
            append_alert (self, new_alerts, dev, msg);
        }
        gint *copy = g_new (gint, 1);
        *copy = health_pct;
        g_hash_table_insert (self->last_health, g_strdup (dev), copy);
    }

    /* ── Failing SMART attributes ───────────────────────────── */
    GArray *attrs = puls_smart_data_get_ata_attributes (data);
    if (attrs) {
        for (guint i = 0; i < attrs->len; i++) {
            PulsSmartAttribute *a = &g_array_index (attrs, PulsSmartAttribute, i);
            if (a->failing_now) {
                g_autofree gchar *msg = g_strdup_printf (
                    "[FAIL] %s: SMART attribute \"%s\" (ID 0x%02x) is failing!",
                    dev, a->name ? a->name : "Unknown", a->id);
                append_alert (self, new_alerts, dev, msg);
                break; /* one toast per refresh is enough */
            }
        }
    }

    if (new_alerts->len == 0) {
        g_ptr_array_free (new_alerts, TRUE);
        return NULL;
    }
    return new_alerts; /* caller must g_ptr_array_free(arr, FALSE) — entries owned by log */
}
