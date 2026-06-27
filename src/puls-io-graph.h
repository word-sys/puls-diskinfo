/*
 * puls-io-graph.h
 *
 * Real-time disk I/O activity graph widget (Cairo drawing).
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PULS_TYPE_IO_GRAPH (puls_io_graph_get_type ())

G_DECLARE_FINAL_TYPE (PulsIoGraph, puls_io_graph, PULS, IO_GRAPH, GtkDrawingArea)

/**
 * puls_io_graph_new:
 *
 * Creates a new real-time disk I/O activity graph widget.
 *
 * Returns: (transfer full): a new #PulsIoGraph widget.
 */
GtkWidget *puls_io_graph_new (void);

/**
 * puls_io_graph_add_data:
 * @self: a #PulsIoGraph
 * @read_bytes_per_sec: read throughput in bytes per second
 * @write_bytes_per_sec: write throughput in bytes per second
 * @active_percent: disk active time as a percentage (0–100)
 *
 * Pushes a new data point into the ring buffer and queues a redraw.
 */
void puls_io_graph_add_data (PulsIoGraph *self,
                              double       read_bytes_per_sec,
                              double       write_bytes_per_sec,
                              double       active_percent);

/**
 * puls_io_graph_clear:
 * @self: a #PulsIoGraph
 *
 * Clears all historical data and resets the graph.
 */
void puls_io_graph_clear (PulsIoGraph *self);

G_END_DECLS
