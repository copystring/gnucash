/*
 * test-assistant-hierarchy-ownership.cpp -- hierarchy cell binding ownership
 */

#include <config.h>

#include <gtk/gtk.h>

#include "Account.h"
#include "assistant-hierarchy.h"
#include "gnc-component-manager.h"
#include "gnc-engine.h"
#include "gnc-prefs-utils.h"
#include "gnc-session.h"
#include "qof.h"

static void
object_finalized (gpointer data, GObject *object)
{
    gboolean *finalized = static_cast<gboolean*>(data);

    *finalized = TRUE;
    (void)object;
}

static gboolean
drain_main_context_until (gint64 deadline)
{
    constexpr guint max_iterations = 1000;

    for (guint iteration = 0; iteration < max_iterations; iteration++)
    {
        if (!g_main_context_pending (NULL))
            return TRUE;
        if (g_get_monotonic_time () >= deadline)
            return FALSE;
        g_main_context_iteration (NULL, FALSE);
    }
    return !g_main_context_pending (NULL);
}

static gboolean
drain_main_context (void)
{
    return drain_main_context_until (g_get_monotonic_time () + G_TIME_SPAN_SECOND);
}

static GtkWidget *
find_buildable_widget (GtkWidget *widget, const gchar *buildable_id)
{
    if (GTK_IS_BUILDABLE (widget) &&
        g_strcmp0 (gtk_buildable_get_buildable_id (GTK_BUILDABLE (widget)),
                    buildable_id) == 0)
        return widget;

    for (auto child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        auto result = find_buildable_widget (child, buildable_id);

        if (result)
            return result;
    }
    return nullptr;
}

static GtkTreeExpander *
find_bound_expander (GtkWidget *widget, GtkTreeListRow *tree_row)
{
    if (GTK_IS_TREE_EXPANDER (widget) &&
        gtk_tree_expander_get_list_row (GTK_TREE_EXPANDER (widget)) == tree_row)
        return GTK_TREE_EXPANDER (widget);

    for (auto child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        auto result = find_bound_expander (child, tree_row);

        if (result)
            return result;
    }
    return nullptr;
}

static gboolean
wait_for_bound_expander (GtkColumnView *view, GtkTreeListRow *tree_row)
{
    const gint64 deadline = g_get_monotonic_time () + 2 * G_TIME_SPAN_SECOND;

    gtk_widget_queue_draw (GTK_WIDGET (view));
    while (g_get_monotonic_time () < deadline)
    {
        if (!drain_main_context_until (deadline))
            return FALSE;
        if (find_bound_expander (GTK_WIDGET (view), tree_row))
            return TRUE;
        const gint64 remaining = deadline - g_get_monotonic_time ();

        if (remaining > 0)
            g_usleep (static_cast<gulong>(MIN (remaining, 10 * 1000)));
    }
    return FALSE;
}

static void
test_hierarchy_account_row_recycled_bind_is_released (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    GtkWindow *window;
    GtkColumnView *final_view;
    GtkColumnView *category_view;
    GtkButton *next_button;
    GtkButton *select_all_button;
    GtkSelectionModel *selection = nullptr;
    GtkTreeListRow *tree_row;
    GObject *account_row;
    gboolean row_finalized = FALSE;
    gboolean categories_selected = FALSE;

    gnc_set_current_session (session);
    gnc_account_create_root (book);

    window = GTK_WINDOW (gnc_ui_hierarchy_assistant (TRUE));
    g_object_ref (window);
    final_view = GTK_COLUMN_VIEW (find_buildable_widget (GTK_WIDGET (window),
                                                          "final_account_view"));
    category_view = GTK_COLUMN_VIEW (find_buildable_widget (GTK_WIDGET (window),
                                                             "account_categories_view"));
    next_button = GTK_BUTTON (find_buildable_widget (GTK_WIDGET (window),
                                                      "hierarchy_next"));
    select_all_button = GTK_BUTTON (find_buildable_widget (GTK_WIDGET (window),
                                                            "select_all_button"));
    g_assert_nonnull (final_view);
    g_assert_nonnull (category_view);
    g_assert_nonnull (next_button);
    g_assert_nonnull (select_all_button);

    for (guint page = 0; page < 8 && !gtk_column_view_get_model (final_view); page++)
    {
        auto category_model = gtk_column_view_get_model (category_view);

        if (!categories_selected && category_model &&
            g_list_model_get_n_items (G_LIST_MODEL (category_model)) > 0)
        {
            g_signal_emit_by_name (select_all_button, "clicked");
            categories_selected = TRUE;
            g_assert_true (drain_main_context ());
        }
        g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (next_button)));
        g_signal_emit_by_name (next_button, "clicked");
        g_assert_true (drain_main_context ());
    }

    selection = gtk_column_view_get_model (final_view);
    g_assert_nonnull (selection);
    g_assert_true (categories_selected);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (selection)), >, 0);

    tree_row = GTK_TREE_LIST_ROW (g_list_model_get_item (G_LIST_MODEL (selection), 0));
    account_row = gtk_tree_list_row_get_item (tree_row);
    g_assert_nonnull (account_row);
    g_object_weak_ref (account_row, object_finalized, &row_finalized);
    g_object_unref (account_row);
    g_object_ref (selection);

    for (guint recycle = 0; recycle < 4; recycle++)
    {
        gtk_column_view_set_model (final_view, nullptr);
        g_assert_true (drain_main_context ());
        gtk_column_view_set_model (final_view, selection);
        g_assert_true (wait_for_bound_expander (final_view, tree_row));
    }

    g_object_unref (tree_row);
    g_object_unref (selection);
    gtk_window_destroy (window);
    g_object_unref (window);
    g_assert_true (drain_main_context ());
    g_assert_true (row_finalized);

    gnc_clear_current_session ();
}

int
main (int argc, char **argv)
{
    int status;

    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    g_test_init (&argc, &argv, NULL);
    gtk_init ();
    qof_log_init_filename_special ("stderr");
    qof_log_set_level ("gnc", static_cast<QofLogLevel>(G_LOG_LEVEL_DEBUG));
    gnc_engine_init_static (argc, argv);
    gnc_prefs_init ();
    gnc_component_manager_init ();

    g_test_add_func ("/gnome/assistant-hierarchy/account-row-bind-ownership",
                     test_hierarchy_account_row_recycled_bind_is_released);
    status = g_test_run ();

    gnc_component_manager_shutdown ();
    gnc_prefs_remove_registered ();
    gnc_engine_shutdown ();
    return status;
}
