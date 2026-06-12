/*
 * main.c
 *
 * Entry point for PULS DiskInfo.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-application.h"
#include <locale.h>
#include <adwaita.h>

int
main (int argc, char *argv[])
{
    setlocale (LC_ALL, "");
    adw_init ();

    g_autoptr(PulsApplication) app = puls_application_new ();

    return g_application_run (G_APPLICATION (app), argc, argv);
}
