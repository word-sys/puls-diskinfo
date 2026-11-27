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

PulsSmartData       *puls_smart_parser_parse_json        (const gchar             *json_str,
                                                          GError                 **error);

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
