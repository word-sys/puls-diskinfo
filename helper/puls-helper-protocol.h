/*
 * puls-helper-protocol.h
 *
 * Shared IPC framing constants and command definitions for the
 * PULS DiskInfo privileged helper (Protocol v2).
 *
 * Pure standard C header with zero external dependencies so it can be
 * shared between the minimal privileged helper and libpuls_core.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#define PULS_HELPER_PROTO_VERSION       2U
#define PULS_HELPER_PROTO_TAG           "v2"

/* Framing limits */
#define PULS_HELPER_MAX_LINE_LEN        4096U
#define PULS_HELPER_MAX_PAYLOAD_LEN     (4U * 1024U * 1024U)   /* 4 MiB */
#define PULS_HELPER_MAX_STREAM_SCAN_LEN (16U * 1024U * 1024U)  /* 16 MiB per probe */

/* Commands (Client -> Helper) */
#define PULS_HELPER_CMD_HELLO           "HELLO"
#define PULS_HELPER_CMD_READ_SMART      "READ_SMART"
#define PULS_HELPER_CMD_ACTION          "ACTION"
#define PULS_HELPER_CMD_STREAM_SCAN     "STREAM_SCAN"
#define PULS_HELPER_CMD_QUIT            "QUIT"

/* Whitelisted Subcommands / Options */
#define PULS_HELPER_READ_OPT_STANDBY    "standby"
#define PULS_HELPER_ACTION_SHORT        "short"
#define PULS_HELPER_ACTION_LONG         "long"
#define PULS_HELPER_ACTION_ABORT        "abort"

/* Framed Response Prefixes (Helper -> Client) */
#define PULS_HELPER_RESP_OK             "OK"
#define PULS_HELPER_RESP_ERR            "ERR"

/* Standardized Error Codes */
#define PULS_HELPER_ERR_HANDSHAKE_REQ   "E_HANDSHAKE_REQUIRED"
#define PULS_HELPER_ERR_VERSION         "E_VERSION_MISMATCH"
#define PULS_HELPER_ERR_UNKNOWN_CMD     "E_UNKNOWN_CMD"
#define PULS_HELPER_ERR_INVALID_SUBCMD  "E_INVALID_SUBCMD"
#define PULS_HELPER_ERR_INVALID_ARG     "E_INVALID_ARG"
#define PULS_HELPER_ERR_INVALID_DEVICE  "E_INVALID_DEVICE"
#define PULS_HELPER_ERR_INVALID_ACTION  "E_INVALID_ACTION"
#define PULS_HELPER_ERR_NOT_FOUND       "E_DEVICE_NOT_FOUND"
#define PULS_HELPER_ERR_IO              "E_IO_ERROR"
#define PULS_HELPER_ERR_INTERNAL        "E_INTERNAL"

#include <stddef.h>
#include <string.h>

static inline int
puls_helper_is_lower_alpha (char c)
{
    return (c >= 'a' && c <= 'z');
}

static inline int
puls_helper_is_digit (char c)
{
    return (c >= '0' && c <= '9');
}

static inline int
puls_helper_match_alpha_disk (const char *name, const char *prefix)
{
    size_t plen = strlen (prefix);
    if (strncmp (name, prefix, plen) != 0)
        return 0;

    const char *p = name + plen;
    size_t count = 0;
    while (*p != '\0') {
        if (!puls_helper_is_lower_alpha (*p))
            return 0;
        count++;
        p++;
    }

    return (count >= 1 && count <= 4);
}

static inline int
puls_helper_match_uint_suffix (const char *p, size_t max_digits, int allow_zero)
{
    if (!puls_helper_is_digit (*p))
        return 0;
    if (!allow_zero && *p == '0' && *(p + 1) == '\0')
        return 0;

    size_t digits = 0;
    while (*p != '\0') {
        if (!puls_helper_is_digit (*p))
            return 0;
        digits++;
        p++;
    }

    return (digits >= 1 && digits <= max_digits);
}

/**
 * puls_helper_is_valid_device_syntax:
 * @path: Candidate device path string.
 *
 * Strict whitelist validator for storage device node paths. Rejects directory
 * traversal (`..`), nested slashes, whitespace, shell metacharacters,
 * partitions, and pseudo devices.
 *
 * Accepted patterns:
 * - `/dev/sd[a-z]{1,4}`, `/dev/hd[a-z]{1,4}`, `/dev/vd[a-z]{1,4}`, `/dev/xvd[a-z]{1,4}`
 * - `/dev/nvme[0-9]{1,3}` (controller)
 * - `/dev/nvme[0-9]{1,3}(c[0-9]{1,3})?n[1-9][0-9]{0,3}` (namespace)
 * - `/dev/mmcblk[0-9]{1,3}`
 * - `/dev/sg[0-9]{1,4}`
 *
 * Returns: 1 if valid, 0 otherwise.
 */
static inline int
puls_helper_is_valid_device_syntax (const char *path)
{
    if (path == NULL)
        return 0;

    size_t len = strlen (path);
    if (len < 8 || len >= 32)
        return 0;

    if (strncmp (path, "/dev/", 5) != 0)
        return 0;

    const char *name = path + 5;

    /* Reject any non-lowercase-alphanumeric character (blocks /, ., ;, |, &, $, spaces, uppercase) */
    for (const char *p = name; *p != '\0'; p++) {
        if (!puls_helper_is_lower_alpha (*p) && !puls_helper_is_digit (*p))
            return 0;
    }

    if (puls_helper_match_alpha_disk (name, "sd") ||
        puls_helper_match_alpha_disk (name, "hd") ||
        puls_helper_match_alpha_disk (name, "vd") ||
        puls_helper_match_alpha_disk (name, "xvd"))
        return 1;

    if (strncmp (name, "mmcblk", 6) == 0)
        return puls_helper_match_uint_suffix (name + 6, 3, 1);

    if (strncmp (name, "sg", 2) == 0)
        return puls_helper_match_uint_suffix (name + 2, 4, 1);

    if (strncmp (name, "nvme", 4) == 0) {
        const char *p = name + 4;
        size_t ctrl_digits = 0;
        while (puls_helper_is_digit (*p)) {
            ctrl_digits++;
            p++;
        }
        if (ctrl_digits == 0 || ctrl_digits > 3)
            return 0;
        if (*p == '\0')
            return 1; /* /dev/nvme<N> controller */

        if (*p == 'c') {
            p++;
            size_t c_digits = 0;
            while (puls_helper_is_digit (*p)) {
                c_digits++;
                p++;
            }
            if (c_digits == 0 || c_digits > 3)
                return 0;
        }

        if (*p != 'n')
            return 0;
        p++;
        if (*p == '0')
            return 0; /* NSID must be >= 1 */
        return puls_helper_match_uint_suffix (p, 4, 0);
    }

    return 0;
}

/**
 * puls_helper_is_valid_command:
 * @cmd: Top-level protocol command verb.
 *
 * Strictly validates @cmd against the minimal privileged helper whitelist:
 * `HELLO`, `READ_SMART`, `ACTION`, `STREAM_SCAN`, `QUIT`.
 *
 * Returns: 1 if whitelisted, 0 otherwise.
 */
static inline int
puls_helper_is_valid_command (const char *cmd)
{
    if (cmd == NULL)
        return 0;

    return (strcmp (cmd, PULS_HELPER_CMD_HELLO) == 0 ||
            strcmp (cmd, PULS_HELPER_CMD_READ_SMART) == 0 ||
            strcmp (cmd, PULS_HELPER_CMD_ACTION) == 0 ||
            strcmp (cmd, PULS_HELPER_CMD_STREAM_SCAN) == 0 ||
            strcmp (cmd, PULS_HELPER_CMD_QUIT) == 0);
}

/**
 * puls_helper_is_valid_read_option:
 * @opt: Optional subcommand modifier for `READ_SMART`.
 *
 * Strictly allows only `NULL` (default SMART read) or `"standby"` (`-n standby`).
 *
 * Returns: 1 if whitelisted, 0 otherwise.
 */
static inline int
puls_helper_is_valid_read_option (const char *opt)
{
    if (opt == NULL)
        return 1;

    return (strcmp (opt, PULS_HELPER_READ_OPT_STANDBY) == 0);
}

/**
 * puls_helper_is_valid_action:
 * @action: Self-test action subcommand for `ACTION`.
 *
 * Strictly allows only the auditable self-test subset:
 * `"short"`, `"long"`, and `"abort"`.
 *
 * Returns: 1 if whitelisted, 0 otherwise.
 */
static inline int
puls_helper_is_valid_action (const char *action)
{
    if (action == NULL)
        return 0;

    return (strcmp (action, PULS_HELPER_ACTION_SHORT) == 0 ||
            strcmp (action, PULS_HELPER_ACTION_LONG) == 0 ||
            strcmp (action, PULS_HELPER_ACTION_ABORT) == 0);
}

/**
 * puls_helper_is_valid_cli_subcommand:
 * @subcmd: Optional subcommand for single-shot CLI invocation.
 *
 * Strictly allows `NULL` (SMART read), `"standby"`, `"short"`, `"long"`, or `"abort"`.
 *
 * Returns: 1 if whitelisted, 0 otherwise.
 */
static inline int
puls_helper_is_valid_cli_subcommand (const char *subcmd)
{
    if (subcmd == NULL)
        return 1;

    return (puls_helper_is_valid_read_option (subcmd) ||
            puls_helper_is_valid_action (subcmd));
}
