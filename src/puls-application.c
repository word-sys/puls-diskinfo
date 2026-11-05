/*
 * puls-application.c
 *
 * AdwApplication subclass — loads CSS, configures style manager, and creates main window.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-application.h"
#include "puls-window.h"
#include "puls-settings.h"

GResource *puls_diskinfo_get_resource (void);

struct _PulsApplication {
    AdwApplication parent_instance;

    GtkCssProvider *css_provider;
};

G_DEFINE_TYPE (PulsApplication, puls_application, ADW_TYPE_APPLICATION)

static void
load_css (PulsApplication *self)
{
    self->css_provider = gtk_css_provider_new ();

    gtk_css_provider_load_from_resource (self->css_provider,
        "/io/github/puls/diskinfo/css/style.css");

    GdkDisplay *display = gdk_display_get_default ();
    if (display) {
        gtk_style_context_add_provider_for_display (
            display,
            GTK_STYLE_PROVIDER (self->css_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display (display);
        gtk_icon_theme_add_resource_path (icon_theme, "/io/github/puls/diskinfo/icons");
    }
}

static void
puls_application_activate (GApplication *app)
{
    PulsApplication *self = PULS_APPLICATION (app);

    if (self->css_provider == NULL)
        load_css (self);

    PulsSettings *settings = puls_settings_get_default ();
    gint theme = puls_settings_get_theme_preference (settings);
    AdwStyleManager *sm = adw_style_manager_get_default ();
    
    if (theme == 1)
        adw_style_manager_set_color_scheme (sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
    else if (theme == 2)
        adw_style_manager_set_color_scheme (sm, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme (sm, ADW_COLOR_SCHEME_DEFAULT);

    PulsWindow *window = puls_window_new (self);
    gtk_window_present (GTK_WINDOW (window));
}

static void
puls_application_dispose (GObject *object)
{
    PulsApplication *self = PULS_APPLICATION (object);
    g_clear_object (&self->css_provider);
    G_OBJECT_CLASS (puls_application_parent_class)->dispose (object);
}

static void
puls_application_class_init (PulsApplicationClass *klass)
{
    GApplicationClass *app_class = G_APPLICATION_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    app_class->activate = puls_application_activate;
    object_class->dispose = puls_application_dispose;
}

static const GOptionEntry app_cli_entries[] = {
    { "version", 'v', 0, G_OPTION_ARG_NONE, NULL, "Show application version", NULL },
    { "headless", 0, 0, G_OPTION_ARG_NONE, NULL, "Run disk scan in headless terminal mode", NULL },
    { "json", 0, 0, G_OPTION_ARG_NONE, NULL, "Output scan result as JSON", NULL },
    { "mock-disk", 'm', 0, G_OPTION_ARG_NONE, NULL, "Simulate mock storage devices", NULL },
    { NULL }
};

static void
puls_application_init (PulsApplication *self)
{
    g_resources_register (puls_diskinfo_get_resource ());
    self->css_provider = NULL;
    g_application_add_main_option_entries (G_APPLICATION (self), app_cli_entries);
}

PulsApplication *
puls_application_new (void)
{
    return g_object_new (PULS_TYPE_APPLICATION,
                         "application-id", "io.github.puls.diskinfo",
                         "flags", G_APPLICATION_FLAGS_NONE,
                         NULL);
}
