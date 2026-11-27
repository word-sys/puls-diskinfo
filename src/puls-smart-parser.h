/*
 * puls-smart-parser.h
 *
 * Parses smartctl JSON output into PulsSmartData.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib.h>
#include "puls-smart-data.h"
#include "puls-utils.h"

G_BEGIN_DECLS

typedef enum {
    PULS_SMART_DEVTYPE_AUTO = 0,
    PULS_SMART_DEVTYPE_SAT,
    PULS_SMART_DEVTYPE_SNT_ASMEDIA,
    PULS_SMART_DEVTYPE_SNT_JMICRON,
    PULS_SMART_DEVTYPE_SNT_REALTEK,
} PulsSmartDeviceType;

#define PULS_SMART_USB_PROBE_MAX_STEPS 5

typedef gchar *(*PulsSmartProbeFunc) (const gchar         *device_path,
                                      PulsSmartDeviceType  dev_type,
                                      gpointer             user_data,
                                      GError             **error);

typedef struct {
    gint     raw_status;         /* -1 if unknown/missing, else 0..255 */
    guint    mask;               /* Bitmask of PulsSmartctlExitBit */
    gboolean has_cmdline_error;  /* Bit 0 (1) */
    gboolean has_open_failed;    /* Bit 1 (2) */
    gboolean has_checksum_error; /* Bit 2 (4) */
    gboolean has_disk_failing;   /* Bit 3 (8) */
    gboolean has_prefail_warn;   /* Bit 4 (16) */
    gboolean has_error_log;      /* Bit 5 (32) */
    gboolean has_selftest_error; /* Bit 6 (64) */
    gboolean is_fatal;           /* TRUE if cmdline/open error with no data bits */
} PulsSmartctlExitInfo;

PulsSmartData       *puls_smart_parser_parse_json        (const gchar             *json_str,
                                                          GError                 **error);
PulsSmartData       *puls_smart_parser_parse_json_with_recovery (const gchar      *json_str,
                                                                        gboolean         *out_recovered,
                                                                        guint            *out_schema_warnings,
                                                                        GError          **error);
gchar               *puls_smart_parser_recover_json_string (const gchar           *raw_input);
gboolean             puls_smart_parser_validate_schema   (const gchar             *json_str,
                                                          guint                   *out_warning_count,
                                                          GError                 **error);

void                 puls_smartctl_decode_exit_status    (gint                     exit_status,
                                                          PulsSmartctlExitInfo    *out_info);
gboolean             puls_smartctl_exit_status_is_fatal  (gint                     exit_status);
gchar               *puls_smartctl_format_exit_status    (gint                     exit_status);
gint                 puls_smart_parser_extract_exit_status (const gchar           *json_str);

const gchar         *puls_smart_device_type_to_flag      (PulsSmartDeviceType      dev_type);
PulsSmartDeviceType  puls_smart_device_type_from_usb_ids (const gchar             *vid,
                                                          const gchar             *pid,
                                                          const gchar             *bridge_hint);
guint                puls_smart_build_usb_probe_order    (const PulsUsbBridgeInfo *bridge_info,
                                                          const PulsSmartctlCaps  *caps,
                                                          PulsSmartDeviceType     *out_order,
                                                          guint                    max_len);
gboolean             puls_smart_json_has_device_data     (const gchar             *json_str);
gchar               *puls_smart_resolve_usb_bridge_json  (const gchar             *device_path,
                                                          const PulsUsbBridgeInfo *bridge_info,
                                                          const PulsSmartctlCaps  *caps,
                                                          const gchar             *initial_auto_json,
                                                          PulsSmartProbeFunc       probe_fn,
                                                          gpointer                 user_data,
                                                          PulsSmartDeviceType     *out_resolved_type,
                                                          GError                 **error);

G_END_DECLS
