/**
   @file osso-systemui-tklock.c

   @brief Maemo systemui tklock plugin

   Copyright (C) 2012,2020 Ivaylo Dimitrov <freemangordon@abv.bg>

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

#include <gdk/gdkx.h>
#include <hildon/hildon.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include <systemui.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <string.h>
#include <syslog.h>

#include "osso-systemui-tklock-priv.h"
#include "tklock-grab.h"

#define DBUS_MCE_MATCH_RULE \
  "type='signal',path='/com/nokia/mce/signal'," \
  "interface='com.nokia.mce.signal'," \
  "member='display_status_ind'"

tklock_plugin_data *plugin_data = NULL;
system_ui_callback_t system_ui_callback = {};
static gboolean display_off = FALSE;
static guint destroy_locks_id = 0;
static Window ee_window = 0;

static void
ee_create_window()
{
  Display *dpy;
  GdkScreen *screen;
  XVisualInfo vinfo;
  Colormap cmap;
  XSetWindowAttributes attr;
  Atom state;
  const guint layer = 10;

  SYSTEMUI_DEBUG_FN;

  if (ee_window)
    return;

  dpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
  screen = gdk_screen_get_default();

  XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, VisualDepthMask, &vinfo);

  cmap = XCreateColormap(dpy, DefaultRootWindow(dpy), vinfo.visual, AllocNone);

  attr.colormap = cmap;
  attr.override_redirect = True;
  attr.border_pixel = BlackPixel(dpy, GDK_SCREEN_XNUMBER(screen));
  attr.background_pixel = BlackPixel(dpy, GDK_SCREEN_XNUMBER(screen));

  ee_window =  XCreateWindow(
        dpy, DefaultRootWindow(dpy), 0, 0, gdk_screen_get_width(screen),
        gdk_screen_get_height(screen), 0, 32, InputOutput, vinfo.visual,
        CWBackPixel | CWBorderPixel | CWOverrideRedirect|CWColormap, &attr);

  if (!ee_window)
  {
    SYSTEMUI_WARNING("Couldn't create Hamm window -> destroy colormap");

    if(cmap)
      XFreeColormap(dpy, cmap);

    return;
  }

  state = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  XChangeProperty(dpy, ee_window, XInternAtom(dpy, "_NET_WM_STATE", False),
                  XA_ATOM, 32, PropModeReplace, (unsigned char *)&state, 1);
  XChangeProperty(dpy, ee_window,
                  XInternAtom(dpy, "_HILDON_STACKING_LAYER", False),
                  XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&layer, 1);
  XMapWindow(dpy, ee_window);
  XFreeColormap(dpy, cmap);
}

void
ee_destroy_window()
{
  SYSTEMUI_DEBUG_FN;

  if (ee_window)
  {
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    XUnmapWindow(dpy, ee_window);
    XDestroyWindow(dpy, ee_window);
    ee_window = 0;
  }
}

static gboolean
tklock_destroy_locks_cb(gpointer user_data)
{
  SYSTEMUI_DEBUG_FN;

  destroy_locks_id = 0;

  if (display_off)
    return FALSE;

  ee_destroy_window();

  if (!plugin_data)
    return FALSE;

  if (plugin_data->gp_tklock && !plugin_data->gp_tklock->disabled)
    gp_tklock_destroy_lock(plugin_data->gp_tklock);

  if (plugin_data->vtklock)
    visual_tklock_destroy_lock(plugin_data->vtklock);

  systemui_free_callback(&plugin_data->sysui_cb);

  return FALSE;
}

static void
tklock_destroy_locks_timeout_remove()
{
  SYSTEMUI_DEBUG_FN;

  if (destroy_locks_id)
  {
    g_source_remove(destroy_locks_id);
    destroy_locks_id = 0;
  }
}

static void
vtklock_unlock_handler()
{
  SYSTEMUI_DEBUG_FN;

  systemui_do_callback(plugin_data->data, &plugin_data->sysui_cb,
                       TKLOCK_UNLOCK);
}

static void
gp_tklock_unlock_handler()
{
  SYSTEMUI_DEBUG_FN;

  systemui_do_callback(plugin_data->data, &plugin_data->sysui_cb,
                       TKLOCK_UNLOCK);
  systemui_do_callback(plugin_data->data, &plugin_data->sysui_cb,
                       TKLOCK_CLOSED);
  systemui_free_callback(&plugin_data->sysui_cb);
}

static DBusHandlerResult
display_status_cb(DBusConnection *connection, DBusMessage *message, void *data)
{
  SYSTEMUI_DEBUG_FN;

  if (dbus_message_is_signal(message, MCE_SIGNAL_IF, MCE_DISPLAY_SIG))
  {
    const char *status;

    if (dbus_message_get_args(message, NULL,
                              DBUS_TYPE_STRING, &status,
                              DBUS_TYPE_INVALID))
    {
      SYSTEMUI_DEBUG("status '%s'", status);

      if (!strcmp(status, MCE_DISPLAY_OFF_STRING))
      {
        tklock_destroy_locks_timeout_remove();
        display_off = TRUE;
      }
      else
      {
        display_off = FALSE;
        ee_destroy_window();
      }
    }
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int
tklock_open(const char *interface, const char *method, GArray *args,
            system_ui_data *data, system_ui_handler_arg *out)
{
  static tklock_mode mode = TKLOCK_NONE;
  int supported_args[3] = {'u', 'b', 'b'};
  system_ui_handler_arg* hargs = ((system_ui_handler_arg *)args->data);

  SYSTEMUI_DEBUG_FN;

  if (!check_plugin_arguments(args, supported_args, 1) &&
      !check_plugin_arguments(args, supported_args, 2) &&
      !check_plugin_arguments(args, supported_args, 3))
  {
    return DBUS_TYPE_INVALID;
  }

  SYSTEMUI_DEBUG("hargs[4].data.u32[%u]", hargs[4].data.u32);
  SYSTEMUI_DEBUG("mode [%u]", mode);

  switch (hargs[4].data.u32)
  {
    case TKLOCK_ONEINPUT:
    {
      gp_tklock_t *gp_tklock = plugin_data->gp_tklock;

      if (gp_tklock)
      {
        if (!gp_tklock->window)
          gp_tklock_create_window(gp_tklock);
      }
      else
      {
        plugin_data->gp_tklock = gp_tklock_init(plugin_data->data->system_bus);
        gp_tklock = plugin_data->gp_tklock;
      }

      if (!gp_tklock->one_input_mode_finished_handler)
      {
        gp_tklock_set_one_input_mode_handler(gp_tklock,
                                             gp_tklock_unlock_handler);
      }

      gp_tklock->one_input = TRUE;
      gp_tklock->one_input_status = TKLOCK_ONE_INPUT_DISABLED;
      gp_tklock_enable_lock(gp_tklock);
      mode = TKLOCK_ONEINPUT;
      break;
    }
    case TKLOCK_ENABLE_VISUAL:
    {
      vtklock_t *vtklock = plugin_data->vtklock;

      ee_destroy_window();

      if (vtklock)
      {
        if (!vtklock->window)
          visual_tklock_create_view_whimsy(vtklock);
      }
      else
      {
        vtklock = visual_tklock_new(plugin_data->data->system_bus);
        plugin_data->vtklock = vtklock;
        visual_tklock_set_unlock_handler(vtklock, vtklock_unlock_handler);
      }

      visual_tklock_present_view(vtklock);

      if (mode == TKLOCK_ENABLE)
        gp_tklock_disable_lock(plugin_data->gp_tklock, FALSE);

      mode = TKLOCK_ENABLE_VISUAL;
      break;
    }
    case TKLOCK_ENABLE:
    {
      gp_tklock_t *gp_tklock;

      if (mode == TKLOCK_ONEINPUT)
      {
        systemui_do_callback(plugin_data->data, &plugin_data->sysui_cb,
                             TKLOCK_CLOSED);
      }
      else if (mode == TKLOCK_ENABLE_VISUAL)
      {
        vtklock_t *vtklock = plugin_data->vtklock;

        if (vtklock && vtklock->window)
          visual_tklock_destroy_lock(vtklock);
      }

      ee_create_window();

      gp_tklock = plugin_data->gp_tklock;

      if (gp_tklock)
      {
        if (!gp_tklock->window)
          gp_tklock_create_window(gp_tklock);
      }
      else
        plugin_data->gp_tklock = gp_tklock_init(plugin_data->data->system_bus);

      gp_tklock = plugin_data->gp_tklock;
      gp_tklock->one_input = FALSE;
      gp_tklock_enable_lock(gp_tklock);
      mode = TKLOCK_ENABLE;
      tklock_destroy_locks_timeout_remove();
      destroy_locks_id =
          g_timeout_add_seconds(2, tklock_destroy_locks_cb, NULL);
      break;
    }
    default:
      return DBUS_TYPE_INVALID;
  }

  if (check_set_callback(args, &plugin_data->sysui_cb))
    out->data.i32 = -3;
  else
    out->data.i32 = -2;

  return DBUS_TYPE_INT32;
}

int
tklock_close(const char *interface, const char *method, GArray *args,
             system_ui_data *data, system_ui_handler_arg *out)
{
  system_ui_handler_arg *hargs = ((system_ui_handler_arg *)args->data);
  int supported_args[] = {'b'};
  gp_tklock_t *gp_tklock;
  dbus_bool_t silent;

  SYSTEMUI_DEBUG_FN;

  if (check_plugin_arguments(args, supported_args, 1))
  {
    silent = hargs[4].data.bool_val;
    SYSTEMUI_DEBUG("hargs[4].data.bool_val[%u]", hargs[4].data.bool_val);
  }
  else
    silent = TRUE;

  ee_destroy_window();
  tklock_destroy_locks_timeout_remove();

  if (!plugin_data)
  {
    SYSTEMUI_WARNING("tklock wasn't initialized, nop");
    return DBUS_TYPE_VARIANT;
  }

  gp_tklock = plugin_data->gp_tklock;

  if (gp_tklock)
  {
    SYSTEMUI_DEBUG("gp_tklock->one_input %d", gp_tklock->one_input);
    SYSTEMUI_DEBUG("gp_tklock->one_input_status %d",
                   gp_tklock->one_input_status);

    if (gp_tklock->one_input)
    {
      g_signal_handler_is_connected(gp_tklock->window, gp_tklock->btn_press_id);

      if (gp_tklock->one_input_status != TKLOCK_ONE_INPUT_BUTTON_RELEASED ||
          !silent)
      {
        SYSTEMUI_DEBUG("Keeping systemui callback");
        return DBUS_TYPE_VARIANT;
      }
    }

    gp_tklock->one_input_status = TKLOCK_ONE_INPUT_DISABLED;

    SYSTEMUI_DEBUG("gp_tklock->disabled %d", gp_tklock->disabled);

    if (!gp_tklock->disabled)
      gp_tklock_destroy_lock(gp_tklock);
  }

  if (plugin_data->vtklock)
    visual_tklock_destroy_lock(plugin_data->vtklock);

  systemui_free_callback(&plugin_data->sysui_cb);

  return DBUS_TYPE_VARIANT;
}

static gboolean
tklock_setup_plugin(system_ui_data *data)
{
  SYSTEMUI_DEBUG_FN;

  plugin_data = g_slice_new0(tklock_plugin_data);

  if (!plugin_data)
  {
    SYSTEMUI_ERROR("failed to allocate memory for the plugin data");
    return FALSE;
  }

  plugin_data->data = data;

  systemui_add_handler(SYSTEMUI_TKLOCK_OPEN_REQ, tklock_open, data);
  systemui_add_handler(SYSTEMUI_TKLOCK_CLOSE_REQ, tklock_close, data);

  dbus_connection_add_filter(data->system_bus, display_status_cb, NULL, NULL);
  dbus_bus_add_match(data->system_bus, DBUS_MCE_MATCH_RULE, NULL);

  return TRUE;
}

gboolean
plugin_init(system_ui_data *data)
{
  SYSTEMUI_DEBUG_FN;

  if (!data)
  {
    SYSTEMUI_ERROR("initialization parameter value is invalid");
    return FALSE;
  }

  g_return_val_if_fail(tklock_setup_plugin(data), FALSE);

  return TRUE;
}

void
plugin_close(system_ui_data *data)
{
  SYSTEMUI_DEBUG_FN;

  if (plugin_data->data != data)
    SYSTEMUI_WARNING("systemui context is inconsistent");

  if (data)
  {
    systemui_remove_handler(SYSTEMUI_TKLOCK_OPEN_REQ, data);
    systemui_remove_handler(SYSTEMUI_TKLOCK_CLOSE_REQ, data);
  }

  gp_tklock_destroy_lock(plugin_data->gp_tklock);
  visual_tklock_destroy_lock(plugin_data->vtklock);

  g_slice_free(tklock_plugin_data, plugin_data);
  plugin_data = NULL;
}
