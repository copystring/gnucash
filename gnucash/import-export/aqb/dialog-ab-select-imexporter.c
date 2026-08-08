/*
 * dialog-ab-select-imexporter.c --
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652
 * Boston, MA  02110-1301,  USA       gnu@gnu.org
 */

/**
 * @internal
 * @file dialog-ab-select-imexporter.h
 * @brief Dialog to select AQBanking importer/exporter and format profile.
 * @author  Copyright (C) 2022 John Ralls <jralls@ceridwen.us>
 */

#include <config.h>

#include <stdbool.h>
#include <glib/gi18n.h>
#include "dialog-ab-select-imexporter.h"
#include <dialog-utils.h>

__attribute__((unused)) static QofLogModule log_module = G_LOG_DOMAIN;

struct _GncABSelectImExDlg
{
    GtkWidget *dialog;
    GtkWidget *parent;
    GtkStringList *imexporter_list;
    GtkStringList *profile_list;
    GtkSingleSelection *imexporter_selection;
    GtkSingleSelection *profile_selection;
    GtkWidget *select_imexporter;
    GtkWidget *select_profile;
    GtkWidget *ok_button;

    AB_BANKING* abi;
};

// Expose the selection handlers to GtkBuilder.
static void imexporter_changed (GtkSelectionModel *selection, guint position,
                                guint n_items, gpointer data);
static void profile_changed (GtkSelectionModel *selection, guint position,
                             guint n_items, gpointer data);

static void
clear_widget_pointer (GtkWidget *widget, gpointer data)
{
    (void)widget;
    *((GtkWidget**)data) = NULL;
}

enum
{
    NAME_COL,
    PROF_COL
};

static guint
populate_list_store (GtkStringList *model, GList *entries)
{
    guint count = 0;
    gtk_string_list_splice (model, 0, g_list_model_get_n_items (G_LIST_MODEL (model)), NULL);
    for (GList* node = entries; node; node = g_list_next (node))
    {
        AB_Node_Pair *pair = (AB_Node_Pair*)(node->data);
        GtkStringObject *item;
        gtk_string_list_append (model, pair->name);
        item = g_list_model_get_item (G_LIST_MODEL (model), count++);
        g_object_set_data_full (G_OBJECT (item), "description", g_strdup (pair->descr), g_free);
        g_object_unref (item);
        g_slice_free1 (sizeof(AB_Node_Pair), pair);
    }
    return count;
}

static void
text_factory_setup (GtkListItemFactory *factory, GtkListItem *list_item, gpointer)
{
    GtkWidget *label = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_list_item_set_child (list_item, label);
}

static void
text_factory_bind (GtkListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
    GtkStringObject *item = GTK_STRING_OBJECT (gtk_list_item_get_item (list_item));
    const gchar *text = GPOINTER_TO_INT (user_data)
        ? g_object_get_data (G_OBJECT (item), "description")
        : gtk_string_object_get_string (item);
    gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (list_item)), text ? text : "");
}

static GtkWidget *
create_selection_view (GtkStringList *list, GtkSingleSelection **selection_out,
                       const gchar *first_title)
{
    GtkSingleSelection *selection = gtk_single_selection_new (G_LIST_MODEL (list));
    GtkWidget *view = gtk_column_view_new (GTK_SELECTION_MODEL (selection));
    GtkListItemFactory *name_factory = gtk_signal_list_item_factory_new ();
    GtkListItemFactory *description_factory = gtk_signal_list_item_factory_new ();
    GtkColumnViewColumn *column;

    gtk_single_selection_set_autoselect (selection, FALSE);
    g_signal_connect (name_factory, "setup", G_CALLBACK (text_factory_setup), NULL);
    g_signal_connect (name_factory, "bind", G_CALLBACK (text_factory_bind), NULL);
    column = gtk_column_view_column_new (first_title, name_factory);
    gtk_column_view_append_column (GTK_COLUMN_VIEW (view), column);

    g_signal_connect (description_factory, "setup", G_CALLBACK (text_factory_setup), NULL);
    g_signal_connect (description_factory, "bind", G_CALLBACK (text_factory_bind), GINT_TO_POINTER (1));
    column = gtk_column_view_column_new (_("Description"), description_factory);
    gtk_column_view_column_set_expand (column, TRUE);
    gtk_column_view_append_column (GTK_COLUMN_VIEW (view), column);

    *selection_out = selection;
    return view;
}

GncABSelectImExDlg*
gnc_ab_select_imex_dlg_new (GtkWidget* parent, AB_BANKING* abi)
{
    GncABSelectImExDlg* imexd;
    GtkBuilder* builder;
    GList* imexporters;

    g_return_val_if_fail (abi, NULL);
    imexporters = gnc_ab_imexporter_list (abi);
    g_return_val_if_fail (imexporters, NULL);
    imexd = g_new0(GncABSelectImExDlg, 1);
    imexd->parent = parent;
    imexd->abi = abi;

    builder = gtk_builder_new();
    gnc_builder_add_from_file (builder, "dialog-ab.glade",
                               "aqbanking-select-imexporter-dialog");
    imexd->dialog =
        GTK_WIDGET (gtk_builder_get_object (builder,
                                            "aqbanking-select-imexporter-dialog"));
    g_signal_connect (imexd->dialog, "destroy",
                      G_CALLBACK (clear_widget_pointer), &imexd->dialog);
    imexd->imexporter_list = gtk_string_list_new (NULL);
    imexd->profile_list = gtk_string_list_new (NULL);
    imexd->select_imexporter = create_selection_view (imexd->imexporter_list,
                                                       &imexd->imexporter_selection,
                                                       _("File Format"));
    imexd->select_profile = create_selection_view (imexd->profile_list,
                                                   &imexd->profile_selection,
                                                   _("Profiles"));
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (gtk_builder_get_object (
                                       builder, "imexporter-scroll")), imexd->select_imexporter);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (gtk_builder_get_object (
                                       builder, "profile-scroll")), imexd->select_profile);
    imexd->ok_button =
        GTK_WIDGET (gtk_builder_get_object (builder, "imex-okbutton"));

    populate_list_store (imexd->imexporter_list,
                         imexporters);

    g_signal_connect (imexd->imexporter_selection, "selection-changed", G_CALLBACK(imexporter_changed),
                      imexd);
    g_signal_connect (imexd->profile_selection, "selection-changed", G_CALLBACK(profile_changed),
                      imexd);
    g_list_free (imexporters);
    g_object_unref (G_OBJECT (builder));

    gtk_window_set_transient_for (GTK_WINDOW (imexd->dialog),
                                  GTK_WINDOW (imexd->parent));

    return imexd;
}

void
gnc_ab_select_imex_dlg_destroy (GncABSelectImExDlg* imexd)
{

    if (imexd->imexporter_list)
        gtk_string_list_splice (imexd->imexporter_list, 0,
                                g_list_model_get_n_items (G_LIST_MODEL (imexd->imexporter_list)), NULL);

    if (imexd->profile_list)
        gtk_string_list_splice (imexd->profile_list, 0,
                                g_list_model_get_n_items (G_LIST_MODEL (imexd->profile_list)), NULL);

    if (imexd->dialog)
        gtk_window_destroy (GTK_WINDOW(imexd->dialog));

    g_free (imexd);
}

void
imexporter_changed (GtkSelectionModel *selection, guint, guint, gpointer data)
{
    GncABSelectImExDlg* imexd = (GncABSelectImExDlg*)data;
    GtkStringObject *item;

    gtk_widget_set_sensitive (imexd->ok_button, FALSE);

    item = gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (selection));
    if (item)
    {
        GList* profiles = NULL;
        guint profile_count;
        const char *name = gtk_string_object_get_string (item);

        if (name && *name)
            profiles = gnc_ab_imexporter_profile_list (imexd->abi, name);

        gtk_string_list_splice (imexd->profile_list, 0,
                                g_list_model_get_n_items (G_LIST_MODEL (imexd->profile_list)), NULL);

        if (profiles)
        {
             profile_count = populate_list_store (imexd->profile_list, profiles);
        }
        else
        {
            gtk_widget_set_sensitive (imexd->ok_button, TRUE);
            g_object_unref (item);
            return;
        }

        if (profile_count == 1)
            gtk_single_selection_set_selected (imexd->profile_selection, 0);
        g_list_free (profiles);
        g_object_unref (item);
        return;
    }
}

void
profile_changed (GtkSelectionModel *selection, guint, guint, gpointer data)
{
    GncABSelectImExDlg* imexd = (GncABSelectImExDlg*)data;

    gtk_widget_set_sensitive (imexd->ok_button,
                              gtk_single_selection_get_selected (GTK_SINGLE_SELECTION (selection))
                              != GTK_INVALID_LIST_POSITION);
}

gboolean
gnc_ab_select_imex_dlg_run (GncABSelectImExDlg* imexd)
{
    int response = gnc_dialog_run_non_destructive (GTK_DIALOG (imexd->dialog));

    return response == GTK_RESPONSE_OK ? TRUE : FALSE;
}

static char*
selection_get_name (GtkSingleSelection *selection)
{
    GtkStringObject *item = gtk_single_selection_get_selected_item (selection);
    char *name = item ? g_strdup (gtk_string_object_get_string (item)) : NULL;
    g_clear_object (&item);
    return name;
}

static void
selection_set_name (GtkSingleSelection *selection, const char* name)
{
    GListModel *model = gtk_single_selection_get_model (selection);
    for (guint index = 0; index < g_list_model_get_n_items (model); index++)
    {
        GtkStringObject *item = g_list_model_get_item (model, index);
        if (!g_strcmp0(name, gtk_string_object_get_string (item)))
        {
            gtk_single_selection_set_selected (selection, index);
            g_object_unref (item);
            break;
        }
        g_object_unref (item);
    }
}

char*
gnc_ab_select_imex_dlg_get_imexporter_name (GncABSelectImExDlg* imexd)
{
    return selection_get_name (imexd->imexporter_selection);
}

char*
gnc_ab_select_imex_dlg_get_profile_name (GncABSelectImExDlg* imexd)
{
    return selection_get_name (imexd->profile_selection);
}

void
gnc_ab_select_imex_dlg_set_imexporter_name (GncABSelectImExDlg* imexd, const char* name)
{
    if (name)
        selection_set_name (imexd->imexporter_selection, name);
}

void
gnc_ab_select_imex_dlg_set_profile_name (GncABSelectImExDlg* imexd, const char* name)
{
    if (name)
        selection_set_name (imexd->profile_selection, name);
}
