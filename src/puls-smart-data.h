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

/* ── Health Status ─────────────────────────────────────────── */

typedef enum {
    PULS_HEALTH_GOOD,
    PULS_HEALTH_CAUTION,
    PULS_HEALTH_BAD,
    PULS_HEALTH_UNKNOWN
} PulsHealthStatus;

/* ── Drive Type ────────────────────────────────────────────── */

typedef enum {
    PULS_DRIVE_TYPE_HDD,
    PULS_DRIVE_TYPE_SATA_SSD,
    PULS_DRIVE_TYPE_NVME_SSD,
    PULS_DRIVE_TYPE_USB,
    PULS_DRIVE_TYPE_SSHD,
    PULS_DRIVE_TYPE_UNKNOWN
} PulsDriveType;

/* ── ATA SMART Attribute ───────────────────────────────────── */

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

/* ── NVMe Health Info ──────────────────────────────────────── */

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

/* ── PulsSmartData GObject ─────────────────────────────────── */

#define PULS_TYPE_SMART_DATA (puls_smart_data_get_type ())
G_DECLARE_FINAL_TYPE (PulsSmartData, puls_smart_data, PULS, SMART_DATA, GObject)

PulsSmartData   *puls_smart_data_new            (void);

/* Identity */
const gchar     *puls_smart_data_get_device_path     (PulsSmartData *self);
const gchar     *puls_smart_data_get_model_name      (PulsSmartData *self);
const gchar     *puls_smart_data_get_serial_number   (PulsSmartData *self);
const gchar     *puls_smart_data_get_firmware_version (PulsSmartData *self);
const gchar     *puls_smart_data_get_interface_type  (PulsSmartData *self);
guint64          puls_smart_data_get_capacity_bytes  (PulsSmartData *self);
gint             puls_smart_data_get_rotation_rpm    (PulsSmartData *self);
PulsDriveType    puls_smart_data_get_drive_type      (PulsSmartData *self);

void puls_smart_data_set_device_path     (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_model_name      (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_serial_number   (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_firmware_version (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_interface_type  (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_capacity_bytes  (PulsSmartData *self, guint64 val);
void puls_smart_data_set_rotation_rpm    (PulsSmartData *self, gint val);
void puls_smart_data_set_drive_type      (PulsSmartData *self, PulsDriveType val);

guint32          puls_smart_data_get_logical_sector_size (PulsSmartData *self);
guint32          puls_smart_data_get_physical_sector_size (PulsSmartData *self);
const gchar     *puls_smart_data_get_form_factor (PulsSmartData *self);

void puls_smart_data_set_logical_sector_size (PulsSmartData *self, guint32 val);
void puls_smart_data_set_physical_sector_size (PulsSmartData *self, guint32 val);
void puls_smart_data_set_form_factor (PulsSmartData *self, const gchar *val);


/* Health */
PulsHealthStatus puls_smart_data_get_health          (PulsSmartData *self);
gboolean         puls_smart_data_get_smart_enabled   (PulsSmartData *self);
gint             puls_smart_data_get_temperature     (PulsSmartData *self);

void puls_smart_data_set_health        (PulsSmartData *self, PulsHealthStatus val);
void puls_smart_data_set_smart_enabled (PulsSmartData *self, gboolean val);
void puls_smart_data_set_temperature   (PulsSmartData *self, gint val);

/* Usage stats */
guint64 puls_smart_data_get_power_on_hours      (PulsSmartData *self);
guint64 puls_smart_data_get_power_cycle_count   (PulsSmartData *self);
guint64 puls_smart_data_get_total_bytes_written (PulsSmartData *self);
guint64 puls_smart_data_get_total_bytes_read    (PulsSmartData *self);

void puls_smart_data_set_power_on_hours      (PulsSmartData *self, guint64 val);
void puls_smart_data_set_power_cycle_count   (PulsSmartData *self, guint64 val);
void puls_smart_data_set_total_bytes_written (PulsSmartData *self, guint64 val);
void puls_smart_data_set_total_bytes_read    (PulsSmartData *self, guint64 val);

/* Features */
gboolean puls_smart_data_get_supports_trim (PulsSmartData *self);
gboolean puls_smart_data_get_supports_ncq  (PulsSmartData *self);
gboolean puls_smart_data_get_supports_apm  (PulsSmartData *self);
gboolean puls_smart_data_get_supports_aam  (PulsSmartData *self);
gboolean puls_smart_data_get_supports_devsleep (PulsSmartData *self);
gboolean puls_smart_data_get_supports_write_cache (PulsSmartData *self);

void puls_smart_data_set_supports_trim (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_ncq  (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_apm  (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_aam  (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_devsleep (PulsSmartData *self, gboolean val);
void puls_smart_data_set_supports_write_cache (PulsSmartData *self, gboolean val);

/* Standard and Transfer Mode */
const gchar     *puls_smart_data_get_standard     (PulsSmartData *self);
const gchar     *puls_smart_data_get_transfer_mode (PulsSmartData *self);
void             puls_smart_data_set_standard     (PulsSmartData *self, const gchar *val);
void             puls_smart_data_set_transfer_mode (PulsSmartData *self, const gchar *val);

/* Self-test info */
gboolean         puls_smart_data_get_self_test_in_progress    (PulsSmartData *self);
gint             puls_smart_data_get_self_test_percent        (PulsSmartData *self);
const gchar     *puls_smart_data_get_self_test_status_str    (PulsSmartData *self);
const gchar     *puls_smart_data_get_self_test_type_str      (PulsSmartData *self);

void puls_smart_data_set_self_test_in_progress (PulsSmartData *self, gboolean val);
void puls_smart_data_set_self_test_percent     (PulsSmartData *self, gint val);
void puls_smart_data_set_self_test_status_str  (PulsSmartData *self, const gchar *val);
void puls_smart_data_set_self_test_type_str    (PulsSmartData *self, const gchar *val);

/* ATA Attributes */
GArray      *puls_smart_data_get_ata_attributes  (PulsSmartData *self);
void         puls_smart_data_add_ata_attribute   (PulsSmartData *self,
                                                  const PulsSmartAttribute *attr);
void         puls_smart_data_clear_ata_attributes (PulsSmartData *self);

/* NVMe Health */
PulsNvmeHealth *puls_smart_data_get_nvme_health (PulsSmartData *self);
void            puls_smart_data_set_nvme_health (PulsSmartData *self,
                                                 const PulsNvmeHealth *health);

/* Utility */
const gchar *puls_health_status_to_string (PulsHealthStatus status);
const gchar *puls_drive_type_to_string    (PulsDriveType type);
const gchar *puls_drive_type_to_icon      (PulsDriveType type);

G_END_DECLS
