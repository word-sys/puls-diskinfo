/*
 * puls-smart-table.c
 *
 * SMART attributes table using GtkColumnView.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-smart-table.h"

#define PULS_TYPE_ATTR_ROW (puls_attr_row_get_type ())
G_DECLARE_FINAL_TYPE (PulsAttrRow, puls_attr_row, PULS, ATTR_ROW, GObject)

struct _PulsAttrRow {
    GObject parent_instance;
    guint8   id;
    gchar   *name;
    gint     current;
    gint     worst;
    gint     threshold;
    guint64  raw_value;
    gchar   *raw_string;
    gboolean failed_past;
    gboolean failing_now;
};

G_DEFINE_TYPE (PulsAttrRow, puls_attr_row, G_TYPE_OBJECT)

static void
puls_attr_row_finalize (GObject *object)
{
    PulsAttrRow *self = PULS_ATTR_ROW (object);
    g_free (self->name);
    g_free (self->raw_string);
    G_OBJECT_CLASS (puls_attr_row_parent_class)->finalize (object);
}

static void
puls_attr_row_class_init (PulsAttrRowClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = puls_attr_row_finalize;
}

static void
puls_attr_row_init (PulsAttrRow *self G_GNUC_UNUSED)
{
}

static PulsAttrRow *
puls_attr_row_new (const PulsSmartAttribute *attr)
{
    PulsAttrRow *row = g_object_new (PULS_TYPE_ATTR_ROW, NULL);
    row->id         = attr->id;
    row->name       = g_strdup (attr->name ? attr->name : "Unknown");
    row->current    = attr->current;
    row->worst      = attr->worst;
    row->threshold  = attr->threshold;
    row->raw_value  = attr->raw_value;
    row->raw_string = g_strdup (attr->raw_string ? attr->raw_string : "0");
    row->failed_past  = attr->failed_past;
    row->failing_now  = attr->failing_now;
    return row;
}

struct _PulsSmartTable {
    GtkWidget parent_instance;

    GtkWidget      *scrolled_window;
    GtkWidget      *column_view;
    GtkWidget      *empty_label;
    GtkWidget      *stack;
    GListStore     *store;
};

G_DEFINE_TYPE (PulsSmartTable, puls_smart_table, GTK_TYPE_WIDGET)

static void
setup_label_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
                  GtkListItem        *list_item,
                  gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_add_css_class (label, "smart-cell");
    gtk_list_item_set_child (list_item, label);
}

static void
bind_id_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
              GtkListItem        *list_item,
              gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_list_item_get_child (list_item);
    PulsAttrRow *row = gtk_list_item_get_item (list_item);

    g_autofree gchar *text = g_strdup_printf ("%02X", row->id);
    gtk_label_set_text (GTK_LABEL (label), text);

    gtk_widget_remove_css_class (label, "attr-good");
    gtk_widget_remove_css_class (label, "attr-caution");
    gtk_widget_remove_css_class (label, "attr-bad");

    if (row->failing_now)
        gtk_widget_add_css_class (label, "attr-bad");
    else if (row->failed_past || (row->threshold > 0 && row->current - row->threshold <= 10))
        gtk_widget_add_css_class (label, "attr-caution");
    else
        gtk_widget_add_css_class (label, "attr-good");
}

static void
bind_name_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
                GtkListItem        *list_item,
                gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_list_item_get_child (list_item);
    PulsAttrRow *row = gtk_list_item_get_item (list_item);
    gtk_label_set_text (GTK_LABEL (label), row->name);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
}

static void
bind_current_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
                   GtkListItem        *list_item,
                   gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_list_item_get_child (list_item);
    PulsAttrRow *row = gtk_list_item_get_item (list_item);
    g_autofree gchar *text = g_strdup_printf ("%d", row->current);
    gtk_label_set_text (GTK_LABEL (label), text);
    gtk_label_set_xalign (GTK_LABEL (label), 1.0);
}

static void
bind_worst_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
                 GtkListItem        *list_item,
                 gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_list_item_get_child (list_item);
    PulsAttrRow *row = gtk_list_item_get_item (list_item);
    g_autofree gchar *text = g_strdup_printf ("%d", row->worst);
    gtk_label_set_text (GTK_LABEL (label), text);
    gtk_label_set_xalign (GTK_LABEL (label), 1.0);
}

static void
bind_threshold_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
                     GtkListItem        *list_item,
                     gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_list_item_get_child (list_item);
    PulsAttrRow *row = gtk_list_item_get_item (list_item);
    g_autofree gchar *text = g_strdup_printf ("%d", row->threshold);
    gtk_label_set_text (GTK_LABEL (label), text);
    gtk_label_set_xalign (GTK_LABEL (label), 1.0);
}

static void
bind_raw_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
               GtkListItem        *list_item,
               gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_list_item_get_child (list_item);
    PulsAttrRow *row = gtk_list_item_get_item (list_item);
    
    g_autofree gchar *hex_str = g_strdup_printf ("%012llX", (unsigned long long)row->raw_value);
    gtk_label_set_text (GTK_LABEL (label), hex_str);
    gtk_label_set_xalign (GTK_LABEL (label), 1.0);
    gtk_widget_set_tooltip_text (label, row->raw_string);
}

static void
bind_status_cell (GtkListItemFactory *factory G_GNUC_UNUSED,
                  GtkListItem        *list_item,
                  gpointer            user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_list_item_get_child (list_item);
    PulsAttrRow *row = gtk_list_item_get_item (list_item);

    gtk_widget_remove_css_class (label, "status-ok");
    gtk_widget_remove_css_class (label, "status-warn");
    gtk_widget_remove_css_class (label, "status-fail");

    if (row->failing_now) {
        gtk_label_set_text (GTK_LABEL (label), "● FAIL");
        gtk_widget_add_css_class (label, "status-fail");
    } else if (row->failed_past) {
        gtk_label_set_text (GTK_LABEL (label), "● Past");
        gtk_widget_add_css_class (label, "status-warn");
    } else if (row->threshold > 0 && row->current > 0 &&
               row->current - row->threshold <= 10) {
        gtk_label_set_text (GTK_LABEL (label), "● Warn");
        gtk_widget_add_css_class (label, "status-warn");
    } else {
        gtk_label_set_text (GTK_LABEL (label), "● OK");
        gtk_widget_add_css_class (label, "status-ok");
    }
}

static GtkColumnViewColumn *
create_column (const gchar                 *title,
               gint                         fixed_width,
               GCallback                    bind_func)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (setup_label_cell), NULL);
    g_signal_connect (factory, "bind", bind_func, NULL);

    GtkColumnViewColumn *col = gtk_column_view_column_new (title, factory);
    if (fixed_width > 0)
        gtk_column_view_column_set_fixed_width (col, fixed_width);
    else
        gtk_column_view_column_set_expand (col, TRUE);

    return col;
}

static void
puls_smart_table_dispose (GObject *object)
{
    PulsSmartTable *self = PULS_SMART_TABLE (object);
    g_clear_pointer (&self->stack, gtk_widget_unparent);
    G_OBJECT_CLASS (puls_smart_table_parent_class)->dispose (object);
}

static void
puls_smart_table_class_init (PulsSmartTableClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = puls_smart_table_dispose;
    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name (widget_class, "smart-table");
}

static void
puls_smart_table_init (PulsSmartTable *self)
{
    self->store = g_list_store_new (PULS_TYPE_ATTR_ROW);

    self->stack = gtk_stack_new ();
    gtk_widget_set_parent (self->stack, GTK_WIDGET (self));
    gtk_widget_set_vexpand (self->stack, TRUE);
    gtk_widget_set_hexpand (self->stack, TRUE);

    self->empty_label = gtk_label_new ("No S.M.A.R.T. attributes available");
    gtk_widget_add_css_class (self->empty_label, "dim-label");
    gtk_stack_add_named (GTK_STACK (self->stack), self->empty_label, "empty");

    GtkNoSelection *selection = gtk_no_selection_new (G_LIST_MODEL (g_object_ref (self->store)));

    self->column_view = gtk_column_view_new (GTK_SELECTION_MODEL (selection));
    gtk_widget_add_css_class (self->column_view, "smart-column-view");
    gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (self->column_view), TRUE);
    gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (self->column_view), TRUE);

    gtk_column_view_append_column (GTK_COLUMN_VIEW (self->column_view),
        create_column ("ID", 50, G_CALLBACK (bind_id_cell)));
    gtk_column_view_append_column (GTK_COLUMN_VIEW (self->column_view),
        create_column ("Attribute Name", -1, G_CALLBACK (bind_name_cell)));
    gtk_column_view_append_column (GTK_COLUMN_VIEW (self->column_view),
        create_column ("Current", 80, G_CALLBACK (bind_current_cell)));
    gtk_column_view_append_column (GTK_COLUMN_VIEW (self->column_view),
        create_column ("Worst", 70, G_CALLBACK (bind_worst_cell)));
    gtk_column_view_append_column (GTK_COLUMN_VIEW (self->column_view),
        create_column ("Thresh", 70, G_CALLBACK (bind_threshold_cell)));
    gtk_column_view_append_column (GTK_COLUMN_VIEW (self->column_view),
        create_column ("Raw Value", 140, G_CALLBACK (bind_raw_cell)));
    gtk_column_view_append_column (GTK_COLUMN_VIEW (self->column_view),
        create_column ("Status", 80, G_CALLBACK (bind_status_cell)));

    self->scrolled_window = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_min_content_height (
        GTK_SCROLLED_WINDOW (self->scrolled_window), 200);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                  self->column_view);

    gtk_stack_add_named (GTK_STACK (self->stack), self->scrolled_window, "table");
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");

    gtk_widget_add_css_class (GTK_WIDGET (self), "smart-table");
}

GtkWidget *
puls_smart_table_new (void)
{
    return g_object_new (PULS_TYPE_SMART_TABLE, NULL);
}

void
puls_smart_table_set_data (PulsSmartTable *self,
                           PulsSmartData  *data)
{
    g_return_if_fail (PULS_IS_SMART_TABLE (self));

    g_list_store_remove_all (self->store);

    if (data == NULL) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
        return;
    }

    GArray *attrs = puls_smart_data_get_ata_attributes (data);
    if (attrs == NULL || attrs->len == 0) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
        return;
    }

    for (guint i = 0; i < attrs->len; i++) {
        PulsSmartAttribute *attr = &g_array_index (attrs, PulsSmartAttribute, i);
        PulsAttrRow *row = puls_attr_row_new (attr);
        g_list_store_append (self->store, row);
        g_object_unref (row);
    }

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "table");
}

void
puls_smart_table_clear (PulsSmartTable *self)
{
    g_return_if_fail (PULS_IS_SMART_TABLE (self));
    g_list_store_remove_all (self->store);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
}
