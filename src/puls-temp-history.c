/*
 * puls-temp-history.c
 *
 * Session-only temperature history circular buffer implementation.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-temp-history.h"
#include <string.h>

struct _PulsTempHistory {
    GObject parent_instance;

    PulsTempSample samples[PULS_TEMP_HISTORY_MAX_SAMPLES];
    guint          count;   
    guint          head;   
};

G_DEFINE_TYPE (PulsTempHistory, puls_temp_history, G_TYPE_OBJECT)

static void
puls_temp_history_class_init (PulsTempHistoryClass *klass G_GNUC_UNUSED)
{
}

static void
puls_temp_history_init (PulsTempHistory *self)
{
    memset (self->samples, 0, sizeof (self->samples));
    self->count = 0;
    self->head  = 0;
}

PulsTempHistory *
puls_temp_history_new (void)
{
    return g_object_new (PULS_TYPE_TEMP_HISTORY, NULL);
}

void
puls_temp_history_add_sample (PulsTempHistory *self, gint temp_celsius)
{
    g_return_if_fail (PULS_IS_TEMP_HISTORY (self));

    self->samples[self->head].timestamp_us = g_get_monotonic_time ();
    self->samples[self->head].temp_celsius = temp_celsius;

    self->head = (self->head + 1) % PULS_TEMP_HISTORY_MAX_SAMPLES;
    if (self->count < PULS_TEMP_HISTORY_MAX_SAMPLES)
        self->count++;
}

void
puls_temp_history_clear (PulsTempHistory *self)
{
    g_return_if_fail (PULS_IS_TEMP_HISTORY (self));
    self->count = 0;
    self->head  = 0;
}

guint
puls_temp_history_get_count (PulsTempHistory *self)
{
    g_return_val_if_fail (PULS_IS_TEMP_HISTORY (self), 0);
    return self->count;
}

/*
 * Returns samples in chronological order. The circular buffer stores
 * samples with head pointing to the *next* write slot, so the oldest
 * sample is at index `head` when the buffer is full, or index 0 when
 * it is not yet full.
 *
 * For simplicity we return a pointer to the raw array; callers must
 * use puls_temp_history_get_count() to know how many are valid, and
 * must iterate from (head - count + MAX) % MAX onwards to get them in
 * order.  The helper iterators below do this correctly.
 */
const PulsTempSample *
puls_temp_history_get_samples (PulsTempHistory *self)
{
    g_return_val_if_fail (PULS_IS_TEMP_HISTORY (self), NULL);
    return self->samples;
}

gint
puls_temp_history_get_min (PulsTempHistory *self)
{
    g_return_val_if_fail (PULS_IS_TEMP_HISTORY (self), -1);
    if (self->count == 0) return -1;

    gint mn = G_MAXINT;
    for (guint i = 0; i < self->count; i++) {
        guint idx = (self->head + PULS_TEMP_HISTORY_MAX_SAMPLES - self->count + i)
                    % PULS_TEMP_HISTORY_MAX_SAMPLES;
        if (self->samples[idx].temp_celsius < mn)
            mn = self->samples[idx].temp_celsius;
    }
    return mn;
}

gint
puls_temp_history_get_max (PulsTempHistory *self)
{
    g_return_val_if_fail (PULS_IS_TEMP_HISTORY (self), -1);
    if (self->count == 0) return -1;

    gint mx = G_MININT;
    for (guint i = 0; i < self->count; i++) {
        guint idx = (self->head + PULS_TEMP_HISTORY_MAX_SAMPLES - self->count + i)
                    % PULS_TEMP_HISTORY_MAX_SAMPLES;
        if (self->samples[idx].temp_celsius > mx)
            mx = self->samples[idx].temp_celsius;
    }
    return mx;
}

gint
puls_temp_history_get_avg (PulsTempHistory *self)
{
    g_return_val_if_fail (PULS_IS_TEMP_HISTORY (self), -1);
    if (self->count == 0) return -1;

    gint64 sum = 0;
    for (guint i = 0; i < self->count; i++) {
        guint idx = (self->head + PULS_TEMP_HISTORY_MAX_SAMPLES - self->count + i)
                    % PULS_TEMP_HISTORY_MAX_SAMPLES;
        sum += self->samples[idx].temp_celsius;
    }
    return (gint)(sum / (gint64)self->count);
}
