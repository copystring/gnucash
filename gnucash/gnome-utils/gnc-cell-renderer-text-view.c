#include <config.h>
#include "gnc-cell-renderer-text-view.h"
static void
setup (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    GtkWidget *label = gtk_editable_label_new (NULL);
    gtk_editable_set_enable_undo (GTK_EDITABLE (label), TRUE);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    gtk_list_item_set_child (item, label);
    (void)factory;
    (void)data;
}
GtkListItemFactory *
gnc_cell_renderer_text_view_new (void)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (setup), NULL);
    return factory;
}
