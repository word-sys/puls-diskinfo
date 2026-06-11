/*
 * puls-preferences-window.h
 *
 * Preferences window for Puls DiskInfo.
 *
 * Copyright (C) 2024 Puls DiskInfo Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define PULS_TYPE_PREFERENCES_WINDOW (puls_preferences_window_get_type ())
G_DECLARE_FINAL_TYPE (PulsPreferencesWindow, puls_preferences_window,
                      PULS, PREFERENCES_WINDOW, AdwPreferencesWindow)

GtkWidget *puls_preferences_window_new (GtkWindow *parent);

G_END_DECLS
