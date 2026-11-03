/*
 * puls-mock-data.h
 *
 * Mock storage device data simulator for testing and headless evaluation.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "puls-disk-manager.h"

G_BEGIN_DECLS

void     puls_set_mock_mode_enabled (gboolean enabled);
gboolean puls_is_mock_mode_enabled  (void);
void     puls_mock_data_populate_manager (PulsDiskManager *manager);

G_END_DECLS
