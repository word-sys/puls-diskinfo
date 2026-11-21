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

/* Framed Response Prefixes (Helper -> Client) */
#define PULS_HELPER_RESP_OK             "OK"
#define PULS_HELPER_RESP_ERR            "ERR"

/* Standardized Error Codes */
#define PULS_HELPER_ERR_HANDSHAKE_REQ   "E_HANDSHAKE_REQUIRED"
#define PULS_HELPER_ERR_VERSION         "E_VERSION_MISMATCH"
#define PULS_HELPER_ERR_UNKNOWN_CMD     "E_UNKNOWN_CMD"
#define PULS_HELPER_ERR_INVALID_ARG     "E_INVALID_ARG"
#define PULS_HELPER_ERR_INVALID_DEVICE  "E_INVALID_DEVICE"
#define PULS_HELPER_ERR_INVALID_ACTION  "E_INVALID_ACTION"
#define PULS_HELPER_ERR_NOT_FOUND       "E_DEVICE_NOT_FOUND"
#define PULS_HELPER_ERR_IO              "E_IO_ERROR"
#define PULS_HELPER_ERR_INTERNAL        "E_INTERNAL"
