/*
 * tklock-grab.c
 *
 * Copyright (C) 2021 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <gtk/gtk.h>
#include <systemui.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>

gboolean
tklock_grab_try(GdkWindow *window, GdkEventMask event_mask,
                GdkWindow *confine_to)
{
  GdkGrabStatus status;

  status = gdk_pointer_grab(window, event_mask ? FALSE : TRUE, event_mask,
                            confine_to, NULL, GDK_CURRENT_TIME);

  if (status != GDK_GRAB_SUCCESS)
    return FALSE;

  status = gdk_keyboard_grab(window, TRUE, GDK_CURRENT_TIME);

  return status == GDK_GRAB_SUCCESS;
}

void
tklock_grab_release()
{
  SYSTEMUI_DEBUG_FN;

  gdk_pointer_ungrab(GDK_CURRENT_TIME);
  gdk_keyboard_ungrab(GDK_CURRENT_TIME);
}

void
tklock_unlock(DBusConnection *conn)
{
  DBusMessage *mcall;

  SYSTEMUI_DEBUG_FN;

  mcall = dbus_message_new_method_call(MCE_SERVICE, MCE_REQUEST_PATH,
                                       MCE_REQUEST_IF, MCE_DISPLAY_ON_REQ);
  if (mcall)
  {
    dbus_message_set_no_reply(mcall, TRUE);
    dbus_connection_send(conn, mcall, NULL);
    dbus_connection_flush(conn);
    dbus_message_unref(mcall);
  }

  mcall = dbus_message_new_method_call(MCE_SERVICE, MCE_REQUEST_PATH,
                                       MCE_REQUEST_IF,
                                       MCE_TKLOCK_MODE_CHANGE_REQ);
  if (mcall)
  {
    const char *unlocked = MCE_TK_UNLOCKED;

    dbus_message_append_args(mcall,
                             DBUS_TYPE_STRING, &unlocked,
                             DBUS_TYPE_INVALID);
    dbus_message_set_no_reply(mcall, TRUE);
    dbus_connection_send(conn, mcall, NULL);
    dbus_connection_flush(conn);
    dbus_message_unref(mcall);
  }
}
