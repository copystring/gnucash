#include <config.h>
#include "gnc-cell-renderer-text-flag.h"
static void
setup (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *icon = gtk_image_new ();
    GtkWidget *label = gtk_label_new (NULL);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (box), icon);
    gtk_box_append (GTK_BOX (box), label);
    gtk_list_item_set_child (item, box);
    (void)factory;
    (void)data;
}
GtkListItemFactory *
gnc_cell_renderer_text_flag_new (void)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (setup), NULL);
    return factory;
}
