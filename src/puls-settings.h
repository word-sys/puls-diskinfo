/*
 * puls-settings.h
 *
 * Settings management for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PULS_TYPE_SETTINGS (puls_settings_get_type ())
G_DECLARE_FINAL_TYPE (PulsSettings, puls_settings, PULS, SETTINGS, GObject)

PulsSettings *puls_settings_get_default        (void);

gint          puls_settings_get_polling_interval (PulsSettings *self);
void          puls_settings_set_polling_interval (PulsSettings *self, gint val);

gboolean      puls_settings_get_use_fahrenheit   (PulsSettings *self);
void          puls_settings_set_use_fahrenheit   (PulsSettings *self, gboolean val);

gint          puls_settings_get_caution_temp     (PulsSettings *self);
void          puls_settings_set_caution_temp     (PulsSettings *self, gint val);

gint          puls_settings_get_theme_preference (PulsSettings *self);
void          puls_settings_set_theme_preference (PulsSettings *self, gint val);

void          puls_settings_save                 (PulsSettings *self);

G_END_DECLS
