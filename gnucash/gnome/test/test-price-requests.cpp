#include <gtk/gtk.h>
#include <initializer_list>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"
#include "gnc-pricedb.h"
#include "gnc-commodity.h"
#include "qof.h"
extern "C" {
GObject *gnc_test_price_delete_request_new (GNCPrice *);
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

int main (int argc, char **argv)
{
    ::testing::InitGoogleTest (&argc, argv);
    gtk_init ();
    qof_init ();
    gnc::test::initialize_logging ();
    auto result = RUN_ALL_TESTS ();
    qof_close ();
    return result;
}
