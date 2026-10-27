#ifndef GNC_TEST_SHUTDOWN_H
#define GNC_TEST_SHUTDOWN_H

#if !defined(GNC_ENABLE_SHUTDOWN_TEST_HOOKS) || !defined(__linux__)
#error "Private shutdown instrumentation requires an enabled Linux test build"
#endif

#include <glib.h>

G_BEGIN_DECLS

typedef void (*GncTestShutdownCallback)(int exit_status, gpointer user_data);

__attribute__((visibility("default")))
void gnc_test_shutdown_install(GncTestShutdownCallback callback, gpointer user_data);

__attribute__((visibility("default")))
void gnc_test_shutdown_restore(void);

__attribute__((visibility("default")))
gboolean gnc_test_shutdown_is_installed(void);

__attribute__((visibility("hidden")))
void gnc_test_shutdown_bind_gui_thread(void);

__attribute__((visibility("hidden")))
gboolean gnc_test_shutdown_dispatch(int exit_status);

G_END_DECLS

#endif
