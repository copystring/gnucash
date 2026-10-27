#include "gnc-test-shutdown.h"

namespace
{
gpointer gui_thread{};
GncTestShutdownCallback shutdown_callback{};
gpointer shutdown_user_data{};
bool in_callback{};

void
require(gboolean condition, const char *expression)
{
    if (!condition)
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__,
                                 G_STRFUNC, expression);
}

void
require_gui_context()
{
    auto thread = g_atomic_pointer_get(&gui_thread);
    require(thread != nullptr, "GUI thread bound by GncMainWindow construction");
    require(thread == g_thread_self(), "calling thread is the bound GUI thread");
    require(g_main_context_is_owner(g_main_context_default()),
            "calling thread owns the default GMainContext");
}
}

void
gnc_test_shutdown_bind_gui_thread(void)
{
    auto context = g_main_context_get_thread_default();
    require(context == nullptr || context == g_main_context_default(),
            "GncMainWindow uses the default GMainContext");
    auto thread = g_thread_ref(g_thread_self());
    if (!g_atomic_pointer_compare_and_exchange(&gui_thread, nullptr, thread))
        g_thread_unref(thread);
    require(g_atomic_pointer_get(&gui_thread) == g_thread_self(),
            "GncMainWindow construction stays on the bound GUI thread");
}

void
gnc_test_shutdown_install(GncTestShutdownCallback callback, gpointer user_data)
{
    require_gui_context();
    require(callback != nullptr, "shutdown callback is non-null");
    require(!in_callback, "shutdown callback is not executing");
    require(shutdown_callback == nullptr, "shutdown hook is not already installed");
    shutdown_user_data = user_data;
    shutdown_callback = callback;
}

void
gnc_test_shutdown_restore(void)
{
    require_gui_context();
    require(!in_callback, "shutdown callback is not executing");
    require(shutdown_callback != nullptr, "shutdown hook is installed");
    shutdown_callback = nullptr;
    shutdown_user_data = nullptr;
}

gboolean
gnc_test_shutdown_is_installed(void)
{
    require_gui_context();
    return shutdown_callback != nullptr;
}

gboolean
gnc_test_shutdown_dispatch(int exit_status)
{
    require_gui_context();
    if (shutdown_callback == nullptr)
        return FALSE;
    require(!in_callback, "shutdown callback is not reentered");
    in_callback = true;
    shutdown_callback(exit_status, shutdown_user_data);
    in_callback = false;
    return TRUE;
}
