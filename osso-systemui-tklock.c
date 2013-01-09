/**
   @file osso-systemui-tklock.c

   @brief Maemo systemui tklock plugin

   Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>

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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>
#include <locale.h>
#include <sys/stat.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <syslog.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkevents.h>
#include <gdk/gdkx.h>
#include <gconf/gconf-client.h>
#include <pango/pango.h>
#include <dbus/dbus.h>
#include <clockd/libtime.h>
#include <hildon/hildon.h>
#include <hildon/hildon-gtk.h>
#include <sqlite3.h>

#include <systemui.h>

#include "osso-systemui-tklock-priv.h"
#include "eventeater.h"

/*typedef struct{
 GtkWidget *window;
 DBusConnection *systemui_conn;
 gboolean window_hidden;
} tklock;
*/
tklock_plugin_data *plugin_data = NULL;
system_ui_callback_t system_ui_callback = {0,};

guint event_count = 0;
guint g_notifications[6] = {0,};
time_t g_notifications_mtime;

static void
vtklock_update_date_time(vtklockts *ts);
static void
visual_tklock_destroy(vtklock_t *vtklock);
static void
visual_tklock_destroy_lock(vtklock_t *vtklock);
static void
visual_tklock_create_view_whimsy(vtklock_t *vtklock);
static vtklock_t*
visual_tklock_new(DBusConnection *systemui_conn);
static void
visual_tklock_present_view(vtklock_t *vtklock);
static void
vtklock_unlock_handler();
static void
visual_tklock_set_unlock_handler(vtklock_t *vtklock, void(*unlock_handler)());
static gboolean
slider_change_value_cb(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);
static void
slider_value_changed_cb(GtkRange *range, gpointer user_data);

static int
tklock_open_handler(const char *interface,
                    const char *method,
                    GArray *args,
                    system_ui_data *data,
                    system_ui_handler_arg *out)
{
  int supported_args[3] = {'u', 'b', 'b'};
  system_ui_handler_arg* hargs = ((system_ui_handler_arg*)args->data);
  int rv;

  SYSTEMUI_DEBUG_FN;

  if( !check_plugin_arguments(args, supported_args, 1) &&
      !check_plugin_arguments(args, supported_args, 2) &&
      !check_plugin_arguments(args, supported_args, 3) )
  {
    return 0;
  }

  SYSTEMUI_DEBUG("hargs[4].data.u32[%u]", hargs[4].data.u32);

  switch(hargs[4].data.u32)
  {
    case TKLOCK_ENABLE_VISUAL:
    {
      ee_destroy_window();
      if(!plugin_data->vtklock)
      {
        plugin_data->vtklock = visual_tklock_new(plugin_data->data->system_bus);
        visual_tklock_set_unlock_handler(plugin_data->vtklock, vtklock_unlock_handler);
      }

      if(!plugin_data->vtklock->window)
        visual_tklock_create_view_whimsy(plugin_data->vtklock);

      visual_tklock_present_view(plugin_data->vtklock);

      plugin_data->mode = TKLOCK_ENABLE_VISUAL;
      break;
    }
    case TKLOCK_ENABLE:
    {
      ee_destroy_window();
      if(plugin_data->mode == TKLOCK_ONEINPUT)
      {
        do_callback(plugin_data->data, &system_ui_callback, TKLOCK_ONEINPUT);
      }
      else if(plugin_data->mode == TKLOCK_ENABLE_VISUAL)
      {
        if(plugin_data->vtklock && plugin_data->vtklock->window)
          visual_tklock_destroy_lock(plugin_data->vtklock);
      }

      plugin_data->mode = TKLOCK_ENABLE;

      break;
    }
    case TKLOCK_ONEINPUT:
    {
      ee_create_window(plugin_data);
      plugin_data->mode = TKLOCK_ONEINPUT;
      break;
    }

  default:
    return 0;
  }

  out->arg_type = 'i';

  rv = check_set_callback(args, &system_ui_callback);

  if(rv)
    out->data.i32 = -3;
  else
    out->data.i32 = -2;

  return 'i';
}

static int
tklock_close_handler(const char *interface,
                     const char *method,
                     GArray *args,
                     system_ui_data *data,
                     system_ui_handler_arg *out)
{
  int supported_args[3] = {'b'};
  dbus_bool_t locked;
  system_ui_handler_arg* hargs = ((system_ui_handler_arg*)args->data);

  SYSTEMUI_DEBUG_FN;

  if(check_plugin_arguments(args, supported_args, 1))
  {
    locked = hargs[4].data.bool_val;
    SYSTEMUI_DEBUG("hargs[4].data.bool_val[%u]", hargs[4].data.bool_val);
  }
  else
    locked = TRUE;

  if(!plugin_data->data)
  {
    SYSTEMUI_WARNING("tklock wasn't initialized, nop");
    goto out;
  }

  if(plugin_data->vtklock)
    visual_tklock_destroy_lock(plugin_data->vtklock);

  if(locked || plugin_data->one_input_mode_event == ButtonRelease)
  {
    plugin_data->one_input_mode_event = 0;
    ee_destroy_window();
    systemui_free_callback(&system_ui_callback);
  }

out:
  return 'v';
}

static void
one_input_mode_handler()
{
  SYSTEMUI_DEBUG_FN;

  systemui_do_callback(plugin_data->data, &system_ui_callback, TKLOCK_ENABLE);
  systemui_do_callback(plugin_data->data, &system_ui_callback, TKLOCK_ONEINPUT);
  systemui_free_callback(&system_ui_callback);
}

static gboolean
tklock_setup_plugin(system_ui_data *data)
{
  SYSTEMUI_DEBUG_FN;

  plugin_data = g_slice_alloc0(sizeof(tklock_plugin_data));
  if(!plugin_data)
  {
    SYSTEMUI_ERROR("failed to allocate memory for the plugin data");
    return FALSE;
  }
  plugin_data->data = data;
  plugin_data->one_input_mode_finished_handler = one_input_mode_handler;
  plugin_data->one_input_mode_event = 0;
  return TRUE;
}

gboolean
plugin_init(system_ui_data *data)
{
  openlog("systemui-tklock", LOG_ALERT | LOG_USER, LOG_NDELAY);

  SYSTEMUI_DEBUG_FN;

  if( !data )
  {
    SYSTEMUI_ERROR("initialization parameter value is invalid");
    return FALSE;
  }

  g_return_val_if_fail(tklock_setup_plugin(data), FALSE);

  systemui_add_handler(SYSTEMUI_TKLOCK_OPEN_REQ,
                       tklock_open_handler,
                       data);

  systemui_add_handler(SYSTEMUI_TKLOCK_CLOSE_REQ,
                       tklock_close_handler,
                       data);

/*  dbus_bus_add_match(data->system_bus,
                     "type='signal',path='/com/nokia/mce/signal',interface='com.nokia.mce.signal',member='display_off'",
                     NULL);*/

  return TRUE;
}

static DBusHandlerResult
vtklock_dbus_filter(DBusConnection *connection, DBusMessage *message, void *user_data)
{
  vtklock_t *vtklock = (vtklock_t*)user_data;

  SYSTEMUI_DEBUG_FN;

  if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL &&
     !g_strcmp0(dbus_message_get_member(message),"time_changed"))
  {
    g_assert( user_data != NULL);
    time_get_synced();
    vtklock_update_date_time(&vtklock->ts);
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static void
vtklock_remove_clockd_dbus_filter(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock->systemui_conn != NULL);

  dbus_bus_remove_match(vtklock->systemui_conn,
                        "type='signal',sender='com.nokia.clockd',interface='com.nokia.clockd',path='/com/nokia/clockd',member='time_changed'",
                        NULL);
  dbus_connection_remove_filter(vtklock->systemui_conn,
                                vtklock_dbus_filter,
                                vtklock);
}

static void
vtklock_add_clockd_dbus_filter(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock->systemui_conn != NULL);

  dbus_bus_add_match(vtklock->systemui_conn,
                        "type='signal',sender='com.nokia.clockd',interface='com.nokia.clockd',path='/com/nokia/clockd',member='time_changed'",
                        NULL);
  if( !dbus_connection_add_filter(vtklock->systemui_conn,
                                  vtklock_dbus_filter,
                                  vtklock,
                                  NULL))
    SYSTEMUI_WARNING("failed to install dbus message filter");
}

void plugin_close(system_ui_data *data)
{
  SYSTEMUI_DEBUG_FN;

  if(plugin_data->data != data)
  {
    SYSTEMUI_ERROR("systemui context is inconsistent");
  }

  if(plugin_data->data)
  {
    remove_handler("tklock_open", plugin_data->data);
    remove_handler("tklock_close", plugin_data->data);
  }

  visual_tklock_destroy(plugin_data->vtklock);

  g_slice_free(tklock_plugin_data,plugin_data);

  plugin_data = NULL;

  closelog();
  /* FIXME dbus_bus_remove_match is missing */

  /*dbus_bus_remove_match(
    sui->system_bus,
    "type='signal',interface='com.nokia.mce.signal',path='/com/nokia/mce/signal'",
    &error);*/

}

static vtklock_t*
visual_tklock_new(DBusConnection *systemui_conn)
{
  vtklock_t *vtklock;

  SYSTEMUI_DEBUG_FN;

  vtklock = g_slice_alloc0(sizeof(vtklock_t));

  if(!vtklock)
  {
    SYSTEMUI_ERROR("failed to allocate memory");
    return NULL;
  }

  g_assert( systemui_conn != NULL );

  vtklock->systemui_conn = systemui_conn;
  visual_tklock_create_view_whimsy(vtklock);

  vtklock->priority = 290;
  vtklock->update_date_time_cb_tag = 0;

  return vtklock;
}

static void
vtklock_update_date_time(vtklockts *ts)
{
  struct tm tm;
  const char *msgid;
  char time_buf[64];
  GConfClient *gc;

  SYSTEMUI_DEBUG_FN;

  g_assert(ts != NULL);

  time_get_synced();
  time_get_local(&tm);

  gc = gconf_client_get_default();

  g_assert(gc);

  if ( gconf_client_get_bool(gc, "/apps/clock/time-format", 0) )
    msgid = "wdgt_va_24h_time";
  else
    msgid = tm.tm_hour > 11 ? "wdgt_va_12h_time_pm" : "wdgt_va_12h_time_am";

  time_format_time(&tm,
                   dgettext("hildon-libs", msgid),
                   time_buf, sizeof(time_buf));

  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(ts->time_label)), time_buf))
  {
     gtk_label_set_text(GTK_LABEL(ts->time_label), time_buf);
  }

  time_format_time(&tm,
                   dgettext("hildon-libs", "wdgt_va_date_long"),
                   time_buf, sizeof(time_buf));

  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(ts->date_label)), time_buf))
  {
     gtk_label_set_text(GTK_LABEL(ts->date_label), time_buf);
  }
}

static void
visual_tklock_destroy_lock(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  if(!vtklock || !vtklock->window)
    return;

  gtk_grab_remove(vtklock->window);

  vtklock_remove_clockd_dbus_filter(vtklock);

  if(vtklock->update_date_time_cb_tag)
  {
    g_source_remove(vtklock->update_date_time_cb_tag);
    vtklock->update_date_time_cb_tag = 0;
  }

  if(g_signal_handler_is_connected(vtklock->slider,vtklock->slider_change_value_id))
    g_signal_handler_disconnect(vtklock->slider,vtklock->slider_change_value_id);

  if(g_signal_handler_is_connected(vtklock->slider,vtklock->slider_value_changed_id))
    g_signal_handler_disconnect(vtklock->slider,vtklock->slider_value_changed_id);

  ipm_hide_window(vtklock->window);
  gtk_widget_unrealize(vtklock->window);
  gtk_widget_destroy(vtklock->window);

  vtklock->slider_adjustment = NULL;
  vtklock->window = NULL;
  vtklock->ts.date_label = NULL;
  vtklock->ts.time_label = NULL;
  vtklock->slider = NULL;
}

static void
visual_tklock_destroy(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  if(!vtklock)
    return;

  visual_tklock_destroy_lock(vtklock);

  g_slice_free(vtklock_t, vtklock);
}

static int
convert_str_to_index(const char *str)
{
  if(!strcmp("chat-message", str))
    return 0;
  if(!strcmp("sms-message", str))
    return 0;
  if(!strcmp("auth-request", str))
    return 1;
  if(!strcmp("chat-invitation", str))
    return 2;
  if(!strcmp("missed-call", str))
    return 3;
  if(!strcmp("email-message", str))
    return 4;
  if(!strcmp("voice-mail", str))
    return 5;

  SYSTEMUI_WARNING("Unknown string! return -1");

  return -1;
}

static int
get_missed_events_cb(void*user_data, int numcols, char**column_text, char**column_name)
{
  vtklock_t *vtklock = (vtklock_t *)user_data;
  int index;

  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  if(numcols != 3)
  {
    SYSTEMUI_WARNING("select returned error values count");
    return -1;
  }

  if(!column_text[0] || !column_text[1] || !column_text[2])
  {
    SYSTEMUI_WARNING("select return error values");
    return -1;
  }

  index = convert_str_to_index(column_text[0]);
  if(index == -1)
    return 0;

  vtklock->event[index].hint = g_ascii_strtoll(column_text[1], NULL, 10);
  vtklock->event[index].count += g_ascii_strtoll(column_text[2], NULL, 10);

  event_count += vtklock->event[index].count;

  return 0;
}

static void
get_missed_events_from_db(vtklock_t *vtklock)
{
  sqlite3 *pdb;
  char *sql;
  char *errmsg = NULL;
  int i;
  gchar * db_fname;
  struct stat sb;

  SYSTEMUI_DEBUG_FN;
  db_fname = g_build_filename(g_get_home_dir(), ".config/hildon-desktop/notifications.db", NULL);

  if(stat(db_fname, &sb))
  {
    SYSTEMUI_NOTICE("error in reading db file info [%s]", db_fname);
  }

  if(g_notifications_mtime == sb.st_mtime)
    goto out;

  event_count = 0;

  for(i = 0; i< 6; i++)
  {
    vtklock->event[i].count = 0;
    vtklock->event[i].hint = 0;
    g_notifications[i] = 0;
  }

  if(sqlite3_open_v2(db_fname,
                  &pdb,
                  SQLITE_OPEN_READONLY,
                  NULL) != SQLITE_OK)
  {
    SYSTEMUI_WARNING("error in opening db [%s]", db_fname);
    goto db_close_out;
  }

  sql = sqlite3_mprintf("SELECT H.value, H2.value, COUNT(*) "
                        "FROM notifications N, hints H, hints H2 "
                        "WHERE N.id=H.nid AND H.id='category' and H2.id = 'time' and H2.nid = H.nid "
                        "GROUP BY  H.value "
                        "ORDER BY H2.value;");

  if(sqlite3_exec(pdb,
               sql,
               get_missed_events_cb,
               vtklock,
               &errmsg) != SQLITE_OK)
  {
     SYSTEMUI_WARNING("Unable to get data about missed events from db: %s", errmsg);
     sqlite3_free(errmsg);
  }

  sqlite3_free(sql);

db_close_out:
  sqlite3_close(pdb);

out:
  g_free(db_fname);
}

gboolean vtklock_reset_slider_position(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);
  g_assert(vtklock->slider != NULL && GTK_IS_RANGE(vtklock->slider));

  vtklock->slider_value = 3.0;
  vtklock->slider_status = 1;
  gtk_range_set_value(GTK_RANGE(vtklock->slider), vtklock->slider_value);

  return TRUE;
}

static GtkWidget *
vtklock_create_date_time_widget(vtklockts *ts, gboolean force_fake_portrait)
{
  GtkWidget *box,*time_box,*date_box;
  GtkWidget *date_label, *time_label;
  PangoFontDescription *font_desc;

  SYSTEMUI_DEBUG_FN;

  g_assert(ts != NULL && ts->time_label == NULL && ts->date_label == NULL);

  if(force_fake_portrait)
  {
    box = gtk_hbox_new(FALSE, 4);
    time_box = gtk_vbox_new(TRUE, 0);
    date_box = gtk_vbox_new(TRUE, 0);
  }
  else
  {
    box = gtk_vbox_new(FALSE, 4);
    time_box = gtk_hbox_new(TRUE, 0);
    date_box = gtk_hbox_new(TRUE, 0);
  }

  font_desc = pango_font_description_new();
  pango_font_description_set_family(font_desc, "Nokia Sans");
  pango_font_description_set_absolute_size(font_desc, 75 * PANGO_SCALE);

  time_label = gtk_label_new("vt");
  gtk_widget_modify_font(time_label, font_desc);
  pango_font_description_free(font_desc);

  date_label = gtk_label_new("vt");

  hildon_helper_set_logical_color(date_label, GTK_RC_FG, GTK_STATE_NORMAL, "SecondaryTextColor");
  hildon_helper_set_logical_color(date_label, GTK_RC_FG, GTK_STATE_PRELIGHT, "SecondaryTextColor");

  if(force_fake_portrait)
  {
    gtk_label_set_angle(GTK_LABEL(date_label), 270.0);
    gtk_label_set_angle(GTK_LABEL(time_label), 270.0);
  }

  gtk_box_pack_start(GTK_BOX(time_box), time_label, TRUE, TRUE, FALSE);
  gtk_box_pack_start(GTK_BOX(date_box), date_label, TRUE, TRUE, FALSE);


  if(force_fake_portrait)
  {
    gtk_box_pack_end(GTK_BOX(box), time_box, FALSE, FALSE, FALSE);
    gtk_box_pack_end(GTK_BOX(box), date_box, FALSE, FALSE, FALSE);
  }
  else
  {
    gtk_box_pack_start(GTK_BOX(box), time_box, FALSE, FALSE, FALSE);
    gtk_box_pack_start(GTK_BOX(box), date_box, FALSE, FALSE, FALSE);
  }

  ts->date_label=date_label;
  ts->time_label=time_label;

  return box;
}

static const char *
get_icon_name(guint index)
{
  const char *icon_names[]={
    "tasklaunch_sms_chat",
    "tasklaunch_authorization_response",
    "general_chatroom_invitation",
    "general_application_call",
    "general_email",
    "tasklaunch_voice_mail",
  };

  return icon_names[index];
}

static GtkWidget *
vtklock_create_event_icons(vtklock_t *vtklock, gboolean force_fake_portrait)
{
  int i;
  GtkWidget *align;
  GtkWidget *icon_packer;

  SYSTEMUI_DEBUG_FN;

  if(force_fake_portrait)
  {
    align = gtk_alignment_new(0, 0.5, 0, 0);
    icon_packer = gtk_vbox_new(TRUE, 40);
  }
  else
  {
    align = gtk_alignment_new(0.5, 0, 0, 0);
    icon_packer = gtk_hbox_new(TRUE, 40);
  }

  for(i=0; i<6; i++)
  {
    PangoFontDescription *font_desc;
    GtkWidget *count_label;
    GdkPixbuf *pixbuf;
    GtkWidget *image;
    GtkWidget *packer;
    gchar count_str[10];
    const char *icon_name;

    if(!vtklock->event[i].count)
      continue;

    icon_name = get_icon_name(i);
    g_assert(icon_name != NULL);

    font_desc = pango_font_description_new();
    pango_font_description_set_family(font_desc, "Nokia Sans");
    pango_font_description_set_absolute_size(font_desc, 25 * PANGO_SCALE);

    g_assert(g_snprintf(count_str, sizeof(count_str), "%d", vtklock->event[i].count) != 0);

    count_label = gtk_label_new(count_str);
    g_assert(count_label != NULL);

    gtk_widget_modify_font(count_label, font_desc);

    pango_font_description_free(font_desc);

    pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                    icon_name, 48,
                                    GTK_ICON_LOOKUP_NO_SVG,
                                    NULL);
    if(pixbuf && force_fake_portrait)
    {
      GdkPixbuf *pixbuf_rotated;

      gtk_label_set_angle(GTK_LABEL(count_label), 270.0);
      pixbuf_rotated = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
      g_object_unref(pixbuf);
      pixbuf = pixbuf_rotated;
    }

    g_assert(pixbuf != NULL);

    image = gtk_image_new_from_pixbuf(pixbuf);
    g_assert(image != NULL);

    g_object_unref(pixbuf);

    if(force_fake_portrait)
      packer = gtk_vbox_new(TRUE, 0);
    else
      packer = gtk_hbox_new(TRUE, 0);

    gtk_box_pack_start(GTK_BOX(packer), image, TRUE, TRUE, FALSE);
    gtk_box_pack_end(GTK_BOX(packer), count_label, TRUE, TRUE, FALSE);

    gtk_box_pack_start(GTK_BOX(icon_packer), packer, TRUE, TRUE, FALSE);
  }

  gtk_container_add(GTK_CONTAINER(align), icon_packer);

  return align;

}

static void
set_gdk_property(GtkWidget *widget, GdkAtom property, gboolean value)
{
  if(GTK_WIDGET_REALIZED(widget))
  {
    gdk_property_change(widget->window,
                        property,
                        gdk_x11_xatom_to_atom(XA_CARDINAL),
                        32,
                        GDK_PROP_MODE_REPLACE,
                        (const guchar*)&value,
                        1
                        );
  }
}
static void
visual_tklock_set_hildon_flags(GtkWidget *window, gboolean force_fake_portrait)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(window);

  set_gdk_property(window, gdk_atom_intern_static_string("_HILDON_WM_ACTION_NO_TRANSITIONS"), TRUE);
  gtk_window_fullscreen(GTK_WINDOW(window));
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
  hildon_gtk_window_set_do_not_disturb(GTK_WINDOW(window), TRUE);

  if(force_fake_portrait)
  {
    set_gdk_property(window, gdk_atom_intern_static_string("_HILDON_PORTRAIT_MODE_SUPPORT"), TRUE);
    set_gdk_property(window, gdk_atom_intern_static_string("_HILDON_PORTRAIT_MODE_REQUEST"), TRUE);
  }
}

static GtkWidget *
visual_tklock_create_slider(gboolean force_fake_portrait)
{
  GtkWidget *slider;

  SYSTEMUI_DEBUG_FN;

  slider  = force_fake_portrait?hildon_gtk_vscale_new():hildon_gtk_hscale_new();
  g_object_set(slider, "jump-to-position", FALSE, NULL);

  gtk_widget_set_name(slider, force_fake_portrait?"sui-tklock-slider-portrait":"sui-tklock-slider");

  if(force_fake_portrait)
    gtk_widget_set_size_request(slider, -1, 440);
  else
    gtk_widget_set_size_request(slider, 440, -1);

  gtk_range_set_update_policy(GTK_RANGE(slider), GTK_UPDATE_DISCONTINUOUS);
  gtk_range_set_range(GTK_RANGE(slider),0.0, 40.0);
  gtk_range_set_value(GTK_RANGE(slider), 3.0);

  return slider;

}

static void
fill_background(vtklock_t *vtklock, gboolean portrait, gboolean fake)
{
  GdkPixbuf *pixbuf;
  GdkPixmap *bg_pixmap = NULL;

  if (portrait)
    pixbuf = gdk_pixbuf_new_from_file("/etc/hildon/theme/backgrounds/lockslider-portrait.png", NULL);
  else
    pixbuf = gdk_pixbuf_new_from_file("/etc/hildon/theme/backgrounds/lockslider.png", NULL);

  if(pixbuf)
  {
    GtkStyle *gs;
    if(fake)
    {

      GdkPixbuf *pixbuf_rotated;

      pixbuf_rotated = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
      g_object_unref(pixbuf);
      pixbuf = pixbuf_rotated;

    }

    gdk_pixbuf_render_pixmap_and_mask(pixbuf, &bg_pixmap, NULL, 255);

    g_object_unref(pixbuf);

    /* FIXME */
    /*
     * Could we use that:
     * gdk_window_set_back_pixmap( GDK_WINDOW (window), pixMap, TRUE);
     *
     * also, do we really need to copy the style, gtk_style_new should to the job too.
     *
     *the code bellow leads to ~1MB of memory usage even when vtklock is not visible
     */
    gs = gtk_style_copy(gtk_rc_get_style(vtklock->window));
    gs->bg_pixmap[0] = bg_pixmap;
    gtk_widget_set_style(vtklock->window, gs);
    g_object_unref(gs);
  }
  else
  {
    /* FIXME - what is the real color value? */
    GdkColor color = {0, 0, 0, 128};
    gtk_widget_modify_bg(vtklock->window, GTK_STATE_NORMAL, &color);
  }
}

static gboolean
configure_event_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
  g_return_val_if_fail(widget != NULL, FALSE);
  g_return_val_if_fail(event->type == GDK_CONFIGURE, FALSE);
  g_return_val_if_fail(data != NULL, FALSE);

  fill_background((vtklock_t *)data, (gdk_screen_height() > gdk_screen_width()), FALSE);

  return FALSE;
}

static void
visual_tklock_create_view_whimsy(vtklock_t *vtklock)
{
  GdkPixbuf *pixbuf;
  GtkWidget *slider_align;
  GtkWidget *window_align;
  GtkWidget *label;
  GtkWidget *label_align;
  GtkWidget *label_packer;
  GtkWidget *timestamp_packer;
  GtkWidget *timestamp_packer_align;
  GtkWidget *icon_packer_align = NULL;
  GConfClient *gc;
  GtkRequisition sr;
  gboolean force_fake_portrait;

  SYSTEMUI_DEBUG_FN;

  if(vtklock->window)
    return;

  get_missed_events_from_db(vtklock);

  force_fake_portrait = gdk_screen_height() > gdk_screen_width()?TRUE:FALSE;
  vtklock->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(vtklock->window), "visual_tklock");
  gtk_window_set_decorated(GTK_WINDOW(vtklock->window), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(vtklock->window), TRUE);

  gc = gconf_client_get_default();

  /* check if autorotation is enabled */
  if (gc && gconf_client_get_bool(gc, "/system/systemui/tklock/auto_rotation", NULL) )
  {
    /* Check if we have force_fake_portrait lockslider background */
    pixbuf = gdk_pixbuf_new_from_file("/etc/hildon/theme/backgrounds/lockslider-portrait.png", NULL);
    if(pixbuf)
    {
      g_object_unref(pixbuf);
      hildon_gtk_window_set_portrait_flags(GTK_WINDOW(vtklock->window), HILDON_PORTRAIT_MODE_SUPPORT);
      g_signal_connect(G_OBJECT(vtklock->window), "configure-event", G_CALLBACK(configure_event_cb), vtklock);
      fill_background(vtklock, force_fake_portrait, FALSE);
      force_fake_portrait = FALSE;
    }
    else
    {
      fill_background(vtklock, FALSE, force_fake_portrait);
    }
  }
  else
  {
    fill_background(vtklock, FALSE, force_fake_portrait);
  }

  if (gc)
    g_object_unref(gc);

  vtklock->slider = visual_tklock_create_slider(force_fake_portrait);

  vtklock->slider_status = 1;

  slider_align = gtk_alignment_new(0.5,0.5,0,0);
  gtk_container_add(GTK_CONTAINER(slider_align), vtklock->slider);

  vtklock->slider_adjustment = gtk_range_get_adjustment(GTK_RANGE(vtklock->slider));

  g_assert(vtklock->slider_adjustment != NULL);

  vtklock_reset_slider_position(vtklock);

  label = gtk_label_new(dgettext("osso-system-lock", "secu_swipe_to_unlock"));
  hildon_helper_set_logical_font(label, "SystemFont");

  if(force_fake_portrait)
      gtk_label_set_angle(GTK_LABEL(label), 270.0);

  timestamp_packer = vtklock_create_date_time_widget(&vtklock->ts, force_fake_portrait);
  g_assert(timestamp_packer != NULL);

  vtklock_update_date_time(&vtklock->ts);

  if(force_fake_portrait)
    timestamp_packer_align = gtk_alignment_new(0.0, 0.5, 0, 0);
  else
    timestamp_packer_align = gtk_alignment_new(0.5, 1.0, 0, 0);

  gtk_container_add(GTK_CONTAINER(timestamp_packer_align), timestamp_packer);

  if(event_count)
    icon_packer_align = vtklock_create_event_icons(vtklock, force_fake_portrait);

  if(force_fake_portrait)
  {
    label_align = gtk_alignment_new(1.0, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(label_align), label);
    label_packer = gtk_hbox_new(FALSE, 24);
  }
  else
  {
    label_align = gtk_alignment_new(0.5, 0.0, 0, 0);
    gtk_container_add(GTK_CONTAINER(label_align), label);
    label_packer = gtk_vbox_new(FALSE, 24);
  }

  if(force_fake_portrait)
  {
    gtk_box_pack_end(GTK_BOX(label_packer), timestamp_packer_align, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(label_packer), slider_align, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(label_packer), label_align, FALSE, FALSE, 0);

    if(event_count)
      gtk_box_pack_end(GTK_BOX(label_packer), icon_packer_align, FALSE, FALSE, 0);

    window_align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(window_align), 8, 24, 1, 0);
  }
  else
  {
    gtk_box_pack_start(GTK_BOX(label_packer), timestamp_packer_align, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(label_packer), slider_align, FALSE, FALSE, 0);

    if(event_count)
      gtk_box_pack_end(GTK_BOX(label_packer), icon_packer_align, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(label_packer), label_align, FALSE, FALSE, 0);

    window_align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(window_align), 0, 0, 0, 16);

  }

  gtk_container_add(GTK_CONTAINER(window_align), label_packer);
  gtk_container_add(GTK_CONTAINER(vtklock->window), window_align);

  vtklock->slider_change_value_id = g_signal_connect_data(vtklock->slider,
                                                          "change-value",
                                                          G_CALLBACK(slider_change_value_cb),
                                                          vtklock,
                                                          NULL, 0);
  vtklock->slider_value_changed_id = g_signal_connect_data(vtklock->slider,
                                                           "value-changed",
                                                           G_CALLBACK(slider_value_changed_cb),
                                                           vtklock,
                                                           NULL, 0);

  gtk_widget_show_all(window_align);

  gtk_widget_size_request(vtklock->slider, &sr);

  if(event_count)
  {
    if(force_fake_portrait)
      gtk_alignment_set_padding(GTK_ALIGNMENT(icon_packer_align),
                                0, 0, 30, 60-sr.width/2);
    else
      gtk_alignment_set_padding(GTK_ALIGNMENT(icon_packer_align),
                                30, 60-sr.height/2, 0, 0);
  }
  else
  {
    GtkRequisition r;

    gtk_widget_size_request(label, &r);

    if(force_fake_portrait)
      gtk_alignment_set_padding(GTK_ALIGNMENT(label_align),
                                0, 0, (abs(480-sr.width)/2-48)-r.width, 0);
    else
      gtk_alignment_set_padding(GTK_ALIGNMENT(label_align),
                                0, (abs(480-sr.height)/2-48)-r.height, 0, 0);
  }

  gtk_widget_realize(vtklock->window);
  visual_tklock_set_hildon_flags(vtklock->window, force_fake_portrait);
  gtk_widget_show_all(vtklock->window);
  vtklock_add_clockd_dbus_filter(vtklock);
}

static void
visual_tklock_present_view(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  vtklock_update_date_time(&vtklock->ts);

  if(!gtk_grab_get_current())
  {
    gtk_grab_add(vtklock->window);
  }

  gtk_widget_realize(vtklock->window);
  gdk_flush();
  ipm_show_window(vtklock->window, vtklock->priority);
  gdk_window_invalidate_rect(vtklock->window->window, NULL, TRUE);
  gdk_window_process_all_updates();
  gdk_flush();

  if(!vtklock->update_date_time_cb_tag)
  {
    vtklock->update_date_time_cb_tag = g_timeout_add(1000,
                                                     (GSourceFunc)vtklock_update_date_time,
                                                     &vtklock->ts);
  }
}

static void
vtklock_unlock_handler()
{
  SYSTEMUI_DEBUG_FN;

  systemui_do_callback( plugin_data->data, &system_ui_callback, TKLOCK_ENABLE);
}

static void
visual_tklock_set_unlock_handler(vtklock_t *vtklock, void(*unlock_handler)())
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);
  vtklock->unlock_handler = unlock_handler;
}

static gboolean
slider_change_value_cb(GtkRange     *range,
                       GtkScrollType scroll,
                       gdouble       value,
                       gpointer      user_data)
{
  vtklock_t *vtklock = (vtklock_t *)user_data;

  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  if((value-3.0) > 0.5)
  {
    if(
       (value - vtklock->slider_adjustment->upper < 0.899999976) &&
       (value - vtklock->slider_adjustment->upper > -0.899999976)
      )
    {
      vtklock->slider_adjustment->lower = 4;

      if(vtklock->unlock_handler)
        vtklock->unlock_handler();
    }
    if(scroll != GTK_SCROLL_STEP_BACKWARD)
      return FALSE;
  }
  return FALSE;
}

static void
slider_value_changed_cb(GtkRange *range,
                        gpointer  user_data)
{
  vtklock_t *vtklock = (vtklock_t *)user_data;

  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  if(vtklock->slider_status != 4)
  {
    gdouble value;

    value = gtk_range_get_value(range);

    if(
       (vtklock->slider_adjustment->upper-value < 5) &&
       (vtklock->slider_adjustment->upper-value >-5)
       )
    {
      gtk_range_set_value(GTK_RANGE(vtklock->slider), vtklock->slider_adjustment->upper);
      vtklock->slider_value = vtklock->slider_adjustment->upper;
      if(vtklock->unlock_handler)
        vtklock->unlock_handler();
    }
    else
      vtklock_reset_slider_position(vtklock);
  }
}
