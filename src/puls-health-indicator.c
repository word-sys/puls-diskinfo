/*
 * puls-health-indicator.c
 *
 * Health status badge — large colored circle with text label.
 *
 * Copyright (C) 2024 Barın Güzeldemirci
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
    GtkWidget *icon_label;
    GtkWidget *text_label;

    PulsHealthStatus status;
};

G_DEFINE_TYPE (PulsHealthIndicator, puls_health_indicator, GTK_TYPE_WIDGET)

static void
update_display (PulsHealthIndicator *self)
{
    const gchar *icon = "⚪";
    const gchar *text = "Unknown";
    const gchar *css_class = "health-unknown";

    switch (self->status) {
    case PULS_HEALTH_GOOD:
        icon = "●";
        text = "GOOD";
        css_class = "health-good";
        break;
    case PULS_HEALTH_CAUTION:
        icon = "●";
        text = "CAUTION";
        css_class = "health-caution";
        break;
    case PULS_HEALTH_BAD:
        icon = "●";
        text = "BAD";
        css_class = "health-bad";
        break;
    case PULS_HEALTH_UNKNOWN:
    default:
        icon = "●";
        text = "UNKNOWN";
        css_class = "health-unknown";
        break;
    }

    gtk_label_set_text (GTK_LABEL (self->icon_label), icon);
    gtk_label_set_text (GTK_LABEL (self->text_label), text);

    const gchar *classes[] = { "health-good", "health-caution", "health-bad",
                               "health-unknown", NULL };
    for (gint i = 0; classes[i]; i++) {
        gtk_widget_remove_css_class (GTK_WIDGET (self), classes[i]);
    }
    gtk_widget_add_css_class (GTK_WIDGET (self), css_class);
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
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = puls_health_indicator_dispose;
    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name (widget_class, "health-indicator");
}

static void
puls_health_indicator_init (PulsHealthIndicator *self)
{
    self->status = PULS_HEALTH_UNKNOWN;

    self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_halign (self->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (self->box, GTK_ALIGN_CENTER);
    gtk_widget_set_parent (self->box, GTK_WIDGET (self));

    self->icon_label = gtk_label_new ("●");
    gtk_widget_add_css_class (self->icon_label, "health-icon");
    gtk_widget_set_halign (self->icon_label, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (self->box), self->icon_label);

    self->text_label = gtk_label_new ("UNKNOWN");
    gtk_widget_add_css_class (self->text_label, "health-text");
    gtk_widget_set_halign (self->text_label, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (self->box), self->text_label);

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

    if (self->status == status)
        return;

    self->status = status;
    update_display (self);
}

PulsHealthStatus
puls_health_indicator_get_status (PulsHealthIndicator *self)
{
    g_return_val_if_fail (PULS_IS_HEALTH_INDICATOR (self), PULS_HEALTH_UNKNOWN);
    return self->status;
}
