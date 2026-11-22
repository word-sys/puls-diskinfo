/*
 * puls-helper-stream.h
 *
 * Pure POSIX C bidirectional chunked streaming engine for PULS DiskInfo
 * Protocol v2 (`CHUNK <seq> <len>` telemetry frames + non-blocking `CANCEL`).
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "puls-helper-protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline int
puls_helper_stream_write_ok (FILE *out_fp, const char *payload, size_t len)
{
    if (fprintf (out_fp, "%s %zu\n", PULS_HELPER_RESP_OK, len) < 0)
        return 0;
    if (len > 0 && payload != NULL) {
        if (fwrite (payload, 1, len, out_fp) != len)
            return 0;
    }
    return (fflush (out_fp) == 0 && !ferror (out_fp));
}

static inline int
puls_helper_stream_write_err (FILE *out_fp, const char *err_code, const char *json_payload)
{
    const char *msg = json_payload ? json_payload : "";
    size_t len = strlen (msg);
    if (fprintf (out_fp, "%s %s %zu\n", PULS_HELPER_RESP_ERR, err_code, len) < 0)
        return 0;
    if (len > 0) {
        if (fwrite (msg, 1, len, out_fp) != len)
            return 0;
    }
    return (fflush (out_fp) == 0 && !ferror (out_fp));
}

static inline int
puls_helper_stream_write_chunk (FILE *out_fp, uint64_t seq, const char *payload, size_t len)
{
    if (fprintf (out_fp, "%s %" PRIu64 " %zu\n", PULS_HELPER_RESP_CHUNK, seq, len) < 0)
        return 0;
    if (len > 0 && payload != NULL) {
        if (fwrite (payload, 1, len, out_fp) != len)
            return 0;
    }
    return (fflush (out_fp) == 0 && !ferror (out_fp));
}

/**
 * puls_helper_stream_poll_cancel:
 * @ctrl_fd: Input control pipe file descriptor (e.g. `STDIN_FILENO`), or -1.
 *
 * Non-blocking check for bidirectional client cancellation (`CANCEL\n`) or
 * client disconnect (`POLLHUP`/`POLLERR`) between streaming chunks.
 *
 * Returns: 1 if stream should abort immediately, 0 to continue.
 */
static inline int
puls_helper_stream_poll_cancel (int ctrl_fd)
{
    if (ctrl_fd < 0)
        return 0;

    struct pollfd pfd;
    pfd.fd = ctrl_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc;
    do {
        rc = poll (&pfd, 1, 0);
    } while (rc < 0 && errno == EINTR);

    if (rc <= 0)
        return 0;

    if ((pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 &&
        (pfd.revents & POLLIN) == 0) {
        return 1;
    }

    if ((pfd.revents & POLLIN) != 0) {
        char buf[64];
        size_t pos = 0;

        while (pos + 1 < sizeof (buf)) {
            char ch = '\0';
            ssize_t got = read (ctrl_fd, &ch, 1);
            if (got < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (got == 0)
                return 1; /* EOF on client stdin */
            if (ch == '\n')
                break;
            buf[pos++] = ch;

            /* Wait briefly only if mid-line so we consume the complete token */
            pfd.revents = 0;
            if (poll (&pfd, 1, 20) <= 0 || (pfd.revents & POLLIN) == 0)
                break;
        }

        while (pos > 0 &&
               (buf[pos - 1] == '\r' || buf[pos - 1] == ' ' || buf[pos - 1] == '\t')) {
            pos--;
        }
        buf[pos] = '\0';

        if (strcmp (buf, PULS_HELPER_CMD_CANCEL) == 0 ||
            strcmp (buf, PULS_HELPER_CMD_QUIT) == 0) {
            return 1;
        }
    }

    return 0;
}

static inline double
puls_helper_elapsed_ms (const struct timespec *start, const struct timespec *end)
{
    double ms = ((double)(end->tv_sec - start->tv_sec) * 1000.0) +
                ((double)(end->tv_nsec - start->tv_nsec) / 1000000.0);
    return (ms > 0.0) ? ms : 0.0;
}

/**
 * puls_helper_stream_read_fd:
 * @data_fd: Already-verified read-only file descriptor (`O_RDONLY | O_NOFOLLOW`).
 * @ctrl_fd: Client control pipe (`STDIN_FILENO`) polled non-blockingly for `CANCEL`.
 * @out_fp: Framed output stream (`stdout`).
 * @device_label: Sanitized device label included in JSON telemetry.
 * @start_off: Starting byte offset.
 * @read_len: Total bytes to read (`1 .. PULS_HELPER_MAX_STREAM_SCAN_LEN`).
 * @chunk_bytes: Telemetry chunk size (`PULS_HELPER_MIN_STREAM_CHUNK .. PULS_HELPER_MAX_STREAM_CHUNK`).
 * @inter_chunk_delay_us: Optional pacing delay in microseconds (0 in production).
 *
 * Streams continuous read-only sector/throughput telemetry chunks as framed
 * `CHUNK <seq> <len>` messages followed by a terminal `OK` or `ERR` frame.
 *
 * Returns: 1 if completed cleanly (OK), 0 if cancelled or failed (ERR emitted).
 */
static inline int
puls_helper_stream_read_fd (int         data_fd,
                            int         ctrl_fd,
                            FILE       *out_fp,
                            const char *device_label,
                            uint64_t    start_off,
                            uint64_t    read_len,
                            uint64_t    chunk_bytes,
                            unsigned    inter_chunk_delay_us)
{
    if (chunk_bytes < PULS_HELPER_MIN_STREAM_CHUNK ||
        chunk_bytes > PULS_HELPER_MAX_STREAM_CHUNK) {
        chunk_bytes = PULS_HELPER_DEFAULT_STREAM_CHUNK;
    }

    char *io_buf = (char *)malloc ((size_t)chunk_bytes);
    if (io_buf == NULL) {
        puls_helper_stream_write_err (out_fp, PULS_HELPER_ERR_INTERNAL,
                                      "{\"error\":\"Out of memory allocating stream buffer\"}\n");
        return 0;
    }

    struct timespec ts_total_start, ts_now;
    clock_gettime (CLOCK_MONOTONIC, &ts_total_start);

    uint64_t remaining = read_len;
    uint64_t offset = start_off;
    uint64_t bytes_done = 0;
    uint64_t seq = 0;
    int io_failed = 0;
    int cancelled = 0;

    while (remaining > 0) {
        if (puls_helper_stream_poll_cancel (ctrl_fd)) {
            cancelled = 1;
            break;
        }

        uint64_t chunk_target = (remaining > chunk_bytes) ? chunk_bytes : remaining;
        uint64_t chunk_start_off = offset;
        uint64_t chunk_got = 0;

        struct timespec ts_chunk_start, ts_chunk_end;
        clock_gettime (CLOCK_MONOTONIC, &ts_chunk_start);

        while (chunk_got < chunk_target) {
            size_t want = (size_t)(chunk_target - chunk_got);
            ssize_t got = pread (data_fd, io_buf + chunk_got, want, (off_t)offset);
            if (got < 0) {
                if (errno == EINTR)
                    continue;
                io_failed = 1;
                break;
            }
            if (got == 0)
                break; /* EOF */
            chunk_got += (uint64_t)got;
            offset += (uint64_t)got;
        }

        clock_gettime (CLOCK_MONOTONIC, &ts_chunk_end);

        if (io_failed)
            break;
        if (chunk_got == 0)
            break;

        bytes_done += chunk_got;
        remaining -= chunk_got;
        seq++;

        double chunk_ms = puls_helper_elapsed_ms (&ts_chunk_start, &ts_chunk_end);
        double progress_pct = ((double)bytes_done * 100.0) / (double)read_len;
        double dt_sec = (chunk_ms > 0.001) ? (chunk_ms / 1000.0) : 0.000001;
        double mb_per_sec = ((double)chunk_got / (1024.0 * 1024.0)) / dt_sec;

        char chunk_json[512];
        int cn = snprintf (chunk_json, sizeof (chunk_json),
                           "{\"seq\":%" PRIu64 ",\"device\":\"%s\",\"offset\":%" PRIu64
                           ",\"chunk_bytes\":%" PRIu64 ",\"bytes_read\":%" PRIu64
                           ",\"total_bytes\":%" PRIu64 ",\"progress\":%.2f"
                           ",\"chunk_ms\":%.3f,\"mb_per_sec\":%.2f}\n",
                           seq, device_label, chunk_start_off, chunk_got,
                           bytes_done, read_len, progress_pct, chunk_ms, mb_per_sec);
        if (cn < 0 || (size_t)cn >= sizeof (chunk_json) ||
            !puls_helper_stream_write_chunk (out_fp, seq, chunk_json, (size_t)cn)) {
            io_failed = 1;
            break;
        }

        if (remaining > 0 && inter_chunk_delay_us > 0) {
            struct timespec req;
            req.tv_sec = (time_t)(inter_chunk_delay_us / 1000000U);
            req.tv_nsec = (long)((inter_chunk_delay_us % 1000000U) * 1000U);
            nanosleep (&req, NULL);
        }

        if (remaining > 0 && puls_helper_stream_poll_cancel (ctrl_fd)) {
            cancelled = 1;
            break;
        }
    }

    free (io_buf);
    clock_gettime (CLOCK_MONOTONIC, &ts_now);
    double total_ms = puls_helper_elapsed_ms (&ts_total_start, &ts_now);

    if (cancelled) {
        char cancel_json[384];
        int n = snprintf (cancel_json, sizeof (cancel_json),
                          "{\"status\":\"cancelled\",\"device\":\"%s\",\"bytes_read\":%" PRIu64
                          ",\"total_bytes\":%" PRIu64 ",\"chunks\":%" PRIu64
                          ",\"elapsed_ms\":%.3f}\n",
                          device_label, bytes_done, read_len, seq, total_ms);
        if (n > 0 && (size_t)n < sizeof (cancel_json))
            puls_helper_stream_write_err (out_fp, PULS_HELPER_ERR_CANCELLED, cancel_json);
        else
            puls_helper_stream_write_err (out_fp, PULS_HELPER_ERR_CANCELLED,
                                          "{\"status\":\"cancelled\"}\n");
        return 0;
    }

    if (io_failed) {
        puls_helper_stream_write_err (out_fp, PULS_HELPER_ERR_IO,
                                      "{\"error\":\"Read error during STREAM_SCAN\"}\n");
        return 0;
    }

    double total_sec = (total_ms > 0.001) ? (total_ms / 1000.0) : 0.000001;
    double avg_mb_s = ((double)bytes_done / (1024.0 * 1024.0)) / total_sec;

    char resp[512];
    int n = snprintf (resp, sizeof (resp),
                      "{\"status\":\"ok\",\"device\":\"%s\",\"start\":%" PRIu64
                      ",\"length\":%" PRIu64 ",\"bytes_read\":%" PRIu64
                      ",\"chunks\":%" PRIu64 ",\"elapsed_ms\":%.3f"
                      ",\"avg_mb_per_sec\":%.2f}\n",
                      device_label, start_off, read_len, bytes_done, seq, total_ms, avg_mb_s);
    if (n < 0 || (size_t)n >= sizeof (resp)) {
        puls_helper_stream_write_err (out_fp, PULS_HELPER_ERR_INTERNAL,
                                      "{\"error\":\"Failed to format STREAM_SCAN response\"}\n");
        return 0;
    }

    return puls_helper_stream_write_ok (out_fp, resp, (size_t)n);
}
