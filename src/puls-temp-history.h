/*
 * puls-temp-history.h
 *
 * Session-only temperature history circular buffer.
 * Stores (timestamp, temp_celsius) pairs for the current session.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PULS_TEMP_HISTORY_MAX_SAMPLES 480   /* 8 hours at 1 sample/minute */

typedef struct {
    gint64 timestamp_us;  
    gint   temp_celsius;
} PulsTempSample;

#define PULS_TYPE_TEMP_HISTORY (puls_temp_history_get_type ())
G_DECLARE_FINAL_TYPE (PulsTempHistory, puls_temp_history, PULS, TEMP_HISTORY, GObject)

PulsTempHistory *puls_temp_history_new       (void);
void             puls_temp_history_add_sample (PulsTempHistory *self, gint temp_celsius);
void             puls_temp_history_clear      (PulsTempHistory *self);

guint                    puls_temp_history_get_count   (PulsTempHistory *self);
const PulsTempSample    *puls_temp_history_get_samples (PulsTempHistory *self);

gint puls_temp_history_get_min (PulsTempHistory *self);
gint puls_temp_history_get_max (PulsTempHistory *self);
gint puls_temp_history_get_avg (PulsTempHistory *self);

G_END_DECLS
