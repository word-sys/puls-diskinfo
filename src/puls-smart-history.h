/*
 * puls-smart-history.h
 *
 * Session-only S.M.A.R.T. attribute snapshot store.
 * Keeps the last two snapshots per device to derive trend arrows
 * (↑ improving, ↓ degrading, — stable) for the SMART table.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "puls-smart-data.h"

G_BEGIN_DECLS

typedef enum {
    PULS_ATTR_TREND_STABLE    = 0,
    PULS_ATTR_TREND_IMPROVING = 1,
    PULS_ATTR_TREND_DEGRADING = 2,
} PulsAttrTrend;

#define PULS_TYPE_SMART_HISTORY (puls_smart_history_get_type ())
G_DECLARE_FINAL_TYPE (PulsSmartHistory, puls_smart_history, PULS, SMART_HISTORY, GObject)

PulsSmartHistory *puls_smart_history_get_default (void);

void puls_smart_history_record (PulsSmartHistory *self,
                                 PulsSmartData    *data);
PulsAttrTrend puls_smart_history_get_trend (PulsSmartHistory *self,
                                             const gchar      *device_path,
                                             guint8            attr_id);

G_END_DECLS
