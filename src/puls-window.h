/*
 * puls-window.h
 *
 * Main application window for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <adwaita.h>
#include "puls-application.h"

G_BEGIN_DECLS

#define PULS_TYPE_WINDOW (puls_window_get_type ())
G_DECLARE_FINAL_TYPE (PulsWindow, puls_window, PULS, WINDOW, AdwApplicationWindow)

PulsWindow *puls_window_new (PulsApplication *app);

G_END_DECLS
