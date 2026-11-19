/*
 * puls-utils.c
 *
 * Utility functions for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
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
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static GSubprocess *helper_proc = NULL;
static GOutputStream *helper_stdin = NULL;
static GDataInputStream *helper_stdout = NULL;
static GMutex helper_mutex;

typedef struct {
    gchar *device_path;
    gchar *action;
} ActionTaskData;

gchar *
puls_format_bytes_decimal (guint64 bytes)
{
    const gdouble KB = 1000.0;
    const gdouble MB = 1000.0 * 1000.0;
    const gdouble GB = 1000.0 * 1000.0 * 1000.0;
    const gdouble TB = 1000.0 * 1000.0 * 1000.0 * 1000.0;
    const gdouble PB = 1000.0 * 1000.0 * 1000.0 * 1000.0 * 1000.0;
    const gdouble EB = 1000.0 * 1000.0 * 1000.0 * 1000.0 * 1000.0 * 1000.0;

    if (bytes >= (guint64)EB)
        return g_strdup_printf ("%.1f EB", (gdouble)bytes / EB);
    else if (bytes >= (guint64)PB)
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
puls_format_bytes_binary (guint64 bytes)
{
    const gdouble KiB = 1024.0;
    const gdouble MiB = 1024.0 * 1024.0;
    const gdouble GiB = 1024.0 * 1024.0 * 1024.0;
    const gdouble TiB = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    const gdouble PiB = 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;
    const gdouble EiB = 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;

    if (bytes >= (guint64)EiB)
        return g_strdup_printf ("%.1f EiB", (gdouble)bytes / EiB);
    else if (bytes >= (guint64)PiB)
        return g_strdup_printf ("%.1f PiB", (gdouble)bytes / PiB);
    else if (bytes >= (guint64)TiB)
        return g_strdup_printf ("%.1f TiB", (gdouble)bytes / TiB);
    else if (bytes >= (guint64)GiB)
        return g_strdup_printf ("%.1f GiB", (gdouble)bytes / GiB);
    else if (bytes >= (guint64)MiB)
        return g_strdup_printf ("%.1f MiB", (gdouble)bytes / MiB);
    else if (bytes >= (guint64)KiB)
        return g_strdup_printf ("%.1f KiB", (gdouble)bytes / KiB);
    else
        return g_strdup_printf ("%" G_GUINT64_FORMAT " B", bytes);
}

gchar *
puls_format_bytes_dual (guint64 bytes)
{
    if (bytes >= 1000) {
        g_autofree gchar *dec = puls_format_bytes_decimal (bytes);
        g_autofree gchar *bin = puls_format_bytes_binary (bytes);
        return g_strdup_printf ("%s (%s)", dec, bin);
    } else {
        return g_strdup_printf ("%" G_GUINT64_FORMAT " B", bytes);
    }
}

gchar *
puls_format_capacity_unit (guint64 bytes, PulsCapacityUnit unit)
{
    switch (unit) {
    case PULS_CAPACITY_UNIT_DECIMAL:
        return puls_format_bytes_decimal (bytes);
    case PULS_CAPACITY_UNIT_BINARY:
        return puls_format_bytes_binary (bytes);
    case PULS_CAPACITY_UNIT_DUAL:
    default:
        return puls_format_bytes_dual (bytes);
    }
}

gchar *
puls_format_capacity_unit_exact (guint64 bytes, PulsCapacityUnit unit)
{
    g_autofree gchar *formatted = puls_format_capacity_unit (bytes, unit);
    g_autofree gchar *exact = puls_format_number (bytes);

    if (unit == PULS_CAPACITY_UNIT_DUAL)
        return g_strdup_printf ("%s [%s bytes]", formatted, exact);
    else
        return g_strdup_printf ("%s (%s bytes)", formatted, exact);
}

gchar *
puls_format_bytes (guint64 bytes)
{
    return puls_format_bytes_decimal (bytes);
}

gchar *
puls_format_bytes_exact (guint64 bytes)
{
    return puls_format_capacity_unit_exact (bytes, PULS_CAPACITY_UNIT_DUAL);
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
        g_str_has_prefix (path, "/dev/mmcblk") ||
        (g_str_has_prefix (path, "/dev/sg") && puls_scsi_is_generic_name (path)))
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

gchar *
puls_sanitize_device_name (const gchar *device_path)
{
    if (device_path == NULL || device_path[0] == '\0')
        return NULL;

    if (strstr (device_path, ".."))
        return NULL;

    const gchar *name = strrchr (device_path, '/');
    if (name)
        name++;
    else
        name = device_path;

    if (name[0] == '\0' || strcmp (name, ".") == 0)
        return NULL;

    for (const gchar *p = name; *p != '\0'; p++) {
        if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-' && *p != ':')
            return NULL;
    }

    return g_strdup (name);
}

gchar *
puls_sysfs_get_block_path (const gchar *device_name, const gchar *subpath)
{
    g_autofree gchar *clean_name = puls_sanitize_device_name (device_name);
    if (clean_name == NULL)
        return NULL;

    if (subpath == NULL || subpath[0] == '\0')
        return g_strdup_printf ("/sys/block/%s", clean_name);

    if (strstr (subpath, ".."))
        return NULL;

    while (subpath[0] == '/')
        subpath++;

    return g_strdup_printf ("/sys/block/%s/%s", clean_name, subpath);
}

gchar *
puls_sysfs_read_string (const gchar *path, gsize max_len)
{
    if (path == NULL || path[0] != '/')
        return NULL;

    if (max_len == 0 || max_len > 65536)
        max_len = PULS_SYSFS_DEFAULT_MAX_LEN;

    int fd = open (path, O_RDONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    gchar *buf = g_malloc (max_len + 1);
    gsize total_read = 0;

    while (total_read < max_len) {
        ssize_t n = read (fd, buf + total_read, max_len - total_read);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close (fd);
            g_free (buf);
            return NULL;
        }
        if (n == 0)
            break;
        total_read += (gsize)n;
    }

    close (fd);
    buf[total_read] = '\0';

    g_strstrip (buf);
    return buf;
}

guint64
puls_sysfs_read_uint64 (const gchar *path, guint64 default_val, gboolean *ok)
{
    g_autofree gchar *str = puls_sysfs_read_string (path, 128);
    if (str == NULL || str[0] == '\0') {
        if (ok)
            *ok = FALSE;
        return default_val;
    }

    gchar *endptr = NULL;
    errno = 0;
    guint64 val = g_ascii_strtoull (str, &endptr, 0);
    if (endptr == str || errno == ERANGE) {
        if (ok)
            *ok = FALSE;
        return default_val;
    }

    if (ok)
        *ok = TRUE;
    return val;
}

gint64
puls_sysfs_read_int64 (const gchar *path, gint64 default_val, gboolean *ok)
{
    g_autofree gchar *str = puls_sysfs_read_string (path, 128);
    if (str == NULL || str[0] == '\0') {
        if (ok)
            *ok = FALSE;
        return default_val;
    }

    gchar *endptr = NULL;
    errno = 0;
    gint64 val = g_ascii_strtoll (str, &endptr, 0);
    if (endptr == str || errno == ERANGE) {
        if (ok)
            *ok = FALSE;
        return default_val;
    }

    if (ok)
        *ok = TRUE;
    return val;
}

gchar *
puls_sysfs_read_device_file (const gchar *device_name, const gchar *subpath, gsize max_len)
{
    g_autofree gchar *path = puls_sysfs_get_block_path (device_name, subpath);
    if (path == NULL)
        return NULL;

    return puls_sysfs_read_string (path, max_len);
}

guint64
puls_sysfs_read_device_uint64 (const gchar *device_name, const gchar *subpath, guint64 default_val)
{
    g_autofree gchar *path = puls_sysfs_get_block_path (device_name, subpath);
    if (path == NULL)
        return default_val;

    return puls_sysfs_read_uint64 (path, default_val, NULL);
}

gint64
puls_sysfs_read_device_int64 (const gchar *device_name, const gchar *subpath, gint64 default_val)
{
    g_autofree gchar *path = puls_sysfs_get_block_path (device_name, subpath);
    if (path == NULL)
        return default_val;

    return puls_sysfs_read_int64 (path, default_val, NULL);
}

gchar *
puls_sysfs_resolve_link (const gchar *path)
{
    if (path == NULL || path[0] != '/')
        return NULL;

    gchar real_buf[PATH_MAX];
    if (realpath (path, real_buf) != NULL)
        return g_strdup (real_buf);

    return g_file_read_link (path, NULL);
}

static gboolean
check_dir_has_temp_input (const gchar *dir_path)
{
    if (dir_path == NULL)
        return FALSE;

    g_autofree gchar *t1 = g_build_filename (dir_path, "temp1_input", NULL);
    if (g_file_test (t1, G_FILE_TEST_EXISTS))
        return TRUE;

    g_autofree gchar *t0 = g_build_filename (dir_path, "temp0_input", NULL);
    if (g_file_test (t0, G_FILE_TEST_EXISTS))
        return TRUE;

    return FALSE;
}

gchar *
puls_sysfs_find_hwmon_dir (const gchar *device_name)
{
    g_autofree gchar *clean_name = puls_sanitize_device_name (device_name);
    if (clean_name == NULL)
        return NULL;

    g_autofree gchar *dev_link = g_strdup_printf ("/sys/block/%s/device", clean_name);
    if (!g_file_test (dev_link, G_FILE_TEST_EXISTS)) {
        g_autofree gchar *ctrl_name = NULL;
        if (puls_nvme_parse_device_name (clean_name, &ctrl_name, NULL)) {
            g_free (dev_link);
            dev_link = g_strdup_printf ("/sys/class/nvme/%s", ctrl_name);
        }
    }
    g_autofree gchar *real_dev = puls_sysfs_resolve_link (dev_link);

    const gchar *candidates[4];
    g_autofree gchar *parent_dev = NULL;
    guint cand_count = 0;

    candidates[cand_count++] = dev_link;
    if (real_dev) {
        candidates[cand_count++] = real_dev;
        parent_dev = g_path_get_dirname (real_dev);
        if (parent_dev && strcmp (parent_dev, "/") != 0 && strcmp (parent_dev, "/sys") != 0) {
            candidates[cand_count++] = parent_dev;
        }
    }

    for (guint c = 0; c < cand_count; c++) {
        const gchar *base_dir = candidates[c];
        if (!g_file_test (base_dir, G_FILE_TEST_IS_DIR))
            continue;

        GDir *dir = g_dir_open (base_dir, 0, NULL);
        if (!dir)
            continue;

        const gchar *entry;
        while ((entry = g_dir_read_name (dir)) != NULL) {
            if (g_str_has_prefix (entry, "hwmon")) {
                g_autofree gchar *hwmon_path = g_build_filename (base_dir, entry, NULL);
                if (check_dir_has_temp_input (hwmon_path)) {
                    g_dir_close (dir);
                    return g_steal_pointer (&hwmon_path);
                }

                if (g_file_test (hwmon_path, G_FILE_TEST_IS_DIR)) {
                    GDir *sub = g_dir_open (hwmon_path, 0, NULL);
                    if (sub) {
                        const gchar *sub_entry;
                        while ((sub_entry = g_dir_read_name (sub)) != NULL) {
                            if (g_str_has_prefix (sub_entry, "hwmon")) {
                                g_autofree gchar *sub_hwmon = g_build_filename (hwmon_path, sub_entry, NULL);
                                if (check_dir_has_temp_input (sub_hwmon)) {
                                    g_dir_close (sub);
                                    g_dir_close (dir);
                                    return g_steal_pointer (&sub_hwmon);
                                }
                            }
                        }
                        g_dir_close (sub);
                    }
                }
            }
        }
        g_dir_close (dir);
    }

    if (real_dev) {
        GDir *class_dir = g_dir_open ("/sys/class/hwmon", 0, NULL);
        if (class_dir) {
            const gchar *hw_name;
            while ((hw_name = g_dir_read_name (class_dir)) != NULL) {
                g_autofree gchar *hw_path = g_build_filename ("/sys/class/hwmon", hw_name, NULL);
                g_autofree gchar *hw_dev_link = g_build_filename (hw_path, "device", NULL);
                g_autofree gchar *hw_real_dev = puls_sysfs_resolve_link (hw_dev_link);

                if (hw_real_dev) {
                    if (g_strcmp0 (hw_real_dev, real_dev) == 0 ||
                        (parent_dev && g_strcmp0 (hw_real_dev, parent_dev) == 0) ||
                        g_str_has_prefix (real_dev, hw_real_dev) ||
                        g_str_has_prefix (hw_real_dev, real_dev)) {
                        if (check_dir_has_temp_input (hw_path)) {
                            g_dir_close (class_dir);
                            return g_steal_pointer (&hw_path);
                        }
                    }
                }
            }
            g_dir_close (class_dir);
        }
    }

    return NULL;
}

GList *
puls_sysfs_read_hwmon_sensors_from_dir (const gchar *hwmon_dir)
{
    if (hwmon_dir == NULL)
        return NULL;

    GList *sensors = NULL;

    for (guint n = 0; n <= 16; n++) {
        g_autofree gchar *input_file = g_strdup_printf ("temp%u_input", n);
        g_autofree gchar *input_path = g_build_filename (hwmon_dir, input_file, NULL);

        gboolean ok = FALSE;
        gint64 milli = puls_sysfs_read_int64 (input_path, -1, &ok);
        if (!ok || milli < -50000 || milli > 200000)
            continue;

        gint temp_c = (gint)((milli + (milli >= 0 ? 500 : -500)) / 1000);

        g_autofree gchar *label_file = g_strdup_printf ("temp%u_label", n);
        g_autofree gchar *label_path = g_build_filename (hwmon_dir, label_file, NULL);
        g_autofree gchar *label_str = puls_sysfs_read_string (label_path, 64);

        if (label_str == NULL || label_str[0] == '\0') {
            g_free (label_str);
            if (n == 0 || n == 1)
                label_str = g_strdup ("Composite");
            else
                label_str = g_strdup_printf ("Sensor %u", n);
        }

        gint warn_c = -1;
        g_autofree gchar *max_file = g_strdup_printf ("temp%u_max", n);
        g_autofree gchar *max_path = g_build_filename (hwmon_dir, max_file, NULL);
        gint64 warn_milli = puls_sysfs_read_int64 (max_path, -1, &ok);
        if (ok && warn_milli > 0)
            warn_c = (gint)((warn_milli + 500) / 1000);

        gint crit_c = -1;
        g_autofree gchar *crit_file = g_strdup_printf ("temp%u_crit", n);
        g_autofree gchar *crit_path = g_build_filename (hwmon_dir, crit_file, NULL);
        gint64 crit_milli = puls_sysfs_read_int64 (crit_path, -1, &ok);
        if (ok && crit_milli > 0)
            crit_c = (gint)((crit_milli + 500) / 1000);

        PulsThermalSensor *sensor = puls_thermal_sensor_new (label_str, temp_c, warn_c, crit_c);
        sensors = g_list_append (sensors, sensor);
    }

    return sensors;
}

gint
puls_sysfs_read_hwmon_temp_from_dir (const gchar *hwmon_dir)
{
    GList *sensors = puls_sysfs_read_hwmon_sensors_from_dir (hwmon_dir);
    if (sensors == NULL)
        return -1;

    PulsThermalSensor *first = sensors->data;
    gint temp = first->temperature;
    g_list_free_full (sensors, (GDestroyNotify)puls_thermal_sensor_free);
    return temp;
}

gint
puls_sysfs_read_hwmon_temp (const gchar *device_name)
{
    g_autofree gchar *dir = puls_sysfs_find_hwmon_dir (device_name);
    if (dir == NULL)
        return -1;

    return puls_sysfs_read_hwmon_temp_from_dir (dir);
}

GList *
puls_sysfs_read_hwmon_sensors (const gchar *device_name)
{
    g_autofree gchar *dir = puls_sysfs_find_hwmon_dir (device_name);
    if (dir == NULL)
        return NULL;

    return puls_sysfs_read_hwmon_sensors_from_dir (dir);
}

gboolean
puls_is_rotational (const gchar *device_name)
{
    return puls_sysfs_read_queue_uint64 (device_name, "rotational", 1) != 0;
}

guint64
puls_sysfs_read_queue_uint64 (const gchar *device_name,
                              const gchar *attribute,
                              guint64      default_val)
{
    if (attribute == NULL || attribute[0] == '\0')
        return default_val;

    g_autofree gchar *subpath = g_strdup_printf ("queue/%s", attribute);
    return puls_sysfs_read_device_uint64 (device_name, subpath, default_val);
}

gchar *
puls_sysfs_read_queue_string (const gchar *device_name,
                              const gchar *attribute,
                              gsize        max_len)
{
    if (attribute == NULL || attribute[0] == '\0')
        return NULL;

    g_autofree gchar *subpath = g_strdup_printf ("queue/%s", attribute);
    return puls_sysfs_read_device_file (device_name, subpath, max_len);
}

gboolean
puls_sysfs_read_queue_attributes_from_dir (const gchar         *queue_dir,
                                           PulsQueueAttributes *out_attrs)
{
    if (queue_dir == NULL || out_attrs == NULL)
        return FALSE;

    if (!g_file_test (queue_dir, G_FILE_TEST_IS_DIR))
        return FALSE;

    memset (out_attrs, 0, sizeof (*out_attrs));

    g_autofree gchar *p_disc_gran = g_build_filename (queue_dir, "discard_granularity", NULL);
    g_autofree gchar *p_disc_max  = g_build_filename (queue_dir, "discard_max_bytes", NULL);
    g_autofree gchar *p_disc_hw   = g_build_filename (queue_dir, "discard_max_hw_bytes", NULL);
    g_autofree gchar *p_rot       = g_build_filename (queue_dir, "rotational", NULL);
    g_autofree gchar *p_lbs       = g_build_filename (queue_dir, "logical_block_size", NULL);
    g_autofree gchar *p_pbs       = g_build_filename (queue_dir, "physical_block_size", NULL);
    g_autofree gchar *p_opt_io    = g_build_filename (queue_dir, "optimal_io_size", NULL);
    g_autofree gchar *p_min_io    = g_build_filename (queue_dir, "minimum_io_size", NULL);
    g_autofree gchar *p_wcache    = g_build_filename (queue_dir, "write_cache", NULL);
    g_autofree gchar *p_max_seg   = g_build_filename (queue_dir, "max_segments", NULL);
    g_autofree gchar *p_max_sec   = g_build_filename (queue_dir, "max_sectors_kb", NULL);

    out_attrs->discard_granularity  = puls_sysfs_read_uint64 (p_disc_gran, 0, NULL);
    out_attrs->discard_max_bytes    = puls_sysfs_read_uint64 (p_disc_max, 0, NULL);
    out_attrs->discard_max_hw_bytes = puls_sysfs_read_uint64 (p_disc_hw, 0, NULL);
    out_attrs->supports_discard     = (out_attrs->discard_max_bytes > 0 &&
                                       out_attrs->discard_granularity > 0);

    out_attrs->rotational           = (puls_sysfs_read_uint64 (p_rot, 0, NULL) != 0);
    out_attrs->logical_block_size   = (guint32)puls_sysfs_read_uint64 (p_lbs, 512, NULL);
    out_attrs->physical_block_size  = (guint32)puls_sysfs_read_uint64 (p_pbs, 512, NULL);
    out_attrs->optimal_io_size      = puls_sysfs_read_uint64 (p_opt_io, 0, NULL);
    out_attrs->minimum_io_size      = puls_sysfs_read_uint64 (p_min_io, 0, NULL);
    out_attrs->max_segments         = (guint32)puls_sysfs_read_uint64 (p_max_seg, 0, NULL);
    out_attrs->max_sectors_kb       = puls_sysfs_read_uint64 (p_max_sec, 0, NULL);

    g_autofree gchar *wc_str = puls_sysfs_read_string (p_wcache, sizeof (out_attrs->write_cache) - 1);
    if (wc_str != NULL && wc_str[0] != '\0') {
        g_strlcpy (out_attrs->write_cache, wc_str, sizeof (out_attrs->write_cache));
        out_attrs->write_cache_enabled = (g_strcmp0 (wc_str, "none") != 0);
    } else {
        out_attrs->write_cache[0] = '\0';
        out_attrs->write_cache_enabled = FALSE;
    }

    return TRUE;
}

gboolean
puls_sysfs_read_queue_attributes (const gchar         *device_name,
                                  PulsQueueAttributes *out_attrs)
{
    if (device_name == NULL || out_attrs == NULL)
        return FALSE;

    g_autofree gchar *clean_name = puls_sanitize_device_name (device_name);
    if (clean_name == NULL)
        return FALSE;

    g_autofree gchar *queue_dir = g_strdup_printf ("/sys/block/%s/queue", clean_name);
    return puls_sysfs_read_queue_attributes_from_dir (queue_dir, out_attrs);
}

gboolean
puls_sysfs_detect_trim_support (const gchar *device_name)
{
    guint64 max_bytes = puls_sysfs_read_queue_uint64 (device_name, "discard_max_bytes", 0);
    guint64 gran = puls_sysfs_read_queue_uint64 (device_name, "discard_granularity", 0);
    return (max_bytes > 0 && gran > 0);
}

void
puls_pcie_link_info_clear (PulsPcieLinkInfo *info)
{
    if (info == NULL)
        return;
    g_free (info->device_dir);
    g_free (info->description);
    memset (info, 0, sizeof (*info));
}

gchar *
puls_format_pcie_link (PulsPcieGeneration current_gen,
                       guint8             current_width,
                       PulsPcieGeneration max_gen,
                       guint8             max_width)
{
    if (current_gen == PULS_PCIE_GEN_UNKNOWN && max_gen == PULS_PCIE_GEN_UNKNOWN)
        return NULL;

    const gchar *curr_str = puls_pcie_generation_to_string (current_gen);
    const gchar *max_str  = puls_pcie_generation_to_string (max_gen);

    if (current_gen != PULS_PCIE_GEN_UNKNOWN && max_gen != PULS_PCIE_GEN_UNKNOWN && max_width > 0) {
        return g_strdup_printf ("%s x%u (Current: %s x%u | Max: %s x%u)",
                                curr_str, current_width,
                                curr_str, current_width,
                                max_str, max_width);
    }

    if (current_gen != PULS_PCIE_GEN_UNKNOWN) {
        if (current_width > 0)
            return g_strdup_printf ("%s x%u", curr_str, current_width);
        return g_strdup (curr_str);
    }

    if (max_width > 0)
        return g_strdup_printf ("Max: %s x%u", max_str, max_width);
    return g_strdup_printf ("Max: %s", max_str);
}

gchar *
puls_sysfs_find_pcie_dir (const gchar *device_name)
{
    g_autofree gchar *clean_name = puls_sanitize_device_name (device_name);
    if (clean_name == NULL)
        return NULL;

    /* Candidate 1: /sys/block/<clean_name>/device/device (standard for NVMe controllers) */
    g_autofree gchar *p1 = g_strdup_printf ("/sys/block/%s/device/device", clean_name);
    g_autofree gchar *f1 = g_build_filename (p1, "current_link_speed", NULL);
    if (g_file_test (f1, G_FILE_TEST_EXISTS))
        return puls_sysfs_resolve_link (p1);

    /* Candidate 1b: /sys/class/nvme/<controller>/device (controller-level / multipath fallback) */
    g_autofree gchar *nvme_ctrl = NULL;
    if (puls_nvme_parse_device_name (clean_name, &nvme_ctrl, NULL)) {
        g_autofree gchar *p_ctrl = g_strdup_printf ("/sys/class/nvme/%s/device", nvme_ctrl);
        g_autofree gchar *f_ctrl = g_build_filename (p_ctrl, "current_link_speed", NULL);
        if (g_file_test (f_ctrl, G_FILE_TEST_EXISTS))
            return puls_sysfs_resolve_link (p_ctrl);
    }

    /* Candidate 2: /sys/block/<clean_name>/device (direct PCI block device) */
    g_autofree gchar *p2 = g_strdup_printf ("/sys/block/%s/device", clean_name);
    g_autofree gchar *f2 = g_build_filename (p2, "current_link_speed", NULL);
    if (g_file_test (f2, G_FILE_TEST_EXISTS))
        return puls_sysfs_resolve_link (p2);

    /* Candidate 3: Resolve /sys/block/<clean_name>/device and walk up parents */
    g_autofree gchar *real_dev = puls_sysfs_resolve_link (p2);
    if (real_dev) {
        gchar *curr = g_strdup (real_dev);
        for (gint i = 0; i < 6; i++) {
            g_autofree gchar *test_speed = g_build_filename (curr, "current_link_speed", NULL);
            if (g_file_test (test_speed, G_FILE_TEST_EXISTS))
                return curr;

            gchar *parent = g_path_get_dirname (curr);
            g_free (curr);
            curr = parent;
            if (curr == NULL || g_strcmp0 (curr, "/") == 0 || g_strcmp0 (curr, "/sys") == 0 || g_strcmp0 (curr, ".") == 0) {
                g_free (curr);
                break;
            }
        }
    }

    return NULL;
}

gboolean
puls_sysfs_read_pcie_link_from_dir (const gchar      *pci_dir,
                                    PulsPcieLinkInfo *out_info)
{
    if (pci_dir == NULL || out_info == NULL)
        return FALSE;

    if (!g_file_test (pci_dir, G_FILE_TEST_IS_DIR))
        return FALSE;

    puls_pcie_link_info_clear (out_info);

    g_autofree gchar *p_curr_spd = g_build_filename (pci_dir, "current_link_speed", NULL);
    g_autofree gchar *p_curr_w   = g_build_filename (pci_dir, "current_link_width", NULL);
    g_autofree gchar *p_max_spd  = g_build_filename (pci_dir, "max_link_speed", NULL);
    g_autofree gchar *p_max_w    = g_build_filename (pci_dir, "max_link_width", NULL);

    g_autofree gchar *curr_spd_str = puls_sysfs_read_string (p_curr_spd, sizeof (out_info->current_speed_raw) - 1);
    g_autofree gchar *max_spd_str  = puls_sysfs_read_string (p_max_spd, sizeof (out_info->max_speed_raw) - 1);

    if (curr_spd_str) {
        g_strlcpy (out_info->current_speed_raw, curr_spd_str, sizeof (out_info->current_speed_raw));
        out_info->current_gen = puls_pcie_speed_to_generation (curr_spd_str);
    }

    if (max_spd_str) {
        g_strlcpy (out_info->max_speed_raw, max_spd_str, sizeof (out_info->max_speed_raw));
        out_info->max_gen = puls_pcie_speed_to_generation (max_spd_str);
    }

    out_info->current_width = (guint8)puls_sysfs_read_uint64 (p_curr_w, 0, NULL);
    out_info->max_width     = (guint8)puls_sysfs_read_uint64 (p_max_w, 0, NULL);

    if (out_info->current_gen == PULS_PCIE_GEN_UNKNOWN && out_info->current_width == 0 &&
        out_info->max_gen == PULS_PCIE_GEN_UNKNOWN && out_info->max_width == 0) {
        return FALSE;
    }

    if (out_info->max_gen > PULS_PCIE_GEN_UNKNOWN &&
        out_info->current_gen > PULS_PCIE_GEN_UNKNOWN &&
        out_info->current_gen < out_info->max_gen) {
        out_info->is_downgraded = TRUE;
    }

    if (out_info->max_width > 0 &&
        out_info->current_width > 0 &&
        out_info->current_width < out_info->max_width) {
        out_info->is_downgraded = TRUE;
    }

    out_info->device_dir = g_strdup (pci_dir);
    out_info->description = puls_format_pcie_link (out_info->current_gen,
                                                   out_info->current_width,
                                                   out_info->max_gen,
                                                   out_info->max_width);
    return TRUE;
}

gboolean
puls_sysfs_read_pcie_link (const gchar      *device_name,
                           PulsPcieLinkInfo *out_info)
{
    if (device_name == NULL || out_info == NULL)
        return FALSE;

    g_autofree gchar *pci_dir = puls_sysfs_find_pcie_dir (device_name);
    if (pci_dir == NULL)
        return FALSE;

    return puls_sysfs_read_pcie_link_from_dir (pci_dir, out_info);
}

gchar *
puls_get_transport (const gchar *device_name)
{
    if (device_name == NULL)
        return g_strdup ("unknown");

    if (g_str_has_prefix (device_name, "nvme"))
        return g_strdup ("nvme");

    if (g_str_has_prefix (device_name, "mmcblk"))
        return g_strdup ("mmc");

    if (g_str_has_prefix (device_name, "loop") ||
        g_str_has_prefix (device_name, "ram")  ||
        g_str_has_prefix (device_name, "zram") ||
        g_str_has_prefix (device_name, "dm-")  ||
        g_str_has_prefix (device_name, "vd")   ||
        g_str_has_prefix (device_name, "xvd"))
        return g_strdup ("virtual");

    g_autofree gchar *device_link = g_strdup_printf ("/sys/block/%s/device",
                                                      device_name);
    g_autofree gchar *resolved = g_file_read_link (device_link, NULL);

    if (resolved) {
        if (strstr (resolved, "usb"))
            return g_strdup ("usb");
        if (strstr (resolved, "sas") || strstr (resolved, "expander"))
            return g_strdup ("sas");
        if (strstr (resolved, "ata") || strstr (resolved, "sata") || strstr (resolved, "scsi"))
            return g_strdup ("sata");
    }

    gchar real_path_buf[PATH_MAX];
    if (realpath (device_link, real_path_buf) != NULL) {
        if (strstr (real_path_buf, "usb"))
            return g_strdup ("usb");
        if (strstr (real_path_buf, "sas"))
            return g_strdup ("sas");
    }

    if (g_str_has_prefix (device_name, "sd") || g_str_has_prefix (device_name, "hd"))
        return g_strdup ("sata");

    return g_strdup ("unknown");
}

PulsTransportProtocol
puls_detect_transport_protocol (const gchar *device_path)
{
    if (device_path == NULL)
        return PULS_TRANSPORT_UNKNOWN;

    const gchar *device_name = strrchr (device_path, '/');
    if (device_name)
        device_name++;
    else
        device_name = device_path;

    g_autofree gchar *transport = puls_get_transport (device_name);
    return puls_transport_protocol_from_string (transport);
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

    if (g_str_has_prefix (device_name, "loop") ||
        g_str_has_prefix (device_name, "ram")  ||
        g_str_has_prefix (device_name, "zram") ||
        g_str_has_prefix (device_name, "dm-")  ||
        g_str_has_prefix (device_name, "vd")   ||
        g_str_has_prefix (device_name, "xvd"))
        return PULS_DRIVE_TYPE_VIRTUAL;

    if (g_str_has_prefix (device_name, "mmcblk")) {
        g_autofree gchar *type_path = g_strdup_printf ("/sys/block/%s/device/type", device_name);
        g_autofree gchar *type_contents = NULL;
        if (g_file_get_contents (type_path, &type_contents, NULL, NULL)) {
            g_strstrip (type_contents);
            if (g_ascii_strcasecmp (type_contents, "SD") == 0)
                return PULS_DRIVE_TYPE_SD_CARD;
        }
        return PULS_DRIVE_TYPE_EMMC;
    }

    g_autofree gchar *transport = puls_get_transport (device_name);

    if (g_strcmp0 (transport, "nvme") == 0)
        return PULS_DRIVE_TYPE_NVME_SSD;

    if (g_strcmp0 (transport, "usb") == 0)
        return PULS_DRIVE_TYPE_USB;

    if (g_strcmp0 (transport, "sas") == 0) {
        gboolean rotational = puls_is_rotational (device_name);
        return rotational ? PULS_DRIVE_TYPE_SAS_HDD : PULS_DRIVE_TYPE_SAS_SSD;
    }

    if (g_strcmp0 (transport, "virtual") == 0)
        return PULS_DRIVE_TYPE_VIRTUAL;

    gboolean rotational = puls_is_rotational (device_name);
    return rotational ? PULS_DRIVE_TYPE_SATA_HDD : PULS_DRIVE_TYPE_SATA_SSD;
}

void
puls_usb_bridge_info_clear (PulsUsbBridgeInfo *info)
{
    if (info == NULL)
        return;
    g_free (info->device_dir);
    g_free (info->version);
    g_free (info->vendor_id);
    g_free (info->product_id);
    g_free (info->manufacturer);
    g_free (info->product);
    g_free (info->serial);
    g_free (info->bridge_chip);
    g_free (info->display_string);
    memset (info, 0, sizeof (*info));
}

gchar *
puls_identify_usb_bridge_chip (const gchar *vendor_id,
                               const gchar *product_id,
                               const gchar *manufacturer,
                               const gchar *product)
{
    if (vendor_id == NULL)
        return NULL;

    g_autofree gchar *vid = g_ascii_strdown (vendor_id, -1);
    g_autofree gchar *pid = product_id ? g_ascii_strdown (product_id, -1) : NULL;

    /* ASMedia (174c) */
    if (g_strcmp0 (vid, "174c") == 0) {
        if (pid) {
            if (g_strcmp0 (pid, "55aa") == 0) return g_strdup ("ASMedia ASM1051/1053 SATA Bridge");
            if (g_strcmp0 (pid, "1153") == 0) return g_strdup ("ASMedia ASM1153E SATA 6Gb/s Bridge");
            if (g_strcmp0 (pid, "2362") == 0) return g_strdup ("ASMedia ASM2362 NVMe-USB Bridge");
            if (g_strcmp0 (pid, "2364") == 0) return g_strdup ("ASMedia ASM2364 NVMe-USB Bridge");
            if (g_strcmp0 (pid, "2464") == 0) return g_strdup ("ASMedia ASM2464PD USB4/TB to PCIe Bridge");
        }
        return g_strdup ("ASMedia USB Storage Bridge");
    }

    /* Realtek (0bda) */
    if (g_strcmp0 (vid, "0bda") == 0) {
        if (pid) {
            if (g_strcmp0 (pid, "9210") == 0) return g_strdup ("Realtek RTL9210/RTL9210B NVMe/SATA Bridge");
            if (g_strcmp0 (pid, "9201") == 0) return g_strdup ("Realtek RTL9201 Storage Bridge");
        }
        return g_strdup ("Realtek USB Storage Bridge");
    }

    /* JMicron (152d) */
    if (g_strcmp0 (vid, "152d") == 0) {
        if (pid) {
            if (g_strcmp0 (pid, "0567") == 0) return g_strdup ("JMicron JMS567 SATA 6Gb/s Bridge");
            if (g_strcmp0 (pid, "0578") == 0) return g_strdup ("JMicron JMS578 SATA 6Gb/s Bridge");
            if (g_strcmp0 (pid, "0583") == 0) return g_strdup ("JMicron JMS583 NVMe-USB Bridge");
            if (g_strcmp0 (pid, "0580") == 0) return g_strdup ("JMicron JMS580 Type-C to SATA Bridge");
        }
        return g_strdup ("JMicron USB Storage Bridge");
    }

    /* Genesys Logic (05e3) */
    if (g_strcmp0 (vid, "05e3") == 0) {
        if (pid) {
            if (g_strcmp0 (pid, "0749") == 0) return g_strdup ("Genesys Logic GL3321 USB-SATA Bridge");
            if (g_strcmp0 (pid, "0751") == 0) return g_strdup ("Genesys Logic GL3324 NVMe Bridge");
        }
        return g_strdup ("Genesys Logic USB Storage Bridge");
    }

    /* VIA Labs (2109) */
    if (g_strcmp0 (vid, "2109") == 0) {
        if (pid) {
            if (g_strcmp0 (pid, "0715") == 0 || g_strcmp0 (pid, "0716") == 0)
                return g_strdup ("VIA VL715/VL716 SATA/NVMe Bridge");
        }
        return g_strdup ("VIA Labs USB Storage Bridge");
    }

    /* Innostor (1f75) */
    if (g_strcmp0 (vid, "1f75") == 0)
        return g_strdup ("Innostor IS916/IS917 Flash Controller");

    /* Phison (13fe) */
    if (g_strcmp0 (vid, "13fe") == 0)
        return g_strdup ("Phison USB Storage Controller");

    /* Fallback on product descriptor strings */
    if (product) {
        if (strstr (product, "RTL9210")) return g_strdup ("Realtek RTL9210 NVMe/SATA Bridge");
        if (strstr (product, "JMS583"))  return g_strdup ("JMicron JMS583 NVMe Bridge");
        if (strstr (product, "ASM2362")) return g_strdup ("ASMedia ASM2362 NVMe Bridge");
        if (strstr (product, "ASM2364")) return g_strdup ("ASMedia ASM2364 NVMe Bridge");
        if (manufacturer && strlen (manufacturer) > 0)
            return g_strdup_printf ("%s %s", manufacturer, product);
        return g_strdup (product);
    }

    return NULL;
}

gchar *
puls_sysfs_find_usb_device_dir (const gchar *device_name)
{
    g_autofree gchar *clean_name = puls_sanitize_device_name (device_name);
    if (clean_name == NULL)
        return NULL;

    g_autofree gchar *dev_link = g_strdup_printf ("/sys/block/%s/device", clean_name);
    g_autofree gchar *real_path = puls_sysfs_resolve_link (dev_link);
    if (real_path == NULL)
        return NULL;

    gchar *current_dir = g_strdup (real_path);
    for (gint i = 0; i < 12; i++) {
        g_autofree gchar *speed_path = g_build_filename (current_dir, "speed", NULL);
        g_autofree gchar *id_vendor_path = g_build_filename (current_dir, "idVendor", NULL);

        if (g_file_test (speed_path, G_FILE_TEST_EXISTS) && g_file_test (id_vendor_path, G_FILE_TEST_EXISTS))
            return current_dir;

        gchar *parent_dir = g_path_get_dirname (current_dir);
        g_free (current_dir);
        current_dir = parent_dir;

        if (current_dir == NULL || g_strcmp0 (current_dir, "/") == 0 ||
            g_strcmp0 (current_dir, "/sys") == 0 || g_strcmp0 (current_dir, ".") == 0) {
            g_free (current_dir);
            break;
        }
    }

    return NULL;
}

gboolean
puls_sysfs_read_usb_bridge_info_from_dir (const gchar       *usb_dir,
                                          PulsUsbBridgeInfo *out_info)
{
    if (usb_dir == NULL || out_info == NULL)
        return FALSE;

    if (!g_file_test (usb_dir, G_FILE_TEST_IS_DIR))
        return FALSE;

    puls_usb_bridge_info_clear (out_info);

    g_autofree gchar *p_speed   = g_build_filename (usb_dir, "speed", NULL);
    g_autofree gchar *p_version = g_build_filename (usb_dir, "version", NULL);
    g_autofree gchar *p_vid     = g_build_filename (usb_dir, "idVendor", NULL);
    g_autofree gchar *p_pid     = g_build_filename (usb_dir, "idProduct", NULL);
    g_autofree gchar *p_mfg     = g_build_filename (usb_dir, "manufacturer", NULL);
    g_autofree gchar *p_prod    = g_build_filename (usb_dir, "product", NULL);
    g_autofree gchar *p_serial  = g_build_filename (usb_dir, "serial", NULL);

    out_info->speed_mbps = puls_sysfs_read_uint64 (p_speed, 0, NULL);
    out_info->speed = puls_usb_speed_from_mbps (out_info->speed_mbps);

    out_info->version = puls_sysfs_read_string (p_version, 32);
    out_info->vendor_id = puls_sysfs_read_string (p_vid, 32);
    out_info->product_id = puls_sysfs_read_string (p_pid, 32);
    out_info->manufacturer = puls_sysfs_read_string (p_mfg, 128);
    out_info->product = puls_sysfs_read_string (p_prod, 128);
    out_info->serial = puls_sysfs_read_string (p_serial, 128);

    out_info->bridge_chip = puls_identify_usb_bridge_chip (out_info->vendor_id,
                                                           out_info->product_id,
                                                           out_info->manufacturer,
                                                           out_info->product);

    const gchar *badge = puls_usb_speed_to_generation_badge (out_info->speed);
    if (out_info->bridge_chip && out_info->bridge_chip[0] != '\0') {
        out_info->display_string = g_strdup_printf ("%s | %s", badge, out_info->bridge_chip);
    } else if (out_info->product && out_info->product[0] != '\0') {
        out_info->display_string = g_strdup_printf ("%s | %s", badge, out_info->product);
    } else {
        out_info->display_string = g_strdup (badge);
    }

    out_info->device_dir = g_strdup (usb_dir);
    return TRUE;
}

gboolean
puls_sysfs_read_usb_bridge_info (const gchar       *device_name,
                                 PulsUsbBridgeInfo *out_info)
{
    if (device_name == NULL || out_info == NULL)
        return FALSE;

    g_autofree gchar *usb_dir = puls_sysfs_find_usb_device_dir (device_name);
    if (usb_dir == NULL)
        return FALSE;

    return puls_sysfs_read_usb_bridge_info_from_dir (usb_dir, out_info);
}

gchar *
puls_detect_usb_speed_version (const gchar *device_name)
{
    if (device_name == NULL)
        return NULL;

    PulsUsbBridgeInfo info = { 0 };
    if (puls_sysfs_read_usb_bridge_info (device_name, &info)) {
        gchar *res = g_strdup (info.display_string);
        puls_usb_bridge_info_clear (&info);
        return res;
    }

    return NULL;
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

static gboolean
device_name_is_partition_of (const gchar *child_name, const gchar *parent_name)
{
    if (child_name == NULL || parent_name == NULL)
        return FALSE;
    if (g_strcmp0 (child_name, parent_name) == 0)
        return TRUE;
    if (!g_str_has_prefix (child_name, parent_name))
        return FALSE;

    const gchar *suffix = child_name + strlen (parent_name);
    if (*suffix == '\0')
        return FALSE;

    gboolean parent_ends_with_digit = g_ascii_isdigit (parent_name[strlen (parent_name) - 1]);
    if (parent_ends_with_digit) {
        /* When parent ends with a digit (e.g. nvme0n1, mmcblk0, loop0),
         * partition names must begin with 'p' followed by digits (e.g. nvme0n1p1). */
        if (suffix[0] == 'p' && g_ascii_isdigit (suffix[1])) {
            for (gsize i = 1; suffix[i] != '\0'; i++) {
                if (!g_ascii_isdigit (suffix[i]))
                    return FALSE;
            }
            return TRUE;
        }
        return FALSE;
    } else {
        /* When parent ends with a letter (e.g. sda, vda, hda),
         * partition names are digits directly (e.g. sda1, sda12)
         * or in some rare cases 'p' + digits (e.g. nbda1 or nbdap1). */
        if (g_ascii_isdigit (suffix[0])) {
            for (gsize i = 0; suffix[i] != '\0'; i++) {
                if (!g_ascii_isdigit (suffix[i]))
                    return FALSE;
            }
            return TRUE;
        }
        if (suffix[0] == 'p' && g_ascii_isdigit (suffix[1])) {
            for (gsize i = 1; suffix[i] != '\0'; i++) {
                if (!g_ascii_isdigit (suffix[i]))
                    return FALSE;
            }
            return TRUE;
        }
        return FALSE;
    }
}

static void
collect_holders_recursive (const gchar *block_name, GHashTable *holders_set, gint depth)
{
    if (depth <= 0 || block_name == NULL || holders_set == NULL)
        return;

    g_autofree gchar *holders_dir = g_build_filename ("/sys/class/block", block_name, "holders", NULL);
    GDir *dir = g_dir_open (holders_dir, 0, NULL);
    if (!dir)
        return;

    const gchar *entry;
    while ((entry = g_dir_read_name (dir)) != NULL) {
        if (!g_hash_table_contains (holders_set, entry)) {
            g_hash_table_add (holders_set, g_strdup (entry));
            collect_holders_recursive (entry, holders_set, depth - 1);
        }
    }
    g_dir_close (dir);
}

static GHashTable *
puls_collect_holders_for_device (const gchar *parent_name)
{
    GHashTable *holders_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    if (parent_name == NULL || *parent_name == '\0')
        return holders_set;

    /* Check parent device holders: /sys/class/block/<parent_name>/holders */
    collect_holders_recursive (parent_name, holders_set, 8);

    /* Check partition holders: /sys/class/block/<partition>/holders */
    GDir *dir = g_dir_open ("/sys/class/block", 0, NULL);
    if (dir) {
        const gchar *entry;
        while ((entry = g_dir_read_name (dir)) != NULL) {
            if (device_name_is_partition_of (entry, parent_name)) {
                collect_holders_recursive (entry, holders_set, 8);
            }
        }
        g_dir_close (dir);
    }

    return holders_set;
}

static gboolean
check_dm_slaves_recursive (const gchar *dm_name, const gchar *parent_name, gint depth)
{
    if (depth <= 0 || dm_name == NULL || parent_name == NULL)
        return FALSE;

    g_autofree gchar *slaves_dir = g_build_filename ("/sys/class/block", dm_name, "slaves", NULL);
    GDir *dir = g_dir_open (slaves_dir, 0, NULL);
    if (!dir)
        return FALSE;

    gboolean found = FALSE;
    const gchar *entry;
    while ((entry = g_dir_read_name (dir)) != NULL) {
        if (device_name_is_partition_of (entry, parent_name)) {
            found = TRUE;
            break;
        }
        if (g_str_has_prefix (entry, "dm-")) {
            if (check_dm_slaves_recursive (entry, parent_name, depth - 1)) {
                found = TRUE;
                break;
            }
        }
    }
    g_dir_close (dir);
    return found;
}

static gboolean
is_mock_mapper_for_device (const gchar *mnt_fsname, const gchar *parent_name)
{
    if (mnt_fsname == NULL || parent_name == NULL)
        return FALSE;

    const gchar *p = strstr (mnt_fsname, parent_name);
    if (p == NULL)
        return FALSE;

    gsize parent_len = strlen (parent_name);
    const gchar *after = p + parent_len;

    gboolean parent_ends_digit = g_ascii_isdigit (parent_name[parent_len - 1]);
    gsize part_suffix_len = 0;

    if (parent_ends_digit) {
        if (after[0] == 'p' && g_ascii_isdigit (after[1])) {
            part_suffix_len = 1;
            while (g_ascii_isdigit (after[part_suffix_len]))
                part_suffix_len++;
        }
    } else {
        while (g_ascii_isdigit (after[part_suffix_len]))
            part_suffix_len++;
    }

    if (part_suffix_len > 0) {
        gchar term = after[part_suffix_len];
        if (term == '\0' || term == '_' || term == '-' || term == '.' || term == '/')
            return TRUE;
    }

    return FALSE;
}

static gboolean
puls_is_partition_or_dm_of (const gchar  *mnt_fsname,
                            const gchar  *parent_device_path,
                            const gchar  *parent_name,
                            GHashTable   *holders_set)
{
    if (mnt_fsname == NULL || parent_device_path == NULL || parent_name == NULL)
        return FALSE;

    /* Direct match with parent device path */
    if (g_strcmp0 (mnt_fsname, parent_device_path) == 0)
        return TRUE;

    /* Extract leaf device name from mount fsname (e.g. /dev/sda1 -> sda1) */
    const gchar *fs_name = strrchr (mnt_fsname, '/');
    fs_name = fs_name ? fs_name + 1 : mnt_fsname;

    /* Direct partition matching */
    if (device_name_is_partition_of (fs_name, parent_name))
        return TRUE;

    /* Check holders collected from sysfs */
    if (holders_set != NULL && g_hash_table_contains (holders_set, fs_name))
        return TRUE;

    /* If fs_name is dm-X, check its slaves */
    if (g_str_has_prefix (fs_name, "dm-") && check_dm_slaves_recursive (fs_name, parent_name, 8))
        return TRUE;

    /* Resolve realpath (handles /dev/mapper/..., /dev/disk/by-uuid/..., etc.) */
    gchar *resolved = realpath (mnt_fsname, NULL);
    if (resolved != NULL) {
        const gchar *res_name = strrchr (resolved, '/');
        res_name = res_name ? res_name + 1 : resolved;

        gboolean match = FALSE;
        if (device_name_is_partition_of (res_name, parent_name))
            match = TRUE;
        else if (holders_set != NULL && g_hash_table_contains (holders_set, res_name))
            match = TRUE;
        else if (g_str_has_prefix (res_name, "dm-") && check_dm_slaves_recursive (res_name, parent_name, 8))
            match = TRUE;

        free (resolved);
        if (match)
            return TRUE;
    }

    /* Fallback for mock mounts / synthetic mapper device names */
    if (is_mock_mapper_for_device (mnt_fsname, parent_name))
        return TRUE;

    return FALSE;
}

GList *
puls_get_disk_partitions_from_mounts (const gchar *device_path,
                                     const gchar *mounts_file)
{
    if (device_path == NULL || mounts_file == NULL || *device_path == '\0' || *mounts_file == '\0')
        return NULL;

    const gchar *parent_name = strrchr (device_path, '/');
    parent_name = parent_name ? parent_name + 1 : device_path;
    if (*parent_name == '\0')
        return NULL;

    FILE *f = setmntent (mounts_file, "r");
    if (f == NULL)
        return NULL;

    GHashTable *holders_set = puls_collect_holders_for_device (parent_name);
    GList *list = NULL;
    struct mntent *mnt;

    while ((mnt = getmntent (f)) != NULL) {
        if (mnt->mnt_fsname == NULL || mnt->mnt_dir == NULL)
            continue;

        if (puls_is_partition_or_dm_of (mnt->mnt_fsname, device_path, parent_name, holders_set)) {
            /* Avoid duplicate (device_path, mount_point) entries */
            gboolean exists = FALSE;
            for (GList *l = list; l != NULL; l = l->next) {
                PulsPartitionInfo *existing = l->data;
                if (g_strcmp0 (existing->device_path, mnt->mnt_fsname) == 0 &&
                    g_strcmp0 (existing->mount_point, mnt->mnt_dir) == 0) {
                    exists = TRUE;
                    break;
                }
            }
            if (exists)
                continue;

            PulsPartitionInfo *info = g_new0 (PulsPartitionInfo, 1);
            info->device_path = g_strdup (mnt->mnt_fsname);
            info->mount_point = g_strdup (mnt->mnt_dir);
            info->fs_type = g_strdup (mnt->mnt_type ? mnt->mnt_type : "unknown");

            struct statvfs vfs;
            if (statvfs (mnt->mnt_dir, &vfs) == 0 && vfs.f_blocks > 0) {
                info->total_bytes = (guint64)vfs.f_blocks * vfs.f_frsize;
                info->available_bytes = (guint64)vfs.f_bavail * vfs.f_frsize;
            } else {
                info->total_bytes = 0;
                info->available_bytes = 0;
            }

            list = g_list_append (list, info);
        }
    }

    g_hash_table_destroy (holders_set);
    endmntent (f);
    return list;
}

GList *
puls_get_disk_partitions (const gchar *device_path)
{
    return puls_get_disk_partitions_from_mounts (device_path, "/proc/mounts");
}

GList *
puls_get_all_disk_partitions (const gchar *device_path)
{
    if (device_path == NULL)
        return NULL;

    gchar *argv[] = {
        "lsblk", "-nlo", "NAME,TYPE", (gchar *)device_path, NULL
    };

    g_autofree gchar *output = NULL;
    if (!g_spawn_sync (NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH,
                       NULL, NULL,
                       &output, NULL, NULL, NULL) || output == NULL)
        return NULL;

    GList *list = NULL;
    gchar **lines = g_strsplit (output, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip (lines[i]);
        if (*line == '\0') continue;

        gchar **cols = g_strsplit_set (line, " \t", -1);
        const gchar *name = NULL;
        const gchar *type = NULL;
        gint filled = 0;
        for (gint j = 0; cols[j] != NULL; j++) {
            gchar *tok = g_strstrip (cols[j]);
            if (*tok == '\0') continue;
            if (filled == 0) { name = tok; filled++; }
            else             { type = tok; filled++; break; }
        }

        if (name && type && g_strcmp0 (type, "part") == 0) {
            gchar *full_path = g_strdup_printf ("/dev/%s", name);
            PulsPartitionInfo *info = g_new0 (PulsPartitionInfo, 1);
            info->device_path   = full_path;
            info->mount_point   = g_strdup ("—");
            info->fs_type       = g_strdup ("unknown");
            info->total_bytes   = 0;
            info->available_bytes = 0;
            list = g_list_append (list, info);
        }
        g_strfreev (cols);
    }
    g_strfreev (lines);

    if (list == NULL) {
        PulsPartitionInfo *info = g_new0 (PulsPartitionInfo, 1);
        info->device_path   = g_strdup (device_path);
        info->mount_point   = g_strdup ("—");
        info->fs_type       = g_strdup ("unknown");
        info->total_bytes   = 0;
        info->available_bytes = 0;
        list = g_list_append (list, info);
    }

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

gboolean
puls_get_partition_usage (const gchar *mount_point,
                          guint64     *out_used_bytes,
                          guint64     *out_total_bytes)
{
    g_return_val_if_fail (mount_point != NULL, FALSE);

    struct statvfs vfs;
    if (statvfs (mount_point, &vfs) != 0)
        return FALSE;

    guint64 total = (guint64)vfs.f_blocks * vfs.f_frsize;
    guint64 avail = (guint64)vfs.f_bavail * vfs.f_frsize;
    guint64 used  = (total > avail) ? (total - avail) : 0;

    if (out_total_bytes) *out_total_bytes = total;
    if (out_used_bytes)  *out_used_bytes  = used;
    return TRUE;
}

static GLogWriterOutput
puls_log_writer (GLogLevelFlags   log_level,
                 const GLogField *fields,
                 gsize            n_fields,
                 gpointer         user_data G_GNUC_UNUSED)
{
    const gchar *domain = NULL;
    const gchar *message = NULL;

    for (gsize i = 0; i < n_fields; i++) {
        if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0)
            domain = fields[i].value;
        else if (g_strcmp0 (fields[i].key, "MESSAGE") == 0)
            message = fields[i].value;
    }

    if (domain == NULL)
        domain = "puls";

    if (!g_str_has_prefix (domain, "puls"))
        return g_log_writer_default (log_level, fields, n_fields, user_data);

    if ((log_level & (G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO)) != 0) {
        const gchar *debug_env = g_getenv ("G_MESSAGES_DEBUG");
        if (debug_env == NULL)
            return G_LOG_WRITER_HANDLED;

        if (g_strcmp0 (debug_env, "all") != 0 &&
            strstr (debug_env, "puls") == NULL &&
            strstr (debug_env, domain) == NULL)
            return G_LOG_WRITER_HANDLED;
    }

    if (message == NULL)
        message = "";

    GDateTime *now = g_date_time_new_now_local ();
    g_autofree gchar *time_str = g_date_time_format (now, "%H:%M:%S");
    gint usec = g_date_time_get_microsecond (now) / 1000;
    g_date_time_unref (now);

    gboolean is_tty = isatty (fileno (stderr));
    const gchar *level_tag = "INFO ";
    const gchar *color_start = "";
    const gchar *color_end = is_tty ? "\033[0m" : "";

    if (log_level & G_LOG_LEVEL_DEBUG) {
        level_tag = "DEBUG";
        if (is_tty) color_start = "\033[36m";
    } else if (log_level & G_LOG_LEVEL_INFO) {
        level_tag = "INFO ";
        if (is_tty) color_start = "\033[32m";
    } else if (log_level & G_LOG_LEVEL_WARNING) {
        level_tag = "WARN ";
        if (is_tty) color_start = "\033[33m";
    } else if (log_level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR)) {
        level_tag = "ERROR";
        if (is_tty) color_start = "\033[1;31m";
    }

    if (is_tty) {
        fprintf (stderr, "[%s.%03d] %s[%s]%s \033[35m[%s]\033[0m %s\n",
                 time_str, usec,
                 color_start, level_tag, color_end,
                 domain, message);
    } else {
        fprintf (stderr, "[%s.%03d] [%s] [%s] %s\n",
                 time_str, usec,
                 level_tag,
                 domain, message);
    }
    fflush (stderr);

    return G_LOG_WRITER_HANDLED;
}

void
puls_log_init (void)
{
    static gsize initialized = 0;
    if (g_once_init_enter (&initialized)) {
        g_log_set_writer_func (puls_log_writer, NULL, NULL);
        g_once_init_leave (&initialized, 1);
    }
}

void
puls_log_structured (const gchar   *domain,
                     GLogLevelFlags level,
                     const gchar   *file,
                     gint           line,
                     const gchar   *func,
                     const gchar   *format,
                     ...)
{
    va_list args;
    va_start (args, format);
    g_autofree gchar *formatted = g_strdup_vprintf (format, args);
    va_end (args);

    g_autofree gchar *line_str = g_strdup_printf ("%d", line);

    g_log_structured (domain ? domain : "puls",
                      level,
                      "CODE_FILE", file ? file : "",
                      "CODE_LINE", line_str,
                      "CODE_FUNC", func ? func : "",
                      "MESSAGE", "%s", formatted);
}

/**
 * puls_nvme_topology_clear:
 * @topo: A #PulsNvmeTopology struct.
 *
 * Frees owned fields in @topo and resets its memory to zero.
 */
void
puls_nvme_topology_clear (PulsNvmeTopology *topo)
{
    if (topo == NULL)
        return;

    g_free (topo->controller_name);
    g_free (topo->controller_path);
    g_free (topo->namespace_name);
    g_free (topo->namespace_path);
    g_free (topo->transport);
    g_free (topo->subsysnqn);
    g_free (topo->eui64);
    g_free (topo->nguid);
    g_free (topo->wwid);
    g_free (topo->state);
    memset (topo, 0, sizeof (*topo));
    topo->cntlid = -1;
}

typedef enum {
    NVME_KIND_INVALID = 0,
    NVME_KIND_CONTROLLER,
    NVME_KIND_NAMESPACE,
    NVME_KIND_PARTITION
} NvmeNameKind;

static NvmeNameKind
classify_nvme_name (const gchar *name_or_path,
                    gchar      **out_controller_name,
                    guint32     *out_nsid)
{
    if (name_or_path == NULL)
        return NVME_KIND_INVALID;

    const gchar *base = strrchr (name_or_path, '/');
    base = base ? base + 1 : name_or_path;

    if (!g_str_has_prefix (base, "nvme"))
        return NVME_KIND_INVALID;

    const gchar *p = base + 4;
    if (!g_ascii_isdigit (*p))
        return NVME_KIND_INVALID;

    while (g_ascii_isdigit (*p))
        p++;

    const gchar *ctrl_end = p;

    if (*p == '\0') {
        if (out_controller_name)
            *out_controller_name = g_strndup (base, (gsize)(ctrl_end - base));
        if (out_nsid)
            *out_nsid = 0;
        return NVME_KIND_CONTROLLER;
    }

    /* Optional multipath controller sub-instance (e.g. nvme0c0n1) */
    if (*p == 'c') {
        p++;
        if (!g_ascii_isdigit (*p))
            return NVME_KIND_INVALID;
        while (g_ascii_isdigit (*p))
            p++;
    }

    if (*p != 'n')
        return NVME_KIND_INVALID;
    p++;

    if (!g_ascii_isdigit (*p))
        return NVME_KIND_INVALID;

    const gchar *ns_start = p;
    while (g_ascii_isdigit (*p))
        p++;

    guint64 parsed_nsid = g_ascii_strtoull (ns_start, NULL, 10);
    if (parsed_nsid == 0 || parsed_nsid > G_MAXUINT32)
        return NVME_KIND_INVALID;

    NvmeNameKind kind = NVME_KIND_INVALID;
    if (*p == '\0') {
        kind = NVME_KIND_NAMESPACE;
    } else if (*p == 'p') {
        p++;
        if (!g_ascii_isdigit (*p))
            return NVME_KIND_INVALID;
        while (g_ascii_isdigit (*p))
            p++;
        if (*p != '\0')
            return NVME_KIND_INVALID;
        kind = NVME_KIND_PARTITION;
    } else {
        return NVME_KIND_INVALID;
    }

    if (out_controller_name)
        *out_controller_name = g_strndup (base, (gsize)(ctrl_end - base));
    if (out_nsid)
        *out_nsid = (guint32)parsed_nsid;

    return kind;
}

/**
 * puls_nvme_is_controller_name:
 * @name_or_path: Device name or path (e.g. `nvme0` or `/dev/nvme0`).
 *
 * Returns: %TRUE if @name_or_path identifies a bare NVMe controller node.
 */
gboolean
puls_nvme_is_controller_name (const gchar *name_or_path)
{
    return classify_nvme_name (name_or_path, NULL, NULL) == NVME_KIND_CONTROLLER;
}

/**
 * puls_nvme_is_namespace_name:
 * @name_or_path: Device name or path (e.g. `nvme0n1` or `nvme0c0n1`).
 *
 * Returns: %TRUE if @name_or_path identifies a whole NVMe namespace block device
 * (excluding bare controllers like `nvme0` and partitions like `nvme0n1p1`).
 */
gboolean
puls_nvme_is_namespace_name (const gchar *name_or_path)
{
    return classify_nvme_name (name_or_path, NULL, NULL) == NVME_KIND_NAMESPACE;
}

/**
 * puls_nvme_parse_device_name:
 * @name_or_path: Device name or path (e.g. `/dev/nvme0n1`, `nvme0c1n2`, `nvme0`).
 * @out_controller_name: (out) (optional): Location to store controller name (`nvme0`).
 * @out_nsid: (out) (optional): Location to store parsed namespace ID (`0` if controller only).
 *
 * Parses an NVMe controller, namespace, multipath namespace, or namespace partition
 * name into its owning controller name and optional namespace ID.
 *
 * Returns: %TRUE if @name_or_path is a syntactically valid NVMe identifier.
 */
gboolean
puls_nvme_parse_device_name (const gchar  *name_or_path,
                             gchar       **out_controller_name,
                             guint32      *out_nsid)
{
    return classify_nvme_name (name_or_path, out_controller_name, out_nsid) != NVME_KIND_INVALID;
}

/**
 * puls_nvme_resolve_controller_path:
 * @device_path: Block device path or name (e.g. `/dev/nvme0n1`).
 *
 * Resolves the owning NVMe controller device node (`/dev/nvme0`) for an NVMe
 * namespace by inspecting `/sys/block/<name>/device` first and falling back to
 * NVMe device name topology parsing.
 *
 * Returns: (nullable) (transfer full): Newly allocated controller path, or %NULL if not NVMe.
 */
gchar *
puls_nvme_resolve_controller_path (const gchar *device_path)
{
    if (device_path == NULL)
        return NULL;

    g_autofree gchar *clean_name = puls_sanitize_device_name (device_path);
    g_autofree gchar *ctrl_name = NULL;

    if (clean_name != NULL && puls_nvme_is_namespace_name (clean_name)) {
        g_autofree gchar *dev_link = g_strdup_printf ("/sys/block/%s/device", clean_name);
        g_autofree gchar *target = puls_sysfs_resolve_link (dev_link);
        if (target != NULL) {
            g_autofree gchar *target_base = g_path_get_basename (target);
            if (puls_nvme_is_controller_name (target_base))
                return g_strdup_printf ("/dev/%s", target_base);
        }
    }

    if (puls_nvme_parse_device_name (device_path, &ctrl_name, NULL) && ctrl_name != NULL)
        return g_strdup_printf ("/dev/%s", ctrl_name);

    return NULL;
}

/**
 * puls_nvme_resolve_smart_target:
 * @device_path: Storage block device path (e.g. `/dev/nvme0n1` or `/dev/sda`).
 *
 * Determines the appropriate device node to query via `smartctl`: for NVMe
 * namespaces, targets the owning NVMe controller (`/dev/nvme0`) unless sysfs
 * confirms a real namespace exists without the corresponding controller char node;
 * for all other storage devices, returns a copy of @device_path.
 *
 * Returns: (nullable) (transfer full): Newly allocated target path for SMART queries.
 */
gchar *
puls_nvme_resolve_smart_target (const gchar *device_path)
{
    if (device_path == NULL)
        return NULL;

    g_autofree gchar *ctrl_path = puls_nvme_resolve_controller_path (device_path);
    if (ctrl_path == NULL)
        return g_strdup (device_path);

    g_autofree gchar *clean_name = puls_sanitize_device_name (device_path);
    if (clean_name != NULL && puls_nvme_is_namespace_name (clean_name)) {
        g_autofree gchar *sys_block = g_strdup_printf ("/sys/block/%s", clean_name);
        if (g_file_test (sys_block, G_FILE_TEST_IS_DIR) &&
            !g_file_test (ctrl_path, G_FILE_TEST_EXISTS) &&
            g_file_test (device_path, G_FILE_TEST_EXISTS)) {
            return g_strdup (device_path);
        }
    }

    return g_steal_pointer (&ctrl_path);
}

static gchar *
read_optional_trimmed_attr (const gchar *dir,
                            const gchar *attr,
                            gsize        max_len)
{
    if (dir == NULL || attr == NULL)
        return NULL;

    g_autofree gchar *p = g_build_filename (dir, attr, NULL);
    g_autofree gchar *val = puls_sysfs_read_string (p, max_len);
    if (val == NULL || val[0] == '\0')
        return NULL;

    return g_steal_pointer (&val);
}

/**
 * puls_nvme_read_topology_from_dir:
 * @block_dir: Path to an NVMe namespace block sysfs directory (e.g. `/sys/block/nvme0n1`).
 * @out_topo: Output #PulsNvmeTopology struct.
 *
 * Reads controller and namespace identity attributes from @block_dir and its
 * linked `device` controller directory.
 *
 * Returns: %TRUE if @block_dir represents an NVMe namespace/controller topology.
 */
gboolean
puls_nvme_read_topology_from_dir (const gchar      *block_dir,
                                  PulsNvmeTopology *out_topo)
{
    g_return_val_if_fail (out_topo != NULL, FALSE);

    memset (out_topo, 0, sizeof (*out_topo));
    out_topo->cntlid = -1;

    if (block_dir == NULL || !g_file_test (block_dir, G_FILE_TEST_IS_DIR))
        return FALSE;

    g_autofree gchar *dir_base = g_path_get_basename (block_dir);
    g_autofree gchar *parsed_ctrl = NULL;
    guint32 parsed_nsid = 0;

    if (!puls_nvme_parse_device_name (dir_base, &parsed_ctrl, &parsed_nsid))
        return FALSE;

    out_topo->is_nvme = TRUE;
    out_topo->namespace_name = g_strdup (dir_base);
    out_topo->namespace_path = g_strdup_printf ("/dev/%s", dir_base);

    g_autofree gchar *nsid_path = g_build_filename (block_dir, "nsid", NULL);
    gboolean nsid_ok = FALSE;
    guint64 sysfs_nsid = puls_sysfs_read_uint64 (nsid_path, 0, &nsid_ok);
    out_topo->nsid = (nsid_ok && sysfs_nsid > 0 && sysfs_nsid <= G_MAXUINT32)
                     ? (guint32)sysfs_nsid
                     : parsed_nsid;

    out_topo->eui64 = read_optional_trimmed_attr (block_dir, "eui", 128);
    out_topo->nguid = read_optional_trimmed_attr (block_dir, "nguid", 128);
    out_topo->wwid  = read_optional_trimmed_attr (block_dir, "wwid", 256);

    g_autofree gchar *dev_sub = g_build_filename (block_dir, "device", NULL);
    g_autofree gchar *ctrl_dir = puls_sysfs_resolve_link (dev_sub);
    if (ctrl_dir == NULL && g_file_test (dev_sub, G_FILE_TEST_IS_DIR))
        ctrl_dir = g_strdup (dev_sub);

    if (ctrl_dir != NULL) {
        g_autofree gchar *link_base = g_path_get_basename (ctrl_dir);
        if (puls_nvme_is_controller_name (link_base))
            out_topo->controller_name = g_strdup (link_base);
    }

    if (out_topo->controller_name == NULL && parsed_ctrl != NULL)
        out_topo->controller_name = g_strdup (parsed_ctrl);

    if (out_topo->controller_name != NULL) {
        out_topo->controller_path = g_strdup_printf ("/dev/%s", out_topo->controller_name);
        if (ctrl_dir == NULL) {
            g_autofree gchar *class_ctrl = g_strdup_printf ("/sys/class/nvme/%s",
                                                            out_topo->controller_name);
            if (g_file_test (class_ctrl, G_FILE_TEST_IS_DIR))
                ctrl_dir = g_steal_pointer (&class_ctrl);
        }
    }

    if (ctrl_dir != NULL) {
        g_autofree gchar *cntlid_path = g_build_filename (ctrl_dir, "cntlid", NULL);
        gboolean cntlid_ok = FALSE;
        gint64 cntlid = puls_sysfs_read_int64 (cntlid_path, -1, &cntlid_ok);
        if (cntlid_ok && cntlid >= 0)
            out_topo->cntlid = (gint)cntlid;

        out_topo->transport = read_optional_trimmed_attr (ctrl_dir, "transport", 64);
        out_topo->subsysnqn = read_optional_trimmed_attr (ctrl_dir, "subsysnqn", 512);
        out_topo->state     = read_optional_trimmed_attr (ctrl_dir, "state", 64);
    }

    return TRUE;
}

/**
 * puls_nvme_read_topology:
 * @device_path: Block device path or name (e.g. `/dev/nvme0n1`).
 * @out_topo: Output #PulsNvmeTopology struct.
 *
 * Resolves NVMe controller vs namespace topology from sysfs, falling back to
 * deterministic device path parsing when sysfs entries are unavailable.
 *
 * Returns: %TRUE if @device_path is an NVMe namespace or controller.
 */
gboolean
puls_nvme_read_topology (const gchar      *device_path,
                         PulsNvmeTopology *out_topo)
{
    g_return_val_if_fail (out_topo != NULL, FALSE);

    memset (out_topo, 0, sizeof (*out_topo));
    out_topo->cntlid = -1;

    if (device_path == NULL)
        return FALSE;

    g_autofree gchar *clean_name = puls_sanitize_device_name (device_path);
    if (clean_name != NULL) {
        g_autofree gchar *block_dir = g_strdup_printf ("/sys/block/%s", clean_name);
        if (puls_nvme_read_topology_from_dir (block_dir, out_topo))
            return TRUE;
    }

    g_autofree gchar *ctrl_name = NULL;
    guint32 nsid = 0;
    NvmeNameKind kind = classify_nvme_name (device_path, &ctrl_name, &nsid);
    if (kind == NVME_KIND_INVALID)
        return FALSE;

    out_topo->is_nvme = TRUE;
    out_topo->controller_name = g_strdup (ctrl_name);
    out_topo->controller_path = ctrl_name ? g_strdup_printf ("/dev/%s", ctrl_name) : NULL;
    out_topo->nsid = nsid;

    if (kind == NVME_KIND_NAMESPACE && clean_name != NULL) {
        out_topo->namespace_name = g_strdup (clean_name);
        out_topo->namespace_path = g_strdup_printf ("/dev/%s", clean_name);
    }

    return TRUE;
}

/**
 * puls_scsi_generic_info_clear:
 * @info: A #PulsScsiGenericInfo struct.
 *
 * Frees owned fields in @info and resets it to defaults.
 */
void
puls_scsi_generic_info_clear (PulsScsiGenericInfo *info)
{
    if (info == NULL)
        return;

    g_free (info->sg_name);
    g_free (info->sg_path);
    g_free (info->block_name);
    g_free (info->block_path);
    g_free (info->scsi_address);
    g_free (info->vendor);
    g_free (info->model);
    g_free (info->rev);
    memset (info, 0, sizeof (*info));
    info->scsi_type = -1;
}

/**
 * puls_scsi_generic_info_free:
 * @info: A heap-allocated #PulsScsiGenericInfo.
 *
 * Clears and frees @info.
 */
void
puls_scsi_generic_info_free (PulsScsiGenericInfo *info)
{
    if (info == NULL)
        return;
    puls_scsi_generic_info_clear (info);
    g_free (info);
}

/**
 * puls_scsi_is_generic_name:
 * @name_or_path: Device name (`sg0`) or path (`/dev/sg0`).
 *
 * Returns: %TRUE if @name_or_path matches a valid SCSI generic node (`sg<N>`).
 */
gboolean
puls_scsi_is_generic_name (const gchar *name_or_path)
{
    if (name_or_path == NULL || *name_or_path == '\0')
        return FALSE;

    const gchar *base = g_str_has_prefix (name_or_path, "/dev/")
                        ? (name_or_path + 5)
                        : name_or_path;
    if (strchr (base, '/') != NULL)
        return FALSE;

    if (!g_str_has_prefix (base, "sg"))
        return FALSE;

    const gchar *p = base + 2;
    if (!g_ascii_isdigit (*p))
        return FALSE;

    while (g_ascii_isdigit (*p))
        p++;

    return (*p == '\0');
}

/**
 * puls_scsi_parse_hbtl_address:
 * @sysfs_path: Sysfs device path or SCSI tuple (e.g. `.../0:0:0:0`).
 * @out_hbtl: (out) (optional): Location to store parsed `H:B:T:L` string.
 *
 * Extracts a 4-part SCSI `Host:Bus:Target:Lun` tuple from @sysfs_path.
 *
 * Returns: %TRUE if a valid `H:B:T:L` tuple was found.
 */
gboolean
puls_scsi_parse_hbtl_address (const gchar  *sysfs_path,
                              gchar       **out_hbtl)
{
    if (out_hbtl)
        *out_hbtl = NULL;

    if (sysfs_path == NULL || *sysfs_path == '\0')
        return FALSE;

    g_autofree gchar *base = g_path_get_basename (sysfs_path);
    const gchar *p = base;
    gint parts = 0;

    for (gint i = 0; i < 4; i++) {
        if (!g_ascii_isdigit (*p))
            return FALSE;
        while (g_ascii_isdigit (*p))
            p++;
        parts++;
        if (i < 3) {
            if (*p != ':')
                return FALSE;
            p++;
        }
    }

    if (*p != '\0' || parts != 4)
        return FALSE;

    if (out_hbtl)
        *out_hbtl = g_strdup (base);

    return TRUE;
}

static gchar *
resolve_sg_device_dir (const gchar *sysfs_root,
                       const gchar *sg_name)
{
    g_autofree gchar *dev_link = g_build_filename (sysfs_root, "class", "scsi_generic",
                                                   sg_name, "device", NULL);
    gchar *resolved = puls_sysfs_resolve_link (dev_link);
    if (resolved != NULL)
        return resolved;

    if (g_file_test (dev_link, G_FILE_TEST_IS_DIR))
        return g_steal_pointer (&dev_link);

    return NULL;
}

static gchar *
find_first_child_matching (const gchar *dir_path,
                           gboolean   (*predicate)(const gchar *))
{
    if (dir_path == NULL || !g_file_test (dir_path, G_FILE_TEST_IS_DIR))
        return NULL;

    GDir *dir = g_dir_open (dir_path, 0, NULL);
    if (dir == NULL)
        return NULL;

    GList *matches = NULL;
    const gchar *entry;
    while ((entry = g_dir_read_name (dir)) != NULL) {
        if (predicate == NULL || predicate (entry))
            matches = g_list_append (matches, g_strdup (entry));
    }
    g_dir_close (dir);

    if (matches == NULL)
        return NULL;

    matches = g_list_sort (matches, (GCompareFunc)g_strcmp0);
    gchar *best = g_strdup (matches->data);
    g_list_free_full (matches, g_free);
    return best;
}

static gboolean
is_scsi_block_entry (const gchar *entry)
{
    return (entry != NULL && *entry != '\0' && entry[0] != '.' &&
            strchr (entry, '/') == NULL);
}

/**
 * puls_scsi_generic_to_block_from_sysfs:
 * @sysfs_root: Sysfs root directory (typically `"/sys"`).
 * @sg_device: SCSI generic device name or path (e.g. `"/dev/sg0"` or `"sg0"`).
 *
 * Looks up the block device path (`/dev/sdX`) bound to SCSI generic device
 * @sg_device under @sysfs_root.
 *
 * Returns: (nullable) (transfer full): `/dev/<block>` path, or %NULL if unmapped.
 */
gchar *
puls_scsi_generic_to_block_from_sysfs (const gchar *sysfs_root,
                                       const gchar *sg_device)
{
    if (sysfs_root == NULL || !puls_scsi_is_generic_name (sg_device))
        return NULL;

    const gchar *sg_name = g_str_has_prefix (sg_device, "/dev/") ? (sg_device + 5) : sg_device;
    g_autofree gchar *dev_dir = resolve_sg_device_dir (sysfs_root, sg_name);
    if (dev_dir == NULL)
        return NULL;

    /* Standard sysfs layout: <scsi_device>/block/<sdX> */
    g_autofree gchar *block_sub = g_build_filename (dev_dir, "block", NULL);
    g_autofree gchar *block_name = find_first_child_matching (block_sub, is_scsi_block_entry);
    if (block_name != NULL)
        return g_strdup_printf ("/dev/%s", block_name);

    /* Legacy sysfs layout: <scsi_device>/block:<sdX> symlink */
    GDir *dir = g_dir_open (dev_dir, 0, NULL);
    if (dir != NULL) {
        const gchar *entry;
        while ((entry = g_dir_read_name (dir)) != NULL) {
            if (g_str_has_prefix (entry, "block:") && strlen (entry) > 6) {
                gchar *res = g_strdup_printf ("/dev/%s", entry + 6);
                g_dir_close (dir);
                return res;
            }
        }
        g_dir_close (dir);
    }

    return NULL;
}

/**
 * puls_scsi_generic_to_block:
 * @sg_device: SCSI generic device name or path (e.g. `"/dev/sg0"`).
 *
 * Convenience wrapper querying `"/sys"`.
 *
 * Returns: (nullable) (transfer full): Corresponding block device path or %NULL.
 */
gchar *
puls_scsi_generic_to_block (const gchar *sg_device)
{
    return puls_scsi_generic_to_block_from_sysfs ("/sys", sg_device);
}

/**
 * puls_scsi_block_to_generic_from_sysfs:
 * @sysfs_root: Sysfs root directory (typically `"/sys"`).
 * @block_device: Block device name or path (e.g. `"/dev/sda"` or `"sda"`).
 *
 * Resolves the SCSI generic character device (`/dev/sgX`) corresponding to
 * block device @block_device under @sysfs_root.
 *
 * Returns: (nullable) (transfer full): `/dev/sgX` path, or %NULL if unavailable.
 */
gchar *
puls_scsi_block_to_generic_from_sysfs (const gchar *sysfs_root,
                                       const gchar *block_device)
{
    if (sysfs_root == NULL || block_device == NULL)
        return NULL;

    g_autofree gchar *clean_name = puls_sanitize_device_name (block_device);
    if (clean_name == NULL)
        return NULL;

    g_autofree gchar *dev_link = g_build_filename (sysfs_root, "block", clean_name, "device", NULL);
    g_autofree gchar *real_dev = puls_sysfs_resolve_link (dev_link);
    const gchar *dev_dir = real_dev ? real_dev : dev_link;

    if (!g_file_test (dev_dir, G_FILE_TEST_IS_DIR))
        return NULL;

    /* Candidate 1: <block>/device/scsi_generic/sgX directory */
    g_autofree gchar *sg_sub = g_build_filename (dev_dir, "scsi_generic", NULL);
    g_autofree gchar *sg_name = find_first_child_matching (sg_sub, puls_scsi_is_generic_name);
    if (sg_name != NULL)
        return g_strdup_printf ("/dev/%s", sg_name);

    /* Candidate 2: legacy <block>/device/generic symlink */
    g_autofree gchar *gen_link = g_build_filename (dev_dir, "generic", NULL);
    g_autofree gchar *gen_target = puls_sysfs_resolve_link (gen_link);
    if (gen_target != NULL) {
        g_autofree gchar *base = g_path_get_basename (gen_target);
        if (puls_scsi_is_generic_name (base))
            return g_strdup_printf ("/dev/%s", base);
    }

    /* Candidate 3: match canonical sysfs device path or H:B:T:L under class/scsi_generic */
    g_autofree gchar *target_hbtl = NULL;
    puls_scsi_parse_hbtl_address (dev_dir, &target_hbtl);

    g_autofree gchar *class_dir = g_build_filename (sysfs_root, "class", "scsi_generic", NULL);
    GDir *dir = g_dir_open (class_dir, 0, NULL);
    if (dir == NULL)
        return NULL;

    const gchar *entry;
    while ((entry = g_dir_read_name (dir)) != NULL) {
        if (!puls_scsi_is_generic_name (entry))
            continue;

        g_autofree gchar *sg_dev_dir = resolve_sg_device_dir (sysfs_root, entry);
        if (sg_dev_dir == NULL)
            continue;

        gboolean matched = (real_dev != NULL && g_strcmp0 (sg_dev_dir, real_dev) == 0);

        if (!matched && target_hbtl != NULL) {
            g_autofree gchar *sg_hbtl = NULL;
            if (puls_scsi_parse_hbtl_address (sg_dev_dir, &sg_hbtl) &&
                g_strcmp0 (sg_hbtl, target_hbtl) == 0)
                matched = TRUE;
        }

        if (matched) {
            gchar *result = g_strdup_printf ("/dev/%s", entry);
            g_dir_close (dir);
            return result;
        }
    }

    g_dir_close (dir);
    return NULL;
}

/**
 * puls_scsi_block_to_generic:
 * @block_device: Block device name or path (e.g. `"/dev/sda"`).
 *
 * Convenience wrapper querying `"/sys"`.
 *
 * Returns: (nullable) (transfer full): Corresponding `/dev/sgX` path or %NULL.
 */
gchar *
puls_scsi_block_to_generic (const gchar *block_device)
{
    return puls_scsi_block_to_generic_from_sysfs ("/sys", block_device);
}

/**
 * puls_scsi_read_generic_info_from_sysfs:
 * @sysfs_root: Sysfs root directory (typically `"/sys"`).
 * @sg_device: SCSI generic device name or path (`"sg0"` or `"/dev/sg0"`).
 * @out_info: Output #PulsScsiGenericInfo struct.
 *
 * Populates SCSI generic to block mapping and SCSI device attributes (`H:B:T:L`,
 * vendor, model, revision, SCSI type) from sysfs.
 *
 * Returns: %TRUE if @sg_device exists under @sysfs_root.
 */
gboolean
puls_scsi_read_generic_info_from_sysfs (const gchar         *sysfs_root,
                                        const gchar         *sg_device,
                                        PulsScsiGenericInfo *out_info)
{
    g_return_val_if_fail (out_info != NULL, FALSE);

    memset (out_info, 0, sizeof (*out_info));
    out_info->scsi_type = -1;

    if (sysfs_root == NULL || !puls_scsi_is_generic_name (sg_device))
        return FALSE;

    const gchar *sg_name = g_str_has_prefix (sg_device, "/dev/") ? (sg_device + 5) : sg_device;
    g_autofree gchar *sg_class_entry = g_build_filename (sysfs_root, "class", "scsi_generic", sg_name, NULL);
    if (!g_file_test (sg_class_entry, G_FILE_TEST_EXISTS))
        return FALSE;

    out_info->sg_name = g_strdup (sg_name);
    out_info->sg_path = g_strdup_printf ("/dev/%s", sg_name);
    out_info->block_path = puls_scsi_generic_to_block_from_sysfs (sysfs_root, sg_name);
    if (out_info->block_path != NULL)
        out_info->block_name = g_path_get_basename (out_info->block_path);

    g_autofree gchar *dev_dir = resolve_sg_device_dir (sysfs_root, sg_name);
    if (dev_dir != NULL) {
        puls_scsi_parse_hbtl_address (dev_dir, &out_info->scsi_address);
        out_info->vendor = read_optional_trimmed_attr (dev_dir, "vendor", 64);
        out_info->model  = read_optional_trimmed_attr (dev_dir, "model", 128);
        out_info->rev    = read_optional_trimmed_attr (dev_dir, "rev", 64);

        g_autofree gchar *type_path = g_build_filename (dev_dir, "type", NULL);
        gboolean ok = FALSE;
        gint64 tval = puls_sysfs_read_int64 (type_path, -1, &ok);
        if (ok && tval >= 0)
            out_info->scsi_type = (gint)tval;
    }

    return TRUE;
}

/**
 * puls_scsi_read_generic_info:
 * @sg_device: SCSI generic device name or path.
 * @out_info: Output #PulsScsiGenericInfo struct.
 *
 * Convenience wrapper reading from `"/sys"`.
 */
gboolean
puls_scsi_read_generic_info (const gchar         *sg_device,
                             PulsScsiGenericInfo *out_info)
{
    return puls_scsi_read_generic_info_from_sysfs ("/sys", sg_device, out_info);
}

static gint
compare_sg_info_by_name (gconstpointer a, gconstpointer b)
{
    const PulsScsiGenericInfo *ia = a;
    const PulsScsiGenericInfo *ib = b;
    return g_strcmp0 (ia->sg_name, ib->sg_name);
}

/**
 * puls_scsi_list_generic_mappings_from_sysfs:
 * @sysfs_root: Sysfs root directory.
 *
 * Enumerates all `sg*` entries under `@sysfs_root/class/scsi_generic`.
 *
 * Returns: (element-type PulsScsiGenericInfo) (transfer full): List of
 * #PulsScsiGenericInfo structs freed via puls_scsi_generic_info_free().
 */
GList *
puls_scsi_list_generic_mappings_from_sysfs (const gchar *sysfs_root)
{
    if (sysfs_root == NULL)
        return NULL;

    g_autofree gchar *class_dir = g_build_filename (sysfs_root, "class", "scsi_generic", NULL);
    GDir *dir = g_dir_open (class_dir, 0, NULL);
    if (dir == NULL)
        return NULL;

    GList *list = NULL;
    const gchar *entry;
    while ((entry = g_dir_read_name (dir)) != NULL) {
        if (!puls_scsi_is_generic_name (entry))
            continue;

        PulsScsiGenericInfo *info = g_new0 (PulsScsiGenericInfo, 1);
        info->scsi_type = -1;
        if (puls_scsi_read_generic_info_from_sysfs (sysfs_root, entry, info)) {
            list = g_list_append (list, info);
        } else {
            puls_scsi_generic_info_free (info);
        }
    }

    g_dir_close (dir);
    list = g_list_sort (list, compare_sg_info_by_name);
    return list;
}

/**
 * puls_scsi_list_generic_mappings:
 *
 * Enumerates all SCSI generic mappings from `"/sys"`.
 *
 * Returns: (element-type PulsScsiGenericInfo) (transfer full): Sorted list of mappings.
 */
GList *
puls_scsi_list_generic_mappings (void)
{
    return puls_scsi_list_generic_mappings_from_sysfs ("/sys");
}

/**
 * puls_raid_member_info_clear:
 * @info: A #PulsRaidMemberInfo struct.
 *
 * Frees owned fields in @info and resets numeric fields to defaults.
 */
void
puls_raid_member_info_clear (PulsRaidMemberInfo *info)
{
    if (info == NULL)
        return;

    g_free (info->array_name);
    g_free (info->array_path);
    g_free (info->level);
    g_free (info->array_state);
    g_free (info->member_device);
    g_free (info->member_state);
    g_free (info->sync_action);
    g_free (info->status_text);
    memset (info, 0, sizeof (*info));
    info->raid_disks = -1;
    info->degraded_count = -1;
}

/**
 * puls_raid_member_info_free:
 * @info: Heap-allocated #PulsRaidMemberInfo.
 *
 * Clears and frees @info.
 */
void
puls_raid_member_info_free (PulsRaidMemberInfo *info)
{
    if (info == NULL)
        return;
    puls_raid_member_info_clear (info);
    g_free (info);
}

/**
 * puls_raid_is_md_device_name:
 * @name: Block device name or path (e.g. `md0`, `md127`, `md_d0`).
 *
 * Returns: %TRUE if @name identifies a Linux software RAID (md) array node.
 */
gboolean
puls_raid_is_md_device_name (const gchar *name)
{
    if (name == NULL || *name == '\0')
        return FALSE;

    const gchar *base = g_str_has_prefix (name, "/dev/") ? (name + 5) : name;
    if (strchr (base, '/') != NULL)
        return FALSE;

    if (!g_str_has_prefix (base, "md"))
        return FALSE;

    const gchar *p = base + 2;
    if (g_str_has_prefix (p, "_d"))
        p += 2;

    if (!g_ascii_isdigit (*p))
        return FALSE;

    while (g_ascii_isdigit (*p))
        p++;

    return (*p == '\0');
}

static gchar *
normalize_raid_level (const gchar *raw_level)
{
    if (raw_level == NULL || *raw_level == '\0')
        return NULL;

    return g_ascii_strup (raw_level, -1);
}

/**
 * puls_raid_format_member_status:
 * @info: Populated #PulsRaidMemberInfo.
 *
 * Formats a concise human-readable RAID membership summary, such as
 * `"Member of /dev/md0 [RAID1]"` or `"Member of /dev/md0 [RAID5, Degraded]"`.
 *
 * Returns: (nullable) (transfer full): Newly allocated status string.
 */
gchar *
puls_raid_format_member_status (const PulsRaidMemberInfo *info)
{
    if (info == NULL || info->array_path == NULL)
        return NULL;

    const gchar *qualifier = NULL;
    if (info->member_state != NULL && strstr (info->member_state, "faulty") != NULL)
        qualifier = "Faulty";
    else if (info->member_state != NULL && strstr (info->member_state, "spare") != NULL)
        qualifier = "Spare";
    else if (info->degraded_count > 0)
        qualifier = "Degraded";
    else if (info->sync_action != NULL &&
             (g_strcmp0 (info->sync_action, "resync") == 0 ||
              g_strcmp0 (info->sync_action, "recover") == 0 ||
              g_strcmp0 (info->sync_action, "reshape") == 0))
        qualifier = "Rebuilding";

    if (info->level != NULL && qualifier != NULL)
        return g_strdup_printf ("Member of %s [%s, %s]", info->array_path, info->level, qualifier);
    if (info->level != NULL)
        return g_strdup_printf ("Member of %s [%s]", info->array_path, info->level);
    if (qualifier != NULL)
        return g_strdup_printf ("Member of %s [%s]", info->array_path, qualifier);

    return g_strdup_printf ("Member of %s", info->array_path);
}

static PulsRaidMemberInfo *
read_md_membership_for_array (const gchar *md_dir,
                              const gchar *md_name,
                              const gchar *disk_name)
{
    g_autofree gchar *slaves_dir = g_build_filename (md_dir, "slaves", NULL);
    GDir *sdir = g_dir_open (slaves_dir, 0, NULL);
    if (sdir == NULL)
        return NULL;

    g_autofree gchar *matched_slave = NULL;
    const gchar *slave;
    while ((slave = g_dir_read_name (sdir)) != NULL) {
        if (device_name_is_partition_of (slave, disk_name)) {
            matched_slave = g_strdup (slave);
            break;
        }
    }
    g_dir_close (sdir);

    if (matched_slave == NULL)
        return NULL;

    PulsRaidMemberInfo *info = g_new0 (PulsRaidMemberInfo, 1);
    info->raid_disks = -1;
    info->degraded_count = -1;
    info->array_name = g_strdup (md_name);
    info->array_path = g_strdup_printf ("/dev/%s", md_name);
    info->member_device = g_steal_pointer (&matched_slave);

    g_autofree gchar *md_sub = g_build_filename (md_dir, "md", NULL);
    if (g_file_test (md_sub, G_FILE_TEST_IS_DIR)) {
        g_autofree gchar *raw_level = read_optional_trimmed_attr (md_sub, "level", 64);
        info->level = normalize_raid_level (raw_level);
        info->array_state = read_optional_trimmed_attr (md_sub, "array_state", 64);
        info->sync_action = read_optional_trimmed_attr (md_sub, "sync_action", 64);

        g_autofree gchar *rd_path = g_build_filename (md_sub, "raid_disks", NULL);
        gboolean rd_ok = FALSE;
        gint64 rd_val = puls_sysfs_read_int64 (rd_path, -1, &rd_ok);
        if (rd_ok && rd_val >= 0)
            info->raid_disks = (gint)rd_val;

        g_autofree gchar *deg_path = g_build_filename (md_sub, "degraded", NULL);
        gboolean deg_ok = FALSE;
        gint64 deg_val = puls_sysfs_read_int64 (deg_path, -1, &deg_ok);
        if (deg_ok && deg_val >= 0)
            info->degraded_count = (gint)deg_val;

        g_autofree gchar *dev_state_sub = g_strdup_printf ("dev-%s", info->member_device);
        g_autofree gchar *dev_state_dir = g_build_filename (md_sub, dev_state_sub, NULL);
        info->member_state = read_optional_trimmed_attr (dev_state_dir, "state", 64);
    }

    info->status_text = puls_raid_format_member_status (info);
    return info;
}

static gint
compare_raid_info_by_array (gconstpointer a, gconstpointer b)
{
    const PulsRaidMemberInfo *ra = a;
    const PulsRaidMemberInfo *rb = b;
    return g_strcmp0 (ra->array_name, rb->array_name);
}

/**
 * puls_raid_list_memberships_from_sysfs:
 * @sys_block_dir: Path following `/sys/block` layout.
 * @device_path: Disk name or path (e.g. `"/dev/sda"` or `"sda"`).
 *
 * Scans `@sys_block_dir/md<N>/slaves/` for arrays containing @device_path or any
 * of its partitions.
 *
 * Returns: (element-type PulsRaidMemberInfo) (transfer full): Sorted list of
 * #PulsRaidMemberInfo structs freed via puls_raid_member_info_free().
 */
GList *
puls_raid_list_memberships_from_sysfs (const gchar *sys_block_dir,
                                       const gchar *device_path)
{
    if (sys_block_dir == NULL || device_path == NULL)
        return NULL;

    g_autofree gchar *disk_name = puls_sanitize_device_name (device_path);
    if (disk_name == NULL || puls_raid_is_md_device_name (disk_name))
        return NULL;

    GDir *dir = g_dir_open (sys_block_dir, 0, NULL);
    if (dir == NULL)
        return NULL;

    GList *results = NULL;
    const gchar *entry;
    while ((entry = g_dir_read_name (dir)) != NULL) {
        if (!puls_raid_is_md_device_name (entry))
            continue;

        g_autofree gchar *md_dir = g_build_filename (sys_block_dir, entry, NULL);
        PulsRaidMemberInfo *info = read_md_membership_for_array (md_dir, entry, disk_name);
        if (info != NULL)
            results = g_list_append (results, info);
    }

    g_dir_close (dir);
    results = g_list_sort (results, compare_raid_info_by_array);
    return results;
}

/**
 * puls_raid_list_memberships:
 * @device_path: Disk name or path (e.g. `"/dev/sda"`).
 *
 * Convenience wrapper scanning `"/sys/block"`.
 */
GList *
puls_raid_list_memberships (const gchar *device_path)
{
    return puls_raid_list_memberships_from_sysfs ("/sys/block", device_path);
}

/**
 * puls_raid_detect_member_from_sysfs:
 * @sys_block_dir: Path following `/sys/block` layout.
 * @device_path: Disk name or path (e.g. `"/dev/sda"`).
 * @out_info: Output #PulsRaidMemberInfo struct.
 *
 * Detects primary Linux software RAID (`mdadm`) membership for @device_path.
 *
 * Returns: %TRUE if @device_path is a member of at least one `md*` array.
 */
gboolean
puls_raid_detect_member_from_sysfs (const gchar        *sys_block_dir,
                                    const gchar        *device_path,
                                    PulsRaidMemberInfo *out_info)
{
    g_return_val_if_fail (out_info != NULL, FALSE);

    memset (out_info, 0, sizeof (*out_info));
    out_info->raid_disks = -1;
    out_info->degraded_count = -1;

    GList *list = puls_raid_list_memberships_from_sysfs (sys_block_dir, device_path);
    if (list == NULL)
        return FALSE;

    PulsRaidMemberInfo *first = list->data;
    out_info->array_name     = g_strdup (first->array_name);
    out_info->array_path     = g_strdup (first->array_path);
    out_info->level          = g_strdup (first->level);
    out_info->array_state    = g_strdup (first->array_state);
    out_info->member_device  = g_strdup (first->member_device);
    out_info->member_state   = g_strdup (first->member_state);
    out_info->sync_action    = g_strdup (first->sync_action);
    out_info->raid_disks     = first->raid_disks;
    out_info->degraded_count = first->degraded_count;
    out_info->status_text    = g_strdup (first->status_text);

    g_list_free_full (list, (GDestroyNotify)puls_raid_member_info_free);
    return TRUE;
}

/**
 * puls_raid_detect_member:
 * @device_path: Disk name or path (e.g. `"/dev/sda"`).
 * @out_info: Output #PulsRaidMemberInfo struct.
 *
 * Convenience wrapper querying `"/sys/block"`.
 */
gboolean
puls_raid_detect_member (const gchar        *device_path,
                         PulsRaidMemberInfo *out_info)
{
    return puls_raid_detect_member_from_sysfs ("/sys/block", device_path, out_info);
}

