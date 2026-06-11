/*
 * puls-disk-manager.c
 *
 * Disk enumeration via /sys/block + smartctl, with D-Bus/sysfs fallback.
 *
 * Copyright (C) 2024 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-disk-manager.h"
#include "puls-smart-parser.h"
#include "puls-utils.h"
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

enum {
    SIGNAL_DISK_ADDED,
    SIGNAL_DISK_REMOVED,
    SIGNAL_DISK_UPDATED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

struct _PulsDiskManager {
    GObject parent_instance;

    GHashTable *devices;
    GList *device_paths;
};

G_DEFINE_TYPE (PulsDiskManager, puls_disk_manager, G_TYPE_OBJECT)

static gboolean
is_real_disk (const gchar *name)
{
    if (g_str_has_prefix (name, "loop") ||
        g_str_has_prefix (name, "ram") ||
        g_str_has_prefix (name, "dm-") ||
        g_str_has_prefix (name, "sr") ||
        g_str_has_prefix (name, "fd") ||
        g_str_has_prefix (name, "zram"))
        return FALSE;

    if (g_str_has_prefix (name, "sd") && strlen (name) > 3) {
        const gchar *suffix = name + 2;
        while (*suffix && g_ascii_isalpha (*suffix))
            suffix++;
        if (*suffix && g_ascii_isdigit (*suffix))
            return FALSE;
    }

    if (g_str_has_prefix (name, "nvme")) {
        if (strstr (name, "p") != NULL) {
            const gchar *n_pos = strstr (name + 4, "n");
            if (n_pos) {
                const gchar *p_pos = strstr (n_pos, "p");
                if (p_pos && g_ascii_isdigit (*(p_pos + 1)))
                    return FALSE;
            }
        }
    }

    if (g_str_has_prefix (name, "mmcblk")) {
        if (strstr (name, "p") != NULL) {
            const gchar *p_pos = strstr (name + 6, "p");
            if (p_pos && g_ascii_isdigit (*(p_pos + 1)))
                return FALSE;
        }
    }

    if (g_str_has_prefix (name, "sd") ||
        g_str_has_prefix (name, "nvme") ||
        g_str_has_prefix (name, "hd") ||
        g_str_has_prefix (name, "vd") ||
        g_str_has_prefix (name, "mmcblk"))
        return TRUE;

    return FALSE;
}

static GList *
scan_sys_block (void)
{
    GList *devices = NULL;
    GDir *dir = g_dir_open ("/sys/block", 0, NULL);

    if (dir == NULL)
        return NULL;

    const gchar *name;
    while ((name = g_dir_read_name (dir)) != NULL) {
        if (is_real_disk (name)) {
            gchar *dev_path = g_strdup_printf ("/dev/%s", name);
            devices = g_list_append (devices, dev_path);
        }
    }

    g_dir_close (dir);
    devices = g_list_sort (devices, (GCompareFunc)g_strcmp0);

    return devices;
}

static void
puls_disk_manager_finalize (GObject *object)
{
    PulsDiskManager *self = PULS_DISK_MANAGER (object);

    g_hash_table_destroy (self->devices);
    g_list_free_full (self->device_paths, g_free);

    G_OBJECT_CLASS (puls_disk_manager_parent_class)->finalize (object);
}

static void
puls_disk_manager_class_init (PulsDiskManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = puls_disk_manager_finalize;

    signals[SIGNAL_DISK_ADDED] =
        g_signal_new ("disk-added",
                       G_TYPE_FROM_CLASS (klass),
                       G_SIGNAL_RUN_LAST,
                       0, NULL, NULL, NULL,
                       G_TYPE_NONE, 1, G_TYPE_STRING);

    signals[SIGNAL_DISK_REMOVED] =
        g_signal_new ("disk-removed",
                       G_TYPE_FROM_CLASS (klass),
                       G_SIGNAL_RUN_LAST,
                       0, NULL, NULL, NULL,
                       G_TYPE_NONE, 1, G_TYPE_STRING);

    signals[SIGNAL_DISK_UPDATED] =
        g_signal_new ("disk-updated",
                       G_TYPE_FROM_CLASS (klass),
                       G_SIGNAL_RUN_LAST,
                       0, NULL, NULL, NULL,
                       G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
puls_disk_manager_init (PulsDiskManager *self)
{
    self->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_object_unref);
    self->device_paths = NULL;
}

PulsDiskManager *
puls_disk_manager_new (void)
{
    return g_object_new (PULS_TYPE_DISK_MANAGER, NULL);
}

void
puls_disk_manager_scan (PulsDiskManager *self)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (self));

    GList *found = scan_sys_block ();

    for (GList *l = found; l != NULL; l = l->next) {
        const gchar *dev_path = l->data;
        GError *error = NULL;

        if (!g_hash_table_contains (self->devices, dev_path)) {
            g_autofree gchar *json = puls_run_smartctl_sync (dev_path, &error);

            PulsSmartData *data = NULL;
            if (json) {
                data = puls_smart_parser_parse_json (json, &error);
            }

            if (data == NULL) {
                data = puls_smart_data_new ();
                puls_smart_data_set_device_path (data, dev_path);
                puls_smart_data_set_drive_type (data, puls_detect_drive_type (dev_path));

                const gchar *base = strrchr (dev_path, '/');
                if (base) base++; else base = dev_path;

                g_autofree gchar *model_path = g_strdup_printf (
                    "/sys/block/%s/device/model", base);
                g_autofree gchar *model = NULL;
                if (g_file_get_contents (model_path, &model, NULL, NULL)) {
                    g_strstrip (model);
                    puls_smart_data_set_model_name (data, model);
                }
            }

            g_hash_table_insert (self->devices, g_strdup (dev_path), data);
            self->device_paths = g_list_append (self->device_paths,
                                                g_strdup (dev_path));
            g_signal_emit (self, signals[SIGNAL_DISK_ADDED], 0, dev_path);
        }

        g_clear_error (&error);
    }

    g_list_free_full (found, g_free);
}

GList *
puls_disk_manager_get_devices (PulsDiskManager *self)
{
    g_return_val_if_fail (PULS_IS_DISK_MANAGER (self), NULL);
    return self->device_paths;
}

guint
puls_disk_manager_get_device_count (PulsDiskManager *self)
{
    g_return_val_if_fail (PULS_IS_DISK_MANAGER (self), 0);
    return g_hash_table_size (self->devices);
}

PulsSmartData *
puls_disk_manager_get_smart_data (PulsDiskManager *self,
                                  const gchar     *device_path)
{
    g_return_val_if_fail (PULS_IS_DISK_MANAGER (self), NULL);
    g_return_val_if_fail (device_path != NULL, NULL);

    return g_hash_table_lookup (self->devices, device_path);
}

typedef struct {
    PulsDiskManager *manager;
    gchar *device_path;
} RefreshData;

static void
refresh_data_free (RefreshData *rd)
{
    g_free (rd->device_path);
    g_free (rd);
}

static void
on_smartctl_done (GObject      *source G_GNUC_UNUSED,
                 GAsyncResult *result,
                 gpointer      user_data)
{
    GTask *task = G_TASK (user_data);
    RefreshData *rd = g_task_get_task_data (task);
    GError *error = NULL;

    g_autofree gchar *json = puls_run_smartctl_finish (result, &error);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    PulsSmartData *data = puls_smart_parser_parse_json (json, &error);
    if (!data) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_hash_table_replace (rd->manager->devices,
                          g_strdup (rd->device_path),
                          data);

    g_signal_emit (rd->manager, signals[SIGNAL_DISK_UPDATED], 0,
                   rd->device_path);

    g_task_return_pointer (task, g_object_ref (data), g_object_unref);
    g_object_unref (task);
}

void
puls_disk_manager_refresh_async (PulsDiskManager     *self,
                                  const gchar         *device_path,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (self));

    GTask *task = g_task_new (self, cancellable, callback, user_data);

    RefreshData *rd = g_new0 (RefreshData, 1);
    rd->manager = self;
    rd->device_path = g_strdup (device_path);
    g_task_set_task_data (task, rd, (GDestroyNotify)refresh_data_free);

    puls_run_smartctl_async (device_path, cancellable, on_smartctl_done, task);
}

PulsSmartData *
puls_disk_manager_refresh_finish (PulsDiskManager *self G_GNUC_UNUSED,
                                   GAsyncResult    *result,
                                   GError         **error)
{
    return g_task_propagate_pointer (G_TASK (result), error);
}
