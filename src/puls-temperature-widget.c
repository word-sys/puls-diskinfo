/*
 * puls-temperature-widget.c
 *
 * Temperature gauge — progress bar with color gradient.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-temperature-widget.h"
#include "puls-settings.h"
#include "puls-utils.h"

struct _PulsTemperatureWidget {
    GtkWidget parent_instance;

    GtkWidget *box;
    GtkWidget *temp_label;
    GtkWidget *progress_bar;
    GtkWidget *range_label;

    gint temperature;
};

G_DEFINE_TYPE (PulsTemperatureWidget, puls_temperature_widget, GTK_TYPE_WIDGET)

static const gchar *
get_temp_css_class (gint celsius)
{
    if (celsius < 0)   return "temp-unknown";
    if (celsius < 30)  return "temp-cold";
    if (celsius < 50)  return "temp-normal";
    if (celsius < 60)  return "temp-warm";
    return "temp-hot";
}

static void
update_display (PulsTemperatureWidget *self)
{
    g_autofree gchar *temp_str = NULL;
    PulsSettings *settings = puls_settings_get_default ();
    gboolean fahr = puls_settings_get_use_fahrenheit (settings);

    if (self->temperature < 0) {
        temp_str = g_strdup ("N/A");
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress_bar), 0.0);
    } else {
        if (fahr) {
            gint temp_f = (self->temperature * 9 / 5) + 32;
            temp_str = g_strdup_printf ("🌡 %d °F", temp_f);
        } else {
            temp_str = g_strdup_printf ("🌡 %d °C", self->temperature);
        }
        gdouble fraction = CLAMP ((gdouble)self->temperature / 100.0, 0.0, 1.0);
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress_bar), fraction);
    }

    gtk_label_set_text (GTK_LABEL (self->temp_label), temp_str);

    const gchar *classes[] = { "temp-cold", "temp-normal", "temp-warm",
                               "temp-hot", "temp-unknown", NULL };
    for (gint i = 0; classes[i]; i++)
        gtk_widget_remove_css_class (self->progress_bar, classes[i]);

    gtk_widget_add_css_class (self->progress_bar,
                               get_temp_css_class (self->temperature));

    if (self->temperature >= 0) {
        const gchar *status;
        if (fahr) {
            if (self->temperature < 50)
                status = "Safe range: < 122 °F";
            else if (self->temperature < 60)
                status = "⚠ Getting warm";
            else if (self->temperature < 70)
                status = "⚠ Hot — check cooling";
            else
                status = "🔥 Critical temperature!";
        } else {
            if (self->temperature < 50)
                status = "Safe range: < 50 °C";
            else if (self->temperature < 60)
                status = "⚠ Getting warm";
            else if (self->temperature < 70)
                status = "⚠ Hot — check cooling";
            else
                status = "🔥 Critical temperature!";
        }
        gtk_label_set_text (GTK_LABEL (self->range_label), status);
    } else {
        gtk_label_set_text (GTK_LABEL (self->range_label), "Temperature unavailable");
    }
}

static void
puls_temperature_widget_dispose (GObject *object)
{
    PulsTemperatureWidget *self = PULS_TEMPERATURE_WIDGET (object);
    g_clear_pointer (&self->box, gtk_widget_unparent);
    G_OBJECT_CLASS (puls_temperature_widget_parent_class)->dispose (object);
}

static void
puls_temperature_widget_class_init (PulsTemperatureWidgetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = puls_temperature_widget_dispose;
    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name (widget_class, "temperature-widget");
}

static void
puls_temperature_widget_init (PulsTemperatureWidget *self)
{
    self->temperature = -1;

    self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand (self->box, TRUE);
    gtk_widget_set_parent (self->box, GTK_WIDGET (self));

    self->temp_label = gtk_label_new ("N/A");
    gtk_widget_add_css_class (self->temp_label, "temp-value");
    gtk_widget_set_halign (self->temp_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (self->box), self->temp_label);

    self->progress_bar = gtk_progress_bar_new ();
    gtk_widget_set_hexpand (self->progress_bar, TRUE);
    gtk_widget_add_css_class (self->progress_bar, "temp-bar");
    gtk_box_append (GTK_BOX (self->box), self->progress_bar);

    self->range_label = gtk_label_new ("Temperature unavailable");
    gtk_widget_add_css_class (self->range_label, "temp-range");
    gtk_widget_set_halign (self->range_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (self->box), self->range_label);

    gtk_widget_add_css_class (GTK_WIDGET (self), "temperature-widget");

    update_display (self);
}

GtkWidget *
puls_temperature_widget_new (void)
{
    return g_object_new (PULS_TYPE_TEMPERATURE_WIDGET, NULL);
}

void
puls_temperature_widget_set_temperature (PulsTemperatureWidget *self,
                                          gint                   celsius)
{
    g_return_if_fail (PULS_IS_TEMPERATURE_WIDGET (self));

    if (self->temperature == celsius)
        return;

    self->temperature = celsius;
    update_display (self);
}

gint
puls_temperature_widget_get_temperature (PulsTemperatureWidget *self)
{
    g_return_val_if_fail (PULS_IS_TEMPERATURE_WIDGET (self), -1);
    return self->temperature;
}
