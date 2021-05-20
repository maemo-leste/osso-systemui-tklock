/**
   @file gp-tklock.c

   @brief Maemo systemui tklock plugin gp-tklock code

   Copyright (C) 2020 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>

   This file is part of osso-systemui-tklock.

   osso-systemui-tklock is free software;
   you can redistribute it and/or modify it under the terms of the
   GNU Lesser General Public License version 2.1 as published by the
   Free Software Foundation.

   osso-systemui-tklock is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with osso-systemui-powerkeymenu.
   If not, see <http://www.gnu.org/licenses/>.
*/

#include <hildon/hildon.h>
#include <dbus/dbus.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include <syslog.h>
#include <systemui.h>
#include <systemui/tklock-dbus-names.h>

#include "gp-tklock.h"
#include "tklock-grab.h"

static guint try_grab_count = 0;

static void
gp_tklock_unlock(DBusConnection *conn)
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

static gboolean
gp_tklock_try_grab(gpointer user_data)
{
  gp_tklock_t *gp_tklock = user_data;
  gboolean rv = FALSE;

  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);

  tklock_grab_release();

  if (try_grab_count)
    gtk_window_close_other_temporaries(GTK_WINDOW(gp_tklock->window));

  if (!tklock_grab_try(gp_tklock->window->window, NULL))
  {
    if (++try_grab_count > 3)
    {
      SYSTEMUI_ERROR("GRAB FAILED, gp_tklock can't be enabled\n"
                     "request display unblank");
      gp_tklock_unlock(gp_tklock->systemui_conn);
      gp_tklock->grab_notify = 0;
      try_grab_count = 0;
      gp_tklock->grab_status = TKLOCK_GRAB_FAILED;
      gp_tklock->one_input = FALSE;
    }
    else
      rv = TRUE;
  }
  else
  {
    gp_tklock->grab_notify = 0;
    gp_tklock->grab_status = TKLOCK_GRAB_ENABLED;
    gtk_grab_add(gp_tklock->window);
  }

  return rv;
}

static gboolean
gp_tklock_map_cb(GtkWidget *widget, GdkEvent *event, gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);

  if (gdk_pointer_is_grabbed())
  {
    SYSTEMUI_ERROR("GRAB FAILED (systemui grab), gp_tklock can't be enabled\n"
                   "request display unblank");
    gp_tklock_unlock(gp_tklock->systemui_conn);
    gp_tklock->grab_status = TKLOCK_GRAB_FAILED;
    gp_tklock->one_input = FALSE;
  }
  else if (tklock_grab_try(widget->window, gp_tklock->window->window) &&
           !gp_tklock->grab_notify)
  {
    gp_tklock->grab_notify = g_timeout_add(200, gp_tklock_try_grab, gp_tklock);
  }
  else
  {
    gp_tklock->grab_status = TKLOCK_GRAB_ENABLED;
    gtk_grab_add(gp_tklock->window);
  }

  return TRUE;
}

static void
gp_tklock_remove_grab_notify(gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);

  if (gp_tklock->grab_notify)
  {
    g_source_remove(gp_tklock->grab_notify);
    gp_tklock->grab_notify = 0;
  }
}

static void
release_grabs(gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  gp_tklock_remove_grab_notify(gp_tklock);
  tklock_grab_release();
  gtk_grab_remove(gp_tklock->window);
}

static int
ee_one_input_mode_finished(gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);
  g_assert(gp_tklock->window != NULL);

  if (!gp_tklock->disabled)
  {
    gtk_widget_hide(gp_tklock->window);
    gp_tklock->disabled = TRUE;
  }

  if (!gp_tklock->one_input_mode_finished_handler)
    SYSTEMUI_WARNING("one_input_mode_finished_handler wasn't registered, nop");
  else
    gp_tklock->one_input_mode_finished_handler();

  release_grabs(gp_tklock);

  return 0;
}

static gboolean
gp_tklock_button_release_event_cb(GtkWidget *widget, GdkEvent *event,
                                  gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);

  if (gp_tklock->one_input)
  {
    if (event->type == GDK_BUTTON_RELEASE)
    {
      ee_one_input_mode_finished(gp_tklock);
      gp_tklock->one_input_status = TKLOCK_ONE_INPUT_BUTTON_RELEASED;
    }
    else if (event->type == GDK_BUTTON_PRESS)
      gp_tklock->one_input_status = TKLOCK_ONE_INPUT_BUTTON_PRESSED;
  }

  return TRUE;
}

static gboolean
gp_tklock_key_press_event_cb(GtkWidget *widget, GdkEventKey *event,
                             gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);
  g_assert(gp_tklock->systemui_conn != NULL);

  if (gp_tklock->one_input)
    ee_one_input_mode_finished(gp_tklock);
  else if (event->type == GDK_KEY_PRESS && event->keyval != GDK_Execute)
  {
    dbus_uint32_t hw_key = event->hardware_keycode;

    if (hw_key == 73 ||  /* FK07 */
        hw_key == 74 ||  /* FK08 */
        hw_key == 121 || /* XF86AudioMute */
        hw_key == 122 || /* XF86AudioLowerVolume */
        hw_key == 123 || /* XF86AudioRaiseVolume */
        hw_key == 171 || /* XF86AudioNext */
        hw_key == 172 || /* XF86AudioPlay, XF86AudioPause */
        hw_key == 173 || /* XF86AudioPrev */
        hw_key == 174 || /* XF86AudioStop, XF86Eject */
        hw_key == 208 || /* XF86AudioPlay */
        hw_key == 209)   /* XF86AudioPause */
    {
      dbus_uint32_t keyval = event->keyval;
      DBusMessage *message = dbus_message_new_signal(TKLOCK_SIGNAL_PATH,
                                                     TKLOCK_SIGNAL_IF,
                                                     TKLOCK_MM_KEY_PRESS_SIG);

      if (message)
      {
        if (dbus_message_append_args(message,
                                     DBUS_TYPE_UINT32, &hw_key,
                                     DBUS_TYPE_UINT32, &keyval,
                                     DBUS_TYPE_INVALID))
        {
          dbus_connection_send(gp_tklock->systemui_conn, message, NULL);
        }

        dbus_message_unref(message);
      }
    }
  }

  return TRUE;
}

void
gp_tklock_create_window(gp_tklock_t *gp_tklock)
{
  GtkWidget *window;
  GdkGeometry geo;

  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gp_tklock->window = window;

  gtk_window_set_title(GTK_WINDOW(window), "gp_tklock");
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);

  geo.max_height = 15;
  geo.min_width = 15;
  geo.min_height = 15;
  geo.max_width = 15;

  gtk_window_set_geometry_hints(GTK_WINDOW(window), window, &geo,
                                GDK_HINT_MAX_SIZE | GDK_HINT_MIN_SIZE);
  g_signal_connect_after(window, "map-event", G_CALLBACK(gp_tklock_map_cb),
                         gp_tklock);
  gtk_widget_realize(window);
  gdk_window_set_events(window->window,
                        GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK);
  g_signal_connect(window, "key-press-event",
                   G_CALLBACK(gp_tklock_key_press_event_cb), gp_tklock);

  gp_tklock->btn_press_id =
      g_signal_connect(window, "button-press-event",
         G_CALLBACK(gp_tklock_button_release_event_cb), gp_tklock);
  gp_tklock->btn_release_id =
      g_signal_connect(window, "button-release-event",
                       G_CALLBACK(gp_tklock_button_release_event_cb),
                       gp_tklock);

  gdk_window_set_override_redirect(window->window, TRUE);
}

void
gp_tklock_enable_lock(gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);

  if (gp_tklock->disabled)
  {
    gtk_widget_show(gp_tklock->window);
    gtk_window_move((GtkWindow *)gp_tklock->window, -15, -15);
    gp_tklock->disabled = FALSE;
  }
}

gp_tklock_t *
gp_tklock_init(DBusConnection *conn)
{
  gp_tklock_t *gp_tklock = g_slice_new0(gp_tklock_t);

  SYSTEMUI_DEBUG_FN;

  if (gp_tklock)
  {
    g_assert(conn != NULL);

    gp_tklock->systemui_conn = conn;
    gp_tklock_create_window(gp_tklock);
    gp_tklock->grab_status = TKLOCK_GRAB_DISABLED;
    gp_tklock->grab_notify = 0;
    gp_tklock->disabled = TRUE;
  }
  else
    SYSTEMUI_ERROR("failed to allocate memory");

  return gp_tklock;
}

void
gp_tklock_disable_lock(gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(gp_tklock != NULL);

  gp_tklock_remove_grab_notify(gp_tklock);

  if (gp_tklock->grab_status == TKLOCK_GRAB_ENABLED)
  {
    release_grabs(gp_tklock);
    gp_tklock->grab_status = TKLOCK_GRAB_DISABLED;
  }

  if (!gp_tklock->disabled)
  {
    gtk_widget_hide(gp_tklock->window);
    gp_tklock->disabled = TRUE;
  }

  gdk_error_trap_push();
  gdk_window_invalidate_rect(GDK_ROOT_PARENT(), NULL, TRUE);
  gdk_flush();
  gdk_error_trap_pop();
}

void
gp_tklock_destroy_lock(gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  if (!gp_tklock)
    return;

  gp_tklock_disable_lock(gp_tklock);

  gtk_widget_unrealize(gp_tklock->window);
  gtk_widget_destroy(gp_tklock->window);
  gp_tklock->window = NULL;
}

void
gp_tklock_destroy(gp_tklock_t *gp_tklock)
{
  SYSTEMUI_DEBUG_FN;

  if (!gp_tklock)
    return;

  gp_tklock_destroy_lock(gp_tklock);

  g_assert(gp_tklock->grab_notify == 0);
  g_slice_free(gp_tklock_t, gp_tklock);
}

void
gp_tklock_set_one_input_mode_handler(gp_tklock_t *gp_tklock, void (*handler)())
{
  g_assert(gp_tklock != NULL);

  gp_tklock->one_input_mode_finished_handler = handler;
}
