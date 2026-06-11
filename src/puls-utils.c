/*
 * puls-utils.c
 *
 * Utility functions for PULS DiskInfo.
 *
 * Copyright (C) 2024 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <unistd.h>

static GSubprocess *helper_proc = NULL;
static GOutputStream *helper_stdin = NULL;
static GDataInputStream *helper_stdout = NULL;
static GMutex helper_mutex;

typedef struct {
    gchar *device_path;
    gchar *action;
} ActionTaskData;

gchar *
puls_format_bytes (guint64 bytes)
{
    const gdouble KB = 1000.0;
    const gdouble MB = 1000.0 * 1000.0;
    const gdouble GB = 1000.0 * 1000.0 * 1000.0;
    const gdouble TB = 1000.0 * 1000.0 * 1000.0 * 1000.0;
    const gdouble PB = 1000.0 * 1000.0 * 1000.0 * 1000.0 * 1000.0;

    if (bytes >= (guint64)PB)
        return g_strdup_printf ("%.1f PB", (gdouble)bytes / PB);
    else if (bytes >= (guint64)TB)
        return g_strdup_printf ("%.1f TB", (gdouble)bytes / TB);
    else if (bytes >= (guint64)GB)
        return g_strdup_printf ("%.1f GB", (gdouble)bytes / GB);
    else if (bytes >= (guint64)MB)
        return g_strdup_printf ("%.1f MB", (gdouble)bytes / MB);
    else if (bytes >= (guint64)KB)
        return g_strdup_printf ("%.1f KB", (gdouble)bytes / KB);
    else
        return g_strdup_printf ("%" G_GUINT64_FORMAT " B", bytes);
}

gchar *
puls_format_bytes_exact (guint64 bytes)
{
    g_autofree gchar *formatted = puls_format_bytes (bytes);
    g_autofree gchar *exact = puls_format_number (bytes);

    return g_strdup_printf ("%s (%s bytes)", formatted, exact);
}

gchar *
puls_format_hours (guint64 hours)
{
    if (hours == 0)
        return g_strdup ("0 h");

    guint64 years = hours / (365 * 24);
    guint64 days  = (hours % (365 * 24)) / 24;
    guint64 hrs   = hours % 24;

    GString *result = g_string_new (NULL);

    if (years > 0)
        g_string_append_printf (result, "%" G_GUINT64_FORMAT "y ", years);
    if (days > 0 || years > 0)
        g_string_append_printf (result, "%" G_GUINT64_FORMAT "d ", days);
    g_string_append_printf (result, "%" G_GUINT64_FORMAT "h", hrs);

    return g_string_free (result, FALSE);
}

gchar *
puls_format_temperature (gint celsius)
{
    if (celsius < 0)
        return g_strdup ("N/A");
    return g_strdup_printf ("%d °C", celsius);
}

gchar *
puls_format_number (guint64 number)
{
    g_autofree gchar *raw = g_strdup_printf ("%" G_GUINT64_FORMAT, number);
    gint len = strlen (raw);

    if (len <= 3)
        return g_strdup (raw);

    gint commas = (len - 1) / 3;
    gint result_len = len + commas + 1;
    gchar *result = g_malloc (result_len);

    gint src = len - 1;
    gint dst = result_len - 2;
    gint count = 0;

    result[result_len - 1] = '\0';

    while (src >= 0) {
        result[dst--] = raw[src--];
        count++;
        if (count % 3 == 0 && src >= 0)
            result[dst--] = ',';
    }

    return result;
}

static gboolean
ensure_helper_running (GError **error)
{
    g_mutex_lock (&helper_mutex);

    if (helper_proc != NULL) {
        g_mutex_unlock (&helper_mutex);
        return TRUE;
    }

    g_autofree gchar *helper_path = puls_get_helper_path ();
    if (helper_path == NULL) {
        g_mutex_unlock (&helper_mutex);
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Helper executable not found");
        return FALSE;
    }

    gchar **argv;
    if (geteuid () == 0) {
        argv = g_new0 (gchar *, 2);
        argv[0] = g_strdup (helper_path);
        argv[1] = NULL;
    } else {
        argv = g_new0 (gchar *, 3);
        argv[0] = g_strdup ("pkexec");
        argv[1] = g_strdup (helper_path);
        argv[2] = NULL;
    }

    GSubprocessLauncher *launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE);
    helper_proc = g_subprocess_launcher_spawnv (launcher, (const gchar * const *)argv, error);
    g_object_unref (launcher);
    g_strfreev (argv);

    if (helper_proc == NULL) {
        g_mutex_unlock (&helper_mutex);
        return FALSE;
    }

    helper_stdin = g_subprocess_get_stdin_pipe (helper_proc);
    GInputStream *raw_stdout = g_subprocess_get_stdout_pipe (helper_proc);
    helper_stdout = g_data_input_stream_new (raw_stdout);

    g_mutex_unlock (&helper_mutex);
    return TRUE;
}

static gchar *
communicate_with_helper (const gchar *command, GError **error)
{
    if (!ensure_helper_running (error)) {
        return NULL;
    }

    g_mutex_lock (&helper_mutex);

    gsize bytes_written = 0;
    gboolean ok = g_output_stream_write_all (helper_stdin, command, strlen (command), &bytes_written, NULL, error);
    if (!ok) {
        g_clear_object (&helper_proc);
        helper_stdin = NULL;
        g_clear_object (&helper_stdout);
        g_mutex_unlock (&helper_mutex);
        return NULL;
    }

    gchar *len_str = g_data_input_stream_read_line (helper_stdout, NULL, NULL, error);
    if (len_str == NULL) {
        g_clear_object (&helper_proc);
        helper_stdin = NULL;
        g_clear_object (&helper_stdout);
        g_mutex_unlock (&helper_mutex);
        if (error && *error == NULL) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Helper EOF or disconnected");
        }
        return NULL;
    }

    gsize len = g_ascii_strtoull (len_str, NULL, 10);
    g_free (len_str);

    if (len == 0) {
        g_mutex_unlock (&helper_mutex);
        return g_strdup ("");
    }

    gchar *buf = g_malloc (len + 1);
    gsize bytes_read = 0;
    ok = g_input_stream_read_all (G_INPUT_STREAM (helper_stdout), buf, len, &bytes_read, NULL, error);
    if (!ok || bytes_read != len) {
        g_free (buf);
        g_clear_object (&helper_proc);
        helper_stdin = NULL;
        g_clear_object (&helper_stdout);
        g_mutex_unlock (&helper_mutex);
        if (error && *error == NULL) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Short read from helper");
        }
        return NULL;
    }

    buf[len] = '\0';
    g_mutex_unlock (&helper_mutex);
    return buf;
}

static void
smartctl_thread_func (GTask        *task,
                      gpointer      source_object G_GNUC_UNUSED,
                      gpointer      task_data,
                      GCancellable *cancellable G_GNUC_UNUSED)
{
    const gchar *device_path = (const gchar *)task_data;
    GError *error = NULL;
    gchar *stdout_buf = puls_run_smartctl_sync (device_path, &error);

    if (error) {
        g_task_return_error (task, error);
        return;
    }

    if (stdout_buf == NULL || strlen (stdout_buf) == 0) {
        g_free (stdout_buf);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "smartctl returned no output for %s",
                                device_path);
        return;
    }

    g_task_return_pointer (task, stdout_buf, g_free);
}

void
puls_run_smartctl_async (const gchar        *device_path,
                         GCancellable       *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer            user_data)
{
    g_return_if_fail (device_path != NULL);

    GTask *task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_task_data (task, g_strdup (device_path), g_free);
    g_task_run_in_thread (task, smartctl_thread_func);
    g_object_unref (task);
}

gchar *
puls_run_smartctl_finish (GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer (G_TASK (result), error);
}

gchar *
puls_run_smartctl_sync (const gchar *device_path, GError **error)
{
    g_autofree gchar *cmd = g_strdup_printf ("READ %s\n", device_path);
    gchar *res = communicate_with_helper (cmd, error);
    if (res != NULL)
        return res;

    if (error)
        g_clear_error (error);

    g_autofree gchar *cmdline = g_strdup_printf ("smartctl -a -j %s", device_path);
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;
    g_spawn_command_line_sync (cmdline, &stdout_buf, &stderr_buf, &exit_status, error);
    g_free (stderr_buf);
    return stdout_buf;
}

gboolean
puls_detect_smartmontools (gchar **version_out)
{
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    gboolean ok = g_spawn_command_line_sync ("smartctl --version",
                                             &stdout_buf,
                                             &stderr_buf,
                                             &exit_status,
                                             &error);
    g_free (stderr_buf);
    g_clear_error (&error);

    if (!ok || stdout_buf == NULL) {
        g_free (stdout_buf);
        if (version_out)
            *version_out = NULL;
        return FALSE;
    }

    if (version_out) {
        gchar *line_end = strchr (stdout_buf, '\n');
        if (line_end)
            *version_out = g_strndup (stdout_buf, line_end - stdout_buf);
        else
            *version_out = g_strdup (stdout_buf);
    }

    g_free (stdout_buf);
    return TRUE;
}

gboolean
puls_device_path_is_valid (const gchar *path)
{
    if (path == NULL)
        return FALSE;

    if (g_str_has_prefix (path, "/dev/sd") ||
        g_str_has_prefix (path, "/dev/nvme") ||
        g_str_has_prefix (path, "/dev/hd") ||
        g_str_has_prefix (path, "/dev/vd") ||
        g_str_has_prefix (path, "/dev/mmcblk"))
        return TRUE;

    return FALSE;
}

gchar *
puls_get_helper_path (void)
{
    const gchar *paths[] = {
        "/usr/libexec/puls-diskinfo-helper",
        "/usr/local/libexec/puls-diskinfo-helper",
        NULL
    };

    for (gint i = 0; paths[i] != NULL; i++) {
        if (g_file_test (paths[i], G_FILE_TEST_IS_EXECUTABLE))
            return g_strdup (paths[i]);
    }

    g_autofree gchar *exe_dir = NULL;
    g_autofree gchar *self_link = g_file_read_link ("/proc/self/exe", NULL);
    if (self_link) {
        exe_dir = g_path_get_dirname (self_link);
        g_autofree gchar *dev_path = g_build_filename (exe_dir, "..", "helper",
                                                       "puls-diskinfo-helper", NULL);
        if (g_file_test (dev_path, G_FILE_TEST_IS_EXECUTABLE))
            return g_strdup (dev_path);
    }

    return NULL;
}

gboolean
puls_is_rotational (const gchar *device_name)
{
    g_autofree gchar *path = g_strdup_printf ("/sys/block/%s/queue/rotational",
                                               device_name);
    g_autofree gchar *contents = NULL;

    if (!g_file_get_contents (path, &contents, NULL, NULL))
        return TRUE;

    return g_ascii_strtoull (g_strstrip (contents), NULL, 10) != 0;
}

gchar *
puls_get_transport (const gchar *device_name)
{
    if (g_str_has_prefix (device_name, "nvme"))
        return g_strdup ("nvme");

    g_autofree gchar *device_link = g_strdup_printf ("/sys/block/%s/device",
                                                      device_name);
    g_autofree gchar *resolved = g_file_read_link (device_link, NULL);

    if (resolved && strstr (resolved, "usb"))
        return g_strdup ("usb");

    return g_strdup ("sata");
}

PulsDriveType
puls_detect_drive_type (const gchar *device_path)
{
    g_return_val_if_fail (device_path != NULL, PULS_DRIVE_TYPE_UNKNOWN);

    const gchar *device_name = strrchr (device_path, '/');
    if (device_name)
        device_name++;
    else
        device_name = device_path;

    g_autofree gchar *transport = puls_get_transport (device_name);

    if (g_strcmp0 (transport, "nvme") == 0)
        return PULS_DRIVE_TYPE_NVME_SSD;

    if (g_strcmp0 (transport, "usb") == 0)
        return PULS_DRIVE_TYPE_USB;

    gboolean rotational = puls_is_rotational (device_name);

    if (rotational)
        return PULS_DRIVE_TYPE_HDD;
    else
        return PULS_DRIVE_TYPE_SATA_SSD;
}

void
puls_partition_info_free (PulsPartitionInfo *info)
{
    if (info == NULL)
        return;
    g_free (info->device_path);
    g_free (info->mount_point);
    g_free (info->fs_type);
    g_free (info);
}

GList *
puls_get_disk_partitions (const gchar *device_path)
{
    if (device_path == NULL)
        return NULL;

    GList *list = NULL;
    FILE *f = setmntent ("/proc/mounts", "r");
    if (f == NULL)
        return NULL;

    struct mntent *mnt;
    while ((mnt = getmntent (f)) != NULL) {
        gboolean is_part = FALSE;
        if (g_strcmp0 (mnt->mnt_fsname, device_path) == 0) {
            is_part = TRUE;
        } else if (g_str_has_prefix (mnt->mnt_fsname, device_path)) {
            const gchar *suffix = mnt->mnt_fsname + strlen (device_path);
            if (g_ascii_isdigit (suffix[0])) {
                is_part = TRUE;
            } else if (suffix[0] == 'p' && g_ascii_isdigit (suffix[1])) {
                is_part = TRUE;
            }
        }

        if (is_part) {
            PulsPartitionInfo *info = g_new0 (PulsPartitionInfo, 1);
            info->device_path = g_strdup (mnt->mnt_fsname);
            info->mount_point = g_strdup (mnt->mnt_dir);
            info->fs_type = g_strdup (mnt->mnt_type);

            struct statvfs vfs;
            if (statvfs (mnt->mnt_dir, &vfs) == 0) {
                info->total_bytes = (guint64)vfs.f_blocks * vfs.f_frsize;
                info->available_bytes = (guint64)vfs.f_bavail * vfs.f_frsize;
            } else {
                info->total_bytes = 0;
                info->available_bytes = 0;
            }

            list = g_list_append (list, info);
        }
    }

    endmntent (f);
    return list;
}

static void
action_task_data_free (ActionTaskData *atd)
{
    g_free (atd->device_path);
    g_free (atd->action);
    g_free (atd);
}

static void
smartctl_action_thread_func (GTask        *task,
                             gpointer      source_object G_GNUC_UNUSED,
                             gpointer      task_data,
                             GCancellable *cancellable G_GNUC_UNUSED)
{
    ActionTaskData *atd = (ActionTaskData *)task_data;
    GError *error = NULL;
    gchar *stdout_buf = puls_run_smartctl_action_sync (atd->device_path, atd->action, &error);

    if (error) {
        g_task_return_error (task, error);
        return;
    }

    g_task_return_pointer (task, stdout_buf, g_free);
}

void
puls_run_smartctl_action_async (const gchar        *device_path,
                                const gchar        *action,
                                GCancellable       *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    g_return_if_fail (device_path != NULL);
    g_return_if_fail (action != NULL);

    ActionTaskData *atd = g_new0 (ActionTaskData, 1);
    atd->device_path = g_strdup (device_path);
    atd->action = g_strdup (action);

    GTask *task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_task_data (task, atd, (GDestroyNotify)action_task_data_free);
    g_task_run_in_thread (task, smartctl_action_thread_func);
    g_object_unref (task);
}

gchar *
puls_run_smartctl_action_finish (GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer (G_TASK (result), error);
}

gchar *
puls_run_smartctl_action_sync (const gchar *device_path, const gchar *action, GError **error)
{
    g_autofree gchar *cmd = g_strdup_printf ("TEST %s %s\n", device_path, action);
    gchar *res = communicate_with_helper (cmd, error);
    if (res != NULL)
        return res;

    if (error)
        g_clear_error (error);

    g_autofree gchar *cmdline = NULL;
    if (g_strcmp0 (action, "short") == 0) {
        cmdline = g_strdup_printf ("smartctl -t short %s", device_path);
    } else if (g_strcmp0 (action, "long") == 0) {
        cmdline = g_strdup_printf ("smartctl -t long %s", device_path);
    } else if (g_strcmp0 (action, "abort") == 0) {
        cmdline = g_strdup_printf ("smartctl -X %s", device_path);
    } else {
        cmdline = g_strdup_printf ("smartctl -a -j %s", device_path);
    }

    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;
    g_spawn_command_line_sync (cmdline, &stdout_buf, &stderr_buf, &exit_status, error);
    g_free (stderr_buf);

    return stdout_buf ? stdout_buf : g_strdup ("");
}
