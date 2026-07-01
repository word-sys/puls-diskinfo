/*
 * puls-surface-scan.h
 *
 * Non-destructive (read-only) disk surface scan.
 * Reads every sector of the device via O_DIRECT pread() and reports
 * which sectors are OK, slow (>500ms), or unreadable (bad).
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
    PULS_SECTOR_UNKNOWN   = 0,
    PULS_SECTOR_OK        = 1,
    PULS_SECTOR_SLOW      = 2,   /* read time > SLOW_THRESHOLD_MS */
    PULS_SECTOR_ERROR     = 3,   /* unreadable */
} PulsSectorState;

#define PULS_SURFACE_SLOW_THRESHOLD_MS  500   /* ms */
#define PULS_SURFACE_BLOCK_SIZE         (512 * 1024)  /* 512 KiB per read */

typedef struct {
    guint64 total_sectors;
    guint64 sectors_ok;
    guint64 sectors_slow;
    guint64 sectors_error;
    guint64 sectors_scanned;
} PulsSurfaceScanResult;

typedef void (*PulsSurfaceProgressFunc) (guint64 scanned,
                                         guint64 total,
                                         guint64 lba,
                                         PulsSectorState state,
                                         gpointer user_data);

typedef void (*PulsSurfaceFinishedFunc) (const PulsSurfaceScanResult *result,
                                         gboolean                     cancelled,
                                         const gchar                 *error_msg,
                                         gpointer                     user_data);

void puls_surface_scan_run_async (const gchar            *device_path,
                                  GCancellable           *cancellable,
                                  PulsSurfaceProgressFunc progress_cb,
                                  PulsSurfaceFinishedFunc finished_cb,
                                  gpointer                user_data);

G_END_DECLS
