#include <gtk/gtk.h>
#include <gtest/gtest.h>
extern "C" GObject *gnc_test_delete_account_request_new (GObject *);
extern "C" gboolean gnc_test_delete_account_request_page_alive (GObject *);

TEST (DeleteAccountRequest, WeakPageAndSharedLifetime)
{
    auto page = G_OBJECT (g_object_new (G_TYPE_OBJECT, nullptr));
    auto request = gnc_test_delete_account_request_new (page);
    bool finalized = false;
    g_object_weak_ref (request, [] (gpointer data, GObject *) {
        *static_cast<bool *> (data) = true;
    }, &finalized);
    g_object_ref (request); // asynchronous callback owns a second reference
    EXPECT_TRUE (gnc_test_delete_account_request_page_alive (request));
    g_object_unref (page);
    EXPECT_FALSE (gnc_test_delete_account_request_page_alive (request));
    g_object_unref (request);
    EXPECT_FALSE (finalized);
    g_object_run_dispose (request);
    g_object_run_dispose (request);
    g_object_unref (request);
    EXPECT_TRUE (finalized);
}
