/*
 * puls-health-indicator.c
 *
 * Health status badge — colored badge with health % and estimated lifetime.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-health-indicator.h"

struct _PulsHealthIndicator {
    GtkWidget parent_instance;

    GtkWidget *box;
    GtkWidget *text_label;
    GtkWidget *pct_label;
    GtkWidget *lifetime_label;

    PulsHealthStatus status;
    gint             health_pct;
    gint             lifetime_days;
};

G_DEFINE_TYPE (PulsHealthIndicator, puls_health_indicator, GTK_TYPE_WIDGET)

static void
update_display (PulsHealthIndicator *self)
{
    const gchar *text      = "UNKNOWN";
    const gchar *css_class = "health-unknown";

    switch (self->status) {
    case PULS_HEALTH_GOOD:
        text      = "GOOD";
        css_class = "health-good";
        break;
    case PULS_HEALTH_CAUTION:
        text      = "CAUTION";
        css_class = "health-caution";
        break;
    case PULS_HEALTH_BAD:
        text      = "BAD";
        css_class = "health-bad";
        break;
    case PULS_HEALTH_UNKNOWN:
    default:
        text      = "UNKNOWN";
        css_class = "health-unknown";
        break;
    }

    gtk_label_set_text (GTK_LABEL (self->text_label), text);

    const gchar *classes[] = { "health-good", "health-caution", "health-bad",
                               "health-unknown", NULL };
    for (gint i = 0; classes[i]; i++)
        gtk_widget_remove_css_class (GTK_WIDGET (self), classes[i]);
    gtk_widget_add_css_class (GTK_WIDGET (self), css_class);

    /* Health % sub-label */
    if (self->health_pct >= 0) {
        g_autofree gchar *pct_str = g_strdup_printf ("%d%%", self->health_pct);
        gtk_label_set_text (GTK_LABEL (self->pct_label), pct_str);
        gtk_widget_set_visible (self->pct_label, TRUE);
    } else {
        gtk_widget_set_visible (self->pct_label, FALSE);
    }

    /* Estimated lifetime sub-label */
    if (self->lifetime_days >= 0) {
        g_autofree gchar *lt_str = NULL;
        if (self->lifetime_days >= 365)
            lt_str = g_strdup_printf ("Est. %.1f yr remaining",
                                      (double)self->lifetime_days / 365.25);
        else
            lt_str = g_strdup_printf ("Est. %d day%s remaining",
                                      self->lifetime_days,
                                      self->lifetime_days == 1 ? "" : "s");
        gtk_label_set_text (GTK_LABEL (self->lifetime_label), lt_str);
        gtk_widget_set_visible (self->lifetime_label, TRUE);
    } else {
        gtk_widget_set_visible (self->lifetime_label, FALSE);
    }
}

static void
puls_health_indicator_dispose (GObject *object)
{
    PulsHealthIndicator *self = PULS_HEALTH_INDICATOR (object);
    g_clear_pointer (&self->box, gtk_widget_unparent);
    G_OBJECT_CLASS (puls_health_indicator_parent_class)->dispose (object);
}

static void
puls_health_indicator_class_init (PulsHealthIndicatorClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = puls_health_indicator_dispose;
    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name (widget_class, "health-indicator");
}

static void
puls_health_indicator_init (PulsHealthIndicator *self)
{
    self->status       = PULS_HEALTH_UNKNOWN;
    self->health_pct   = -1;
    self->lifetime_days = -1;

    self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_halign (self->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (self->box, GTK_ALIGN_CENTER);
    gtk_widget_set_parent (self->box, GTK_WIDGET (self));

    self->text_label = gtk_label_new ("UNKNOWN");
    gtk_widget_add_css_class (self->text_label, "health-text");
    gtk_widget_set_halign (self->text_label, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (self->box), self->text_label);

    self->pct_label = gtk_label_new ("");
    gtk_widget_add_css_class (self->pct_label, "health-pct");
    gtk_widget_set_halign (self->pct_label, GTK_ALIGN_CENTER);
    gtk_widget_set_visible (self->pct_label, FALSE);
    gtk_box_append (GTK_BOX (self->box), self->pct_label);

    self->lifetime_label = gtk_label_new ("");
    gtk_widget_add_css_class (self->lifetime_label, "health-lifetime");
    gtk_widget_set_halign (self->lifetime_label, GTK_ALIGN_CENTER);
    gtk_widget_set_visible (self->lifetime_label, FALSE);
    gtk_box_append (GTK_BOX (self->box), self->lifetime_label);

    gtk_widget_add_css_class (GTK_WIDGET (self), "health-indicator");

    update_display (self);
}

GtkWidget *
puls_health_indicator_new (void)
{
    return g_object_new (PULS_TYPE_HEALTH_INDICATOR, NULL);
}

void
puls_health_indicator_set_status (PulsHealthIndicator *self,
                                   PulsHealthStatus     status)
{
    g_return_if_fail (PULS_IS_HEALTH_INDICATOR (self));
    self->status = status;
    update_display (self);
}

PulsHealthStatus
puls_health_indicator_get_status (PulsHealthIndicator *self)
{
    g_return_val_if_fail (PULS_IS_HEALTH_INDICATOR (self), PULS_HEALTH_UNKNOWN);
    return self->status;
}

void
puls_health_indicator_set_health_percent (PulsHealthIndicator *self, gint pct)
{
    g_return_if_fail (PULS_IS_HEALTH_INDICATOR (self));
    self->health_pct = pct;
    update_display (self);
}

void
puls_health_indicator_set_lifetime_days (PulsHealthIndicator *self, gint days)
{
    g_return_if_fail (PULS_IS_HEALTH_INDICATOR (self));
    self->lifetime_days = days;
    update_display (self);
}
