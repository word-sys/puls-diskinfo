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


typedef enum {
    PULS_CAPACITY_UNIT_DUAL    = 0, /* Dual: "512.1 GB (476.9 GiB)" */
    PULS_CAPACITY_UNIT_DECIMAL = 1, /* Decimal (SI, base 1000): "512.1 GB" */
    PULS_CAPACITY_UNIT_BINARY  = 2  /* Binary (IEC, base 1024): "476.9 GiB" */
} PulsCapacityUnit;

gchar *puls_format_bytes              (guint64 bytes);
gchar *puls_format_bytes_decimal      (guint64 bytes);
gchar *puls_format_bytes_binary       (guint64 bytes);
gchar *puls_format_bytes_dual         (guint64 bytes);
gchar *puls_format_capacity_unit      (guint64 bytes, PulsCapacityUnit unit);
gchar *puls_format_capacity_unit_exact(guint64 bytes, PulsCapacityUnit unit);
gchar *puls_format_bytes_exact        (guint64 bytes);
gchar *puls_format_hours              (guint64 hours);
gchar *puls_format_temperature        (gint    celsius);
gchar *puls_format_number             (guint64 number);


typedef struct {
    gchar *device_path;
    gchar *mount_point;
    gchar *fs_type;
    guint64 total_bytes;
    guint64 available_bytes;
} PulsPartitionInfo;

void   puls_partition_info_free (PulsPartitionInfo *info);
GList *puls_get_disk_partitions     (const gchar *device_path);
GList *puls_get_disk_partitions_from_mounts (const gchar *device_path,
                                             const gchar *mounts_file);
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
gchar   *puls_run_smartctl_sync_with_options (const gchar *device_path,
                                              gboolean     check_standby,
                                              GError     **error);
gchar   *puls_run_smartctl_sync_with_type    (const gchar *device_path,
                                              gboolean     check_standby,
                                              const gchar *device_type,
                                              GError     **error);

/* Drive Standby / Sleep State Detection (-n standby) */
gboolean       puls_should_use_standby_check     (const gchar *device_path);
gboolean       puls_should_use_standby_check_for_type (const gchar   *device_path,
                                                       PulsDriveType  drive_type,
                                                       gboolean       is_rotational);
gchar         *puls_build_smartctl_read_command  (const gchar *device_path,
                                                  gboolean     check_standby);
gchar         *puls_build_helper_read_command    (const gchar *device_path,
                                                  gboolean     check_standby);

/* Privileged Helper IPC Protocol v2 Framing & Handshake */
gboolean       puls_helper_action_is_valid       (const gchar *action);
gchar         *puls_helper_format_hello          (guint        version);
gchar         *puls_helper_format_read_smart     (const gchar *device_path,
                                                  gboolean     check_standby);
gchar         *puls_helper_format_read_smart_with_type (const gchar *device_path,
                                                        gboolean     check_standby,
                                                        const gchar *device_type);
gchar         *puls_helper_format_action         (const gchar *device_path,
                                                  const gchar *action);
gchar         *puls_helper_format_stream_scan    (const gchar *device_path,
                                                  guint64      start_offset,
                                                  guint64      length);
gchar         *puls_helper_format_stream_scan_chunked (const gchar *device_path,
                                                       guint64      start_offset,
                                                       guint64      length,
                                                       guint64      chunk_bytes);
gboolean       puls_helper_parse_frame_header    (const gchar *header_line,
                                                  gboolean    *out_ok,
                                                  gchar      **out_err_code,
                                                  gsize       *out_payload_len);
gboolean       puls_helper_parse_chunk_header    (const gchar *header_line,
                                                  guint64     *out_seq,
                                                  gsize       *out_payload_len);

typedef struct {
    guint64 seq;
    guint64 offset;
    guint64 chunk_bytes;
    guint64 bytes_read;
    guint64 total_bytes;
    gdouble progress_pct;
    gdouble chunk_ms;
    gdouble mb_per_sec;
} PulsHelperStreamChunk;

typedef gboolean (*PulsHelperStreamCallback)     (guint64      seq,
                                                  const gchar *chunk_payload,
                                                  gpointer     user_data);

gboolean       puls_helper_parse_stream_chunk_json (const gchar           *json_payload,
                                                    PulsHelperStreamChunk *out_chunk);
gboolean       puls_helper_exchange_command      (GOutputStream    *stdin_stream,
                                                  GDataInputStream *stdout_stream,
                                                  const gchar      *command,
                                                  gboolean         *out_ok,
                                                  gchar           **out_err_code,
                                                  gchar           **out_payload,
                                                  GError          **error);
gboolean       puls_helper_exchange_stream       (GOutputStream            *stdin_stream,
                                                  GDataInputStream         *stdout_stream,
                                                  const gchar              *command,
                                                  PulsHelperStreamCallback  chunk_cb,
                                                  gpointer                  user_data,
                                                  gboolean                 *out_ok,
                                                  gchar                   **out_err_code,
                                                  gchar                   **out_final_payload,
                                                  GError                  **error);

/* Privilege Elevation & Sudo / Storage Group Fallback */
typedef enum {
    PULS_ELEVATION_NONE = 0,       /* Unprivileged direct attempt */
    PULS_ELEVATION_DIRECT_ROOT,    /* Running as euid == 0 */
    PULS_ELEVATION_DIRECT_GROUP,   /* User belongs to 'disk' or 'storage' group */
    PULS_ELEVATION_PKEXEC,         /* Polkit pkexec available */
    PULS_ELEVATION_SUDO            /* Non-interactive sudo (-n) fallback */
} PulsElevationMode;

typedef struct {
    gboolean is_root;
    gboolean in_storage_group;
    gboolean polkit_available;
    gboolean sudo_available;
    gboolean is_wsl;
    gboolean is_container;
} PulsElevationCaps;

const gchar      *puls_elevation_mode_to_string      (PulsElevationMode        mode);
gboolean          puls_group_list_has_storage_access (const gchar * const     *group_names);
gboolean          puls_user_in_storage_group         (void);
gboolean          puls_is_polkit_available           (void);
gboolean          puls_is_sudo_available             (void);
void              puls_detect_elevation_caps         (PulsElevationCaps       *out_caps);
PulsElevationMode puls_resolve_elevation_mode        (const PulsElevationCaps *caps);
PulsElevationMode puls_get_elevation_mode            (void);
gchar           **puls_helper_build_spawn_argv       (PulsElevationMode        mode,
                                                      const gchar             *helper_bin);

/* Zero-Elevation Mode for Group-Readable / Direct-Accessible Device Nodes */
typedef gchar *(*PulsSmartctlDirectFunc)             (const gchar             *device_path,
                                                      gboolean                 check_standby,
                                                      gpointer                 user_data,
                                                      GError                 **error);
typedef gchar *(*PulsSmartctlHelperFunc)             (const gchar             *device_path,
                                                      gboolean                 check_standby,
                                                      gpointer                 user_data,
                                                      GError                 **error);

gboolean          puls_node_has_direct_rw_access     (const gchar             *path);
gboolean          puls_device_supports_zero_elevation(const gchar             *device_path);
gboolean          puls_smartctl_output_is_permission_denied (const gchar      *output);
gchar            *puls_execute_smartctl_with_zero_elevation (const gchar            *device_path,
                                                             gboolean                check_standby,
                                                             gboolean                allow_direct_rw,
                                                             PulsSmartctlDirectFunc  direct_fn,
                                                             PulsSmartctlHelperFunc  helper_fn,
                                                             gpointer                user_data,
                                                             gboolean               *out_used_direct,
                                                             GError                **error);

/* Privileged Helper Watchdog, Crash Recovery & Zombie Cleanup */
typedef struct _PulsHelperClient PulsHelperClient;

typedef struct {
    GPid              pid;
    gboolean          running;
    guint             launch_count;
    guint             crash_count;
    guint             reaped_count;
    PulsElevationMode elevation_mode;
} PulsHelperStats;

PulsHelperClient *puls_helper_client_new            (const gchar      *helper_path,
                                                     gboolean          use_pkexec);
PulsHelperClient *puls_helper_client_new_with_mode  (const gchar      *helper_path,
                                                     PulsElevationMode mode);
void              puls_helper_client_free           (PulsHelperClient *client);
gboolean          puls_helper_client_ensure_running (PulsHelperClient *client,
                                                     GError          **error);
gboolean          puls_helper_client_watchdog_check (PulsHelperClient *client);
void              puls_helper_client_watchdog_start (PulsHelperClient *client,
                                                     guint             interval_ms);
void              puls_helper_client_watchdog_stop  (PulsHelperClient *client);
gboolean          puls_helper_client_exchange       (PulsHelperClient *client,
                                                     const gchar      *command,
                                                     gboolean         *out_ok,
                                                     gchar           **out_err_code,
                                                     gchar           **out_payload,
                                                     GError          **error);
gboolean          puls_helper_client_stream         (PulsHelperClient         *client,
                                                     const gchar              *command,
                                                     PulsHelperStreamCallback  chunk_cb,
                                                     gpointer                  user_data,
                                                     gboolean                 *out_ok,
                                                     gchar                   **out_err_code,
                                                     gchar                   **out_final_payload,
                                                     GError                  **error);
void              puls_helper_client_shutdown       (PulsHelperClient *client);
void              puls_helper_client_get_stats      (PulsHelperClient *client,
                                                     PulsHelperStats  *out_stats);

gboolean          puls_helper_watchdog_check        (void);
void              puls_helper_watchdog_start        (guint             interval_ms);
void              puls_helper_watchdog_stop         (void);
void              puls_helper_shutdown              (void);
void              puls_helper_get_stats             (PulsHelperStats  *out_stats);
gboolean          puls_run_stream_scan_sync         (const gchar              *device_path,
                                                     guint64                   start_offset,
                                                     guint64                   length,
                                                     guint64                   chunk_bytes,
                                                     PulsHelperStreamCallback  chunk_cb,
                                                     gpointer                  user_data,
                                                     gchar                   **out_summary_json,
                                                     GError                  **error);

/* Polkit Policy XML Introspection & Validation */
typedef struct {
    gchar *id;
    gchar *description_default;
    gchar *description_en;
    gchar *description_tr;
    gchar *message_default;
    gchar *message_en;
    gchar *message_tr;
    gchar *icon_name;
    gchar *allow_any;
    gchar *allow_inactive;
    gchar *allow_active;
    gchar *exec_path;
    gchar *allow_gui;
    gchar *imply;
} PulsPolkitActionInfo;

void           puls_polkit_action_info_free      (PulsPolkitActionInfo *info);
gboolean       puls_polkit_parse_policy_xml      (const gchar          *xml_content,
                                                  GPtrArray           **out_actions,
                                                  GError              **error);
gboolean       puls_polkit_validate_policy_xml   (const gchar          *xml_content,
                                                  GPtrArray           **out_actions,
                                                  GError              **error);
gboolean       puls_polkit_validate_policy_file  (const gchar          *policy_path,
                                                  GPtrArray           **out_actions,
                                                  GError              **error);

PulsPowerState puls_parse_hdparm_power_state     (const gchar *hdparm_output);
PulsPowerState puls_parse_smartctl_standby_json  (const gchar *json_str);

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


/* smartmontools Feature & Version Negotiation */
typedef struct {
    gboolean available;
    guint    major;
    guint    minor;
    guint    patch;
    guint    svn_rev;
    gchar    version_string[32];
    gchar    release_date[16];
    gchar    raw_banner[128];
    gboolean supports_json;         /* smartctl >= 7.0 (`-j` / `--json`, JSON v1.0) */
    guint    json_schema_major;     /* 1 when supports_json, else 0 */
    guint    json_schema_minor;     /* 0 when supports_json, else 0 */
    gboolean supports_sat;          /* smartctl >= 5.39 (`-d sat`) */
    gboolean supports_sntjmicron;   /* smartctl >= 7.1 (`-d sntjmicron`) */
    gboolean supports_sntasmedia;   /* smartctl >= 7.2 (`-d sntasmedia`) */
    gboolean supports_sntrealtek;   /* smartctl >= 7.2 (`-d sntrealtek`) */
    gboolean supports_json_compact; /* smartctl >= 7.3 (`--json=c`) */
} PulsSmartctlCaps;

gboolean                puls_detect_smartmontools                (gchar                 **version_out);
gboolean                puls_parse_smartctl_version              (const gchar            *version_output,
                                                                  PulsSmartctlCaps       *out_caps);
gboolean                puls_smartctl_version_at_least           (const PulsSmartctlCaps *caps,
                                                                  guint                   req_major,
                                                                  guint                   req_minor);
gboolean                puls_smartctl_probe_capabilities         (PulsSmartctlCaps       *out_caps);
const PulsSmartctlCaps *puls_smartctl_get_capabilities           (void);
gchar                  *puls_smartctl_format_capabilities_summary(const PulsSmartctlCaps *caps);
gboolean                puls_smartctl_parse_json_format_version  (const gchar            *json_str,
                                                                  guint                  *out_major,
                                                                  guint                  *out_minor);
gchar                  *puls_build_smartctl_read_command_for_caps(const gchar            *device_path,
                                                                  gboolean                check_standby,
                                                                  const PulsSmartctlCaps *caps);
gchar                  *puls_build_smartctl_read_command_with_type(const gchar            *device_path,
                                                                   gboolean                check_standby,
                                                                   const gchar            *device_type,
                                                                   const PulsSmartctlCaps *caps);
gboolean puls_device_path_is_valid          (const gchar *path);
gboolean puls_validate_device_node_in_dir   (const gchar *dev_root,
                                             const gchar *device_path,
                                             GError     **error);
gboolean puls_validate_device_node          (const gchar *device_path,
                                             GError     **error);
gchar   *puls_get_helper_path               (void);

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

/* NVMe Controller vs Namespace Topology */
typedef struct {
    gboolean is_nvme;
    gchar   *controller_name;  /* e.g. "nvme0" */
    gchar   *controller_path;  /* e.g. "/dev/nvme0" (target for SMART queries) */
    gchar   *namespace_name;   /* e.g. "nvme0n1" */
    gchar   *namespace_path;   /* e.g. "/dev/nvme0n1" (target for mounts/partitions) */
    guint32  nsid;             /* Namespace ID (e.g. 1) */
    gint     cntlid;           /* Controller ID (-1 if unknown) */
    gchar   *transport;        /* Controller transport ("pcie", "tcp", "rdma", etc.) */
    gchar   *subsysnqn;        /* NVMe Qualified Name from controller sysfs */
    gchar   *eui64;            /* Namespace EUI-64 identifier */
    gchar   *nguid;            /* Namespace NGUID identifier */
    gchar   *wwid;             /* Namespace WWID identifier */
    gchar   *state;            /* Controller state (e.g. "live") */
} PulsNvmeTopology;

void     puls_nvme_topology_clear         (PulsNvmeTopology  *topo);
gboolean puls_nvme_is_controller_name     (const gchar       *name_or_path);
gboolean puls_nvme_is_namespace_name      (const gchar       *name_or_path);
gboolean puls_nvme_parse_device_name      (const gchar       *name_or_path,
                                           gchar            **out_controller_name,
                                           guint32           *out_nsid);
gchar   *puls_nvme_resolve_controller_path (const gchar      *device_path);
gchar   *puls_nvme_resolve_smart_target   (const gchar       *device_path);
gboolean puls_nvme_read_topology_from_dir (const gchar       *block_dir,
                                           PulsNvmeTopology  *out_topo);
gboolean puls_nvme_read_topology          (const gchar       *device_path,
                                           PulsNvmeTopology  *out_topo);

/* SCSI Generic (/dev/sg*) <-> Block (/dev/sd*) Mapping */
typedef struct {
    gchar *sg_name;        /* e.g. "sg0" */
    gchar *sg_path;        /* e.g. "/dev/sg0" */
    gchar *block_name;     /* e.g. "sda" */
    gchar *block_path;     /* e.g. "/dev/sda" */
    gchar *scsi_address;   /* H:B:T:L tuple e.g. "0:0:0:0" */
    gchar *vendor;         /* SCSI vendor string */
    gchar *model;          /* SCSI model string */
    gchar *rev;            /* SCSI revision string */
    gint   scsi_type;      /* SCSI peripheral device type (-1 if unknown) */
} PulsScsiGenericInfo;

void     puls_scsi_generic_info_clear               (PulsScsiGenericInfo *info);
void     puls_scsi_generic_info_free                (PulsScsiGenericInfo *info);
gboolean puls_scsi_is_generic_name                  (const gchar         *name_or_path);
gboolean puls_scsi_parse_hbtl_address               (const gchar         *sysfs_path,
                                                     gchar              **out_hbtl);
gchar   *puls_scsi_generic_to_block_from_sysfs      (const gchar         *sysfs_root,
                                                     const gchar         *sg_device);
gchar   *puls_scsi_generic_to_block                 (const gchar         *sg_device);
gchar   *puls_scsi_block_to_generic_from_sysfs      (const gchar         *sysfs_root,
                                                     const gchar         *block_device);
gchar   *puls_scsi_block_to_generic                 (const gchar         *block_device);
gboolean puls_scsi_read_generic_info_from_sysfs     (const gchar         *sysfs_root,
                                                     const gchar         *sg_device,
                                                     PulsScsiGenericInfo *out_info);
gboolean puls_scsi_read_generic_info                (const gchar         *sg_device,
                                                     PulsScsiGenericInfo *out_info);
GList   *puls_scsi_list_generic_mappings_from_sysfs (const gchar         *sysfs_root);
GList   *puls_scsi_list_generic_mappings            (void);

/* Software RAID (mdadm) Member Resolution */
typedef struct {
    gchar *array_name;     /* e.g. "md0" */
    gchar *array_path;     /* e.g. "/dev/md0" */
    gchar *level;          /* Normalized level e.g. "RAID1", "RAID5", "RAID10" */
    gchar *array_state;    /* e.g. "clean", "active" */
    gchar *member_device;  /* Slave name e.g. "sda1" or "sda" */
    gchar *member_state;   /* e.g. "in_sync", "spare", "faulty" */
    gchar *sync_action;    /* e.g. "idle", "resync", "recover" */
    gint   raid_disks;     /* Total array disk slots (-1 if unknown) */
    gint   degraded_count; /* Number of degraded/missing disks (-1 if unknown) */
    gchar *status_text;    /* Formatted e.g. "Member of /dev/md0 [RAID1]" */
} PulsRaidMemberInfo;

void     puls_raid_member_info_clear           (PulsRaidMemberInfo       *info);
void     puls_raid_member_info_free            (PulsRaidMemberInfo       *info);
gboolean puls_raid_is_md_device_name           (const gchar              *name);
gchar   *puls_raid_format_member_status        (const PulsRaidMemberInfo *info);
gboolean puls_raid_detect_member_from_sysfs    (const gchar              *sys_block_dir,
                                                const gchar              *device_path,
                                                PulsRaidMemberInfo       *out_info);
gboolean puls_raid_detect_member               (const gchar              *device_path,
                                                PulsRaidMemberInfo       *out_info);
GList   *puls_raid_list_memberships_from_sysfs (const gchar              *sys_block_dir,
                                                const gchar              *device_path);
GList   *puls_raid_list_memberships            (const gchar              *device_path);

/* ZFS Pool Physical Member Detection */
typedef struct {
    gchar   *pool_name;      /* e.g. "tank", "rpool" */
    gchar   *pool_state;     /* e.g. "ONLINE", "DEGRADED" */
    gchar   *vdev_type;      /* e.g. "RAIDZ1", "MIRROR", "STRIPE", "LOG", "CACHE", "SPARE" */
    gchar   *member_name;    /* Raw entry name from zpool status */
    gchar   *member_state;   /* e.g. "ONLINE", "DEGRADED", "FAULTED", "AVAIL" */
    guint64  read_errors;
    guint64  write_errors;
    guint64  cksum_errors;
    gchar   *status_text;    /* Formatted e.g. "ZFS Pool: tank [RAIDZ1, ONLINE]" */
} PulsZfsMemberInfo;

void     puls_zfs_member_info_clear            (PulsZfsMemberInfo       *info);
void     puls_zfs_member_info_free             (PulsZfsMemberInfo       *info);
gchar   *puls_zfs_format_member_status         (const PulsZfsMemberInfo *info);
gboolean puls_zfs_has_label_signature          (const guint8            *data,
                                                gsize                    len);
GList   *puls_zfs_list_memberships_from_status (const gchar             *zpool_status_text,
                                                const gchar             *by_id_dir,
                                                const gchar             *device_path);
gboolean puls_zfs_detect_member_from_status    (const gchar             *zpool_status_text,
                                                const gchar             *by_id_dir,
                                                const gchar             *device_path,
                                                PulsZfsMemberInfo       *out_info);
gboolean puls_zfs_detect_member                (const gchar             *device_path,
                                                PulsZfsMemberInfo       *out_info);

/* Physical Form Factor Detection Engine */
PulsFormFactor puls_detect_form_factor                (const gchar          *smartctl_ff,
                                                       const gchar          *model_name,
                                                       const gchar          *device_path,
                                                       PulsDriveType         drive_type,
                                                       PulsTransportProtocol transport,
                                                       gint                  rotation_rpm,
                                                       guint64               capacity_bytes,
                                                       guint8                pcie_max_width);
PulsFormFactor puls_detect_form_factor_for_smart_data (PulsSmartData        *data);

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
