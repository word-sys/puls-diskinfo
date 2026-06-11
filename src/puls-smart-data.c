/*
 * puls-smart-data.c
 *
 * S.M.A.R.T. data structures implementation.
 *
 * Copyright (C) 2024 Puls DiskInfo Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-smart-data.h"
#include <string.h>

/* ── PulsSmartAttribute helpers ─────────────────────────────── */

PulsSmartAttribute *
puls_smart_attribute_copy (const PulsSmartAttribute *attr)
{
    PulsSmartAttribute *copy;

    g_return_val_if_fail (attr != NULL, NULL);

    copy = g_new0 (PulsSmartAttribute, 1);
    copy->id         = attr->id;
    copy->name       = g_strdup (attr->name);
    copy->current    = attr->current;
    copy->worst      = attr->worst;
    copy->threshold  = attr->threshold;
    copy->raw_value  = attr->raw_value;
    copy->raw_string = g_strdup (attr->raw_string);
    copy->failed_past  = attr->failed_past;
    copy->failing_now  = attr->failing_now;

    return copy;
}

void
puls_smart_attribute_free (PulsSmartAttribute *attr)
{
    if (attr == NULL)
        return;

    g_free (attr->name);
    g_free (attr->raw_string);
    g_free (attr);
}

/* ── PulsNvmeHealth helpers ────────────────────────────────── */

PulsNvmeHealth *
puls_nvme_health_copy (const PulsNvmeHealth *health)
{
    PulsNvmeHealth *copy;

    g_return_val_if_fail (health != NULL, NULL);

    copy = g_new0 (PulsNvmeHealth, 1);
    memcpy (copy, health, sizeof (PulsNvmeHealth));

    return copy;
}

void
puls_nvme_health_free (PulsNvmeHealth *health)
{
    g_free (health);
}

/* ── PulsSmartData GObject ─────────────────────────────────── */

struct _PulsSmartData {
    GObject parent_instance;

    /* Identity */
    gchar        *device_path;
    gchar        *model_name;
    gchar        *serial_number;
    gchar        *firmware_version;
    gchar        *interface_type;
    guint64       capacity_bytes;
    gint          rotation_rpm;
    PulsDriveType drive_type;

    /* Health */
    PulsHealthStatus health;
    gboolean         smart_enabled;
    gint             temperature;

    /* Usage stats */
    guint64 power_on_hours;
    guint64 power_cycle_count;
    guint64 total_bytes_written;
    guint64 total_bytes_read;

    /* Features */
    gboolean supports_trim;
    gboolean supports_ncq;
    gboolean supports_apm;

    /* ATA SMART attributes */
    GArray *ata_attributes;

    /* NVMe health */
    PulsNvmeHealth *nvme_health;

    /* Self test state */
    gboolean        self_test_in_progress;
    gint            self_test_percent;
    gchar          *self_test_status_str;
    gchar          *self_test_type_str;
};

G_DEFINE_TYPE (PulsSmartData, puls_smart_data, G_TYPE_OBJECT)

static void
puls_smart_data_finalize (GObject *object)
{
    PulsSmartData *self = PULS_SMART_DATA (object);

    g_free (self->device_path);
    g_free (self->model_name);
    g_free (self->serial_number);
    g_free (self->firmware_version);
    g_free (self->interface_type);

    if (self->ata_attributes) {
        for (guint i = 0; i < self->ata_attributes->len; i++) {
            PulsSmartAttribute *attr = &g_array_index (self->ata_attributes,
                                                       PulsSmartAttribute, i);
            g_free (attr->name);
            g_free (attr->raw_string);
        }
        g_array_free (self->ata_attributes, TRUE);
    }

    puls_nvme_health_free (self->nvme_health);
    g_free (self->self_test_status_str);
    g_free (self->self_test_type_str);

    G_OBJECT_CLASS (puls_smart_data_parent_class)->finalize (object);
}

static void
puls_smart_data_class_init (PulsSmartDataClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = puls_smart_data_finalize;
}

static void
puls_smart_data_init (PulsSmartData *self)
{
    self->device_path      = NULL;
    self->model_name       = NULL;
    self->serial_number    = NULL;
    self->firmware_version = NULL;
    self->interface_type   = NULL;
    self->capacity_bytes   = 0;
    self->rotation_rpm     = -1;
    self->drive_type       = PULS_DRIVE_TYPE_UNKNOWN;
    self->health           = PULS_HEALTH_UNKNOWN;
    self->smart_enabled    = FALSE;
    self->temperature      = -1;
    self->power_on_hours   = 0;
    self->power_cycle_count = 0;
    self->total_bytes_written = 0;
    self->total_bytes_read    = 0;
    self->supports_trim = FALSE;
    self->supports_ncq  = FALSE;
    self->supports_apm  = FALSE;
    self->ata_attributes = g_array_new (FALSE, TRUE, sizeof (PulsSmartAttribute));
    self->nvme_health    = NULL;
    self->self_test_in_progress = FALSE;
    self->self_test_percent     = 0;
    self->self_test_status_str  = NULL;
    self->self_test_type_str    = NULL;
}

PulsSmartData *
puls_smart_data_new (void)
{
    return g_object_new (PULS_TYPE_SMART_DATA, NULL);
}

/* ── Identity getters/setters ──────────────────────────────── */

#define IMPL_STRING_ACCESSOR(field)                                          \
    const gchar *                                                            \
    puls_smart_data_get_##field (PulsSmartData *self)                         \
    {                                                                        \
        g_return_val_if_fail (PULS_IS_SMART_DATA (self), NULL);              \
        return self->field;                                                   \
    }                                                                        \
    void                                                                     \
    puls_smart_data_set_##field (PulsSmartData *self, const gchar *val)       \
    {                                                                        \
        g_return_if_fail (PULS_IS_SMART_DATA (self));                        \
        g_free (self->field);                                                \
        self->field = g_strdup (val);                                        \
    }

IMPL_STRING_ACCESSOR (device_path)
IMPL_STRING_ACCESSOR (model_name)
IMPL_STRING_ACCESSOR (serial_number)
IMPL_STRING_ACCESSOR (firmware_version)
IMPL_STRING_ACCESSOR (interface_type)

#undef IMPL_STRING_ACCESSOR

guint64
puls_smart_data_get_capacity_bytes (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->capacity_bytes;
}

void
puls_smart_data_set_capacity_bytes (PulsSmartData *self, guint64 val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->capacity_bytes = val;
}

gint
puls_smart_data_get_rotation_rpm (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->rotation_rpm;
}

void
puls_smart_data_set_rotation_rpm (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->rotation_rpm = val;
}

PulsDriveType
puls_smart_data_get_drive_type (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), PULS_DRIVE_TYPE_UNKNOWN);
    return self->drive_type;
}

void
puls_smart_data_set_drive_type (PulsSmartData *self, PulsDriveType val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->drive_type = val;
}

/* ── Health getters/setters ────────────────────────────────── */

PulsHealthStatus
puls_smart_data_get_health (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), PULS_HEALTH_UNKNOWN);
    return self->health;
}

void
puls_smart_data_set_health (PulsSmartData *self, PulsHealthStatus val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->health = val;
}

gboolean
puls_smart_data_get_smart_enabled (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), FALSE);
    return self->smart_enabled;
}

void
puls_smart_data_set_smart_enabled (PulsSmartData *self, gboolean val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->smart_enabled = val;
}

gint
puls_smart_data_get_temperature (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->temperature;
}

void
puls_smart_data_set_temperature (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->temperature = val;
}

/* ── Usage stat getters/setters ────────────────────────────── */

#define IMPL_UINT64_ACCESSOR(field)                                          \
    guint64                                                                  \
    puls_smart_data_get_##field (PulsSmartData *self)                         \
    {                                                                        \
        g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);                 \
        return self->field;                                                   \
    }                                                                        \
    void                                                                     \
    puls_smart_data_set_##field (PulsSmartData *self, guint64 val)            \
    {                                                                        \
        g_return_if_fail (PULS_IS_SMART_DATA (self));                        \
        self->field = val;                                                    \
    }

IMPL_UINT64_ACCESSOR (power_on_hours)
IMPL_UINT64_ACCESSOR (power_cycle_count)
IMPL_UINT64_ACCESSOR (total_bytes_written)
IMPL_UINT64_ACCESSOR (total_bytes_read)

#undef IMPL_UINT64_ACCESSOR

/* ── Feature getters/setters ───────────────────────────────── */

#define IMPL_BOOL_ACCESSOR(field)                                            \
    gboolean                                                                 \
    puls_smart_data_get_##field (PulsSmartData *self)                         \
    {                                                                        \
        g_return_val_if_fail (PULS_IS_SMART_DATA (self), FALSE);             \
        return self->field;                                                   \
    }                                                                        \
    void                                                                     \
    puls_smart_data_set_##field (PulsSmartData *self, gboolean val)           \
    {                                                                        \
        g_return_if_fail (PULS_IS_SMART_DATA (self));                        \
        self->field = val;                                                    \
    }

IMPL_BOOL_ACCESSOR (supports_trim)
IMPL_BOOL_ACCESSOR (supports_ncq)
IMPL_BOOL_ACCESSOR (supports_apm)

#undef IMPL_BOOL_ACCESSOR

/* ── ATA Attributes ────────────────────────────────────────── */

GArray *
puls_smart_data_get_ata_attributes (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), NULL);
    return self->ata_attributes;
}

void
puls_smart_data_add_ata_attribute (PulsSmartData            *self,
                                   const PulsSmartAttribute *attr)
{
    PulsSmartAttribute copy;

    g_return_if_fail (PULS_IS_SMART_DATA (self));
    g_return_if_fail (attr != NULL);

    copy.id         = attr->id;
    copy.name       = g_strdup (attr->name);
    copy.current    = attr->current;
    copy.worst      = attr->worst;
    copy.threshold  = attr->threshold;
    copy.raw_value  = attr->raw_value;
    copy.raw_string = g_strdup (attr->raw_string);
    copy.failed_past  = attr->failed_past;
    copy.failing_now  = attr->failing_now;

    g_array_append_val (self->ata_attributes, copy);
}

void
puls_smart_data_clear_ata_attributes (PulsSmartData *self)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));

    for (guint i = 0; i < self->ata_attributes->len; i++) {
        PulsSmartAttribute *attr = &g_array_index (self->ata_attributes,
                                                   PulsSmartAttribute, i);
        g_free (attr->name);
        g_free (attr->raw_string);
    }
    g_array_set_size (self->ata_attributes, 0);
}

/* ── NVMe Health ───────────────────────────────────────────── */

PulsNvmeHealth *
puls_smart_data_get_nvme_health (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), NULL);
    return self->nvme_health;
}

void
puls_smart_data_set_nvme_health (PulsSmartData      *self,
                                  const PulsNvmeHealth *health)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));

    puls_nvme_health_free (self->nvme_health);
    self->nvme_health = health ? puls_nvme_health_copy (health) : NULL;
}

/* ── Self Test getters/setters ─────────────────────────────── */

gboolean
puls_smart_data_get_self_test_in_progress (PulsSmartData *self)
{    
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), FALSE);
    return self->self_test_in_progress;
}

void
puls_smart_data_set_self_test_in_progress (PulsSmartData *self, gboolean val)
{    
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->self_test_in_progress = val;
}

gint
puls_smart_data_get_self_test_percent (PulsSmartData *self)
{    
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->self_test_percent;
}

void
puls_smart_data_set_self_test_percent (PulsSmartData *self, gint val)
{    
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->self_test_percent = val;
}

const gchar *
puls_smart_data_get_self_test_status_str (PulsSmartData *self)
{    
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), NULL);
    return self->self_test_status_str;
}

void
puls_smart_data_set_self_test_status_str (PulsSmartData *self, const gchar *val)
{    
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    g_free (self->self_test_status_str);
    self->self_test_status_str = g_strdup (val);
}

const gchar *
puls_smart_data_get_self_test_type_str (PulsSmartData *self)
{    
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), NULL);
    return self->self_test_type_str;
}

void
puls_smart_data_set_self_test_type_str (PulsSmartData *self, const gchar *val)
{    
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    g_free (self->self_test_type_str);
    self->self_test_type_str = g_strdup (val);
}

/* ── Utility ───────────────────────────────────────────────── */

const gchar *
puls_health_status_to_string (PulsHealthStatus status)
{
    switch (status) {
    case PULS_HEALTH_GOOD:    return "Good";
    case PULS_HEALTH_CAUTION: return "Caution";
    case PULS_HEALTH_BAD:     return "Bad";
    case PULS_HEALTH_UNKNOWN:
    default:                  return "Unknown";
    }
}

const gchar *
puls_drive_type_to_string (PulsDriveType type)
{
    switch (type) {
    case PULS_DRIVE_TYPE_HDD:      return "HDD";
    case PULS_DRIVE_TYPE_SATA_SSD: return "SATA SSD";
    case PULS_DRIVE_TYPE_NVME_SSD: return "NVMe SSD";
    case PULS_DRIVE_TYPE_USB:      return "USB";
    case PULS_DRIVE_TYPE_SSHD:     return "SSHD";
    case PULS_DRIVE_TYPE_UNKNOWN:
    default:                       return "Unknown";
    }
}

const gchar *
puls_drive_type_to_icon (PulsDriveType type)
{
    switch (type) {
    case PULS_DRIVE_TYPE_HDD:      return "drive-harddisk-symbolic";
    case PULS_DRIVE_TYPE_SATA_SSD: return "drive-harddisk-solidstate-symbolic";
    case PULS_DRIVE_TYPE_NVME_SSD: return "drive-harddisk-solidstate-symbolic";
    case PULS_DRIVE_TYPE_USB:      return "drive-removable-media-symbolic";
    case PULS_DRIVE_TYPE_SSHD:     return "drive-harddisk-symbolic";
    case PULS_DRIVE_TYPE_UNKNOWN:
    default:                       return "drive-harddisk-symbolic";
    }
}
