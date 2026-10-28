#include <gtk/gtk.h>
#include <initializer_list>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"
#include "gnc-pricedb.h"
#include "gnc-commodity.h"
#include "qof.h"
#include "qofsession.h"
#include "cashobjects.h"
#include "gnc-component-manager.h"
extern "C" {
GObject *gnc_test_price_delete_request_new (GNCPrice *);
GObject *gnc_test_price_delete_request_for_book (GtkWidget *, GNCPrice *, QofBook *);
void gnc_test_price_delete_finished (GObject *, gint);
gboolean gnc_test_price_delete_request_pending (GObject *);
void gnc_test_price_old_remove_watch_book (GObject *, QofBook *);
void gnc_test_price_delete_request_complete (GObject *, gboolean);
GObject *gnc_test_price_old_remove_request_new (GtkWindow *);
void gnc_test_price_old_remove_request_complete (GObject *);
void gnc_test_price_old_remove_confirmation_finished (GObject *, gint);
gboolean gnc_test_price_old_remove_confirmation_is_delete (gint);
GObject *gnc_test_price_remove_row_new (gnc_commodity *);
}

static void finalized (gpointer count, GObject *)
{
    ++*static_cast<unsigned *> (count);
}

TEST (PriceRequests, DeleteCancelAndMissingParentReleasePriceOnce)
{
    for (auto confirmed : {FALSE, TRUE})
    {
        // The price has no database or book dependencies in this ownership test.
        auto price = GNC_PRICE (g_object_new (GNC_TYPE_PRICE, nullptr));
        unsigned destroyed = 0;
        g_object_weak_ref (G_OBJECT (price), finalized, &destroyed);
        auto request = gnc_test_price_delete_request_new (price);
        gnc_price_unref (price); // Only the request now holds a price reference.
        EXPECT_EQ (destroyed, 0u);
        gnc_test_price_delete_request_complete (request, confirmed);
        EXPECT_EQ (destroyed, 1u);
        gnc_test_price_delete_request_complete (request, confirmed);
        g_object_run_dispose (request);
        g_object_run_dispose (request);
        EXPECT_EQ (destroyed, 1u);
        g_object_unref (request);
    }
}

TEST (PriceRequests, DeleteDisposeWithoutResponseReleasesPrice)
{
    auto price = GNC_PRICE (g_object_new (GNC_TYPE_PRICE, nullptr));
    unsigned destroyed = 0;
    g_object_weak_ref (G_OBJECT (price), finalized, &destroyed);
    auto request = gnc_test_price_delete_request_new (price);
    gnc_price_unref (price);
    g_object_unref (request);
    EXPECT_EQ (destroyed, 1u);
}

TEST (PriceRequests, CommodityRowOwnsDirectObject)
{
    auto commodity = GNC_COMMODITY (g_object_new (GNC_TYPE_COMMODITY, nullptr));
    unsigned destroyed = 0;
    g_object_weak_ref (G_OBJECT (commodity), finalized, &destroyed);
    auto row = gnc_test_price_remove_row_new (commodity);
    EXPECT_EQ (g_object_get_data (row, "gnc-price-remove-commodity"), commodity);
    g_object_unref (commodity);
    EXPECT_EQ (destroyed, 0u);
    g_object_unref (row);
    EXPECT_EQ (destroyed, 1u);
}

TEST (PriceRequests, OldRemoveActionResponseUsesAccept)
{
    EXPECT_TRUE (gnc_test_price_old_remove_confirmation_is_delete (GTK_RESPONSE_ACCEPT));
    for (auto response : {GTK_RESPONSE_OK, GTK_RESPONSE_CANCEL,
                          GTK_RESPONSE_DELETE_EVENT, GTK_RESPONSE_NONE})
        EXPECT_FALSE (gnc_test_price_old_remove_confirmation_is_delete (response));
}

TEST (PriceRequests, OldRemoveDisposeIsIdempotent)
{
    auto dialog = GTK_WINDOW (gtk_window_new ());
    auto request = gnc_test_price_old_remove_request_new (dialog);
    unsigned destroyed = 0;
    g_object_weak_ref (request, finalized, &destroyed);
    g_object_run_dispose (request);
    // GObject weak notifications run during dispose, not only finalization.
    EXPECT_EQ (destroyed, 1u);
    g_object_run_dispose (request);
    EXPECT_EQ (destroyed, 1u);
    g_object_unref (request);
    EXPECT_EQ (destroyed, 1u);
}

TEST (PriceRequests, OldRemoveLateConfirmationAfterCancel)
{
    for (auto response : {GTK_RESPONSE_ACCEPT, GTK_RESPONSE_CANCEL})
    {
        auto dialog = GTK_WINDOW (gtk_window_new ());
        auto request = gnc_test_price_old_remove_request_new (dialog);
        unsigned destroyed = 0;
        g_object_weak_ref (request, finalized, &destroyed);
        g_object_ref (request); // Pending confirmation's reference.
        gnc_test_price_old_remove_request_complete (request); // Drop UI owner.
        EXPECT_EQ (destroyed, 0u);
        gnc_test_price_old_remove_request_complete (request); // No second drop.
        gnc_test_price_old_remove_confirmation_finished (request, response);
        EXPECT_EQ (destroyed, 1u);
    }
}

// Real engine objects and collections: raw GObjects do not cover book teardown.
static GNCPrice *book_price (QofBook *book)
{
    auto table = gnc_commodity_table_get_table (book);
    auto commodity = gnc_commodity_table_insert (table,
        gnc_commodity_new (book, "Stock", "TEST", "STOCK", "", 100));
    auto currency = gnc_commodity_table_insert (table,
        gnc_commodity_new (book, "Currency", "CURRENCY", "CUR", "", 100));
    auto price = gnc_price_create (book);
    gnc_price_set_commodity (price, commodity);
    gnc_price_set_currency (price, currency);
    gnc_price_set_time64 (price, 1234567890);
    gnc_price_set_source (price, PRICE_SOURCE_USER_PRICE);
    gnc_price_set_value (price, gnc_numeric_create (1, 1));
    EXPECT_TRUE (gnc_pricedb_add_price (gnc_pricedb_get_db (book), price));
    return price;
}

TEST (PriceRequests, RealSessionParentCloseThenBookDestroyAndLateResponse)
{
    for (auto response : {GTK_RESPONSE_YES, GTK_RESPONSE_NO})
    {
        auto session = qof_session_new (nullptr);
        auto book = qof_session_get_book (session);
        auto price = book_price (book);
        auto parent = GTK_WIDGET (gtk_window_new ());
        auto request = gnc_test_price_delete_request_for_book (parent, price, book);
        gnc_price_unref (price);
        EXPECT_TRUE (gnc_test_price_delete_request_pending (request));
        gtk_window_destroy (GTK_WINDOW (parent));
        EXPECT_FALSE (gnc_test_price_delete_request_pending (request));
        qof_session_destroy (session);
        unsigned destroyed = 0;
        g_object_weak_ref (request, finalized, &destroyed);
        gnc_test_price_delete_finished (request, response);
        EXPECT_EQ (destroyed, 1u);
    }
}

TEST (PriceRequests, RealSessionBookDestroyWhileEventsSuspendedThenLateResponse)
{
    for (auto response : {GTK_RESPONSE_YES, GTK_RESPONSE_NO})
    {
        auto session = qof_session_new (nullptr);
        auto book = qof_session_get_book (session);
        auto price = book_price (book);
        auto request = gnc_test_price_delete_request_for_book (nullptr, price, book);
        gnc_price_unref (price);
        unsigned price_destroyed = 0;
        g_object_weak_ref (G_OBJECT (price), finalized, &price_destroyed);
        qof_event_suspend ();
        qof_session_destroy (session); // Book destroy force-event must still run.
        qof_event_resume ();
        EXPECT_FALSE (gnc_test_price_delete_request_pending (request));
        EXPECT_EQ (price_destroyed, 1u);
        unsigned destroyed = 0;
        g_object_weak_ref (request, finalized, &destroyed);
        gnc_test_price_delete_finished (request, response);
        EXPECT_EQ (destroyed, 1u);
    }
}

TEST (PriceRequests, CommoditySemanticDestroyCancelsPendingPriceRequest)
{
    auto session = qof_session_new (nullptr);
    auto book = qof_session_get_book (session);
    auto price = book_price (book);
    auto commodity = gnc_price_get_commodity (price);
    auto request = gnc_test_price_delete_request_for_book (nullptr, price, book);
    gnc_price_unref (price);
    // Do not leave a database price pointing at the commodity being destroyed.
    gnc_pricedb_remove_price (gnc_pricedb_get_db (book), price);
    gnc_commodity_destroy (commodity);
    EXPECT_FALSE (gnc_test_price_delete_request_pending (request));
    qof_session_destroy (session);
    gnc_test_price_delete_finished (request, GTK_RESPONSE_YES);
}

TEST (PriceRequests, OldRemoveBookDestroyThenLateConfirmation)
{
    auto session = qof_session_new (nullptr);
    auto book = qof_session_get_book (session);
    auto price = book_price (book);
    gnc_price_unref (price);
    auto request = gnc_test_price_old_remove_request_new (GTK_WINDOW (gtk_window_new ()));
    gnc_test_price_old_remove_watch_book (request, book);
    g_object_ref (request); // Pending confirmation survives the UI owner.
    unsigned destroyed = 0;
    g_object_weak_ref (request, finalized, &destroyed);
    qof_event_suspend ();
    qof_session_destroy (session);
    qof_event_resume ();
    EXPECT_EQ (destroyed, 0u);
    gnc_test_price_old_remove_confirmation_finished (request, GTK_RESPONSE_ACCEPT);
    EXPECT_EQ (destroyed, 1u);
}

int main (int argc, char **argv)
{
    ::testing::InitGoogleTest (&argc, argv);
    gtk_init ();
    qof_init ();
    if (!cashobjects_register ()) g_error ("cashobjects registration failed");
    gnc_component_manager_init ();
    gnc::test::initialize_logging ();
    auto result = RUN_ALL_TESTS ();
    gnc_component_manager_shutdown ();
    qof_close ();
    return result;
}
