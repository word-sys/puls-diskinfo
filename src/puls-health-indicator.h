/*
 * puls-health-indicator.h
 *
 * Health status badge widget.
 *
 * Copyright (C) 2024 Puls DiskInfo Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <gtk/gtk.h>
#include "puls-smart-data.h"

G_BEGIN_DECLS

#define PULS_TYPE_HEALTH_INDICATOR (puls_health_indicator_get_type ())
G_DECLARE_FINAL_TYPE (PulsHealthIndicator, puls_health_indicator,
                      PULS, HEALTH_INDICATOR, GtkWidget)

GtkWidget       *puls_health_indicator_new        (void);
void             puls_health_indicator_set_status  (PulsHealthIndicator *self,
                                                    PulsHealthStatus     status);
PulsHealthStatus puls_health_indicator_get_status  (PulsHealthIndicator *self);

G_END_DECLS
