/*
 * puls-smart-history.c
 *
 * Session-only SMART attribute snapshot store implementation.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-smart-history.h"

/* A snapshot is a hash table of attr_id (guint8) -> current value (gint) */
typedef struct {
    GHashTable *attr_values;  /* guint -> gint (boxed) */
} Snapshot;

static void
snapshot_free (Snapshot *s)
{
    if (s) {
        g_hash_table_destroy (s->attr_values);
        g_free (s);
    }
}

/* Per-device we keep the previous and current snapshot */
typedef struct {
    Snapshot *prev;
    Snapshot *curr;
} DeviceHistory;

static void
device_history_free (DeviceHistory *h)
{
    if (h) {
        snapshot_free (h->prev);
        snapshot_free (h->curr);
        g_free (h);
    }
}

struct _PulsSmartHistory {
    GObject     parent_instance;
    GHashTable *devices;  /* device_path -> DeviceHistory* */
};

G_DEFINE_TYPE (PulsSmartHistory, puls_smart_history, G_TYPE_OBJECT)

static void
puls_smart_history_finalize (GObject *object)
{
    PulsSmartHistory *self = PULS_SMART_HISTORY (object);
    g_hash_table_destroy (self->devices);
    G_OBJECT_CLASS (puls_smart_history_parent_class)->finalize (object);
}

static void
puls_smart_history_class_init (PulsSmartHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = puls_smart_history_finalize;
}

static void
puls_smart_history_init (PulsSmartHistory *self)
{
    self->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free,
                                           (GDestroyNotify)device_history_free);
}

PulsSmartHistory *
puls_smart_history_get_default (void)
{
    static PulsSmartHistory *singleton = NULL;
    if (!singleton)
        singleton = g_object_new (PULS_TYPE_SMART_HISTORY, NULL);
    return singleton;
}

static Snapshot *
build_snapshot (PulsSmartData *data)
{
    Snapshot *snap = g_new0 (Snapshot, 1);
    snap->attr_values = g_hash_table_new (g_direct_hash, g_direct_equal);

    GArray *attrs = puls_smart_data_get_ata_attributes (data);
    if (attrs) {
        for (guint i = 0; i < attrs->len; i++) {
            PulsSmartAttribute *attr = &g_array_index (attrs, PulsSmartAttribute, i);
            g_hash_table_insert (snap->attr_values,
                                 GUINT_TO_POINTER ((guint)attr->id),
                                 GINT_TO_POINTER (attr->current));
        }
    }
    return snap;
}

void
puls_smart_history_record (PulsSmartHistory *self, PulsSmartData *data)
{
    g_return_if_fail (PULS_IS_SMART_HISTORY (self));
    g_return_if_fail (PULS_IS_SMART_DATA (data));

    const gchar *dev = puls_smart_data_get_device_path (data);
    if (!dev) return;

    DeviceHistory *h = g_hash_table_lookup (self->devices, dev);
    if (!h) {
        h = g_new0 (DeviceHistory, 1);
        h->prev = NULL;
        h->curr = NULL;
        g_hash_table_insert (self->devices, g_strdup (dev), h);
    }

    snapshot_free (h->prev);
    h->prev = h->curr;
    h->curr = build_snapshot (data);
}

PulsAttrTrend
puls_smart_history_get_trend (PulsSmartHistory *self,
                               const gchar      *device_path,
                               guint8            attr_id)
{
    g_return_val_if_fail (PULS_IS_SMART_HISTORY (self), PULS_ATTR_TREND_STABLE);

    DeviceHistory *h = g_hash_table_lookup (self->devices, device_path);
    if (!h || !h->prev || !h->curr)
        return PULS_ATTR_TREND_STABLE;

    gpointer prev_ptr = g_hash_table_lookup (h->prev->attr_values,
                                              GUINT_TO_POINTER ((guint)attr_id));
    gpointer curr_ptr = g_hash_table_lookup (h->curr->attr_values,
                                              GUINT_TO_POINTER ((guint)attr_id));

    if (!prev_ptr || !curr_ptr)
        return PULS_ATTR_TREND_STABLE;

    gint prev_val = GPOINTER_TO_INT (prev_ptr);
    gint curr_val = GPOINTER_TO_INT (curr_ptr);

    if (curr_val > prev_val) return PULS_ATTR_TREND_IMPROVING;
    if (curr_val < prev_val) return PULS_ATTR_TREND_DEGRADING;
    return PULS_ATTR_TREND_STABLE;
}
