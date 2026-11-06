/*
 * puls-utils.h
 *
 * Utility functions for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
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


gchar *puls_format_bytes       (guint64 bytes);
gchar *puls_format_bytes_exact (guint64 bytes);
gchar *puls_format_hours       (guint64 hours);
gchar *puls_format_temperature (gint    celsius);
gchar *puls_format_number      (guint64 number);


typedef struct {
    gchar *device_path;
    gchar *mount_point;
    gchar *fs_type;
    guint64 total_bytes;
    guint64 available_bytes;
} PulsPartitionInfo;

void   puls_partition_info_free (PulsPartitionInfo *info);
GList *puls_get_disk_partitions     (const gchar *device_path);
GList *puls_get_all_disk_partitions (const gchar *device_path);


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


gboolean puls_detect_smartmontools     (gchar **version_out);
gboolean puls_device_path_is_valid     (const gchar *path);
gchar   *puls_get_helper_path         (void);

PulsDriveType         puls_detect_drive_type          (const gchar *device_path);
PulsTransportProtocol puls_detect_transport_protocol  (const gchar *device_path);
gboolean              puls_is_rotational              (const gchar *device_name);
gchar        *puls_get_transport     (const gchar *device_name);
gchar        *puls_detect_usb_speed_version (const gchar *device_name);

gboolean puls_get_partition_usage (const gchar *mount_point,
                                   guint64     *out_used_bytes,
                                   guint64     *out_total_bytes);

void puls_log_init (void);

void puls_log_structured (const gchar   *domain,
                          GLogLevelFlags level,
                          const gchar   *file,
                          gint           line,
                          const gchar   *func,
                          const gchar   *format,
                          ...) G_GNUC_PRINTF (6, 7);

#ifndef PULS_LOG_DEFAULT_DOMAIN
#define PULS_LOG_DEFAULT_DOMAIN "puls"
#endif

#define PULS_LOG_DEBUG(fmt, ...) \
    puls_log_structured (PULS_LOG_DEFAULT_DOMAIN, G_LOG_LEVEL_DEBUG, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

#define PULS_LOG_INFO(fmt, ...) \
    puls_log_structured (PULS_LOG_DEFAULT_DOMAIN, G_LOG_LEVEL_INFO, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

#define PULS_LOG_WARN(fmt, ...) \
    puls_log_structured (PULS_LOG_DEFAULT_DOMAIN, G_LOG_LEVEL_WARNING, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

#define PULS_LOG_ERROR(fmt, ...) \
    puls_log_structured (PULS_LOG_DEFAULT_DOMAIN, G_LOG_LEVEL_CRITICAL, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

#define PULS_LOG_DOMAIN_DEBUG(domain, fmt, ...) \
    puls_log_structured (domain, G_LOG_LEVEL_DEBUG, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

#define PULS_LOG_DOMAIN_INFO(domain, fmt, ...) \
    puls_log_structured (domain, G_LOG_LEVEL_INFO, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

#define PULS_LOG_DOMAIN_WARN(domain, fmt, ...) \
    puls_log_structured (domain, G_LOG_LEVEL_WARNING, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

#define PULS_LOG_DOMAIN_ERROR(domain, fmt, ...) \
    puls_log_structured (domain, G_LOG_LEVEL_CRITICAL, \
                         __FILE__, __LINE__, G_STRFUNC, fmt, ##__VA_ARGS__)

G_END_DECLS
