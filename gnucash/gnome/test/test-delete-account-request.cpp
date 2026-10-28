#include <gtk/gtk.h>
#include <gtest/gtest.h>
extern "C" GObject *gnc_test_delete_account_request_new (GObject *);
extern "C" gboolean gnc_test_delete_account_request_page_alive (GObject *);
extern "C" void gnc_test_delete_account_request_watch_finalize (GObject *, int *);
extern "C" void gnc_test_delete_account_confirmation_cancel (GObject *);

TEST (DeleteAccountRequest, WeakPageAndSharedLifetime)
{
    auto page = G_OBJECT (g_object_new (G_TYPE_OBJECT, nullptr));
    auto request = gnc_test_delete_account_request_new (page);
    int finalized = 0;
    gnc_test_delete_account_request_watch_finalize (request, &finalized);
    g_object_ref (request); // asynchronous callback owns a second reference
    EXPECT_TRUE (gnc_test_delete_account_request_page_alive (request));
    g_object_run_dispose (request);
    g_object_run_dispose (request);
    EXPECT_EQ (finalized, 0);
    EXPECT_TRUE (gnc_test_delete_account_request_page_alive (request));
    g_object_unref (page);
    EXPECT_FALSE (gnc_test_delete_account_request_page_alive (request));
    g_object_unref (request);
    EXPECT_EQ (finalized, 0);
    g_object_unref (request);
    EXPECT_EQ (finalized, 1);
}

TEST (DeleteAccountRequest, CancelCallbackReleasesItsReference)
{
    auto page = G_OBJECT (g_object_new (G_TYPE_OBJECT, nullptr));
    auto request = gnc_test_delete_account_request_new (page);
    int finalized = 0;
    gnc_test_delete_account_request_watch_finalize (request, &finalized);
    gnc_test_delete_account_confirmation_cancel (request);
    EXPECT_EQ (finalized, 0);
    EXPECT_TRUE (gnc_test_delete_account_request_page_alive (request));
    g_object_unref (request);
    EXPECT_EQ (finalized, 1);
    g_object_unref (page);
}
