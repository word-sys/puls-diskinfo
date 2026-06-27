/*
 * puls-io-graph.c
 *
 * Real-time disk I/O activity graph widget.
 * Draws live read/write throughput and disk utilisation using Cairo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-io-graph.h"
#include <math.h>
#include <string.h>

#define GRAPH_HISTORY  60

struct _PulsIoGraph {
    GtkDrawingArea parent_instance;

    double read_history[GRAPH_HISTORY];
    double write_history[GRAPH_HISTORY];
    double active_history[GRAPH_HISTORY];

    int head;

    int count;
    double last_read;
    double last_write;
    double last_active;
};

G_DEFINE_TYPE (PulsIoGraph, puls_io_graph, GTK_TYPE_DRAWING_AREA)

static gchar *
format_speed (double bps)
{
    if (bps >= 1024.0 * 1024.0 * 1024.0)
        return g_strdup_printf ("%.2f GB/s", bps / (1024.0 * 1024.0 * 1024.0));
    if (bps >= 1024.0 * 1024.0)
        return g_strdup_printf ("%.2f MB/s", bps / (1024.0 * 1024.0));
    if (bps >= 1024.0)
        return g_strdup_printf ("%.1f KB/s", bps / 1024.0);
    return g_strdup_printf ("%.0f B/s", bps);
}

/*
 * Determine a sensible Y-axis ceiling for the speed channels.
 * We pick the next power-of-two multiple of a base scale unit so the
 * labels stay clean (e.g. 100 KB/s, 1 MB/s, 10 MB/s …).
 */
static double
nice_ceil (double max_bps)
{
    if (max_bps <= 0.0)
        return 1024.0 * 1024.0;     /* default: 1 MB/s floor */

    double kb = max_bps / 1024.0;
    double magnitude = pow (10.0, floor (log10 (kb)));
    double normalised = kb / magnitude;

    double nice;
    if (normalised <= 1.0)       nice = 1.0;
    else if (normalised <= 2.0)  nice = 2.0;
    else if (normalised <= 5.0)  nice = 5.0;
    else                         nice = 10.0;

    return nice * magnitude * 1024.0 * 1.2;
}

static void
draw_graph (GtkDrawingArea *da G_GNUC_UNUSED,
            cairo_t        *cr,
            int             width,
            int             height,
            gpointer        user_data)
{
    PulsIoGraph *self   = PULS_IO_GRAPH (user_data);
    GtkWidget   *widget = GTK_WIDGET (self);

    /* ── (GTK 4.6 compatible) ── */
    GdkRGBA fg;
    GtkStyleContext *style_ctx = gtk_widget_get_style_context (widget);
    gtk_style_context_get_color (style_ctx, &fg);

    const int pad_left   = 68;
    const int pad_right  = 12;
    const int pad_top    = 10;
    const int pad_bottom = 28;
    const int legend_h   = 28;

    int plot_w = width  - pad_left - pad_right;
    int plot_h = height - pad_top  - pad_bottom - legend_h;

    if (plot_w < 10 || plot_h < 10)
        return;

    cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.04);
    cairo_rectangle (cr, pad_left, pad_top, plot_w, plot_h);
    cairo_fill (cr);

    double peak_speed = 1024.0;
    for (int i = 0; i < self->count; i++) {
        int idx = (self->head + GRAPH_HISTORY - self->count + i) % GRAPH_HISTORY;
        if (self->read_history[idx]  > peak_speed) peak_speed = self->read_history[idx];
        if (self->write_history[idx] > peak_speed) peak_speed = self->write_history[idx];
    }
    double y_max = nice_ceil (peak_speed);

    cairo_set_line_width (cr, 0.5);
    const int grid_rows = 4;
    for (int i = 0; i <= grid_rows; i++) {
        double fy = (double)i / grid_rows;
        double y  = pad_top + plot_h * (1.0 - fy);

        cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.10);
        cairo_move_to (cr, pad_left, y);
        cairo_line_to (cr, pad_left + plot_w, y);
        cairo_stroke (cr);

        double bps = fy * y_max;
        g_autofree gchar *label = format_speed (bps);

        cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.55);
        cairo_select_font_face (cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size (cr, 10.0);

        cairo_text_extents_t ext;
        cairo_text_extents (cr, label, &ext);
        cairo_move_to (cr, pad_left - ext.width - 6, y + ext.height / 2.0);
        cairo_show_text (cr, label);
    }

    for (int s = 0; s <= GRAPH_HISTORY; s += 10) {
        double x = pad_left + plot_w * ((double)s / GRAPH_HISTORY);
        cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.05);
        cairo_move_to (cr, x, pad_top);
        cairo_line_to (cr, x, pad_top + plot_h);
        cairo_stroke (cr);
    }

#define BUILD_CURVE(history) \
    do { \
        gboolean first = TRUE; \
        for (int i = 0; i < self->count; i++) { \
            int idx = (self->head + GRAPH_HISTORY - self->count + i) % GRAPH_HISTORY; \
            double fx = (double)(GRAPH_HISTORY - self->count + i) / (GRAPH_HISTORY - 1); \
            double fy = (history)[idx] / y_max; \
            fy = CLAMP (fy, 0.0, 1.0); \
            double x = pad_left + fx * plot_w; \
            double y = pad_top  + plot_h * (1.0 - fy); \
            if (first) { cairo_move_to (cr, x, y); first = FALSE; } \
            else        { cairo_line_to (cr, x, y); } \
        } \
    } while (0)

    if (self->count >= 2) {
        cairo_set_line_width (cr, 1.5);
        cairo_set_source_rgba (cr, 0.18, 0.82, 0.42, 0.65);
        double dashes[] = { 4.0, 4.0 };
        cairo_set_dash (cr, dashes, 2, 0.0);

        double saved = y_max;
        y_max = 100.0;
        BUILD_CURVE (self->active_history);
        cairo_stroke (cr);
        cairo_set_dash (cr, NULL, 0, 0.0);
        y_max = saved;
    }

    if (self->count >= 2) {
        double x0 = pad_left + (double)(GRAPH_HISTORY - self->count) / (GRAPH_HISTORY - 1) * plot_w;

        cairo_pattern_t *wfill = cairo_pattern_create_linear (0, pad_top, 0, pad_top + plot_h);
        cairo_pattern_add_color_stop_rgba (wfill, 0.0, 0.85, 0.28, 0.94, 0.30);
        cairo_pattern_add_color_stop_rgba (wfill, 1.0, 0.85, 0.28, 0.94, 0.00);
        cairo_set_source (cr, wfill);
        cairo_set_line_width (cr, 0.0);
        BUILD_CURVE (self->write_history);
        {
            double lx = pad_left + plot_w;
            double ly = pad_top  + plot_h;
            cairo_line_to (cr, lx, ly);
            cairo_line_to (cr, x0, ly);
        }
        cairo_close_path (cr);
        cairo_fill (cr);
        cairo_pattern_destroy (wfill);

        cairo_set_line_width (cr, 2.0);
        cairo_set_source_rgba (cr, 0.87, 0.28, 0.94, 0.90);
        BUILD_CURVE (self->write_history);
        cairo_stroke (cr);
    }

    if (self->count >= 2) {
        double x0 = pad_left + (double)(GRAPH_HISTORY - self->count) / (GRAPH_HISTORY - 1) * plot_w;

        cairo_pattern_t *rfill = cairo_pattern_create_linear (0, pad_top, 0, pad_top + plot_h);
        cairo_pattern_add_color_stop_rgba (rfill, 0.0, 0.02, 0.72, 0.83, 0.30);
        cairo_pattern_add_color_stop_rgba (rfill, 1.0, 0.02, 0.72, 0.83, 0.00);
        cairo_set_source (cr, rfill);
        cairo_set_line_width (cr, 0.0);
        BUILD_CURVE (self->read_history);
        {
            double lx = pad_left + plot_w;
            double ly = pad_top  + plot_h;
            cairo_line_to (cr, lx, ly);
            cairo_line_to (cr, x0, ly);
        }
        cairo_close_path (cr);
        cairo_fill (cr);
        cairo_pattern_destroy (rfill);

        cairo_set_line_width (cr, 2.0);
        cairo_set_source_rgba (cr, 0.04, 0.71, 0.83, 0.95);
        BUILD_CURVE (self->read_history);
        cairo_stroke (cr);
    }

#undef BUILD_CURVE

    cairo_set_line_width (cr, 0.7);
    cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.12);
    cairo_rectangle (cr, pad_left, pad_top, plot_w, plot_h);
    cairo_stroke (cr);

    cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.40);
    cairo_select_font_face (cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size (cr, 10.0);
    {
        const char *lbl = "← 60 s ago                                  now →";
        cairo_text_extents_t ext;
        cairo_text_extents (cr, lbl, &ext);
        cairo_move_to (cr, pad_left + (plot_w - ext.width) / 2.0,
                       pad_top + plot_h + 14.0);
        cairo_show_text (cr, lbl);
    }

    int ly_base = pad_top + plot_h + pad_bottom + legend_h / 2 - 6;
    cairo_set_font_size (cr, 11.0);

    struct { double r, g, b; const char *label; double value; gboolean is_pct; } legend[] = {
        { 0.04, 0.71, 0.83, "Read:",   self->last_read,   FALSE },
        { 0.87, 0.28, 0.94, "Write:",  self->last_write,  FALSE },
        { 0.18, 0.82, 0.42, "Active:", self->last_active, TRUE  },
    };

    int lx = pad_left;
    for (int i = 0; i < 3; i++) {
        /* coloured bullet */
        cairo_set_source_rgba (cr, legend[i].r, legend[i].g, legend[i].b, 1.0);
        cairo_arc (cr, lx + 4, ly_base - 4, 4.0, 0.0, 2 * G_PI);
        cairo_fill (cr);

        /* key label — theme fg */
        cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.70);
        cairo_move_to (cr, lx + 12, ly_base);
        cairo_show_text (cr, legend[i].label);

        cairo_text_extents_t ext;
        cairo_text_extents (cr, legend[i].label, &ext);
        lx += (int)ext.width + 14;

        /* value — accent colour */
        g_autofree gchar *val_str = NULL;
        if (legend[i].is_pct)
            val_str = g_strdup_printf ("%.1f%%", legend[i].value);
        else
            val_str = format_speed (legend[i].value);

        cairo_set_source_rgba (cr, legend[i].r, legend[i].g, legend[i].b, 1.0);
        cairo_move_to (cr, lx, ly_base);
        cairo_show_text (cr, val_str);

        cairo_text_extents (cr, val_str, &ext);
        lx += (int)ext.width + 24;
    }
}

static void
puls_io_graph_class_init (PulsIoGraphClass *klass G_GNUC_UNUSED)
{
    /* GTK_TYPE_DRAWING_AREA handles everything. */
}

static void
puls_io_graph_init (PulsIoGraph *self)
{
    memset (self->read_history,   0, sizeof (self->read_history));
    memset (self->write_history,  0, sizeof (self->write_history));
    memset (self->active_history, 0, sizeof (self->active_history));
    self->head       = 0;
    self->count      = 0;
    self->last_read  = 0.0;
    self->last_write = 0.0;
    self->last_active = 0.0;

    gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (self),
                                    draw_graph, self, NULL);
}

GtkWidget *
puls_io_graph_new (void)
{
    return g_object_new (PULS_TYPE_IO_GRAPH, NULL);
}

void
puls_io_graph_add_data (PulsIoGraph *self,
                         double       read_bytes_per_sec,
                         double       write_bytes_per_sec,
                         double       active_percent)
{
    g_return_if_fail (PULS_IS_IO_GRAPH (self));

    self->read_history[self->head]   = read_bytes_per_sec;
    self->write_history[self->head]  = write_bytes_per_sec;
    self->active_history[self->head] = CLAMP (active_percent, 0.0, 100.0);

    self->head = (self->head + 1) % GRAPH_HISTORY;
    if (self->count < GRAPH_HISTORY)
        self->count++;

    self->last_read   = read_bytes_per_sec;
    self->last_write  = write_bytes_per_sec;
    self->last_active = CLAMP (active_percent, 0.0, 100.0);

    gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
puls_io_graph_clear (PulsIoGraph *self)
{
    g_return_if_fail (PULS_IS_IO_GRAPH (self));

    memset (self->read_history,   0, sizeof (self->read_history));
    memset (self->write_history,  0, sizeof (self->write_history));
    memset (self->active_history, 0, sizeof (self->active_history));
    self->head        = 0;
    self->count       = 0;
    self->last_read   = 0.0;
    self->last_write  = 0.0;
    self->last_active = 0.0;

    gtk_widget_queue_draw (GTK_WIDGET (self));
}
