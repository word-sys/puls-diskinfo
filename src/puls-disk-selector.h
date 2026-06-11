/*
 * puls-disk-selector.h
 *
 * Horizontal disk selector bar widget.
 *
 * Copyright (C) 2024 Puls DiskInfo Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <gtk/gtk.h>
#include "puls-smart-data.h"
#include "puls-disk-manager.h"

G_BEGIN_DECLS

#define PULS_TYPE_DISK_SELECTOR (puls_disk_selector_get_type ())
G_DECLARE_FINAL_TYPE (PulsDiskSelector, puls_disk_selector,
                      PULS, DISK_SELECTOR, GtkWidget)

GtkWidget   *puls_disk_selector_new          (PulsDiskManager *manager);
const gchar *puls_disk_selector_get_selected (PulsDiskSelector *self);
void         puls_disk_selector_select       (PulsDiskSelector *self,
                                              const gchar      *device_path);
void         puls_disk_selector_refresh      (PulsDiskSelector *self);

/* Signal: "disk-selected" (const gchar *device_path) */

G_END_DECLS
