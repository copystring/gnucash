/***************************************************************************
 *            test-lots.c
 *
 *  Copyright (C) 2003 Linas Vepstas <linas@linas.org>
 ****************************************************************************/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */
/**
 * @file test-lots.c
 * @brief Minimal test to see if automatic lot scrubbing works.
 * @author Linas Vepstas <linas@linas.org>
 */
#include <glib.h>

#include <config.h>
#include <ctype.h>
#include "qof.h"
#include "Account.h"
#include "gnc-lot.h"
#include "Scrub3.h"
#include "cashobjects.h"
#include "test-stuff.h"
#include "test-engine-stuff.h"
#include "Transaction.h"
#include "cap-gains.h"
#include "Scrub.h"
#include "Scrub2.h"
#include "gnc-session.h"

static gint transaction_num = 32;
static gint	max_iterate = 1;


static void
test_lot_kvp ()
{
    QofSession *sess = get_random_session ();
    QofBook *book = qof_session_get_book (sess);
    GNCLot *lot = gnc_lot_new (book);

    // title
    g_assert_cmpstr (gnc_lot_get_title (lot), ==, NULL);

    gnc_lot_set_title (lot, "");
    g_assert_cmpstr (gnc_lot_get_title (lot), ==, "");

    gnc_lot_set_title (lot, "doc");
    g_assert_cmpstr (gnc_lot_get_title (lot), ==, "doc");

    gnc_lot_set_title (lot, "unset");
    g_assert_cmpstr (gnc_lot_get_title (lot), ==, "unset");

    gnc_lot_set_title (lot, NULL);
    g_assert_cmpstr (gnc_lot_get_title (lot), ==, NULL);

    // notes
    g_assert_cmpstr (gnc_lot_get_notes (lot), ==, NULL);

    gnc_lot_set_notes (lot, "");
    g_assert_cmpstr (gnc_lot_get_notes (lot), ==, "");

    gnc_lot_set_notes (lot, "doc");
    g_assert_cmpstr (gnc_lot_get_notes (lot), ==, "doc");

    gnc_lot_set_notes (lot, "unset");
    g_assert_cmpstr (gnc_lot_get_notes (lot), ==, "unset");

    gnc_lot_set_notes (lot, NULL);
    g_assert_cmpstr (gnc_lot_get_notes (lot), ==, NULL);

    gnc_lot_destroy (lot);
    qof_session_destroy (sess);
}

struct LotAssignmentFixture
{
    QofSession *session;
    QofBook *book;
    Account *account;
    gnc_commodity *currency;
};

static Split *
add_lot_assignment_split (LotAssignmentFixture *fixture, gint64 amount,
                          time64 date)
{
    auto transaction = xaccMallocTransaction (fixture->book);
    auto split = xaccMallocSplit (fixture->book);
    auto numeric = gnc_numeric_create (amount, 1);

    xaccTransBeginEdit (transaction);
    xaccTransSetCurrency (transaction, fixture->currency);
    xaccTransSetDatePostedSecsNormalized (transaction, date);
    xaccSplitSetParent (split, transaction);
    xaccSplitSetAccount (split, fixture->account);
    xaccSplitSetAmount (split, numeric);
    xaccSplitSetValue (split, numeric);
    xaccTransCommitEdit (transaction);
    return split;
}

static LotAssignmentFixture
make_lot_assignment_fixture (void)
{
    LotAssignmentFixture fixture {};
    fixture.session = qof_session_new (qof_book_new ());
    fixture.book = qof_session_get_book (fixture.session);
    fixture.currency = gnc_commodity_new (fixture.book, "Lot Test Currency",
                                          "CURRENCY", "LTC", "", 100);
    auto stock = gnc_commodity_new (fixture.book, "Lot Test Stock", "NYSE",
                                     "LTS", "", 1000);
    auto root = gnc_account_create_root (fixture.book);
    fixture.account = xaccMallocAccount (fixture.book);

    xaccAccountBeginEdit (fixture.account);
    xaccAccountSetName (fixture.account, "Lot assignment account");
    xaccAccountSetType (fixture.account, ACCT_TYPE_STOCK);
    xaccAccountSetCommodity (fixture.account, stock);
    gnc_account_append_child (root, fixture.account);
    xaccAccountCommitEdit (fixture.account);

    add_lot_assignment_split (&fixture, 10, 86400);
    add_lot_assignment_split (&fixture, 10, 172800);
    add_lot_assignment_split (&fixture, -25, 259200);
    return fixture;
}

static gboolean
lot_assignment_matches (const Account *actual, const Account *expected)
{
    auto actual_lots = xaccAccountGetLotList (actual);
    auto expected_lots = xaccAccountGetLotList (expected);
    gboolean matches = g_list_length (actual_lots) == g_list_length (expected_lots) &&
                       xaccAccountGetSplitsSize (actual) ==
                       xaccAccountGetSplitsSize (expected);

    for (auto actual_node = actual_lots, expected_node = expected_lots;
         matches && actual_node && expected_node;
         actual_node = actual_node->next, expected_node = expected_node->next)
    {
        auto actual_lot = GNC_LOT (actual_node->data);
        auto expected_lot = GNC_LOT (expected_node->data);
        matches = gnc_lot_is_closed (actual_lot) == gnc_lot_is_closed (expected_lot) &&
                  gnc_numeric_equal (gnc_lot_get_balance (actual_lot),
                                     gnc_lot_get_balance (expected_lot)) &&
                  gnc_lot_count_splits (actual_lot) ==
                  gnc_lot_count_splits (expected_lot);

        for (auto actual_split = gnc_lot_get_split_list (actual_lot),
                  expected_split = gnc_lot_get_split_list (expected_lot);
             matches && actual_split && expected_split;
             actual_split = actual_split->next, expected_split = expected_split->next)
            matches = gnc_numeric_equal (
                xaccSplitGetAmount (GNC_SPLIT (actual_split->data)),
                xaccSplitGetAmount (GNC_SPLIT (expected_split->data)));
    }

    g_list_free (actual_lots);
    g_list_free (expected_lots);
    return matches;
}

static void
destroy_lot_assignment_fixture (LotAssignmentFixture *fixture)
{
    qof_session_destroy (fixture->session);
}

static LotAssignmentFixture
make_empty_lot_assignment_fixture (void)
{
    LotAssignmentFixture fixture {};
    fixture.session = qof_session_new (qof_book_new ());
    fixture.book = qof_session_get_book (fixture.session);
    fixture.currency = gnc_commodity_new (fixture.book, "Lot Test Currency",
                                          "CURRENCY", "LTC", "", 100);
    auto stock = gnc_commodity_new (fixture.book, "Lot Test Stock", "NYSE",
                                     "LTS", "", 1000);
    auto root = gnc_account_create_root (fixture.book);
    fixture.account = xaccMallocAccount (fixture.book);

    xaccAccountBeginEdit (fixture.account);
    xaccAccountSetName (fixture.account, "Lot work budget account");
    xaccAccountSetType (fixture.account, ACCT_TYPE_STOCK);
    xaccAccountSetCommodity (fixture.account, stock);
    gnc_account_append_child (root, fixture.account);
    xaccAccountCommitEdit (fixture.account);
    return fixture;
}

static void
populate_lot_assignment_noop_workload (LotAssignmentFixture *fixture)
{
    for (guint i = 0; i < 6; ++i)
    {
        auto split = add_lot_assignment_split (fixture, 0, (i + 1) * 86400);
        if (i % 2 == 0)
            xaccTransVoid (xaccSplitGetParent (split), "bounded no-op");
    }

    auto assigned = add_lot_assignment_split (fixture, 10, 7 * 86400);
    xaccSplitAssign (assigned);
    add_lot_assignment_split (fixture, 10, 8 * 86400);
    add_lot_assignment_split (fixture, -25, 9 * 86400);
}

static void
test_incremental_lot_assignment_work_budget (void)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto incremental = make_empty_lot_assignment_fixture ();
    auto synchronous = make_empty_lot_assignment_fixture ();
    populate_lot_assignment_noop_workload (&incremental);
    populate_lot_assignment_noop_workload (&synchronous);
    gnc_set_current_session (incremental.session);
    auto context = gnc_scrub_context_begin (incremental.book);
    auto plan = gnc_lot_assignment_plan_begin (incremental.account, context);
    do_test (plan != NULL, "start bounded no-op lot assignment");

    guint steps = 0;
    while (plan && gnc_lot_assignment_plan_get_state (plan) ==
           GNC_LOT_ASSIGNMENT_PLAN_RUNNING && steps < 32)
    {
        auto before_examined = gnc_lot_assignment_plan_get_examined (plan);
        auto before_completed = gnc_lot_assignment_plan_get_completed (plan);
        gnc_lot_assignment_plan_step (plan, 2);
        do_test (gnc_lot_assignment_plan_get_examined (plan) - before_examined <= 2,
                 "lot assignment step dequeues at most max work entries");
        ++steps;
        if (steps <= 3)
            do_test (gnc_lot_assignment_plan_get_completed (plan) == before_completed,
                     "no-op entries consume bounded work without mutation");
    }

    do_test (steps > 3, "no-op queue reaches done only after multiple work turns");
    do_test (gnc_lot_assignment_plan_get_state (plan) ==
             GNC_LOT_ASSIGNMENT_PLAN_DONE,
             "no-op work budget plan reaches done state");
    xaccAccountAssignLots (synchronous.account);
    do_test (lot_assignment_matches (incremental.account, synchronous.account),
             "no-op work budget plan matches synchronous result");

    gnc_lot_assignment_plan_free (plan);
    gnc_scrub_context_end (context);
    gnc_scrub_context_unref (context);
    gnc_clear_current_session ();
    destroy_lot_assignment_fixture (&synchronous);
}

static void
test_incremental_lot_assignment (void)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto incremental = make_lot_assignment_fixture ();
    auto synchronous = make_lot_assignment_fixture ();
    gnc_set_current_session (incremental.session);
    auto context = gnc_scrub_context_begin (incremental.book);
    auto plan = gnc_lot_assignment_plan_begin (incremental.account, context);
    do_test (context != NULL && plan != NULL,
             "start bounded lot assignment with active scrub context");

    guint steps = 0;
    while (plan && gnc_lot_assignment_plan_get_state (plan) ==
           GNC_LOT_ASSIGNMENT_PLAN_RUNNING && steps < 16)
    {
        auto before = gnc_lot_assignment_plan_get_completed (plan);
        auto state = gnc_lot_assignment_plan_step (plan, 1);
        auto after = gnc_lot_assignment_plan_get_completed (plan);
        do_test (after - before <= 1, "lot assignment step respects max mutations");
        ++steps;
        if (steps == 3)
            do_test (xaccAccountGetSplitsSize (incremental.account) == 4,
                     "first sale assignment queues one remainder split");
        if (steps == 4)
            do_test (xaccAccountGetSplitsSize (incremental.account) == 5,
                     "remainder is assigned before later input and splits again");
        if (state != GNC_LOT_ASSIGNMENT_PLAN_RUNNING)
            break;
    }
    do_test (steps == 5, "lot assignment completes across five bounded mutations");
    do_test (gnc_lot_assignment_plan_get_state (plan) ==
             GNC_LOT_ASSIGNMENT_PLAN_DONE,
             "bounded lot assignment reaches done state");

    xaccAccountAssignLots (synchronous.account);
    do_test (lot_assignment_matches (incremental.account, synchronous.account),
             "bounded lot assignment matches synchronous FIFO result");

    gnc_lot_assignment_plan_free (plan);
    gnc_scrub_context_end (context);
    gnc_scrub_context_unref (context);
    gnc_clear_current_session ();
    destroy_lot_assignment_fixture (&synchronous);
}

static void
test_incremental_lot_assignment_cancel_and_stale (void)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto cancelled = make_lot_assignment_fixture ();
    gnc_set_current_session (cancelled.session);
    auto cancelled_context = gnc_scrub_context_begin (cancelled.book);
    auto cancelled_plan = gnc_lot_assignment_plan_begin (cancelled.account,
                                                          cancelled_context);
    do_test (cancelled_plan != NULL, "start cancellable lot assignment");
    gnc_lot_assignment_plan_step (cancelled_plan, 1);
    auto completed = gnc_lot_assignment_plan_get_completed (cancelled_plan);
    gnc_lot_assignment_plan_cancel (cancelled_plan);
    do_test (gnc_lot_assignment_plan_step (cancelled_plan, 1) ==
             GNC_LOT_ASSIGNMENT_PLAN_CANCELLED &&
             gnc_lot_assignment_plan_get_completed (cancelled_plan) == completed,
             "cancelled plan performs no follow-up mutation");
    gnc_lot_assignment_plan_free (cancelled_plan);
    gnc_scrub_context_end (cancelled_context);
    gnc_scrub_context_unref (cancelled_context);
    gnc_clear_current_session ();

    auto stale = make_lot_assignment_fixture ();
    gnc_set_current_session (stale.session);
    auto stale_context = gnc_scrub_context_begin (stale.book);
    auto stale_plan = gnc_lot_assignment_plan_begin (stale.account, stale_context);
    do_test (stale_plan != NULL, "start stale-context lot assignment");
    gnc_lot_assignment_plan_step (stale_plan, 1);
    completed = gnc_lot_assignment_plan_get_completed (stale_plan);
    gnc_scrub_context_end (stale_context);
    do_test (gnc_lot_assignment_plan_step (stale_plan, 1) ==
             GNC_LOT_ASSIGNMENT_PLAN_STALE &&
             gnc_lot_assignment_plan_get_completed (stale_plan) == completed,
             "stale context prevents a follow-up mutation");
    gnc_lot_assignment_plan_free (stale_plan);
    gnc_scrub_context_unref (stale_context);
    gnc_clear_current_session ();
}

static void
run_test (void)
{
    QofSession *sess;
    QofBook *book;
    Account *root;

    /* --------------------------------------------------------- */
    /* In the first test, we will merely try to see if we can run
     * without crashing.  We don't check to see if data is good. */
    sess = get_random_session ();
    book = qof_session_get_book (sess);
    do_test ((NULL != book), "create random data");

    add_random_transactions_to_book (book, transaction_num);

    root = gnc_book_get_root_account (book);
    xaccAccountTreeScrubLots (root);

    /* --------------------------------------------------------- */
    /* In the second test, we create an account with unrealized gains,
     * and see if that gets fixed correctly, with the correct balances,
     * and etc.
     * XXX not implemented
     */
    success ("automatic lot scrubbing lightly tested and seem to work");
    qof_session_destroy (sess);

}

int
main (int argc, char **argv)
{
    gint i;

    qof_init();
    if (!cashobjects_register())
        exit(1);

    /* Any tests that cause an error or warning to be printed
     * automatically fail! */
    g_log_set_always_fatal((GLogLevelFlags)(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING));
    /* Set up a reproducible test-case */
    srand(0);
    /* Iterate the test a number of times */
    for (i = 0; i < max_iterate; i++)
    {
        fprintf(stdout, " Lots: %d of %d paired tests . . . \r",
                (i + 1) * 2, max_iterate * 2);
        fflush(stdout);
        run_test ();
    }

    test_lot_kvp ();
    test_incremental_lot_assignment ();
    test_incremental_lot_assignment_work_budget ();
    test_incremental_lot_assignment_cancel_and_stale ();

    /* 'erase' the recurring tag line with dummy spaces. */
    fprintf(stdout, "Lots: Test series complete.\n");
    fflush(stdout);
    print_test_results();

    qof_close();
    return get_rv();
}
