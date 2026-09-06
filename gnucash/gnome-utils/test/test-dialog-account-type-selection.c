/*
 * test-dialog-account-type-selection.c -- Account type selection ownership tests
 */

#include <config.h>

#include <gtk/gtk.h>

#include "Account.h"
#include "dialog-account.h"
#include "gnc-component-manager.h"
#include "gnc-engine.h"
#include "gnc-prefs-utils.h"
#include "gnc-session.h"
#include "gnc-tree-model-account-types.h"

static void
object_finalized (gpointer data, GObject *object)
{
    gboolean *finalized = data;

    *finalized = TRUE;
    (void)object;
}

static void
drain_main_context (void)
{
    while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
}

static GtkDropDown *
find_account_type_dropdown (GtkWidget *widget)
{
    if (GTK_IS_DROP_DOWN (widget))
    {
        GListModel *model = gtk_drop_down_get_model (GTK_DROP_DOWN (widget));
        GObject *item = model ? g_list_model_get_item (model, 0) : NULL;
        gboolean is_account_type_dropdown = GNC_IS_ACCOUNT_TYPE_ITEM (item);

        g_clear_object (&item);
        if (is_account_type_dropdown)
            return GTK_DROP_DOWN (widget);
    }

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkDropDown *dropdown = find_account_type_dropdown (child);

        if (dropdown)
            return dropdown;
    }
    return NULL;
}

static GtkWindow *
find_account_window (void)
{
    GListModel *windows = gtk_window_get_toplevels ();

    for (guint index = 0; index < g_list_model_get_n_items (windows); index++)
    {
        GtkWindow *window = g_list_model_get_item (windows, index);

        if (find_account_type_dropdown (GTK_WIDGET (window)))
            return window;
        g_object_unref (window);
    }
    return NULL;
}

static void
test_account_type_selection_owns_model_items (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    GList *valid_types = NULL;
    GtkWindow *window;
    GtkDropDown *dropdown;
    GListStore *model;
    gboolean dialog_finalized = FALSE;
    gboolean item_finalized[2] = { FALSE, FALSE };

    gnc_set_current_session (session);
    gnc_account_create_root (book);
    valid_types = g_list_append (valid_types, GINT_TO_POINTER (ACCT_TYPE_BANK));
    valid_types = g_list_append (valid_types, GINT_TO_POINTER (ACCT_TYPE_CASH));
    gnc_ui_new_account_with_types_and_commodity (NULL, book, valid_types,
                                                  NULL);
    g_list_free (valid_types);

    window = find_account_window ();
    g_assert_nonnull (window);
    dropdown = find_account_type_dropdown (GTK_WIDGET (window));
    g_assert_nonnull (dropdown);
    model = G_LIST_STORE (gtk_drop_down_get_model (dropdown));
    g_assert_nonnull (model);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==,
                      G_N_ELEMENTS (item_finalized));

    for (guint index = 0; index < G_N_ELEMENTS (item_finalized); index++)
    {
        GObject *item = g_list_model_get_item (G_LIST_MODEL (model), index);

        g_assert_true (GNC_IS_ACCOUNT_TYPE_ITEM (item));
        g_object_weak_ref (item, object_finalized, &item_finalized[index]);
        g_object_unref (item);
    }

    for (guint round = 0; round < 4; round++)
        for (guint index = 0; index < G_N_ELEMENTS (item_finalized); index++)
        {
            GObject *item;

            gtk_drop_down_set_selected (dropdown, index);
            item = gtk_drop_down_get_selected_item (dropdown);
            g_assert_true (GNC_IS_ACCOUNT_TYPE_ITEM (item));
            g_assert_false (item_finalized[index]);
        }

    gtk_drop_down_set_selected (dropdown, GTK_INVALID_LIST_POSITION);
    g_assert_null (gtk_drop_down_get_selected_item (dropdown));
    g_list_store_remove_all (model);

    g_object_weak_ref (G_OBJECT (window), object_finalized, &dialog_finalized);
    gtk_window_destroy (window);
    g_object_unref (window);
    drain_main_context ();
    g_assert_true (dialog_finalized);
    g_assert_true (item_finalized[0]);
    g_assert_true (item_finalized[1]);

    gnc_clear_current_session ();
}

int
main (int argc, char **argv)
{
    int status;

    g_setenv ("GNC_UNINSTALLED", "1", TRUE);
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    g_test_init (&argc, &argv, NULL);
    gtk_init ();
    gnc_engine_init_static (argc, argv);
    gnc_prefs_init ();
    gnc_component_manager_init ();

    g_test_add_func ("/gnome-utils/dialog-account/type-selection-ownership",
                     test_account_type_selection_owns_model_items);
    status = g_test_run ();

    gnc_component_manager_shutdown ();
    gnc_prefs_remove_registered ();
    gnc_engine_shutdown ();
    return status;
}
