/*
 * gnc-session.c -- GnuCash's session handling
 *
 * Copyright (C) 2006 Chris Shoemaker <c.shoemaker@cox.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652
 * Boston, MA  02110-1301,  USA       gnu@gnu.org
 */

#include <config.h>
#include "qof.h"
#include "gnc-session.h"
#include "gnc-engine.h"
#include "TransLog.h"

static QofSession * current_session = NULL;
static guint64 current_session_generation = 1;
static QofLogModule log_module = GNC_MOD_ENGINE;

static void
advance_current_session_generation (void)
{
    current_session_generation++;
    if (current_session_generation == 0)
        current_session_generation = 1;
}

QofSession *
gnc_get_current_session (void)
{
    if (!current_session)
    {
        QofBook* book = qof_book_new ();
        qof_event_suspend();
        current_session = qof_session_new (book);
        advance_current_session_generation ();
        qof_event_resume();
    }

    return current_session;
}

gboolean
gnc_current_session_exist(void)
{
    return (current_session != NULL);
}

guint64
gnc_current_session_get_generation(void)
{
    return current_session_generation;
}

void
gnc_set_current_session (QofSession *session)
{
    if (current_session == session)
        return;

    if (current_session &&
        qof_session_has_active_operation_lease (current_session))
    {
        PWARN ("Refusing to replace the current session while it is leased");
        return;
    }
    if (session && qof_session_has_active_operation_lease (session))
    {
        PWARN ("Refusing to install a leased session as current");
        return;
    }

    if (current_session)
        PINFO("Leak of current session.");
    current_session = session;
    advance_current_session_generation ();
}

void gnc_clear_current_session()
{
    if (current_session)
    {
        if (qof_session_has_active_operation_lease (current_session))
        {
            PWARN ("Refusing to clear a leased current session without its token");
            return;
        }

        QofSession *session = current_session;
        current_session = NULL;
        advance_current_session_generation ();
        xaccLogDisable();
        qof_session_destroy(session);
        xaccLogEnable();
    }
}

gboolean
gnc_clear_current_session_with_lease (QofSessionOperationLease *lease)
{
    QofSession *session;

    if (!current_session ||
        !qof_session_operation_lease_is_valid (lease, current_session))
        return FALSE;

    session = current_session;
    current_session = NULL;
    xaccLogDisable ();
    if (!qof_session_destroy_with_lease (session, lease))
    {
        current_session = session;
        xaccLogEnable ();
        return FALSE;
    }
    advance_current_session_generation ();
    xaccLogEnable ();
    return TRUE;
}
