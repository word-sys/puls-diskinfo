/*
 * puls-disk-selector.c
 *
 * Horizontal scrollable disk selector bar with clickable cards.
 *
 * Copyright (C) 2024 Puls DiskInfo Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-disk-selector.h"
#include "puls-utils.h"

/* ── Signals ───────────────────────────────────────────────── */

enum {
    SIGNAL_DISK_SELECTED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* ── Card Data ─────────────────────────────────────────────── */

typedef struct {
    GtkWidget   *button;
    gchar       *device_path;
    GtkWidget   *icon;
    GtkWidget   *name_label;
    GtkWidget   *model_label;
    GtkWidget   *health_label;
    GtkWidget   *temp_label;
} DiskCard;

static void
disk_card_free (DiskCard *card)
{
    g_free (card->device_path);
    g_free (card);
}

/* ── Private Data ──────────────────────────────────────────── */

struct _PulsDiskSelector {
    GtkWidget parent_instance;

    GtkWidget       *scrolled;
    GtkWidget       *cards_box;
    PulsDiskManager *manager;
    GList           *cards;       /* GList of DiskCard* */
    gchar           *selected;
};

G_DEFINE_TYPE (PulsDiskSelector, puls_disk_selector, GTK_TYPE_WIDGET)

/* ── Card Click Handler ────────────────────────────────────── */

static void
on_card_clicked (GtkButton *button, gpointer user_data)
{
    PulsDiskSelector *self = PULS_DISK_SELECTOR (user_data);

    /* Find which card was clicked */
    for (GList *l = self->cards; l != NULL; l = l->next) {
        DiskCard *card = l->data;
        if (card->button == GTK_WIDGET (button)) {
            puls_disk_selector_select (self, card->device_path);
            break;
        }
    }
}

/* ── Update card appearance ────────────────────────────────── */

static void
update_card (DiskCard      *card,
             PulsSmartData *data,
             gboolean       is_selected)
{
    if (data == NULL)
        return;

    /* Device name */
    const gchar *dev_path = puls_smart_data_get_device_path (data);
    if (dev_path) {
        const gchar *name = strrchr (dev_path, '/');
        name = name ? name + 1 : dev_path;
        gtk_label_set_text (GTK_LABEL (card->name_label), name);
    }

    /* Model */
    const gchar *model = puls_smart_data_get_model_name (data);
    if (model) {
        /* Truncate long model names */
        if (strlen (model) > 18) {
            g_autofree gchar *short_model = g_strndup (model, 16);
            g_autofree gchar *display = g_strdup_printf ("%s…", short_model);
            gtk_label_set_text (GTK_LABEL (card->model_label), display);
        } else {
            gtk_label_set_text (GTK_LABEL (card->model_label), model);
        }
    }

    /* Health */
    PulsHealthStatus health = puls_smart_data_get_health (data);
    const gchar *health_text;
    const gchar *health_class;

    switch (health) {
    case PULS_HEALTH_GOOD:
        health_text = "● Good";
        health_class = "card-health-good";
        break;
    case PULS_HEALTH_CAUTION:
        health_text = "● Caution";
        health_class = "card-health-caution";
        break;
    case PULS_HEALTH_BAD:
        health_text = "● Bad";
        health_class = "card-health-bad";
        break;
    default:
        health_text = "● Unknown";
        health_class = "card-health-unknown";
        break;
    }

    gtk_label_set_text (GTK_LABEL (card->health_label), health_text);

    const gchar *hclasses[] = { "card-health-good", "card-health-caution",
                                "card-health-bad", "card-health-unknown", NULL };
    for (gint i = 0; hclasses[i]; i++)
        gtk_widget_remove_css_class (card->health_label, hclasses[i]);
    gtk_widget_add_css_class (card->health_label, health_class);

    /* Temperature */
    gint temp = puls_smart_data_get_temperature (data);
    if (temp >= 0) {
        g_autofree gchar *temp_str = g_strdup_printf ("%d °C", temp);
        gtk_label_set_text (GTK_LABEL (card->temp_label), temp_str);
    } else {
        gtk_label_set_text (GTK_LABEL (card->temp_label), "—");
    }

    /* Icon based on drive type */
    PulsDriveType dtype = puls_smart_data_get_drive_type (data);
    gtk_image_set_from_icon_name (GTK_IMAGE (card->icon),
                                 puls_drive_type_to_icon (dtype));

    /* Selected state */
    if (is_selected)
        gtk_widget_add_css_class (card->button, "disk-card-active");
    else
        gtk_widget_remove_css_class (card->button, "disk-card-active");
}

/* ── Build cards ───────────────────────────────────────────── */

static void
rebuild_cards (PulsDiskSelector *self)
{
    /* Remove old cards */
    for (GList *l = self->cards; l != NULL; l = l->next) {
        DiskCard *card = l->data;
        gtk_box_remove (GTK_BOX (self->cards_box), card->button);
    }
    g_list_free_full (self->cards, (GDestroyNotify)disk_card_free);
    self->cards = NULL;

    GList *devices = puls_disk_manager_get_devices (self->manager);

    for (GList *l = devices; l != NULL; l = l->next) {
        const gchar *dev_path = l->data;

        DiskCard *card = g_new0 (DiskCard, 1);
        card->device_path = g_strdup (dev_path);

        /* Button container */
        card->button = gtk_button_new ();
        gtk_widget_add_css_class (card->button, "disk-card");
        gtk_widget_set_size_request (card->button, 150, -1);
        g_signal_connect (card->button, "clicked",
                          G_CALLBACK (on_card_clicked), self);

        /* Card content */
        GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start (vbox, 8);
        gtk_widget_set_margin_end (vbox, 8);
        gtk_widget_set_margin_top (vbox, 8);
        gtk_widget_set_margin_bottom (vbox, 8);
        gtk_button_set_child (GTK_BUTTON (card->button), vbox);

        /* Icon */
        card->icon = gtk_image_new_from_icon_name ("drive-harddisk-symbolic");
        gtk_image_set_pixel_size (GTK_IMAGE (card->icon), 24);
        gtk_widget_add_css_class (card->icon, "card-icon");
        gtk_box_append (GTK_BOX (vbox), card->icon);

        /* Device name */
        card->name_label = gtk_label_new ("—");
        gtk_widget_add_css_class (card->name_label, "card-device-name");
        gtk_label_set_ellipsize (GTK_LABEL (card->name_label), PANGO_ELLIPSIZE_END);
        gtk_box_append (GTK_BOX (vbox), card->name_label);

        /* Model */
        card->model_label = gtk_label_new ("—");
        gtk_widget_add_css_class (card->model_label, "card-model");
        gtk_label_set_ellipsize (GTK_LABEL (card->model_label), PANGO_ELLIPSIZE_END);
        gtk_box_append (GTK_BOX (vbox), card->model_label);

        /* Health */
        card->health_label = gtk_label_new ("● Unknown");
        gtk_widget_add_css_class (card->health_label, "card-health");
        gtk_box_append (GTK_BOX (vbox), card->health_label);

        /* Temperature */
        card->temp_label = gtk_label_new ("—");
        gtk_widget_add_css_class (card->temp_label, "card-temp");
        gtk_box_append (GTK_BOX (vbox), card->temp_label);

        /* Populate from data */
        PulsSmartData *data = puls_disk_manager_get_smart_data (self->manager,
                                                                dev_path);
        gboolean selected = g_strcmp0 (self->selected, dev_path) == 0;
        update_card (card, data, selected);

        gtk_box_append (GTK_BOX (self->cards_box), card->button);
        self->cards = g_list_append (self->cards, card);
    }
}

/* ── GObject ───────────────────────────────────────────────── */

static void
puls_disk_selector_dispose (GObject *object)
{
    PulsDiskSelector *self = PULS_DISK_SELECTOR (object);

    g_list_free_full (self->cards, (GDestroyNotify)disk_card_free);
    self->cards = NULL;

    g_clear_pointer (&self->scrolled, gtk_widget_unparent);
    g_clear_pointer (&self->selected, g_free);

    G_OBJECT_CLASS (puls_disk_selector_parent_class)->dispose (object);
}

static void
puls_disk_selector_class_init (PulsDiskSelectorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = puls_disk_selector_dispose;
    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name (widget_class, "disk-selector");

    signals[SIGNAL_DISK_SELECTED] =
        g_signal_new ("disk-selected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
puls_disk_selector_init (PulsDiskSelector *self)
{
    self->cards = NULL;
    self->selected = NULL;
    self->manager = NULL;

    self->scrolled = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand (self->scrolled, TRUE);
    gtk_widget_set_parent (self->scrolled, GTK_WIDGET (self));

    self->cards_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start (self->cards_box, 12);
    gtk_widget_set_margin_end (self->cards_box, 12);
    gtk_widget_set_margin_top (self->cards_box, 8);
    gtk_widget_set_margin_bottom (self->cards_box, 8);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scrolled),
                                  self->cards_box);

    gtk_widget_add_css_class (GTK_WIDGET (self), "disk-selector");
}

GtkWidget *
puls_disk_selector_new (PulsDiskManager *manager)
{
    PulsDiskSelector *self = g_object_new (PULS_TYPE_DISK_SELECTOR, NULL);
    self->manager = manager;  /* weak ref, manager outlives selector */
    rebuild_cards (self);
    return GTK_WIDGET (self);
}

const gchar *
puls_disk_selector_get_selected (PulsDiskSelector *self)
{
    g_return_val_if_fail (PULS_IS_DISK_SELECTOR (self), NULL);
    return self->selected;
}

void
puls_disk_selector_select (PulsDiskSelector *self,
                           const gchar      *device_path)
{
    g_return_if_fail (PULS_IS_DISK_SELECTOR (self));

    if (g_strcmp0 (self->selected, device_path) == 0)
        return;

    g_free (self->selected);
    self->selected = g_strdup (device_path);

    /* Update card visual states */
    for (GList *l = self->cards; l != NULL; l = l->next) {
        DiskCard *card = l->data;
        gboolean is_sel = g_strcmp0 (card->device_path, device_path) == 0;

        if (is_sel)
            gtk_widget_add_css_class (card->button, "disk-card-active");
        else
            gtk_widget_remove_css_class (card->button, "disk-card-active");
    }

    g_signal_emit (self, signals[SIGNAL_DISK_SELECTED], 0, device_path);
}

void
puls_disk_selector_refresh (PulsDiskSelector *self)
{
    g_return_if_fail (PULS_IS_DISK_SELECTOR (self));
    rebuild_cards (self);
}
