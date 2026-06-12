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

/* Signals: "disk-added", "disk-removed", "disk-updated" */

G_END_DECLS
