/**
   @file eventeater.c

   @brief Maemo systemui tklock plugin event eater window

   Copyright (C) 2013 Ivaylo Dimitrov <freemangordon@abv.bg>

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
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <syslog.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <systemui.h>

#include "osso-systemui-tklock-priv.h"

static GdkWindow *ee_window = NULL;

void
ee_destroy_window()
{
  SYSTEMUI_DEBUG_FN;

  if(ee_window)
  {
    gdk_window_destroy(ee_window);
    ee_window = 0;
  }
}

static GdkFilterReturn
one_input_event_eater_cb(GdkXEvent *gdkxevent,
                         GdkEvent *gdkevent,
                         gpointer data)
{
  tklock_plugin_data *plugin_data = (tklock_plugin_data *)data;
  XEvent *xevent = gdkxevent;

  SYSTEMUI_DEBUG_FN;

  if (xevent->type==ButtonPress)
  {
    plugin_data->one_input_mode_event = ButtonPress;
  }
  else if (xevent->type==ButtonRelease)
  {
    plugin_data->one_input_mode_event = ButtonRelease;

    if(plugin_data && plugin_data->one_input_mode_finished_handler)
      plugin_data->one_input_mode_finished_handler();

    ee_destroy_window();
  }

  return GDK_FILTER_REMOVE;
}

void
ee_create_window(tklock_plugin_data *plugin_data)
{
  Display *dpy;
  GdkScreen *screen;
  Window window;
  XVisualInfo vinfo;
  Colormap cmap;
  XSetWindowAttributes attributes;

  const guint layer = 10;

  SYSTEMUI_DEBUG_FN;

  /* Just in case */
  if(ee_window)
    return;

  dpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
  screen = gdk_screen_get_default();

  XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, VisualDepthMask, &vinfo);

  cmap = XCreateColormap(dpy, DefaultRootWindow(dpy), vinfo.visual, AllocNone);

  attributes.colormap = cmap;
  attributes.override_redirect = 1;
  /* FIXME XBlackPixel?*/
  attributes.border_pixel = 0;
  attributes.background_pixel = 0;

  window =  XCreateWindow(dpy,
                             DefaultRootWindow(dpy),
                             0, 0,
                             gdk_screen_get_width(screen), gdk_screen_get_height(screen),
                             0,
                             32,
                             InputOutput,
                             vinfo.visual,
                             CWBackPixel|CWBorderPixel|CWOverrideRedirect|CWColormap,
                             &attributes);
  if(!window)
  {
    SYSTEMUI_WARNING("Couldn't create event eater window -> destroy colormap");
    if(cmap)
      XFreeColormap(dpy, cmap);
    return;
  }

  XChangeProperty(dpy,
                  window,
                  XInternAtom(dpy, "_HILDON_STACKING_LAYER", False),
                  XA_CARDINAL,
                  32,
                  PropModeReplace,
                  (unsigned char*)&layer,
                  1);


  XStoreName(dpy, window, "EventEater");
  XMapWindow(dpy, window);

  /* According to similar code in meego - "Grabs are released automatically at unmap" */
  XGrabKeyboard(dpy, window, False, GrabModeAsync, GrabModeAsync, CurrentTime);
  XGrabPointer(dpy, window, False, ButtonPressMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

  XFreeColormap(dpy, cmap);

  plugin_data->one_input_mode_event = 0;
  ee_window = gdk_window_foreign_new(window);
  gdk_window_add_filter(ee_window,
                        one_input_event_eater_cb,
                        plugin_data);

}
