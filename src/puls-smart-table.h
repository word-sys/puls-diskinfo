/*
 * puls-smart-table.h
 *
 * S.M.A.R.T. attributes table widget.
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
#include "puls-smart-data.h"

G_BEGIN_DECLS

#define PULS_TYPE_SMART_TABLE (puls_smart_table_get_type ())
G_DECLARE_FINAL_TYPE (PulsSmartTable, puls_smart_table,
                      PULS, SMART_TABLE, GtkWidget)

GtkWidget *puls_smart_table_new       (void);
void       puls_smart_table_set_data  (PulsSmartTable *self,
                                       PulsSmartData  *data);
void       puls_smart_table_clear     (PulsSmartTable *self);

G_END_DECLS
