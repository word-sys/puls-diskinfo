/*
 * puls-smart-data.h
 *
 * S.M.A.R.T. data structures for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS


typedef enum {
    PULS_HEALTH_GOOD,
    PULS_HEALTH_CAUTION,
    PULS_HEALTH_BAD,
    PULS_HEALTH_UNKNOWN
} PulsHealthStatus;

typedef enum {
    PULS_POWER_STATE_UNKNOWN = 0,
    PULS_POWER_STATE_ACTIVE,
    PULS_POWER_STATE_IDLE,
    PULS_POWER_STATE_STANDBY,
    PULS_POWER_STATE_SLEEP
} PulsPowerState;

typedef enum {
    PULS_DRIVE_TYPE_UNKNOWN = 0,
    PULS_DRIVE_TYPE_NVME_SSD,
    PULS_DRIVE_TYPE_SATA_SSD,
    PULS_DRIVE_TYPE_SATA_HDD,
    PULS_DRIVE_TYPE_SAS_SSD,
    PULS_DRIVE_TYPE_SAS_HDD,
    PULS_DRIVE_TYPE_USB,
    PULS_DRIVE_TYPE_SSHD,
    PULS_DRIVE_TYPE_EMMC,
    PULS_DRIVE_TYPE_SD_CARD,
    PULS_DRIVE_TYPE_VIRTUAL
} PulsDriveType;

#define PULS_DRIVE_TYPE_HDD         PULS_DRIVE_TYPE_SATA_HDD
#define PULS_DRIVE_TYPE_USB_STORAGE PULS_DRIVE_TYPE_USB
#define PULS_DRIVE_TYPE_NVME        PULS_DRIVE_TYPE_NVME_SSD

typedef enum {
    PULS_TRANSPORT_UNKNOWN = 0,
    PULS_TRANSPORT_PCIE,
    PULS_TRANSPORT_SATA,
    PULS_TRANSPORT_SAS,
    PULS_TRANSPORT_USB,
    PULS_TRANSPORT_MMC,
    PULS_TRANSPORT_VIRTUAL
} PulsTransportProtocol;

#define PULS_TRANSPORT_NVME PULS_TRANSPORT_PCIE

typedef enum {
    PULS_PCIE_GEN_UNKNOWN = 0,
    PULS_PCIE_GEN_1       = 1,
    PULS_PCIE_GEN_2       = 2,
    PULS_PCIE_GEN_3       = 3,
    PULS_PCIE_GEN_4       = 4,
    PULS_PCIE_GEN_5       = 5,
    PULS_PCIE_GEN_6       = 6
} PulsPcieGeneration;

typedef enum {
    PULS_SECTOR_FORMAT_UNKNOWN = 0,
    PULS_SECTOR_FORMAT_512N,
    PULS_SECTOR_FORMAT_512E,
    PULS_SECTOR_FORMAT_4KN,
    PULS_SECTOR_FORMAT_OTHER
} PulsSectorFormat;

typedef enum {
    PULS_FORM_FACTOR_UNKNOWN = 0,
    PULS_FORM_FACTOR_M2,
    PULS_FORM_FACTOR_M2_2230,
    PULS_FORM_FACTOR_M2_2242,
    PULS_FORM_FACTOR_M2_2260,
    PULS_FORM_FACTOR_M2_2280,
    PULS_FORM_FACTOR_M2_22110,
    PULS_FORM_FACTOR_2_5_INCH,
    PULS_FORM_FACTOR_3_5_INCH,
    PULS_FORM_FACTOR_1_8_INCH,
    PULS_FORM_FACTOR_U2_U3,
    PULS_FORM_FACTOR_PCIE_AIC,
    PULS_FORM_FACTOR_MSATA,
    PULS_FORM_FACTOR_EDSFF,
    PULS_FORM_FACTOR_USB_EXTERNAL,
    PULS_FORM_FACTOR_EMBEDDED_BGA,
    PULS_FORM_FACTOR_SD_CARD,
    PULS_FORM_FACTOR_VIRTUAL
} PulsFormFactor;

typedef enum {
    PULS_USB_SPEED_UNKNOWN = 0,
    PULS_USB_SPEED_LOW,           /* 1.5 Mbps (USB 1.0 Low-Speed) */
    PULS_USB_SPEED_FULL,          /* 12 Mbps (USB 1.1 Full-Speed) */
    PULS_USB_SPEED_HIGH,          /* 480 Mbps (USB 2.0 High-Speed) */
    PULS_USB_SPEED_SUPER,         /* 5000 Mbps / 5 Gbps (USB 3.0 / USB 3.2 Gen 1 SuperSpeed) */
    PULS_USB_SPEED_SUPER_PLUS,    /* 10000 Mbps / 10 Gbps (USB 3.1 / USB 3.2 Gen 2 SuperSpeed+) */
    PULS_USB_SPEED_SUPER_PLUS_X2, /* 20000 Mbps / 20 Gbps (USB 3.2 Gen 2x2 SuperSpeed++) */
    PULS_USB_SPEED_USB4_40G,      /* 40000 Mbps / 40 Gbps (USB4 40Gbps) */
    PULS_USB_SPEED_USB4_80G       /* 80000 Mbps / 80 Gbps (USB4 80Gbps / USB4 2.0) */
} PulsUsbSpeed;

typedef struct {
    gchar *name;
    gint   temperature;
    gint   warning_temp;
    gint   critical_temp;
} PulsThermalSensor;

PulsThermalSensor *puls_thermal_sensor_new  (const gchar *name,
                                             gint temperature,
                                             gint warning_temp,
                                             gint critical_temp);
PulsThermalSensor *puls_thermal_sensor_copy (const PulsThermalSensor *sensor);
void               puls_thermal_sensor_free (PulsThermalSensor *sensor);

typedef struct {
    guint8   id;
    gchar   *name;
    gint     current;
    gint     worst;
    gint     threshold;
    guint64  raw_value;
    gchar   *raw_string;
    gboolean failed_past;
    gboolean failing_now;
} PulsSmartAttribute;

PulsSmartAttribute *puls_smart_attribute_copy (const PulsSmartAttribute *attr);
void                puls_smart_attribute_free (PulsSmartAttribute *attr);

typedef struct {
    guint8   critical_warning;
    gint     temperature;
    guint8   available_spare;
    guint8   available_spare_threshold;
    guint8   percentage_used;
    guint64  data_units_read;
    guint64  data_units_written;
    guint64  host_read_commands;
    guint64  host_write_commands;
    guint64  controller_busy_time;
    guint64  power_cycles;
    guint64  power_on_hours;
    guint64  unsafe_shutdowns;
    guint64  media_errors;
    guint64  error_log_entries;
} PulsNvmeHealth;

PulsNvmeHealth *puls_nvme_health_copy (const PulsNvmeHealth *health);
void            puls_nvme_health_free (PulsNvmeHealth *health);

#define PULS_TYPE_SMART_DATA (puls_smart_data_get_type ())
G_DECLARE_FINAL_TYPE (PulsSmartData, puls_smart_data, PULS, SMART_DATA, GObject)

PulsSmartData   *puls_smart_data_new            (void);

/* Identity & Topology */
const gchar     *puls_smart_data_get_device_path       (PulsSmartData *self);
const gchar     *puls_smart_data_get_controller_path   (PulsSmartData *self);
const gchar     *puls_smart_data_get_scsi_generic_path (PulsSmartData *self);
guint32          puls_smart_data_get_nvme_nsid         (PulsSmartData *self);
const gchar     *puls_smart_data_get_nvme_subsys_nqn (PulsSmartData *self);
const gchar     *puls_smart_data_get_nvme_wwid         (PulsSmartData *self);
const gchar     *puls_smart_data_get_raid_array_path   (PulsSmartData *self);
const gchar     *puls_smart_data_get_raid_level        (PulsSmartData *self);
const gchar     *puls_smart_data_get_raid_member_status (PulsSmartData *self);
const gchar     *puls_smart_data_get_zfs_pool_name      (PulsSmartData *self);
const gchar     *puls_smart_data_get_zfs_vdev_type      (PulsSmartData *self);
const gchar     *puls_smart_data_get_zfs_member_status  (PulsSmartData *self);
const gchar     *puls_smart_data_get_model_name         (PulsSmartData *self);
const gchar     *puls_smart_data_get_serial_number   (PulsSmartData *self);
const gchar     *puls_smart_data_get_firmware_version (PulsSmartData *self);
const gchar     *puls_smart_data_get_interface_type  (PulsSmartData *self);
guint64          puls_smart_data_get_capacity_bytes  (PulsSmartData *self);
gint             puls_smart_data_get_rotation_rpm    (PulsSmartData *self);
PulsDriveType    puls_smart_data_get_drive_type      (PulsSmartData *self);
PulsTransportProtocol puls_smart_data_get_transport_protocol (PulsSmartData *self);

void puls_smart_data_set_device_path       (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_controller_path   (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_scsi_generic_path (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_nvme_nsid         (PulsSmartData *self, guint32 nsid);
void puls_smart_data_set_nvme_subsys_nqn (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_nvme_wwid         (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_raid_array_path   (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_raid_level        (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_raid_member_status (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_zfs_pool_name      (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_zfs_vdev_type      (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_zfs_member_status  (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_model_name         (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_serial_number   (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_firmware_version (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_interface_type  (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_capacity_bytes  (PulsSmartData *self, guint64 val);
void puls_smart_data_set_rotation_rpm    (PulsSmartData *self, gint val);
void puls_smart_data_set_drive_type      (PulsSmartData *self, PulsDriveType val);
void puls_smart_data_set_transport_protocol (PulsSmartData *self, PulsTransportProtocol protocol);

guint32          puls_smart_data_get_logical_sector_size (PulsSmartData *self);
guint32          puls_smart_data_get_physical_sector_size (PulsSmartData *self);
const gchar     *puls_smart_data_get_form_factor (PulsSmartData *self);
PulsFormFactor   puls_smart_data_get_form_factor_enum (PulsSmartData *self);
PulsSectorFormat puls_smart_data_get_sector_format (PulsSmartData *self);

void puls_smart_data_set_logical_sector_size (PulsSmartData *self, guint32 val);
void puls_smart_data_set_physical_sector_size (PulsSmartData *self, guint32 val);
void puls_smart_data_set_form_factor (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_form_factor_enum (PulsSmartData *self, PulsFormFactor ff);
void puls_smart_data_set_sector_format (PulsSmartData *self, PulsSectorFormat format);

PulsPcieGeneration puls_smart_data_get_pcie_current_gen (PulsSmartData *self);
guint8             puls_smart_data_get_pcie_current_width (PulsSmartData *self);
PulsPcieGeneration puls_smart_data_get_pcie_max_gen     (PulsSmartData *self);
guint8             puls_smart_data_get_pcie_max_width   (PulsSmartData *self);
const gchar       *puls_smart_data_get_protocol_version (PulsSmartData *self);
guint64            puls_smart_data_get_tbw_rating_bytes (PulsSmartData *self);
gint               puls_smart_data_get_wear_level_percent (PulsSmartData *self);

void puls_smart_data_set_pcie_current_gen   (PulsSmartData *self, PulsPcieGeneration gen);
void puls_smart_data_set_pcie_current_width (PulsSmartData *self, guint8 width);
void puls_smart_data_set_pcie_max_gen       (PulsSmartData *self, PulsPcieGeneration gen);
void puls_smart_data_set_pcie_max_width     (PulsSmartData *self, guint8 width);
void puls_smart_data_set_protocol_version   (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_tbw_rating_bytes   (PulsSmartData *self, guint64 val);
void puls_smart_data_set_wear_level_percent (PulsSmartData *self, gint val);

GArray *puls_smart_data_get_thermal_sensors  (PulsSmartData *self);
void    puls_smart_data_add_thermal_sensor   (PulsSmartData *self,
                                              const PulsThermalSensor *sensor);
void    puls_smart_data_clear_thermal_sensors (PulsSmartData *self);


typedef enum {
    PULS_SMARTCTL_EXIT_NONE         = 0,
    PULS_SMARTCTL_EXIT_CMDLINE      = 1 << 0, /* Bit 0 (1): cmdline error */
    PULS_SMARTCTL_EXIT_OPEN_FAILED  = 1 << 1, /* Bit 1 (2): open device failed / standby */
    PULS_SMARTCTL_EXIT_CHECKSUM     = 1 << 2, /* Bit 2 (4): checksum / SMART cmd error */
    PULS_SMARTCTL_EXIT_DISK_FAILING = 1 << 3, /* Bit 3 (8): SMART status DISK FAILING */
    PULS_SMARTCTL_EXIT_PREFAIL_WARN = 1 << 4, /* Bit 4 (16): pre-fail attr <= threshold */
    PULS_SMARTCTL_EXIT_ERROR_LOG    = 1 << 5, /* Bit 5 (32): error log / past threshold */
    PULS_SMARTCTL_EXIT_SELFTEST_ERR = 1 << 6, /* Bit 6 (64): self-test errors */
    PULS_SMARTCTL_EXIT_RESERVED_B7  = 1 << 7, /* Bit 7 (128): reserved non-fatal bit */
} PulsSmartctlExitBit;

PulsHealthStatus puls_smart_data_get_health          (PulsSmartData *self);
gboolean         puls_smart_data_get_smart_enabled   (PulsSmartData *self);
gint             puls_smart_data_get_temperature     (PulsSmartData *self);
PulsPowerState   puls_smart_data_get_power_state     (PulsSmartData *self);
gint             puls_smart_data_get_smartctl_exit_status (PulsSmartData *self);

void puls_smart_data_set_health        (PulsSmartData *self, PulsHealthStatus val);
void puls_smart_data_set_smart_enabled (PulsSmartData *self, gboolean val);
void puls_smart_data_set_temperature   (PulsSmartData *self, gint val);
void puls_smart_data_set_power_state   (PulsSmartData *self, PulsPowerState state);
void puls_smart_data_set_smartctl_exit_status (PulsSmartData *self, gint exit_status);


guint64 puls_smart_data_get_power_on_hours      (PulsSmartData *self);
guint64 puls_smart_data_get_power_cycle_count   (PulsSmartData *self);
guint64 puls_smart_data_get_total_bytes_written (PulsSmartData *self);
guint64 puls_smart_data_get_total_bytes_read    (PulsSmartData *self);

void puls_smart_data_set_power_on_hours      (PulsSmartData *self, guint64 val);
void puls_smart_data_set_power_cycle_count   (PulsSmartData *self, guint64 val);
void puls_smart_data_set_total_bytes_written (PulsSmartData *self, guint64 val);
void puls_smart_data_set_total_bytes_read    (PulsSmartData *self, guint64 val);


gboolean puls_smart_data_get_supports_trim (PulsSmartData *self);
gboolean puls_smart_data_get_supports_ncq  (PulsSmartData *self);
gboolean puls_smart_data_get_supports_apm  (PulsSmartData *self);
gboolean puls_smart_data_get_supports_aam  (PulsSmartData *self);
gboolean puls_smart_data_get_supports_devsleep (PulsSmartData *self);
gboolean puls_smart_data_get_supports_write_cache (PulsSmartData *self);
guint64  puls_smart_data_get_discard_granularity  (PulsSmartData *self);
guint64  puls_smart_data_get_discard_max_bytes    (PulsSmartData *self);

void puls_smart_data_set_supports_trim (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_ncq  (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_apm  (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_aam  (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_devsleep (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_write_cache (PulsSmartData *self, gboolean val);
void puls_smart_data_set_discard_granularity  (PulsSmartData *self, guint64 val);
void puls_smart_data_set_discard_max_bytes    (PulsSmartData *self, guint64 val);

const gchar     *puls_smart_data_get_standard     (PulsSmartData *self);
const gchar     *puls_smart_data_get_transfer_mode (PulsSmartData *self);
void             puls_smart_data_set_standard     (PulsSmartData *self, const gchar *val);
void             puls_smart_data_set_transfer_mode (PulsSmartData *self, const gchar *val);

gboolean         puls_smart_data_get_self_test_in_progress    (PulsSmartData *self);
gint             puls_smart_data_get_self_test_percent        (PulsSmartData *self);
const gchar     *puls_smart_data_get_self_test_status_str    (PulsSmartData *self);
const gchar     *puls_smart_data_get_self_test_type_str      (PulsSmartData *self);

void puls_smart_data_set_self_test_in_progress (PulsSmartData *self, gboolean val);
void puls_smart_data_set_self_test_percent     (PulsSmartData *self, gint val);
void puls_smart_data_set_self_test_status_str  (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_self_test_type_str    (PulsSmartData *self, const gchar *val);

GArray      *puls_smart_data_get_ata_attributes  (PulsSmartData *self);
void         puls_smart_data_add_ata_attribute   (PulsSmartData *self,
                                                  const PulsSmartAttribute *attr);
void         puls_smart_data_clear_ata_attributes (PulsSmartData *self);

PulsNvmeHealth *puls_smart_data_get_nvme_health (PulsSmartData *self);
void            puls_smart_data_set_nvme_health (PulsSmartData *self,
                                                 const PulsNvmeHealth *health);

const gchar *puls_health_status_to_string (PulsHealthStatus status);
const gchar *puls_drive_type_to_string    (PulsDriveType type);
const gchar *puls_drive_type_to_icon      (PulsDriveType type);
gboolean     puls_drive_type_is_ssd       (PulsDriveType type);
gboolean     puls_drive_type_is_rotational (PulsDriveType type);

const gchar          *puls_transport_protocol_to_string  (PulsTransportProtocol protocol);
PulsTransportProtocol puls_transport_protocol_from_string (const gchar *str);

const gchar          *puls_pcie_generation_to_string     (PulsPcieGeneration gen);
PulsPcieGeneration    puls_pcie_generation_from_string   (const gchar *str);
PulsPcieGeneration    puls_pcie_speed_to_generation      (const gchar *speed_str);
const gchar          *puls_pcie_generation_to_max_speed_str (PulsPcieGeneration gen);

const gchar          *puls_sector_format_to_string       (PulsSectorFormat format);
PulsSectorFormat      puls_sector_format_detect          (guint32 logical, guint32 physical);

const gchar          *puls_form_factor_to_string         (PulsFormFactor ff);
PulsFormFactor        puls_form_factor_from_string       (const gchar *str);

const gchar          *puls_power_state_to_string         (PulsPowerState state);
PulsPowerState        puls_power_state_from_string       (const gchar *str);
gboolean              puls_power_state_is_sleeping       (PulsPowerState state);

const gchar          *puls_usb_speed_to_string            (PulsUsbSpeed speed);
const gchar          *puls_usb_speed_to_generation_badge  (PulsUsbSpeed speed);
PulsUsbSpeed          puls_usb_speed_from_mbps            (guint64 mbps);
PulsUsbSpeed          puls_usb_speed_from_string          (const gchar *str);

PulsUsbSpeed          puls_smart_data_get_usb_speed       (PulsSmartData *self);
void                  puls_smart_data_set_usb_speed       (PulsSmartData *self, PulsUsbSpeed speed);
const gchar          *puls_smart_data_get_usb_bridge_chip (PulsSmartData *self);
void                  puls_smart_data_set_usb_bridge_chip (PulsSmartData *self, const gchar *chip);

gint             puls_smart_data_get_health_percent          (PulsSmartData *self);
gint             puls_smart_data_get_estimated_lifetime_days (PulsSmartData *self);
void             puls_smart_data_set_health_percent          (PulsSmartData *self, gint val);
void             puls_smart_data_set_estimated_lifetime_days (PulsSmartData *self, gint val);

guint32          puls_smart_data_get_buffer_size_kb   (PulsSmartData *self);
gint             puls_smart_data_get_apm_level        (PulsSmartData *self);
gint             puls_smart_data_get_aam_level        (PulsSmartData *self);
gint             puls_smart_data_get_spin_up_time_ms  (PulsSmartData *self);
guint64          puls_smart_data_get_error_count_total (PulsSmartData *self);

void puls_smart_data_set_buffer_size_kb    (PulsSmartData *self, guint32 val);
void puls_smart_data_set_apm_level        (PulsSmartData *self, gint val);
void puls_smart_data_set_aam_level        (PulsSmartData *self, gint val);
void puls_smart_data_set_spin_up_time_ms  (PulsSmartData *self, gint val);
void puls_smart_data_set_error_count_total (PulsSmartData *self, guint64 val);

G_END_DECLS
