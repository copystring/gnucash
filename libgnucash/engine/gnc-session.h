/********************************************************************\
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
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/


#include "qof.h"

#ifdef __cplusplus
extern "C" {
#endif

QofSession * gnc_get_current_session (void);
void gnc_clear_current_session(void);
void gnc_set_current_session (QofSession *session);
gboolean gnc_current_session_exist(void);

/**
 * Return the generation of the process-wide current-session slot.
 *
 * The generation changes whenever the slot changes identity. Session
 * operation leases capture it so that a continuation from an earlier current
 * session cannot mutate a later one.
 */
guint64 gnc_current_session_get_generation(void);

#ifdef __cplusplus
}
#endif
