/*
 * puls-benchmark.c
 *
 * Disk performance benchmark implementation.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib/gstdio.h>

#include "puls-benchmark.h"

typedef struct {
    gchar *test_directory;
    guint num_runs;
    guint64 test_size_bytes;
    PulsBenchmarkProgressCallback progress_cb;
    PulsBenchmarkFinishedCallback finished_cb;
    GCancellable *cancellable;
    gpointer user_data;

    PulsBenchmarkResult results[PULS_BENCHMARK_TEST_COUNT];
    gchar *error_msg;
    gboolean cancelled;
} BenchmarkRunner;

typedef struct {
    BenchmarkRunner *runner;
    PulsBenchmarkTest current_test;
    gboolean is_write;
    gdouble progress;
    gdouble current_speed_mbs;
} ProgressUpdate;

static gboolean
notify_progress_idle (gpointer data)
{
    ProgressUpdate *pu = data;
    if (pu->runner->progress_cb && !g_cancellable_is_cancelled (pu->runner->cancellable)) {
        pu->runner->progress_cb (pu->current_test, pu->is_write, pu->progress, pu->current_speed_mbs, pu->runner->user_data);
    }
    g_free (pu);
    return G_SOURCE_REMOVE;
}

static void
queue_progress_update (BenchmarkRunner *runner, PulsBenchmarkTest test, gboolean is_write, gdouble progress, gdouble current_speed_mbs)
{
    ProgressUpdate *pu = g_new0 (ProgressUpdate, 1);
    pu->runner = runner;
    pu->current_test = test;
    pu->is_write = is_write;
    pu->progress = progress;
    pu->current_speed_mbs = current_speed_mbs;
    g_idle_add (notify_progress_idle, pu);
}

typedef struct {
    BenchmarkRunner *runner;
} FinishedUpdate;

static gboolean
notify_finished_idle (gpointer data)
{
    FinishedUpdate *fu = data;
    if (fu->runner->finished_cb) {
        fu->runner->finished_cb (fu->runner->results, fu->runner->cancelled, fu->runner->error_msg, fu->runner->user_data);
    }
    g_free (fu->runner->test_directory);
    g_free (fu->runner->error_msg);
    if (fu->runner->cancellable) {
        g_clear_object (&fu->runner->cancellable);
    }
    g_free (fu->runner);
    g_free (fu);
    return G_SOURCE_REMOVE;
}

typedef struct {
    BenchmarkRunner *runner;
    int fd;
    guint64 start_offset;
    guint64 length;
    gboolean success;
    gchar *error;
} SeqThreadData;

static gpointer
seq_write_thread_func (gpointer data)
{
    SeqThreadData *td = data;
    void *buf = NULL;
    if (posix_memalign (&buf, 4096, 1024 * 1024) != 0) {
        td->error = g_strdup ("Memory alignment failed");
        td->success = FALSE;
        return NULL;
    }
    memset (buf, 0x5A, 1024 * 1024);

    guint64 written = 0;
    while (written < td->length && !g_cancellable_is_cancelled (td->runner->cancellable)) {
        guint64 to_write = MIN (1024 * 1024, td->length - written);
        ssize_t res = pwrite (td->fd, buf, to_write, td->start_offset + written);
        if (res <= 0) {
            td->error = g_strdup_printf ("Sequential pwrite failed: %s", g_strerror (errno));
            td->success = FALSE;
            free (buf);
            return NULL;
        }
        written += res;
    }

    free (buf);
    td->success = TRUE;
    return NULL;
}

static gpointer
seq_read_thread_func (gpointer data)
{
    SeqThreadData *td = data;
    void *buf = NULL;
    if (posix_memalign (&buf, 4096, 1024 * 1024) != 0) {
        td->error = g_strdup ("Memory alignment failed");
        td->success = FALSE;
        return NULL;
    }

    guint64 read_bytes = 0;
    while (read_bytes < td->length && !g_cancellable_is_cancelled (td->runner->cancellable)) {
        guint64 to_read = MIN (1024 * 1024, td->length - read_bytes);
        ssize_t res = pread (td->fd, buf, to_read, td->start_offset + read_bytes);
        if (res <= 0) {
            td->error = g_strdup_printf ("Sequential pread failed: %s", g_strerror (errno));
            td->success = FALSE;
            free (buf);
            return NULL;
        }
        read_bytes += res;
    }

    free (buf);
    td->success = TRUE;
    return NULL;
}

typedef struct {
    BenchmarkRunner *runner;
    int fd;
    guint64 max_size;
    guint num_ops;
    gboolean is_write;
    gboolean success;
    gchar *error;
} RandomThreadData;

static gpointer
random_io_thread_func (gpointer data)
{
    RandomThreadData *td = data;
    void *buf = NULL;
    if (posix_memalign (&buf, 4096, 4096) != 0) {
        td->error = g_strdup ("Memory alignment failed");
        td->success = FALSE;
        return NULL;
    }

    if (td->is_write) {
        memset (buf, 0x3C, 4096);
    }

    guint64 max_blocks = td->max_size / 4096;
    for (guint i = 0; i < td->num_ops && !g_cancellable_is_cancelled (td->runner->cancellable); i++) {
        guint64 block_index = g_random_int_range (0, max_blocks);
        guint64 offset = block_index * 4096;

        ssize_t res;
        if (td->is_write) {
            res = pwrite (td->fd, buf, 4096, offset);
        } else {
            res = pread (td->fd, buf, 4096, offset);
        }

        if (res <= 0) {
            td->error = g_strdup_printf ("Random IO failed: %s", g_strerror (errno));
            td->success = FALSE;
            free (buf);
            return NULL;
        }
    }

    free (buf);
    td->success = TRUE;
    return NULL;
}

static gboolean
run_seq_test (BenchmarkRunner *runner, int fd, guint64 size, guint num_threads, gboolean is_write, gdouble *speed_out)
{
    GThread *threads[8];
    SeqThreadData td[8];
    guint actual_threads = MIN (num_threads, 8);
    guint64 chunk_size = size / actual_threads;

    if (!is_write) {
        posix_fadvise (fd, 0, 0, POSIX_FADV_DONTNEED);
    }

    GTimer *timer = g_timer_new ();

    for (guint i = 0; i < actual_threads; i++) {
        td[i].runner = runner;
        td[i].fd = fd;
        td[i].start_offset = i * chunk_size;
        td[i].length = chunk_size;
        td[i].success = FALSE;
        td[i].error = NULL;

        g_autofree gchar *name = g_strdup_printf ("bench-seq-%u", i);
        threads[i] = g_thread_new (name, is_write ? seq_write_thread_func : seq_read_thread_func, &td[i]);
    }

    for (guint i = 0; i < actual_threads; i++) {
        g_thread_join (threads[i]);
    }

    if (is_write) {
        fdatasync (fd);
    }

    g_timer_stop (timer);
    gdouble elapsed = g_timer_elapsed (timer, NULL);
    g_timer_destroy (timer);

    if (g_cancellable_is_cancelled (runner->cancellable)) {
        for (guint i = 0; i < actual_threads; i++) {
            g_free (td[i].error);
        }
        runner->cancelled = TRUE;
        return FALSE;
    }

    for (guint i = 0; i < actual_threads; i++) {
        if (!td[i].success) {
            runner->error_msg = td[i].error;
            for (guint j = i + 1; j < actual_threads; j++) {
                g_free (td[j].error);
            }
            return FALSE;
        }
        g_free (td[i].error);
    }

    *speed_out = ((gdouble)size / (1000.0 * 1000.0)) / elapsed;
    return TRUE;
}

static gboolean
run_random_test (BenchmarkRunner *runner, int fd, guint64 size, guint num_threads, gboolean is_write, gdouble *speed_out)
{
    GThread *threads[32];
    RandomThreadData td[32];
    guint actual_threads = MIN (num_threads, 32);

    guint total_ops = 1024;
    if (actual_threads > total_ops) {
        actual_threads = total_ops;
    }
    guint ops_per_thread = total_ops / actual_threads;

    if (!is_write) {
        posix_fadvise (fd, 0, 0, POSIX_FADV_DONTNEED);
    }

    GTimer *timer = g_timer_new ();

    for (guint i = 0; i < actual_threads; i++) {
        td[i].runner = runner;
        td[i].fd = fd;
        td[i].max_size = size;
        td[i].num_ops = ops_per_thread;
        td[i].is_write = is_write;
        td[i].success = FALSE;
        td[i].error = NULL;

        g_autofree gchar *name = g_strdup_printf ("bench-rand-%u", i);
        threads[i] = g_thread_new (name, random_io_thread_func, &td[i]);
    }

    for (guint i = 0; i < actual_threads; i++) {
        g_thread_join (threads[i]);
    }

    if (is_write) {
        fdatasync (fd);
    }

    g_timer_stop (timer);
    gdouble elapsed = g_timer_elapsed (timer, NULL);
    g_timer_destroy (timer);

    if (g_cancellable_is_cancelled (runner->cancellable)) {
        for (guint i = 0; i < actual_threads; i++) {
            g_free (td[i].error);
        }
        runner->cancelled = TRUE;
        return FALSE;
    }

    for (guint i = 0; i < actual_threads; i++) {
        if (!td[i].success) {
            runner->error_msg = td[i].error;
            for (guint j = i + 1; j < actual_threads; j++) {
                g_free (td[j].error);
            }
            return FALSE;
        }
        g_free (td[i].error);
    }

    guint64 total_bytes = (guint64)total_ops * 4096;
    *speed_out = ((gdouble)total_bytes / (1000.0 * 1000.0)) / elapsed;
    return TRUE;
}

static gpointer
benchmark_background_thread (gpointer data)
{
    BenchmarkRunner *runner = data;

    GFile *file = g_file_new_for_path (runner->test_directory);
    GFileInfo *info = g_file_query_filesystem_info (file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE "," G_FILE_ATTRIBUTE_FILESYSTEM_TYPE, NULL, NULL);
    gboolean is_fat_or_removable = FALSE;
    if (info) {
        if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE)) {
            guint64 free_space = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
            if (free_space < runner->test_size_bytes * 2) {
                runner->error_msg = g_strdup_printf ("Not enough free space (needs %.1f MB, only %.1f MB available)",
                                                     (double)(runner->test_size_bytes * 2) / (1000.0 * 1000.0),
                                                     (double)free_space / (1000.0 * 1000.0));
            }
        }
        const gchar *fs_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE);
        if (fs_type != NULL) {
            if (g_ascii_strcasecmp (fs_type, "vfat") == 0 ||
                g_ascii_strcasecmp (fs_type, "fat") == 0 ||
                g_ascii_strcasecmp (fs_type, "msdos") == 0 ||
                g_ascii_strcasecmp (fs_type, "exfat") == 0 ||
                g_ascii_strcasecmp (fs_type, "ntfs") == 0 ||
                g_ascii_strcasecmp (fs_type, "fuse") == 0 ||
                g_ascii_strcasecmp (fs_type, "fuseblk") == 0) {
                is_fat_or_removable = TRUE;
            }
        }
        g_object_unref (info);
    }
    g_object_unref (file);

    if (g_str_has_prefix (runner->test_directory, "/media") ||
        g_str_has_prefix (runner->test_directory, "/run/media") ||
        g_str_has_prefix (runner->test_directory, "/mnt")) {
        is_fat_or_removable = TRUE;
    }

    if (runner->error_msg != NULL) {
        FinishedUpdate *fu = g_new0 (FinishedUpdate, 1);
        fu->runner = runner;
        g_idle_add (notify_finished_idle, fu);
        return NULL;
    }

    g_autofree gchar *filename = g_strdup_printf (".puls_benchmark_%d.tmp", (int)getpid ());
    gchar *filepath = g_build_filename (runner->test_directory, filename, NULL);

    g_unlink (filepath);

    int flags = O_RDWR | O_CREAT;
#ifdef O_DIRECT
    if (!is_fat_or_removable) {
        flags |= O_DIRECT;
    }
#endif

    int fd = open (filepath, flags, 0644);
    if (fd < 0 && (flags & O_DIRECT)) {
        flags &= ~O_DIRECT;
        fd = open (filepath, flags, 0644);
    }

    if (fd < 0) {
        runner->error_msg = g_strdup_printf ("Failed to open test file: %s", g_strerror (errno));
        g_free (filepath);

        FinishedUpdate *fu = g_new0 (FinishedUpdate, 1);
        fu->runner = runner;
        g_idle_add (notify_finished_idle, fu);
        return NULL;
    }

    if (ftruncate (fd, runner->test_size_bytes) != 0) {
        runner->error_msg = g_strdup_printf ("Failed to set test file size: %s", g_strerror (errno));
        close (fd);
        g_unlink (filepath);
        g_free (filepath);

        FinishedUpdate *fu = g_new0 (FinishedUpdate, 1);
        fu->runner = runner;
        g_idle_add (notify_finished_idle, fu);
        return NULL;
    }

    for (guint t = 0; t < PULS_BENCHMARK_TEST_COUNT; t++) {
        if (g_cancellable_is_cancelled (runner->cancellable)) {
            runner->cancelled = TRUE;
            break;
        }

        PulsBenchmarkTest current_test = (PulsBenchmarkTest)t;
        gdouble max_write_speed = 0.0;
        gdouble max_read_speed = 0.0;

        guint threads_count = 1;
        gboolean is_seq = TRUE;

        switch (current_test) {
            case PULS_BENCHMARK_TEST_SEQ1M_Q8T1:
                threads_count = is_fat_or_removable ? 1 : 4;
                is_seq = TRUE;
                break;
            case PULS_BENCHMARK_TEST_SEQ1M_Q1T1:
                threads_count = 1;
                is_seq = TRUE;
                break;
            case PULS_BENCHMARK_TEST_RND4K_Q32T1:
                threads_count = is_fat_or_removable ? 1 : 4;
                is_seq = FALSE;
                break;
            case PULS_BENCHMARK_TEST_RND4K_Q1T1:
                threads_count = 1;
                is_seq = FALSE;
                break;
            default:
                break;
        }

        for (guint run = 0; run < runner->num_runs; run++) {
            if (g_cancellable_is_cancelled (runner->cancellable)) {
                runner->cancelled = TRUE;
                break;
            }

            gdouble progress = ((gdouble)run) / runner->num_runs;
            queue_progress_update (runner, current_test, TRUE, progress, max_write_speed);

            gdouble speed = 0.0;
            gboolean ok;
            if (is_seq) {
                ok = run_seq_test (runner, fd, runner->test_size_bytes, threads_count, TRUE, &speed);
            } else {
                ok = run_random_test (runner, fd, runner->test_size_bytes, threads_count, TRUE, &speed);
            }

            if (!ok) {
                break;
            }

            fsync (fd);

            if (speed > max_write_speed) {
                max_write_speed = speed;
            }
        }

        if (runner->cancelled || runner->error_msg) {
            break;
        }

        queue_progress_update (runner, current_test, TRUE, 1.0, max_write_speed);
        runner->results[current_test].write_mbs = max_write_speed;
        runner->results[current_test].write_done = TRUE;

        for (guint run = 0; run < runner->num_runs; run++) {
            if (g_cancellable_is_cancelled (runner->cancellable)) {
                runner->cancelled = TRUE;
                break;
            }

            gdouble progress = ((gdouble)run) / runner->num_runs;
            queue_progress_update (runner, current_test, FALSE, progress, max_read_speed);

            gdouble speed = 0.0;
            gboolean ok;
            if (is_seq) {
                ok = run_seq_test (runner, fd, runner->test_size_bytes, threads_count, FALSE, &speed);
            } else {
                ok = run_random_test (runner, fd, runner->test_size_bytes, threads_count, FALSE, &speed);
            }

            if (!ok) {
                break;
            }

            if (speed > max_read_speed) {
                max_read_speed = speed;
            }
        }

        if (runner->cancelled || runner->error_msg) {
            break;
        }

        queue_progress_update (runner, current_test, FALSE, 1.0, max_read_speed);
        runner->results[current_test].read_mbs = max_read_speed;
        runner->results[current_test].read_done = TRUE;
    }

    fsync (fd);
    close (fd);
    g_unlink (filepath);

    int dir_fd = open (runner->test_directory, O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        fsync (dir_fd);
        close (dir_fd);
    }
    g_free (filepath);

    FinishedUpdate *fu = g_new0 (FinishedUpdate, 1);
    fu->runner = runner;
    g_idle_add (notify_finished_idle, fu);
    return NULL;
}

void
puls_benchmark_run_async (const gchar *test_directory,
                          guint num_runs,
                          guint64 test_size_bytes,
                          PulsBenchmarkProgressCallback progress_cb,
                          PulsBenchmarkFinishedCallback finished_cb,
                          GCancellable *cancellable,
                          gpointer user_data)
{
    g_return_if_fail (test_directory != NULL);

    BenchmarkRunner *runner = g_new0 (BenchmarkRunner, 1);
    runner->test_directory = g_strdup (test_directory);
    runner->num_runs = num_runs;
    runner->test_size_bytes = test_size_bytes;
    runner->progress_cb = progress_cb;
    runner->finished_cb = finished_cb;
    if (cancellable) {
        runner->cancellable = g_object_ref (cancellable);
    } else {
        runner->cancellable = NULL;
    }
    runner->user_data = user_data;
    runner->error_msg = NULL;
    runner->cancelled = FALSE;

    g_thread_new ("bench-runner", benchmark_background_thread, runner);
}


typedef struct {
    gchar                    *device_path;
    guint                     num_samples;
    GCancellable             *cancellable;
    PulsSeekLatencyFinishedCb finished_cb;
    gpointer                  user_data;
} SeekTaskData;

static void
seek_task_data_free (SeekTaskData *d)
{
    g_free (d->device_path);
    g_clear_object (&d->cancellable);
    g_free (d);
}

typedef struct {
    PulsSeekLatencyFinishedCb cb;
    PulsSeekLatencyResult     result;
    gboolean                  cancelled;
    gchar                    *error_msg;
    gpointer                  user_data;
} SeekFinishedIdle;

static gboolean
seek_finished_idle (gpointer data)
{
    SeekFinishedIdle *f = data;
    f->cb (&f->result, f->cancelled, f->error_msg, f->user_data);
    g_free (f->error_msg);
    g_free (f);
    return G_SOURCE_REMOVE;
}

static void
seek_task_thread (GTask *task G_GNUC_UNUSED,
                  gpointer source G_GNUC_UNUSED,
                  gpointer task_data,
                  GCancellable *cancellable)
{
    SeekTaskData *d = task_data;
    PulsSeekLatencyResult result = { 0 };
    gboolean seek_cancelled = FALSE;
    gchar *error_msg = NULL;

    const gchar *dev_name = strrchr (d->device_path, '/');
    dev_name = dev_name ? dev_name + 1 : d->device_path;

    off_t dev_size = 0;
    g_autofree gchar *sysfs_size_path = g_build_filename ("/sys/block", dev_name, "size", NULL);
    g_autofree gchar *size_str = NULL;
    if (g_file_get_contents (sysfs_size_path, &size_str, NULL, NULL)) {
        dev_size = (off_t)(g_ascii_strtoull (size_str, NULL, 10) * 512ULL);
    }

    int fd = open (d->device_path, O_RDONLY | O_CLOEXEC);
    FILE *pipe_fp = NULL;
    if (fd < 0 && errno == EACCES) {
        g_autofree gchar *cmd = g_strdup_printf ("pkexec dd if=%s bs=512 status=none 2>/dev/null", d->device_path);
        pipe_fp = popen (cmd, "r");
        if (pipe_fp) {
            fd = fileno (pipe_fp);
        }
    }

    if (fd < 0) {
        error_msg = g_strdup_printf ("Cannot open %s: %s", d->device_path, g_strerror (errno));
        goto done;
    }

    if (dev_size <= 0) {
        dev_size = lseek (fd, 0, SEEK_END);
    }

    if (dev_size <= 0) {
        error_msg = g_strdup ("Cannot determine device size");
        if (pipe_fp) pclose (pipe_fp); else close (fd);
        goto done;
    }

    /* 512-byte sector buffer */
    void *buf = NULL;
    if (posix_memalign (&buf, 512, 512) != 0) {
        error_msg = g_strdup ("Memory allocation failed");
        if (pipe_fp) pclose (pipe_fp); else close (fd);
        goto done;
    }

    guint samples = 0;
    gdouble sum_ms = 0.0;
    gdouble min_ms = G_MAXDOUBLE;
    gdouble max_ms = 0.0;

    guint64 seed = (guint64)g_get_monotonic_time ();
    guint64 max_block = (guint64)(dev_size / 512);

    for (guint i = 0; i < d->num_samples; i++) {
        if (g_cancellable_is_cancelled (cancellable)) {
            seek_cancelled = TRUE;
            break;
        }

        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        guint64 block = seed % max_block;
        off_t offset = (off_t)(block * 512);

        struct timespec t0, t1;
        clock_gettime (CLOCK_MONOTONIC, &t0);
        if (pipe_fp != NULL) {
            read (fd, buf, 512);
        } else {
            pread (fd, buf, 512, offset);
        }
        clock_gettime (CLOCK_MONOTONIC, &t1);

        gdouble ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
                   + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;

        sum_ms += ms;
        if (ms < min_ms) min_ms = ms;
        if (ms > max_ms) max_ms = ms;
        samples++;
    }

    free (buf);
    if (pipe_fp)
        pclose (pipe_fp);
    else
        close (fd);

    if (samples > 0) {
        result.avg_ms  = sum_ms / (double)samples;
        result.min_ms  = min_ms;
        result.max_ms  = max_ms;
        result.samples = samples;
        result.done    = TRUE;
    }

done:;
    SeekFinishedIdle *f = g_new0 (SeekFinishedIdle, 1);
    f->cb        = d->finished_cb;
    f->result    = result;
    f->cancelled = seek_cancelled;
    f->error_msg = error_msg;
    f->user_data = d->user_data;
    g_idle_add (seek_finished_idle, f);
}

void
puls_benchmark_seek_async (const gchar              *device_path,
                            guint                     num_samples,
                            GCancellable             *cancellable,
                            PulsSeekLatencyFinishedCb finished_cb,
                            gpointer                  user_data)
{
    g_return_if_fail (device_path != NULL);
    g_return_if_fail (finished_cb != NULL);

    SeekTaskData *d = g_new0 (SeekTaskData, 1);
    d->device_path  = g_strdup (device_path);
    d->num_samples  = num_samples > 0 ? num_samples : 200;
    d->cancellable  = cancellable ? g_object_ref (cancellable) : g_cancellable_new ();
    d->finished_cb  = finished_cb;
    d->user_data    = user_data;

    GTask *task = g_task_new (NULL, d->cancellable, NULL, NULL);
    g_task_set_task_data (task, d, (GDestroyNotify)seek_task_data_free);
    g_task_run_in_thread (task, seek_task_thread);
    g_object_unref (task);
}

