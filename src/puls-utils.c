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
    return puls_sysfs_read_device_uint64 (device_name, "queue/rotational", 1) != 0;
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
        g_str_has_prefix (device_name, "dm-"))
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
        g_str_has_prefix (device_name, "dm-"))
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

gchar *
puls_detect_usb_speed_version (const gchar *device_name)
{
    g_autofree gchar *device_link = g_strdup_printf ("/sys/block/%s/device", device_name);
    gchar real_path_buf[PATH_MAX];
    if (realpath (device_link, real_path_buf) == NULL) {
        return NULL;
    }

    gchar *current_dir = g_strdup (real_path_buf);
    gchar *speed_str = NULL;
    gchar *version_str = NULL;

    for (gint i = 0; i < 12; i++) {
        g_autofree gchar *speed_path = g_build_filename (current_dir, "speed", NULL);
        g_autofree gchar *version_path = g_build_filename (current_dir, "version", NULL);

        if (g_file_test (speed_path, G_FILE_TEST_EXISTS) && g_file_test (version_path, G_FILE_TEST_EXISTS)) {
            g_file_get_contents (speed_path, &speed_str, NULL, NULL);
            g_file_get_contents (version_path, &version_str, NULL, NULL);
            break;
        }

        gchar *parent_dir = g_path_get_dirname (current_dir);
        g_free (current_dir);
        current_dir = parent_dir;

        if (g_strcmp0 (current_dir, "/") == 0 || g_strcmp0 (current_dir, "/sys") == 0 || g_strcmp0 (current_dir, ".") == 0) {
            break;
        }
    }

    g_free (current_dir);

    if (speed_str && version_str) {
        g_strstrip (speed_str);
        g_strstrip (version_str);

        gint speed_val = (gint)g_ascii_strtoll (speed_str, NULL, 10);
        const gchar *gen = "USB";

        if (speed_val == 480) {
            gen = "USB 2.0 (High-Speed)";
        } else if (speed_val == 5000) {
            gen = "USB 3.0 (SuperSpeed)";
        } else if (speed_val == 10000) {
            gen = "USB 3.1 (SuperSpeed+)";
        } else if (speed_val == 20000) {
            gen = "USB 3.2 (SuperSpeed++)";
        } else {
            gchar *res = g_strdup_printf ("USB %s (%d Mbps)", version_str, speed_val);
            g_free (speed_str);
            g_free (version_str);
            return res;
        }

        gchar *res = g_strdup_printf ("USB %s | %s", version_str, gen);
        g_free (speed_str);
        g_free (version_str);
        return res;
    }

    g_free (speed_str);
    g_free (version_str);
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
is_dm_slave_of (const gchar *mnt_fsname, const gchar *parent_device_path)
{
    if (mnt_fsname == NULL || parent_device_path == NULL)
        return FALSE;

    const gchar *parent_name = strrchr (parent_device_path, '/');
    parent_name = parent_name ? parent_name + 1 : parent_device_path;

    gchar *resolved = realpath (mnt_fsname, NULL);
    if (resolved == NULL)
        return FALSE;

    gboolean is_slave = FALSE;
    const gchar *dm_name = strrchr (resolved, '/');
    if (dm_name) {
        dm_name = dm_name + 1;
        if (g_str_has_prefix (dm_name, "dm-")) {
            g_autofree gchar *slaves_dir = g_build_filename ("/sys/class/block", dm_name, "slaves", NULL);
            GDir *dir = g_dir_open (slaves_dir, 0, NULL);
            if (dir) {
                const gchar *entry;
                while ((entry = g_dir_read_name (dir)) != NULL) {
                    if (g_str_has_prefix (entry, parent_name)) {
                        is_slave = TRUE;
                        break;
                    }
                }
                g_dir_close (dir);
            }
        }
    }
    free (resolved);
    return is_slave;
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
        } else if (is_dm_slave_of (mnt->mnt_fsname, device_path)) {
            is_part = TRUE;
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

