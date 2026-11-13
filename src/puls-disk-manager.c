/*
 * puls-disk-manager.c
 *
 * Disk enumeration via /sys/block + smartctl, with D-Bus/sysfs fallback.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-disk-manager.h"
#include "puls-mock-data.h"
#include "puls-smart-parser.h"
#include "puls-utils.h"
#include <errno.h>
#include <gio/gio.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PULS_HOTPLUG_DEBOUNCE_MS 350
#define PULS_UEVENT_BUFFER_SIZE  4096

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
    gboolean mock_mode;

    gboolean monitoring;
    GVolumeMonitor *volume_monitor;
    gulong drive_connected_id;
    gulong drive_disconnected_id;
    gulong volume_added_id;
    gulong volume_removed_id;
    GSocket *udev_socket;
    GSource *udev_source;
    guint rescan_timeout_id;
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

    puls_disk_manager_stop_monitoring (self);

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
    self->mock_mode = puls_is_mock_mode_enabled ();

    self->monitoring = FALSE;
    self->volume_monitor = NULL;
    self->drive_connected_id = 0;
    self->drive_disconnected_id = 0;
    self->volume_added_id = 0;
    self->volume_removed_id = 0;
    self->udev_socket = NULL;
    self->udev_source = NULL;
    self->rescan_timeout_id = 0;
}

void
puls_disk_manager_set_mock_mode (PulsDiskManager *self, gboolean mock_mode)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (self));
    self->mock_mode = mock_mode;
}

gboolean
puls_disk_manager_get_mock_mode (PulsDiskManager *self)
{
    g_return_val_if_fail (PULS_IS_DISK_MANAGER (self), FALSE);
    return self->mock_mode;
}

void
puls_disk_manager_add_mock_device (PulsDiskManager *self,
                                   const gchar     *device_path,
                                   PulsSmartData   *data)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (self));
    g_return_if_fail (device_path != NULL);
    g_return_if_fail (data != NULL);

    if (!g_hash_table_contains (self->devices, device_path)) {
        g_hash_table_insert (self->devices, g_strdup (device_path), data);
        self->device_paths = g_list_append (self->device_paths, g_strdup (device_path));
        g_signal_emit (self, signals[SIGNAL_DISK_ADDED], 0, device_path);
    } else {
        g_hash_table_replace (self->devices, g_strdup (device_path), data);
        g_signal_emit (self, signals[SIGNAL_DISK_UPDATED], 0, device_path);
    }
}

PulsDiskManager *
puls_disk_manager_new (void)
{
    return g_object_new (PULS_TYPE_DISK_MANAGER, NULL);
}

static void
puls_smart_data_fill_sysfs_fallbacks (PulsSmartData *data)
{
    const gchar *dev_path = puls_smart_data_get_device_path (data);
    if (dev_path == NULL)
        return;

    const gchar *base = strrchr (dev_path, '/');
    if (base)
        base++;
    else
        base = dev_path;

    PulsDriveType dtype = puls_smart_data_get_drive_type (data);
    if (dtype == PULS_DRIVE_TYPE_UNKNOWN) {
        dtype = puls_detect_drive_type (dev_path);
        puls_smart_data_set_drive_type (data, dtype);
    }

    PulsTransportProtocol transport = puls_smart_data_get_transport_protocol (data);
    if (transport == PULS_TRANSPORT_UNKNOWN) {
        transport = puls_detect_transport_protocol (dev_path);
        puls_smart_data_set_transport_protocol (data, transport);
    }

    const gchar *model = puls_smart_data_get_model_name (data);
    if (model == NULL || strlen (model) == 0 || g_strcmp0 (model, "Unknown") == 0) {
        g_autofree gchar *model_contents = puls_sysfs_read_device_file (base, "device/model", 128);
        g_autofree gchar *vendor_contents = puls_sysfs_read_device_file (base, "device/vendor", 128);
        g_autofree gchar *name_contents = puls_sysfs_read_device_file (base, "device/name", 128);

        if (model_contents) {
            if (vendor_contents) {
                g_autofree gchar *full_model = g_strdup_printf ("%s %s", vendor_contents, model_contents);
                puls_smart_data_set_model_name (data, full_model);
            } else {
                puls_smart_data_set_model_name (data, model_contents);
            }
        } else if (name_contents) {
            puls_smart_data_set_model_name (data, name_contents);
        } else {
            puls_smart_data_set_model_name (data, "Generic Storage Device");
        }
    }

    const gchar *serial = puls_smart_data_get_serial_number (data);
    if (serial == NULL || strlen (serial) == 0 || g_strcmp0 (serial, "Unknown") == 0) {
        g_autofree gchar *serial_contents = puls_sysfs_read_device_file (base, "device/serial", 128);
        g_autofree gchar *cid_contents = puls_sysfs_read_device_file (base, "device/cid", 128);

        if (serial_contents) {
            puls_smart_data_set_serial_number (data, serial_contents);
        } else if (cid_contents) {
            puls_smart_data_set_serial_number (data, cid_contents);
        } else if (dtype == PULS_DRIVE_TYPE_USB) {
            g_autofree gchar *device_link = g_strdup_printf ("/sys/block/%s/device", base);
            gchar real_path_buf[PATH_MAX];
            if (realpath (device_link, real_path_buf) != NULL) {
                gchar *current_dir = g_strdup (real_path_buf);
                gchar *usb_serial = NULL;
                for (gint i = 0; i < 12; i++) {
                    g_autofree gchar *sub_serial_path = g_build_filename (current_dir, "serial", NULL);
                    if (g_file_test (sub_serial_path, G_FILE_TEST_EXISTS)) {
                        g_file_get_contents (sub_serial_path, &usb_serial, NULL, NULL);
                        break;
                    }
                    gchar *parent_dir = g_path_get_dirname (current_dir);
                    g_free (current_dir);
                    current_dir = parent_dir;
                    if (g_strcmp0 (current_dir, "/") == 0 || g_strcmp0 (current_dir, "/sys") == 0 || g_strcmp0 (current_dir, ".") == 0) {
                        break;
                    }
                }
                g_free (current_dir);
                if (usb_serial) {
                    g_strstrip (usb_serial);
                    puls_smart_data_set_serial_number (data, usb_serial);
                    g_free (usb_serial);
                } else {
                    puls_smart_data_set_serial_number (data, "N/A");
                }
            } else {
                puls_smart_data_set_serial_number (data, "N/A");
            }
        } else {
            puls_smart_data_set_serial_number (data, "N/A");
        }
    }

    const gchar *fw = puls_smart_data_get_firmware_version (data);
    if (fw == NULL || strlen (fw) == 0 || g_strcmp0 (fw, "Unknown") == 0) {
        g_autofree gchar *fw_contents = puls_sysfs_read_device_file (base, "device/rev", 64);
        if (fw_contents == NULL)
            fw_contents = puls_sysfs_read_device_file (base, "device/firmware_rev", 64);
        puls_smart_data_set_firmware_version (data, fw_contents ? fw_contents : "N/A");
    }

    guint64 cap = puls_smart_data_get_capacity_bytes (data);
    if (cap == 0) {
        guint64 sectors = puls_sysfs_read_device_uint64 (base, "size", 0);
        if (sectors > 0)
            puls_smart_data_set_capacity_bytes (data, sectors * 512ULL);
    }

    const gchar *iface = puls_smart_data_get_interface_type (data);
    if (dtype == PULS_DRIVE_TYPE_USB || transport == PULS_TRANSPORT_USB) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0 || g_ascii_strcasecmp (iface, "usb") == 0) {
            puls_smart_data_set_interface_type (data, "USB");
        }
        PulsUsbBridgeInfo usb_info = { 0 };
        if (puls_sysfs_read_usb_bridge_info (base, &usb_info)) {
            puls_smart_data_set_usb_speed (data, usb_info.speed);
            if (usb_info.bridge_chip)
                puls_smart_data_set_usb_bridge_chip (data, usb_info.bridge_chip);

            const gchar *tmode = puls_smart_data_get_transfer_mode (data);
            if ((tmode == NULL || strlen (tmode) == 0) && usb_info.display_string)
                puls_smart_data_set_transfer_mode (data, usb_info.display_string);

            puls_usb_bridge_info_clear (&usb_info);
        } else {
            const gchar *tmode = puls_smart_data_get_transfer_mode (data);
            if (tmode == NULL || strlen (tmode) == 0) {
                g_autofree gchar *usb_mode = puls_detect_usb_speed_version (base);
                if (usb_mode)
                    puls_smart_data_set_transfer_mode (data, usb_mode);
            }
        }
    } else if (dtype == PULS_DRIVE_TYPE_NVME_SSD || transport == PULS_TRANSPORT_PCIE) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "NVMe");
        }
        PulsPcieLinkInfo pcie_info = { 0 };
        if (puls_sysfs_read_pcie_link (base, &pcie_info)) {
            if (puls_smart_data_get_pcie_current_gen (data) == PULS_PCIE_GEN_UNKNOWN && pcie_info.current_gen != PULS_PCIE_GEN_UNKNOWN)
                puls_smart_data_set_pcie_current_gen (data, pcie_info.current_gen);

            if (puls_smart_data_get_pcie_current_width (data) == 0 && pcie_info.current_width > 0)
                puls_smart_data_set_pcie_current_width (data, pcie_info.current_width);

            if (puls_smart_data_get_pcie_max_gen (data) == PULS_PCIE_GEN_UNKNOWN && pcie_info.max_gen != PULS_PCIE_GEN_UNKNOWN)
                puls_smart_data_set_pcie_max_gen (data, pcie_info.max_gen);

            if (puls_smart_data_get_pcie_max_width (data) == 0 && pcie_info.max_width > 0)
                puls_smart_data_set_pcie_max_width (data, pcie_info.max_width);

            const gchar *tmode = puls_smart_data_get_transfer_mode (data);
            if ((tmode == NULL || strlen (tmode) == 0) && pcie_info.description != NULL)
                puls_smart_data_set_transfer_mode (data, pcie_info.description);

            puls_pcie_link_info_clear (&pcie_info);
        }
    } else if (dtype == PULS_DRIVE_TYPE_HDD || dtype == PULS_DRIVE_TYPE_SATA_SSD) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "SATA");
        }
    } else if (dtype == PULS_DRIVE_TYPE_SAS_SSD || dtype == PULS_DRIVE_TYPE_SAS_HDD) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "SAS");
        }
    } else if (dtype == PULS_DRIVE_TYPE_EMMC) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "eMMC");
        }
    } else if (dtype == PULS_DRIVE_TYPE_SD_CARD) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "SD");
        }
    } else if (dtype == PULS_DRIVE_TYPE_VIRTUAL) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "Virtual");
        }
    }

    PulsQueueAttributes q_attrs;
    if (puls_sysfs_read_queue_attributes (base, &q_attrs)) {
        if (puls_smart_data_get_logical_sector_size (data) == 0 && q_attrs.logical_block_size > 0)
            puls_smart_data_set_logical_sector_size (data, q_attrs.logical_block_size);

        if (puls_smart_data_get_physical_sector_size (data) == 0 && q_attrs.physical_block_size > 0)
            puls_smart_data_set_physical_sector_size (data, q_attrs.physical_block_size);

        if (puls_smart_data_get_discard_granularity (data) == 0 && q_attrs.discard_granularity > 0)
            puls_smart_data_set_discard_granularity (data, q_attrs.discard_granularity);

        if (puls_smart_data_get_discard_max_bytes (data) == 0 && q_attrs.discard_max_bytes > 0)
            puls_smart_data_set_discard_max_bytes (data, q_attrs.discard_max_bytes);

        if (!puls_smart_data_get_supports_trim (data) && q_attrs.supports_discard)
            puls_smart_data_set_supports_trim (data, TRUE);

        if (!puls_smart_data_get_supports_write_cache (data) && q_attrs.write_cache_enabled)
            puls_smart_data_set_supports_write_cache (data, TRUE);

        if (puls_smart_data_get_rotation_rpm (data) < 0) {
            if (!q_attrs.rotational)
                puls_smart_data_set_rotation_rpm (data, 0);
        }
    }

    if (dtype == PULS_DRIVE_TYPE_NVME_SSD && puls_smart_data_get_rotation_rpm (data) < 0) {
        puls_smart_data_set_rotation_rpm (data, 0);
    }

    const gchar *ff = puls_smart_data_get_form_factor (data);
    if (ff == NULL || strlen (ff) == 0 || g_strcmp0 (ff, "Unknown") == 0) {
        if (dtype == PULS_DRIVE_TYPE_NVME_SSD) {
            puls_smart_data_set_form_factor (data, "M.2");
        } else if (dtype == PULS_DRIVE_TYPE_USB) {
            puls_smart_data_set_form_factor (data, "USB Drive / External");
        } else if (dtype == PULS_DRIVE_TYPE_SATA_SSD || dtype == PULS_DRIVE_TYPE_SAS_SSD) {
            puls_smart_data_set_form_factor (data, "2.5-inch");
        } else if (dtype == PULS_DRIVE_TYPE_HDD || dtype == PULS_DRIVE_TYPE_SAS_HDD) {
            puls_smart_data_set_form_factor (data, "3.5-inch / 2.5-inch");
        } else if (dtype == PULS_DRIVE_TYPE_EMMC) {
            puls_smart_data_set_form_factor (data, "Embedded (BGA)");
        } else if (dtype == PULS_DRIVE_TYPE_SD_CARD) {
            puls_smart_data_set_form_factor (data, "SD / MicroSD");
        } else if (dtype == PULS_DRIVE_TYPE_VIRTUAL) {
            puls_smart_data_set_form_factor (data, "Virtual Volume");
        } else {
            puls_smart_data_set_form_factor (data, "Unknown");
        }
    }

    if (puls_smart_data_get_temperature (data) < 0) {
        gint hwmon_temp = puls_sysfs_read_hwmon_temp (base);
        if (hwmon_temp >= 0) {
            puls_smart_data_set_temperature (data, hwmon_temp);
            PULS_LOG_DEBUG ("Drive %s: populated temperature %d °C via sysfs hwmon", base, hwmon_temp);
        }
    }

    GArray *curr_sensors = puls_smart_data_get_thermal_sensors (data);
    if (curr_sensors == NULL || curr_sensors->len == 0) {
        GList *hwmon_sensors = puls_sysfs_read_hwmon_sensors (base);
        for (GList *l = hwmon_sensors; l != NULL; l = l->next) {
            PulsThermalSensor *s = l->data;
            puls_smart_data_add_thermal_sensor (data, s);
        }
        g_list_free_full (hwmon_sensors, (GDestroyNotify)puls_thermal_sensor_free);
    }
}

static void
probe_and_add_device (PulsDiskManager *self,
                      const gchar     *dev_path)
{
    if (g_hash_table_contains (self->devices, dev_path))
        return;

    PulsSmartData *data = NULL;

    if (!self->mock_mode) {
        GError *error = NULL;
        g_autofree gchar *json = puls_run_smartctl_sync (dev_path, &error);
        g_clear_error (&error);

        if (json != NULL) {
            GError *parse_err = NULL;
            data = puls_smart_parser_parse_json (json, &parse_err);
            g_clear_error (&parse_err);
        }
    }

    if (data == NULL)
        data = puls_smart_data_new ();

    if (puls_smart_data_get_device_path (data) == NULL)
        puls_smart_data_set_device_path (data, dev_path);

    puls_smart_data_fill_sysfs_fallbacks (data);

    g_hash_table_insert (self->devices, g_strdup (dev_path), data);
    self->device_paths = g_list_append (self->device_paths, g_strdup (dev_path));
    g_signal_emit (self, signals[SIGNAL_DISK_ADDED], 0, dev_path);
}

/**
 * puls_disk_manager_remove_device:
 * @self: A #PulsDiskManager.
 * @device_path: Block device path such as `/dev/sdb`.
 *
 * Removes a tracked device from the manager and emits `disk-removed`.
 *
 * Returns: %TRUE if the device existed and was removed.
 */
gboolean
puls_disk_manager_remove_device (PulsDiskManager *self,
                                 const gchar     *device_path)
{
    g_return_val_if_fail (PULS_IS_DISK_MANAGER (self), FALSE);
    g_return_val_if_fail (device_path != NULL, FALSE);

    if (!g_hash_table_contains (self->devices, device_path))
        return FALSE;

    g_autofree gchar *path_copy = g_strdup (device_path);

    for (GList *l = self->device_paths; l != NULL; l = l->next) {
        if (g_strcmp0 (l->data, path_copy) == 0) {
            g_free (l->data);
            self->device_paths = g_list_delete_link (self->device_paths, l);
            break;
        }
    }

    g_hash_table_remove (self->devices, path_copy);
    PULS_LOG_INFO ("Hotplug: removed device %s", path_copy);
    g_signal_emit (self, signals[SIGNAL_DISK_REMOVED], 0, path_copy);
    return TRUE;
}

void
puls_disk_manager_scan (PulsDiskManager *self)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (self));

    if (self->mock_mode) {
        PULS_LOG_DEBUG ("Scanning in mock storage simulation mode");
        puls_mock_data_populate_manager (self);
        return;
    }

    GList *found = scan_sys_block ();
    PULS_LOG_DEBUG ("Scanning /sys/block: found %u device path(s)", g_list_length (found));

    /* Collect devices that disappeared since the previous scan */
    GPtrArray *stale = g_ptr_array_new_with_free_func (g_free);
    for (GList *l = self->device_paths; l != NULL; l = l->next) {
        const gchar *existing = l->data;
        if (g_list_find_custom (found, existing, (GCompareFunc)g_strcmp0) == NULL)
            g_ptr_array_add (stale, g_strdup (existing));
    }

    for (guint i = 0; i < stale->len; i++) {
        puls_disk_manager_remove_device (self, g_ptr_array_index (stale, i));
    }
    g_ptr_array_unref (stale);

    for (GList *l = found; l != NULL; l = l->next) {
        probe_and_add_device (self, (const gchar *)l->data);
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
    g_clear_error (&error);

    PulsSmartData *data = NULL;
    if (json) {
        GError *parse_err = NULL;
        data = puls_smart_parser_parse_json (json, &parse_err);
        g_clear_error (&parse_err);
    }

    if (data == NULL) {
        data = puls_smart_data_new ();
    }
    if (puls_smart_data_get_device_path (data) == NULL) {
        puls_smart_data_set_device_path (data, rd->device_path);
    }

    puls_smart_data_fill_sysfs_fallbacks (data);

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

    if (self->mock_mode) {
        PulsSmartData *data = g_hash_table_lookup (self->devices, device_path);
        g_signal_emit (self, signals[SIGNAL_DISK_UPDATED], 0, device_path);
        g_task_return_pointer (task, data ? g_object_ref (data) : NULL, g_object_unref);
        g_object_unref (task);
        return;
    }

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

/**
 * puls_uevent_info_clear:
 * @info: A #PulsUeventInfo structure to reset.
 *
 * Frees owned strings in @info and zeroes its fields.
 */
void
puls_uevent_info_clear (PulsUeventInfo *info)
{
    if (info == NULL)
        return;

    g_clear_pointer (&info->subsystem, g_free);
    g_clear_pointer (&info->devtype, g_free);
    g_clear_pointer (&info->devname, g_free);
    g_clear_pointer (&info->devpath, g_free);
    info->action = PULS_UEVENT_UNKNOWN;
}

static PulsUeventAction
parse_uevent_action (const gchar *val)
{
    if (g_strcmp0 (val, "add") == 0)
        return PULS_UEVENT_ADD;
    if (g_strcmp0 (val, "remove") == 0)
        return PULS_UEVENT_REMOVE;
    if (g_strcmp0 (val, "change") == 0)
        return PULS_UEVENT_CHANGE;
    return PULS_UEVENT_UNKNOWN;
}

static gchar *
normalize_devname (const gchar *val)
{
    if (val == NULL || *val == '\0')
        return NULL;
    if (g_str_has_prefix (val, "/dev/"))
        return g_strdup (val);
    return g_strdup_printf ("/dev/%s", val);
}

/**
 * puls_disk_manager_parse_uevent:
 * @payload: Raw NUL-delimited kernel uevent payload.
 * @len: Payload length in bytes.
 * @out_info: Output #PulsUeventInfo structure.
 *
 * Parses `ACTION`, `SUBSYSTEM`, `DEVTYPE`, `DEVNAME`, and `DEVPATH` fields from
 * a kernel kobject uevent netlink frame.
 *
 * Returns: %TRUE if meaningful uevent metadata was parsed.
 */
gboolean
puls_disk_manager_parse_uevent (const gchar    *payload,
                                gsize           len,
                                PulsUeventInfo *out_info)
{
    g_return_val_if_fail (out_info != NULL, FALSE);

    memset (out_info, 0, sizeof (*out_info));
    if (payload == NULL || len == 0)
        return FALSE;

    gsize offset = 0;
    while (offset < len) {
        const gchar *cur = payload + offset;
        gsize remaining = len - offset;
        gsize tok_len = strnlen (cur, remaining);

        if (tok_len > 0) {
            g_autofree gchar *tok = g_strndup (cur, tok_len);
            const gchar *eq = strchr (tok, '=');

            if (eq != NULL) {
                g_autofree gchar *key = g_strndup (tok, (gsize)(eq - tok));
                const gchar *val = eq + 1;

                if (g_strcmp0 (key, "ACTION") == 0) {
                    out_info->action = parse_uevent_action (val);
                } else if (g_strcmp0 (key, "SUBSYSTEM") == 0) {
                    g_free (out_info->subsystem);
                    out_info->subsystem = g_strdup (val);
                } else if (g_strcmp0 (key, "DEVTYPE") == 0) {
                    g_free (out_info->devtype);
                    out_info->devtype = g_strdup (val);
                } else if (g_strcmp0 (key, "DEVNAME") == 0) {
                    g_free (out_info->devname);
                    out_info->devname = normalize_devname (val);
                } else if (g_strcmp0 (key, "DEVPATH") == 0) {
                    g_free (out_info->devpath);
                    out_info->devpath = g_strdup (val);
                }
            }
        }

        offset += tok_len + 1;
    }

    return (out_info->action != PULS_UEVENT_UNKNOWN || out_info->subsystem != NULL);
}

static gchar *
resolve_uevent_disk_path (const PulsUeventInfo *info)
{
    if (g_strcmp0 (info->subsystem, "block") != 0)
        return NULL;

    if (info->devtype != NULL && g_strcmp0 (info->devtype, "disk") != 0)
        return NULL;

    g_autofree gchar *resolved = g_strdup (info->devname);
    if (resolved == NULL && info->devpath != NULL) {
        g_autofree gchar *base_from_path = g_path_get_basename (info->devpath);
        resolved = normalize_devname (base_from_path);
    }

    if (resolved == NULL)
        return NULL;

    g_autofree gchar *base = g_path_get_basename (resolved);
    if (!is_real_disk (base))
        return NULL;

    return g_steal_pointer (&resolved);
}

/**
 * puls_disk_manager_handle_uevent:
 * @self: A #PulsDiskManager.
 * @payload: Raw NUL-delimited uevent buffer.
 * @len: Length of @payload in bytes.
 *
 * Filters and applies a block-disk uevent (`add`, `remove`, or `change`),
 * updating tracked devices and emitting `disk-added`, `disk-removed`, or
 * `disk-updated`.
 *
 * Returns: %TRUE if the uevent resulted in a tracked disk state update.
 */
gboolean
puls_disk_manager_handle_uevent (PulsDiskManager *self,
                                 const gchar     *payload,
                                 gsize            len)
{
    g_return_val_if_fail (PULS_IS_DISK_MANAGER (self), FALSE);

    PulsUeventInfo info;
    if (!puls_disk_manager_parse_uevent (payload, len, &info)) {
        puls_uevent_info_clear (&info);
        return FALSE;
    }

    g_autofree gchar *resolved_dev = resolve_uevent_disk_path (&info);
    PulsUeventAction action = info.action;
    puls_uevent_info_clear (&info);

    if (resolved_dev == NULL)
        return FALSE;

    switch (action) {
    case PULS_UEVENT_ADD:
        if (!g_hash_table_contains (self->devices, resolved_dev)) {
            PULS_LOG_INFO ("Hotplug uevent: device attached %s", resolved_dev);
            probe_and_add_device (self, resolved_dev);
            return TRUE;
        }
        return FALSE;
    case PULS_UEVENT_REMOVE:
        return puls_disk_manager_remove_device (self, resolved_dev);
    case PULS_UEVENT_CHANGE:
        if (g_hash_table_contains (self->devices, resolved_dev)) {
            if (self->mock_mode) {
                g_signal_emit (self, signals[SIGNAL_DISK_UPDATED], 0, resolved_dev);
            } else {
                puls_disk_manager_refresh_async (self, resolved_dev, NULL, NULL, NULL);
            }
            return TRUE;
        }
        return FALSE;
    case PULS_UEVENT_UNKNOWN:
    default:
        return FALSE;
    }
}

static gboolean
on_debounced_rescan_timeout (gpointer user_data)
{
    PulsDiskManager *self = PULS_DISK_MANAGER (user_data);
    self->rescan_timeout_id = 0;

    if (!self->mock_mode)
        puls_disk_manager_scan (self);

    return G_SOURCE_REMOVE;
}

static void
schedule_debounced_rescan (PulsDiskManager *self)
{
    if (self->rescan_timeout_id > 0)
        g_source_remove (self->rescan_timeout_id);

    self->rescan_timeout_id = g_timeout_add (PULS_HOTPLUG_DEBOUNCE_MS,
                                             on_debounced_rescan_timeout,
                                             self);
}

static void
on_volume_monitor_changed (GVolumeMonitor *monitor G_GNUC_UNUSED,
                           gpointer        item G_GNUC_UNUSED,
                           gpointer        user_data)
{
    PulsDiskManager *self = PULS_DISK_MANAGER (user_data);
    PULS_LOG_DEBUG ("Hotplug: GVolumeMonitor event received, scheduling rescan");
    schedule_debounced_rescan (self);
}

static gboolean
on_udev_socket_io (GSocket      *socket,
                   GIOCondition  condition,
                   gpointer      user_data)
{
    PulsDiskManager *self = PULS_DISK_MANAGER (user_data);

    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        PULS_LOG_DEBUG ("Hotplug: udev netlink socket closed");
        self->udev_source = NULL;
        return G_SOURCE_REMOVE;
    }

    gchar buf[PULS_UEVENT_BUFFER_SIZE];
    GError *err = NULL;
    gssize received = g_socket_receive (socket, buf, sizeof (buf) - 1, NULL, &err);

    if (received < 0) {
        if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            PULS_LOG_DEBUG ("Hotplug: udev receive error: %s", err ? err->message : "unknown");
        g_clear_error (&err);
        return G_SOURCE_CONTINUE;
    }

    if (received > 0 && !self->mock_mode) {
        buf[received] = '\0';
        puls_disk_manager_handle_uevent (self, buf, (gsize)received);
    }

    return G_SOURCE_CONTINUE;
}

static void
start_udev_netlink_listener (PulsDiskManager *self)
{
    int fd = socket (AF_NETLINK,
                     SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                     NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        PULS_LOG_DEBUG ("Hotplug: netlink socket unavailable (%s)", g_strerror (errno));
        return;
    }

    struct sockaddr_nl addr;
    memset (&addr, 0, sizeof (addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = 0;
    addr.nl_groups = 1; /* Kernel uevent multicast group */

    if (bind (fd, (struct sockaddr *)&addr, sizeof (addr)) != 0) {
        PULS_LOG_DEBUG ("Hotplug: netlink bind failed (%s)", g_strerror (errno));
        close (fd);
        return;
    }

    GError *err = NULL;
    self->udev_socket = g_socket_new_from_fd (fd, &err);
    if (self->udev_socket == NULL) {
        PULS_LOG_DEBUG ("Hotplug: failed to wrap netlink fd: %s", err ? err->message : "unknown");
        g_clear_error (&err);
        close (fd);
        return;
    }

    self->udev_source = g_socket_create_source (self->udev_socket, G_IO_IN, NULL);
    g_source_set_callback (self->udev_source,
                           G_SOURCE_FUNC (on_udev_socket_io),
                           self,
                           NULL);
    g_source_attach (self->udev_source, g_main_context_default ());
    PULS_LOG_INFO ("Hotplug: kernel udev netlink listener active");
}

/**
 * puls_disk_manager_start_monitoring:
 * @self: A #PulsDiskManager.
 *
 * Starts real-time drive hotplug monitoring using both a Linux kernel udev
 * netlink listener (`NETLINK_KOBJECT_UEVENT`) and GIO #GVolumeMonitor signals.
 * Safe to call repeatedly; falls back gracefully if netlink is unavailable.
 */
void
puls_disk_manager_start_monitoring (PulsDiskManager *self)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (self));

    if (self->monitoring)
        return;

    self->monitoring = TRUE;

    start_udev_netlink_listener (self);

    self->volume_monitor = g_volume_monitor_get ();
    if (self->volume_monitor != NULL) {
        self->drive_connected_id =
            g_signal_connect (self->volume_monitor, "drive-connected",
                              G_CALLBACK (on_volume_monitor_changed), self);
        self->drive_disconnected_id =
            g_signal_connect (self->volume_monitor, "drive-disconnected",
                              G_CALLBACK (on_volume_monitor_changed), self);
        self->volume_added_id =
            g_signal_connect (self->volume_monitor, "volume-added",
                              G_CALLBACK (on_volume_monitor_changed), self);
        self->volume_removed_id =
            g_signal_connect (self->volume_monitor, "volume-removed",
                              G_CALLBACK (on_volume_monitor_changed), self);
        PULS_LOG_INFO ("Hotplug: GVolumeMonitor drive/volume listeners active");
    }
}

/**
 * puls_disk_manager_stop_monitoring:
 * @self: A #PulsDiskManager.
 *
 * Stops hotplug listeners and releases any attached main-loop sources.
 */
void
puls_disk_manager_stop_monitoring (PulsDiskManager *self)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (self));

    if (self->rescan_timeout_id > 0) {
        g_source_remove (self->rescan_timeout_id);
        self->rescan_timeout_id = 0;
    }

    if (self->udev_source != NULL) {
        g_source_destroy (self->udev_source);
        g_source_unref (self->udev_source);
        self->udev_source = NULL;
    }

    g_clear_object (&self->udev_socket);

    if (self->volume_monitor != NULL) {
        if (self->drive_connected_id > 0) {
            g_signal_handler_disconnect (self->volume_monitor, self->drive_connected_id);
            self->drive_connected_id = 0;
        }
        if (self->drive_disconnected_id > 0) {
            g_signal_handler_disconnect (self->volume_monitor, self->drive_disconnected_id);
            self->drive_disconnected_id = 0;
        }
        if (self->volume_added_id > 0) {
            g_signal_handler_disconnect (self->volume_monitor, self->volume_added_id);
            self->volume_added_id = 0;
        }
        if (self->volume_removed_id > 0) {
            g_signal_handler_disconnect (self->volume_monitor, self->volume_removed_id);
            self->volume_removed_id = 0;
        }
        g_clear_object (&self->volume_monitor);
    }

    self->monitoring = FALSE;
}

/**
 * puls_disk_manager_is_monitoring:
 * @self: A #PulsDiskManager.
 *
 * Returns: %TRUE if hotplug monitoring is currently active.
 */
gboolean
puls_disk_manager_is_monitoring (PulsDiskManager *self)
{
    g_return_val_if_fail (PULS_IS_DISK_MANAGER (self), FALSE);
    return self->monitoring;
}
