/*
 * puls-disk-manager.h
 *
 * Disk enumeration and monitoring via UDisks2 D-Bus / sysfs fallback.
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
#include <gio/gio.h>
#include "puls-smart-data.h"

G_BEGIN_DECLS

#define PULS_TYPE_DISK_MANAGER (puls_disk_manager_get_type ())
G_DECLARE_FINAL_TYPE (PulsDiskManager, puls_disk_manager, PULS, DISK_MANAGER, GObject)

/**
 * PulsUeventAction:
 * @PULS_UEVENT_UNKNOWN: Unknown or unhandled uevent action.
 * @PULS_UEVENT_ADD: Device added (`ACTION=add`).
 * @PULS_UEVENT_REMOVE: Device removed (`ACTION=remove`).
 * @PULS_UEVENT_CHANGE: Device state changed (`ACTION=change`).
 */
typedef enum {
    PULS_UEVENT_UNKNOWN = 0,
    PULS_UEVENT_ADD,
    PULS_UEVENT_REMOVE,
    PULS_UEVENT_CHANGE
} PulsUeventAction;

/**
 * PulsUeventInfo:
 * Parsed metadata from a Linux kernel kobject uevent netlink message.
 */
typedef struct {
    PulsUeventAction action;
    gchar *subsystem;
    gchar *devtype;
    gchar *devname;
    gchar *devpath;
} PulsUeventInfo;

PulsDiskManager *puls_disk_manager_new           (void);
void             puls_disk_manager_scan           (PulsDiskManager *self);
GList           *puls_disk_manager_get_devices    (PulsDiskManager *self);
guint            puls_disk_manager_get_device_count (PulsDiskManager *self);

PulsSmartData   *puls_disk_manager_get_smart_data (PulsDiskManager *self,
                                                   const gchar     *device_path);
void             puls_disk_manager_refresh_async  (PulsDiskManager     *self,
                                                   const gchar         *device_path,
                                                   GCancellable        *cancellable,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data);
PulsSmartData   *puls_disk_manager_refresh_finish (PulsDiskManager *self,
                                                   GAsyncResult    *result,
                                                   GError         **error);


void             puls_disk_manager_set_mock_mode  (PulsDiskManager     *self,
                                                   gboolean             mock_mode);
gboolean         puls_disk_manager_get_mock_mode  (PulsDiskManager     *self);
void             puls_disk_manager_add_mock_device (PulsDiskManager    *self,
                                                    const gchar         *device_path,
                                                    PulsSmartData       *data);
gboolean         puls_disk_manager_remove_device  (PulsDiskManager     *self,
                                                   const gchar         *device_path);

void             puls_uevent_info_clear           (PulsUeventInfo      *info);
gboolean         puls_disk_manager_parse_uevent   (const gchar         *payload,
                                                   gsize                len,
                                                   PulsUeventInfo      *out_info);
gboolean         puls_disk_manager_handle_uevent  (PulsDiskManager     *self,
                                                   const gchar         *payload,
                                                   gsize                len);
void             puls_disk_manager_start_monitoring (PulsDiskManager   *self);
void             puls_disk_manager_stop_monitoring  (PulsDiskManager   *self);
gboolean         puls_disk_manager_is_monitoring    (PulsDiskManager   *self);

G_END_DECLS
