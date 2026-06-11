/*
 * puls-utils.h
 *
 * Utility functions for Puls DiskInfo.
 *
 * Copyright (C) 2024 Puls DiskInfo Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include "puls-smart-data.h"

G_BEGIN_DECLS

/* ── Formatting ────────────────────────────────────────────── */

gchar *puls_format_bytes       (guint64 bytes);
gchar *puls_format_bytes_exact (guint64 bytes);
gchar *puls_format_hours       (guint64 hours);
gchar *puls_format_temperature (gint    celsius);
gchar *puls_format_number      (guint64 number);

/* ── Partition / Mount Information ────────────────────────── */

typedef struct {
    gchar *device_path;
    gchar *mount_point;
    gchar *fs_type;
    guint64 total_bytes;
    guint64 available_bytes;
} PulsPartitionInfo;

void   puls_partition_info_free (PulsPartitionInfo *info);
GList *puls_get_disk_partitions  (const gchar *device_path);

/* ── smartctl Execution ────────────────────────────────────── */

typedef void (*PulsSmartctlCallback) (const gchar *json_output,
                                      GError      *error,
                                      gpointer     user_data);

void     puls_run_smartctl_async  (const gchar          *device_path,
                                   GCancellable         *cancellable,
                                   GAsyncReadyCallback   callback,
                                   gpointer              user_data);

gchar   *puls_run_smartctl_finish (GAsyncResult *result,
                                   GError      **error);

gchar   *puls_run_smartctl_sync   (const gchar *device_path,
                                   GError     **error);

void     puls_run_smartctl_action_async  (const gchar          *device_path,
                                          const gchar          *action,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);

gchar   *puls_run_smartctl_action_finish (GAsyncResult *result,
                                          GError      **error);

gchar   *puls_run_smartctl_action_sync   (const gchar          *device_path,
                                          const gchar          *action,
                                          GError              **error);

/* ── System Detection ──────────────────────────────────────── */

gboolean puls_detect_smartmontools     (gchar **version_out);
gboolean puls_device_path_is_valid     (const gchar *path);
gchar   *puls_get_helper_path         (void);

/* ── Drive Type Detection ──────────────────────────────────── */

PulsDriveType puls_detect_drive_type (const gchar *device_path);
gboolean      puls_is_rotational     (const gchar *device_name);
gchar        *puls_get_transport     (const gchar *device_name);

G_END_DECLS
