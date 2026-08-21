#include <config.h>

#include <gtk/gtk.h>

#include "gnc-gtk-utils.h"

static const gchar *
statusbar_text (GtkWidget *statusbar)
{
    GtkWidget *label = gtk_widget_get_first_child (statusbar);

    g_assert_true (GTK_IS_LABEL (label));
    return gtk_label_get_text (GTK_LABEL (label));
}

static void
test_message_stack (void)
{
    GtkWidget *statusbar = gnc_statusbar_new ();
    guint base;
    guint section;
    guint tooltip;

    g_object_ref_sink (statusbar);
    g_assert_true (gnc_statusbar_is (statusbar));
    g_assert_cmpstr (statusbar_text (statusbar), ==, " ");

    base = gnc_statusbar_push (statusbar, 0, "base");
    section = gnc_statusbar_push (statusbar, 1, "section");
    tooltip = gnc_statusbar_push (statusbar, 0, "tooltip");
    g_assert_cmpuint (base, !=, 0);
    g_assert_cmpuint (section, !=, 0);
    g_assert_cmpuint (tooltip, !=, 0);
    g_assert_cmpstr (statusbar_text (statusbar), ==, "tooltip");

    gnc_statusbar_pop (statusbar, 0);
    g_assert_cmpstr (statusbar_text (statusbar), ==, "section");
    gnc_statusbar_remove (statusbar, 1, section);
    g_assert_cmpstr (statusbar_text (statusbar), ==, "base");
    gnc_statusbar_remove (statusbar, 0, base);
    g_assert_cmpstr (statusbar_text (statusbar), ==, " ");

    g_object_unref (statusbar);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    gtk_init ();
    g_test_add_func ("/gnome-utils/statusbar/message-stack", test_message_stack);

    return g_test_run ();
}
