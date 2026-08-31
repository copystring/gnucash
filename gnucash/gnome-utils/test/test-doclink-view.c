#include <config.h>

#include <gtk/gtk.h>

#include "dialog-doclink-view.h"

static GtkImage *
find_image (GtkWidget *widget)
{
    GtkWidget *child;

    if (GTK_IS_IMAGE (widget))
        return GTK_IMAGE (widget);
    for (child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkImage *image = find_image (child);

        if (image)
            return image;
    }
    return NULL;
}

static void
drain_main_context (void)
{
    for (guint turn = 0; turn < 100; turn++)
        g_main_context_iteration (NULL, FALSE);
}

static DoclinkViewItem *
doclink_item_new (const gchar *icon_name)
{
    DoclinkViewItem *item = g_object_new (DOCLINKVIEW_TYPE_ITEM, NULL);

    item->uri_relative_pix = g_strdup (icon_name);
    return item;
}

static void
test_relative_icon_is_cleared_on_rebind (void)
{
    GListStore *model = g_list_store_new (DOCLINKVIEW_TYPE_ITEM);
    DoclinkViewItem *with_icon = doclink_item_new ("emblem-ok-symbolic");
    DoclinkViewItem *without_icon = doclink_item_new (NULL);
    GtkWindow *window = GTK_WINDOW (gtk_window_new ());
    GtkWidget *scroller = gtk_scrolled_window_new ();
    GtkWidget *view;
    GtkImage *image;

    g_object_ref_sink (window);
    g_list_store_append (model, with_icon);
    view = gnc_doclink_create_column_view (scroller, G_LIST_MODEL (model));
    gtk_window_set_default_size (window, 480, 120);
    gtk_window_set_child (window, scroller);
    gtk_window_present (window);
    drain_main_context ();

    image = find_image (view);
    g_assert_nonnull (image);
    g_assert_cmpstr (gtk_image_get_icon_name (image), ==, "emblem-ok-symbolic");

    g_list_store_remove_all (model);
    g_list_store_append (model, without_icon);
    drain_main_context ();

    image = find_image (view);
    g_assert_nonnull (image);
    g_assert_null (gtk_image_get_icon_name (image));

    gtk_window_destroy (window);
    g_object_unref (window);
    g_object_unref (without_icon);
    g_object_unref (with_icon);
    g_object_unref (model);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    gtk_init ();

    g_test_add_func ("/gnome-utils/doclink-view/relative-icon-cleared-on-rebind",
                     test_relative_icon_is_cleared_on_rebind);
    return g_test_run ();
}
