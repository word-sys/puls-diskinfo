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

/* Hardened Sysfs Helpers */
#define PULS_SYSFS_DEFAULT_MAX_LEN 4096

gchar   *puls_sanitize_device_name     (const gchar *device_path);
gchar   *puls_sysfs_get_block_path     (const gchar *device_name, const gchar *subpath);
gchar   *puls_sysfs_read_string        (const gchar *path, gsize max_len);
guint64  puls_sysfs_read_uint64        (const gchar *path, guint64 default_val, gboolean *ok);
gint64   puls_sysfs_read_int64         (const gchar *path, gint64 default_val, gboolean *ok);

gchar   *puls_sysfs_read_device_file   (const gchar *device_name, const gchar *subpath, gsize max_len);
guint64  puls_sysfs_read_device_uint64 (const gchar *device_name, const gchar *subpath, guint64 default_val);
gint64   puls_sysfs_read_device_int64  (const gchar *device_name, const gchar *subpath, gint64 default_val);

gchar   *puls_sysfs_resolve_link       (const gchar *path);

/* Direct Unprivileged hwmon Temperature Readers */
gchar  *puls_sysfs_find_hwmon_dir              (const gchar *device_name);
gint    puls_sysfs_read_hwmon_temp             (const gchar *device_name);
GList  *puls_sysfs_read_hwmon_sensors          (const gchar *device_name);
gint    puls_sysfs_read_hwmon_temp_from_dir    (const gchar *hwmon_dir);
GList  *puls_sysfs_read_hwmon_sensors_from_dir (const gchar *hwmon_dir);

/* Queue Attributes and TRIM / Discard Detection */
typedef struct {
    guint64  discard_granularity;   /* bytes (0 if unsupported) */
    guint64  discard_max_bytes;     /* bytes (0 if unsupported) */
    guint64  discard_max_hw_bytes;  /* bytes (0 if unsupported) */
    gboolean supports_discard;      /* TRUE if discard_max_bytes > 0 && discard_granularity > 0 */
    gboolean rotational;            /* TRUE = HDD/rotational, FALSE = SSD/non-rotational */
    guint32  logical_block_size;    /* bytes (e.g. 512, 4096) */
    guint32  physical_block_size;   /* bytes (e.g. 512, 4096) */
    guint64  optimal_io_size;       /* bytes */
    guint64  minimum_io_size;       /* bytes */
    gchar    write_cache[32];       /* "write back", "write through", "none" */
    gboolean write_cache_enabled;   /* TRUE if write caching is enabled */
    guint32  max_segments;          /* max segments in request */
    guint64  max_sectors_kb;        /* max sectors in KB */
} PulsQueueAttributes;

guint64  puls_sysfs_read_queue_uint64               (const gchar *device_name,
                                                     const gchar *attribute,
                                                     guint64      default_val);
gchar   *puls_sysfs_read_queue_string               (const gchar *device_name,
                                                     const gchar *attribute,
                                                     gsize        max_len);
gboolean puls_sysfs_read_queue_attributes          (const gchar         *device_name,
                                                     PulsQueueAttributes *out_attrs);
gboolean puls_sysfs_read_queue_attributes_from_dir (const gchar         *queue_dir,
                                                     PulsQueueAttributes *out_attrs);
gboolean puls_sysfs_detect_trim_support            (const gchar *device_name);

/* PCIe Link Speed and Width Inspector */
typedef struct {
    gchar              *device_dir;
    PulsPcieGeneration  current_gen;
    guint8              current_width;
    gchar               current_speed_raw[32];
    PulsPcieGeneration  max_gen;
    guint8              max_width;
    gchar               max_speed_raw[32];
    gboolean            is_downgraded;
    gchar              *description;
} PulsPcieLinkInfo;

void     puls_pcie_link_info_clear          (PulsPcieLinkInfo *info);
gchar   *puls_format_pcie_link              (PulsPcieGeneration current_gen,
                                             guint8             current_width,
                                             PulsPcieGeneration max_gen,
                                             guint8             max_width);
gchar   *puls_sysfs_find_pcie_dir           (const gchar *device_name);
gboolean puls_sysfs_read_pcie_link_from_dir (const gchar      *pci_dir,
                                             PulsPcieLinkInfo *out_info);
gboolean puls_sysfs_read_pcie_link          (const gchar      *device_name,
                                             PulsPcieLinkInfo *out_info);

/* USB Speed and Bridge Controller Inspector */
typedef struct {
    gchar       *device_dir;       /* USB device directory in sysfs */
    PulsUsbSpeed speed;            /* enum classification */
    guint64      speed_mbps;       /* speed in Mbps (e.g. 480, 5000, 10000, 20000, 40000) */
    gchar       *version;          /* USB spec version (e.g. "3.10", "2.00") */
    gchar       *vendor_id;        /* 4-digit hex vendor ID (e.g. "174c") */
    gchar       *product_id;       /* 4-digit hex product ID (e.g. "2362") */
    gchar       *manufacturer;     /* Manufacturer descriptor string */
    gchar       *product;          /* Product descriptor string */
    gchar       *serial;           /* USB serial number string */
    gchar       *bridge_chip;      /* Identified bridge IC (e.g. "ASMedia ASM2362 NVMe-USB Bridge") */
    gchar       *display_string;   /* Combined badge & bridge string (e.g. "USB 3.1 (10G) | ASMedia ASM2362") */
} PulsUsbBridgeInfo;

void     puls_usb_bridge_info_clear               (PulsUsbBridgeInfo *info);
gchar   *puls_identify_usb_bridge_chip            (const gchar *vendor_id,
                                                   const gchar *product_id,
                                                   const gchar *manufacturer,
                                                   const gchar *product);
gchar   *puls_sysfs_find_usb_device_dir           (const gchar *device_name);
gboolean puls_sysfs_read_usb_bridge_info_from_dir (const gchar       *usb_dir,
                                                   PulsUsbBridgeInfo *out_info);
gboolean puls_sysfs_read_usb_bridge_info          (const gchar       *device_name,
                                                   PulsUsbBridgeInfo *out_info);

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
