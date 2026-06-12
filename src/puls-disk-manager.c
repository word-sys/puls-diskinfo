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

    const gchar *model = puls_smart_data_get_model_name (data);
    if (model == NULL || strlen (model) == 0 || g_strcmp0 (model, "Unknown") == 0) {
        g_autofree gchar *vendor_path = g_strdup_printf ("/sys/block/%s/device/vendor", base);
        g_autofree gchar *model_path = g_strdup_printf ("/sys/block/%s/device/model", base);
        g_autofree gchar *name_path = g_strdup_printf ("/sys/block/%s/device/name", base);
        g_autofree gchar *vendor_contents = NULL;
        g_autofree gchar *model_contents = NULL;
        g_autofree gchar *name_contents = NULL;

        if (g_file_get_contents (model_path, &model_contents, NULL, NULL)) {
            g_strstrip (model_contents);
            if (g_file_get_contents (vendor_path, &vendor_contents, NULL, NULL)) {
                g_strstrip (vendor_contents);
                g_autofree gchar *full_model = g_strdup_printf ("%s %s", vendor_contents, model_contents);
                puls_smart_data_set_model_name (data, full_model);
            } else {
                puls_smart_data_set_model_name (data, model_contents);
            }
        } else if (g_file_get_contents (name_path, &name_contents, NULL, NULL)) {
            g_strstrip (name_contents);
            puls_smart_data_set_model_name (data, name_contents);
        } else {
            puls_smart_data_set_model_name (data, "Generic Storage Device");
        }
    }

    const gchar *serial = puls_smart_data_get_serial_number (data);
    if (serial == NULL || strlen (serial) == 0 || g_strcmp0 (serial, "Unknown") == 0) {
        g_autofree gchar *serial_path = g_strdup_printf ("/sys/block/%s/device/serial", base);
        g_autofree gchar *cid_path = g_strdup_printf ("/sys/block/%s/device/cid", base);
        g_autofree gchar *serial_contents = NULL;
        g_autofree gchar *cid_contents = NULL;

        if (g_file_get_contents (serial_path, &serial_contents, NULL, NULL)) {
            g_strstrip (serial_contents);
            puls_smart_data_set_serial_number (data, serial_contents);
        } else if (g_file_get_contents (cid_path, &cid_contents, NULL, NULL)) {
            g_strstrip (cid_contents);
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
        g_autofree gchar *rev_path = g_strdup_printf ("/sys/block/%s/device/rev", base);
        g_autofree gchar *fw_rev_path = g_strdup_printf ("/sys/block/%s/device/firmware_rev", base);
        g_autofree gchar *fw_contents = NULL;

        if (g_file_get_contents (rev_path, &fw_contents, NULL, NULL)) {
            g_strstrip (fw_contents);
            puls_smart_data_set_firmware_version (data, fw_contents);
        } else if (g_file_get_contents (fw_rev_path, &fw_contents, NULL, NULL)) {
            g_strstrip (fw_contents);
            puls_smart_data_set_firmware_version (data, fw_contents);
        } else {
            puls_smart_data_set_firmware_version (data, "N/A");
        }
    }

    guint64 cap = puls_smart_data_get_capacity_bytes (data);
    if (cap == 0) {
        g_autofree gchar *size_path = g_strdup_printf ("/sys/block/%s/size", base);
        g_autofree gchar *size_contents = NULL;
        if (g_file_get_contents (size_path, &size_contents, NULL, NULL)) {
            g_strstrip (size_contents);
            guint64 sectors = g_ascii_strtoull (size_contents, NULL, 10);
            puls_smart_data_set_capacity_bytes (data, sectors * 512ULL);
        }
    }

    const gchar *iface = puls_smart_data_get_interface_type (data);
    if (dtype == PULS_DRIVE_TYPE_USB) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0 || g_ascii_strcasecmp (iface, "usb") == 0) {
            puls_smart_data_set_interface_type (data, "USB");
        }
        const gchar *tmode = puls_smart_data_get_transfer_mode (data);
        if (tmode == NULL || strlen (tmode) == 0) {
            g_autofree gchar *usb_mode = puls_detect_usb_speed_version (base);
            if (usb_mode) {
                puls_smart_data_set_transfer_mode (data, usb_mode);
            }
        }
    } else if (dtype == PULS_DRIVE_TYPE_NVME_SSD) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "NVMe");
        }
        const gchar *tmode = puls_smart_data_get_transfer_mode (data);
        if (tmode == NULL || strlen (tmode) == 0) {
            g_autofree gchar *curr_speed_path = g_strdup_printf ("/sys/block/%s/device/device/current_link_speed", base);
            g_autofree gchar *curr_width_path = g_strdup_printf ("/sys/block/%s/device/device/current_link_width", base);
            g_autofree gchar *max_speed_path = g_strdup_printf ("/sys/block/%s/device/device/max_link_speed", base);
            g_autofree gchar *max_width_path = g_strdup_printf ("/sys/block/%s/device/device/max_link_width", base);

            g_autofree gchar *curr_speed = NULL;
            g_autofree gchar *curr_width = NULL;
            g_autofree gchar *max_speed = NULL;
            g_autofree gchar *max_width = NULL;

            g_file_get_contents (curr_speed_path, &curr_speed, NULL, NULL);
            g_file_get_contents (curr_width_path, &curr_width, NULL, NULL);
            g_file_get_contents (max_speed_path, &max_speed, NULL, NULL);
            g_file_get_contents (max_width_path, &max_width, NULL, NULL);

            if (curr_speed && curr_width) {
                g_strstrip (curr_speed);
                g_strstrip (curr_width);
                
                const gchar *curr_pcie = "PCIe";
                if (strstr (curr_speed, "2.5 GT/s")) curr_pcie = "PCIe 1.0";
                else if (strstr (curr_speed, "5.0 GT/s")) curr_pcie = "PCIe 2.0";
                else if (strstr (curr_speed, "8.0 GT/s")) curr_pcie = "PCIe 3.0";
                else if (strstr (curr_speed, "16.0 GT/s")) curr_pcie = "PCIe 4.0";
                else if (strstr (curr_speed, "32.0 GT/s")) curr_pcie = "PCIe 5.0";
                else if (strstr (curr_speed, "64.0 GT/s")) curr_pcie = "PCIe 6.0";

                g_autofree gchar *curr_full = g_strdup_printf ("%s x%s", curr_pcie, curr_width);

                if (max_speed && max_width) {
                    g_strstrip (max_speed);
                    g_strstrip (max_width);

                    const gchar *max_pcie = "PCIe";
                    if (strstr (max_speed, "2.5 GT/s")) max_pcie = "PCIe 1.0";
                    else if (strstr (max_speed, "5.0 GT/s")) max_pcie = "PCIe 2.0";
                    else if (strstr (max_speed, "8.0 GT/s")) max_pcie = "PCIe 3.0";
                    else if (strstr (max_speed, "16.0 GT/s")) max_pcie = "PCIe 4.0";
                    else if (strstr (max_speed, "32.0 GT/s")) max_pcie = "PCIe 5.0";
                    else if (strstr (max_speed, "64.0 GT/s")) max_pcie = "PCIe 6.0";

                    g_autofree gchar *max_full = g_strdup_printf ("%s x%s", max_pcie, max_width);
                    g_autofree gchar *full_mode = g_strdup_printf ("%s | %s", curr_full, max_full);
                    puls_smart_data_set_transfer_mode (data, full_mode);
                } else {
                    puls_smart_data_set_transfer_mode (data, curr_full);
                }
            }
        }
    } else if (dtype == PULS_DRIVE_TYPE_HDD || dtype == PULS_DRIVE_TYPE_SATA_SSD) {
        if (iface == NULL || strlen (iface) == 0 || g_strcmp0 (iface, "Unknown") == 0) {
            puls_smart_data_set_interface_type (data, "SATA");
        }
    }

    guint32 lbs = puls_smart_data_get_logical_sector_size (data);
    if (lbs == 0) {
        g_autofree gchar *lbs_path = g_strdup_printf ("/sys/block/%s/queue/logical_block_size", base);
        g_autofree gchar *lbs_contents = NULL;
        if (g_file_get_contents (lbs_path, &lbs_contents, NULL, NULL)) {
            g_strstrip (lbs_contents);
            puls_smart_data_set_logical_sector_size (data, (guint32)g_ascii_strtoull (lbs_contents, NULL, 10));
        }
    }

    guint32 pbs = puls_smart_data_get_physical_sector_size (data);
    if (pbs == 0) {
        g_autofree gchar *pbs_path = g_strdup_printf ("/sys/block/%s/queue/physical_block_size", base);
        g_autofree gchar *pbs_contents = NULL;
        if (g_file_get_contents (pbs_path, &pbs_contents, NULL, NULL)) {
            g_strstrip (pbs_contents);
            puls_smart_data_set_physical_sector_size (data, (guint32)g_ascii_strtoull (pbs_contents, NULL, 10));
        }
    }

    const gchar *ff = puls_smart_data_get_form_factor (data);
    if (ff == NULL || strlen (ff) == 0 || g_strcmp0 (ff, "Unknown") == 0) {
        if (dtype == PULS_DRIVE_TYPE_NVME_SSD) {
            puls_smart_data_set_form_factor (data, "M.2");
        } else if (dtype == PULS_DRIVE_TYPE_USB) {
            puls_smart_data_set_form_factor (data, "USB Drive / External");
        } else if (dtype == PULS_DRIVE_TYPE_SATA_SSD) {
            puls_smart_data_set_form_factor (data, "2.5-inch");
        } else if (dtype == PULS_DRIVE_TYPE_HDD) {
            puls_smart_data_set_form_factor (data, "3.5-inch / 2.5-inch");
        } else {
            puls_smart_data_set_form_factor (data, "Unknown");
        }
    }
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
                GError *parse_err = NULL;
                data = puls_smart_parser_parse_json (json, &parse_err);
                g_clear_error (&parse_err);
            }

            if (data == NULL) {
                data = puls_smart_data_new ();
            }
            if (puls_smart_data_get_device_path (data) == NULL) {
                puls_smart_data_set_device_path (data, dev_path);
            }

            puls_smart_data_fill_sysfs_fallbacks (data);

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
