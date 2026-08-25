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
#include <qof-backend.hpp>
#include <cstdlib>
#include "../gnc-backend-prov.hpp"
#include "../Account.h"

static QofBook * exported_book {nullptr};
static bool safe_sync_called {false};
static bool sync_called {false};
static bool load_error {true};
static bool data_loaded {false};

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
    void session_end() {}
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

TEST (QofSessionOperationLeaseTest, current_session_change_invalidates_lease)
{
    if (gnc_current_session_exist ())
        gnc_clear_current_session ();

    auto session_1 = qof_session_new (qof_book_new ());
    auto session_2 = qof_session_new (qof_book_new ());
    gnc_set_current_session (session_1);
    auto generation = gnc_current_session_get_generation ();
    auto lease = qof_session_operation_lease_acquire (session_1);
    ASSERT_NE (lease, nullptr);

    gnc_set_current_session (session_2);
    EXPECT_NE (gnc_current_session_get_generation (), generation);
    EXPECT_FALSE (qof_session_operation_lease_is_valid (lease, session_1));
    EXPECT_FALSE (qof_session_has_active_operation_lease (session_1));
    EXPECT_EQ (qof_session_operation_lease_get_id (lease), 0);

    qof_session_operation_lease_release (lease);
    qof_session_destroy (session_1);
    gnc_clear_current_session ();
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
