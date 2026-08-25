/********************************************************************
 * test-qofsession.cpp: A Google Test suite for Qof Session.        *
 * Copyright 2016 Aaron Laws                                        *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, you can retrieve it from        *
 * https://www.gnu.org/licenses/old-licenses/gpl-2.0.html            *
 * or contact:                                                      *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 ********************************************************************/

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

#include "../guid.hpp"
#include <qofsession.hpp>
#include <gnc-session.h>
#include <Scrub.h>
#include <ScrubP.h>
#include <qof-backend.hpp>
#include <cstdlib>
#include "../gnc-backend-prov.hpp"
#include "../Account.h"

static QofBook * exported_book {nullptr};
static bool safe_sync_called {false};
static bool sync_called {false};
static bool load_error {true};
static bool data_loaded {false};
static bool observe_current_session_during_end {false};
static bool current_session_was_detached_on_end {false};

struct DestroyAccount
{
    void operator()(Account *acct)
    {
        xaccAccountBeginEdit (acct);
        xaccAccountDestroy (acct);
    }
};

using AccountPtr = std::unique_ptr<Account, DestroyAccount>;

class QofSessionMockBackend : public QofBackend
{
    AccountPtr m_root;
public:
    QofSessionMockBackend() = default;
    QofSessionMockBackend(const QofSessionMockBackend&) = delete;
    QofSessionMockBackend(const QofSessionMockBackend&&) = delete;
    virtual ~QofSessionMockBackend() = default;
    void session_begin(QofSession*, const char*, SessionOpenMode) {}
    void session_end()
    {
        if (observe_current_session_during_end)
            current_session_was_detached_on_end =
                !gnc_current_session_exist ();
    }
    void load(QofBook*, QofBackendLoadType);
    void sync(QofBook*);
    void safe_sync(QofBook*);
    void export_coa(QofBook*);
};

void QofSessionMockBackend::load (QofBook *book, QofBackendLoadType)
{
    if (load_error)
        set_error(ERR_BACKEND_NO_BACKEND);
    else
        m_root = AccountPtr{gnc_account_create_root (book)};
    data_loaded = true;
}

void QofSessionMockBackend::safe_sync (QofBook *)
{
    safe_sync_called = true;
}

void QofSessionMockBackend::sync (QofBook *)
{
    sync_called = true;
}

void QofSessionMockBackend::export_coa(QofBook * book)
{
    exported_book = book;
}

static QofBackend*
test_backend_factory ()
{
    return new QofSessionMockBackend;
}

struct MockProvider : public QofBackendProvider
{
    MockProvider (char const * name, char const * access_method)
        : QofBackendProvider {name, access_method} {}
    QofBackend * create_backend (void) {return test_backend_factory ();}
    bool type_check (char const * type) {return true;}
};

static QofBackendProvider_ptr
get_provider ()
{
    return QofBackendProvider_ptr {new MockProvider {"Mock Backend", "file"}};
}

TEST (QofSessionTest, swap_books)
{
    qof_backend_register_provider (get_provider ());
    QofSession s1(qof_book_new());
    s1.begin ("book1", SESSION_NORMAL_OPEN);
    QofSession s2(qof_book_new());
    s2.begin ("book2", SESSION_NORMAL_OPEN);
    QofBook * b1 {s1.get_book ()};
    QofBook * b2 {s2.get_book ()};
    ASSERT_NE (b1, b2);
    s1.swap_books (s2);
    EXPECT_EQ (s1.get_book (), b2);
    EXPECT_EQ (s2.get_book (), b1);
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionTest, ensure_all_data_loaded)
{
    qof_backend_register_provider (get_provider ());
    QofSession s(qof_book_new());
    s.begin ("book1", SESSION_NORMAL_OPEN);
    data_loaded = false;
    s.ensure_all_data_loaded ();
    EXPECT_EQ (data_loaded, true);
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionTest, get_error)
{
    qof_backend_register_provider (get_provider ());
    QofSession s(qof_book_new());
    s.begin ("book1", SESSION_NORMAL_OPEN);
    s.ensure_all_data_loaded ();
    EXPECT_NE (s.get_error (), ERR_BACKEND_NO_ERR);
    //get_error should not clear the error.
    EXPECT_NE (s.get_error (), ERR_BACKEND_NO_ERR);
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionTest, pop_error)
{
    qof_backend_register_provider (get_provider ());
    QofSession s(qof_book_new());
    s.begin ("book1", SESSION_NORMAL_OPEN);
    //We run the test first, and make sure there is an error condition.
    s.ensure_all_data_loaded ();
    EXPECT_NE (s.pop_error (), ERR_BACKEND_NO_ERR);
    EXPECT_EQ (s.get_error (), ERR_BACKEND_NO_ERR);
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionTest, clear_error)
{
    qof_backend_register_provider (get_provider ());
    QofSession s(qof_book_new());
    s.begin ("book1", SESSION_NORMAL_OPEN);
    //We run the test first, and make sure there is an error condition.
    s.ensure_all_data_loaded ();
    EXPECT_NE (s.get_error (), ERR_BACKEND_NO_ERR);
    //Now we run it, and clear_error to make sure the error is actually cleared.
    s.ensure_all_data_loaded ();
    s.clear_error ();
    EXPECT_EQ (s.get_error (), ERR_BACKEND_NO_ERR);
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionTest, load)
{
    /* We register a provider that gives a backend that throws an error on load.
     * This error during load should cause the qof session to destroy the book
     * and create a new one.
     */
    qof_backend_register_provider (get_provider ());
    QofSession s{qof_book_new()};
    s.begin ("book1", SESSION_NORMAL_OPEN);
    char *guidstr1 = guid_to_string(qof_instance_get_guid(s.get_book ()));
    s.load (nullptr);
    char *guidstr2 = guid_to_string(qof_instance_get_guid(s.get_book ()));
    EXPECT_STRNE (guidstr1, guidstr2);
    g_free(guidstr1);
    g_free(guidstr2);

    /* Now we'll do the load without returning an error from the backend,
     * and ensure that it's the new book from the previous test.
     */
    load_error = false;
    guidstr1 = guid_to_string(qof_instance_get_guid(s.get_book ()));
    s.load (nullptr);
    guidstr2 = guid_to_string(qof_instance_get_guid(s.get_book ()));
    EXPECT_STREQ (guidstr1, guidstr2);
    g_free(guidstr1);
    g_free(guidstr2);
    EXPECT_EQ (s.get_error(), ERR_BACKEND_NO_ERR);
    //But it's still empty, to the book shouldn't need saving
    EXPECT_FALSE(qof_book_session_not_saved (s.get_book ()));
    // I'll put load_error back just to be tidy.
    load_error = true;
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionTest, save)
{
    qof_backend_register_provider (get_provider ());
    QofSession s(qof_book_new());
    s.begin ("book1", SESSION_NORMAL_OPEN);
    load_error = false;
    s.load (nullptr);
    qof_book_mark_session_dirty (s.get_book ());
    s.save (nullptr);
    EXPECT_EQ (sync_called, true);
    qof_backend_unregister_all_providers ();
    sync_called = false;
    load_error = true;
}

TEST (QofSessionTest, safe_save)
{
    qof_backend_register_provider (get_provider ());
    QofSession s(qof_book_new());
    s.begin ("book1", SESSION_NORMAL_OPEN);
    s.safe_save (nullptr);
    EXPECT_EQ (safe_sync_called, true);
    qof_backend_unregister_all_providers ();
    safe_sync_called = false;
}

TEST (QofSessionTest, export_session)
{
    qof_backend_register_provider (get_provider ());
    auto b1 = qof_book_new();
    QofSession s1(b1);
    s1.begin ("book1", SESSION_NORMAL_OPEN);
    qof_book_set_backend(b1, s1.get_backend());
    auto b2 = qof_book_new();
    QofSession s2(b2);
    s2.begin ("book2", SESSION_NORMAL_OPEN);
    qof_book_set_backend(b2, s2.get_backend());
    s2.export_session (s1, nullptr);
    EXPECT_EQ (exported_book, b1);

    qof_backend_unregister_all_providers ();
}

TEST (QofSessionOperationLeaseTest, is_exclusive_and_session_bound)
{
    auto session_1 = qof_session_new (qof_book_new ());
    auto session_2 = qof_session_new (qof_book_new ());
    auto book_1 = qof_session_get_book (session_1);
    auto book_2 = qof_session_get_book (session_2);
    auto lease = qof_session_operation_lease_acquire (session_1);

    ASSERT_NE (lease, nullptr);
    auto operation_id = qof_session_operation_lease_get_id (lease);
    EXPECT_NE (operation_id, 0);
    EXPECT_TRUE (qof_session_operation_lease_is_valid (lease, session_1));
    EXPECT_FALSE (qof_session_operation_lease_is_valid (lease, session_2));
    EXPECT_EQ (qof_session_operation_lease_acquire (session_1), nullptr);
    EXPECT_FALSE (qof_session_save_with_lease (session_2, lease, nullptr));

    qof_session_swap_data (session_1, session_2);
    EXPECT_EQ (qof_session_get_book (session_1), book_1);
    EXPECT_EQ (qof_session_get_book (session_2), book_2);
    EXPECT_TRUE (qof_session_operation_lease_is_valid (lease, session_1));

    qof_session_operation_lease_release (lease);
    EXPECT_FALSE (qof_session_has_active_operation_lease (session_1));

    auto next_lease = qof_session_operation_lease_acquire (session_1);
    ASSERT_NE (next_lease, nullptr);
    EXPECT_NE (qof_session_operation_lease_get_id (next_lease), operation_id);
    qof_session_operation_lease_release (next_lease);

    qof_session_swap_data (session_1, session_2);
    EXPECT_EQ (qof_session_get_book (session_1), book_2);
    EXPECT_EQ (qof_session_get_book (session_2), book_1);

    qof_session_destroy (session_1);
    qof_session_destroy (session_2);
}

TEST (QofSessionOperationLeaseTest, exposes_exclusive_operation_kind)
{
    auto session = qof_session_new (qof_book_new ());
    auto lease = qof_session_operation_lease_acquire_for (
        session, QOF_SESSION_OPERATION_SAVE_AS);

    ASSERT_NE (lease, nullptr);
    EXPECT_EQ (qof_session_operation_lease_get_kind (lease),
               QOF_SESSION_OPERATION_SAVE_AS);
    EXPECT_TRUE (qof_session_has_active_operation_kind (
        session, QOF_SESSION_OPERATION_SAVE_AS));
    EXPECT_FALSE (qof_session_has_active_operation_kind (
        session, QOF_SESSION_OPERATION_SAVE));
    EXPECT_EQ (qof_session_operation_lease_acquire_for (
        session, QOF_SESSION_OPERATION_EXPORT), nullptr);

    qof_session_operation_lease_release (lease);
    EXPECT_FALSE (qof_session_has_active_operation_kind (
        session, QOF_SESSION_OPERATION_SAVE_AS));
    qof_session_destroy (session);
}

TEST (QofSessionOperationLeaseTest, clears_current_session_only_for_owner)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    qof_backend_register_provider (get_provider ());
    auto current = qof_session_new (qof_book_new ());
    auto foreign = qof_session_new (qof_book_new ());
    qof_session_begin (current, "book1", SESSION_NORMAL_OPEN);
    qof_book_set_backend (qof_session_get_book (current),
                          qof_session_get_backend (current));
    gnc_set_current_session (current);
    auto generation = gnc_current_session_get_generation ();
    auto current_lease = qof_session_operation_lease_acquire_for (
        current, QOF_SESSION_OPERATION_CLOSE);
    auto foreign_lease = qof_session_operation_lease_acquire_for (
        foreign, QOF_SESSION_OPERATION_CLOSE);

    ASSERT_NE (current_lease, nullptr);
    ASSERT_NE (foreign_lease, nullptr);
    EXPECT_FALSE (gnc_clear_current_session_with_lease (foreign_lease));
    EXPECT_TRUE (gnc_current_session_exist ());
    EXPECT_EQ (gnc_get_current_session (), current);
    EXPECT_EQ (gnc_current_session_get_generation (), generation);

    observe_current_session_during_end = true;
    current_session_was_detached_on_end = false;
    EXPECT_TRUE (gnc_clear_current_session_with_lease (current_lease));
    observe_current_session_during_end = false;
    EXPECT_TRUE (current_session_was_detached_on_end);
    EXPECT_FALSE (gnc_current_session_exist ());
    EXPECT_NE (gnc_current_session_get_generation (), generation);
    EXPECT_EQ (qof_session_operation_lease_get_id (current_lease), 0);

    qof_session_operation_lease_release (current_lease);
    qof_session_operation_lease_release (foreign_lease);
    qof_session_destroy (foreign);
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionOperationLeaseTest, protects_session_operations)
{
    qof_backend_register_provider (get_provider ());
    auto session_1 = qof_session_new (qof_book_new ());
    auto session_2 = qof_session_new (qof_book_new ());
    auto lease_1 = qof_session_operation_lease_acquire (session_1);
    auto lease_2 = qof_session_operation_lease_acquire (session_2);
    ASSERT_NE (lease_1, nullptr);
    ASSERT_NE (lease_2, nullptr);

    EXPECT_FALSE (qof_session_begin_with_lease (
        session_1, lease_2, "book1", SESSION_NORMAL_OPEN));
    EXPECT_TRUE (qof_session_operation_lease_is_valid (lease_1, session_1));

    qof_session_begin (session_1, "book1", SESSION_NORMAL_OPEN);
    EXPECT_STREQ (qof_session_get_url (session_1), "");
    EXPECT_TRUE (qof_session_begin_with_lease (
        session_1, lease_1, "book1", SESSION_NORMAL_OPEN));
    EXPECT_STREQ (qof_session_get_url (session_1), "book1");

    load_error = false;
    ASSERT_TRUE (qof_session_load_with_lease (session_1, lease_1, nullptr));
    ASSERT_TRUE (qof_session_operation_lease_is_valid (lease_1, session_1));

    sync_called = false;
    qof_book_mark_session_dirty (qof_session_get_book (session_1));
    qof_session_save (session_1, nullptr);
    EXPECT_FALSE (sync_called);
    EXPECT_TRUE (qof_session_save_with_lease (session_1, lease_1, nullptr));
    EXPECT_TRUE (sync_called);

    safe_sync_called = false;
    qof_session_safe_save (session_1, nullptr);
    EXPECT_FALSE (safe_sync_called);
    EXPECT_TRUE (qof_session_safe_save_with_lease (
        session_1, lease_1, nullptr));
    EXPECT_TRUE (safe_sync_called);

    qof_session_end (session_1);
    EXPECT_STREQ (qof_session_get_url (session_1), "book1");
    EXPECT_TRUE (qof_session_end_with_lease (session_1, lease_1));
    EXPECT_STREQ (qof_session_get_url (session_1), "");

    EXPECT_FALSE (qof_session_destroy_with_lease (session_1, lease_2));
    EXPECT_TRUE (qof_session_operation_lease_is_valid (lease_1, session_1));
    EXPECT_TRUE (qof_session_destroy_with_lease (session_1, lease_1));
    EXPECT_EQ (qof_session_operation_lease_get_id (lease_1), 0);
    qof_session_operation_lease_release (lease_1);

    qof_session_operation_lease_release (lease_2);
    qof_session_destroy (session_2);
    qof_backend_unregister_all_providers ();
    sync_called = false;
    safe_sync_called = false;
    load_error = true;
}

TEST (QofSessionOperationLeaseTest, current_lease_blocks_set_and_legacy_clear)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto session_1 = qof_session_new (qof_book_new ());
    auto session_2 = qof_session_new (qof_book_new ());
    gnc_set_current_session (session_1);
    auto generation = gnc_current_session_get_generation ();
    auto lease = qof_session_operation_lease_acquire (session_1);
    ASSERT_NE (lease, nullptr);

    gnc_set_current_session (session_1);
    EXPECT_EQ (gnc_current_session_get_generation (), generation);
    EXPECT_EQ (gnc_get_current_session (), session_1);

    gnc_set_current_session (session_2);
    gnc_clear_current_session ();
    EXPECT_EQ (gnc_current_session_get_generation (), generation);
    EXPECT_EQ (gnc_get_current_session (), session_1);
    EXPECT_TRUE (qof_session_operation_lease_is_valid (lease, session_1));

    qof_session_operation_lease_release (lease);
    gnc_set_current_session (session_2);
    EXPECT_NE (gnc_current_session_get_generation (), generation);
    EXPECT_EQ (gnc_get_current_session (), session_2);
    gnc_clear_current_session ();
    EXPECT_FALSE (gnc_current_session_exist ());
    qof_session_destroy (session_1);
}

TEST (QofSessionOperationLeaseTest, leased_replacement_cannot_become_current)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto current = qof_session_new (qof_book_new ());
    auto replacement = qof_session_new (qof_book_new ());
    gnc_set_current_session (current);
    auto generation = gnc_current_session_get_generation ();
    auto replacement_lease = qof_session_operation_lease_acquire (
        replacement);
    ASSERT_NE (replacement_lease, nullptr);

    gnc_set_current_session (replacement);
    EXPECT_EQ (gnc_current_session_get_generation (), generation);
    EXPECT_EQ (gnc_get_current_session (), current);
    EXPECT_TRUE (qof_session_operation_lease_is_valid (
        replacement_lease, replacement));

    qof_session_operation_lease_release (replacement_lease);
    gnc_set_current_session (replacement);
    EXPECT_NE (gnc_current_session_get_generation (), generation);
    EXPECT_EQ (gnc_get_current_session (), replacement);
    gnc_clear_current_session ();
    EXPECT_FALSE (gnc_current_session_exist ());
    qof_session_destroy (current);
}

TEST (QofSessionOperationLeaseTest, swap_requires_both_leases_and_invalidates_them)
{
    auto session_1 = qof_session_new (qof_book_new ());
    auto session_2 = qof_session_new (qof_book_new ());
    auto book_1 = qof_session_get_book (session_1);
    auto book_2 = qof_session_get_book (session_2);
    auto lease_1 = qof_session_operation_lease_acquire (session_1);
    auto lease_2 = qof_session_operation_lease_acquire (session_2);
    ASSERT_NE (lease_1, nullptr);
    ASSERT_NE (lease_2, nullptr);

    EXPECT_FALSE (qof_session_swap_data_with_leases (
        session_1, lease_2, session_2, lease_1));
    EXPECT_TRUE (qof_session_operation_lease_is_valid (lease_1, session_1));
    EXPECT_TRUE (qof_session_operation_lease_is_valid (lease_2, session_2));
    EXPECT_EQ (qof_session_get_book (session_1), book_1);
    EXPECT_EQ (qof_session_get_book (session_2), book_2);

    EXPECT_TRUE (qof_session_swap_data_with_leases (
        session_1, lease_1, session_2, lease_2));
    EXPECT_EQ (qof_session_get_book (session_1), book_2);
    EXPECT_EQ (qof_session_get_book (session_2), book_1);
    EXPECT_FALSE (qof_session_operation_lease_is_valid (lease_1, session_1));
    EXPECT_FALSE (qof_session_operation_lease_is_valid (lease_2, session_2));

    qof_session_operation_lease_release (lease_1);
    qof_session_operation_lease_release (lease_2);
    qof_session_destroy (session_1);
    qof_session_destroy (session_2);
}

TEST (QofSessionOperationLeaseTest, book_replacement_invalidates_lease)
{
    qof_backend_register_provider (get_provider ());
    auto session = qof_session_new (qof_book_new ());
    auto lease = qof_session_operation_lease_acquire (session);
    ASSERT_NE (lease, nullptr);
    ASSERT_TRUE (qof_session_begin_with_lease (
        session, lease, "book1", SESSION_NORMAL_OPEN));

    load_error = true;
    EXPECT_TRUE (qof_session_load_with_lease (session, lease, nullptr));
    EXPECT_FALSE (qof_session_operation_lease_is_valid (lease, session));
    EXPECT_EQ (qof_session_operation_lease_get_id (lease), 0);

    qof_session_operation_lease_release (lease);
    qof_session_destroy (session);
    qof_backend_unregister_all_providers ();
}

TEST (QofSessionOperationLeaseTest, scrub_context_is_current_book_bound)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto current = qof_session_new (qof_book_new ());
    auto foreign = qof_session_new (qof_book_new ());
    auto current_book = qof_session_get_book (current);
    auto foreign_book = qof_session_get_book (foreign);
    gnc_set_current_session (current);
    auto generation = gnc_current_session_get_generation ();

    EXPECT_EQ (gnc_scrub_context_begin (foreign_book), nullptr);
    auto context = gnc_scrub_context_begin (current_book);
    ASSERT_NE (context, nullptr);
    EXPECT_TRUE (gnc_scrub_context_is_active (context));
    EXPECT_TRUE (gnc_scrub_context_owns_book (context, current_book));
    EXPECT_FALSE (gnc_scrub_context_owns_book (context, foreign_book));
    EXPECT_TRUE (qof_session_has_active_operation_kind (
        current, QOF_SESSION_OPERATION_SCRUB));
    EXPECT_EQ (qof_session_operation_lease_acquire_for (
        current, QOF_SESSION_OPERATION_SAVE), nullptr);
    EXPECT_FALSE (gnc_scrub_legacy_operation_allowed (
        current_book, "test scrub"));
    EXPECT_TRUE (gnc_scrub_legacy_operation_allowed (
        foreign_book, "test scrub"));

    gnc_scrub_context_cancel (context);
    EXPECT_TRUE (gnc_scrub_context_is_cancelled (context));
    gnc_set_current_session (foreign);
    EXPECT_EQ (gnc_get_current_session (), current);
    EXPECT_EQ (gnc_current_session_get_generation (), generation);
    EXPECT_TRUE (gnc_scrub_context_is_active (context));

    gnc_scrub_context_end (context);
    EXPECT_FALSE (gnc_scrub_context_is_active (context));
    EXPECT_FALSE (qof_session_has_active_operation_kind (
        current, QOF_SESSION_OPERATION_SCRUB));
    EXPECT_TRUE (gnc_scrub_legacy_operation_allowed (
        current_book, "test scrub"));
    gnc_scrub_context_unref (context);

    gnc_set_current_session (foreign);
    EXPECT_EQ (gnc_get_current_session (), foreign);
    gnc_clear_current_session ();
    qof_session_destroy (current);
}

TEST (QofSessionOperationLeaseTest, scrub_checks_do_not_create_current_session)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto generation = gnc_current_session_get_generation ();
    auto book = qof_book_new ();
    ASSERT_FALSE (gnc_current_session_exist ());

    EXPECT_EQ (gnc_scrub_context_begin (book), nullptr);
    EXPECT_FALSE (gnc_current_session_exist ());
    EXPECT_EQ (gnc_current_session_get_generation (), generation);
    EXPECT_TRUE (gnc_scrub_legacy_operation_allowed (
        book, "headless scrub"));
    EXPECT_FALSE (gnc_current_session_exist ());
    EXPECT_EQ (gnc_current_session_get_generation (), generation);

    qof_book_destroy (book);
}

TEST (QofSessionOperationLeaseTest, scrub_context_releases_exactly_once)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto session = qof_session_new (qof_book_new ());
    gnc_set_current_session (session);
    auto context = gnc_scrub_context_begin (qof_session_get_book (session));
    ASSERT_NE (context, nullptr);
    auto retained = gnc_scrub_context_ref (context);

    gnc_scrub_context_end (context);
    gnc_scrub_context_end (context);
    EXPECT_FALSE (gnc_scrub_context_is_active (context));
    EXPECT_FALSE (qof_session_has_active_operation_lease (session));
    gnc_scrub_context_unref (context);

    auto next_lease = qof_session_operation_lease_acquire_for (
        session, QOF_SESSION_OPERATION_SAVE);
    ASSERT_NE (next_lease, nullptr);
    qof_session_operation_lease_release (next_lease);
    gnc_clear_current_session ();
    auto generation = gnc_current_session_get_generation ();
    ASSERT_FALSE (gnc_current_session_exist ());

    EXPECT_FALSE (gnc_scrub_context_is_active (retained));
    EXPECT_FALSE (gnc_current_session_exist ());
    EXPECT_EQ (gnc_current_session_get_generation (), generation);
    gnc_scrub_context_unref (retained);
}
