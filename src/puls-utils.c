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
#include "../helper/puls-helper-protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <json-glib/json-glib.h>

struct _PulsHelperClient {
    GMutex            mutex;
    GPid              pid;
    GOutputStream    *stdin_stream;
    GDataInputStream *stdout_stream;
    gchar            *helper_path;
    PulsElevationMode elevation_mode;
    gboolean          allow_fallback;
    guint             launch_count;
    guint             crash_count;
    guint             reaped_count;
    guint             watchdog_source_id;
};

static PulsHelperClient default_helper_client;
static gsize default_helper_client_init = 0;

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

/**
 * puls_helper_action_is_valid:
 * @action: (nullable): Candidate self-test action string.
 *
 * Strictly validates @action against the privileged helper's whitelisted
 * self-test subcommands (`"short"`, `"long"`, `"abort"`).
 *
 * Returns: %TRUE if @action is permitted, %FALSE otherwise.
 */
gboolean
puls_helper_action_is_valid (const gchar *action)
{
    return puls_helper_is_valid_action (action) != 0;
}

/**
 * puls_helper_format_hello:
 * @version: Protocol major version (e.g. `2`).
 *
 * Formats a helper handshake command (`HELLO v<version>\n`).
 *
 * Returns: (transfer full): Newly allocated command line.
 */
gchar *
puls_helper_format_hello (guint version)
{
    return g_strdup_printf ("%s v%u\n", PULS_HELPER_CMD_HELLO, version);
}

/**
 * puls_helper_format_read_smart:
 * @device_path: Storage device node path.
 * @check_standby: Whether to pass standby check option.
 *
 * Returns: (transfer full): Framed v2 `READ_SMART` command line.
 */
gchar *
puls_helper_format_read_smart (const gchar *device_path,
                               gboolean     check_standby)
{
    g_return_val_if_fail (device_path != NULL, NULL);

    if (check_standby)
        return g_strdup_printf ("%s %s standby\n", PULS_HELPER_CMD_READ_SMART, device_path);

    return g_strdup_printf ("%s %s\n", PULS_HELPER_CMD_READ_SMART, device_path);
}

/**
 * puls_helper_format_action:
 * @device_path: Storage device node path.
 * @action: SMART action (`short`, `long`, `abort`).
 *
 * Returns: (transfer full): Framed v2 `ACTION` command line.
 */
gchar *
puls_helper_format_action (const gchar *device_path,
                           const gchar *action)
{
    g_return_val_if_fail (device_path != NULL, NULL);
    g_return_val_if_fail (action != NULL, NULL);

    return g_strdup_printf ("%s %s %s\n", PULS_HELPER_CMD_ACTION, device_path, action);
}

/**
 * puls_helper_format_stream_scan:
 * @device_path: Storage device node path.
 * @start_offset: Starting byte offset.
 * @length: Number of bytes to probe.
 *
 * Returns: (transfer full): Framed v2 `STREAM_SCAN` command line.
 */
gchar *
puls_helper_format_stream_scan (const gchar *device_path,
                                guint64      start_offset,
                                guint64      length)
{
    g_return_val_if_fail (device_path != NULL, NULL);

    return g_strdup_printf ("%s %s %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n",
                            PULS_HELPER_CMD_STREAM_SCAN, device_path, start_offset, length);
}

/**
 * puls_helper_format_stream_scan_chunked:
 * @device_path: Storage device node path.
 * @start_offset: Starting byte offset.
 * @length: Number of bytes to probe.
 * @chunk_bytes: Streaming telemetry chunk size in bytes (512 .. 1 MiB).
 *
 * Returns: (transfer full): Framed v2 `STREAM_SCAN` command with explicit chunk size.
 */
gchar *
puls_helper_format_stream_scan_chunked (const gchar *device_path,
                                        guint64      start_offset,
                                        guint64      length,
                                        guint64      chunk_bytes)
{
    g_return_val_if_fail (device_path != NULL, NULL);

    return g_strdup_printf ("%s %s %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n",
                            PULS_HELPER_CMD_STREAM_SCAN, device_path,
                            start_offset, length, chunk_bytes);
}

static gboolean
parse_strict_size_token (const gchar *tok, gsize *out_len)
{
    if (tok == NULL || *tok == '\0' || !g_ascii_isdigit (*tok))
        return FALSE;

    errno = 0;
    gchar *end = NULL;
    guint64 val = g_ascii_strtoull (tok, &end, 10);
    if (errno != 0 || end == tok || *end != '\0')
        return FALSE;

    if (val > PULS_HELPER_MAX_PAYLOAD_LEN)
        return FALSE;

    *out_len = (gsize)val;
    return TRUE;
}

/**
 * puls_helper_parse_frame_header:
 * @header_line: Single ASCII response header line (`OK <len>` or `ERR <code> <len>`).
 * @out_ok: (out): Set to %TRUE for `OK`, %FALSE for `ERR`.
 * @out_err_code: (out) (optional): Error code string when `ERR`, else %NULL.
 * @out_payload_len: (out): Exact payload length in bytes (bounded by 4 MiB).
 *
 * Returns: %TRUE if @header_line is a valid Protocol v2 frame header.
 */
gboolean
puls_helper_parse_frame_header (const gchar  *header_line,
                                gboolean     *out_ok,
                                gchar       **out_err_code,
                                gsize        *out_payload_len)
{
    if (out_ok)
        *out_ok = FALSE;
    if (out_err_code)
        *out_err_code = NULL;
    if (out_payload_len)
        *out_payload_len = 0;

    if (header_line == NULL || *header_line == '\0')
        return FALSE;

    g_autofree gchar *trimmed = g_strdup (header_line);
    g_strstrip (trimmed);
    g_auto(GStrv) parts = g_strsplit (trimmed, " ", -1);
    guint n_parts = g_strv_length (parts);

    if (n_parts == 2 && g_strcmp0 (parts[0], PULS_HELPER_RESP_OK) == 0) {
        gsize len = 0;
        if (!parse_strict_size_token (parts[1], &len))
            return FALSE;
        if (out_ok)
            *out_ok = TRUE;
        if (out_payload_len)
            *out_payload_len = len;
        return TRUE;
    }

    if (n_parts == 3 && g_strcmp0 (parts[0], PULS_HELPER_RESP_ERR) == 0 && parts[1][0] != '\0') {
        gsize len = 0;
        if (!parse_strict_size_token (parts[2], &len))
            return FALSE;
        if (out_ok)
            *out_ok = FALSE;
        if (out_err_code)
            *out_err_code = g_strdup (parts[1]);
        if (out_payload_len)
            *out_payload_len = len;
        return TRUE;
    }

    return FALSE;
}

/**
 * puls_helper_exchange_command:
 * @stdin_stream: Helper stdin pipe.
 * @stdout_stream: Helper stdout buffered stream.
 * @command: Full command line ending in `\n`.
 * @out_ok: (out) (optional): %TRUE if helper returned `OK`, %FALSE for `ERR`.
 * @out_err_code: (out) (optional): Helper error code when `ERR`.
 * @out_payload: (out) (optional): Exact NUL-terminated payload bytes.
 * @error: Return location for transport/framing #GError.
 *
 * Sends @command and reads one complete Protocol v2 response frame.
 *
 * Returns: %TRUE on successful transport & valid frame parsing (inspect @out_ok
 * to distinguish `OK` from `ERR`), %FALSE on transport or framing failure.
 */
gboolean
puls_helper_exchange_command (GOutputStream     *stdin_stream,
                              GDataInputStream  *stdout_stream,
                              const gchar       *command,
                              gboolean          *out_ok,
                              gchar            **out_err_code,
                              gchar            **out_payload,
                              GError           **error)
{
    g_return_val_if_fail (G_IS_OUTPUT_STREAM (stdin_stream), FALSE);
    g_return_val_if_fail (G_IS_DATA_INPUT_STREAM (stdout_stream), FALSE);
    g_return_val_if_fail (command != NULL, FALSE);

    if (out_ok)
        *out_ok = FALSE;
    if (out_err_code)
        *out_err_code = NULL;
    if (out_payload)
        *out_payload = NULL;

    gsize bytes_written = 0;
    if (!g_output_stream_write_all (stdin_stream, command, strlen (command),
                                    &bytes_written, NULL, error))
        return FALSE;

    g_autofree gchar *header = g_data_input_stream_read_line (stdout_stream, NULL, NULL, error);
    if (header == NULL) {
        if (error && *error == NULL)
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Helper EOF or disconnected");
        return FALSE;
    }

    gboolean resp_ok = FALSE;
    g_autofree gchar *err_code = NULL;
    gsize payload_len = 0;

    if (!puls_helper_parse_frame_header (header, &resp_ok, &err_code, &payload_len)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Invalid helper protocol frame header: %s", header);
        return FALSE;
    }

    gchar *buf = g_malloc (payload_len + 1);
    if (payload_len > 0) {
        gsize bytes_read = 0;
        if (!g_input_stream_read_all (G_INPUT_STREAM (stdout_stream), buf, payload_len,
                                      &bytes_read, NULL, error) ||
            bytes_read != payload_len) {
            g_free (buf);
            if (error && *error == NULL)
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Short read from helper");
            return FALSE;
        }
    }
    buf[payload_len] = '\0';

    if (out_ok)
        *out_ok = resp_ok;
    if (out_err_code)
        *out_err_code = g_steal_pointer (&err_code);
    if (out_payload)
        *out_payload = buf;
    else
        g_free (buf);

    return TRUE;
}

/**
 * puls_helper_parse_chunk_header:
 * @header_line: Single ASCII streaming chunk header (`CHUNK <seq> <len>`).
 * @out_seq: (out) (optional): Monotonic chunk sequence number (>= 1).
 * @out_payload_len: (out) (optional): Exact chunk JSON payload length in bytes.
 *
 * Returns: %TRUE if @header_line is a valid Protocol v2 `CHUNK` header.
 */
gboolean
puls_helper_parse_chunk_header (const gchar *header_line,
                                guint64     *out_seq,
                                gsize       *out_payload_len)
{
    if (out_seq)
        *out_seq = 0;
    if (out_payload_len)
        *out_payload_len = 0;

    if (header_line == NULL || *header_line == '\0')
        return FALSE;

    g_autofree gchar *trimmed = g_strdup (header_line);
    g_strstrip (trimmed);
    g_auto(GStrv) parts = g_strsplit (trimmed, " ", -1);
    if (g_strv_length (parts) != 3 || g_strcmp0 (parts[0], PULS_HELPER_RESP_CHUNK) != 0)
        return FALSE;

    if (parts[1][0] == '\0' || !g_ascii_isdigit (parts[1][0]))
        return FALSE;

    errno = 0;
    gchar *seq_end = NULL;
    guint64 seq = g_ascii_strtoull (parts[1], &seq_end, 10);
    if (errno != 0 || seq_end == parts[1] || *seq_end != '\0' || seq == 0)
        return FALSE;

    gsize len = 0;
    if (!parse_strict_size_token (parts[2], &len))
        return FALSE;

    if (out_seq)
        *out_seq = seq;
    if (out_payload_len)
        *out_payload_len = len;

    return TRUE;
}

/**
 * puls_helper_parse_stream_chunk_json:
 * @json_payload: UTF-8 JSON object payload from a `CHUNK` frame.
 * @out_chunk: (out): Populated #PulsHelperStreamChunk telemetry metrics.
 *
 * Returns: %TRUE if @json_payload is a valid telemetry JSON object.
 */
gboolean
puls_helper_parse_stream_chunk_json (const gchar           *json_payload,
                                     PulsHelperStreamChunk *out_chunk)
{
    g_return_val_if_fail (out_chunk != NULL, FALSE);
    memset (out_chunk, 0, sizeof (*out_chunk));

    if (json_payload == NULL || *json_payload == '\0')
        return FALSE;

    g_autoptr(JsonParser) parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json_payload, -1, NULL))
        return FALSE;

    JsonNode *root = json_parser_get_root (parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
        return FALSE;

    JsonObject *obj = json_node_get_object (root);
    if (!json_object_has_member (obj, "seq") ||
        !json_object_has_member (obj, "bytes_read") ||
        !json_object_has_member (obj, "total_bytes"))
        return FALSE;

    out_chunk->seq          = (guint64)MAX (0, json_object_get_int_member (obj, "seq"));
    out_chunk->offset       = json_object_has_member (obj, "offset")
                              ? (guint64)MAX (0, json_object_get_int_member (obj, "offset")) : 0;
    out_chunk->chunk_bytes  = json_object_has_member (obj, "chunk_bytes")
                              ? (guint64)MAX (0, json_object_get_int_member (obj, "chunk_bytes")) : 0;
    out_chunk->bytes_read   = (guint64)MAX (0, json_object_get_int_member (obj, "bytes_read"));
    out_chunk->total_bytes  = (guint64)MAX (0, json_object_get_int_member (obj, "total_bytes"));
    out_chunk->progress_pct = json_object_has_member (obj, "progress")
                              ? json_object_get_double_member (obj, "progress") : 0.0;
    out_chunk->chunk_ms     = json_object_has_member (obj, "chunk_ms")
                              ? json_object_get_double_member (obj, "chunk_ms") : 0.0;
    out_chunk->mb_per_sec   = json_object_has_member (obj, "mb_per_sec")
                              ? json_object_get_double_member (obj, "mb_per_sec") : 0.0;

    return (out_chunk->seq > 0);
}

/**
 * puls_helper_exchange_stream:
 * @stdin_stream: Helper stdin pipe.
 * @stdout_stream: Helper stdout buffered stream.
 * @command: Full streaming command line ending in `\n`.
 * @chunk_cb: (nullable) (scope call): Callback invoked for each `CHUNK` telemetry
 *   frame. Return %TRUE to keep streaming or %FALSE to send a bidirectional
 *   `CANCEL\n` control frame immediately.
 * @user_data: User context for @chunk_cb.
 * @out_ok: (out) (optional): %TRUE if terminal frame was `OK`, %FALSE for `ERR`.
 * @out_err_code: (out) (optional): Terminal error token when `ERR`.
 * @out_final_payload: (out) (optional): Terminal summary/error payload bytes.
 * @error: (out) (optional): Return location for transport/framing #GError.
 *
 * Sends @command and consumes 0 or more intermediate `CHUNK <seq> <len>` frames
 * until the terminal `OK` or `ERR` frame completes the stream.
 *
 * Returns: %TRUE on valid framed stream completion (check @out_ok / @out_err_code).
 */
gboolean
puls_helper_exchange_stream (GOutputStream            *stdin_stream,
                             GDataInputStream         *stdout_stream,
                             const gchar              *command,
                             PulsHelperStreamCallback  chunk_cb,
                             gpointer                  user_data,
                             gboolean                 *out_ok,
                             gchar                   **out_err_code,
                             gchar                   **out_final_payload,
                             GError                  **error)
{
    g_return_val_if_fail (G_IS_OUTPUT_STREAM (stdin_stream), FALSE);
    g_return_val_if_fail (G_IS_DATA_INPUT_STREAM (stdout_stream), FALSE);
    g_return_val_if_fail (command != NULL, FALSE);

    if (out_ok)
        *out_ok = FALSE;
    if (out_err_code)
        *out_err_code = NULL;
    if (out_final_payload)
        *out_final_payload = NULL;

    gsize bytes_written = 0;
    if (!g_output_stream_write_all (stdin_stream, command, strlen (command),
                                    &bytes_written, NULL, error))
        return FALSE;

    gboolean cancel_sent = FALSE;

    for (;;) {
        g_autofree gchar *header = g_data_input_stream_read_line (stdout_stream, NULL, NULL, error);
        if (header == NULL) {
            if (error && *error == NULL)
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Helper EOF or disconnected during stream");
            return FALSE;
        }

        guint64 chunk_seq = 0;
        gsize payload_len = 0;

        if (puls_helper_parse_chunk_header (header, &chunk_seq, &payload_len)) {
            g_autofree gchar *chunk_buf = g_malloc (payload_len + 1);
            if (payload_len > 0) {
                gsize got = 0;
                if (!g_input_stream_read_all (G_INPUT_STREAM (stdout_stream),
                                              chunk_buf, payload_len,
                                              &got, NULL, error) ||
                    got != payload_len) {
                    if (error && *error == NULL)
                        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                     "Short read on helper CHUNK payload");
                    return FALSE;
                }
            }
            chunk_buf[payload_len] = '\0';

            if (!cancel_sent && chunk_cb != NULL) {
                gboolean keep_going = chunk_cb (chunk_seq, chunk_buf, user_data);
                if (!keep_going) {
                    static const gchar cancel_cmd[] = PULS_HELPER_CMD_CANCEL "\n";
                    gsize cw = 0;
                    (void)g_output_stream_write_all (stdin_stream, cancel_cmd,
                                                     sizeof (cancel_cmd) - 1,
                                                     &cw, NULL, NULL);
                    cancel_sent = TRUE;
                }
            }
            continue;
        }

        gboolean resp_ok = FALSE;
        g_autofree gchar *err_code = NULL;

        if (!puls_helper_parse_frame_header (header, &resp_ok, &err_code, &payload_len)) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Invalid helper stream frame header: %s", header);
            return FALSE;
        }

        gchar *final_buf = g_malloc (payload_len + 1);
        if (payload_len > 0) {
            gsize got = 0;
            if (!g_input_stream_read_all (G_INPUT_STREAM (stdout_stream),
                                          final_buf, payload_len,
                                          &got, NULL, error) ||
                got != payload_len) {
                g_free (final_buf);
                if (error && *error == NULL)
                    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "Short read on helper terminal stream payload");
                return FALSE;
            }
        }
        final_buf[payload_len] = '\0';

        if (out_ok)
            *out_ok = resp_ok;
        if (out_err_code)
            *out_err_code = g_steal_pointer (&err_code);
        if (out_final_payload)
            *out_final_payload = final_buf;
        else
            g_free (final_buf);

        return TRUE;
    }
}

static void
ignore_sigpipe_once (void)
{
    static gsize sigpipe_ignored = 0;
    if (g_once_init_enter (&sigpipe_ignored)) {
        signal (SIGPIPE, SIG_IGN);
        g_once_init_leave (&sigpipe_ignored, 1);
    }
}

static void
close_helper_streams_locked (PulsHelperClient *client)
{
    if (client->stdin_stream != NULL) {
        g_output_stream_close (client->stdin_stream, NULL, NULL);
        g_clear_object (&client->stdin_stream);
    }
    if (client->stdout_stream != NULL) {
        g_input_stream_close (G_INPUT_STREAM (client->stdout_stream), NULL, NULL);
        g_clear_object (&client->stdout_stream);
    }
}

static pid_t
waitpid_eintr (pid_t pid, int *status, int options)
{
    pid_t res;
    do {
        res = waitpid (pid, status, options);
    } while (res == -1 && errno == EINTR);
    return res;
}

static void
log_reaped_helper_exit (pid_t pid, int status, gboolean unexpected)
{
    if (WIFEXITED (status)) {
        if (unexpected) {
            PULS_LOG_WARN ("Privileged helper (pid=%d) exited unexpectedly with status %d",
                           (int)pid, WEXITSTATUS (status));
        } else {
            PULS_LOG_DEBUG ("Privileged helper (pid=%d) reaped cleanly (status=%d)",
                            (int)pid, WEXITSTATUS (status));
        }
    } else if (WIFSIGNALED (status)) {
        if (unexpected) {
            PULS_LOG_WARN ("Privileged helper (pid=%d) terminated by signal %d; scheduling recovery",
                           (int)pid, WTERMSIG (status));
        } else {
            PULS_LOG_DEBUG ("Privileged helper (pid=%d) reaped after signal %d",
                            (int)pid, WTERMSIG (status));
        }
    }
}

/**
 * check_and_reap_if_exited_locked:
 *
 * Non-blocking watchdog probe using `waitpid(pid, &status, WNOHANG)`.
 * If the child exited or was killed, reaps the zombie immediately and resets
 * stream/PID state.
 *
 * Returns: %TRUE if helper process is currently alive, %FALSE otherwise.
 */
static gboolean
check_and_reap_if_exited_locked (PulsHelperClient *client, gboolean record_as_crash)
{
    if (client->pid <= 0)
        return FALSE;

    int status = 0;
    pid_t target = (pid_t)client->pid;
    pid_t res = waitpid_eintr (target, &status, WNOHANG);

    if (res == 0)
        return TRUE; /* Still alive */

    if (res == target) {
        log_reaped_helper_exit (target, status, record_as_crash);
        client->reaped_count++;
        if (record_as_crash)
            client->crash_count++;
    } else if (res == -1 && errno == ECHILD) {
        if (record_as_crash)
            client->crash_count++;
        PULS_LOG_WARN ("Privileged helper (pid=%d) already gone (ECHILD)", (int)target);
    }

    g_spawn_close_pid (client->pid);
    client->pid = 0;
    close_helper_streams_locked (client);
    return FALSE;
}

/**
 * terminate_and_reap_helper_locked:
 *
 * Ensures the helper child process is terminated and strictly reaped via
 * `waitpid()` so no zombie child process can ever linger.
 */
static void
terminate_and_reap_helper_locked (PulsHelperClient *client,
                                  gboolean          graceful,
                                  gboolean          record_as_crash)
{
    if (client->pid <= 0) {
        close_helper_streams_locked (client);
        return;
    }

    if (graceful && client->stdin_stream != NULL) {
        static const gchar quit_cmd[] = PULS_HELPER_CMD_QUIT "\n";
        gsize bytes_written = 0;
        (void)g_output_stream_write_all (client->stdin_stream, quit_cmd,
                                         sizeof (quit_cmd) - 1,
                                         &bytes_written, NULL, NULL);
    }

    /* Closing stdin delivers EOF to the helper loop */
    close_helper_streams_locked (client);

    pid_t target = (pid_t)client->pid;
    int status = 0;
    pid_t res = waitpid_eintr (target, &status, WNOHANG);

    /* Wait briefly for clean EOF/QUIT exit before escalating to signals */
    for (guint i = 0; res == 0 && i < 20; i++) {
        g_usleep (5000); /* 5 ms */
        res = waitpid_eintr (target, &status, WNOHANG);
    }

    if (res == 0) {
        (void)kill (target, SIGTERM);
        for (guint i = 0; res == 0 && i < 20; i++) {
            g_usleep (5000);
            res = waitpid_eintr (target, &status, WNOHANG);
        }
    }

    if (res == 0) {
        (void)kill (target, SIGKILL);
        res = waitpid_eintr (target, &status, 0);
    }

    if (res == target) {
        log_reaped_helper_exit (target, status, record_as_crash);
        client->reaped_count++;
    }

    if (record_as_crash)
        client->crash_count++;

    g_spawn_close_pid (client->pid);
    client->pid = 0;
}

/* =========================================================================
 * Privilege Elevation & Sudo / Storage Group Fallback Mechanisms
 * ========================================================================= */

/**
 * puls_elevation_mode_to_string:
 * @mode: A #PulsElevationMode.
 *
 * Returns: Human-readable label for logging and diagnostics.
 */
const gchar *
puls_elevation_mode_to_string (PulsElevationMode mode)
{
    switch (mode) {
    case PULS_ELEVATION_DIRECT_ROOT:
        return "direct-root";
    case PULS_ELEVATION_DIRECT_GROUP:
        return "direct-group";
    case PULS_ELEVATION_PKEXEC:
        return "pkexec";
    case PULS_ELEVATION_SUDO:
        return "sudo";
    case PULS_ELEVATION_NONE:
    default:
        return "none";
    }
}

/**
 * puls_group_list_has_storage_access:
 * @group_names: (nullable): NULL-terminated array of group names.
 *
 * Checks whether any group in @group_names grants raw block device access
 * (`"disk"` on Debian/Ubuntu/Fedora/RHEL or `"storage"` on Arch/derivatives).
 *
 * Returns: %TRUE if `"disk"` or `"storage"` is present.
 */
gboolean
puls_group_list_has_storage_access (const gchar * const *group_names)
{
    if (group_names == NULL)
        return FALSE;

    for (gsize i = 0; group_names[i] != NULL; i++) {
        if (g_strcmp0 (group_names[i], "disk") == 0 ||
            g_strcmp0 (group_names[i], "storage") == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
parse_group_gid_from_text (const gchar *etc_group_text,
                           const gchar *group_name,
                           gid_t       *out_gid)
{
    if (etc_group_text == NULL || group_name == NULL || *group_name == '\0')
        return FALSE;

    gsize name_len = strlen (group_name);
    const gchar *line = etc_group_text;

    while (*line != '\0') {
        const gchar *nl = strchr (line, '\n');
        gsize line_len = nl ? (gsize)(nl - line) : strlen (line);

        if (line_len > name_len + 2 &&
            strncmp (line, group_name, name_len) == 0 &&
            line[name_len] == ':') {
            /* Format: name:passwd:gid:members */
            const gchar *p = line + name_len + 1;
            const gchar *end_line = line + line_len;
            const gchar *second_colon = memchr (p, ':', (size_t)(end_line - p));
            if (second_colon != NULL && second_colon + 1 < end_line) {
                const gchar *gid_start = second_colon + 1;
                const gchar *third_colon = memchr (gid_start, ':', (size_t)(end_line - gid_start));
                gsize gid_len = third_colon ? (gsize)(third_colon - gid_start)
                                            : (gsize)(end_line - gid_start);
                if (gid_len > 0 && gid_len < 16) {
                    gchar gid_buf[16];
                    memcpy (gid_buf, gid_start, gid_len);
                    gid_buf[gid_len] = '\0';
                    if (g_ascii_isdigit (gid_buf[0])) {
                        errno = 0;
                        gchar *endptr = NULL;
                        guint64 parsed = g_ascii_strtoull (gid_buf, &endptr, 10);
                        if (errno == 0 && endptr != gid_buf && *endptr == '\0' &&
                            parsed <= G_MAXUINT32) {
                            *out_gid = (gid_t)parsed;
                            return TRUE;
                        }
                    }
                }
            }
        }

        if (nl == NULL)
            break;
        line = nl + 1;
    }

    return FALSE;
}

/**
 * puls_user_in_storage_group:
 *
 * Checks whether the effective user belongs to the local `"disk"` or
 * `"storage"` primary or supplementary UNIX group (resolved directly from
 * `/etc/group` without blocking on network NSS backends).
 *
 * Returns: %TRUE if the current user has storage-group membership.
 */
gboolean
puls_user_in_storage_group (void)
{
    g_autofree gchar *etc_group = NULL;
    if (!g_file_get_contents ("/etc/group", &etc_group, NULL, NULL) || etc_group == NULL)
        return FALSE;

    gid_t target_gids[2];
    gsize n_targets = 0;

    gid_t gid = 0;
    if (parse_group_gid_from_text (etc_group, "disk", &gid))
        target_gids[n_targets++] = gid;
    if (parse_group_gid_from_text (etc_group, "storage", &gid))
        target_gids[n_targets++] = gid;

    if (n_targets == 0)
        return FALSE;

    gid_t egid = getegid ();
    for (gsize t = 0; t < n_targets; t++) {
        if (egid == target_gids[t])
            return TRUE;
    }

    int n_groups = getgroups (0, NULL);
    if (n_groups <= 0)
        return FALSE;

    g_autofree gid_t *groups = g_new0 (gid_t, (gsize)n_groups);
    int got = getgroups (n_groups, groups);
    if (got <= 0)
        return FALSE;

    for (int i = 0; i < got; i++) {
        for (gsize t = 0; t < n_targets; t++) {
            if (groups[i] == target_gids[t])
                return TRUE;
        }
    }

    return FALSE;
}

static gboolean
binary_is_setuid_root (const gchar * const *candidates)
{
    for (gsize i = 0; candidates[i] != NULL; i++) {
        struct stat st;
        if (stat (candidates[i], &st) == 0 &&
            S_ISREG (st.st_mode) &&
            st.st_uid == 0 &&
            (st.st_mode & S_ISUID) != 0 &&
            access (candidates[i], X_OK) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
detect_is_wsl (void)
{
    g_autofree gchar *release = NULL;
    if (!g_file_get_contents ("/proc/sys/kernel/osrelease", &release, NULL, NULL) ||
        release == NULL)
        return FALSE;

    g_autofree gchar *lower = g_ascii_strdown (release, -1);
    return (strstr (lower, "microsoft") != NULL || strstr (lower, "wsl") != NULL);
}

static gboolean
detect_is_container (void)
{
    if (g_file_test ("/.dockerenv", G_FILE_TEST_EXISTS) ||
        g_file_test ("/run/.containerenv", G_FILE_TEST_EXISTS))
        return TRUE;

    const gchar *container_env = g_getenv ("container");
    if (container_env != NULL && *container_env != '\0')
        return TRUE;

    return FALSE;
}

/**
 * puls_is_polkit_available:
 *
 * Determines whether Polkit (`pkexec`) is usable in the current environment:
 * requires a setuid-root `pkexec` binary, an active system D-Bus socket, and
 * a non-WSL desktop/session context where a Polkit agent can respond.
 *
 * Returns: %TRUE if `pkexec` is expected to function, %FALSE otherwise.
 */
gboolean
puls_is_polkit_available (void)
{
    static const gchar * const pkexec_paths[] = {
        "/usr/bin/pkexec",
        "/bin/pkexec",
        NULL
    };

    if (!binary_is_setuid_root (pkexec_paths))
        return FALSE;

    struct stat bus_st;
    gboolean has_system_bus =
        (stat ("/run/dbus/system_bus_socket", &bus_st) == 0 && S_ISSOCK (bus_st.st_mode)) ||
        (stat ("/var/run/dbus/system_bus_socket", &bus_st) == 0 && S_ISSOCK (bus_st.st_mode));
    if (!has_system_bus)
        return FALSE;

    if (detect_is_wsl ())
        return FALSE;

    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");
    const gchar *xdg_session = g_getenv ("XDG_SESSION_ID");

    if ((display == NULL || *display == '\0') &&
        (wayland == NULL || *wayland == '\0') &&
        (xdg_session == NULL || *xdg_session == '\0')) {
        return FALSE;
    }

    return TRUE;
}

/**
 * puls_is_sudo_available:
 *
 * Checks whether a setuid-root `sudo` binary is installed and executable.
 *
 * Returns: %TRUE if `sudo` is available for fallback execution.
 */
gboolean
puls_is_sudo_available (void)
{
    static const gchar * const sudo_paths[] = {
        "/usr/bin/sudo",
        "/bin/sudo",
        NULL
    };

    return binary_is_setuid_root (sudo_paths);
}

/**
 * puls_detect_elevation_caps:
 * @out_caps: (out): Populated #PulsElevationCaps describing the runtime environment.
 */
void
puls_detect_elevation_caps (PulsElevationCaps *out_caps)
{
    g_return_if_fail (out_caps != NULL);

    memset (out_caps, 0, sizeof (*out_caps));
    out_caps->is_root          = (geteuid () == 0);
    out_caps->in_storage_group = puls_user_in_storage_group ();
    out_caps->is_wsl           = detect_is_wsl ();
    out_caps->is_container     = detect_is_container ();
    out_caps->polkit_available = puls_is_polkit_available ();
    out_caps->sudo_available   = puls_is_sudo_available ();
}

/**
 * puls_resolve_elevation_mode:
 * @caps: Environment capability snapshot.
 *
 * Deterministic priority resolver for privilege elevation:
 * 1. Root (`PULS_ELEVATION_DIRECT_ROOT`)
 * 2. `disk`/`storage` group membership (`PULS_ELEVATION_DIRECT_GROUP`)
 * 3. Polkit `pkexec` (`PULS_ELEVATION_PKEXEC`)
 * 4. Non-interactive `sudo -n` fallback (`PULS_ELEVATION_SUDO`)
 * 5. Unprivileged direct attempt (`PULS_ELEVATION_NONE`)
 *
 * Returns: Selected #PulsElevationMode.
 */
PulsElevationMode
puls_resolve_elevation_mode (const PulsElevationCaps *caps)
{
    g_return_val_if_fail (caps != NULL, PULS_ELEVATION_NONE);

    if (caps->is_root)
        return PULS_ELEVATION_DIRECT_ROOT;

    if (caps->in_storage_group)
        return PULS_ELEVATION_DIRECT_GROUP;

    if (caps->polkit_available && !caps->is_wsl)
        return PULS_ELEVATION_PKEXEC;

    if (caps->sudo_available)
        return PULS_ELEVATION_SUDO;

    return PULS_ELEVATION_NONE;
}

/**
 * puls_get_elevation_mode:
 *
 * Probes the live host environment and returns the optimal #PulsElevationMode.
 */
PulsElevationMode
puls_get_elevation_mode (void)
{
    PulsElevationCaps caps;
    puls_detect_elevation_caps (&caps);
    return puls_resolve_elevation_mode (&caps);
}

/**
 * puls_helper_build_spawn_argv:
 * @mode: Selected #PulsElevationMode.
 * @helper_bin: Absolute path to `puls-diskinfo-helper`.
 *
 * Builds the `NULL`-terminated `argv` vector for spawning the helper under
 * @mode (`pkexec <bin>`, `sudo -n <bin>`, or direct `<bin>`).
 *
 * Returns: (transfer full): Newly allocated `GStrv` (free with g_strfreev()).
 */
gchar **
puls_helper_build_spawn_argv (PulsElevationMode mode, const gchar *helper_bin)
{
    g_return_val_if_fail (helper_bin != NULL && *helper_bin != '\0', NULL);

    gchar **argv = NULL;

    switch (mode) {
    case PULS_ELEVATION_PKEXEC:
        argv = g_new0 (gchar *, 3);
        argv[0] = g_strdup ("pkexec");
        argv[1] = g_strdup (helper_bin);
        break;
    case PULS_ELEVATION_SUDO:
        argv = g_new0 (gchar *, 4);
        argv[0] = g_strdup ("sudo");
        argv[1] = g_strdup ("-n");
        argv[2] = g_strdup (helper_bin);
        break;
    case PULS_ELEVATION_DIRECT_ROOT:
    case PULS_ELEVATION_DIRECT_GROUP:
    case PULS_ELEVATION_NONE:
    default:
        argv = g_new0 (gchar *, 2);
        argv[0] = g_strdup (helper_bin);
        break;
    }

    return argv;
}

static gboolean
spawn_helper_with_mode_locked (PulsHelperClient  *client,
                               const gchar       *bin,
                               PulsElevationMode  mode,
                               GError           **error)
{
    g_auto(GStrv) argv = puls_helper_build_spawn_argv (mode, bin);
    if (argv == NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Failed to build helper spawn argv");
        return FALSE;
    }

    GPid child_pid = 0;
    gint stdin_fd = -1;
    gint stdout_fd = -1;

    GSpawnFlags flags = G_SPAWN_SEARCH_PATH |
                        G_SPAWN_DO_NOT_REAP_CHILD |
                        G_SPAWN_CLOEXEC_PIPES;

    if (!g_spawn_async_with_pipes (NULL, argv, NULL, flags,
                                   NULL, NULL, &child_pid,
                                   &stdin_fd, &stdout_fd, NULL,
                                   error)) {
        return FALSE;
    }

    client->pid = child_pid;
    client->stdin_stream = g_unix_output_stream_new (stdin_fd, TRUE);
    GInputStream *raw_stdout = g_unix_input_stream_new (stdout_fd, TRUE);
    client->stdout_stream = g_data_input_stream_new (raw_stdout);
    g_object_unref (raw_stdout);

    /* Perform strict Protocol v2 version handshake */
    g_autofree gchar *hello_cmd = puls_helper_format_hello (PULS_HELPER_PROTO_VERSION);
    gboolean hello_ok = FALSE;
    g_autofree gchar *hello_err = NULL;
    g_autofree gchar *hello_payload = NULL;

    if (!puls_helper_exchange_command (client->stdin_stream, client->stdout_stream,
                                       hello_cmd, &hello_ok, &hello_err, &hello_payload,
                                       error) ||
        !hello_ok) {
        if (error && *error == NULL) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                         "Helper protocol handshake failed (%s): %s",
                         hello_err ? hello_err : "unknown",
                         hello_payload ? hello_payload : "");
        }
        if (hello_err != NULL) {
            PULS_LOG_WARN ("Helper Protocol %s handshake failed (%s)",
                           PULS_HELPER_PROTO_TAG, hello_err);
        } else {
            PULS_LOG_DEBUG ("Helper Protocol %s handshake unavailable via %s (transport closed)",
                            PULS_HELPER_PROTO_TAG, puls_elevation_mode_to_string (mode));
        }
        terminate_and_reap_helper_locked (client, FALSE, FALSE);
        return FALSE;
    }

    client->elevation_mode = mode;
    client->launch_count++;
    PULS_LOG_DEBUG ("Helper Protocol %s handshake succeeded via %s (pid=%d, launches=%u)",
                    PULS_HELPER_PROTO_TAG, puls_elevation_mode_to_string (mode),
                    (int)client->pid, client->launch_count);
    return TRUE;
}

static gboolean
spawn_helper_locked (PulsHelperClient *client, GError **error)
{
    ignore_sigpipe_once ();

    g_autofree gchar *resolved_path = NULL;
    const gchar *bin = client->helper_path;
    if (bin == NULL || *bin == '\0') {
        resolved_path = puls_get_helper_path ();
        bin = resolved_path;
    }

    if (bin == NULL || *bin == '\0') {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Helper executable not found");
        return FALSE;
    }

    GError *primary_err = NULL;
    if (spawn_helper_with_mode_locked (client, bin, client->elevation_mode, &primary_err))
        return TRUE;

    /* Automatic runtime fallback when Polkit is unavailable or declined/headless */
    if (client->allow_fallback && client->elevation_mode == PULS_ELEVATION_PKEXEC) {
        if (puls_is_sudo_available ()) {
            PULS_LOG_INFO ("Polkit elevation failed (%s); falling back to sudo -n",
                           primary_err ? primary_err->message : "unavailable");
            GError *sudo_err = NULL;
            if (spawn_helper_with_mode_locked (client, bin, PULS_ELEVATION_SUDO, &sudo_err)) {
                g_clear_error (&primary_err);
                return TRUE;
            }
            g_clear_error (&sudo_err);
        }

        if (puls_user_in_storage_group ()) {
            PULS_LOG_INFO ("Falling back to direct storage-group helper execution");
            GError *grp_err = NULL;
            if (spawn_helper_with_mode_locked (client, bin, PULS_ELEVATION_DIRECT_GROUP, &grp_err)) {
                g_clear_error (&primary_err);
                return TRUE;
            }
            g_clear_error (&grp_err);
        }
    }

    g_propagate_error (error, primary_err);
    return FALSE;
}

static gboolean
ensure_helper_running_locked (PulsHelperClient *client, GError **error)
{
    if (check_and_reap_if_exited_locked (client, TRUE))
        return TRUE;

    return spawn_helper_locked (client, error);
}

/**
 * puls_helper_client_new_with_mode:
 * @helper_path: (nullable): Explicit path to helper binary, or %NULL to auto-discover.
 * @mode: Explicit #PulsElevationMode to use when spawning the helper.
 *
 * Creates an isolated helper IPC client configured with @mode, built-in crash
 * watchdog, and `waitpid()` zombie reaping.
 *
 * Returns: (transfer full): A new #PulsHelperClient.
 */
PulsHelperClient *
puls_helper_client_new_with_mode (const gchar       *helper_path,
                                  PulsElevationMode  mode)
{
    PulsHelperClient *client = g_new0 (PulsHelperClient, 1);
    g_mutex_init (&client->mutex);
    client->helper_path = g_strdup (helper_path);
    client->elevation_mode = mode;
    client->allow_fallback = FALSE;
    return client;
}

/**
 * puls_helper_client_new:
 * @helper_path: (nullable): Explicit path to helper binary, or %NULL to auto-discover.
 * @use_pkexec: Whether to elevate via `pkexec` when spawning the helper.
 *
 * Creates an isolated helper IPC client with built-in crash watchdog and
 * `waitpid()` zombie reaping.
 *
 * Returns: (transfer full): A new #PulsHelperClient.
 */
PulsHelperClient *
puls_helper_client_new (const gchar *helper_path, gboolean use_pkexec)
{
    return puls_helper_client_new_with_mode (
        helper_path,
        use_pkexec ? PULS_ELEVATION_PKEXEC : PULS_ELEVATION_DIRECT_ROOT);
}

/**
 * puls_helper_client_watchdog_stop:
 * @client: A #PulsHelperClient.
 *
 * Stops the periodic GLib watchdog timer if active.
 */
void
puls_helper_client_watchdog_stop (PulsHelperClient *client)
{
    g_return_if_fail (client != NULL);

    g_mutex_lock (&client->mutex);
    guint src_id = client->watchdog_source_id;
    client->watchdog_source_id = 0;
    g_mutex_unlock (&client->mutex);

    if (src_id != 0)
        g_source_remove (src_id);
}

/**
 * puls_helper_client_shutdown:
 * @client: A #PulsHelperClient.
 *
 * Stops the watchdog timer, sends `QUIT` to any running helper process, closes
 * IPC pipes, and reaps the child via `waitpid()`.
 */
void
puls_helper_client_shutdown (PulsHelperClient *client)
{
    g_return_if_fail (client != NULL);

    puls_helper_client_watchdog_stop (client);

    g_mutex_lock (&client->mutex);
    terminate_and_reap_helper_locked (client, TRUE, FALSE);
    g_mutex_unlock (&client->mutex);
}

/**
 * puls_helper_client_free:
 * @client: (nullable): A #PulsHelperClient to destroy.
 *
 * Shuts down and reaps any running helper child, then frees @client.
 */
void
puls_helper_client_free (PulsHelperClient *client)
{
    if (client == NULL)
        return;

    puls_helper_client_shutdown (client);
    g_free (client->helper_path);
    g_mutex_clear (&client->mutex);
    g_free (client);
}

/**
 * puls_helper_client_ensure_running:
 * @client: A #PulsHelperClient.
 * @error: (out) (optional): Return location for a #GError.
 *
 * Verifies the helper child is alive (reaping any zombie if terminated) and
 * spawns + handshakes a fresh instance if not running.
 *
 * Returns: %TRUE if a live, handshaked helper process is ready.
 */
gboolean
puls_helper_client_ensure_running (PulsHelperClient *client, GError **error)
{
    g_return_val_if_fail (client != NULL, FALSE);

    g_mutex_lock (&client->mutex);
    gboolean ok = ensure_helper_running_locked (client, error);
    g_mutex_unlock (&client->mutex);
    return ok;
}

/**
 * puls_helper_client_watchdog_check:
 * @client: A #PulsHelperClient.
 *
 * Non-blocking watchdog check via `waitpid(..., WNOHANG)`. Detects unexpected
 * helper termination and reaps any zombie immediately.
 *
 * Returns: %TRUE if the helper is currently running and alive, %FALSE otherwise.
 */
gboolean
puls_helper_client_watchdog_check (PulsHelperClient *client)
{
    g_return_val_if_fail (client != NULL, FALSE);

    g_mutex_lock (&client->mutex);
    gboolean alive = check_and_reap_if_exited_locked (client, TRUE);
    g_mutex_unlock (&client->mutex);
    return alive;
}

static gboolean
helper_watchdog_timer_cb (gpointer user_data)
{
    PulsHelperClient *client = (PulsHelperClient *)user_data;
    (void)puls_helper_client_watchdog_check (client);
    return G_SOURCE_CONTINUE;
}

/**
 * puls_helper_client_watchdog_start:
 * @client: A #PulsHelperClient.
 * @interval_ms: Poll interval in milliseconds (minimum 20 ms).
 *
 * Attaches a periodic GLib watchdog timer to reap any crashed helper child
 * promptly even while the GUI is idle between disk queries.
 */
void
puls_helper_client_watchdog_start (PulsHelperClient *client, guint interval_ms)
{
    g_return_if_fail (client != NULL);

    if (interval_ms < 20)
        interval_ms = 20;

    puls_helper_client_watchdog_stop (client);

    guint new_id = g_timeout_add (interval_ms, helper_watchdog_timer_cb, client);

    g_mutex_lock (&client->mutex);
    guint old_id = client->watchdog_source_id;
    client->watchdog_source_id = new_id;
    g_mutex_unlock (&client->mutex);

    if (old_id != 0)
        g_source_remove (old_id);
}

/**
 * puls_helper_client_exchange:
 * @client: A #PulsHelperClient.
 * @command: Framed Protocol v2 command line ending in `\n`.
 * @out_ok: (out) (optional): %TRUE for `OK`, %FALSE for `ERR`.
 * @out_err_code: (out) (optional): Helper error token when `ERR`.
 * @out_payload: (out) (optional): Payload bytes.
 * @error: (out) (optional): Return location for a #GError.
 *
 * Sends @command to the helper with transparent crash detection, `waitpid()`
 * zombie reaping, and automatic one-shot restart + retry if the helper died
 * before or during command delivery.
 *
 * Returns: %TRUE if a valid Protocol v2 response frame was received.
 */
gboolean
puls_helper_client_exchange (PulsHelperClient  *client,
                             const gchar       *command,
                             gboolean          *out_ok,
                             gchar            **out_err_code,
                             gchar            **out_payload,
                             GError           **error)
{
    g_return_val_if_fail (client != NULL, FALSE);
    g_return_val_if_fail (command != NULL, FALSE);

    g_mutex_lock (&client->mutex);

    gboolean success = FALSE;
    gboolean is_quit = (g_strcmp0 (command, PULS_HELPER_CMD_QUIT "\n") == 0);

    for (guint attempt = 0; attempt < 2; attempt++) {
        GError *local_err = NULL;

        if (!ensure_helper_running_locked (client, &local_err)) {
            g_propagate_error (error, local_err);
            break;
        }

        gboolean resp_ok = FALSE;
        gchar *err_code = NULL;
        gchar *payload = NULL;

        if (puls_helper_exchange_command (client->stdin_stream, client->stdout_stream,
                                          command, &resp_ok, &err_code, &payload,
                                          &local_err)) {
            if (is_quit && resp_ok)
                terminate_and_reap_helper_locked (client, FALSE, FALSE);

            if (out_ok)
                *out_ok = resp_ok;
            if (out_err_code)
                *out_err_code = err_code;
            else
                g_free (err_code);
            if (out_payload)
                *out_payload = payload;
            else
                g_free (payload);

            success = TRUE;
            break;
        }

        /* Transport failure (broken pipe / EOF / crash mid-command): reap & recover */
        terminate_and_reap_helper_locked (client, FALSE, TRUE);

        if (attempt == 0 && !is_quit) {
            PULS_LOG_WARN ("Helper IPC transport failed (%s); restarting helper and retrying",
                           local_err ? local_err->message : "EOF");
            g_clear_error (&local_err);
            continue;
        }

        g_propagate_error (error, local_err);
        break;
    }

    g_mutex_unlock (&client->mutex);
    return success;
}

/**
 * puls_helper_client_stream:
 * @client: A #PulsHelperClient.
 * @command: Framed Protocol v2 streaming command line ending in `\n`.
 * @chunk_cb: (nullable) (scope call): Real-time telemetry chunk callback.
 * @user_data: Context pointer for @chunk_cb.
 * @out_ok: (out) (optional): %TRUE for terminal `OK`, %FALSE for `ERR`.
 * @out_err_code: (out) (optional): Error token when `ERR`.
 * @out_final_payload: (out) (optional): Terminal JSON payload.
 * @error: (out) (optional): Return location for a #GError.
 *
 * Executes a continuous chunked streaming command over @client with watchdog
 * liveness verification and bidirectional `CANCEL` support.
 *
 * Returns: %TRUE if the stream completed with a valid terminal frame.
 */
gboolean
puls_helper_client_stream (PulsHelperClient         *client,
                           const gchar              *command,
                           PulsHelperStreamCallback  chunk_cb,
                           gpointer                  user_data,
                           gboolean                 *out_ok,
                           gchar                   **out_err_code,
                           gchar                   **out_final_payload,
                           GError                  **error)
{
    g_return_val_if_fail (client != NULL, FALSE);
    g_return_val_if_fail (command != NULL, FALSE);

    g_mutex_lock (&client->mutex);

    if (!ensure_helper_running_locked (client, error)) {
        g_mutex_unlock (&client->mutex);
        return FALSE;
    }

    GError *local_err = NULL;
    gboolean ok = puls_helper_exchange_stream (client->stdin_stream,
                                               client->stdout_stream,
                                               command,
                                               chunk_cb,
                                               user_data,
                                               out_ok,
                                               out_err_code,
                                               out_final_payload,
                                               &local_err);
    if (!ok) {
        terminate_and_reap_helper_locked (client, FALSE, TRUE);
        g_propagate_error (error, local_err);
    }

    g_mutex_unlock (&client->mutex);
    return ok;
}

/**
 * puls_helper_client_get_stats:
 * @client: A #PulsHelperClient.
 * @out_stats: (out): Snapshot of PID, liveness, launch, crash, and reap counters.
 */
void
puls_helper_client_get_stats (PulsHelperClient *client, PulsHelperStats *out_stats)
{
    g_return_if_fail (client != NULL);
    g_return_if_fail (out_stats != NULL);

    g_mutex_lock (&client->mutex);
    out_stats->pid            = client->pid;
    out_stats->running        = (client->pid > 0);
    out_stats->launch_count   = client->launch_count;
    out_stats->crash_count    = client->crash_count;
    out_stats->reaped_count   = client->reaped_count;
    out_stats->elevation_mode = client->elevation_mode;
    g_mutex_unlock (&client->mutex);
}

static PulsHelperClient *
get_default_helper_client (void)
{
    if (g_once_init_enter (&default_helper_client_init)) {
        memset (&default_helper_client, 0, sizeof (default_helper_client));
        g_mutex_init (&default_helper_client.mutex);
        default_helper_client.elevation_mode = puls_get_elevation_mode ();
        default_helper_client.allow_fallback = TRUE;
        PULS_LOG_DEBUG ("Initialized helper elevation mode: %s",
                        puls_elevation_mode_to_string (default_helper_client.elevation_mode));
        atexit (puls_helper_shutdown);
        g_once_init_leave (&default_helper_client_init, 1);
    }
    return &default_helper_client;
}

gboolean
puls_helper_watchdog_check (void)
{
    if (g_atomic_pointer_get (&default_helper_client_init) == 0)
        return FALSE;
    return puls_helper_client_watchdog_check (&default_helper_client);
}

void
puls_helper_watchdog_start (guint interval_ms)
{
    puls_helper_client_watchdog_start (get_default_helper_client (), interval_ms);
}

void
puls_helper_watchdog_stop (void)
{
    if (g_atomic_pointer_get (&default_helper_client_init) == 0)
        return;
    puls_helper_client_watchdog_stop (&default_helper_client);
}

void
puls_helper_shutdown (void)
{
    if (g_atomic_pointer_get (&default_helper_client_init) == 0)
        return;
    puls_helper_client_shutdown (&default_helper_client);
}

static gchar *
communicate_with_helper (const gchar *command, GError **error)
{
    gboolean resp_ok = FALSE;
    g_autofree gchar *err_code = NULL;
    gchar *payload = NULL;

    if (!puls_helper_client_exchange (get_default_helper_client (), command,
                                      &resp_ok, &err_code, &payload, error)) {
        return NULL;
    }

    if (!resp_ok) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Helper command rejected (%s): %s",
                     err_code ? err_code : PULS_HELPER_ERR_INTERNAL,
                     payload ? payload : "");
        g_free (payload);
        return NULL;
    }

    return payload;
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

/**
 * puls_node_has_direct_rw_access:
 * @path: (nullable): Filesystem path to a device node or test node.
 *
 * Checks that @path exists, is NOT a symlink (`!S_ISLNK`), and is both
 * readable and writable (`access(path, R_OK | W_OK) == 0`) by the current
 * user without requiring privilege elevation.
 *
 * Returns: %TRUE if direct read/write access is permitted, %FALSE otherwise.
 */
gboolean
puls_node_has_direct_rw_access (const gchar *path)
{
    if (path == NULL || *path == '\0')
        return FALSE;

    struct stat lst;
    if (lstat (path, &lst) != 0)
        return FALSE;

    if (S_ISLNK (lst.st_mode))
        return FALSE;

    return (access (path, R_OK | W_OK) == 0);
}

/**
 * puls_device_supports_zero_elevation:
 * @device_path: (nullable): Storage device node path under `/dev`.
 *
 * Verifies that @device_path is a genuine, non-symlink block or character
 * storage device node under `/dev` AND that the current user has direct
 * `R_OK | W_OK` permissions (`access(device_path, R_OK | W_OK) == 0`) so
 * `smartctl` can run directly without invoking `pkexec` or the privileged
 * helper.
 *
 * Returns: %TRUE if zero-elevation direct execution is permitted.
 */
gboolean
puls_device_supports_zero_elevation (const gchar *device_path)
{
    if (!puls_validate_device_node (device_path, NULL))
        return FALSE;

    return (access (device_path, R_OK | W_OK) == 0);
}

/**
 * puls_smartctl_output_is_permission_denied:
 * @output: (nullable): Raw stdout/JSON captured from a direct `smartctl` call.
 *
 * Detects whether a direct unprivileged `smartctl` invocation was rejected by
 * the kernel due to missing device or `CAP_SYS_RAWIO`/`CAP_SYS_ADMIN`
 * privileges, indicating that elevated fallback should be attempted.
 *
 * Returns: %TRUE if @output indicates a permission error.
 */
gboolean
puls_smartctl_output_is_permission_denied (const gchar *output)
{
    if (output == NULL || *output == '\0')
        return TRUE;

    return (strstr (output, "Permission denied") != NULL ||
            strstr (output, "Operation not permitted") != NULL ||
            strstr (output, "EACCES") != NULL ||
            strstr (output, "EPERM") != NULL);
}

/**
 * puls_execute_smartctl_with_zero_elevation:
 * @device_path: Target device path.
 * @check_standby: Whether to pass `-n standby`.
 * @allow_direct_rw: Result of permission check (`R_OK | W_OK`).
 * @direct_fn: Direct unprivileged `smartctl` runner (bypassing helper/pkexec).
 * @helper_fn: (nullable): Elevated helper/fallback runner used when direct access
 *   is unavailable or kernel ioctl returns permission denied.
 * @user_data: Opaque context passed to @direct_fn and @helper_fn.
 * @out_used_direct: (out) (optional): Set to %TRUE if @direct_fn fulfilled the
 *   query without invoking @helper_fn.
 * @error: (out) (optional): Return location for a #GError.
 *
 * Orchestrates Zero-Elevation Mode: when @allow_direct_rw is %TRUE, executes
 * @direct_fn directly without invoking `pkexec` or the helper. Falls back to
 * @helper_fn only when direct permissions are absent or kernel ioctls reject
 * the unprivileged call.
 *
 * Returns: (transfer full) (nullable): `smartctl` output string on success.
 */
gchar *
puls_execute_smartctl_with_zero_elevation (const gchar            *device_path,
                                           gboolean                check_standby,
                                           gboolean                allow_direct_rw,
                                           PulsSmartctlDirectFunc  direct_fn,
                                           PulsSmartctlHelperFunc  helper_fn,
                                           gpointer                user_data,
                                           gboolean               *out_used_direct,
                                           GError                **error)
{
    g_return_val_if_fail (device_path != NULL, NULL);
    g_return_val_if_fail (direct_fn != NULL, NULL);

    if (out_used_direct != NULL)
        *out_used_direct = FALSE;

    if (allow_direct_rw) {
        GError *direct_err = NULL;
        gchar *direct_out = direct_fn (device_path, check_standby, user_data, &direct_err);

        if (direct_out != NULL &&
            *direct_out != '\0' &&
            !puls_smartctl_output_is_permission_denied (direct_out)) {
            g_clear_error (&direct_err);
            if (out_used_direct != NULL)
                *out_used_direct = TRUE;
            return direct_out;
        }

        PULS_LOG_DEBUG ("Zero-elevation direct read for %s did not succeed (%s); falling back to helper",
                        device_path,
                        direct_err ? direct_err->message : "permission denied or empty output");
        g_free (direct_out);

        if (helper_fn == NULL) {
            if (direct_err != NULL)
                g_propagate_error (error, direct_err);
            else
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                             "Direct smartctl read denied for %s", device_path);
            return NULL;
        }
        g_clear_error (&direct_err);
    }

    if (helper_fn == NULL) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                     "No helper available for non-accessible device %s", device_path);
        return NULL;
    }

    return helper_fn (device_path, check_standby, user_data, error);
}

static gchar *
spawn_smartctl_direct_sync (const gchar *cmdline, GError **error)
{
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;

    g_spawn_command_line_sync (cmdline, &stdout_buf, &stderr_buf, &exit_status, error);
    g_free (stderr_buf);
    return stdout_buf;
}

static gchar *
spawn_smartctl_fallback_sync (const gchar       *cmdline,
                              PulsElevationMode  mode,
                              GError           **error)
{
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;

    if (mode == PULS_ELEVATION_SUDO && puls_is_sudo_available ()) {
        g_autofree gchar *sudo_cmd = g_strdup_printf ("sudo -n %s", cmdline);
        GError *sudo_err = NULL;
        if (g_spawn_command_line_sync (sudo_cmd, &stdout_buf, &stderr_buf, &exit_status, &sudo_err) &&
            stdout_buf != NULL && *stdout_buf != '\0') {
            g_free (stderr_buf);
            return stdout_buf;
        }
        g_clear_pointer (&stdout_buf, g_free);
        g_clear_pointer (&stderr_buf, g_free);
        g_clear_error (&sudo_err);
    }

    return spawn_smartctl_direct_sync (cmdline, error);
}

static gchar *
prod_smartctl_direct_read (const gchar  *device_path,
                           gboolean      check_standby,
                           gpointer      user_data G_GNUC_UNUSED,
                           GError      **error)
{
    g_autofree gchar *cmdline = puls_build_smartctl_read_command (device_path, check_standby);
    return spawn_smartctl_direct_sync (cmdline, error);
}

static gchar *
prod_smartctl_helper_read (const gchar  *device_path,
                           gboolean      check_standby,
                           gpointer      user_data G_GNUC_UNUSED,
                           GError      **error)
{
    PulsElevationMode mode = puls_get_elevation_mode ();

    /* Use privileged or group-credential helper when elevation/group access applies */
    if (mode != PULS_ELEVATION_NONE) {
        g_autofree gchar *cmd = puls_build_helper_read_command (device_path, check_standby);
        gchar *res = communicate_with_helper (cmd, error);
        if (res != NULL)
            return res;

        if (error)
            g_clear_error (error);
    }

    g_autofree gchar *cmdline = puls_build_smartctl_read_command (device_path, check_standby);
    return spawn_smartctl_fallback_sync (cmdline, mode, error);
}

gchar *
puls_run_smartctl_sync_with_options (const gchar *device_path,
                                     gboolean     check_standby,
                                     GError     **error)
{
    if (!puls_device_path_is_valid (device_path)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Invalid or unsafe device path: %s",
                     device_path ? device_path : "(null)");
        return NULL;
    }

    gboolean zero_elev = puls_device_supports_zero_elevation (device_path);
    gboolean used_direct = FALSE;

    gchar *out = puls_execute_smartctl_with_zero_elevation (
        device_path,
        check_standby,
        zero_elev,
        prod_smartctl_direct_read,
        prod_smartctl_helper_read,
        NULL,
        &used_direct,
        error);

    if (used_direct && out != NULL) {
        PULS_LOG_DEBUG ("Zero-elevation mode: direct smartctl read succeeded for %s (pkexec/helper bypassed)",
                        device_path);
    }

    return out;
}

gchar *
puls_run_smartctl_sync (const gchar *device_path, GError **error)
{
    gboolean use_standby = puls_should_use_standby_check (device_path);
    return puls_run_smartctl_sync_with_options (device_path, use_standby, error);
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

/**
 * puls_device_path_is_valid:
 * @path: (nullable): Device path string.
 *
 * Strictly validates that @path conforms to the whole-disk storage node syntax
 * whitelist (`/dev/sda`, `/dev/nvme0`, `/dev/nvme0n1`, `/dev/vda`, `/dev/xvda`,
 * `/dev/hda`, `/dev/mmcblk0`, `/dev/sg0`). Rejects directory traversal (`..`),
 * shell metacharacters, partitions (`/dev/sda1`, `/dev/nvme0n1p1`), and
 * pseudo-devices (`/dev/null`, `/dev/loop0`).
 *
 * Returns: %TRUE if @path satisfies the syntactic whitelist.
 */
gboolean
puls_device_path_is_valid (const gchar *path)
{
    return puls_helper_is_valid_device_syntax (path) != 0;
}

/**
 * puls_validate_device_node_in_dir:
 * @dev_root: Base directory for device nodes (e.g. `"/dev"` or a test directory).
 * @device_path: Full path to the device node inside @dev_root (or `/dev/<name>`).
 * @error: (out) (optional): Return location for a #GError.
 *
 * Performs multi-layer path sanitization and anti-hijacking verification:
 * 1. Validates the basename against the strict whole-disk device syntax whitelist.
 * 2. Performs `lstat()` and rejects symlinks (`S_ISLNK`) or non-device files
 *    (requiring `S_ISBLK` or `S_ISCHR`).
 * 3. Resolves both @dev_root and the target node via `realpath()` and verifies
 *    the canonical target path matches `<canonical_root>/<basename>` with no
 *    symlink indirection or traversal.
 *
 * Returns: %TRUE if the device node is a genuine, non-symlink block/character
 * device inside @dev_root, %FALSE otherwise.
 */
gboolean
puls_validate_device_node_in_dir (const gchar *dev_root,
                                  const gchar *device_path,
                                  GError     **error)
{
    if (dev_root == NULL || *dev_root == '\0' ||
        device_path == NULL || *device_path == '\0') {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Device root or path is NULL or empty");
        return FALSE;
    }

    const gchar *root_prefix = dev_root;
    g_autofree gchar *trimmed_root = NULL;
    gsize root_len = strlen (dev_root);
    while (root_len > 1 && dev_root[root_len - 1] == '/')
        root_len--;
    trimmed_root = g_strndup (dev_root, root_len);
    root_prefix = trimmed_root;

    if (!g_str_has_prefix (device_path, root_prefix) ||
        device_path[root_len] != '/' ||
        device_path[root_len + 1] == '\0') {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Device path '%s' is not directly under '%s'",
                     device_path, root_prefix);
        return FALSE;
    }

    const gchar *base_name = device_path + root_len + 1;
    if (strchr (base_name, '/') != NULL) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Subdirectory or traversal in device name '%s'",
                     device_path);
        return FALSE;
    }

    g_autofree gchar *canonical_dev_syntax = g_strdup_printf ("/dev/%s", base_name);
    if (!puls_device_path_is_valid (canonical_dev_syntax)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Device name '%s' failed strict syntax whitelist",
                     base_name);
        return FALSE;
    }

    struct stat lst;
    if (lstat (device_path, &lst) != 0) {
        gint saved_errno = errno;
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                     "Cannot lstat device node '%s': %s",
                     device_path, g_strerror (saved_errno));
        return FALSE;
    }

    if (S_ISLNK (lst.st_mode)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Symlink hijacking rejected for '%s'", device_path);
        return FALSE;
    }

    if (!S_ISBLK (lst.st_mode) && !S_ISCHR (lst.st_mode)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Path '%s' is not a block or character device node",
                     device_path);
        return FALSE;
    }

    char resolved_root[PATH_MAX];
    char resolved_node[PATH_MAX];
    if (realpath (root_prefix, resolved_root) == NULL ||
        realpath (device_path, resolved_node) == NULL) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Canonical realpath check failed for '%s'", device_path);
        return FALSE;
    }

    g_autofree gchar *expected_resolved = g_strdup_printf ("%s/%s",
                                                           resolved_root,
                                                           base_name);
    if (g_strcmp0 (resolved_node, expected_resolved) != 0) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Canonical path mismatch for '%s' (resolved to '%s')",
                     device_path, resolved_node);
        return FALSE;
    }

    return TRUE;
}

/**
 * puls_validate_device_node:
 * @device_path: Full path under `/dev` (e.g. `"/dev/nvme0n1"`).
 * @error: (out) (optional): Return location for a #GError.
 *
 * Convenience wrapper around puls_validate_device_node_in_dir() with
 * `dev_root` set to `"/dev"`.
 *
 * Returns: %TRUE if @device_path is a verified block/char device node in `/dev`.
 */
gboolean
puls_validate_device_node (const gchar *device_path,
                           GError     **error)
{
    if (!puls_device_path_is_valid (device_path)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Invalid device path syntax: %s",
                     device_path ? device_path : "(null)");
        return FALSE;
    }

    return puls_validate_device_node_in_dir ("/dev", device_path, error);
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
    if (!puls_device_path_is_valid (device_path)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Invalid or unsafe device path: %s",
                     device_path ? device_path : "(null)");
        return NULL;
    }

    if (!puls_helper_action_is_valid (action)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Disallowed smartctl action '%s'; only 'short', 'long', or 'abort' is permitted",
                     action ? action : "(null)");
        return NULL;
    }

    g_autofree gchar *cmdline = NULL;
    if (g_strcmp0 (action, PULS_HELPER_ACTION_SHORT) == 0) {
        cmdline = g_strdup_printf ("smartctl -t short %s", device_path);
    } else if (g_strcmp0 (action, PULS_HELPER_ACTION_LONG) == 0) {
        cmdline = g_strdup_printf ("smartctl -t long %s", device_path);
    } else if (g_strcmp0 (action, PULS_HELPER_ACTION_ABORT) == 0) {
        cmdline = g_strdup_printf ("smartctl -X %s", device_path);
    } else {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Disallowed smartctl action: %s", action);
        return NULL;
    }

    /* Zero-elevation fast path: run smartctl directly when device node allows R_OK | W_OK */
    if (puls_device_supports_zero_elevation (device_path)) {
        GError *direct_err = NULL;
        gchar *direct_out = spawn_smartctl_direct_sync (cmdline, &direct_err);
        if (direct_out != NULL &&
            *direct_out != '\0' &&
            !puls_smartctl_output_is_permission_denied (direct_out)) {
            g_clear_error (&direct_err);
            PULS_LOG_DEBUG ("Zero-elevation mode: direct smartctl action '%s' succeeded for %s",
                            action, device_path);
            return direct_out;
        }
        g_free (direct_out);
        g_clear_error (&direct_err);
    }

    PulsElevationMode mode = puls_get_elevation_mode ();

    if (mode != PULS_ELEVATION_NONE) {
        g_autofree gchar *cmd = puls_helper_format_action (device_path, action);
        gchar *res = communicate_with_helper (cmd, error);
        if (res != NULL)
            return res;

        if (error)
            g_clear_error (error);
    }

    gchar *stdout_buf = spawn_smartctl_fallback_sync (cmdline, mode, error);
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

/**
 * puls_zfs_member_info_clear:
 * @info: A #PulsZfsMemberInfo struct.
 *
 * Frees owned fields in @info and resets it to zero.
 */
void
puls_zfs_member_info_clear (PulsZfsMemberInfo *info)
{
    if (info == NULL)
        return;

    g_free (info->pool_name);
    g_free (info->pool_state);
    g_free (info->vdev_type);
    g_free (info->member_name);
    g_free (info->member_state);
    g_free (info->status_text);
    memset (info, 0, sizeof (*info));
}

/**
 * puls_zfs_member_info_free:
 * @info: Heap-allocated #PulsZfsMemberInfo.
 *
 * Clears and frees @info.
 */
void
puls_zfs_member_info_free (PulsZfsMemberInfo *info)
{
    if (info == NULL)
        return;
    puls_zfs_member_info_clear (info);
    g_free (info);
}

/**
 * puls_zfs_format_member_status:
 * @info: Populated #PulsZfsMemberInfo.
 *
 * Formats a concise ZFS membership summary, such as
 * `"ZFS Pool: tank [RAIDZ1, ONLINE]"` or `"ZFS Pool: rpool [MIRROR, DEGRADED]"`.
 *
 * Returns: (nullable) (transfer full): Newly allocated status string.
 */
gchar *
puls_zfs_format_member_status (const PulsZfsMemberInfo *info)
{
    if (info == NULL || info->pool_name == NULL)
        return NULL;

    const gchar *state = (info->member_state && info->member_state[0] != '\0')
                         ? info->member_state
                         : info->pool_state;

    if (info->vdev_type != NULL && state != NULL)
        return g_strdup_printf ("ZFS Pool: %s [%s, %s]", info->pool_name, info->vdev_type, state);
    if (info->vdev_type != NULL)
        return g_strdup_printf ("ZFS Pool: %s [%s]", info->pool_name, info->vdev_type);
    if (state != NULL)
        return g_strdup_printf ("ZFS Pool: %s [%s]", info->pool_name, state);

    return g_strdup_printf ("ZFS Pool: %s", info->pool_name);
}

static gboolean
buffer_contains_bytes (const guint8 *haystack,
                       gsize         haystack_len,
                       const gchar  *needle)
{
    gsize needle_len = strlen (needle);
    if (needle_len == 0 || haystack_len < needle_len)
        return FALSE;

    for (gsize i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp (haystack + i, needle, needle_len) == 0)
            return TRUE;
    }
    return FALSE;
}

/**
 * puls_zfs_has_label_signature:
 * @data: Raw bytes from a ZFS vdev label or disk header region.
 * @len: Length of @data in bytes.
 *
 * Checks for OpenZFS on-disk signatures: either a 64-bit uberblock magic
 * (`0x0000000000bab10c` in little-endian or big-endian byte order) or the
 * paired XDR nvlist vdev label keys (`pool_guid` and `vdev_tree`).
 *
 * Returns: %TRUE if a valid ZFS disk/label signature is detected.
 */
gboolean
puls_zfs_has_label_signature (const guint8 *data,
                              gsize         len)
{
    if (data == NULL || len < 8)
        return FALSE;

    static const guint8 ub_magic_le[8] = { 0x0c, 0xb1, 0xba, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const guint8 ub_magic_be[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0xb1, 0x0c };

    for (gsize off = 0; off + 8 <= len; off += 8) {
        if (memcmp (data + off, ub_magic_le, 8) == 0 ||
            memcmp (data + off, ub_magic_be, 8) == 0)
            return TRUE;
    }

    if (buffer_contains_bytes (data, len, "pool_guid") &&
        buffer_contains_bytes (data, len, "vdev_tree"))
        return TRUE;

    return FALSE;
}

static const gchar *
classify_zfs_vdev_group (const gchar *token)
{
    if (g_strcmp0 (token, "logs") == 0)
        return "LOG";
    if (g_strcmp0 (token, "cache") == 0)
        return "CACHE";
    if (g_strcmp0 (token, "spares") == 0)
        return "SPARE";
    if (g_strcmp0 (token, "special") == 0)
        return "SPECIAL";
    if (g_strcmp0 (token, "dedup") == 0)
        return "DEDUP";
    if (g_str_has_prefix (token, "mirror-"))
        return "MIRROR";
    if (g_str_has_prefix (token, "raidz3-"))
        return "RAIDZ3";
    if (g_str_has_prefix (token, "raidz2-"))
        return "RAIDZ2";
    if (g_str_has_prefix (token, "raidz1-") || g_str_has_prefix (token, "raidz-"))
        return "RAIDZ1";
    if (g_str_has_prefix (token, "draid"))
        return "DRAID";
    return NULL;
}

static gboolean
zfs_member_matches_disk (const gchar *member_token,
                         const gchar *by_id_dir,
                         const gchar *disk_name)
{
    if (member_token == NULL || disk_name == NULL)
        return FALSE;

    /* 1. Direct block/partition name match (e.g. /dev/sda1, sda1, nvme0n1p3) */
    const gchar *leaf = strrchr (member_token, '/');
    leaf = leaf ? leaf + 1 : member_token;
    if (device_name_is_partition_of (leaf, disk_name))
        return TRUE;

    /* 2. Direct symlink resolution when full path exists (e.g. /dev/disk/by-id/...) */
    if (g_path_is_absolute (member_token)) {
        g_autofree gchar *resolved = puls_sysfs_resolve_link (member_token);
        if (resolved != NULL) {
            g_autofree gchar *res_base = g_path_get_basename (resolved);
            if (device_name_is_partition_of (res_base, disk_name))
                return TRUE;
        }
    }

    /* 3. Resolve leaf name inside by_id_dir (supports bare IDs & mock by-id dirs) */
    if (by_id_dir != NULL && *leaf != '\0') {
        g_autofree gchar *candidate = g_build_filename (by_id_dir, leaf, NULL);
        g_autofree gchar *resolved = puls_sysfs_resolve_link (candidate);
        if (resolved != NULL) {
            g_autofree gchar *res_base = g_path_get_basename (resolved);
            if (device_name_is_partition_of (res_base, disk_name))
                return TRUE;
        }
    }

    return FALSE;
}

/**
 * puls_zfs_list_memberships_from_status:
 * @zpool_status_text: Output text from `zpool status` / `zpool status -P`.
 * @by_id_dir: (nullable): Directory containing `/dev/disk/by-id` symlinks.
 * @device_path: Physical disk name or path (e.g. `"/dev/sda"`).
 *
 * Parses @zpool_status_text and resolves leaf vdev entries (including `/dev/disk/by-id/`
 * symlinks) to identify all ZFS pool memberships for @device_path.
 *
 * Returns: (element-type PulsZfsMemberInfo) (transfer full): List of memberships.
 */
GList *
puls_zfs_list_memberships_from_status (const gchar *zpool_status_text,
                                       const gchar *by_id_dir,
                                       const gchar *device_path)
{
    if (zpool_status_text == NULL || device_path == NULL)
        return NULL;

    g_autofree gchar *disk_name = puls_sanitize_device_name (device_path);
    if (disk_name == NULL)
        return NULL;

    GList *results = NULL;
    g_auto(GStrv) lines = g_strsplit (zpool_status_text, "\n", -1);

    g_autofree gchar *current_pool = NULL;
    g_autofree gchar *current_pool_state = NULL;
    const gchar *current_vdev = "STRIPE";
    gboolean in_config = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        g_autofree gchar *trimmed = g_strdup (lines[i]);
        g_strstrip (trimmed);

        if (trimmed[0] == '\0')
            continue;

        if (g_str_has_prefix (trimmed, "pool:")) {
            g_free (current_pool);
            current_pool = g_strdup (trimmed + 5);
            g_strstrip (current_pool);
            g_clear_pointer (&current_pool_state, g_free);
            current_vdev = "STRIPE";
            in_config = FALSE;
            continue;
        }

        if (g_str_has_prefix (trimmed, "state:")) {
            g_free (current_pool_state);
            current_pool_state = g_strdup (trimmed + 6);
            g_strstrip (current_pool_state);
            continue;
        }

        if (g_str_has_prefix (trimmed, "config:")) {
            in_config = TRUE;
            continue;
        }

        if (g_str_has_prefix (trimmed, "errors:")) {
            in_config = FALSE;
            continue;
        }

        if (!in_config || current_pool == NULL)
            continue;

        if (g_str_has_prefix (trimmed, "NAME") && strstr (trimmed, "STATE") != NULL)
            continue;

        g_auto(GStrv) raw_tokens = g_strsplit_set (trimmed, " \t", -1);
        const gchar *toks[6] = { NULL };
        guint n_toks = 0;
        for (guint t = 0; raw_tokens[t] != NULL && n_toks < 5; t++) {
            if (raw_tokens[t][0] != '\0')
                toks[n_toks++] = raw_tokens[t];
        }

        if (n_toks == 0)
            continue;

        const gchar *name_tok = toks[0];
        if (g_strcmp0 (name_tok, current_pool) == 0) {
            current_vdev = "STRIPE";
            continue;
        }

        const gchar *group = classify_zfs_vdev_group (name_tok);
        if (group != NULL) {
            current_vdev = group;
            continue;
        }

        if (zfs_member_matches_disk (name_tok, by_id_dir, disk_name)) {
            PulsZfsMemberInfo *info = g_new0 (PulsZfsMemberInfo, 1);
            info->pool_name = g_strdup (current_pool);
            info->pool_state = g_strdup (current_pool_state);
            info->vdev_type = g_strdup (current_vdev);
            info->member_name = g_strdup (name_tok);
            info->member_state = (n_toks >= 2) ? g_strdup (toks[1]) : g_strdup (current_pool_state);
            info->read_errors  = (n_toks >= 3) ? g_ascii_strtoull (toks[2], NULL, 10) : 0;
            info->write_errors = (n_toks >= 4) ? g_ascii_strtoull (toks[3], NULL, 10) : 0;
            info->cksum_errors = (n_toks >= 5) ? g_ascii_strtoull (toks[4], NULL, 10) : 0;
            info->status_text = puls_zfs_format_member_status (info);
            results = g_list_append (results, info);
        }
    }

    return results;
}

/**
 * puls_zfs_detect_member_from_status:
 * @zpool_status_text: Output from `zpool status`.
 * @by_id_dir: (nullable): Directory containing `/dev/disk/by-id` symlinks.
 * @device_path: Physical disk name or path (e.g. `"/dev/sda"`).
 * @out_info: Output #PulsZfsMemberInfo struct.
 *
 * Returns: %TRUE if @device_path belongs to at least one ZFS pool in @zpool_status_text.
 */
gboolean
puls_zfs_detect_member_from_status (const gchar       *zpool_status_text,
                                    const gchar       *by_id_dir,
                                    const gchar       *device_path,
                                    PulsZfsMemberInfo *out_info)
{
    g_return_val_if_fail (out_info != NULL, FALSE);

    memset (out_info, 0, sizeof (*out_info));

    GList *list = puls_zfs_list_memberships_from_status (zpool_status_text, by_id_dir, device_path);
    if (list == NULL)
        return FALSE;

    PulsZfsMemberInfo *first = list->data;
    out_info->pool_name    = g_strdup (first->pool_name);
    out_info->pool_state   = g_strdup (first->pool_state);
    out_info->vdev_type    = g_strdup (first->vdev_type);
    out_info->member_name  = g_strdup (first->member_name);
    out_info->member_state = g_strdup (first->member_state);
    out_info->read_errors  = first->read_errors;
    out_info->write_errors = first->write_errors;
    out_info->cksum_errors = first->cksum_errors;
    out_info->status_text  = g_strdup (first->status_text);

    g_list_free_full (list, (GDestroyNotify)puls_zfs_member_info_free);
    return TRUE;
}

/**
 * puls_zfs_detect_member:
 * @device_path: Physical disk name or path (e.g. `"/dev/sda"`).
 * @out_info: Output #PulsZfsMemberInfo struct.
 *
 * Queries `zpool status -P` if `zpool` is installed and matches physical vdev
 * entries against @device_path and `/dev/disk/by-id`.
 *
 * Returns: %TRUE if @device_path is detected as a ZFS pool member.
 */
gboolean
puls_zfs_detect_member (const gchar       *device_path,
                        PulsZfsMemberInfo *out_info)
{
    g_return_val_if_fail (out_info != NULL, FALSE);

    memset (out_info, 0, sizeof (*out_info));

    if (device_path == NULL)
        return FALSE;

    g_autofree gchar *zpool_bin = g_find_program_in_path ("zpool");
    if (zpool_bin == NULL)
        return FALSE;

    const gchar *argv[] = { zpool_bin, "status", "-P", NULL };
    g_autofree gchar *stdout_buf = NULL;
    g_autofree gchar *stderr_buf = NULL;
    gint exit_status = 0;

    if (!g_spawn_sync (NULL, (gchar **)argv, NULL, G_SPAWN_DEFAULT,
                       NULL, NULL, &stdout_buf, &stderr_buf, &exit_status, NULL))
        return FALSE;

    if (exit_status != 0 || stdout_buf == NULL || stdout_buf[0] == '\0')
        return FALSE;

    return puls_zfs_detect_member_from_status (stdout_buf, "/dev/disk/by-id",
                                               device_path, out_info);
}

static gboolean
contains_any_token (const gchar        *haystack_lower,
                    const gchar * const *tokens)
{
    for (guint i = 0; tokens[i] != NULL; i++) {
        if (strstr (haystack_lower, tokens[i]) != NULL)
            return TRUE;
    }
    return FALSE;
}

static PulsFormFactor
detect_m2_size_from_model (const gchar *lower)
{
    if (strstr (lower, "22110"))
        return PULS_FORM_FACTOR_M2_22110;
    if (strstr (lower, "2280"))
        return PULS_FORM_FACTOR_M2_2280;
    if (strstr (lower, "2260"))
        return PULS_FORM_FACTOR_M2_2260;
    if (strstr (lower, "2242"))
        return PULS_FORM_FACTOR_M2_2242;
    if (strstr (lower, "2230"))
        return PULS_FORM_FACTOR_M2_2230;

    static const gchar * const m2_2230_models[] = {
        "mtfdkbk", "kbg40zns", "kbg50zns", "bg4", "bg5", "bg6",
        "sn740", "sn530", "pm991a", "bc711", "bc901", NULL
    };
    if (contains_any_token (lower, m2_2230_models))
        return PULS_FORM_FACTOR_M2_2230;

    static const gchar * const m2_2242_models[] = {
        "mtfdhba", "sn520", "pm991 ", NULL
    };
    if (contains_any_token (lower, m2_2242_models))
        return PULS_FORM_FACTOR_M2_2242;

    static const gchar * const m2_2280_models[] = {
        "mtfdkba", "mtfdkcd", "970 evo", "980 pro", "990 pro", "990 evo",
        "pm9a1", "pm981", "sn850", "sn770", "sn570", "sn580",
        "p5 plus", "p3 plus", "t500", "t700", "kc3000", "nv2",
        "solidigm p44", "p41", "firecuda 530", "mp600", NULL
    };
    if (contains_any_token (lower, m2_2280_models))
        return PULS_FORM_FACTOR_M2_2280;

    return PULS_FORM_FACTOR_UNKNOWN;
}

static gboolean
model_looks_like_35_hdd (const gchar *lower)
{
    static const gchar * const hdd_35_tokens[] = {
        "ironwolf", "exos", "skyhawk", "barracuda pro", "wd red",
        "wd gold", "wd purple", "wd black", "ultrastar", "n300",
        "x300", "mg07", "mg08", "mg09", "mg10", NULL
    };
    if (contains_any_token (lower, hdd_35_tokens))
        return TRUE;

    /* Seagate 3.5" model families (ST*DM*, ST*VN*, ST*NM*, ST*VX*) */
    if (g_str_has_prefix (lower, "st") &&
        (strstr (lower, "dm0") || strstr (lower, "vn0") ||
         strstr (lower, "nm0") || strstr (lower, "vx0")))
        return TRUE;

    /* WD 3.5" model families (WD*EF*, WD*EZ*, WD*FZ*, WD*PUR*) */
    if (g_str_has_prefix (lower, "wd") &&
        (strstr (lower, "efrx") || strstr (lower, "efax") || strstr (lower, "efpx") ||
         strstr (lower, "ezex") || strstr (lower, "ezaz") || strstr (lower, "ezrz") ||
         strstr (lower, "fzex") || strstr (lower, "purz") || strstr (lower, "purx")))
        return TRUE;

    return FALSE;
}

static gboolean
model_looks_like_25_hdd (const gchar *lower)
{
    static const gchar * const hdd_25_tokens[] = {
        "mobile", "mq01", "mq04", "spzx", "lpcx", "jpvx", NULL
    };
    if (contains_any_token (lower, hdd_25_tokens))
        return TRUE;

    if (g_str_has_prefix (lower, "st") && strstr (lower, "lm0"))
        return TRUE;

    return FALSE;
}

static PulsFormFactor
detect_form_factor_from_model (const gchar *model_name)
{
    if (model_name == NULL || model_name[0] == '\0')
        return PULS_FORM_FACTOR_UNKNOWN;

    g_autofree gchar *lower = g_ascii_strdown (model_name, -1);

    /* Explicit PCIe Add-in Cards (HHHL / AIC) */
    static const gchar * const aic_tokens[] = {
        "add-in", "hhhl", " aic", "-aic", "optane 900p", "optane 905p",
        "optane ssd 900p", "optane ssd 905p", "p4800x", "p5800x",
        "pm1725", "sn150", NULL
    };
    if (contains_any_token (lower, aic_tokens))
        return PULS_FORM_FACTOR_PCIE_AIC;

    /* Enterprise U.2 / U.3 NVMe */
    static const gchar * const u2_tokens[] = {
        "u.2", "u.3", "sff-8639", "p4510", "p4610", "p5510", "p5520",
        "pm9a3", "pm983", "pm1733", "pm1735", "7450 pro", "7450 max",
        "sn840", "sn640", "kcm6", "kcd6", "cd6", "cd8", "cm6", "cm7", NULL
    };
    if (contains_any_token (lower, u2_tokens))
        return PULS_FORM_FACTOR_U2_U3;

    /* EDSFF (E1.S / E1.L / E3.S) */
    if (strstr (lower, "edsff") || strstr (lower, "e1.s") ||
        strstr (lower, "e1.l")  || strstr (lower, "e3.s"))
        return PULS_FORM_FACTOR_EDSFF;

    /* mSATA */
    if (strstr (lower, "msata"))
        return PULS_FORM_FACTOR_MSATA;

    /* M.2 specific sizes & known M.2 model families */
    PulsFormFactor m2_size = detect_m2_size_from_model (lower);
    if (m2_size != PULS_FORM_FACTOR_UNKNOWN)
        return m2_size;

    if (strstr (lower, "m.2") || strstr (lower, "ngff"))
        return PULS_FORM_FACTOR_M2;

    if (strstr (lower, "3.5") || model_looks_like_35_hdd (lower))
        return PULS_FORM_FACTOR_3_5_INCH;

    if (strstr (lower, "2.5") || model_looks_like_25_hdd (lower))
        return PULS_FORM_FACTOR_2_5_INCH;

    return PULS_FORM_FACTOR_UNKNOWN;
}

/**
 * puls_detect_form_factor:
 * @smartctl_ff: (nullable): Raw `form_factor` string from `smartctl`.
 * @model_name: (nullable): Drive model string.
 * @device_path: (nullable): Device path (e.g. `/dev/nvme0n1`, `/dev/sda`).
 * @drive_type: Classified #PulsDriveType.
 * @transport: Classified #PulsTransportProtocol.
 * @rotation_rpm: Rotation speed in RPM (`0` for SSD, `-1` if unknown).
 * @capacity_bytes: Total drive capacity in bytes (`0` if unknown).
 * @pcie_max_width: Max PCIe link width (e.g. `4`, `8`, `16`; `0` if unknown).
 *
 * Combines `smartctl` form factor metadata, model heuristics, PCIe link width,
 * transport, RPM, and capacity to determine the physical drive form factor.
 *
 * Returns: Classified #PulsFormFactor.
 */
PulsFormFactor
puls_detect_form_factor (const gchar          *smartctl_ff,
                         const gchar          *model_name,
                         const gchar          *device_path,
                         PulsDriveType         drive_type,
                         PulsTransportProtocol transport,
                         gint                  rotation_rpm,
                         guint64               capacity_bytes,
                         guint8                pcie_max_width)
{
    /* 1. Explicit smartctl form_factor (refine generic M.2 with model size if possible) */
    PulsFormFactor from_smart = puls_form_factor_from_string (smartctl_ff);
    if (from_smart == PULS_FORM_FACTOR_M2 && model_name != NULL) {
        g_autofree gchar *lower = g_ascii_strdown (model_name, -1);
        PulsFormFactor refined = detect_m2_size_from_model (lower);
        if (refined != PULS_FORM_FACTOR_UNKNOWN)
            return refined;
    }
    if (from_smart != PULS_FORM_FACTOR_UNKNOWN)
        return from_smart;

    /* 2. Model-name hardware heuristics */
    PulsFormFactor from_model = detect_form_factor_from_model (model_name);
    if (from_model != PULS_FORM_FACTOR_UNKNOWN)
        return from_model;

    /* 3. Transport, PCIe width, device path, and drive type heuristics */
    gboolean is_nvme = (drive_type == PULS_DRIVE_TYPE_NVME_SSD ||
                        transport == PULS_TRANSPORT_PCIE ||
                        (device_path != NULL && strstr (device_path, "nvme") != NULL));

    if (is_nvme) {
        if (pcie_max_width >= 8)
            return PULS_FORM_FACTOR_PCIE_AIC;
        return PULS_FORM_FACTOR_M2;
    }

    if (drive_type == PULS_DRIVE_TYPE_USB || transport == PULS_TRANSPORT_USB)
        return PULS_FORM_FACTOR_USB_EXTERNAL;

    if (drive_type == PULS_DRIVE_TYPE_EMMC)
        return PULS_FORM_FACTOR_EMBEDDED_BGA;

    if (drive_type == PULS_DRIVE_TYPE_SD_CARD)
        return PULS_FORM_FACTOR_SD_CARD;

    if (drive_type == PULS_DRIVE_TYPE_VIRTUAL || transport == PULS_TRANSPORT_VIRTUAL)
        return PULS_FORM_FACTOR_VIRTUAL;

    if (drive_type == PULS_DRIVE_TYPE_SATA_SSD || drive_type == PULS_DRIVE_TYPE_SAS_SSD)
        return PULS_FORM_FACTOR_2_5_INCH;

    if (drive_type == PULS_DRIVE_TYPE_SATA_HDD ||
        drive_type == PULS_DRIVE_TYPE_SAS_HDD ||
        drive_type == PULS_DRIVE_TYPE_SSHD) {
        if (rotation_rpm >= 7200 || capacity_bytes >= 3000000000000ULL)
            return PULS_FORM_FACTOR_3_5_INCH;
        if (rotation_rpm > 0 && rotation_rpm <= 5400 &&
            capacity_bytes > 0 && capacity_bytes <= 2000000000000ULL)
            return PULS_FORM_FACTOR_2_5_INCH;
        return PULS_FORM_FACTOR_3_5_INCH;
    }

    return PULS_FORM_FACTOR_UNKNOWN;
}

/**
 * puls_detect_form_factor_for_smart_data:
 * @data: A #PulsSmartData instance.
 *
 * Convenience wrapper invoking puls_detect_form_factor() from @data fields.
 *
 * Returns: Classified #PulsFormFactor.
 */
PulsFormFactor
puls_detect_form_factor_for_smart_data (PulsSmartData *data)
{
    g_return_val_if_fail (PULS_IS_SMART_DATA (data), PULS_FORM_FACTOR_UNKNOWN);

    guint8 pcie_width = puls_smart_data_get_pcie_max_width (data);
    if (pcie_width == 0)
        pcie_width = puls_smart_data_get_pcie_current_width (data);

    return puls_detect_form_factor (puls_smart_data_get_form_factor (data),
                                    puls_smart_data_get_model_name (data),
                                    puls_smart_data_get_device_path (data),
                                    puls_smart_data_get_drive_type (data),
                                    puls_smart_data_get_transport_protocol (data),
                                    puls_smart_data_get_rotation_rpm (data),
                                    puls_smart_data_get_capacity_bytes (data),
                                    pcie_width);
}

/**
 * puls_should_use_standby_check_for_type:
 * @device_path: Block device path (e.g. `/dev/sda`, `/dev/nvme0n1`).
 * @drive_type: Classified #PulsDriveType.
 * @is_rotational: Whether the device has spinning platters.
 *
 * Returns: %TRUE if `-n standby` should be passed to `smartctl` to avoid
 * spinning up a sleeping mechanical drive.
 */
gboolean
puls_should_use_standby_check_for_type (const gchar   *device_path,
                                        PulsDriveType  drive_type,
                                        gboolean       is_rotational)
{
    if (device_path == NULL || *device_path == '\0')
        return FALSE;

    g_autofree gchar *clean = puls_sanitize_device_name (device_path);
    if (clean != NULL &&
        (g_str_has_prefix (clean, "nvme") || g_str_has_prefix (clean, "mmcblk")))
        return FALSE;

    if (puls_drive_type_is_rotational (drive_type))
        return TRUE;

    if (drive_type == PULS_DRIVE_TYPE_NVME_SSD ||
        drive_type == PULS_DRIVE_TYPE_SATA_SSD ||
        drive_type == PULS_DRIVE_TYPE_SAS_SSD  ||
        drive_type == PULS_DRIVE_TYPE_EMMC     ||
        drive_type == PULS_DRIVE_TYPE_SD_CARD  ||
        drive_type == PULS_DRIVE_TYPE_VIRTUAL)
        return FALSE;

    return is_rotational;
}

/**
 * puls_should_use_standby_check:
 * @device_path: Block device path (e.g. `/dev/sda`).
 *
 * Inspects sysfs `queue/rotational` for @device_path to decide whether
 * `smartctl` should be invoked with `-n standby`.
 *
 * Returns: %TRUE for rotational mechanical drives, %FALSE for SSDs/NVMe/virtual.
 */
gboolean
puls_should_use_standby_check (const gchar *device_path)
{
    if (device_path == NULL || *device_path == '\0')
        return FALSE;

    g_autofree gchar *clean = puls_sanitize_device_name (device_path);
    if (clean == NULL)
        return FALSE;

    if (g_str_has_prefix (clean, "nvme") ||
        g_str_has_prefix (clean, "mmcblk") ||
        g_str_has_prefix (clean, "vd") ||
        g_str_has_prefix (clean, "xvd"))
        return FALSE;

    /* Default to 0 when sysfs queue/rotational is absent to avoid false positives */
    gboolean rotational = puls_sysfs_read_queue_uint64 (clean, "rotational", 0) != 0;
    return rotational;
}

/**
 * puls_build_smartctl_read_command:
 * @device_path: Device node path.
 * @check_standby: Whether to include `-n standby`.
 *
 * Returns: (transfer full): Command line string for `smartctl`.
 */
gchar *
puls_build_smartctl_read_command (const gchar *device_path,
                                  gboolean     check_standby)
{
    g_return_val_if_fail (device_path != NULL, NULL);

    if (check_standby)
        return g_strdup_printf ("smartctl -n standby -a -j %s", device_path);

    return g_strdup_printf ("smartctl -a -j %s", device_path);
}

/**
 * puls_build_helper_read_command:
 * @device_path: Device node path.
 * @check_standby: Whether to use `READ_STANDBY` protocol command.
 *
 * Returns: (transfer full): Persistent helper command line ending with `\n`.
 */
gchar *
puls_build_helper_read_command (const gchar *device_path,
                                gboolean     check_standby)
{
    return puls_helper_format_read_smart (device_path, check_standby);
}

/**
 * puls_parse_hdparm_power_state:
 * @hdparm_output: Output from `hdparm -C <device>`.
 *
 * Parses `"drive state is:  <state>"` lines into a #PulsPowerState.
 *
 * Returns: Parsed #PulsPowerState, or %PULS_POWER_STATE_UNKNOWN.
 */
PulsPowerState
puls_parse_hdparm_power_state (const gchar *hdparm_output)
{
    if (hdparm_output == NULL || *hdparm_output == '\0')
        return PULS_POWER_STATE_UNKNOWN;

    const gchar *marker = strstr (hdparm_output, "drive state is:");
    if (marker != NULL)
        return puls_power_state_from_string (marker + 15);

    return puls_power_state_from_string (hdparm_output);
}

/**
 * puls_parse_smartctl_standby_json:
 * @json_str: JSON output from `smartctl -n standby -a -j`.
 *
 * Extracts power state from `"power_mode"` or `"smartctl.messages"` (e.g.
 * `"Device is in STANDBY mode, exit(2)"`).
 *
 * Returns: Parsed #PulsPowerState, or %PULS_POWER_STATE_UNKNOWN.
 */
PulsPowerState
puls_parse_smartctl_standby_json (const gchar *json_str)
{
    if (json_str == NULL || *json_str == '\0')
        return PULS_POWER_STATE_UNKNOWN;

    const gchar *pm = strstr (json_str, "\"power_mode\"");
    if (pm != NULL) {
        const gchar *colon = strchr (pm + 12, ':');
        if (colon != NULL) {
            const gchar *q1 = strchr (colon + 1, '"');
            if (q1 != NULL) {
                const gchar *q2 = strchr (q1 + 1, '"');
                if (q2 != NULL && q2 > q1 + 1) {
                    g_autofree gchar *val = g_strndup (q1 + 1, (gsize)(q2 - q1 - 1));
                    PulsPowerState st = puls_power_state_from_string (val);
                    if (st != PULS_POWER_STATE_UNKNOWN)
                        return st;
                }
            }
        }
    }

    if (strstr (json_str, "STANDBY mode") != NULL ||
        strstr (json_str, "in STANDBY") != NULL)
        return PULS_POWER_STATE_STANDBY;

    if (strstr (json_str, "SLEEP mode") != NULL ||
        strstr (json_str, "in SLEEP") != NULL)
        return PULS_POWER_STATE_SLEEP;

    if (strstr (json_str, "ACTIVE or IDLE") != NULL)
        return PULS_POWER_STATE_ACTIVE;

    return PULS_POWER_STATE_UNKNOWN;
}

/* =========================================================================
 * Polkit Policy XML Introspection & Validation
 * ========================================================================= */

/**
 * puls_polkit_action_info_free:
 * @info: (nullable): A #PulsPolkitActionInfo to free.
 *
 * Releases all memory allocated for a parsed Polkit `<action>` entry.
 */
void
puls_polkit_action_info_free (PulsPolkitActionInfo *info)
{
    if (info == NULL)
        return;

    g_free (info->id);
    g_free (info->description_default);
    g_free (info->description_en);
    g_free (info->description_tr);
    g_free (info->message_default);
    g_free (info->message_en);
    g_free (info->message_tr);
    g_free (info->icon_name);
    g_free (info->allow_any);
    g_free (info->allow_inactive);
    g_free (info->allow_active);
    g_free (info->exec_path);
    g_free (info->allow_gui);
    g_free (info->imply);
    g_free (info);
}

static gboolean
polkit_default_value_is_valid (const gchar *val)
{
    if (val == NULL)
        return FALSE;

    return (g_strcmp0 (val, "no") == 0 ||
            g_strcmp0 (val, "auth_self") == 0 ||
            g_strcmp0 (val, "auth_admin") == 0 ||
            g_strcmp0 (val, "auth_self_keep") == 0 ||
            g_strcmp0 (val, "auth_admin_keep") == 0 ||
            g_strcmp0 (val, "yes") == 0);
}

static gboolean
polkit_action_id_is_valid (const gchar *id)
{
    if (id == NULL || *id == '\0')
        return FALSE;

    gboolean has_dot = FALSE;
    for (const gchar *p = id; *p != '\0'; p++) {
        guchar c = (guchar)*p;
        if (c == '.') {
            has_dot = TRUE;
            continue;
        }
        if (!g_ascii_isalnum (c) && c != '-' && c != '_')
            return FALSE;
    }

    return has_dot;
}

typedef struct {
    gboolean              saw_policyconfig;
    gboolean              in_policyconfig;
    gboolean              in_defaults;
    gboolean              capture_text;
    PulsPolkitActionInfo *current_action;
    GPtrArray            *actions;
    GHashTable           *seen_ids;
    gchar                *current_lang;
    gchar                *current_annotate_key;
    GString              *text_buf;
} PolkitParseCtx;

static void
polkit_xml_start_element (GMarkupParseContext  *context G_GNUC_UNUSED,
                          const gchar          *element_name,
                          const gchar         **attribute_names,
                          const gchar         **attribute_values,
                          gpointer              user_data,
                          GError              **error)
{
    PolkitParseCtx *ctx = (PolkitParseCtx *)user_data;

    if (g_strcmp0 (element_name, "policyconfig") == 0) {
        if (ctx->in_policyconfig || ctx->saw_policyconfig) {
            g_set_error_literal (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                 "Nested or duplicate <policyconfig> element");
            return;
        }
        ctx->saw_policyconfig = TRUE;
        ctx->in_policyconfig = TRUE;
        return;
    }

    if (!ctx->in_policyconfig) {
        g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                     "Unexpected root element <%s>; expected <policyconfig>",
                     element_name);
        return;
    }

    if (g_strcmp0 (element_name, "action") == 0) {
        if (ctx->current_action != NULL) {
            g_set_error_literal (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                 "Nested <action> elements are not allowed");
            return;
        }

        const gchar *action_id = NULL;
        for (gsize i = 0; attribute_names[i] != NULL; i++) {
            if (g_strcmp0 (attribute_names[i], "id") == 0)
                action_id = attribute_values[i];
        }

        if (!polkit_action_id_is_valid (action_id)) {
            g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                         "Invalid or missing Polkit action id '%s'",
                         action_id ? action_id : "(null)");
            return;
        }

        if (g_hash_table_contains (ctx->seen_ids, action_id)) {
            g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                         "Duplicate Polkit action id '%s'", action_id);
            return;
        }

        g_hash_table_add (ctx->seen_ids, g_strdup (action_id));
        ctx->current_action = g_new0 (PulsPolkitActionInfo, 1);
        ctx->current_action->id = g_strdup (action_id);
        return;
    }

    if (ctx->current_action == NULL)
        return;

    if (g_strcmp0 (element_name, "defaults") == 0) {
        ctx->in_defaults = TRUE;
        return;
    }

    if (g_strcmp0 (element_name, "description") == 0 ||
        g_strcmp0 (element_name, "message") == 0) {
        g_clear_pointer (&ctx->current_lang, g_free);
        for (gsize i = 0; attribute_names[i] != NULL; i++) {
            if (g_strcmp0 (attribute_names[i], "xml:lang") == 0)
                ctx->current_lang = g_strdup (attribute_values[i]);
        }
        g_string_truncate (ctx->text_buf, 0);
        ctx->capture_text = TRUE;
        return;
    }

    if (g_strcmp0 (element_name, "annotate") == 0) {
        const gchar *key = NULL;
        for (gsize i = 0; attribute_names[i] != NULL; i++) {
            if (g_strcmp0 (attribute_names[i], "key") == 0)
                key = attribute_values[i];
        }
        if (key == NULL || *key == '\0') {
            g_set_error_literal (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                 "<annotate> element missing required 'key' attribute");
            return;
        }
        g_clear_pointer (&ctx->current_annotate_key, g_free);
        ctx->current_annotate_key = g_strdup (key);
        g_string_truncate (ctx->text_buf, 0);
        ctx->capture_text = TRUE;
        return;
    }

    if (g_strcmp0 (element_name, "icon_name") == 0 ||
        (ctx->in_defaults &&
         (g_strcmp0 (element_name, "allow_any") == 0 ||
          g_strcmp0 (element_name, "allow_inactive") == 0 ||
          g_strcmp0 (element_name, "allow_active") == 0))) {
        g_string_truncate (ctx->text_buf, 0);
        ctx->capture_text = TRUE;
    }
}

static void
polkit_xml_text (GMarkupParseContext  *context G_GNUC_UNUSED,
                 const gchar          *text,
                 gsize                 text_len,
                 gpointer              user_data,
                 GError              **error G_GNUC_UNUSED)
{
    PolkitParseCtx *ctx = (PolkitParseCtx *)user_data;
    if (ctx->capture_text)
        g_string_append_len (ctx->text_buf, text, (gssize)text_len);
}

static void
polkit_xml_end_element (GMarkupParseContext  *context G_GNUC_UNUSED,
                        const gchar          *element_name,
                        gpointer              user_data,
                        GError              **error)
{
    PolkitParseCtx *ctx = (PolkitParseCtx *)user_data;

    if (g_strcmp0 (element_name, "policyconfig") == 0) {
        ctx->in_policyconfig = FALSE;
        return;
    }

    if (g_strcmp0 (element_name, "action") == 0) {
        if (ctx->current_action != NULL) {
            g_ptr_array_add (ctx->actions, ctx->current_action);
            ctx->current_action = NULL;
        }
        return;
    }

    if (ctx->current_action == NULL)
        return;

    if (g_strcmp0 (element_name, "defaults") == 0) {
        ctx->in_defaults = FALSE;
        return;
    }

    if (!ctx->capture_text)
        return;

    ctx->capture_text = FALSE;
    g_autofree gchar *val = g_strdup (ctx->text_buf->str);
    g_strstrip (val);

    if (g_strcmp0 (element_name, "description") == 0) {
        if (ctx->current_lang == NULL) {
            g_clear_pointer (&ctx->current_action->description_default, g_free);
            ctx->current_action->description_default = g_steal_pointer (&val);
        } else if (g_strcmp0 (ctx->current_lang, "en") == 0) {
            g_clear_pointer (&ctx->current_action->description_en, g_free);
            ctx->current_action->description_en = g_steal_pointer (&val);
        } else if (g_strcmp0 (ctx->current_lang, "tr") == 0) {
            g_clear_pointer (&ctx->current_action->description_tr, g_free);
            ctx->current_action->description_tr = g_steal_pointer (&val);
        }
        g_clear_pointer (&ctx->current_lang, g_free);
        return;
    }

    if (g_strcmp0 (element_name, "message") == 0) {
        if (ctx->current_lang == NULL) {
            g_clear_pointer (&ctx->current_action->message_default, g_free);
            ctx->current_action->message_default = g_steal_pointer (&val);
        } else if (g_strcmp0 (ctx->current_lang, "en") == 0) {
            g_clear_pointer (&ctx->current_action->message_en, g_free);
            ctx->current_action->message_en = g_steal_pointer (&val);
        } else if (g_strcmp0 (ctx->current_lang, "tr") == 0) {
            g_clear_pointer (&ctx->current_action->message_tr, g_free);
            ctx->current_action->message_tr = g_steal_pointer (&val);
        }
        g_clear_pointer (&ctx->current_lang, g_free);
        return;
    }

    if (g_strcmp0 (element_name, "icon_name") == 0) {
        g_clear_pointer (&ctx->current_action->icon_name, g_free);
        ctx->current_action->icon_name = g_steal_pointer (&val);
        return;
    }

    if (g_strcmp0 (element_name, "allow_any") == 0 ||
        g_strcmp0 (element_name, "allow_inactive") == 0 ||
        g_strcmp0 (element_name, "allow_active") == 0) {
        if (!polkit_default_value_is_valid (val)) {
            g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                         "Invalid Polkit <%s> value '%s'", element_name, val);
            return;
        }
        if (g_strcmp0 (element_name, "allow_any") == 0) {
            g_clear_pointer (&ctx->current_action->allow_any, g_free);
            ctx->current_action->allow_any = g_steal_pointer (&val);
        } else if (g_strcmp0 (element_name, "allow_inactive") == 0) {
            g_clear_pointer (&ctx->current_action->allow_inactive, g_free);
            ctx->current_action->allow_inactive = g_steal_pointer (&val);
        } else {
            g_clear_pointer (&ctx->current_action->allow_active, g_free);
            ctx->current_action->allow_active = g_steal_pointer (&val);
        }
        return;
    }

    if (g_strcmp0 (element_name, "annotate") == 0) {
        if (g_strcmp0 (ctx->current_annotate_key, "org.freedesktop.policykit.exec.path") == 0) {
            g_clear_pointer (&ctx->current_action->exec_path, g_free);
            ctx->current_action->exec_path = g_steal_pointer (&val);
        } else if (g_strcmp0 (ctx->current_annotate_key, "org.freedesktop.policykit.exec.allow_gui") == 0) {
            g_clear_pointer (&ctx->current_action->allow_gui, g_free);
            ctx->current_action->allow_gui = g_steal_pointer (&val);
        } else if (g_strcmp0 (ctx->current_annotate_key, "org.freedesktop.policykit.imply") == 0) {
            g_clear_pointer (&ctx->current_action->imply, g_free);
            ctx->current_action->imply = g_steal_pointer (&val);
        }
        g_clear_pointer (&ctx->current_annotate_key, g_free);
    }
}

/**
 * puls_polkit_parse_policy_xml:
 * @xml_content: Raw UTF-8 XML content of a `.policy` file.
 * @out_actions: (out) (optional) (element-type PulsPolkitActionInfo): Parsed
 *   array of #PulsPolkitActionInfo elements on success.
 * @error: (out) (optional): Return location for a #GError.
 *
 * Parses and validates the XML structure, action IDs, `<defaults>` tokens, and
 * `<annotate>` entries of a PolicyKit 1.0 `.policy` document.
 *
 * Returns: %TRUE on valid XML structure, %FALSE on parse or schema error.
 */
gboolean
puls_polkit_parse_policy_xml (const gchar  *xml_content,
                              GPtrArray   **out_actions,
                              GError      **error)
{
    if (out_actions != NULL)
        *out_actions = NULL;

    if (xml_content == NULL || *xml_content == '\0') {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Polkit policy XML is empty");
        return FALSE;
    }

    PolkitParseCtx ctx = {
        .actions  = g_ptr_array_new_with_free_func ((GDestroyNotify)puls_polkit_action_info_free),
        .seen_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL),
        .text_buf = g_string_new (NULL),
    };

    static const GMarkupParser parser = {
        .start_element = polkit_xml_start_element,
        .end_element   = polkit_xml_end_element,
        .text          = polkit_xml_text,
        .passthrough   = NULL,
        .error         = NULL,
    };

    GMarkupParseContext *mctx = g_markup_parse_context_new (&parser, 0, &ctx, NULL);
    gboolean ok = g_markup_parse_context_parse (mctx, xml_content, -1, error);
    if (ok)
        ok = g_markup_parse_context_end_parse (mctx, error);
    g_markup_parse_context_free (mctx);

    puls_polkit_action_info_free (ctx.current_action);
    g_hash_table_unref (ctx.seen_ids);
    g_free (ctx.current_lang);
    g_free (ctx.current_annotate_key);
    g_string_free (ctx.text_buf, TRUE);

    if (ok && !ctx.saw_policyconfig) {
        g_set_error_literal (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                             "Missing root <policyconfig> element");
        ok = FALSE;
    }

    if (!ok) {
        g_ptr_array_unref (ctx.actions);
        return FALSE;
    }

    if (out_actions != NULL)
        *out_actions = ctx.actions;
    else
        g_ptr_array_unref (ctx.actions);

    return TRUE;
}

/**
 * puls_polkit_validate_policy_xml:
 * @xml_content: Raw UTF-8 XML content of a PULS DiskInfo `.policy` file.
 * @out_actions: (out) (optional) (element-type PulsPolkitActionInfo): Parsed
 *   action array on success.
 * @error: (out) (optional): Return location for a #GError.
 *
 * Parses @xml_content and enforces PULS DiskInfo's security and localization
 * invariants:
 * - At least one `<action>` entry exists.
 * - Every action includes non-empty default, English (`en`), and Turkish (`tr`)
 *   `<description>` and `<message>` texts.
 * - Every action enforces `<allow_active>auth_admin_keep</allow_active>` for
 *   once-per-session administrator authentication and forbids passwordless `yes`.
 * - Exactly one action binds `org.freedesktop.policykit.exec.path` to
 *   `puls-diskinfo-helper` with `org.freedesktop.policykit.exec.allow_gui=false`.
 *
 * Returns: %TRUE if all syntax, localization, and security checks pass.
 */
gboolean
puls_polkit_validate_policy_xml (const gchar  *xml_content,
                                 GPtrArray   **out_actions,
                                 GError      **error)
{
    if (out_actions != NULL)
        *out_actions = NULL;

    g_autoptr(GPtrArray) actions = NULL;
    if (!puls_polkit_parse_policy_xml (xml_content, &actions, error))
        return FALSE;

    if (actions->len == 0) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Polkit policy defines no <action> entries");
        return FALSE;
    }

    guint helper_exec_bindings = 0;

    for (guint i = 0; i < actions->len; i++) {
        const PulsPolkitActionInfo *act = g_ptr_array_index (actions, i);

        if (act->description_default == NULL || *act->description_default == '\0' ||
            act->description_en == NULL || *act->description_en == '\0' ||
            act->description_tr == NULL || *act->description_tr == '\0') {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Polkit action '%s' is missing required default/en/tr <description>",
                         act->id);
            return FALSE;
        }

        if (act->message_default == NULL || *act->message_default == '\0' ||
            act->message_en == NULL || *act->message_en == '\0' ||
            act->message_tr == NULL || *act->message_tr == '\0') {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Polkit action '%s' is missing required default/en/tr <message>",
                         act->id);
            return FALSE;
        }

        if (g_strcmp0 (act->allow_active, "auth_admin_keep") != 0) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Polkit action '%s' must set <allow_active>auth_admin_keep</allow_active>",
                         act->id);
            return FALSE;
        }

        if (act->allow_any == NULL || g_strcmp0 (act->allow_any, "yes") == 0 ||
            act->allow_inactive == NULL || g_strcmp0 (act->allow_inactive, "yes") == 0) {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Polkit action '%s' has missing or overly permissive allow_any/allow_inactive",
                         act->id);
            return FALSE;
        }

        if (act->exec_path != NULL) {
            if (!g_str_has_prefix (act->exec_path, "/") ||
                !g_str_has_suffix (act->exec_path, "/puls-diskinfo-helper")) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Polkit action '%s' has unexpected exec.path '%s'",
                             act->id, act->exec_path);
                return FALSE;
            }
            if (g_strcmp0 (act->allow_gui, "false") != 0) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Polkit action '%s' must set exec.allow_gui=false for headless helper",
                             act->id);
                return FALSE;
            }
            helper_exec_bindings++;
        }
    }

    if (helper_exec_bindings != 1) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Expected exactly 1 helper exec.path binding in policy, found %u",
                     helper_exec_bindings);
        return FALSE;
    }

    if (out_actions != NULL)
        *out_actions = g_steal_pointer (&actions);

    return TRUE;
}

/**
 * puls_polkit_validate_policy_file:
 * @policy_path: Absolute path to a `.policy` XML file on disk.
 * @out_actions: (out) (optional) (element-type PulsPolkitActionInfo): Parsed
 *   action array on success.
 * @error: (out) (optional): Return location for a #GError.
 *
 * Loads @policy_path and validates it via puls_polkit_validate_policy_xml().
 *
 * Returns: %TRUE if valid, %FALSE otherwise.
 */
gboolean
puls_polkit_validate_policy_file (const gchar  *policy_path,
                                  GPtrArray   **out_actions,
                                  GError      **error)
{
    g_return_val_if_fail (policy_path != NULL, FALSE);

    g_autofree gchar *contents = NULL;
    if (!g_file_get_contents (policy_path, &contents, NULL, error))
        return FALSE;

    return puls_polkit_validate_policy_xml (contents, out_actions, error);
}


