/*
 * puls-surface-scan.c
 *
 * Non-destructive (read-only) disk surface scan implementation.
 * Runs in a GTask thread so the GUI stays responsive.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#define _GNU_SOURCE
#include "puls-surface-scan.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <errno.h>
#include <string.h>
#include <time.h>

/* ── Internal task data ──────────────────────────────────────── */

typedef struct {
    gchar                  *device_path;
    GCancellable           *cancellable;
    PulsSurfaceProgressFunc progress_cb;
    PulsSurfaceFinishedFunc finished_cb;
    gpointer                user_data;
} ScanTaskData;

static void
scan_task_data_free (ScanTaskData *d)
{
    g_free (d->device_path);
    g_clear_object (&d->cancellable);
    g_free (d);
}

/* Progress emission on main thread */
typedef struct {
    PulsSurfaceProgressFunc cb;
    guint64                 scanned;
    guint64                 total;
    guint64                 lba;
    PulsSectorState         state;
    gpointer                user_data;
} ProgressIdleData;

static gboolean
emit_progress_idle (gpointer data)
{
    ProgressIdleData *p = data;
    p->cb (p->scanned, p->total, p->lba, p->state, p->user_data);
    g_free (p);
    return G_SOURCE_REMOVE;
}

static void
emit_progress (PulsSurfaceProgressFunc cb,
               guint64 scanned, guint64 total,
               guint64 lba, PulsSectorState state,
               gpointer user_data)
{
    ProgressIdleData *p = g_new0 (ProgressIdleData, 1);
    p->cb        = cb;
    p->scanned   = scanned;
    p->total     = total;
    p->lba       = lba;
    p->state     = state;
    p->user_data = user_data;
    g_idle_add (emit_progress_idle, p);
}

/* Finished emission on main thread */
typedef struct {
    PulsSurfaceFinishedFunc  cb;
    PulsSurfaceScanResult    result;
    gboolean                 cancelled;
    gchar                   *error_msg;
    gpointer                 user_data;
} FinishedIdleData;

static gboolean
emit_finished_idle (gpointer data)
{
    FinishedIdleData *f = data;
    f->cb (&f->result, f->cancelled, f->error_msg, f->user_data);
    g_free (f->error_msg);
    g_free (f);
    return G_SOURCE_REMOVE;
}

static void
emit_finished (PulsSurfaceFinishedFunc cb,
               const PulsSurfaceScanResult *result,
               gboolean cancelled, const gchar *error_msg,
               gpointer user_data)
{
    FinishedIdleData *f = g_new0 (FinishedIdleData, 1);
    f->cb        = cb;
    f->result    = *result;
    f->cancelled = cancelled;
    f->error_msg = g_strdup (error_msg);
    f->user_data = user_data;
    g_idle_add (emit_finished_idle, f);
}

/* ── Worker thread ───────────────────────────────────────────── */

static void
surface_scan_thread (GTask        *task,
                     gpointer      source_object G_GNUC_UNUSED,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
    ScanTaskData *d = task_data;

    PulsSurfaceScanResult result = { 0 };
    gboolean scan_cancelled = FALSE;
    gchar   *error_msg      = NULL;

    /* Open device read-only; use O_DIRECT if possible to bypass cache */
    int fd = open (d->device_path, O_RDONLY | O_DIRECT | O_CLOEXEC);
    if (fd < 0) {
        /* Try without O_DIRECT */
        fd = open (d->device_path, O_RDONLY | O_CLOEXEC);
    }
    if (fd < 0) {
        error_msg = g_strdup_printf ("Cannot open %s: %s",
                                     d->device_path, strerror (errno));
        emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
        g_free (error_msg);
        return;
    }

    /* Get device size in bytes */
    guint64 dev_size = 0;
    if (ioctl (fd, BLKGETSIZE64, &dev_size) < 0) {
        error_msg = g_strdup_printf ("Cannot determine device size for %s: %s",
                                     d->device_path, strerror (errno));
        close (fd);
        emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
        g_free (error_msg);
        return;
    }

    if (dev_size == 0) {
        error_msg = g_strdup ("Device reported zero size");
        close (fd);
        emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
        g_free (error_msg);
        return;
    }

    /* We read in blocks of PULS_SURFACE_BLOCK_SIZE, aligned for O_DIRECT */
    const gsize block_size = PULS_SURFACE_BLOCK_SIZE;
    void *buf = NULL;
    if (posix_memalign (&buf, 4096, block_size) != 0) {
        error_msg = g_strdup ("Memory allocation failed");
        close (fd);
        emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
        g_free (error_msg);
        return;
    }

    guint64 total_blocks = (dev_size + block_size - 1) / block_size;
    result.total_sectors = dev_size / 512;

    guint64 offset = 0;
    guint64 blocks_done = 0;
    /* Emit progress every N blocks to avoid flooding the main loop */
    const guint64 PROGRESS_EVERY = MAX (1, total_blocks / 2000);

    while (offset < dev_size) {
        if (g_cancellable_is_cancelled (cancellable)) {
            scan_cancelled = TRUE;
            break;
        }

        gsize to_read = (gsize)MIN ((guint64)block_size, dev_size - offset);
        guint64 lba = offset / 512;

        struct timespec t0, t1;
        clock_gettime (CLOCK_MONOTONIC, &t0);
        ssize_t nr = pread (fd, buf, to_read, (off_t)offset);
        clock_gettime (CLOCK_MONOTONIC, &t1);

        guint64 elapsed_ms = ((guint64)(t1.tv_sec - t0.tv_sec)) * 1000
                           + ((guint64)(t1.tv_nsec - t0.tv_nsec)) / 1000000;

        guint64 sectors_in_block = to_read / 512;
        result.sectors_scanned += sectors_in_block;

        PulsSectorState state;
        if (nr < 0 || (gsize)nr < to_read) {
            state = PULS_SECTOR_ERROR;
            result.sectors_error += sectors_in_block;
        } else if (elapsed_ms >= PULS_SURFACE_SLOW_THRESHOLD_MS) {
            state = PULS_SECTOR_SLOW;
            result.sectors_slow += sectors_in_block;
        } else {
            state = PULS_SECTOR_OK;
            result.sectors_ok += sectors_in_block;
        }

        blocks_done++;
        if ((blocks_done % PROGRESS_EVERY) == 0) {
            emit_progress (d->progress_cb,
                           result.sectors_scanned, result.total_sectors,
                           lba, state, d->user_data);
        }

        offset += to_read;
    }

    free (buf);
    close (fd);

    emit_finished (d->finished_cb, &result, scan_cancelled, error_msg, d->user_data);
}

/* ── Public API ──────────────────────────────────────────────── */

void
puls_surface_scan_run_async (const gchar            *device_path,
                              GCancellable           *cancellable,
                              PulsSurfaceProgressFunc progress_cb,
                              PulsSurfaceFinishedFunc finished_cb,
                              gpointer                user_data)
{
    g_return_if_fail (device_path != NULL);
    g_return_if_fail (finished_cb != NULL);

    ScanTaskData *d = g_new0 (ScanTaskData, 1);
    d->device_path  = g_strdup (device_path);
    d->cancellable  = cancellable ? g_object_ref (cancellable) : g_cancellable_new ();
    d->progress_cb  = progress_cb;
    d->finished_cb  = finished_cb;
    d->user_data    = user_data;

    GTask *task = g_task_new (NULL, d->cancellable, NULL, NULL);
    g_task_set_task_data (task, d, (GDestroyNotify)scan_task_data_free);
    g_task_run_in_thread (task, surface_scan_thread);
    g_object_unref (task);
}
