/*
 * puls-smart-data.c
 *
 * S.M.A.R.T. data structures implementation.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-smart-data.h"
#include <string.h>


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

PulsThermalSensor *
puls_thermal_sensor_new (const gchar *name, gint temperature, gint warning_temp, gint critical_temp)
{
    PulsThermalSensor *s = g_new0 (PulsThermalSensor, 1);
    s->name = g_strdup (name);
    s->temperature = temperature;
    s->warning_temp = warning_temp;
    s->critical_temp = critical_temp;
    return s;
}

PulsThermalSensor *
puls_thermal_sensor_copy (const PulsThermalSensor *sensor)
{
    if (sensor == NULL)
        return NULL;
    return puls_thermal_sensor_new (sensor->name,
                                    sensor->temperature,
                                    sensor->warning_temp,
                                    sensor->critical_temp);
}

void
puls_thermal_sensor_free (PulsThermalSensor *sensor)
{
    if (sensor == NULL)
        return;
    g_free (sensor->name);
    g_free (sensor);
}


struct _PulsSmartData {
    GObject parent_instance;

    gchar        *device_path;
    gchar        *model_name;
    gchar        *serial_number;
    gchar        *firmware_version;
    gchar        *interface_type;
    gchar        *standard;
    gchar        *transfer_mode;
    gchar        *form_factor;
    guint64       capacity_bytes;
    gint          rotation_rpm;
    guint32       logical_sector_size;
    guint32       physical_sector_size;
    PulsDriveType drive_type;
    PulsTransportProtocol transport_protocol;

    PulsHealthStatus health;
    gboolean         smart_enabled;
    gint             temperature;
    guint64 power_on_hours;
    guint64 power_cycle_count;
    guint64 total_bytes_written;
    guint64 total_bytes_read;


    gboolean supports_trim;
    gboolean supports_ncq;
    gboolean supports_apm;
    gboolean supports_aam;
    gboolean supports_devsleep;
    gboolean supports_write_cache;

    GArray *ata_attributes;

    PulsNvmeHealth *nvme_health;

    gboolean        self_test_in_progress;
    gint            self_test_percent;
    gchar          *self_test_status_str;
    gchar          *self_test_type_str;

    gint            health_percent;
    gint            estimated_lifetime_days;

    guint32         buffer_size_kb;
    gint            apm_level;        /* -1 = not set */
    gint            aam_level;        /* -1 = not set */
    gint            spin_up_time_ms;  /* -1 = not set */
    guint64         error_count_total;

    PulsPcieGeneration pcie_current_gen;
    guint8             pcie_current_width;
    PulsPcieGeneration pcie_max_gen;
    guint8             pcie_max_width;
    gchar             *protocol_version;
    PulsSectorFormat   sector_format;
    guint64            tbw_rating_bytes;
    gint               wear_level_percent;
    GArray            *thermal_sensors;
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
    g_free (self->standard);
    g_free (self->transfer_mode);
    g_free (self->form_factor);

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
    g_free (self->protocol_version);

    if (self->thermal_sensors) {
        for (guint i = 0; i < self->thermal_sensors->len; i++) {
            PulsThermalSensor *s = &g_array_index (self->thermal_sensors,
                                                   PulsThermalSensor, i);
            g_free (s->name);
        }
        g_array_free (self->thermal_sensors, TRUE);
    }

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
    self->standard         = NULL;
    self->transfer_mode    = NULL;
    self->form_factor      = NULL;
    self->capacity_bytes   = 0;
    self->rotation_rpm     = -1;
    self->logical_sector_size = 0;
    self->physical_sector_size = 0;
    self->drive_type       = PULS_DRIVE_TYPE_UNKNOWN;
    self->transport_protocol = PULS_TRANSPORT_UNKNOWN;
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
    self->supports_aam  = FALSE;
    self->supports_devsleep = FALSE;
    self->supports_write_cache = FALSE;
    self->ata_attributes = g_array_new (FALSE, TRUE, sizeof (PulsSmartAttribute));
    self->nvme_health    = NULL;
    self->self_test_in_progress = FALSE;
    self->self_test_percent     = 0;
    self->self_test_status_str  = NULL;
    self->self_test_type_str    = NULL;
    self->health_percent           = -1;
    self->estimated_lifetime_days  = -1;
    self->buffer_size_kb           = 0;
    self->apm_level                = -1;
    self->aam_level                = -1;
    self->spin_up_time_ms          = -1;
    self->error_count_total        = 0;
    self->pcie_current_gen         = PULS_PCIE_GEN_UNKNOWN;
    self->pcie_current_width       = 0;
    self->pcie_max_gen             = PULS_PCIE_GEN_UNKNOWN;
    self->pcie_max_width           = 0;
    self->protocol_version         = NULL;
    self->sector_format            = PULS_SECTOR_FORMAT_UNKNOWN;
    self->tbw_rating_bytes         = 0;
    self->wear_level_percent       = -1;
    self->thermal_sensors          = g_array_new (FALSE, TRUE, sizeof (PulsThermalSensor));
}


PulsSmartData *
puls_smart_data_new (void)
{
    return g_object_new (PULS_TYPE_SMART_DATA, NULL);
}

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
IMPL_STRING_ACCESSOR (standard)
IMPL_STRING_ACCESSOR (transfer_mode)
IMPL_STRING_ACCESSOR (form_factor)

#undef IMPL_STRING_ACCESSOR

guint32
puls_smart_data_get_logical_sector_size (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->logical_sector_size;
}

void
puls_smart_data_set_logical_sector_size (PulsSmartData *self, guint32 val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->logical_sector_size = val;
}

guint32
puls_smart_data_get_physical_sector_size (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->physical_sector_size;
}

void
puls_smart_data_set_physical_sector_size (PulsSmartData *self, guint32 val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->physical_sector_size = val;
}


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

PulsTransportProtocol
puls_smart_data_get_transport_protocol (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), PULS_TRANSPORT_UNKNOWN);
    return self->transport_protocol;
}

void
puls_smart_data_set_transport_protocol (PulsSmartData *self, PulsTransportProtocol protocol)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->transport_protocol = protocol;
}

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
IMPL_BOOL_ACCESSOR (supports_aam)
IMPL_BOOL_ACCESSOR (supports_devsleep)
IMPL_BOOL_ACCESSOR (supports_write_cache)

#undef IMPL_BOOL_ACCESSOR


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
    case PULS_DRIVE_TYPE_NVME_SSD: return "NVMe SSD";
    case PULS_DRIVE_TYPE_SATA_SSD: return "SATA SSD";
    case PULS_DRIVE_TYPE_SATA_HDD: return "SATA HDD";
    case PULS_DRIVE_TYPE_SAS_SSD:  return "SAS SSD";
    case PULS_DRIVE_TYPE_SAS_HDD:  return "SAS HDD";
    case PULS_DRIVE_TYPE_USB:      return "USB";
    case PULS_DRIVE_TYPE_SSHD:     return "SSHD";
    case PULS_DRIVE_TYPE_EMMC:     return "eMMC";
    case PULS_DRIVE_TYPE_SD_CARD:  return "SD Card";
    case PULS_DRIVE_TYPE_VIRTUAL:  return "Virtual";
    case PULS_DRIVE_TYPE_UNKNOWN:
    default:                       return "Unknown";
    }
}

const gchar *
puls_drive_type_to_icon (PulsDriveType type)
{
    switch (type) {
    case PULS_DRIVE_TYPE_NVME_SSD:
    case PULS_DRIVE_TYPE_SATA_SSD:
    case PULS_DRIVE_TYPE_SAS_SSD:
    case PULS_DRIVE_TYPE_EMMC:
        return "drive-harddisk-solidstate-symbolic";
    case PULS_DRIVE_TYPE_USB:
    case PULS_DRIVE_TYPE_SD_CARD:
        return "drive-removable-media-symbolic";
    case PULS_DRIVE_TYPE_SATA_HDD:
    case PULS_DRIVE_TYPE_SAS_HDD:
    case PULS_DRIVE_TYPE_SSHD:
        return "drive-harddisk-symbolic";
    case PULS_DRIVE_TYPE_VIRTUAL:
        return "drive-multidisk-symbolic";
    case PULS_DRIVE_TYPE_UNKNOWN:
    default:
        return "drive-harddisk-symbolic";
    }
}

gboolean
puls_drive_type_is_ssd (PulsDriveType type)
{
    return (type == PULS_DRIVE_TYPE_NVME_SSD ||
            type == PULS_DRIVE_TYPE_SATA_SSD ||
            type == PULS_DRIVE_TYPE_SAS_SSD  ||
            type == PULS_DRIVE_TYPE_EMMC);
}

gboolean
puls_drive_type_is_rotational (PulsDriveType type)
{
    return (type == PULS_DRIVE_TYPE_SATA_HDD ||
            type == PULS_DRIVE_TYPE_SAS_HDD  ||
            type == PULS_DRIVE_TYPE_SSHD);
}

const gchar *
puls_transport_protocol_to_string (PulsTransportProtocol protocol)
{
    switch (protocol) {
    case PULS_TRANSPORT_PCIE:    return "PCIe";
    case PULS_TRANSPORT_SATA:    return "SATA";
    case PULS_TRANSPORT_SAS:     return "SAS";
    case PULS_TRANSPORT_USB:     return "USB";
    case PULS_TRANSPORT_MMC:     return "MMC";
    case PULS_TRANSPORT_VIRTUAL: return "Virtual";
    case PULS_TRANSPORT_UNKNOWN:
    default:                     return "Unknown";
    }
}

PulsTransportProtocol
puls_transport_protocol_from_string (const gchar *str)
{
    if (str == NULL)
        return PULS_TRANSPORT_UNKNOWN;

    if (g_ascii_strcasecmp (str, "pcie") == 0 || g_ascii_strcasecmp (str, "nvme") == 0)
        return PULS_TRANSPORT_PCIE;
    if (g_ascii_strcasecmp (str, "sata") == 0 || g_ascii_strcasecmp (str, "ata") == 0)
        return PULS_TRANSPORT_SATA;
    if (g_ascii_strcasecmp (str, "sas") == 0 || g_ascii_strcasecmp (str, "scsi") == 0)
        return PULS_TRANSPORT_SAS;
    if (g_ascii_strcasecmp (str, "usb") == 0)
        return PULS_TRANSPORT_USB;
    if (g_ascii_strcasecmp (str, "mmc") == 0 || g_ascii_strcasecmp (str, "sd") == 0)
        return PULS_TRANSPORT_MMC;
    if (g_ascii_strcasecmp (str, "virtual") == 0 || g_ascii_strcasecmp (str, "loop") == 0)
        return PULS_TRANSPORT_VIRTUAL;

    return PULS_TRANSPORT_UNKNOWN;
}


gint
puls_smart_data_get_health_percent (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->health_percent;
}

void
puls_smart_data_set_health_percent (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->health_percent = val;
}

gint
puls_smart_data_get_estimated_lifetime_days (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->estimated_lifetime_days;
}

void
puls_smart_data_set_estimated_lifetime_days (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->estimated_lifetime_days = val;
}

guint32
puls_smart_data_get_buffer_size_kb (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->buffer_size_kb;
}

void
puls_smart_data_set_buffer_size_kb (PulsSmartData *self, guint32 val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->buffer_size_kb = val;
}

gint
puls_smart_data_get_apm_level (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->apm_level;
}

void
puls_smart_data_set_apm_level (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->apm_level = val;
}

gint
puls_smart_data_get_aam_level (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->aam_level;
}

void
puls_smart_data_set_aam_level (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->aam_level = val;
}

gint
puls_smart_data_get_spin_up_time_ms (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->spin_up_time_ms;
}

void
puls_smart_data_set_spin_up_time_ms (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->spin_up_time_ms = val;
}

guint64
puls_smart_data_get_error_count_total (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->error_count_total;
}

void
puls_smart_data_set_error_count_total (PulsSmartData *self, guint64 val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->error_count_total = val;
}

PulsSectorFormat
puls_smart_data_get_sector_format (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), PULS_SECTOR_FORMAT_UNKNOWN);
    if (self->sector_format != PULS_SECTOR_FORMAT_UNKNOWN)
        return self->sector_format;

    return puls_sector_format_detect (self->logical_sector_size, self->physical_sector_size);
}

void
puls_smart_data_set_sector_format (PulsSmartData *self, PulsSectorFormat format)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->sector_format = format;
}

PulsPcieGeneration
puls_smart_data_get_pcie_current_gen (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), PULS_PCIE_GEN_UNKNOWN);
    return self->pcie_current_gen;
}

void
puls_smart_data_set_pcie_current_gen (PulsSmartData *self, PulsPcieGeneration gen)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->pcie_current_gen = gen;
}

guint8
puls_smart_data_get_pcie_current_width (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->pcie_current_width;
}

void
puls_smart_data_set_pcie_current_width (PulsSmartData *self, guint8 width)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->pcie_current_width = width;
}

PulsPcieGeneration
puls_smart_data_get_pcie_max_gen (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), PULS_PCIE_GEN_UNKNOWN);
    return self->pcie_max_gen;
}

void
puls_smart_data_set_pcie_max_gen (PulsSmartData *self, PulsPcieGeneration gen)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->pcie_max_gen = gen;
}

guint8
puls_smart_data_get_pcie_max_width (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->pcie_max_width;
}

void
puls_smart_data_set_pcie_max_width (PulsSmartData *self, guint8 width)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->pcie_max_width = width;
}

const gchar *
puls_smart_data_get_protocol_version (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), NULL);
    return self->protocol_version;
}

void
puls_smart_data_set_protocol_version (PulsSmartData *self, const gchar *val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    g_free (self->protocol_version);
    self->protocol_version = g_strdup (val);
}

guint64
puls_smart_data_get_tbw_rating_bytes (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), 0);
    return self->tbw_rating_bytes;
}

void
puls_smart_data_set_tbw_rating_bytes (PulsSmartData *self, guint64 val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->tbw_rating_bytes = val;
}

gint
puls_smart_data_get_wear_level_percent (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), -1);
    return self->wear_level_percent;
}

void
puls_smart_data_set_wear_level_percent (PulsSmartData *self, gint val)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    self->wear_level_percent = val;
}

GArray *
puls_smart_data_get_thermal_sensors (PulsSmartData *self)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (self), NULL);
    return self->thermal_sensors;
}

void
puls_smart_data_add_thermal_sensor (PulsSmartData *self, const PulsThermalSensor *sensor)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    g_return_if_fail (sensor != NULL);

    PulsThermalSensor copy;
    copy.name = g_strdup (sensor->name);
    copy.temperature = sensor->temperature;
    copy.warning_temp = sensor->warning_temp;
    copy.critical_temp = sensor->critical_temp;

    g_array_append_val (self->thermal_sensors, copy);
}

void
puls_smart_data_clear_thermal_sensors (PulsSmartData *self)
{
    g_return_if_fail (PULS_IS_SMART_DATA (self));
    if (self->thermal_sensors == NULL)
        return;

    for (guint i = 0; i < self->thermal_sensors->len; i++) {
        PulsThermalSensor *s = &g_array_index (self->thermal_sensors, PulsThermalSensor, i);
        g_free (s->name);
    }
    g_array_set_size (self->thermal_sensors, 0);
}

const gchar *
puls_pcie_generation_to_string (PulsPcieGeneration gen)
{
    switch (gen) {
    case PULS_PCIE_GEN_1: return "PCIe 1.0";
    case PULS_PCIE_GEN_2: return "PCIe 2.0";
    case PULS_PCIE_GEN_3: return "PCIe 3.0";
    case PULS_PCIE_GEN_4: return "PCIe 4.0";
    case PULS_PCIE_GEN_5: return "PCIe 5.0";
    case PULS_PCIE_GEN_6: return "PCIe 6.0";
    case PULS_PCIE_GEN_UNKNOWN:
    default:              return "Unknown";
    }
}

PulsPcieGeneration
puls_pcie_generation_from_string (const gchar *str)
{
    if (str == NULL)
        return PULS_PCIE_GEN_UNKNOWN;

    if (strstr (str, "6.0") || strstr (str, "Gen6") || strstr (str, "gen6"))
        return PULS_PCIE_GEN_6;
    if (strstr (str, "5.0") || strstr (str, "Gen5") || strstr (str, "gen5"))
        return PULS_PCIE_GEN_5;
    if (strstr (str, "4.0") || strstr (str, "Gen4") || strstr (str, "gen4"))
        return PULS_PCIE_GEN_4;
    if (strstr (str, "3.0") || strstr (str, "Gen3") || strstr (str, "gen3"))
        return PULS_PCIE_GEN_3;
    if (strstr (str, "2.0") || strstr (str, "Gen2") || strstr (str, "gen2"))
        return PULS_PCIE_GEN_2;
    if (strstr (str, "1.0") || strstr (str, "Gen1") || strstr (str, "gen1"))
        return PULS_PCIE_GEN_1;

    return PULS_PCIE_GEN_UNKNOWN;
}

const gchar *
puls_sector_format_to_string (PulsSectorFormat format)
{
    switch (format) {
    case PULS_SECTOR_FORMAT_512N: return "512n";
    case PULS_SECTOR_FORMAT_512E: return "512e";
    case PULS_SECTOR_FORMAT_4KN:  return "4Kn";
    case PULS_SECTOR_FORMAT_OTHER: return "Custom";
    case PULS_SECTOR_FORMAT_UNKNOWN:
    default:                      return "Unknown";
    }
}

PulsSectorFormat
puls_sector_format_detect (guint32 logical, guint32 physical)
{
    if (logical == 0)
        return PULS_SECTOR_FORMAT_UNKNOWN;
    if (physical == 0)
        physical = logical;

    if (logical == 512 && physical == 512)
        return PULS_SECTOR_FORMAT_512N;
    if (logical == 512 && physical == 4096)
        return PULS_SECTOR_FORMAT_512E;
    if (logical == 4096 && physical == 4096)
        return PULS_SECTOR_FORMAT_4KN;

    return PULS_SECTOR_FORMAT_OTHER;
}
