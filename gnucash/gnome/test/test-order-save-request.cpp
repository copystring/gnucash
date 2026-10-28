#include <gtk/gtk.h>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"
extern "C" {
GObject *gnc_test_order_save_request_new (GtkWidget *);
gboolean gnc_test_order_save_request_inert (GObject *);
void gnc_test_order_save_request_arm (GObject *, gpointer, gpointer);
void gnc_test_order_save_request_complete (GObject *, gboolean);
}
class OrderSaveRequestTest : public ::testing::Test
{
protected:
    GtkWidget *window = nullptr;
    GObject *request = nullptr;
    int count = 0;
    static void finalized (gpointer data, GObject *) { ++*static_cast<int *> (data); }
    void SetUp () override
    {
        window = gtk_window_new ();
        g_object_ref_sink (window);
        request = gnc_test_order_save_request_new (window);
        ASSERT_NE (request, nullptr);
        g_object_weak_ref (request, finalized, &count);
        gnc_test_order_save_request_arm (request, window, window);
    }
    void TearDown () override
    {
        if (request) { g_object_weak_unref (request, finalized, &count); g_object_unref (request); }
        if (window) { gtk_window_destroy (GTK_WINDOW (window)); g_object_unref (window); }
    }
};
TEST_F (OrderSaveRequestTest, RepeatedDispose)
{
    g_object_run_dispose (request);
    EXPECT_TRUE (gnc_test_order_save_request_inert (request));
    g_object_run_dispose (request);
    EXPECT_TRUE (gnc_test_order_save_request_inert (request));
    gnc_test_order_save_request_complete (request, TRUE);
    gnc_test_order_save_request_complete (request, FALSE);
    g_object_unref (request); request = nullptr;
    EXPECT_EQ (count, 1);
}
TEST_F (OrderSaveRequestTest, DestroyedBeforeCallback)
{
    gtk_window_destroy (GTK_WINDOW (window));
    g_object_unref (window); window = nullptr;
    EXPECT_TRUE (gnc_test_order_save_request_inert (request));
    gnc_test_order_save_request_complete (request, TRUE);
    g_object_run_dispose (request);
    g_object_unref (request); request = nullptr;
    EXPECT_EQ (count, 1);
}
int main (int argc, char **argv)
{
    gnc::test::initialize_logging ();
    gtk_init ();
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
