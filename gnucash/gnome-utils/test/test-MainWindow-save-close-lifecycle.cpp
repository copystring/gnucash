#include <config.h>
#include <array>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"
#include "Account.h"
#include "cashobjects.h"
#include "gnc-backend-prov.hpp"
#include "gnc-component-manager.h"
#include "gnc-commodity.h"
#include "gnc-engine.h"
#include "gnc-file.h"
#include "gnc-gsettings.h"
#include "gnc-main-window.h"
#include "gnc-session.h"
#include "gnc-test-shutdown.h"
#include "qof-backend.hpp"
#include "qofbackend.h"
#include "qofsession.h"

namespace
{
constexpr std::array<const char *, 5> filters{
    "MainWindowSaveCloseLifecycle.SaveSuccess",
    "MainWindowSaveCloseLifecycle.CleanClose",
    "MainWindowSaveCloseLifecycle.CancelClose",
    "MainWindowSaveCloseLifecycle.SaveError",
    "MainWindowSaveCloseLifecycle.ReentrantClose"};

void require(bool value)
{
    if (!value)
        std::_Exit(EXIT_FAILURE);
}

struct Observation
{
    unsigned writes{};
    unsigned shutdowns{};
    unsigned reentrant_requests{};
    unsigned destroys{};
    unsigned finalizes{};
    unsigned applies{};
    unsigned cancels{};
    unsigned errors{};
    unsigned releases{};
    int status{-1};
    bool fail_write{};
    bool reenter{};
    bool inside_iteration{};
    bool dispatch_returned{};
    bool shutdown_save_active{};
    GncMainWindow *window{};
};

Observation *observation{};

void request_close(GncMainWindow *window)
{
    gboolean handled = FALSE;
#if GTK_CHECK_VERSION(4, 0, 0)
    g_signal_emit_by_name(window, "close-request", &handled);
#else
    GdkEvent *event = gdk_event_new(GDK_DELETE);
    g_signal_emit_by_name(window, "delete-event", event, &handled);
    gdk_event_free(event);
#endif
    require(handled);
}

class WriteBackend final : public QofBackend
{
public:
    void session_begin(QofSession *, const char *, SessionOpenMode) override {}
    void session_end() override {}
    void load(QofBook *, QofBackendLoadType) override {}
    void sync(QofBook *book) override
    {
        auto &o = *observation;
        ++o.writes;
        EXPECT_EQ(qof_session_get_book(gnc_get_current_session()), book);
        EXPECT_TRUE(gnc_file_save_in_progress());
        EXPECT_EQ(o.shutdowns, 0u);
        if (o.reenter)
        {
            o.reenter = false;
            ++o.reentrant_requests;
            request_close(o.window);
            EXPECT_EQ(o.writes, 1u);
            EXPECT_EQ(o.shutdowns, 0u);
        }
        if (o.fail_write)
            set_error(ERR_FILEIO_WRITE_ERROR);
        else
            qof_book_mark_session_saved(book);
    }
    void safe_sync(QofBook *book) override { sync(book); }
};

class WriteProvider final : public QofBackendProvider
{
public:
    WriteProvider() : QofBackendProvider("Lifecycle injected writer", "lifecycle") {}
    QofBackend *create_backend() override { return new WriteBackend; }
    bool type_check(const char *) override { return true; }
};

bool one_case()
{
    if (::testing::GTEST_FLAG(repeat) != 1 || ::testing::GTEST_FLAG(list_tests))
        return false;
    for (auto filter : filters)
        if (::testing::GTEST_FLAG(filter) == filter)
            return true;
    return false;
}

class MainWindowSaveCloseLifecycle : public ::testing::Test
{
protected:
    // Weak-notify, destroy signal and shutdown hook may outlive the fixture,
    // including on assertion/teardown paths. The process exits after one case;
    // deliberately retain this callback data until process exit.
    Observation &observed{*new Observation{}};
    QofSession *session{};
    QofSession *previous{};
    QofBook *book{};
    GncMainWindow *window{};
    std::string directory;
    std::string filename;
    std::string uri;
    bool may_dispatch{};
    bool released{};
    bool xml{};

    static void shutdown(int status, gpointer data)
    {
        auto &o = *static_cast<Observation *>(data);
        require(o.inside_iteration);
        ++o.shutdowns;
        o.status = status;
        o.shutdown_save_active = gnc_file_save_in_progress();
    }

    static void destroyed(GtkWidget *, gpointer data)
    {
        ++static_cast<Observation *>(data)->destroys;
    }

    static void finalized(gpointer data, GObject *)
    {
        ++static_cast<Observation *>(data)->finalizes;
    }

    bool window_closed() const
    {
#if GTK_CHECK_VERSION(4, 0, 0)
        auto model = gtk_window_get_toplevels();
        for (guint i = 0; i < g_list_model_get_n_items(model); ++i)
        {
            auto item = g_list_model_get_item(model, i);
            bool match = item == window;
            g_object_unref(item);
            if (match) return false;
        }
        return true;
#else
        return observed.destroys != 0;
#endif
    }

#if GTK_CHECK_VERSION(4, 0, 0)
    static GtkWidget *find_button(GtkWidget *widget, const char *label)
    {
        if (GTK_IS_BUTTON(widget) &&
            g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)
            return widget;
        for (auto child = gtk_widget_get_first_child(widget); child;
             child = gtk_widget_get_next_sibling(child))
            if (auto found = find_button(child, label)) return found;
        return nullptr;
    }
#endif

    GtkWindow *dialog(bool error = false)
    {
        GtkWindow *result{};
#if GTK_CHECK_VERSION(4, 0, 0)
        auto model = gtk_window_get_toplevels();
        for (guint i = 0; i < g_list_model_get_n_items(model); ++i)
        {
            auto candidate = G_OBJECT(g_list_model_get_item(model, i));
            if (GTK_IS_WINDOW(candidate) &&
                gtk_window_get_transient_for(GTK_WINDOW(candidate)) == GTK_WINDOW(window) &&
                (error ? (find_button(GTK_WIDGET(candidate), "_Close") ||
                          find_button(GTK_WIDGET(candidate), "Close")) :
                         (find_button(GTK_WIDGET(candidate), "_Save") &&
                          find_button(GTK_WIDGET(candidate), "_Cancel"))))
            {
                require(result == nullptr);
                result = GTK_WINDOW(candidate);
            }
            g_object_unref(candidate);
        }
#else
        auto windows = gtk_window_list_toplevels();
        for (auto node = windows; node; node = node->next)
            if (GTK_IS_MESSAGE_DIALOG(node->data) &&
                gtk_window_get_transient_for(GTK_WINDOW(node->data)) == GTK_WINDOW(window))
            {
                require(result == nullptr);
                result = GTK_WINDOW(node->data);
            }
        g_list_free(windows);
#endif
        return result;
    }

    void iteration()
    {
        require(!observed.inside_iteration);
        auto calls_before = observed.shutdowns;
        observed.inside_iteration = true;
        g_main_context_iteration(nullptr, FALSE);
        observed.inside_iteration = false;
        if (calls_before == 0 && observed.shutdowns != 0)
            observed.dispatch_returned = true;
    }

    void until(const std::function<bool()> &done)
    {
        auto deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
        while (!done())
        {
            require(g_get_monotonic_time() < deadline);
            iteration();
            g_usleep(1000);
        }
    }

    void quiet()
    {
        auto end = g_get_monotonic_time() + 600 * G_TIME_SPAN_MILLISECOND;
        while (g_get_monotonic_time() < end)
        {
            iteration();
            require(observed.shutdowns == 0);
            g_usleep(1000);
        }
    }

    void answer(gint response)
    {
        auto question = dialog(response == GTK_RESPONSE_OK);
        require(question != nullptr);
#if GTK_CHECK_VERSION(4, 0, 0)
        auto button = find_button(GTK_WIDGET(question),
                                  response == GTK_RESPONSE_APPLY ? "_Save" :
                                  response == GTK_RESPONSE_OK ? "_Close" : "_Cancel");
        if (!button && response == GTK_RESPONSE_OK)
            button = find_button(GTK_WIDGET(question), "Close");
        require(button != nullptr);
#else
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        require(gtk_dialog_get_widget_for_response(GTK_DIALOG(question), response) != nullptr);
        G_GNUC_END_IGNORE_DEPRECATIONS
#endif
        g_object_ref(question);
        if (response == GTK_RESPONSE_APPLY)
            ++observed.applies;
        if (response == GTK_RESPONSE_CANCEL)
            ++observed.cancels;
#if GTK_CHECK_VERSION(4, 0, 0)
        g_signal_emit_by_name(button, "clicked");
#else
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_dialog_response(GTK_DIALOG(question), response);
        G_GNUC_END_IGNORE_DEPRECATIONS
#endif
        g_object_unref(question);
    }

    void close_dirty(gint response)
    {
        may_dispatch = true;
        request_close(window);
        until([&] { return dialog() != nullptr; });
        EXPECT_EQ(observed.shutdowns, 0u);
        answer(response);
    }

    void shutdown_return()
    {
        until([&] { return observed.shutdowns != 0; });
        require(observed.dispatch_returned);
        require(!observed.inside_iteration);
        require(!gnc_file_save_in_progress());
        EXPECT_EQ(observed.shutdowns, 1u);
        EXPECT_EQ(observed.status, 0);
        EXPECT_FALSE(observed.shutdown_save_active);
        EXPECT_TRUE(window_closed());
        EXPECT_EQ(observed.finalizes, 0u);
        EXPECT_EQ(observed.releases, 0u);
        auto end = g_get_monotonic_time() + 600 * G_TIME_SPAN_MILLISECOND;
        while (g_get_monotonic_time() < end)
        {
            iteration();
            require(observed.shutdowns == 1);
            g_usleep(1000);
        }
        require(g_main_context_acquire(g_main_context_default()));
        gnc_test_shutdown_restore();
        g_main_context_release(g_main_context_default());
    }

    void assert_still_open()
    {
        EXPECT_TRUE(qof_book_session_not_saved(book));
        EXPECT_FALSE(gnc_file_save_in_progress());
        EXPECT_EQ(observed.shutdowns, 0u);
        EXPECT_FALSE(observed.dispatch_returned);
        EXPECT_FALSE(window_closed());
        EXPECT_EQ(observed.finalizes, 0u);
        EXPECT_EQ(observed.releases, 0u);
    }

    void release_without_pumping()
    {
        require(observed.dispatch_returned);
        require(!released);
        require(!gnc_file_save_in_progress());
        // The shutdown hook does not perform application shutdown.
        if (!window_closed())
            gtk_window_destroy(GTK_WINDOW(window));
        require(window_closed() && observed.finalizes == 0);
        g_printerr("RELEASE START refs=%u\n", G_OBJECT(window)->ref_count);
        g_object_unref(window);
        g_printerr("RELEASE END finalized=%u\n", observed.finalizes);
        window = nullptr;
        released = true;
        ++observed.releases;
        EXPECT_EQ(observed.releases, 1u);
        EXPECT_EQ(observed.finalizes, 1u);
    }

    void SetUp() override
    {
        observation = &observed;
        xml = ::testing::GTEST_FLAG(filter) == "MainWindowSaveCloseLifecycle.SaveSuccess";
        if (xml)
        {
            const auto backend_dir = g_getenv("GNC_LIFECYCLE_XML_DIR");
            require(backend_dir != nullptr);
            require(qof_load_backend_library(backend_dir, "gncmod-backend-xml"));
        }
        else
            qof_backend_register_provider(std::make_unique<WriteProvider>());
        GError *error{};
        auto temporary = g_dir_make_tmp("gnc-mainwindow-lifecycle-XXXXXX", &error);
        require(error == nullptr && temporary != nullptr);
        directory = temporary;
        g_free(temporary);
        filename = directory + "/roundtrip.gnucash";
        uri = xml ? "xml://" + filename : "lifecycle://" + filename;
        session = qof_session_new(qof_book_new());
        qof_session_begin(session, uri.c_str(), SESSION_NEW_STORE);
        require(qof_session_get_error(session) == ERR_BACKEND_NO_ERR);
        previous = gnc_current_session_exist() ? gnc_get_current_session() : nullptr;
        require(!previous || !qof_session_has_active_operation_lease(previous));
        require(!qof_session_has_active_operation_lease(session));
        gnc_set_current_session(session);
        require(gnc_current_session_exist() && gnc_get_current_session() == session);
        book = qof_session_get_book(session);
        auto root = gnc_book_get_root_account(book);
        if (!root)
        {
            root = xaccMallocAccount(book);
            xaccAccountBeginEdit(root);
            xaccAccountSetType(root, ACCT_TYPE_ROOT);
            xaccAccountSetName(root, "Lifecycle Root");
            gnc_book_set_root_account(book, root);
            xaccAccountCommitEdit(root);
        }
        auto account = xaccMallocAccount(book);
        xaccAccountBeginEdit(account);
        xaccAccountSetType(account, ACCT_TYPE_BANK);
        auto commodities = gnc_commodity_table_get_table(book);
        auto currency = gnc_commodity_table_lookup(commodities, "CURRENCY", "USD");
        if (!currency)
        {
            currency = gnc_commodity_new(book, "US Dollar", "CURRENCY", "USD", "840", 100);
            currency = gnc_commodity_table_insert(commodities, currency);
        }
        xaccAccountSetCommodity(account, currency);
        xaccAccountSetName(account, "Before lifecycle APPLY");
        gnc_account_append_child(root, account);
        xaccAccountCommitEdit(account);
        if (xml)
        {
            qof_session_save(session, nullptr);
            require(qof_session_get_error(session) == ERR_BACKEND_NO_ERR);
            require(g_file_test(filename.c_str(), G_FILE_TEST_IS_REGULAR));
        }
        qof_book_mark_session_saved(book);
        window = gnc_main_window_new();
        /* gnc_main_window_new uses g_object_new, so the constructor already
         * supplies the fixture's reference in addition to GTK's toplevel ref. */
        observed.window = window;
        g_signal_connect(window, "destroy", G_CALLBACK(destroyed), &observed);
        g_object_weak_ref(G_OBJECT(window), finalized, &observed);
        require(g_main_context_acquire(g_main_context_default()));
        gnc_test_shutdown_install(shutdown, &observed);
        g_main_context_release(g_main_context_default());
    }

    void TearDown() override
    {
        if (HasFailure() || gnc_file_save_in_progress() || observed.inside_iteration)
            std::_Exit(EXIT_FAILURE);
        const bool remains_open =
            ::testing::GTEST_FLAG(filter) == "MainWindowSaveCloseLifecycle.CancelClose" ||
            ::testing::GTEST_FLAG(filter) == "MainWindowSaveCloseLifecycle.SaveError";
        if (remains_open)
        {
            require(!observed.dispatch_returned && observed.shutdowns == 0);
            require(!window_closed() && observed.finalizes == 0);
            require(g_main_context_acquire(g_main_context_default()));
            require(gnc_test_shutdown_is_installed());
            g_main_context_release(g_main_context_default());
        }
        else
        {
            require(observed.dispatch_returned);
            if (!released)
                release_without_pumping();
            require(g_main_context_acquire(g_main_context_default()));
            require(!gnc_test_shutdown_is_installed());
            g_main_context_release(g_main_context_default());
            require(gnc_current_session_exist() && gnc_get_current_session() == session);
            require(!qof_session_has_active_operation_lease(session));
            require(!previous || !qof_session_has_active_operation_lease(previous));
            gnc_set_current_session(previous);
            require(previous ? (gnc_current_session_exist() &&
                                gnc_get_current_session() == previous) :
                               !gnc_current_session_exist());
            qof_session_destroy(session);
            session = nullptr;
            if (xml)
            {
                auto dir = g_dir_open(directory.c_str(), 0, nullptr);
                require(dir != nullptr);
                while (auto name = g_dir_read_name(dir))
                    require(g_unlink((directory + "/" + name).c_str()) == 0);
                g_dir_close(dir);
                require(g_rmdir(directory.c_str()) == 0);
            }
        }
        if (HasFailure())
            std::_Exit(EXIT_FAILURE);
    }
};

TEST_F(MainWindowSaveCloseLifecycle, CleanClose)
{
    EXPECT_FALSE(qof_book_session_not_saved(book));
    may_dispatch = true;
    request_close(window);
    shutdown_return();
    EXPECT_EQ(observed.writes, 0u);
    EXPECT_EQ(observed.applies, 0u);
}

TEST_F(MainWindowSaveCloseLifecycle, CancelClose)
{
    qof_book_mark_session_dirty(book);
    close_dirty(GTK_RESPONSE_CANCEL);
    until([&] { return dialog() == nullptr; });
    quiet();
    assert_still_open();
    EXPECT_EQ(observed.cancels, 1u);
    EXPECT_EQ(observed.writes, 0u);
    request_close(window);
    until([&] { return dialog() != nullptr; });
    answer(GTK_RESPONSE_CANCEL);
    until([&] { return dialog() == nullptr; });
    quiet();
    assert_still_open();
    EXPECT_EQ(observed.cancels, 2u);
}

TEST_F(MainWindowSaveCloseLifecycle, SaveSuccess)
{
    auto root = gnc_book_get_root_account(book);
    auto account = gnc_account_lookup_by_name(root, "Before lifecycle APPLY");
    ASSERT_NE(account, nullptr);
    xaccAccountBeginEdit(account);
    xaccAccountSetName(account, "Persisted by real lifecycle APPLY");
    xaccAccountCommitEdit(account);
    qof_book_mark_session_dirty(book);
    close_dirty(GTK_RESPONSE_APPLY);
    shutdown_return();
    EXPECT_EQ(observed.applies, 1u);
    EXPECT_FALSE(qof_book_session_not_saved(book));
    ASSERT_TRUE(g_file_test(filename.c_str(), G_FILE_TEST_IS_REGULAR));
    auto verify = qof_session_new(qof_book_new());
    qof_session_begin(verify, uri.c_str(), SESSION_READ_ONLY);
    ASSERT_EQ(qof_session_get_error(verify), ERR_BACKEND_NO_ERR);
    qof_session_load(verify, nullptr);
    ASSERT_EQ(qof_session_get_error(verify), ERR_BACKEND_NO_ERR);
    auto loaded_root = gnc_book_get_root_account(qof_session_get_book(verify));
    EXPECT_NE(gnc_account_lookup_by_name(loaded_root, "Persisted by real lifecycle APPLY"), nullptr);
    EXPECT_EQ(gnc_account_lookup_by_name(loaded_root, "Before lifecycle APPLY"), nullptr);
    qof_session_destroy(verify);
}

TEST_F(MainWindowSaveCloseLifecycle, SaveError)
{
    observed.fail_write = true;
    qof_book_mark_session_dirty(book);
    close_dirty(GTK_RESPONSE_APPLY);
    // The failed write opens an informational error before the dirty-book
    // question is presented again; these are different asynchronous dialogs.
    until([&] { return observed.writes == 1 && dialog(true) != nullptr; });
    ++observed.errors;
    answer(GTK_RESPONSE_OK);
#if GTK_CHECK_VERSION(4, 0, 0)
    GtkWindow *chooser{};
    until([&] {
        auto windows = gtk_window_get_toplevels();
        for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
        {
            auto candidate = GTK_WINDOW(g_list_model_get_item(windows, i));
            if (candidate != GTK_WINDOW(window) &&
                gtk_window_get_transient_for(candidate) == GTK_WINDOW(window) &&
                g_strcmp0(gtk_window_get_title(candidate), "Save") == 0)
                chooser = candidate;
            g_object_unref(candidate);
        }
        return chooser != nullptr;
    });
    gtk_window_close(chooser);
#endif
    until([&] { return dialog() != nullptr && !gnc_file_save_in_progress(); });
    answer(GTK_RESPONSE_CANCEL);
    until([&] { return dialog() == nullptr; });
    quiet();
    assert_still_open();
    EXPECT_EQ(observed.writes, 1u);
    EXPECT_EQ(observed.applies, 1u);
    EXPECT_GE(observed.errors, 1u);
    request_close(window);
    until([&] { return dialog() != nullptr; });
    answer(GTK_RESPONSE_CANCEL);
    until([&] { return dialog() == nullptr; });
    quiet();
    assert_still_open();
    EXPECT_EQ(observed.writes, 1u);
}

TEST_F(MainWindowSaveCloseLifecycle, ReentrantClose)
{
    observed.reenter = true;
    qof_book_mark_session_dirty(book);
    close_dirty(GTK_RESPONSE_APPLY);
    shutdown_return();
    EXPECT_EQ(observed.reentrant_requests, 1u);
    EXPECT_EQ(observed.writes, 1u);
    EXPECT_EQ(observed.applies, 1u);
    EXPECT_FALSE(qof_book_session_not_saved(book));
}
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    if (!one_case())
        return EXIT_FAILURE;
    if (!gtk_init_check())
        return EXIT_FAILURE;
    gnc::test::initialize_logging();
    qof_init();
    cashobjects_register();
    gnc_component_manager_init();
    gnc_gsettings_load_backend();
    auto result = RUN_ALL_TESTS();
    auto tests = ::testing::UnitTest::GetInstance();
    if (tests->test_to_run_count() != 1 || tests->successful_test_count() != 1)
        result = EXIT_FAILURE;
    std::_Exit(result);
}
