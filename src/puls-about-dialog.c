/*
 * puls-about-dialog.c
 *
 * About dialog implementation.
 *
 * Copyright (C) 2024 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-about-dialog.h"
#include "puls-utils.h"

#ifndef PULS_VERSION
#define PULS_VERSION "1.0.0"
#endif

void
puls_show_about_dialog (GtkWindow *parent)
{
    const gchar *authors[] = {
        "Barın Güzeldemirci <baringuzeldemir@gmail.com>",
        NULL
    };

    g_autofree gchar *smartctl_ver = NULL;
    puls_detect_smartmontools (&smartctl_ver);

    g_autofree gchar *comments = NULL;
    if (smartctl_ver)
        comments = g_strdup_printf (
            "Disk health and S.M.A.R.T. monitoring for Linux.\n\n"
            "Backend: %s", smartctl_ver);
    else
        comments = g_strdup (
            "Disk health and S.M.A.R.T. monitoring for Linux.\n\n"
            "Warning: smartmontools not found!");

    GtkWidget *dialog = gtk_about_dialog_new ();
    gtk_about_dialog_set_program_name (GTK_ABOUT_DIALOG (dialog), "PULS DiskInfo");
    gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (dialog), PULS_VERSION);
    gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (dialog), comments);
    gtk_about_dialog_set_license_type (GTK_ABOUT_DIALOG (dialog),
                                       GTK_LICENSE_GPL_3_0);
    gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (dialog),
                                  "https://github.com/puls-diskinfo/puls-diskinfo");
    gtk_about_dialog_set_website_label (GTK_ABOUT_DIALOG (dialog),
                                        "GitHub Repository");
    gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (dialog), authors);
    gtk_about_dialog_set_copyright (GTK_ABOUT_DIALOG (dialog),
                                    "© 2024 Barın Güzeldemirci");
    gtk_about_dialog_set_logo_icon_name (GTK_ABOUT_DIALOG (dialog),
                                         "drive-harddisk-solidstate-symbolic");

    if (parent)
        gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
    gtk_window_present (GTK_WINDOW (dialog));
}
