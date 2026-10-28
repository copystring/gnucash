#include <gtk/gtk.h>
#include <gtest/gtest.h>
extern "C" {

GObject *gnc_test_order_save_request_new (GtkWidget *window);
gboolean gnc_test_order_save_request_inert (GObject *object);
void gnc_test_order_save_request_arm (GObject *object, gpointer owner, gpointer book);
void gnc_test_order_save_request_complete (GObject *object, gboolean completed);

}
static void finalized (gpointer data, GObject *object)
{
    (void)object;
    ++*(int *)data;
}

class OrderSaveRequestTest : public ::testing::Test {};
TEST_F (OrderSaveRequestTest, RepeatedDispose)
{
    GtkWidget *window = gtk_window_new ();
    g_object_ref_sink (window);
    GObject *request = gnc_test_order_save_request_new (window);
    int count = 0;
    g_object_weak_ref (request, finalized, &count);
    gnc_test_order_save_request_arm (request, window, window);
    g_object_run_dispose (request);
    g_assert_true (gnc_test_order_save_request_inert (request));
    g_object_run_dispose (request);
    g_assert_true (gnc_test_order_save_request_inert (request));
    /* The pending callback must be harmless even when it reports success. */
    gnc_test_order_save_request_complete (request, TRUE);
    gnc_test_order_save_request_complete (request, FALSE);
    g_object_unref (request);
    g_assert_cmpint (count, ==, 1);
    /* An undisconnected handler would dereference the freed request. */
    gtk_window_destroy (GTK_WINDOW (window));
    g_object_unref (window);
}

TEST_F (OrderSaveRequestTest, DestroyedBeforeCallback)
{
    GtkWidget *window = gtk_window_new ();
    g_object_ref_sink (window);
    GObject *request = gnc_test_order_save_request_new (window);
    int count = 0;
    g_object_weak_ref (request, finalized, &count);
    gnc_test_order_save_request_arm (request, window, window);
    gtk_window_destroy (GTK_WINDOW (window));
    /* GTK destroy is not necessarily emitted until final reference release. */
    g_object_unref (window);
    g_assert_true (gnc_test_order_save_request_inert (request));
    gnc_test_order_save_request_complete (request, TRUE);
    g_object_run_dispose (request);
    g_object_unref (request);
    g_assert_cmpint (count, ==, 1);
}

int main (int argc, char **argv)
{
    gtk_init ();
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
