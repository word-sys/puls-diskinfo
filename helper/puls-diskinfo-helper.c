/*
 * puls-diskinfo-helper.c
 *
 * Privileged helper for PULS DiskInfo.
 * Runs via pkexec to execute smartctl and read-only sector probing over a
 * strict versioned framed IPC protocol (Protocol v2).
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "puls-helper-protocol.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/**
 * is_valid_device:
 * @path: Device node path.
 *
 * Validates that a device path looks like a real storage device node.
 * Accepts whole-disk /dev/sd*, /dev/nvme*, /dev/hd*, /dev/vd*, /dev/xvd*,
 * /dev/mmcblk*, and /dev/sg* nodes via the strict protocol syntax whitelist.
 * Rejects traversal, shell metacharacters, partitions, and pseudo-devices.
 */
static int
is_valid_device (const char *path)
{
    return puls_helper_is_valid_device_syntax (path);
}

/**
 * verify_device_node:
 * @device: Candidate device path.
 * @out_st: (out) (optional): Populated stat buffer from lstat().
 * @out_err_code: (out): Protocol error token on failure.
 * @out_err_json: (out): Protocol JSON error payload on failure.
 *
 * Performs multi-layer path sanitization and anti-hijacking validation:
 * 1. Strict syntactic whitelist (`is_valid_device`).
 * 2. `lstat()` check ensuring the node exists, is NOT a symlink (`!S_ISLNK`),
 *    and IS a block or character special device (`S_ISBLK || S_ISCHR`).
 * 3. `realpath()` check ensuring canonical path equals @device exactly.
 *
 * Returns: 1 if valid and verified, 0 on error.
 */
static int
verify_device_node (const char  *device,
                    struct stat *out_st,
                    const char **out_err_code,
                    const char **out_err_json)
{
    if (!is_valid_device (device)) {
        if (out_err_code)
            *out_err_code = PULS_HELPER_ERR_INVALID_DEVICE;
        if (out_err_json)
            *out_err_json = "{\"error\":\"Invalid device path\"}\n";
        return 0;
    }

    struct stat lst;
    if (lstat (device, &lst) != 0) {
        if (errno == ENOENT) {
            if (out_err_code)
                *out_err_code = PULS_HELPER_ERR_NOT_FOUND;
            if (out_err_json)
                *out_err_json = "{\"error\":\"Device does not exist\"}\n";
        } else {
            if (out_err_code)
                *out_err_code = PULS_HELPER_ERR_INVALID_DEVICE;
            if (out_err_json)
                *out_err_json = "{\"error\":\"Cannot stat device node\"}\n";
        }
        return 0;
    }

    if (S_ISLNK (lst.st_mode) ||
        (!S_ISBLK (lst.st_mode) && !S_ISCHR (lst.st_mode))) {
        if (out_err_code)
            *out_err_code = PULS_HELPER_ERR_INVALID_DEVICE;
        if (out_err_json)
            *out_err_json = "{\"error\":\"Path is not a physical block/char device node\"}\n";
        return 0;
    }

    char resolved[PATH_MAX];
    if (realpath (device, resolved) == NULL || strcmp (resolved, device) != 0) {
        if (out_err_code)
            *out_err_code = PULS_HELPER_ERR_INVALID_DEVICE;
        if (out_err_json)
            *out_err_json = "{\"error\":\"Device path failed canonical realpath check\"}\n";
        return 0;
    }

    if (out_st != NULL)
        *out_st = lst;

    return 1;
}

static void
send_frame_ok (const char *payload, size_t len)
{
    printf ("%s %zu\n", PULS_HELPER_RESP_OK, len);
    if (len > 0 && payload != NULL) {
        (void)fwrite (payload, 1, len, stdout);
    }
    fflush (stdout);
}

static void
send_frame_err (const char *err_code, const char *json_payload)
{
    const char *msg = json_payload ? json_payload : "";
    size_t len = strlen (msg);
    printf ("%s %s %zu\n", PULS_HELPER_RESP_ERR, err_code, len);
    if (len > 0) {
        (void)fwrite (msg, 1, len, stdout);
    }
    fflush (stdout);
}

static int
parse_u64_strict (const char *str, uint64_t *out_val)
{
    if (str == NULL || *str == '\0' || !isdigit ((unsigned char)*str))
        return 0;

    errno = 0;
    char *end = NULL;
    unsigned long long val = strtoull (str, &end, 10);
    if (errno != 0 || end == str || *end != '\0')
        return 0;

    *out_val = (uint64_t)val;
    return 1;
}

static void
run_single_command (const char *device, const char *action)
{
    const char *err_code = NULL;
    const char *err_json = NULL;

    if (!verify_device_node (device, NULL, &err_code, &err_json)) {
        if (err_code != NULL && strcmp (err_code, PULS_HELPER_ERR_NOT_FOUND) == 0) {
            fprintf (stderr, "Error: Device does not exist: %s\n", device);
            exit (3);
        }
        fprintf (stderr, "Error: Invalid device path: %s\n", device);
        exit (2);
    }

    if (action == NULL) {
        execlp ("smartctl", "smartctl", "-a", "-j", device, (char *)NULL);
    } else if (strcmp (action, "standby") == 0) {
        execlp ("smartctl", "smartctl", "-n", "standby", "-a", "-j", device, (char *)NULL);
    } else if (strcmp (action, "short") == 0) {
        execlp ("smartctl", "smartctl", "-t", "short", device, (char *)NULL);
    } else if (strcmp (action, "long") == 0) {
        execlp ("smartctl", "smartctl", "-t", "long", device, (char *)NULL);
    } else if (strcmp (action, "abort") == 0) {
        execlp ("smartctl", "smartctl", "-X", device, (char *)NULL);
    } else {
        fprintf (stderr, "Error: Invalid action: %s\n", action);
        exit (5);
    }

    perror ("Failed to execute smartctl");
    exit (4);
}

static void
handle_smartctl_command (const char *device, const char *action)
{
    if (!is_valid_device (device)) {
        send_frame_err (PULS_HELPER_ERR_INVALID_DEVICE,
                        "{\"error\":\"Invalid device path\"}\n");
        return;
    }

    if (action != NULL &&
        strcmp (action, "standby") != 0 &&
        strcmp (action, "short") != 0 &&
        strcmp (action, "long") != 0 &&
        strcmp (action, "abort") != 0) {
        send_frame_err (PULS_HELPER_ERR_INVALID_ACTION,
                        "{\"error\":\"Invalid smartctl action\"}\n");
        return;
    }

    const char *err_code = NULL;
    const char *err_json = NULL;
    if (!verify_device_node (device, NULL, &err_code, &err_json)) {
        send_frame_err (err_code, err_json);
        return;
    }

    int pfd[2];
    if (pipe (pfd) == -1) {
        send_frame_err (PULS_HELPER_ERR_INTERNAL,
                        "{\"error\":\"Failed to create pipe\"}\n");
        return;
    }

    pid_t pid = fork ();
    if (pid == -1) {
        close (pfd[0]);
        close (pfd[1]);
        send_frame_err (PULS_HELPER_ERR_INTERNAL,
                        "{\"error\":\"Failed to fork child process\"}\n");
        return;
    }

    if (pid == 0) {
        close (pfd[0]);
        dup2 (pfd[1], STDOUT_FILENO);
        dup2 (pfd[1], STDERR_FILENO);
        close (pfd[1]);

        if (action == NULL) {
            execlp ("smartctl", "smartctl", "-a", "-j", device, (char *)NULL);
        } else if (strcmp (action, "standby") == 0) {
            execlp ("smartctl", "smartctl", "-n", "standby", "-a", "-j", device, (char *)NULL);
        } else if (strcmp (action, "short") == 0) {
            execlp ("smartctl", "smartctl", "-t", "short", device, (char *)NULL);
        } else if (strcmp (action, "long") == 0) {
            execlp ("smartctl", "smartctl", "-t", "long", device, (char *)NULL);
        } else if (strcmp (action, "abort") == 0) {
            execlp ("smartctl", "smartctl", "-X", device, (char *)NULL);
        }
        perror ("execlp");
        _exit (4);
    }

    close (pfd[1]);

    char *buf = NULL;
    size_t total_read = 0;
    char chunk[4096];
    ssize_t n_read;
    int truncated = 0;

    while ((n_read = read (pfd[0], chunk, sizeof (chunk))) > 0) {
        if (total_read + (size_t)n_read > PULS_HELPER_MAX_PAYLOAD_LEN) {
            truncated = 1;
            break;
        }
        char *new_buf = realloc (buf, total_read + (size_t)n_read + 1);
        if (!new_buf) {
            free (buf);
            close (pfd[0]);
            int status = 0;
            waitpid (pid, &status, 0);
            send_frame_err (PULS_HELPER_ERR_INTERNAL,
                            "{\"error\":\"Out of memory\"}\n");
            return;
        }
        buf = new_buf;
        memcpy (buf + total_read, chunk, (size_t)n_read);
        total_read += (size_t)n_read;
    }
    close (pfd[0]);

    int status = 0;
    waitpid (pid, &status, 0);

    if (truncated) {
        free (buf);
        send_frame_err (PULS_HELPER_ERR_IO,
                        "{\"error\":\"smartctl payload exceeded framing limit\"}\n");
        return;
    }

    if (buf != NULL) {
        buf[total_read] = '\0';
        send_frame_ok (buf, total_read);
        free (buf);
    } else {
        send_frame_ok ("", 0);
    }
}

static void
handle_stream_scan (const char *device, const char *start_str, const char *len_str)
{
    if (!is_valid_device (device)) {
        send_frame_err (PULS_HELPER_ERR_INVALID_DEVICE,
                        "{\"error\":\"Invalid device path\"}\n");
        return;
    }

    uint64_t start_off = 0;
    uint64_t read_len = 0;
    if (!parse_u64_strict (start_str, &start_off) ||
        !parse_u64_strict (len_str, &read_len) ||
        read_len == 0 ||
        read_len > PULS_HELPER_MAX_STREAM_SCAN_LEN) {
        send_frame_err (PULS_HELPER_ERR_INVALID_ARG,
                        "{\"error\":\"Invalid STREAM_SCAN range parameters\"}\n");
        return;
    }

    struct stat lst;
    const char *err_code = NULL;
    const char *err_json = NULL;
    if (!verify_device_node (device, &lst, &err_code, &err_json)) {
        send_frame_err (err_code, err_json);
        return;
    }

    int fd = open (device, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        send_frame_err (PULS_HELPER_ERR_IO,
                        "{\"error\":\"Failed to open device for read-only scan\"}\n");
        return;
    }

    struct stat fst;
    if (fstat (fd, &fst) != 0 ||
        (!S_ISBLK (fst.st_mode) && !S_ISCHR (fst.st_mode)) ||
        fst.st_ino != lst.st_ino ||
        fst.st_rdev != lst.st_rdev) {
        close (fd);
        send_frame_err (PULS_HELPER_ERR_INVALID_DEVICE,
                        "{\"error\":\"Opened descriptor failed device verification\"}\n");
        return;
    }

    struct timespec ts_start, ts_end;
    clock_gettime (CLOCK_MONOTONIC, &ts_start);

    char io_buf[65536];
    uint64_t remaining = read_len;
    uint64_t offset = start_off;
    uint64_t bytes_done = 0;
    int io_failed = 0;

    while (remaining > 0) {
        size_t want = (remaining > sizeof (io_buf)) ? sizeof (io_buf) : (size_t)remaining;
        ssize_t got = pread (fd, io_buf, want, (off_t)offset);
        if (got < 0) {
            if (errno == EINTR)
                continue;
            io_failed = 1;
            break;
        }
        if (got == 0)
            break;
        bytes_done += (uint64_t)got;
        offset += (uint64_t)got;
        remaining -= (uint64_t)got;
    }
    close (fd);

    clock_gettime (CLOCK_MONOTONIC, &ts_end);
    double elapsed_ms = ((double)(ts_end.tv_sec - ts_start.tv_sec) * 1000.0) +
                        ((double)(ts_end.tv_nsec - ts_start.tv_nsec) / 1000000.0);

    if (io_failed) {
        send_frame_err (PULS_HELPER_ERR_IO,
                        "{\"error\":\"Read error during STREAM_SCAN\"}\n");
        return;
    }

    char resp[512];
    int n = snprintf (resp, sizeof (resp),
                      "{\"status\":\"ok\",\"device\":\"%s\",\"start\":%" PRIu64
                      ",\"length\":%" PRIu64 ",\"bytes_read\":%" PRIu64
                      ",\"elapsed_ms\":%.3f}\n",
                      device, start_off, read_len, bytes_done, elapsed_ms);
    if (n < 0 || (size_t)n >= sizeof (resp)) {
        send_frame_err (PULS_HELPER_ERR_INTERNAL,
                        "{\"error\":\"Failed to format STREAM_SCAN response\"}\n");
        return;
    }

    send_frame_ok (resp, (size_t)n);
}

static void
handle_hello (const char *version_arg, int *handshake_done)
{
    if (version_arg == NULL || strcmp (version_arg, PULS_HELPER_PROTO_TAG) != 0) {
        send_frame_err (PULS_HELPER_ERR_VERSION,
                        "{\"error\":\"Unsupported protocol version\",\"expected\":\"" PULS_HELPER_PROTO_TAG "\"}\n");
        return;
    }

    *handshake_done = 1;
    const char *ok_json =
        "{\"protocol\":\"" PULS_HELPER_PROTO_TAG "\","
        "\"version\":2,"
        "\"capabilities\":[\"READ_SMART\",\"ACTION\",\"STREAM_SCAN\"]}\n";
    send_frame_ok (ok_json, strlen (ok_json));
}

static size_t
tokenize_line (char *line, char *tokens[], size_t max_tokens)
{
    size_t count = 0;
    char *p = line;

    while (*p != '\0' && count < max_tokens) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        tokens[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }

    return count;
}

/* Returns 0 to continue processing, 1 on QUIT. */
static int
dispatch_protocol_line (char *line, int *handshake_done)
{
    char *toks[6] = { NULL };
    size_t n_toks = tokenize_line (line, toks, 6);

    if (n_toks == 0)
        return 0;

    if (strcmp (toks[0], PULS_HELPER_CMD_QUIT) == 0) {
        send_frame_ok ("", 0);
        return 1;
    }

    if (strcmp (toks[0], PULS_HELPER_CMD_HELLO) == 0) {
        if (n_toks != 2) {
            handle_hello (NULL, handshake_done);
        } else {
            handle_hello (toks[1], handshake_done);
        }
        return 0;
    }

    if (!(*handshake_done)) {
        send_frame_err (PULS_HELPER_ERR_HANDSHAKE_REQ,
                        "{\"error\":\"Protocol handshake required (send HELLO " PULS_HELPER_PROTO_TAG ")\"}\n");
        return 0;
    }

    if (strcmp (toks[0], PULS_HELPER_CMD_READ_SMART) == 0) {
        if (n_toks == 2) {
            handle_smartctl_command (toks[1], NULL);
        } else if (n_toks == 3 && strcmp (toks[2], "standby") == 0) {
            handle_smartctl_command (toks[1], "standby");
        } else {
            send_frame_err (PULS_HELPER_ERR_INVALID_ARG,
                            "{\"error\":\"Usage: READ_SMART <dev> [standby]\"}\n");
        }
        return 0;
    }

    if (strcmp (toks[0], PULS_HELPER_CMD_ACTION) == 0) {
        if (n_toks != 3) {
            send_frame_err (PULS_HELPER_ERR_INVALID_ARG,
                            "{\"error\":\"Usage: ACTION <dev> <short|long|abort>\"}\n");
        } else {
            handle_smartctl_command (toks[1], toks[2]);
        }
        return 0;
    }

    if (strcmp (toks[0], PULS_HELPER_CMD_STREAM_SCAN) == 0) {
        if (n_toks != 4) {
            send_frame_err (PULS_HELPER_ERR_INVALID_ARG,
                            "{\"error\":\"Usage: STREAM_SCAN <dev> <start> <len>\"}\n");
        } else {
            handle_stream_scan (toks[1], toks[2], toks[3]);
        }
        return 0;
    }

    send_frame_err (PULS_HELPER_ERR_UNKNOWN_CMD,
                    "{\"error\":\"Unknown command\"}\n");
    return 0;
}

int
main (int argc, char *argv[])
{
    if (argc >= 2) {
        const char *device = argv[1];
        const char *action = (argc == 3) ? argv[2] : NULL;
        run_single_command (device, action);
        return 0;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    int handshake_done = 0;

    while ((line_len = getline (&line, &line_cap, stdin)) != -1) {
        if ((size_t)line_len > PULS_HELPER_MAX_LINE_LEN) {
            send_frame_err (PULS_HELPER_ERR_INVALID_ARG,
                            "{\"error\":\"Command line exceeds maximum length\"}\n");
            continue;
        }

        while (line_len > 0 &&
               (line[line_len - 1] == '\n' ||
                line[line_len - 1] == '\r' ||
                line[line_len - 1] == ' ')) {
            line[line_len - 1] = '\0';
            line_len--;
        }

        if (line_len == 0)
            continue;

        if (dispatch_protocol_line (line, &handshake_done))
            break;
    }

    free (line);
    return 0;
}
