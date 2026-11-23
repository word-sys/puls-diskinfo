/*
 * test-helper.c
 *
 * Standalone privileged helper test harness and deterministic fuzzing suite.
 * Exercises Protocol v2 framing, bounds checking, path sanitization, command
 * whitelisting, CLI argument validation, and memory safety under malformed
 * and adversarial inputs.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>
#include <string.h>

#include "../helper/puls-helper-protocol.h"
#include "puls-utils.h"

#ifndef TEST_HELPER_BIN
#error "TEST_HELPER_BIN must be defined by tests/meson.build"
#endif

typedef struct {
    GSubprocess      *proc;
    GOutputStream    *in_stream;
    GDataInputStream *out_stream;
} HelperHarness;

static void
harness_start (HelperHarness *h)
{
    g_autoptr(GError) err = NULL;
    memset (h, 0, sizeof (*h));

    h->proc = g_subprocess_new (
        G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &err, TEST_HELPER_BIN, NULL);
    g_assert_no_error (err);
    g_assert_nonnull (h->proc);

    h->in_stream = g_subprocess_get_stdin_pipe (h->proc);
    h->out_stream = g_data_input_stream_new (g_subprocess_get_stdout_pipe (h->proc));
}

static void
harness_send_raw_and_read_frame (HelperHarness  *h,
                                 const char     *raw_bytes,
                                 gsize           raw_len,
                                 gboolean       *out_ok,
                                 gchar         **out_err_code,
                                 gchar         **out_payload)
{
    g_autoptr(GError) err = NULL;

    if (out_ok)
        *out_ok = FALSE;
    if (out_err_code)
        *out_err_code = NULL;
    if (out_payload)
        *out_payload = NULL;

    gsize written = 0;
    g_assert_true (g_output_stream_write_all (h->in_stream, raw_bytes, raw_len,
                                              &written, NULL, &err));
    g_assert_no_error (err);
    g_assert_cmpuint (written, ==, raw_len);

    g_autofree gchar *header = g_data_input_stream_read_line (h->out_stream, NULL, NULL, &err);
    g_assert_no_error (err);
    g_assert_nonnull (header);

    gboolean ok = FALSE;
    gchar *err_code = NULL;
    gsize payload_len = 0;
    g_assert_true (puls_helper_parse_frame_header (header, &ok, &err_code, &payload_len));

    gchar *buf = g_malloc (payload_len + 1);
    if (payload_len > 0) {
        gsize got = 0;
        g_assert_true (g_input_stream_read_all (G_INPUT_STREAM (h->out_stream),
                                                buf, payload_len, &got, NULL, &err));
        g_assert_no_error (err);
        g_assert_cmpuint (got, ==, payload_len);
    }
    buf[payload_len] = '\0';

    if (out_ok)
        *out_ok = ok;
    if (out_err_code)
        *out_err_code = err_code;
    else
        g_free (err_code);
    if (out_payload)
        *out_payload = buf;
    else
        g_free (buf);
}

static void
harness_send_line (HelperHarness  *h,
                   const gchar    *line,
                   gboolean       *out_ok,
                   gchar         **out_err_code,
                   gchar         **out_payload)
{
    harness_send_raw_and_read_frame (h, line, strlen (line), out_ok, out_err_code, out_payload);
}

static void
harness_handshake (HelperHarness *h)
{
    gboolean ok = FALSE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;

    harness_send_line (h, "HELLO v2\n", &ok, &err_code, &payload);
    g_assert_true (ok);
    g_assert_null (err_code);
    g_assert_nonnull (strstr (payload, "\"protocol\":\"v2\""));
}

static void
harness_quit_and_cleanup (HelperHarness *h)
{
    g_autoptr(GError) err = NULL;
    gboolean ok = FALSE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;

    harness_send_line (h, "QUIT\n", &ok, &err_code, &payload);
    g_assert_true (ok);
    g_assert_null (err_code);
    g_assert_cmpstr (payload, ==, "");

    g_assert_true (g_subprocess_wait_check (h->proc, NULL, &err));
    g_assert_no_error (err);

    g_clear_object (&h->out_stream);
    g_clear_object (&h->proc);
    h->in_stream = NULL;
}

/* =========================================================================
 * 1. Handshake & State Machine Tests
 * ========================================================================= */
static void
test_handshake_and_state_machine (void)
{
    HelperHarness h;
    harness_start (&h);

    gboolean ok = TRUE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;

    /* Pre-handshake operational commands must all fail with E_HANDSHAKE_REQUIRED */
    static const gchar * const pre_cmds[] = {
        "READ_SMART /dev/sda\n",
        "ACTION /dev/sda short\n",
        "STREAM_SCAN /dev/sda 0 4096\n",
        "CANCEL\n",
        "UNKNOWN_COMMAND\n",
        NULL
    };
    for (gsize i = 0; pre_cmds[i] != NULL; i++) {
        harness_send_line (&h, pre_cmds[i], &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_HANDSHAKE_REQ);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* Bad HELLO variants must all fail with E_VERSION_MISMATCH */
    static const gchar * const bad_hellos[] = {
        "HELLO\n",
        "HELLO v0\n",
        "HELLO v1\n",
        "HELLO v3\n",
        "HELLO V2\n",
        "HELLO 2\n",
        "HELLO v2 extra\n",
        NULL
    };
    for (gsize i = 0; bad_hellos[i] != NULL; i++) {
        harness_send_line (&h, bad_hellos[i], &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_VERSION);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* Valid handshake succeeds and advertises full capability set */
    harness_send_line (&h, "HELLO v2\n", &ok, &err_code, &payload);
    g_assert_true (ok);
    g_assert_null (err_code);
    g_assert_nonnull (strstr (payload, "\"protocol\":\"v2\""));
    g_assert_nonnull (strstr (payload, "\"version\":2"));
    g_assert_nonnull (strstr (payload, "\"streaming\":true"));
    g_assert_nonnull (strstr (payload, "\"chunk_frame\":\"CHUNK\""));
    g_clear_pointer (&payload, g_free);

    /* Re-handshake is idempotent */
    harness_handshake (&h);
    harness_quit_and_cleanup (&h);

    /* Abrupt EOF on stdin exits helper cleanly with status 0 */
    harness_start (&h);
    harness_handshake (&h);
    g_autoptr(GError) err = NULL;
    g_assert_true (g_output_stream_close (h.in_stream, NULL, &err));
    g_assert_no_error (err);
    g_assert_true (g_subprocess_wait_check (h.proc, NULL, &err));
    g_assert_no_error (err);
    g_clear_object (&h.out_stream);
    g_clear_object (&h.proc);
}

/* =========================================================================
 * 2. Line Bounds, O(1) Oversized Drain & Control Character Tests
 * ========================================================================= */
static void
test_line_bounds_and_control_chars (void)
{
    HelperHarness h;
    harness_start (&h);
    harness_handshake (&h);

    gboolean ok = TRUE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;

    /* Whitespace-only lines must be silently ignored without desynchronizing */
    g_autoptr(GError) err = NULL;
    static const char ws_lines[] = "\n   \n\t\t\r\n   \t  \n";
    gsize written = 0;
    g_assert_true (g_output_stream_write_all (h.in_stream, ws_lines,
                                              sizeof (ws_lines) - 1,
                                              &written, NULL, &err));
    g_assert_no_error (err);
    harness_handshake (&h);

    /* Oversized lines: 4097 B, 8192 B, and 131072 B (128 KiB DoS burst) */
    static const gsize big_sizes[] = {
        PULS_HELPER_MAX_LINE_LEN + 1,
        PULS_HELPER_MAX_LINE_LEN * 2,
        131072
    };
    for (gsize i = 0; i < G_N_ELEMENTS (big_sizes); i++) {
        gsize sz = big_sizes[i];
        g_autofree gchar *big = g_malloc (sz + 1);
        memset (big, 'A', sz);
        memcpy (big, "READ_SMART ", 11);
        big[sz - 1] = '\n';
        big[sz] = '\0';

        harness_send_raw_and_read_frame (&h, big, sz, &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* Embedded NUL byte attacks */
    static const char nul1[] = "HELLO v2\0extra\n";
    harness_send_raw_and_read_frame (&h, nul1, sizeof (nul1) - 1, &ok, &err_code, &payload);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    static const char nul2[] = "\0\0\0\n";
    harness_send_raw_and_read_frame (&h, nul2, sizeof (nul2) - 1, &ok, &err_code, &payload);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    /* Forbidden ASCII control chars (0x01..0x1F except HT/LF/CR) + DEL (0x7F) + high bytes */
    for (guint ch = 1; ch <= 0x1F; ch++) {
        if (ch == '\t' || ch == '\n' || ch == '\r')
            continue;
        char bad[32];
        int n = g_snprintf (bad, sizeof (bad), "HELLO v2%c\n", (char)ch);
        harness_send_raw_and_read_frame (&h, bad, (gsize)n, &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    static const unsigned char high_bytes[] = { 0x7F, 0x80, 0xC0, 0xFF };
    for (gsize i = 0; i < G_N_ELEMENTS (high_bytes); i++) {
        char bad[16] = "HELLO v2X\n";
        bad[8] = (char)high_bytes[i];
        harness_send_raw_and_read_frame (&h, bad, 10, &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* Helper remains completely synchronized after all malformed lines */
    harness_handshake (&h);
    harness_quit_and_cleanup (&h);
}

/* =========================================================================
 * 3. Path Injection & Traversal Matrix
 * ========================================================================= */
static void
test_path_injection_and_traversal_matrix (void)
{
    HelperHarness h;
    harness_start (&h);
    harness_handshake (&h);

    static const gchar * const bad_devs[] = {
        "/dev/../etc/passwd",
        "/dev/sda/../../etc/shadow",
        "/dev/./sda",
        "/dev//sda",
        "/dev/sda/",
        "../dev/sda",
        "/dev/sda;reboot",
        "/dev/sda|sh",
        "/dev/sda&&id",
        "/dev/sda$(id)",
        "/dev/sda`id`",
        "-h",
        "--version",
        "/dev/-a",
        "/dev/null",
        "/dev/zero",
        "/dev/mem",
        "/dev/kmem",
        "/dev/tty",
        "/dev/ptmx",
        "/dev/loop0",
        "/dev/dm-0",
        "/dev/md0",
        "/dev/sda1",
        "/dev/nvme0n1p1",
        "/dev/nvme0n0",
        "/dev/mmcblk0p1",
        "/dev/SDA",
        "/DEV/sda",
        "/dev/sdabcde",
        "/dev/nvme0000",
        "/dev/sdaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        NULL
    };

    for (gsize i = 0; bad_devs[i] != NULL; i++) {
        gboolean ok = TRUE;
        g_autofree gchar *err_code = NULL;
        g_autofree gchar *payload = NULL;

        g_autofree gchar *c1 = g_strdup_printf ("READ_SMART %s\n", bad_devs[i]);
        harness_send_line (&h, c1, &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_DEVICE);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);

        g_autofree gchar *c2 = g_strdup_printf ("ACTION %s short\n", bad_devs[i]);
        harness_send_line (&h, c2, &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_DEVICE);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);

        g_autofree gchar *c3 = g_strdup_printf ("STREAM_SCAN %s 0 4096\n", bad_devs[i]);
        harness_send_line (&h, c3, &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_DEVICE);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    harness_quit_and_cleanup (&h);
}

/* =========================================================================
 * 4. Numeric Boundary & Integer Overflow Tests
 * ========================================================================= */
static void
test_numeric_boundary_and_overflow (void)
{
    HelperHarness h;
    harness_start (&h);
    harness_handshake (&h);

    static const gchar * const bad_numeric_cmds[] = {
        "STREAM_SCAN /dev/sdzz 0 0\n",
        "STREAM_SCAN /dev/sdzz -1 512\n",
        "STREAM_SCAN /dev/sdzz 0 -512\n",
        "STREAM_SCAN /dev/sdzz +0 512\n",
        "STREAM_SCAN /dev/sdzz 0x0 512\n",
        "STREAM_SCAN /dev/sdzz 0 0x200\n",
        "STREAM_SCAN /dev/sdzz 0 16777217\n",
        "STREAM_SCAN /dev/sdzz 18446744073709551615 512\n",
        "STREAM_SCAN /dev/sdzz 999999999999999999999999999999 512\n",
        "STREAM_SCAN /dev/sdzz 9223372036854775807 512\n",
        "STREAM_SCAN /dev/sdzz 9223372036854775800 512\n",
        "STREAM_SCAN /dev/sdzz 0 4096 0\n",
        "STREAM_SCAN /dev/sdzz 0 4096 511\n",
        "STREAM_SCAN /dev/sdzz 0 4096 1048577\n",
        "STREAM_SCAN /dev/sdzz 0 4096 abc\n",
        "STREAM_SCAN /dev/sdzz 0 4096 18446744073709551615\n",
        "STREAM_SCAN /dev/sdzz 0 4096 4096 extra\n",
        NULL
    };

    for (gsize i = 0; bad_numeric_cmds[i] != NULL; i++) {
        gboolean ok = TRUE;
        g_autofree gchar *err_code = NULL;
        g_autofree gchar *payload = NULL;

        harness_send_line (&h, bad_numeric_cmds[i], &ok, &err_code, &payload);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
    }

    /* Exact upper boundary (16 MiB total, 1 MiB chunk) passes numeric check -> E_DEVICE_NOT_FOUND */
    gboolean ok = TRUE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;
    harness_send_line (&h, "STREAM_SCAN /dev/sdzz 0 16777216 1048576\n", &ok, &err_code, &payload);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_NOT_FOUND);

    harness_quit_and_cleanup (&h);
}

/* =========================================================================
 * 5. Legacy Single-Shot CLI Harness
 * ========================================================================= */
static gint
run_helper_cli (const gchar * const *argv)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GSubprocess) p = g_subprocess_newv (
        argv,
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &err);
    g_assert_no_error (err);
    g_assert_true (g_subprocess_wait (p, NULL, &err));
    g_assert_no_error (err);
    g_assert_true (g_subprocess_get_if_exited (p));
    return g_subprocess_get_exit_status (p);
}

static void
test_cli_single_shot_harness (void)
{
    const gchar *bad_dev1[]  = { TEST_HELPER_BIN, "/dev/../etc/passwd", NULL };
    const gchar *bad_dev2[]  = { TEST_HELPER_BIN, "/dev/sda;id", NULL };
    const gchar *bad_dev3[]  = { TEST_HELPER_BIN, "/dev/null", NULL };
    const gchar *missing[]   = { TEST_HELPER_BIN, "/dev/sdzz", NULL };
    const gchar *bad_sub1[]  = { TEST_HELPER_BIN, "/dev/sdzz", "format", NULL };
    const gchar *bad_sub2[]  = { TEST_HELPER_BIN, "/dev/sdzz", "conveyance", NULL };
    const gchar *bad_sub3[]  = { TEST_HELPER_BIN, "/dev/sdzz", "--version", NULL };
    const gchar *too_many[]  = { TEST_HELPER_BIN, "/dev/sdzz", "short", "extra", NULL };

    g_assert_cmpint (run_helper_cli (bad_dev1), ==, 2);
    g_assert_cmpint (run_helper_cli (bad_dev2), ==, 2);
    g_assert_cmpint (run_helper_cli (bad_dev3), ==, 2);
    g_assert_cmpint (run_helper_cli (missing),  ==, 3);
    g_assert_cmpint (run_helper_cli (bad_sub1), ==, 5);
    g_assert_cmpint (run_helper_cli (bad_sub2), ==, 5);
    g_assert_cmpint (run_helper_cli (bad_sub3), ==, 5);
    g_assert_cmpint (run_helper_cli (too_many), ==, 5);
}

/* =========================================================================
 * 6. Deterministic Live IPC Protocol Fuzzer (500+ Adversarial Mutations)
 * ========================================================================= */
static uint64_t
fuzz_next_u64 (uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 2685821657736338717ULL;
}

static void
test_deterministic_protocol_fuzzer (void)
{
    HelperHarness h;
    harness_start (&h);
    harness_handshake (&h);

    uint64_t rng = 0x50554C5346555A5AULL; /* "PULSFUZZ" */

    /* Fixed format-string & shell-metachar corpus */
    static const gchar * const format_and_shell_corpus[] = {
        "%s%s%s%s%n\n",
        "READ_SMART %s%s%s%n\n",
        "ACTION /dev/sdzz %p%p%p%n\n",
        "STREAM_SCAN /dev/sdzz %.9999d 512\n",
        "HELLO %n%n%n\n",
        "READ_SMART /dev/sdzz ;cat</etc/passwd\n",
        "ACTION /dev/sdzz short|id\n",
        "STREAM_SCAN /dev/sdzz 0 4096 $(id)\n",
        "   READ_SMART\t\t/dev/sdzz\tstandby   \r\n",
        "QUIT %s\n",
        "CANCEL %n\n",
        NULL
    };

    for (gsize i = 0; format_and_shell_corpus[i] != NULL; i++) {
        gboolean ok = FALSE;
        g_autofree gchar *err_code = NULL;
        g_autofree gchar *payload = NULL;
        harness_send_line (&h, format_and_shell_corpus[i], &ok, &err_code, &payload);
        /* Every single input must produce a valid framed response (no crash/desync) */
        if (!ok)
            g_assert_nonnull (err_code);
    }

    static const gchar * const verbs[] = {
        "HELLO", "READ_SMART", "ACTION", "STREAM_SCAN", "CANCEL",
        "EXEC", "WRITE", "FORMAT", "BADCMD", "read_smart"
    };
    static const gchar * const arg_pool[] = {
        "/dev/sdzz", "/dev/nvme99", "/dev/../etc/passwd", "/dev/null",
        "/dev/sda1", "/dev/sda;id", "standby", "short", "long", "abort",
        "format", "0", "1", "512", "4096", "-1", "99999999999999999999999",
        "v1", "v2", "v3", "%s%n", "--help", "''", "\"\""
    };

    /* 500 deterministic fuzzed IPC lines across 4 mutation families */
    for (guint iter = 0; iter < 500; iter++) {
        guint family = (guint)(fuzz_next_u64 (&rng) % 4);
        gboolean ok = FALSE;
        g_autofree gchar *err_code = NULL;
        g_autofree gchar *payload = NULL;

        if (family == 0) {
            /* Grammar-mutated command with 0..6 fuzzed tokens (avoiding real host devices) */
            GString *cmd = g_string_new (verbs[fuzz_next_u64 (&rng) % G_N_ELEMENTS (verbs)]);
            guint n_args = (guint)(fuzz_next_u64 (&rng) % 7);
            for (guint a = 0; a < n_args; a++) {
                g_string_append_c (cmd, (fuzz_next_u64 (&rng) & 1) ? ' ' : '\t');
                g_string_append (cmd, arg_pool[fuzz_next_u64 (&rng) % G_N_ELEMENTS (arg_pool)]);
            }
            if ((fuzz_next_u64 (&rng) & 3) == 0)
                g_string_append_c (cmd, '\r');
            g_string_append_c (cmd, '\n');

            harness_send_raw_and_read_frame (&h, cmd->str, cmd->len, &ok, &err_code, &payload);
            g_string_free (cmd, TRUE);
        } else if (family == 1) {
            /* Random printable ASCII garbage (1..256 bytes, guaranteed non-blank) */
            gsize len = 1 + (gsize)(fuzz_next_u64 (&rng) % 256);
            g_autofree char *buf = g_malloc (len + 2);
            buf[0] = 'X'; /* Guarantee non-whitespace so helper always emits a frame */
            for (gsize j = 1; j < len; j++) {
                buf[j] = (char)(0x20 + (fuzz_next_u64 (&rng) % 95));
            }
            buf[len] = '\n';
            harness_send_raw_and_read_frame (&h, buf, len + 1, &ok, &err_code, &payload);
        } else if (family == 2) {
            /* Raw binary / NUL / control-char injection (1..128 bytes, no embedded '\n') */
            gsize len = 2 + (gsize)(fuzz_next_u64 (&rng) % 126);
            g_autofree char *buf = g_malloc (len + 1);
            buf[0] = (char)0x00; /* Guaranteed forbidden byte -> E_INVALID_ARG */
            for (gsize j = 1; j < len; j++) {
                unsigned char b = (unsigned char)(fuzz_next_u64 (&rng) & 0xFF);
                if (b == '\n')
                    b = 0x01;
                buf[j] = (char)b;
            }
            buf[len] = '\n';
            harness_send_raw_and_read_frame (&h, buf, len + 1, &ok, &err_code, &payload);
            g_assert_false (ok);
            g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
        } else {
            /* Oversized line (4097..12288 bytes) */
            gsize len = PULS_HELPER_MAX_LINE_LEN + 1 + (gsize)(fuzz_next_u64 (&rng) % 8192);
            g_autofree char *buf = g_malloc (len + 1);
            memset (buf, 'Z', len);
            buf[len] = '\n';
            harness_send_raw_and_read_frame (&h, buf, len + 1, &ok, &err_code, &payload);
            g_assert_false (ok);
            g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
        }
    }

    /* Verify helper survived all 500+ adversarial fuzz frames completely intact */
    harness_handshake (&h);
    harness_quit_and_cleanup (&h);
}

/* =========================================================================
 * 7. In-Process Parser Unit Fuzzer (5,000 Iterations)
 * ========================================================================= */
static void
test_parser_unit_fuzzer (void)
{
    uint64_t rng = 0xC0FFEE1234567890ULL;

    for (guint iter = 0; iter < 5000; iter++) {
        gsize len = (gsize)(fuzz_next_u64 (&rng) % 192);
        char buf[193];
        for (gsize j = 0; j < len; j++) {
            unsigned char b = (unsigned char)(1 + (fuzz_next_u64 (&rng) % 255));
            buf[j] = (char)b;
        }
        buf[len] = '\0';

        /* Exercise every shared & client-side parser against fuzzed strings */
        (void)puls_helper_is_valid_device_syntax (buf);
        (void)puls_helper_is_valid_command (buf);
        (void)puls_helper_is_valid_read_option (buf);
        (void)puls_helper_is_valid_action (buf);
        (void)puls_helper_is_valid_cli_subcommand (buf);

        gboolean ok = FALSE;
        gchar *err_code = NULL;
        gsize plen = 0;
        if (puls_helper_parse_frame_header (buf, &ok, &err_code, &plen)) {
            g_assert_cmpuint (plen, <=, PULS_HELPER_MAX_PAYLOAD_LEN);
            g_free (err_code);
        } else {
            g_assert_null (err_code);
        }

        guint64 seq = 0;
        gsize clen = 0;
        if (puls_helper_parse_chunk_header (buf, &seq, &clen)) {
            g_assert_cmpuint (seq, >=, 1);
            g_assert_cmpuint (clen, <=, PULS_HELPER_MAX_PAYLOAD_LEN);
        }

        PulsHelperStreamChunk chunk;
        (void)puls_helper_parse_stream_chunk_json (buf, &chunk);
    }
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/helper/handshake-and-state-machine", test_handshake_and_state_machine);
    g_test_add_func ("/helper/line-bounds-and-control-chars", test_line_bounds_and_control_chars);
    g_test_add_func ("/helper/path-injection-and-traversal-matrix", test_path_injection_and_traversal_matrix);
    g_test_add_func ("/helper/numeric-boundary-and-overflow", test_numeric_boundary_and_overflow);
    g_test_add_func ("/helper/cli-single-shot-harness", test_cli_single_shot_harness);
    g_test_add_func ("/helper/deterministic-protocol-fuzzer", test_deterministic_protocol_fuzzer);
    g_test_add_func ("/helper/parser-unit-fuzzer", test_parser_unit_fuzzer);

    return g_test_run ();
}
