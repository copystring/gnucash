/********************************************************************\
 * dialog-commodities.c -- commodities dialog                       *
 * Copyright (C) 2001 Gnumatic, Inc.                                *
 * Author: Dave Peticolas <dave@krondo.com>                         *
 * Copyright (C) 2003,2005 David Hampton                            *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "dialog-commodity.h"
#include "gnc-commodity.hpp"
#include "dialog-utils.h"
#include "gnc-commodity.h"
#include "gnc-component-manager.h"
#include "qof.h"
#include "gnc-tree-view-commodity.h"
#include "gnc-prefs.h"
#include "gnc-ui.h"
#include "gnc-ui-util.h"
#include "gnc-gnome-utils.h"
#include "gnc-session.h"
#include "gnc-warnings.h"
#include "Account.hpp"

#include <vector>
#include <string>

#define DIALOG_COMMODITIES_CM_CLASS "dialog-commodities"
#define STATE_SECTION "dialogs/edit_commodities"
#define GNC_PREFS_GROUP   "dialogs.commodities"
#define GNC_PREF_INCL_ISO "include-iso"

/* This static indicates the debugging module that this .o belongs to.  */
/* static short module = MOD_GUI; */

typedef struct
{
    GtkWidget * window;
    QofSession *session;
    QofBook *book;

    GncTreeViewCommodity * commodity_tree;
    GtkWidget * edit_button;
    GtkWidget * remove_button;
    gboolean    show_currencies;
    GtkWidget * rename_namespace_button;

    gboolean is_new;
} CommoditiesDialog;

namespace
{
constexpr const char *RENAME_NAMESPACE_REQUEST_DATA = "gnc-rename-namespace-request";

struct RenameNamespaceRequest
{
    GtkWindow *window;
    GtkEntry *entry;
    GtkLabel *label;
    GncGUID book_guid;
    gchar *old_name;
};

static void
rename_namespace_request_free (gpointer user_data)
{
    auto request = static_cast<RenameNamespaceRequest *> (user_data);

    g_free (request->old_name);
    g_free (request);
}

static gboolean
rename_namespace_request_matches_current_book (const RenameNamespaceRequest *request)
{
    auto book = gnc_get_current_book ();

    return book && guid_equal (qof_instance_get_guid (QOF_INSTANCE (book)),
                               &request->book_guid);
}

static void
rename_namespace_response_cb (GtkWindow *window, gint response, gpointer user_data)
{
    auto request = static_cast<RenameNamespaceRequest *> (user_data);

    if (response != GTK_RESPONSE_OK)
    {
        gtk_window_destroy (window);
        return;
    }

    if (!rename_namespace_request_matches_current_book (request))
    {
        gtk_window_destroy (window);
        return;
    }

    const auto new_name = gtk_editable_get_text (GTK_EDITABLE (request->entry));
    if (!new_name || !*new_name)
    {
        gtk_label_set_text (request->label, _("No new name"));
        return;
    }

    const auto commodity_table = gnc_get_current_commodities ();
    if (!gnc_commodity_table_rename_namespace (commodity_table, request->old_name,
                                                new_name))
    {
        gtk_label_set_text (request->label,
                            _("Rename failed, possibly new name exists"));
        return;
    }

    qof_book_mark_session_dirty (gnc_get_current_book ());
    gtk_window_destroy (window);
}

static void
rename_namespace_cancel_clicked_cb (GtkButton *, gpointer user_data)
{
    auto request = static_cast<RenameNamespaceRequest *> (user_data);
    rename_namespace_response_cb (request->window, GTK_RESPONSE_CANCEL, request);
}

static void
rename_namespace_confirm_clicked_cb (GtkButton *, gpointer user_data)
{
    auto request = static_cast<RenameNamespaceRequest *> (user_data);
    rename_namespace_response_cb (request->window, GTK_RESPONSE_OK, request);
}


constexpr const char *COMMODITIES_DIALOG_DATA = "gnc-commodities-dialog-data";
constexpr const char *DELETE_COMMODITY_REQUEST_DATA = "gnc-delete-commodity-request";

struct DeleteCommodityRequest
{
    GtkWindow *dialog;
    GtkCheckButton *permanent;
    GtkCheckButton *temporary;
    GWeakRef parent;
    GncGUID book_guid;
    GncGUID commodity_guid;
    const gchar *warning;
    gboolean had_prices;
};

static void
commodity_delete_request_free (gpointer user_data)
{
    auto request = static_cast<DeleteCommodityRequest *> (user_data);

    g_weak_ref_clear (&request->parent);
    g_free (request);
}

static gboolean
commodity_delete_request_matches_current_book (const DeleteCommodityRequest *request)
{
    auto book = gnc_get_current_book ();

    return book && guid_equal (qof_instance_get_guid (QOF_INSTANCE (book)),
                               &request->book_guid);
}

static gboolean
commodity_is_used_by_account (QofBook *book, gnc_commodity *commodity)
{
    gboolean used = FALSE;

    gnc_account_foreach_descendant (gnc_book_get_root_account (book),
                                    [commodity, &used] (auto account)
                                    {
                                        if (commodity == xaccAccountGetCommodity (account))
                                            used = TRUE;
                                    });
    return used;
}

static gboolean
commodity_delete_request_complete (DeleteCommodityRequest *request)
{
    auto parent = GTK_WINDOW (g_weak_ref_get (&request->parent));
    CommoditiesDialog *commodities_dialog;

    if (!parent)
        return FALSE;

    commodities_dialog = static_cast<CommoditiesDialog *> (
        g_object_get_data (G_OBJECT (parent), COMMODITIES_DIALOG_DATA));
    if (!commodities_dialog ||
        !commodity_delete_request_matches_current_book (request))
    {
        g_object_unref (parent);
        return FALSE;
    }

    auto book = gnc_get_current_book ();
    auto commodity = gnc_commodity_find_commodity_by_guid (&request->commodity_guid, book);
    if (!commodity || qof_instance_get_destroying (QOF_INSTANCE (commodity)) ||
        gnc_commodity_is_iso (commodity))
    {
        g_object_unref (parent);
        return FALSE;
    }

    if (commodity_is_used_by_account (book, commodity))
    {
        gnc_warning_dialog (parent, "%s",
                            _("This commodity is now used by one or more accounts and may not be deleted."));
        g_object_unref (parent);
        return FALSE;
    }

    auto price_db = gnc_pricedb_get_db (book);
    auto prices = gnc_pricedb_get_prices (price_db, commodity, nullptr);
    if (!request->had_prices && prices)
    {
        gnc_price_list_destroy (prices);
        gnc_warning_dialog (parent, "%s",
                            _("This commodity acquired price quotes before deletion. Review it and try again."));
        g_object_unref (parent);
        return FALSE;
    }

    auto commodity_table = gnc_commodity_table_get_table (book);
    for (auto node = prices; node; node = node->next)
        gnc_pricedb_remove_price (price_db, GNC_PRICE (node->data));
    gnc_price_list_destroy (prices);

    gnc_commodity_table_remove (commodity_table, commodity);
    gnc_commodity_destroy (commodity);

    gnc_gui_refresh_all ();
    g_object_unref (parent);
    return TRUE;
}

static gboolean
commodity_delete_request_complete_idle (gpointer user_data)
{
    commodity_delete_request_complete (static_cast<DeleteCommodityRequest *> (user_data));
    return G_SOURCE_REMOVE;
}

static void
commodity_delete_response_cb (GtkWindow *dialog, gint response, gpointer user_data)
{
    auto request = static_cast<DeleteCommodityRequest *> (user_data);

    if (response == GTK_RESPONSE_OK)
    {
        const auto succeeded = commodity_delete_request_complete (request);
        if (succeeded && gtk_check_button_get_active (request->permanent))
            gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_PERM, request->warning,
                               GTK_RESPONSE_OK);
        else if (succeeded && gtk_check_button_get_active (request->temporary))
            gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_TEMP, request->warning,
                               GTK_RESPONSE_OK);
    }
    gtk_window_destroy (dialog);
}

static void
commodity_delete_cancel_clicked_cb (GtkButton *, gpointer user_data)
{
    auto request = static_cast<DeleteCommodityRequest *> (user_data);
    commodity_delete_response_cb (request->dialog, GTK_RESPONSE_CANCEL, request);
}

static void
commodity_delete_confirm_clicked_cb (GtkButton *, gpointer user_data)
{
    auto request = static_cast<DeleteCommodityRequest *> (user_data);
    commodity_delete_response_cb (request->dialog, GTK_RESPONSE_OK, request);
}

static void
commodity_delete_permanent_toggled_cb (GtkCheckButton *permanent, gpointer user_data)
{
    auto request = static_cast<DeleteCommodityRequest *> (user_data);
    const auto is_permanent = gtk_check_button_get_active (permanent);

    gtk_widget_set_sensitive (GTK_WIDGET (request->temporary), !is_permanent);
    if (is_permanent)
        gtk_check_button_set_active (request->temporary, FALSE);
}

static void
commodity_delete_confirm_async (CommoditiesDialog *cd, gnc_commodity *commodity,
                                gboolean had_prices, const gchar *message,
                                const gchar *warning)
{
    auto request = g_new0 (DeleteCommodityRequest, 1);
    request->book_guid = *qof_instance_get_guid (QOF_INSTANCE (cd->book));
    request->commodity_guid = *qof_instance_get_guid (QOF_INSTANCE (commodity));
    request->warning = warning;
    request->had_prices = had_prices;
    g_weak_ref_init (&request->parent, G_OBJECT (cd->window));

    auto remembered = gnc_prefs_get_int (GNC_PREFS_GROUP_WARNINGS_PERM, warning);
    if (!remembered)
        remembered = gnc_prefs_get_int (GNC_PREFS_GROUP_WARNINGS_TEMP, warning);
    if (remembered)
    {
        if (remembered == GTK_RESPONSE_OK)
            g_idle_add_full (G_PRIORITY_DEFAULT, commodity_delete_request_complete_idle,
                             request, commodity_delete_request_free);
        else
            commodity_delete_request_free (request);
        return;
    }

    auto dialog = GTK_WINDOW (gtk_window_new ());
    auto content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    auto detail = GTK_LABEL (gtk_label_new (message));
    auto permanent = GTK_CHECK_BUTTON (
        gtk_check_button_new_with_mnemonic (_("Remember and don't _ask me again.")));
    auto temporary = GTK_CHECK_BUTTON (
        gtk_check_button_new_with_mnemonic (_("Remember and don't ask me again this _session.")));
    auto actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    auto cancel = GTK_BUTTON (gtk_button_new_with_mnemonic (_("_Cancel")));
    auto confirm = GTK_BUTTON (gtk_button_new_with_mnemonic (_("_Delete")));

    request->dialog = dialog;
    request->permanent = permanent;
    request->temporary = temporary;
    gtk_window_set_title (dialog, _("Delete commodity?"));
    gtk_window_set_modal (dialog, TRUE);
    gtk_window_set_resizable (dialog, FALSE);
    gtk_window_set_transient_for (dialog, GTK_WINDOW (cd->window));
    gtk_widget_set_margin_top (content, 12);
    gtk_widget_set_margin_bottom (content, 12);
    gtk_widget_set_margin_start (content, 12);
    gtk_widget_set_margin_end (content, 12);
    gtk_label_set_wrap (detail, TRUE);
    gtk_widget_set_halign (actions, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (actions), GTK_WIDGET (cancel));
    gtk_box_append (GTK_BOX (actions), GTK_WIDGET (confirm));
    gtk_box_append (GTK_BOX (content), GTK_WIDGET (detail));
    gtk_box_append (GTK_BOX (content), GTK_WIDGET (permanent));
    gtk_box_append (GTK_BOX (content), GTK_WIDGET (temporary));
    gtk_box_append (GTK_BOX (content), actions);
    gtk_window_set_child (dialog, content);
    gtk_window_set_default_widget (dialog, GTK_WIDGET (cancel));

    g_object_set_data_full (G_OBJECT (dialog), DELETE_COMMODITY_REQUEST_DATA,
                            request, commodity_delete_request_free);
    g_signal_connect (cancel, "clicked", G_CALLBACK (commodity_delete_cancel_clicked_cb),
                      request);
    g_signal_connect (confirm, "clicked", G_CALLBACK (commodity_delete_confirm_clicked_cb),
                      request);
    g_signal_connect (permanent, "toggled",
                      G_CALLBACK (commodity_delete_permanent_toggled_cb), request);
    gtk_window_present (dialog);
}

struct CommodityDialogRequest
{
    GWeakRef parent;
    GncGUID book_guid;
    gboolean refresh;
};

static void
commodity_dialog_request_free (gpointer user_data)
{
    auto request = static_cast<CommodityDialogRequest *> (user_data);
    g_weak_ref_clear (&request->parent);
    g_free (request);
}

static void
commodity_dialog_operation_finished (gnc_commodity *commodity, gpointer user_data)
{
    auto request = static_cast<CommodityDialogRequest *> (user_data);
    auto parent = GTK_WINDOW (g_weak_ref_get (&request->parent));

    if (!parent)
    {
        commodity_dialog_request_free (request);
        return;
    }

    auto dialog = static_cast<CommoditiesDialog *> (
        g_object_get_data (G_OBJECT (parent), COMMODITIES_DIALOG_DATA));
    auto book = gnc_get_current_book ();
    if (dialog && commodity && book &&
        guid_equal (qof_instance_get_guid (QOF_INSTANCE (book)), &request->book_guid) &&
        !qof_instance_get_destroying (QOF_INSTANCE (commodity)))
    {
        auto commodity_guid = *qof_instance_get_guid (QOF_INSTANCE (commodity));
        auto current_commodity = gnc_commodity_find_commodity_by_guid (&commodity_guid, book);
        if (current_commodity)
        {
            gnc_tree_view_commodity_select_commodity (dialog->commodity_tree,
                                                       current_commodity);
            if (request->refresh)
                gnc_gui_refresh_all ();
        }
    }

    g_object_unref (parent);
    commodity_dialog_request_free (request);
}

static CommodityDialogRequest *
commodity_dialog_request_new (CommoditiesDialog *dialog, gboolean refresh)
{
    auto book = gnc_get_current_book ();
    if (!book)
        return nullptr;

    auto request = g_new0 (CommodityDialogRequest, 1);
    g_weak_ref_init (&request->parent, G_OBJECT (dialog->window));
    request->book_guid = *qof_instance_get_guid (QOF_INSTANCE (book));
    request->refresh = refresh;
    return request;
}

static void
commodity_dialog_edit_async (CommoditiesDialog *dialog, gnc_commodity *commodity)
{
    auto request = commodity_dialog_request_new (dialog, TRUE);
    if (!request)
        return;

    gnc_ui_edit_commodity_async (commodity, dialog->window, nullptr,
                                 commodity_dialog_operation_finished, request);
}

static void
commodity_dialog_add_async (CommoditiesDialog *dialog, const char *name_space)
{
    auto request = commodity_dialog_request_new (dialog, FALSE);
    if (!request)
        return;

    gnc_ui_new_commodity_async (name_space, dialog->window, nullptr,
                                commodity_dialog_operation_finished, request);
}}

void gnc_commodities_window_destroy_cb (GtkWidget *object, CommoditiesDialog *cd);

extern "C" {
void gnc_commodities_dialog_add_clicked (GtkWidget *widget, gpointer data);
void gnc_commodities_dialog_edit_clicked (GtkWidget *widget, gpointer data);
void gnc_commodities_dialog_remove_clicked (GtkWidget *widget, gpointer data);
void gnc_commodities_dialog_close_clicked (GtkWidget *widget, gpointer data);

void gnc_commodities_dialog_rename_namespace_clicked (GtkWidget *widget, gpointer data);

void gnc_commodities_show_currencies_toggled (GtkToggleButton *toggle, CommoditiesDialog *cd);
}

static gboolean gnc_commodities_window_key_pressed_cb (GtkEventControllerKey *key,
                                                        guint keyval, guint keycode,
                                                        GdkModifierType state,
                                                        gpointer data);


void
gnc_commodities_window_destroy_cb (GtkWidget *object,   CommoditiesDialog *cd)
{
    g_object_steal_data (G_OBJECT (object), COMMODITIES_DIALOG_DATA);
    gnc_unregister_gui_component_by_data (DIALOG_COMMODITIES_CM_CLASS, cd);

    if (cd->window)
    {
        gtk_window_destroy (GTK_WINDOW(cd->window));
        cd->window = NULL;
    }
    g_free (cd);
}

static gboolean
gnc_commodities_window_close_request_cb (GtkWindow *window, gpointer data)
{
    // This callback allows the window size to be saved on closing with the X.
    gnc_save_window_size (GNC_PREFS_GROUP, window);
    return FALSE;
}

void
gnc_commodities_dialog_edit_clicked (GtkWidget *widget, gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);
    gnc_commodity *commodity;

    commodity = gnc_tree_view_commodity_get_selected_commodity (cd->commodity_tree);
    if (commodity == NULL)
        return;

    commodity_dialog_edit_async (cd, commodity);

}

static void
row_activated_cb (GtkTreeView *view, GtkTreePath *path,
                  GtkTreeViewColumn *column, CommoditiesDialog *cd)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    g_return_if_fail(view);

    model = gtk_tree_view_get_model(view);
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        if (gtk_tree_model_iter_has_child(model, &iter))
        {
            /* There are children, so it's not a commodity.
             * Just expand or collapse the row. */
            if (gtk_tree_view_row_expanded(view, path))
                gtk_tree_view_collapse_row(view, path);
            else
                gtk_tree_view_expand_row(view, path, FALSE);
        }
        else
            /* It's a commodity, so click the Edit button. */
            gnc_commodities_dialog_edit_clicked (NULL, cd);
    }
}

void
gnc_commodities_dialog_remove_clicked (GtkWidget *widget, gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);
    auto commodity = gnc_tree_view_commodity_get_selected_commodity (cd->commodity_tree);
    if (!commodity)
        return;

    std::vector<Account*> commodity_accounts;
    gnc_account_foreach_descendant (gnc_book_get_root_account (cd->book),
                                    [commodity, &commodity_accounts] (auto account)
                                    {
                                        if (commodity == xaccAccountGetCommodity (account))
                                            commodity_accounts.push_back (account);
                                    });

    /* FIXME check for transaction references */
    if (!commodity_accounts.empty ())
    {
        std::string message {_("This commodity is currently used by the following accounts. You may "
                               "not delete it.\n")};
        for (const auto account : commodity_accounts)
        {
            auto full_name = gnc_account_get_full_name (account);
            message.append ("\n* ").append (full_name);
            g_free (full_name);
        }
        gnc_warning_dialog (GTK_WINDOW (cd->window), "%s", message.c_str ());
        return;
    }

    auto price_db = gnc_pricedb_get_db (cd->book);
    auto prices = gnc_pricedb_get_prices (price_db, commodity, nullptr);
    const auto had_prices = prices != nullptr;
    gnc_price_list_destroy (prices);

    commodity_delete_confirm_async (
        cd, commodity, had_prices,
        had_prices
            ? _("This commodity has price quotes. Are you sure you want to delete the selected "
                "commodity and its price quotes?")
            : _("Are you sure you want to delete the selected commodity?"),
        had_prices ? GNC_PREF_WARN_PRICE_COMM_DEL_QUOTES : GNC_PREF_WARN_PRICE_COMM_DEL);
}
void
gnc_commodities_dialog_add_clicked (GtkWidget *widget, gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);
    auto commodity = gnc_tree_view_commodity_get_selected_commodity (cd->commodity_tree);
    auto name_space = commodity ? gnc_commodity_get_namespace (commodity) : nullptr;

    commodity_dialog_add_async (cd, name_space);
}

void
gnc_commodities_dialog_close_clicked (GtkWidget *widget, gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);

    gnc_close_gui_component_by_data (DIALOG_COMMODITIES_CM_CLASS, cd);
}

void
gnc_commodities_dialog_rename_namespace_clicked (GtkWidget *widget, gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);
    auto ns = gnc_tree_view_commodity_get_selected_namespace (cd->commodity_tree);

    if (!ns)
        return;

    const auto ns_name = gnc_commodity_namespace_get_name (ns);

    auto dialog = GTK_WINDOW (gtk_window_new ());
    auto content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    auto form = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    auto label = GTK_LABEL (gtk_label_new_with_mnemonic (_("New _name:")));
    auto entry = GTK_ENTRY (gtk_entry_new ());
    auto feedback = GTK_LABEL (gtk_label_new (nullptr));
    auto actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    auto cancel = GTK_BUTTON (gtk_button_new_with_mnemonic (_("_Cancel")));
    auto confirm = GTK_BUTTON (gtk_button_new_with_mnemonic (_("_Rename")));
    auto request = g_new0 (RenameNamespaceRequest, 1);

    request->window = dialog;
    request->entry = entry;
    request->label = feedback;
    request->book_guid = *qof_instance_get_guid (QOF_INSTANCE (cd->book));
    request->old_name = g_strdup (ns_name);

    gtk_window_set_title (dialog, _("Rename Namespace"));
    gtk_window_set_modal (dialog, TRUE);
    gtk_window_set_resizable (dialog, FALSE);
    gtk_window_set_transient_for (dialog, GTK_WINDOW (cd->window));
    gtk_widget_set_name (GTK_WIDGET (dialog), "gnc-id-rename-namespace");
    gnc_widget_style_context_add_class (GTK_WIDGET (dialog), "gnc-class-securities");

    gtk_widget_set_margin_top (content, 12);
    gtk_widget_set_margin_bottom (content, 12);
    gtk_widget_set_margin_start (content, 12);
    gtk_widget_set_margin_end (content, 12);
    gtk_widget_set_hexpand (GTK_WIDGET (entry), TRUE);
    gtk_label_set_mnemonic_widget (label, GTK_WIDGET (entry));
    gtk_label_set_wrap (feedback, TRUE);
    gtk_widget_set_halign (actions, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (form), GTK_WIDGET (label));
    gtk_box_append (GTK_BOX (form), GTK_WIDGET (entry));
    gtk_box_append (GTK_BOX (actions), GTK_WIDGET (cancel));
    gtk_box_append (GTK_BOX (actions), GTK_WIDGET (confirm));
    gtk_box_append (GTK_BOX (content), form);
    gtk_box_append (GTK_BOX (content), GTK_WIDGET (feedback));
    gtk_box_append (GTK_BOX (content), actions);
    gtk_window_set_child (dialog, content);

    gtk_editable_set_text (GTK_EDITABLE (entry), ns_name);
    gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
    gtk_entry_set_activates_default (entry, TRUE);
    gtk_window_set_default_widget (dialog, GTK_WIDGET (confirm));

    g_object_set_data_full (G_OBJECT (dialog), RENAME_NAMESPACE_REQUEST_DATA,
                            request, rename_namespace_request_free);
    g_signal_connect (cancel, "clicked", G_CALLBACK (rename_namespace_cancel_clicked_cb),
                      request);
    g_signal_connect (confirm, "clicked", G_CALLBACK (rename_namespace_confirm_clicked_cb),
                      request);
    gtk_window_present (dialog);
}

static void
gnc_commodities_dialog_selection_changed (GtkTreeSelection *selection,
        CommoditiesDialog *cd)
{
    gboolean remove_ok;
    gnc_commodity *commodity;

    commodity = gnc_tree_view_commodity_get_selected_commodity (cd->commodity_tree);
    remove_ok = commodity && !gnc_commodity_is_iso(commodity);
    gtk_widget_set_sensitive (cd->edit_button, commodity != NULL);
    gtk_widget_set_sensitive (cd->remove_button, remove_ok);

    gtk_widget_set_sensitive (cd->rename_namespace_button, !commodity);

    if (!commodity)
    {
        gnc_commodity_namespace *ns = gnc_tree_view_commodity_get_selected_namespace (cd->commodity_tree);
        const char *ns_name = gnc_commodity_namespace_get_name (ns);

        gtk_widget_set_sensitive (cd->rename_namespace_button,
                                  !(g_strcmp0 (ns_name, GNC_COMMODITY_NS_LEGACY) == 0 ||
                                    g_strcmp0 (ns_name, GNC_COMMODITY_NS_CURRENCY) == 0));
    }
}

void
gnc_commodities_show_currencies_toggled (GtkToggleButton *toggle,
        CommoditiesDialog *cd)
{
    cd->show_currencies = gtk_toggle_button_get_active (toggle);
    gnc_tree_view_commodity_refilter (cd->commodity_tree);
}

static gboolean
gnc_commodities_dialog_filter_ns_func (gnc_commodity_namespace *name_space,
                                       gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);
    const gchar *name;
    GList *list;

    /* Never show the template list */
    name = gnc_commodity_namespace_get_name (name_space);
    if (g_strcmp0 (name, GNC_COMMODITY_NS_TEMPLATE) == 0)
        return FALSE;

    /* Check whether or not to show commodities */
    if (!cd->show_currencies && gnc_commodity_namespace_is_iso(name))
        return FALSE;

    /* Show any other namespace that has commodities */
    list = gnc_commodity_namespace_get_commodity_list(name_space);
    gboolean rv = (list != NULL);
    g_list_free (list);
    return rv;
}

static gboolean
gnc_commodities_dialog_filter_cm_func (gnc_commodity *commodity,
                                       gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);

    if (cd->show_currencies)
        return TRUE;
    return !gnc_commodity_is_iso(commodity);
}

static void
gnc_commodities_dialog_create (GtkWidget * parent, CommoditiesDialog *cd)
{
    GtkWidget *button;
    GtkWidget *scrolled_window;
    GtkBuilder *builder;
    GtkTreeView *view;
    GtkTreeSelection *selection;

    builder = gtk_builder_new();
    gnc_builder_add_from_file (builder, "dialog-commodities.ui", "securities_window");

    cd->window = GTK_WIDGET(gtk_builder_get_object (builder, "securities_window"));
    g_object_set_data (G_OBJECT (cd->window), COMMODITIES_DIALOG_DATA, cd);
    cd->session = gnc_get_current_session();
    cd->book = qof_session_get_book(cd->session);
    cd->show_currencies = gnc_prefs_get_bool(GNC_PREFS_GROUP, GNC_PREF_INCL_ISO);

    // Set the name for this dialog so it can be easily manipulated with css
    gtk_widget_set_name (GTK_WIDGET(cd->window), "gnc-id-commodity");
    gnc_widget_style_context_add_class (GTK_WIDGET(cd->window), "gnc-class-securities");

    /* buttons */
    cd->remove_button = GTK_WIDGET(gtk_builder_get_object (builder, "remove_button"));
    cd->edit_button = GTK_WIDGET(gtk_builder_get_object (builder, "edit_button"));

    cd->rename_namespace_button = GTK_WIDGET(gtk_builder_get_object (builder, "rename_namespace_button"));
    gtk_widget_set_sensitive (cd->rename_namespace_button, FALSE);

    /* commodity tree */
    scrolled_window = GTK_WIDGET(gtk_builder_get_object (builder, "commodity_list_window"));
    view = gnc_tree_view_commodity_new(cd->book,
                                       "state-section", STATE_SECTION,
                                       "show-column-menu", TRUE,
                                       NULL);
    cd->commodity_tree = GNC_TREE_VIEW_COMMODITY(view);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_WIDGET(view));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(cd->commodity_tree), TRUE);
    gnc_tree_view_commodity_set_filter (cd->commodity_tree,
                                        gnc_commodities_dialog_filter_ns_func,
                                        gnc_commodities_dialog_filter_cm_func,
                                        cd, NULL);
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
    g_signal_connect (G_OBJECT (selection), "changed",
                      G_CALLBACK (gnc_commodities_dialog_selection_changed), cd);

    g_signal_connect (G_OBJECT (cd->commodity_tree), "row-activated",
                      G_CALLBACK (row_activated_cb), cd);

    /* Show currency button */
    button = GTK_WIDGET(gtk_builder_get_object (builder, "show_currencies_button"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(button), cd->show_currencies);

    /* default to 'close' button */
    button = GTK_WIDGET(gtk_builder_get_object (builder, "close_button"));
    gtk_window_set_default_widget (GTK_WINDOW (cd->window), button);
    gtk_widget_grab_focus (button);

    g_signal_connect (cd->window, "destroy",
                      G_CALLBACK(gnc_commodities_window_destroy_cb), cd);

    g_signal_connect (cd->window, "close-request",
                      G_CALLBACK(gnc_commodities_window_close_request_cb), cd);

    GtkEventController *key_controller = gtk_event_controller_key_new ();
    gtk_widget_add_controller (cd->window, key_controller);
    g_signal_connect (key_controller, "key-pressed",
                      G_CALLBACK (gnc_commodities_window_key_pressed_cb), cd);

    gnc_builder_connect_signals_full (builder, gnc_builder_connect_full_func, cd);
    g_object_unref (G_OBJECT(builder));

    gnc_restore_window_size (GNC_PREFS_GROUP, GTK_WINDOW(cd->window), GTK_WINDOW(parent));
}

static void
close_handler (gpointer user_data)
{
    auto cd = static_cast<CommoditiesDialog*>(user_data);

    gnc_save_window_size (GNC_PREFS_GROUP, GTK_WINDOW(cd->window));

    gnc_prefs_set_bool (GNC_PREFS_GROUP, GNC_PREF_INCL_ISO, cd->show_currencies);

    gtk_window_destroy (GTK_WINDOW(cd->window));
}

static void
refresh_handler (GHashTable *changes, gpointer user_data)
{
    auto cd = static_cast<CommoditiesDialog*>(user_data);

    g_return_if_fail(cd != NULL);

    gnc_tree_view_commodity_refilter (cd->commodity_tree);
}

static gboolean
show_handler (const char *klass, gint component_id,
              gpointer user_data, gpointer iter_data)
{
    auto cd = static_cast<CommoditiesDialog*>(user_data);

    if (!cd)
        return(FALSE);
    gtk_window_present (GTK_WINDOW(cd->window));
    return(TRUE);
}

static gboolean
gnc_commodities_window_key_pressed_cb (GtkEventControllerKey *key,
                                        guint keyval, guint keycode,
                                        GdkModifierType state, gpointer data)
{
    auto cd = static_cast<CommoditiesDialog*>(data);

    if (keyval == GDK_KEY_Escape)
    {
        close_handler (cd);
        return TRUE;
    }
    else
        return FALSE;
}

/********************************************************************\
 * gnc_commodities_dialog                                           *
 *   opens up a window to edit price information                    *
 *                                                                  *
 * Args:   parent  - the parent of the window to be created         *
 * Return: nothing                                                  *
\********************************************************************/
void
gnc_commodities_dialog (GtkWidget * parent)
{
    gint component_id;

    if (gnc_forall_gui_components (DIALOG_COMMODITIES_CM_CLASS,
                                   show_handler, NULL))
        return;

    auto cd = static_cast<CommoditiesDialog*>(g_new0 (CommoditiesDialog, 1));

    gnc_commodities_dialog_create (parent, cd);

    component_id = gnc_register_gui_component (DIALOG_COMMODITIES_CM_CLASS,
                   refresh_handler, close_handler,
                   cd);
    gnc_gui_component_set_session (component_id, cd->session);

    gtk_widget_grab_focus (GTK_WIDGET(cd->commodity_tree));

    gtk_window_present (GTK_WINDOW (cd->window));
}
