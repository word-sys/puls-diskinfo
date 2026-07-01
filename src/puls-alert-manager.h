/*
 * puls-alert-manager.h
 *
 * Alert manager — watches PulsSmartData updates and fires alerts for:
 *  - Temperature exceeding threshold
 *  - Health % dropping below threshold
 *  - Critical SMART attributes worsening (reallocated/pending/uncorrectable sectors)
 *  - Self-test failures
 *
 * Alerts are delivered as AdwToast notifications in the main window.
 * The alert manager holds a session-only log of all fired alerts.
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
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PULS_TYPE_ALERT_MANAGER (puls_alert_manager_get_type ())
G_DECLARE_FINAL_TYPE (PulsAlertManager, puls_alert_manager, PULS, ALERT_MANAGER, GObject)

typedef struct {
    gchar    *message;
    gchar    *device_path;
    GDateTime *timestamp;
} PulsAlertEntry;

PulsAlertManager *puls_alert_manager_get_default (void);

/*
 * Called after each refresh with the latest data.
 * Returns a GPtrArray of newly-fired alert messages (gchar*, caller owns the array;
 * the strings themselves are owned by the log and must NOT be freed by the caller).
 * Returns NULL (or empty array) when no new alerts fired.
 */
GPtrArray *puls_alert_manager_check (PulsAlertManager *self,
                                      PulsSmartData    *data);

/*
 * Retrieve the full session log as a GPtrArray of PulsAlertEntry*.
 * The array and its contents are owned by the manager — do not free.
 */
GPtrArray *puls_alert_manager_get_log (PulsAlertManager *self);

void puls_alert_manager_set_temp_threshold   (PulsAlertManager *self, gint celsius);
void puls_alert_manager_set_health_threshold (PulsAlertManager *self, gint percent);

G_END_DECLS
