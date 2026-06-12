/*
 * puls-smart-parser.h
 *
 * Parses smartctl JSON output into PulsSmartData.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <glib.h>
#include "puls-smart-data.h"

G_BEGIN_DECLS

PulsSmartData *puls_smart_parser_parse_json (const gchar *json_str,
                                             GError     **error);

G_END_DECLS
