/*
 * puls-disk-info-view.h
 *
 * Per-disk detail panel widget.
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

G_BEGIN_DECLS

#define PULS_TYPE_DISK_INFO_VIEW (puls_disk_info_view_get_type ())
G_DECLARE_FINAL_TYPE (PulsDiskInfoView, puls_disk_info_view,
                      PULS, DISK_INFO_VIEW, GtkWidget)

GtkWidget *puls_disk_info_view_new       (void);
void       puls_disk_info_view_set_data  (PulsDiskInfoView *self,
                                          PulsSmartData    *data);

G_END_DECLS
