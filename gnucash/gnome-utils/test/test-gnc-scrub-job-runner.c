#include <config.h>

#include <glib.h>

#include "gnc-scrub-job-runner.h"
#include "Account.h"
#include "Split.h"
#include "Transaction.h"
#include "gnc-commodity.h"
#include "gnc-session.h"
#include "qofbook.h"
#include "qofsession.h"

typedef struct
{
    guint progress_calls;
    guint done_calls;
    GncScrubJobState state;
} Observer;

static GncScrubJob *
make_orphan_job (guint transaction_count)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    Account *root = gnc_account_create_root (book);
    Account *account = xaccMallocAccount (book);
    gnc_commodity *currency = gnc_commodity_new (book, "Runner test currency",
                                                   "CURRENCY", "RUN", "", 100);

    gnc_set_current_session (session);
    xaccAccountBeginEdit (account);
    xaccAccountSetName (account, "Runner test account");
    xaccAccountSetType (account, ACCT_TYPE_BANK);
    xaccAccountSetCommodity (account, currency);
    gnc_account_append_child (root, account);
    xaccAccountCommitEdit (account);

    for (guint index = 0; index < transaction_count; ++index)
    {
        Transaction *transaction = xaccMallocTransaction (book);
        Split *attached = xaccMallocSplit (book);
        Split *orphan = xaccMallocSplit (book);

        xaccTransBeginEdit (transaction);
        xaccTransSetCurrency (transaction, currency);
        xaccSplitSetParent (attached, transaction);
        xaccSplitSetAccount (attached, account);
        xaccSplitSetParent (orphan, transaction);
        xaccTransCommitEdit (transaction);
    }

    return gnc_scrub_orphans_job_begin (account, FALSE);
}

static void
progress_cb (GncScrubJobRunner *runner, guint completed, guint total,
             gpointer user_data)
{
    Observer *observer = user_data;

    (void)runner;
    g_assert_cmpuint (completed, <=, total);
    observer->progress_calls++;
}

static void
done_cb (GncScrubJobRunner *runner, GncScrubJobState state, gpointer user_data)
{
    Observer *observer = user_data;

    (void)runner;
    observer->done_calls++;
    observer->state = state;
}

static void
iterate_until (Observer *observer, guint progress_calls)
{
    for (guint attempt = 0; attempt < 32 && observer->progress_calls < progress_calls;
         ++attempt)
        g_main_context_iteration (NULL, FALSE);

    g_assert_cmpuint (observer->progress_calls, >=, progress_calls);
}

static void
test_runs_one_bounded_step_per_idle (void)
{
    Observer observer = { 0 };
    GncScrubJobRunner *runner = gnc_scrub_job_runner_start (
        make_orphan_job (3), NULL, NULL, 1, progress_cb, done_cb, &observer, NULL);

    iterate_until (&observer, 1);
    g_assert_cmpuint (observer.done_calls, ==, 0);
    iterate_until (&observer, 2);
    g_assert_cmpuint (observer.done_calls, ==, 0);
    iterate_until (&observer, 3);
    g_assert_cmpuint (observer.done_calls, ==, 1);
    g_assert_cmpint (observer.state, ==, GNC_SCRUB_JOB_DONE);
    g_assert_true (gnc_scrub_job_runner_is_finished (runner));
    gnc_scrub_job_runner_unref (runner);
    gnc_clear_current_session ();
}

static void
test_cancellable_completes_once_between_steps (void)
{
    Observer observer = { 0 };
    GCancellable *cancellable = g_cancellable_new ();
    GncScrubJobRunner *runner = gnc_scrub_job_runner_start (
        make_orphan_job (3), NULL, cancellable, 1, progress_cb, done_cb,
        &observer, NULL);

    iterate_until (&observer, 1);
    g_assert_cmpuint (observer.done_calls, ==, 0);
    g_cancellable_cancel (cancellable);
    g_assert_cmpuint (observer.done_calls, ==, 0);
    iterate_until (&observer, 2);
    g_assert_cmpuint (observer.done_calls, ==, 1);
    g_assert_cmpint (observer.state, ==, GNC_SCRUB_JOB_CANCELLED);
    for (guint attempt = 0; attempt < 4; ++attempt)
        g_main_context_iteration (NULL, FALSE);
    g_assert_cmpuint (observer.done_calls, ==, 1);
    g_assert_true (gnc_scrub_job_runner_is_finished (runner));
    gnc_scrub_job_runner_unref (runner);
    g_object_unref (cancellable);
    gnc_clear_current_session ();
}

static void
test_owner_destruction_completes_once (void)
{
    Observer observer = { 0 };
    GObject *owner = g_object_new (G_TYPE_OBJECT, NULL);
    GncScrubJobRunner *runner = gnc_scrub_job_runner_start (
        make_orphan_job (3), owner, NULL, 1, progress_cb, done_cb, &observer, NULL);

    iterate_until (&observer, 1);
    g_object_unref (owner);
    g_assert_cmpuint (observer.done_calls, ==, 1);
    g_assert_cmpint (observer.state, ==, GNC_SCRUB_JOB_CANCELLED);
    for (guint attempt = 0; attempt < 4; ++attempt)
        g_main_context_iteration (NULL, FALSE);
    g_assert_cmpuint (observer.done_calls, ==, 1);
    gnc_scrub_job_runner_unref (runner);
    gnc_clear_current_session ();
}
static void
test_pre_cancelled_cancellable_completes_once (void)
{
    Observer observer = { 0 };
    GCancellable *cancellable = g_cancellable_new ();
    GncScrubJobRunner *runner;

    g_cancellable_cancel (cancellable);
    runner = gnc_scrub_job_runner_start (make_orphan_job (1), NULL, cancellable,
                                         1, progress_cb, done_cb, &observer, NULL);
    iterate_until (&observer, 1);
    g_assert_cmpuint (observer.done_calls, ==, 1);
    g_assert_cmpint (observer.state, ==, GNC_SCRUB_JOB_CANCELLED);
    g_assert_true (gnc_scrub_job_runner_is_finished (runner));
    gnc_scrub_job_runner_unref (runner);
    g_object_unref (cancellable);
    gnc_clear_current_session ();
}
int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    gnc_engine_init (argc, argv);
    g_test_add_func ("/gnome-utils/scrub-job-runner/bounded-idle",
                     test_runs_one_bounded_step_per_idle);
    g_test_add_func ("/gnome-utils/scrub-job-runner/cancellable",
                     test_cancellable_completes_once_between_steps);
    g_test_add_func ("/gnome-utils/scrub-job-runner/owner-destroyed",
                     test_owner_destruction_completes_once);
    g_test_add_func ("/gnome-utils/scrub-job-runner/pre-cancelled",
                     test_pre_cancelled_cancellable_completes_once);

    int status = g_test_run ();
    gnc_engine_shutdown ();
    return status;
}