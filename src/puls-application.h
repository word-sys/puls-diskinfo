/*
 * puls-application.h
 *
 * GtkApplication subclass for PULS DiskInfo.
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

G_BEGIN_DECLS

#define PULS_TYPE_APPLICATION (puls_application_get_type ())
G_DECLARE_FINAL_TYPE (PulsApplication, puls_application,
                      PULS, APPLICATION, AdwApplication)

PulsApplication *puls_application_new (void);

G_END_DECLS
