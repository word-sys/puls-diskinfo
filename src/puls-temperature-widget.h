/*
 * puls-temperature-widget.h
 *
 * Temperature gauge display widget.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PULS_TYPE_TEMPERATURE_WIDGET (puls_temperature_widget_get_type ())
G_DECLARE_FINAL_TYPE (PulsTemperatureWidget, puls_temperature_widget,
                      PULS, TEMPERATURE_WIDGET, GtkWidget)

GtkWidget *puls_temperature_widget_new             (void);
void       puls_temperature_widget_set_temperature  (PulsTemperatureWidget *self,
                                                     gint                   celsius);
gint       puls_temperature_widget_get_temperature  (PulsTemperatureWidget *self);

G_END_DECLS
