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
#include "puls-utils.h"
#include "../helper/puls-helper-protocol.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <errno.h>
#include <string.h>
#include <time.h>


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

typedef struct {
    ScanTaskData          *task_data;
    GCancellable          *cancellable;
    PulsSurfaceScanResult *result;
    guint64                last_emit_ms;
} HelperScanCtx;

static gboolean
helper_stream_chunk_cb (guint64      seq G_GNUC_UNUSED,
                        const gchar *chunk_payload,
                        gpointer     user_data)
{
    HelperScanCtx *ctx = (HelperScanCtx *)user_data;

    PulsHelperStreamChunk chunk = { 0 };
    if (puls_helper_parse_stream_chunk_json (chunk_payload, &chunk)) {
        guint64 sectors_in_chunk = chunk.chunk_bytes / 512;
        guint64 lba = chunk.offset / 512;
        ctx->result->sectors_scanned += sectors_in_chunk;

        PulsSectorState state = (chunk.chunk_ms >= (gdouble)PULS_SURFACE_SLOW_THRESHOLD_MS)
                                ? PULS_SECTOR_SLOW
                                : PULS_SECTOR_OK;
        if (state == PULS_SECTOR_SLOW)
            ctx->result->sectors_slow += sectors_in_chunk;
        else
            ctx->result->sectors_ok += sectors_in_chunk;

        guint64 now_ms = (guint64)(g_get_monotonic_time () / 1000);
        if (state != PULS_SECTOR_OK || (now_ms - ctx->last_emit_ms) >= 50) {
            ctx->last_emit_ms = now_ms;
            emit_progress (ctx->task_data->progress_cb,
                           ctx->result->sectors_scanned,
                           ctx->result->total_sectors,
                           lba, state, ctx->task_data->user_data);
        }
    }

    return !g_cancellable_is_cancelled (ctx->cancellable);
}

static void
run_helper_stream_surface_scan (ScanTaskData          *d,
                                GCancellable          *cancellable,
                                guint64                dev_size,
                                PulsSurfaceScanResult *result)
{
    if (dev_size == 0) {
        emit_finished (d->finished_cb, result, FALSE,
                       "Cannot determine device size for helper surface scan",
                       d->user_data);
        return;
    }

    result->total_sectors = dev_size / 512;
    gboolean scan_cancelled = FALSE;
    g_autofree gchar *error_msg = NULL;

    HelperScanCtx hctx = {
        .task_data    = d,
        .cancellable  = cancellable,
        .result       = result,
        .last_emit_ms = 0,
    };

    guint64 offset = 0;
    while (offset < dev_size) {
        if (g_cancellable_is_cancelled (cancellable)) {
            scan_cancelled = TRUE;
            break;
        }

        guint64 window = MIN ((guint64)PULS_HELPER_MAX_STREAM_SCAN_LEN, dev_size - offset);
        guint64 chunk_sz = MIN ((guint64)PULS_SURFACE_BLOCK_SIZE, window);
        if (chunk_sz < PULS_HELPER_MIN_STREAM_CHUNK)
            chunk_sz = PULS_HELPER_MIN_STREAM_CHUNK;

        g_autoptr(GError) err = NULL;
        g_autofree gchar *summary = NULL;
        if (!puls_run_stream_scan_sync (d->device_path, offset, window, chunk_sz,
                                        helper_stream_chunk_cb, &hctx,
                                        &summary, &err)) {
            if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
                g_cancellable_is_cancelled (cancellable)) {
                scan_cancelled = TRUE;
                break;
            }
            guint64 sectors_in_window = window / 512;
            result->sectors_error += sectors_in_window;
            result->sectors_scanned += sectors_in_window;
            emit_progress (d->progress_cb,
                           result->sectors_scanned, result->total_sectors,
                           offset / 512, PULS_SECTOR_ERROR, d->user_data);
            error_msg = g_strdup (err ? err->message : "Privileged helper surface scan failed");
            break;
        }

        offset += window;
    }

    emit_finished (d->finished_cb, result, scan_cancelled, error_msg, d->user_data);
}

static void
surface_scan_thread (GTask        *task G_GNUC_UNUSED,
                     gpointer      source_object G_GNUC_UNUSED,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
    ScanTaskData *d = task_data;

    PulsSurfaceScanResult result = { 0 };
    gboolean scan_cancelled = FALSE;
    gchar   *error_msg      = NULL;

    if (!puls_device_path_is_valid (d->device_path)) {
        error_msg = g_strdup_printf ("Invalid or unsafe device path: %s",
                                     d->device_path ? d->device_path : "(null)");
        emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
        g_free (error_msg);
        return;
    }

    const gchar *dev_name = strrchr (d->device_path, '/');
    dev_name = dev_name ? dev_name + 1 : d->device_path;

    guint64 dev_size = 0;
    g_autofree gchar *sysfs_size_path = g_build_filename ("/sys/block", dev_name, "size", NULL);
    g_autofree gchar *size_str = NULL;
    if (g_file_get_contents (sysfs_size_path, &size_str, NULL, NULL)) {
        dev_size = (guint64)g_ascii_strtoull (size_str, NULL, 10) * 512ULL;
    }

    /* Zero-elevation direct read with O_NOFOLLOW when device permissions allow */
    int fd = open (d->device_path, O_RDONLY | O_DIRECT | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        fd = open (d->device_path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    }

    if (fd < 0 && errno == EACCES) {
        /* Route through hardened Protocol v2 STREAM_SCAN helper instead of shell dd */
        run_helper_stream_surface_scan (d, cancellable, dev_size, &result);
        return;
    }

    if (fd < 0) {
        error_msg = g_strdup_printf ("Cannot open %s: %s",
                                     d->device_path, strerror (errno));
        emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
        g_free (error_msg);
        return;
    }

    if (dev_size == 0) {
        if (ioctl (fd, BLKGETSIZE64, &dev_size) < 0 || dev_size == 0) {
            error_msg = g_strdup ("Cannot determine device size");
            close (fd);
            emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
            g_free (error_msg);
            return;
        }
    }

    const gsize block_size = PULS_SURFACE_BLOCK_SIZE;
    void *buf = NULL;
    if (posix_memalign (&buf, 4096, block_size) != 0) {
        error_msg = g_strdup ("Memory allocation failed");
        close (fd);
        emit_finished (d->finished_cb, &result, FALSE, error_msg, d->user_data);
        g_free (error_msg);
        return;
    }

    result.total_sectors = dev_size / 512;

    guint64 offset = 0;
    guint64 last_emit_ms = 0;

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
        guint64 now_ms = ((guint64)t1.tv_sec) * 1000 + ((guint64)t1.tv_nsec) / 1000000;

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

        if (state != PULS_SECTOR_OK || (now_ms - last_emit_ms) >= 50) {
            last_emit_ms = now_ms;
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
