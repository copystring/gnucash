/*
 * dialog-tax-table.c -- Dialog to create and edit tax-tables
 * Copyright (C) 2002 Derek Atkins
 * Author: Derek Atkins <warlord@MIT.EDU>
 *
 * Copyright (c) 2006 David Hampton <hampton@employees.org>
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

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "dialog-utils.h"
#include "gnc-component-manager.h"
#include "gnc-session.h"
#include "gnc-ui.h"
#include "gnc-gui-query.h"
#include "gnc-gtk-utils.h"
#include "gnc-ui-util.h"
#include "qof.h"
#include "gnc-amount-edit.h"
#include "gnc-tree-view-account.h"
#include "gnc-account-sel.h"

#include "gncTaxTable.h"
#include "dialog-tax-table.h"

#define DIALOG_TAX_TABLE_CM_CLASS "tax-table-dialog"
#define GNC_PREFS_GROUP "dialogs.business.tax-tables"

enum tax_table_cols
{
    TAX_TABLE_COL_NAME = 0,
    TAX_TABLE_COL_POINTER,
    NUM_TAX_TABLE_COLS
};

enum tax_entry_cols
{
    TAX_ENTRY_COL_NAME = 0,
    TAX_ENTRY_COL_POINTER,
    TAX_ENTRY_COL_AMOUNT,
    NUM_TAX_ENTRY_COLS
};

void tax_table_new_table_cb (GtkButton *button, TaxTableWindow *ttw);
void tax_table_rename_table_cb (GtkButton *button, TaxTableWindow *ttw);
void tax_table_delete_table_cb (GtkButton *button, TaxTableWindow *ttw);
void tax_table_new_entry_cb (GtkButton *button, TaxTableWindow *ttw);
void tax_table_edit_entry_cb (GtkButton *button, TaxTableWindow *ttw);
void tax_table_delete_entry_cb (GtkButton *button, TaxTableWindow *ttw);
void tax_table_window_close (GtkWidget *widget, gpointer data);
void tax_table_window_destroy_cb (GtkWidget *widget, gpointer data);

struct _taxtable_window
{
    GtkWidget *dialog;
    GtkWidget *names_view;
    GtkWidget *entries_view;

    GncTaxTable      *current_table;
    GncTaxTableEntry *current_entry;
    QofBook          *book;
    gint              component_id;
    QofSession       *session;
};

typedef struct _new_taxtable
{
    GtkWidget *dialog;
    GtkWidget *name_entry;
    GtkWidget *amount_entry;
    GtkWidget *acct_tree;

    GncTaxTable      *created_table;
    TaxTableWindow   *ttw;
    GncTaxTableEntry *entry;
    gint              type;
    gboolean          new_table;
} NewTaxTable;

static gboolean
new_tax_table_check_entry (NewTaxTable *ntt, GError **error)
{
    GNCPrintAmountInfo print_info;
    gnc_numeric value;
    gint result;
    GError *tmp_error = NULL;

    if (ntt->type == GNC_AMT_TYPE_VALUE)
    {
        Account *acc = gnc_tree_view_account_get_selected_account (GNC_TREE_VIEW_ACCOUNT(ntt->acct_tree));
        gnc_commodity *currency = xaccAccountGetCommodity (acc);
        print_info = gnc_commodity_print_info (currency, FALSE);
        gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT(ntt->amount_entry),
                                      gnc_commodity_get_fraction (currency));
    }
    else
    {
        print_info = gnc_integral_print_info ();
        print_info.max_decimal_places = 5;
        gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT (ntt->amount_entry), 100000);
    }

    gnc_amount_edit_set_print_info (GNC_AMOUNT_EDIT(ntt->amount_entry), print_info);

    result = gnc_amount_edit_expr_is_valid (GNC_AMOUNT_EDIT(ntt->amount_entry),
                                            &value, TRUE, &tmp_error);

    if (result == 1)
    {
        if (error)
            g_propagate_error (error, tmp_error);
        else
            g_error_free (tmp_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean
new_tax_table_ok_cb (NewTaxTable *ntt)
{
    TaxTableWindow *ttw;
    const char *name = NULL;
    char *message;
    Account *acc;
    gnc_numeric amount;
    GError *error = NULL;

    g_return_val_if_fail (ntt, FALSE);
    ttw = ntt->ttw;

    /* Verify that we've got real, valid data */

    /* verify the name, maybe */
    if (ntt->new_table)
    {
        name = gnc_entry_get_text (GTK_ENTRY(ntt->name_entry));
        if (name == NULL || *name == '\0')
        {
            message = _("You must provide a name for this Tax Table.");
            gnc_error_dialog (GTK_WINDOW(ntt->dialog), "%s", message);
            return FALSE;
        }
        if (gncTaxTableLookupByName (ttw->book, name))
        {
            message = g_strdup_printf (_(
                                          "You must provide a unique name for this Tax Table. "
                                          "Your choice \"%s\" is already in use."), name);
            gnc_error_dialog (GTK_WINDOW(ntt->dialog), "%s", message);
            g_free (message);
            return FALSE;
        }
    }

    /* test for valid value */
    if (!new_tax_table_check_entry (ntt, &error))
    {
        message = g_strdup (error->message);
        gnc_error_dialog (GTK_WINDOW(ntt->dialog), "%s", message);
        g_free (message);
        g_error_free (error);
        return FALSE;
    }

    /* verify the amount. Note that negative values are allowed (required for European tax rules) */
    amount = gnc_amount_edit_get_amount (GNC_AMOUNT_EDIT(ntt->amount_entry));
    if (ntt->type == GNC_AMT_TYPE_PERCENT &&
            gnc_numeric_compare (gnc_numeric_abs (amount),
                                 gnc_numeric_create (100, 1)) > 0)
    {
        message = _("Percentage amount must be between -100 and 100.");
        gnc_error_dialog (GTK_WINDOW(ntt->dialog), "%s", message);
        return FALSE;
    }

    /* verify the account */
    acc = gnc_tree_view_account_get_selected_account (GNC_TREE_VIEW_ACCOUNT(ntt->acct_tree));
    if (acc == NULL)
    {
        message = _("You must choose a Tax Account.");
        gnc_error_dialog (GTK_WINDOW(ntt->dialog), "%s", message);
        return FALSE;
    }

    gnc_suspend_gui_refresh ();

    /* Ok, it's all valid, now either change to add this thing */
    if (ntt->new_table)
    {
        GncTaxTable *table = gncTaxTableCreate (ttw->book);
        gncTaxTableBeginEdit (table);
        gncTaxTableSetName (table, name);
        /* Reset the current table */
        ttw->current_table = table;
        ntt->created_table = table;
    }
    else
        gncTaxTableBeginEdit (ttw->current_table);

    /* Create/edit the entry */
    {
        GncTaxTableEntry *entry;

        if (ntt->entry)
        {
            entry = ntt->entry;
        }
        else
        {
            entry = gncTaxTableEntryCreate ();
            gncTaxTableAddEntry (ttw->current_table, entry);
            ttw->current_entry = entry;
        }

        gncTaxTableEntrySetAccount (entry, acc);
        gncTaxTableEntrySetType (entry, ntt->type);
        gncTaxTableEntrySetAmount (entry, amount);
    }

    /* Mark the table as changed and commit it */
    gncTaxTableChanged (ttw->current_table);
    gncTaxTableCommitEdit (ttw->current_table);

    gnc_resume_gui_refresh ();
    return TRUE;
}

static void
combo_changed (GtkWidget *widget, NewTaxTable *ntt)
{
    gint index;

    g_return_if_fail (GTK_IS_COMBO_BOX(widget));
    g_return_if_fail (ntt);

    index = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
    ntt->type = index + 1;

    new_tax_table_check_entry (ntt, NULL);
    (void)selection;
    (void)position;
    (void)n_items;
}

static void
tax_table_account_selection_changed_cb (GtkSelectionModel *selection, guint position,
                                      guint n_items, NewTaxTable *ntt)
{
    new_tax_table_check_entry (ntt, NULL);
    (void)selection;
    (void)position;
    (void)n_items;
}

static GncTaxTable *
new_tax_table_dialog (TaxTableWindow *ttw, gboolean new_table,
                      GncTaxTableEntry *entry, const char *name)
{
    GncTaxTable *created_table = NULL;
    NewTaxTable *ntt;
    GtkBuilder *builder;
    GtkWidget *box, *widget, *combo;
    gboolean done;
    gint response, index;
    GtkSelectionModel *account_selection;

    if (!ttw) return NULL;
    if (new_table && entry) return NULL;

    ntt = g_new0 (NewTaxTable, 1);
    ntt->ttw = ttw;
    ntt->entry = entry;
    ntt->new_table = new_table;

    if (entry)
        ntt->type = gncTaxTableEntryGetType (entry);
    else
        ntt->type = GNC_AMT_TYPE_PERCENT;

    /* Open and read the Glade File */
    builder = gtk_builder_new ();
    gtk_builder_set_current_object (builder, G_OBJECT(ntt));
    gnc_builder_add_from_file (builder, "dialog-tax-table.glade", "type_liststore");
    gnc_builder_add_from_file (builder, "dialog-tax-table.glade", "new_tax_table_dialog");

    ntt->dialog = GTK_WIDGET(gtk_builder_get_object (builder, "new_tax_table_dialog"));

    // Set the name for this dialog so it can be easily manipulated with css
    gtk_widget_set_name (GTK_WIDGET(ntt->dialog), "gnc-id-tax-table");
    gnc_widget_style_context_add_class (GTK_WIDGET(ntt->dialog), "gnc-class-taxes");

    ntt->name_entry = GTK_WIDGET(gtk_builder_get_object (builder, "name_entry"));
    if (name)
        gnc_entry_set_text (GTK_ENTRY(ntt->name_entry), name);

    /* Create the menu */
    combo = GTK_WIDGET(gtk_builder_get_object (builder, "type_combobox"));
    index = ntt->type ? ntt->type : GNC_AMT_TYPE_VALUE;
    gtk_combo_box_set_active (GTK_COMBO_BOX(combo), index - 1);
    g_signal_connect (combo, "changed", G_CALLBACK(combo_changed), ntt);

    /* Attach our own widgets */
    box = GTK_WIDGET(gtk_builder_get_object (builder, "amount_box"));
    ntt->amount_entry = widget = gnc_amount_edit_new ();
    gnc_amount_edit_set_evaluate_on_enter (GNC_AMOUNT_EDIT(widget), TRUE);
    gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT(widget), 100000);
    gtk_box_append (GTK_BOX(box), GTK_WIDGET(widget));

    box = GTK_WIDGET(gtk_builder_get_object (builder, "acct_window"));
    ntt->acct_tree = GTK_WIDGET(gnc_tree_view_account_new (FALSE));
    gtk_box_prepend (GTK_BOX(box), GTK_WIDGET(ntt->acct_tree));
    gnc_tree_view_account_set_headers_visible (
        GNC_TREE_VIEW_ACCOUNT (ntt->acct_tree), FALSE);
    account_selection = gnc_tree_view_account_get_selection_model (
        GNC_TREE_VIEW_ACCOUNT (ntt->acct_tree));
    g_signal_connect (account_selection, "selection-changed",
                      G_CALLBACK (tax_table_account_selection_changed_cb), ntt);

    /* Make 'enter' do the right thing */
    gtk_entry_set_activates_default (GTK_ENTRY(gnc_amount_edit_gtk_entry
                                    (GNC_AMOUNT_EDIT(ntt->amount_entry))),
                                    TRUE);

    /* Fix mnemonics for generated target widgets */
    widget = GTK_WIDGET(gtk_builder_get_object (builder, "value_label"));
    gnc_amount_edit_make_mnemonic_target (GNC_AMOUNT_EDIT(ntt->amount_entry), widget);
    widget = GTK_WIDGET(gtk_builder_get_object (builder, "account_label"));
    gtk_label_set_mnemonic_widget (GTK_LABEL(widget), ntt->acct_tree);

    /* Fill in the widgets appropriately */
    if (entry)
    {
        gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT(ntt->amount_entry),
                                    gncTaxTableEntryGetAmount (entry));
        gnc_tree_view_account_set_selected_account (GNC_TREE_VIEW_ACCOUNT(ntt->acct_tree),
                gncTaxTableEntryGetAccount (entry));
    }

    /* Set our parent */
    gtk_window_set_transient_for (GTK_WINDOW(ntt->dialog), GTK_WINDOW(ttw->dialog));

    /* Setup signals */
gnc_builder_connect_signals_full (builder, gnc_builder_connect_full_func, ntt);

    /* Show what we should */
//FIXME gtk4    gtk_widget_show_all (ntt->dialog);
    if (new_table == FALSE)
    {
        gtk_widget_set_visible (GTK_WIDGET(gtk_builder_get_object (builder, "table_title")), FALSE);
        gtk_widget_set_visible (GTK_WIDGET(gtk_builder_get_object (builder, "table_name")), FALSE);
        gtk_widget_set_visible (GTK_WIDGET(gtk_builder_get_object (builder, "spacer")), FALSE);
        gtk_widget_set_visible (GTK_WIDGET(ntt->name_entry), FALSE);

        /* Tables are great for layout, but a pain when you hide widgets */
        GTK_WIDGET(gtk_builder_get_object (builder, "ttd_table"));
        gtk_widget_grab_focus (gnc_amount_edit_gtk_entry
                               (GNC_AMOUNT_EDIT(ntt->amount_entry)));
    }
    else
        gtk_widget_grab_focus (ntt->name_entry);

    /* Display the dialog now that we're done manipulating it */
    gtk_widget_set_visible (GTK_WIDGET(ntt->dialog), TRUE);

    done = FALSE;
    while (!done)
    {
//FIXME gtk4        response = gtk_dialog_run (GTK_DIALOG(ntt->dialog));
gtk_window_set_modal (GTK_WINDOW(ntt->dialog), TRUE); //FIXME gtk4
response = GTK_RESPONSE_CANCEL; //FIXME gtk4

        switch (response)
        {
        case GTK_RESPONSE_OK:
            if (new_tax_table_ok_cb (ntt))
            {
                created_table = ntt->created_table;
                done = TRUE;
            }
            break;
        default:
            done = TRUE;
            break;
        }
    }

    g_object_unref (G_OBJECT(builder));

//FIXME gtk4    gtk_window_destroy (GTK_WINDOW(ntt->dialog));
    g_free (ntt);

    return created_table;
}

/***********************************************************************/

static void
tax_table_entries_refresh (TaxTableWindow *ttw)
{
    GList *list, *node;
    GtkTreeView *view;
    GtkListStore *store;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeSelection *selection;
    GtkTreeRowReference *reference = NULL;
    GncTaxTableEntry *selected_entry;

    g_return_if_fail (ttw);

    view = GTK_TREE_VIEW(ttw->entries_view);
    store = GTK_LIST_STORE(gtk_tree_view_get_model (view));

    /* Clear the list */
    selected_entry = ttw->current_entry;
    gtk_list_store_clear (store);
    if (ttw->current_table == NULL)
        return;

    /* Add the items to the list */
    list = gncTaxTableGetEntries (ttw->current_table);
    if (list)
        list = g_list_reverse (g_list_copy (list));

    for (node = list ; node; node = node->next)
    {
        char *row_text[3];
        GncTaxTableEntry *entry = node->data;
        Account *acc = gncTaxTableEntryGetAccount (entry);
        gnc_numeric amount = gncTaxTableEntryGetAmount (entry);

        row_text[0] = gnc_account_get_full_name (acc);
        switch (gncTaxTableEntryGetType (entry))
        {
        case GNC_AMT_TYPE_PERCENT:
            row_text[1] =
                g_strdup_printf ("%s%%",
                                 xaccPrintAmount (amount,
                                                  gnc_default_print_info (FALSE)));
            break;
        case GNC_AMT_TYPE_VALUE:
            row_text[1] =
                g_strdup_printf ("%s",
                                 xaccPrintAmount (amount,
                                                  gnc_default_print_info (TRUE)));
            break;
         default:
             row_text[1] = NULL;
             break;
        }

        gtk_list_store_prepend (store, &iter);
        gtk_list_store_set (store, &iter,
                            TAX_ENTRY_COL_NAME, row_text[0],
                            TAX_ENTRY_COL_POINTER, entry,
                            TAX_ENTRY_COL_AMOUNT, row_text[1],
                            -1);
        if (entry == selected_entry)
        {
            path = gtk_tree_model_get_path (GTK_TREE_MODEL(store), &iter);
            reference = gtk_tree_row_reference_new (GTK_TREE_MODEL(store), path);
            gtk_tree_path_free (path);
        }

        g_free (row_text[0]);
        g_free (row_text[1]);
    }

    if (list)
        g_list_free (list);

    if (reference)
    {
        path = gtk_tree_row_reference_get_path (reference);
        gtk_tree_row_reference_free (reference);
        if (path)
        {
            selection = gtk_tree_view_get_selection (view);
            gtk_tree_selection_select_path (selection, path);
            gtk_tree_view_scroll_to_cell (view, path, NULL, TRUE, 0.5, 0.0);
            gtk_tree_path_free (path);
        }
    }
}

static void
tax_table_window_refresh (TaxTableWindow *ttw)
{
    GList *list, *node;
    GtkTreeView *view;
    GtkListStore *store;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeSelection *selection;
    GtkTreeRowReference *reference = NULL;
    GncTaxTable *saved_current_table = ttw->current_table;

    g_return_if_fail (ttw);
    view = GTK_TREE_VIEW(ttw->names_view);
    store = GTK_LIST_STORE(gtk_tree_view_get_model (view));

    /* Clear the list */
    gtk_list_store_clear(store);

    gnc_gui_component_clear_watches (ttw->component_id);

    /* Add the items to the list */
    list = gncTaxTableGetTables (ttw->book);
    if (list)
        list = g_list_reverse (g_list_copy (list));

    for (node = list; node; node = node->next)
    {
        GncTaxTable *table = node->data;

        gnc_gui_component_watch_entity (ttw->component_id,
                                        gncTaxTableGetGUID (table),
                                        QOF_EVENT_MODIFY);

        gtk_list_store_prepend (store, &iter);
        gtk_list_store_set (store, &iter,
                            TAX_TABLE_COL_NAME, gncTaxTableGetName (table),
                            TAX_TABLE_COL_POINTER, table,
                            -1);

        if (table == saved_current_table)
        {
            path = gtk_tree_model_get_path (GTK_TREE_MODEL(store), &iter);
            reference = gtk_tree_row_reference_new (GTK_TREE_MODEL(store), path);
            gtk_tree_path_free (path);
        }
    }

    if (list)
        g_list_free (list);

    gnc_gui_component_watch_entity_type (ttw->component_id,
                                         GNC_TAXTABLE_MODULE_NAME,
                                         QOF_EVENT_CREATE | QOF_EVENT_DESTROY);

    if (reference)
    {
        path = gtk_tree_row_reference_get_path (reference);
        gtk_tree_row_reference_free (reference);
        if (path)
        {
            selection = gtk_tree_view_get_selection (view);
            gtk_tree_selection_select_path (selection, path);
            gtk_tree_view_scroll_to_cell (view, path, NULL, TRUE, 0.5, 0.0);
            gtk_tree_path_free (path);
        }
    }

    tax_table_entries_refresh (ttw);
    /* select_row() above will refresh the entries window */
}

static void
tax_table_selection_changed (GtkTreeSelection *selection,
                             gpointer          user_data)
{
    TaxTableWindow *ttw = user_data;
    GncTaxTable *table;
    GtkTreeModel *model;
    GtkTreeIter iter;

    g_return_if_fail (ttw);

    if (!gtk_tree_selection_get_selected (selection, &model, &iter))
        return;

    gtk_tree_model_get (model, &iter, TAX_TABLE_COL_POINTER, &table, -1);
    g_return_if_fail (table);

    /* If we've changed, then reset the entry list */
    if (table != ttw->current_table)
    {
        ttw->current_table = table;
        ttw->current_entry = NULL;
    }
    /* And force a refresh of the entries */
    tax_table_entries_refresh (ttw);
}

static void
tax_table_entry_selection_changed (GtkTreeSelection *selection,
                                   gpointer          user_data)
{
    TaxTableWindow *ttw = user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;

    g_return_if_fail (ttw);

    if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
        ttw->current_entry = NULL;
        return;
    }

    gtk_tree_model_get (model, &iter, TAX_ENTRY_COL_POINTER, &ttw->current_entry, -1);
}

static void
tax_table_entry_row_activated (GtkTreeView       *tree_view,
                               GtkTreePath       *path,
                               GtkTreeViewColumn *column,
                               gpointer           user_data)
{
    TaxTableWindow *ttw = user_data;

    new_tax_table_dialog (ttw, FALSE, ttw->current_entry, NULL);
}

void
tax_table_new_table_cb (GtkButton *button, TaxTableWindow *ttw)
{
    g_return_if_fail (ttw);
    new_tax_table_dialog (ttw, TRUE, NULL, NULL);
}


static const char
*rename_tax_table_dialog (GtkWidget *parent,
                          const char *title,
                          const char *msg,
                          const char *button_name,
                          const char *text)
{
    GtkWidget *vbox;
    GtkWidget *main_vbox;
    GtkWidget *label;
    GtkWidget *textbox;
    GtkWidget *dialog;
    GtkWidget *dvbox;

    main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_set_homogeneous (GTK_BOX(main_vbox), FALSE);
    gnc_box_set_all_margins (GTK_BOX(main_vbox), 6);
    gtk_widget_set_visible (GTK_WIDGET(main_vbox), TRUE);

    label = gtk_label_new (msg);
    gtk_label_set_justify (GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_box_append (GTK_BOX(main_vbox), GTK_WIDGET(label));
    gtk_widget_set_visible (GTK_WIDGET(label), TRUE);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_set_homogeneous (GTK_BOX(vbox), TRUE);
    gnc_box_set_all_margins (GTK_BOX(vbox), 6);
    gtk_box_prepend (GTK_BOX(main_vbox), GTK_WIDGET(vbox));
    gtk_widget_set_visible (GTK_WIDGET(vbox), TRUE);

    textbox = gtk_entry_new ();
    gtk_widget_set_visible (GTK_WIDGET(textbox), TRUE);
    gnc_entry_set_text (GTK_ENTRY(textbox), text);
    gtk_box_append (GTK_BOX(vbox), GTK_WIDGET(textbox));

    dialog = gtk_dialog_new_with_buttons (title, GTK_WINDOW(parent),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          _("_Cancel"), GTK_RESPONSE_CANCEL,
                                          button_name, GTK_RESPONSE_OK,
                                          NULL);
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    dvbox = gtk_dialog_get_content_area (GTK_DIALOG(dialog));
    gtk_box_append (GTK_BOX(dvbox), GTK_WIDGET(main_vbox));

//FIXME gtk4    if (gtk_dialog_run (GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
gtk_window_set_modal (GTK_WINDOW(dialog), TRUE); //FIXME gtk4
//    if (gtk_dialog_run (GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
//    {
//        gtk_window_destroy (GTK_WINDOW(dialog));
//        return NULL;
//    }

    text = g_strdup (gnc_entry_get_text (GTK_ENTRY(textbox)));
//FIXME gtk4    gtk_window_destroy (GTK_WINDOW(dialog));
    return text;
}

void
tax_table_rename_table_cb (GtkButton *button, TaxTableWindow *ttw)
{
    const char *oldname;
    const char *newname;
    g_return_if_fail (ttw);

    if (!ttw->current_table)
        return;

    oldname = gncTaxTableGetName (ttw->current_table);
    newname = rename_tax_table_dialog (ttw->dialog, (_("Rename")),
                                       (_("Please enter new name")),
                                       (_("_Rename")), oldname);

    if (newname && *newname != '\0' && (g_strcmp0 (oldname, newname) != 0))
    {
        if (gncTaxTableLookupByName (ttw->book, newname))
        {
            char *message = g_strdup_printf (_("Tax table name \"%s\" already exists."),
                                             newname);
            gnc_error_dialog (GTK_WINDOW(ttw->dialog), "%s", message);
            g_free (message);
        }
        else
        {
            gncTaxTableSetName (ttw->current_table, newname);
        }
    }
}


typedef enum
{
    TAX_TABLE_DELETE,
    TAX_TABLE_ENTRY_DELETE
} TaxTableDeleteKind;

typedef struct
{
    TaxTableWindow *ttw;
    GWeakRef dialog;
    gulong destroy_handler;
    GncGUID table_guid;
    GncTaxTableEntry *entry;
    TaxTableDeleteKind kind;
} TaxTableDeleteRequest;

static void
tax_table_delete_request_destroyed (GtkWidget *dialog,
                                    TaxTableDeleteRequest *request)
{
    (void)dialog;
    request->ttw = NULL;
    request->destroy_handler = 0;
}

static void
tax_table_delete_request_free (TaxTableDeleteRequest *request)
{
    GtkWidget *dialog = g_weak_ref_get (&request->dialog);

    if (dialog && request->destroy_handler)
        g_signal_handler_disconnect (dialog, request->destroy_handler);
    g_clear_object (&dialog);
    g_weak_ref_clear (&request->dialog);
    g_free (request);
}

static void
tax_table_delete_finished (GtkWindow *parent, gint response, gpointer user_data)
{
    TaxTableDeleteRequest *request = user_data;
    GncTaxTable *table = NULL;

    (void)parent;
    if (response == GTK_RESPONSE_YES && request->ttw &&
        !qof_book_shutting_down (request->ttw->book))
        table = gncTaxTableLookup (request->ttw->book, &request->table_guid);

    if (table && request->kind == TAX_TABLE_DELETE &&
        gncTaxTableGetRefcount (table) == 0)
    {
        gnc_suspend_gui_refresh ();
        gncTaxTableBeginEdit (table);
        gncTaxTableDestroy (table);
        if (request->ttw->current_table == table)
        {
            request->ttw->current_table = NULL;
            request->ttw->current_entry = NULL;
        }
        gnc_resume_gui_refresh ();
    }
    else if (table && request->kind == TAX_TABLE_ENTRY_DELETE && request->entry &&
             g_list_length (gncTaxTableGetEntries (table)) > 1 &&
             g_list_find (gncTaxTableGetEntries (table), request->entry))
    {
        gnc_suspend_gui_refresh ();
        gncTaxTableBeginEdit (table);
        gncTaxTableRemoveEntry (table, request->entry);
        gncTaxTableEntryDestroy (request->entry);
        gncTaxTableChanged (table);
        gncTaxTableCommitEdit (table);
        if (request->ttw->current_entry == request->entry)
            request->ttw->current_entry = NULL;
        gnc_resume_gui_refresh ();
    }

    tax_table_delete_request_free (request);
}

static void
tax_table_delete_request (TaxTableWindow *ttw, TaxTableDeleteKind kind)
{
    TaxTableDeleteRequest *request;

    request = g_new0 (TaxTableDeleteRequest, 1);
    request->ttw = ttw;
    g_weak_ref_init (&request->dialog, ttw->dialog);
    request->destroy_handler = g_signal_connect (ttw->dialog, "destroy",
                                                 G_CALLBACK (tax_table_delete_request_destroyed),
                                                 request);
    request->table_guid = gncTaxTableRetGUID (ttw->current_table);
    request->entry = kind == TAX_TABLE_ENTRY_DELETE ? ttw->current_entry : NULL;
    request->kind = kind;

    if (kind == TAX_TABLE_DELETE)
    {
        gnc_verify_dialog_async (GTK_WINDOW (ttw->dialog), FALSE,
                                 tax_table_delete_finished, request,
                                 _("Are you sure you want to delete \"%s\"?"),
                                 gncTaxTableGetName (ttw->current_table));
    }
    else
    {
        gnc_verify_dialog_async (GTK_WINDOW (ttw->dialog), FALSE,
                                 tax_table_delete_finished, request, "%s",
                                 _("Are you sure you want to delete this entry?"));
    }
}

void
tax_table_delete_table_cb (GtkButton *button, TaxTableWindow *ttw)
{
    g_return_if_fail (ttw);

    if (!ttw->current_table)
        return;

    if (gncTaxTableGetRefcount (ttw->current_table) > 0)
    {
        char *message =
            g_strdup_printf (_("Tax table \"%s\" is in use. You cannot delete it."),
                             gncTaxTableGetName (ttw->current_table));
            gnc_error_dialog (GTK_WINDOW(ttw->dialog), "%s", message);
        g_free (message);
        return;
    }

    tax_table_delete_request (ttw, TAX_TABLE_DELETE);
}

void
tax_table_new_entry_cb (GtkButton *button, TaxTableWindow *ttw)
{
    g_return_if_fail (ttw);
    if (!ttw->current_table)
        return;
    new_tax_table_dialog (ttw, FALSE, NULL, NULL);
}

void
tax_table_edit_entry_cb (GtkButton *button, TaxTableWindow *ttw)
{
    g_return_if_fail (ttw);
    if (!ttw->current_entry)
        return;
    new_tax_table_dialog (ttw, FALSE, ttw->current_entry, NULL);
}

void
tax_table_delete_entry_cb (GtkButton *button, TaxTableWindow *ttw)
{
    g_return_if_fail (ttw);
    if (!ttw->current_table || !ttw->current_entry)
        return;

    if (g_list_length (gncTaxTableGetEntries (ttw->current_table)) <= 1)
    {
        char *message = _("You cannot remove the last entry from the tax table. "
                          "Try deleting the tax table if you want to do that.");
        gnc_error_dialog (GTK_WINDOW(ttw->dialog)  , "%s", message);
        return;
    }

    tax_table_delete_request (ttw, TAX_TABLE_ENTRY_DELETE);
}

static void
tax_table_window_refresh_handler (GHashTable *changes, gpointer data)
{
    TaxTableWindow *ttw = data;

    g_return_if_fail (data);
    tax_table_window_refresh (ttw);
}

static void
tax_table_window_close_handler (gpointer data)
{
    TaxTableWindow *ttw = data;
    g_return_if_fail (ttw);

    gnc_save_window_size (GNC_PREFS_GROUP, GTK_WINDOW(ttw->dialog));
//FIXME gtk4    gtk_window_destroy (GTK_WINDOW(ttw->dialog));
}

void
tax_table_window_close (GtkWidget *widget, gpointer data)
{
    TaxTableWindow *ttw = data;
    gnc_close_gui_component (ttw->component_id);
}

static gboolean
tax_table_window_close_request_cb (GtkWindow *window, G_GNUC_UNUSED gpointer user_data)
{
    // This callback allows the window size to be saved on closing with the X.
    gnc_save_window_size (GNC_PREFS_GROUP, window);
    return FALSE;
}

void
tax_table_window_destroy_cb (GtkWidget *widget, gpointer data)
{
    TaxTableWindow *ttw = data;

    if (!ttw) return;

    gnc_unregister_gui_component (ttw->component_id);

    if (ttw->dialog)
    {
//FIXME gtk4        gtk_window_destroy (GTK_WINDOW(ttw->dialog));
        ttw->dialog = NULL;
    }
    g_free (ttw);
}

static gboolean
tax_table_window_key_press_cb (GtkEventControllerKey *key, guint keyval,
                               guint keycode, GdkModifierType state,
                               gpointer user_data)
{
    TaxTableWindow *ttw = user_data;

    if (keyval == GDK_KEY_Escape)
    {
        tax_table_window_close_handler (ttw);
        return TRUE;
    }
    else
        return FALSE;
}

static gboolean
find_handler (gpointer find_data, gpointer data)
{
    TaxTableWindow *ttw = data;
    QofBook *book = find_data;

    return (ttw != NULL && ttw->book == book);
}

/* Create a tax-table window */
TaxTableWindow *
gnc_ui_tax_table_window_new (GtkWindow *parent, QofBook *book)
{
    TaxTableWindow *ttw;
    GtkBuilder *builder;
    GtkTreeView *view;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkListStore *store;
    GtkTreeSelection *selection;

    if (!book) return NULL;

    /*
     * Find an existing tax-table window.  If found, bring it to
     * the front.  If we have an actual owner, then set it in
     * the window.
     */
    ttw = gnc_find_first_gui_component (DIALOG_TAX_TABLE_CM_CLASS, find_handler,
                                        book);
    if (ttw)
    {
        gtk_window_present (GTK_WINDOW(ttw->dialog));
        return ttw;
    }

    /* Didn't find one -- create a new window */
    ttw = g_new0 (TaxTableWindow, 1);
    ttw->book = book;
    ttw->session = gnc_get_current_session ();

    /* Open and read the Glade File */
    builder = gtk_builder_new ();
    gtk_builder_set_current_object (builder, G_OBJECT(ttw));
    gnc_builder_add_from_file (builder, "dialog-tax-table.glade", "tax_table_window");
    ttw->dialog = GTK_WIDGET(gtk_builder_get_object (builder, "tax_table_window"));
    ttw->names_view = GTK_WIDGET(gtk_builder_get_object (builder, "tax_tables_view"));
    ttw->entries_view = GTK_WIDGET(gtk_builder_get_object (builder, "tax_table_entries"));

    // Set the name for this dialog so it can be easily manipulated with css
    gtk_widget_set_name (GTK_WIDGET(ttw->dialog), "gnc-id-new-tax-table");
    gnc_widget_style_context_add_class (GTK_WIDGET(ttw->dialog), "gnc-class-taxes");

    g_signal_connect (ttw->dialog, "close-request",
                      G_CALLBACK(tax_table_window_close_request_cb), ttw);

    GtkEventController *event_controller = gtk_event_controller_key_new ();
    gtk_widget_add_controller (GTK_WIDGET(ttw->dialog), event_controller);
    g_signal_connect (event_controller,
                      "key-pressed",
                      G_CALLBACK(tax_table_window_key_press_cb), ttw);

    /* Create the tax tables view */
    view = GTK_TREE_VIEW(ttw->names_view);
    store = gtk_list_store_new (NUM_TAX_TABLE_COLS, G_TYPE_STRING,
                                G_TYPE_POINTER);
    gtk_tree_view_set_model (view, GTK_TREE_MODEL(store));
    g_object_unref (store);

    /* default sort order */
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store),
                                          TAX_TABLE_COL_NAME,
                                          GTK_SORT_ASCENDING);

    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("", renderer,
             "text", TAX_TABLE_COL_NAME,
             NULL);
    g_object_set (G_OBJECT(column), "reorderable", TRUE, NULL);
    gtk_tree_view_append_column (view, column);
    gtk_tree_view_column_set_sort_column_id (column, TAX_TABLE_COL_NAME);

    selection = gtk_tree_view_get_selection (view);
    g_signal_connect (selection, "changed",
                      G_CALLBACK(tax_table_selection_changed), ttw);

    /* Create the tax table entries view */
    view = GTK_TREE_VIEW(ttw->entries_view);
    store = gtk_list_store_new (NUM_TAX_ENTRY_COLS, G_TYPE_STRING,
                                G_TYPE_POINTER, G_TYPE_STRING);
    gtk_tree_view_set_model (view, GTK_TREE_MODEL(store));
    g_object_unref (store);

    /* default sort order */
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store),
                                          TAX_ENTRY_COL_NAME,
                                          GTK_SORT_ASCENDING);

    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("", renderer,
             "text", TAX_ENTRY_COL_NAME,
             NULL);
    g_object_set (G_OBJECT(column), "reorderable", TRUE, NULL);
    gtk_tree_view_append_column (view, column);
    gtk_tree_view_column_set_sort_column_id (column, TAX_ENTRY_COL_NAME);

    selection = gtk_tree_view_get_selection (view);
    g_signal_connect (selection, "changed",
                      G_CALLBACK(tax_table_entry_selection_changed), ttw);
    g_signal_connect (view, "row-activated",
                      G_CALLBACK(tax_table_entry_row_activated), ttw);

    /* Setup signals */
gnc_builder_connect_signals_full (builder, gnc_builder_connect_full_func, ttw);

    /* register with component manager */
    ttw->component_id =
        gnc_register_gui_component (DIALOG_TAX_TABLE_CM_CLASS,
                                    tax_table_window_refresh_handler,
                                    tax_table_window_close_handler,
                                    ttw);

    gnc_gui_component_set_session (ttw->component_id, ttw->session);

    tax_table_window_refresh (ttw);
    gnc_restore_window_size (GNC_PREFS_GROUP, GTK_WINDOW(ttw->dialog), parent);
//FIXME gtk4    gtk_widget_show_all (ttw->dialog);

    g_object_unref (G_OBJECT(builder));

    return ttw;
}

/* Create a new tax-table by name */
GncTaxTable *
gnc_ui_tax_table_new_from_name (GtkWindow *parent, QofBook *book, const char *name)
{
    TaxTableWindow *ttw;

    if (!book) return NULL;

    ttw = gnc_ui_tax_table_window_new (parent, book);
    if (!ttw) return NULL;

    return new_tax_table_dialog (ttw, TRUE, NULL, name);
}

typedef struct
{
    GtkWindow *window;
    QofBook *book;
    GtkEntry *name_entry;
    GtkDropDown *type_dropdown;
    GNCAmountEdit *amount_edit;
    GNCAccountSel *account_selector;
    GncTaxTableCreatedCB callback;
    gpointer user_data;
    gboolean completed;
} TaxTableCreateRequest;

static void
new_tax_table_async_update_amount_format (TaxTableCreateRequest *request)
{
    GNCPrintAmountInfo print_info;
    Account *account;

    if (gtk_drop_down_get_selected (request->type_dropdown) == 0)
    {
        account = gnc_account_sel_get_account (request->account_selector);
        if (account)
        {
            gnc_commodity *currency = xaccAccountGetCommodity (account);
            print_info = gnc_commodity_print_info (currency, FALSE);
            gnc_amount_edit_set_fraction (request->amount_edit,
                                          gnc_commodity_get_fraction (currency));
        }
        else
        {
            print_info = gnc_integral_print_info ();
            gnc_amount_edit_set_fraction (request->amount_edit, 100000);
        }
    }
    else
    {
        print_info = gnc_integral_print_info ();
        print_info.max_decimal_places = 5;
        gnc_amount_edit_set_fraction (request->amount_edit, 100000);
    }

    gnc_amount_edit_set_print_info (request->amount_edit, print_info);
}

static void
new_tax_table_async_type_changed (GObject *object, GParamSpec *pspec,
                                  gpointer user_data)
{
    (void)object;
    (void)pspec;
    new_tax_table_async_update_amount_format (user_data);
}

static void
new_tax_table_async_account_changed (GNCAccountSel *selector, gpointer user_data)
{
    (void)selector;
    new_tax_table_async_update_amount_format (user_data);
}

static void
new_tax_table_async_request_free (TaxTableCreateRequest *request)
{
    g_clear_object (&request->window);
    g_free (request);
}

static void
new_tax_table_async_complete (TaxTableCreateRequest *request,
                              GncTaxTable *table, gboolean accepted,
                              gboolean destroy_window)
{
    if (!request || request->completed)
        return;

    request->completed = TRUE;
    if (destroy_window)
        gtk_window_destroy (request->window);
    if (request->callback)
        request->callback (table, accepted, request->user_data);
    new_tax_table_async_request_free (request);
}

static gboolean
new_tax_table_async_close_request (GtkWindow *window, gpointer user_data)
{
    (void)window;
    new_tax_table_async_complete (user_data, NULL, FALSE, TRUE);
    return TRUE;
}

static void
new_tax_table_async_destroyed (GtkWidget *widget, gpointer user_data)
{
    TaxTableCreateRequest *request = user_data;

    (void)widget;
    new_tax_table_async_complete (request, NULL, FALSE, FALSE);
}

static void
new_tax_table_async_cancel_clicked (GtkButton *button, gpointer user_data)
{
    (void)button;
    new_tax_table_async_complete (user_data, NULL, FALSE, TRUE);
}

static void
new_tax_table_async_accept_clicked (GtkButton *button, gpointer user_data)
{
    TaxTableCreateRequest *request = user_data;
    const char *name;
    Account *account;
    gnc_numeric amount;
    GncTaxTable *table;
    GncTaxTableEntry *entry;
    GError *error = NULL;
    gint result;
    gint type;

    (void)button;
    if (qof_book_shutting_down (request->book))
    {
        new_tax_table_async_complete (request, NULL, FALSE, TRUE);
        return;
    }

    name = gtk_editable_get_text (GTK_EDITABLE (request->name_entry));
    if (!name || !*name)
    {
        gnc_error_dialog (request->window, "%s",
                          _("You must provide a name for this Tax Table."));
        return;
    }
    if (gncTaxTableLookupByName (request->book, name))
    {
        char *message = g_strdup_printf (
            _("You must provide a unique name for this Tax Table. "
              "Your choice \"%s\" is already in use."), name);
        gnc_error_dialog (request->window, "%s", message);
        g_free (message);
        return;
    }

    account = gnc_account_sel_get_account (request->account_selector);
    if (!account)
    {
        gnc_error_dialog (request->window, "%s", _("You must choose a Tax Account."));
        return;
    }

    result = gnc_amount_edit_expr_is_valid (request->amount_edit, &amount,
                                            TRUE, &error);
    if (result == 1)
    {
        gnc_error_dialog (request->window, "%s", error->message);
        g_clear_error (&error);
        return;
    }

    amount = gnc_amount_edit_get_amount (request->amount_edit);
    type = gtk_drop_down_get_selected (request->type_dropdown) == 0
        ? GNC_AMT_TYPE_VALUE : GNC_AMT_TYPE_PERCENT;
    if (type == GNC_AMT_TYPE_PERCENT &&
        gnc_numeric_compare (gnc_numeric_abs (amount),
                             gnc_numeric_create (100, 1)) > 0)
    {
        gnc_error_dialog (request->window, "%s",
                          _("Percentage amount must be between -100 and 100."));
        return;
    }

    gnc_suspend_gui_refresh ();
    table = gncTaxTableCreate (request->book);
    gncTaxTableBeginEdit (table);
    gncTaxTableSetName (table, name);
    entry = gncTaxTableEntryCreate ();
    gncTaxTableAddEntry (table, entry);
    gncTaxTableEntrySetAccount (entry, account);
    gncTaxTableEntrySetType (entry, type);
    gncTaxTableEntrySetAmount (entry, amount);
    gncTaxTableChanged (table);
    gncTaxTableCommitEdit (table);
    gnc_resume_gui_refresh ();

    new_tax_table_async_complete (request, table, TRUE, TRUE);
}

void
gnc_ui_tax_table_new_from_name_async (GtkWindow *parent, QofBook *book,
                                      const char *name,
                                      GncTaxTableCreatedCB callback,
                                      gpointer user_data)
{
    TaxTableCreateRequest *request;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *button_box;
    GtkWidget *cancel_button;
    GtkWidget *accept_button;
    GtkStringList *types;

    if (!book || qof_book_shutting_down (book))
    {
        if (callback)
            callback (NULL, FALSE, user_data);
        return;
    }

    request = g_new0 (TaxTableCreateRequest, 1);
    request->book = book;
    request->callback = callback;
    request->user_data = user_data;
    request->window = GTK_WINDOW (g_object_ref_sink (gtk_window_new ()));
    gtk_window_set_title (request->window, _("New Tax Table"));
    gtk_window_set_modal (request->window, TRUE);
    gtk_window_set_resizable (request->window, FALSE);
    if (parent)
    {
        gtk_window_set_transient_for (request->window, parent);
        g_signal_connect_object (parent, "destroy",
                                 G_CALLBACK (gtk_window_destroy), request->window,
                                 G_CONNECT_SWAPPED);
    }

    content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (content, 18);
    gtk_widget_set_margin_end (content, 18);
    gtk_widget_set_margin_top (content, 18);
    gtk_widget_set_margin_bottom (content, 18);
    gtk_window_set_child (request->window, content);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_box_append (GTK_BOX (content), grid);

    label = gtk_label_new_with_mnemonic (_("_Name"));
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
    request->name_entry = GTK_ENTRY (gtk_entry_new ());
    gtk_editable_set_text (GTK_EDITABLE (request->name_entry), name ? name : "");
    gtk_widget_set_hexpand (GTK_WIDGET (request->name_entry), TRUE);
    gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (request->name_entry), 1, 0, 1, 1);
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (request->name_entry));

    label = gtk_label_new_with_mnemonic (_("_Type"));
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
    types = gtk_string_list_new ((const char * const[]){ _("Value"), _("Percent"), NULL });
    request->type_dropdown = GTK_DROP_DOWN (gtk_drop_down_new (G_LIST_MODEL (types), NULL));
    gtk_drop_down_set_selected (request->type_dropdown, 1);
    gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (request->type_dropdown), 1, 1, 1, 1);
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (request->type_dropdown));
    g_object_unref (types);

    label = gtk_label_new_with_mnemonic (_("_Value"));
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
    request->amount_edit = GNC_AMOUNT_EDIT (gnc_amount_edit_new ());
    gnc_amount_edit_set_evaluate_on_enter (request->amount_edit, TRUE);
    gtk_widget_set_hexpand (GTK_WIDGET (request->amount_edit), TRUE);
    gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (request->amount_edit), 1, 2, 1, 1);
    gtk_label_set_mnemonic_widget (GTK_LABEL (label),
                                   gnc_amount_edit_gtk_entry (request->amount_edit));

    label = gtk_label_new_with_mnemonic (_("_Account"));
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 1, 1);
    request->account_selector = GNC_ACCOUNT_SEL (gnc_account_sel_new ());
    gtk_widget_set_hexpand (GTK_WIDGET (request->account_selector), TRUE);
    gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (request->account_selector), 1, 3, 1, 1);
    gtk_label_set_mnemonic_widget (GTK_LABEL (label),
                                   GTK_WIDGET (request->account_selector));

    button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign (button_box, GTK_ALIGN_END);
    cancel_button = gtk_button_new_with_mnemonic (_("_Cancel"));
    accept_button = gtk_button_new_with_mnemonic (_("_OK"));
    gtk_box_append (GTK_BOX (button_box), cancel_button);
    gtk_box_append (GTK_BOX (button_box), accept_button);
    gtk_box_append (GTK_BOX (content), button_box);
    gtk_window_set_default_widget (request->window, accept_button);

    g_signal_connect (request->type_dropdown, "notify::selected",
                      G_CALLBACK (new_tax_table_async_type_changed), request);
    g_signal_connect (request->account_selector, "account_sel_changed",
                      G_CALLBACK (new_tax_table_async_account_changed), request);
    g_signal_connect (cancel_button, "clicked",
                      G_CALLBACK (new_tax_table_async_cancel_clicked), request);
    g_signal_connect (accept_button, "clicked",
                      G_CALLBACK (new_tax_table_async_accept_clicked), request);
    g_signal_connect (request->window, "close-request",
                      G_CALLBACK (new_tax_table_async_close_request), request);
    g_signal_connect (request->window, "destroy",
                      G_CALLBACK (new_tax_table_async_destroyed), request);

    new_tax_table_async_update_amount_format (request);
    gtk_widget_grab_focus (GTK_WIDGET (request->name_entry));
    gtk_window_present (request->window);
}
