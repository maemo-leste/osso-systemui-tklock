#include <gdk/gdkx.h>
#include <hildon/hildon.h>
#include <syslog.h>
#include <systemui.h>
#include <X11/Xatom.h>

#include <string.h>
#include <libintl.h>
#include <math.h>
#include <time.h>
#include <clockd/libtime.h>
#include <sqlite3.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "visual-tklock.h"
#include "tklock-grab.h"

#define HILDON_BACKGROUNDS_DIR "/etc/hildon/theme/backgrounds/"
#define LOCKSLIDER_BACKGROUND HILDON_BACKGROUNDS_DIR "lockslider.png"
#define LOCKSLIDER_PORTRAIT_BACKGROUND HILDON_BACKGROUNDS_DIR "lockslider-portrait.png"

#define DBUS_CLOCKD_MATCH_RULE \
  "type='signal',sender='com.nokia.clockd'," \
  "interface='com.nokia.clockd'," \
  "path='/com/nokia/clockd'," \
  "member='time_changed'"

static guint event_idx[6];
guint event_count = 0;

static void
set_gdk_property(GtkWidget *widget, GdkAtom property, guint value)
{
  if (GTK_WIDGET_REALIZED(widget))
  {
    gdk_property_change(widget->window,
                        property,
                        gdk_x11_xatom_to_atom(XA_CARDINAL),
                        32,
                        GDK_PROP_MODE_REPLACE,
                        (const guchar *)&value,
                        1
                        );
  }
}

static gboolean
reset_slider(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);
  g_assert(vtklock->slider != NULL && GTK_IS_RANGE(vtklock->slider));

  vtklock->slider_value = 3.0;
  vtklock->slider_status = 1;
  gtk_range_set_value(GTK_RANGE(vtklock->slider), vtklock->slider_value);

  return TRUE;
}

static void
value_changed_cb(GtkRange *range, gpointer user_data)
{
  vtklock_t *vtklock = (vtklock_t *)user_data;

  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  if (vtklock->slider_status != 4)
  {
    gdouble value;

    value = gtk_range_get_value(range);

    if (fabs(vtklock->slider_adjustment->upper - value) < 5.0)
    {
      gtk_range_set_value(GTK_RANGE(vtklock->slider),
                          vtklock->slider_adjustment->upper);
      vtklock->slider_value = vtklock->slider_adjustment->upper;

      if (vtklock->unlock_handler)
        vtklock->unlock_handler();
    }
    else
      reset_slider(vtklock);
  }
}

static gboolean
update_timestamp(gpointer user_data)
{
  vtklockts *ts = user_data;
  char time_buf[256];
  GConfClient *gc;
  struct tm tm;
  const char *msgid;

  g_assert(ts != NULL);

  time_get_synced();

  if (time_get_local(&tm) != 0)
    memset(&tm, 0, sizeof(tm));

  gc = gconf_client_get_default();

  g_assert(gc);

  if (gconf_client_get_bool(gc, "/apps/clock/time-format", FALSE))
    msgid = "wdgt_va_24h_time";
  else if (tm.tm_hour > 11)
    msgid = "wdgt_va_12h_time_pm";
  else
    msgid = "wdgt_va_12h_time_am";

  time_format_time(&tm, dgettext("hildon-libs", msgid), time_buf,
                   sizeof(time_buf) - 1);

  if (g_strcmp0(gtk_label_get_text(GTK_LABEL(ts->time_label)), time_buf))
    gtk_label_set_text(GTK_LABEL(ts->time_label), time_buf);

  time_format_time(&tm, dgettext("hildon-libs", "wdgt_va_date_long"), time_buf,
                   sizeof(time_buf) - 1);

  if (g_strcmp0(gtk_label_get_text(GTK_LABEL(ts->date_label)), time_buf))
    gtk_label_set_text(GTK_LABEL(ts->date_label), time_buf);

  g_object_unref(gc);

  return TRUE;
}

static gboolean
vtklock_key_press_event_cb(GtkWidget *widget, GdkEvent *event,
                           gpointer user_data)
{
  vtklock_t *vtklock = user_data;

  g_assert(vtklock != NULL && vtklock->window != NULL &&
      GTK_WIDGET_MAPPED(vtklock->window));

  return TRUE;
}

static gboolean
change_value_cb(GtkRange *range, GtkScrollType scroll, gdouble value,
                gpointer user_data)
{
  vtklock_t *vtklock = (vtklock_t *)user_data;

  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  if ((3.0 - value) < 0.5)
  {
    if (fabs(value - vtklock->slider_adjustment->upper) < 0.899999976)
    {
      vtklock->slider_status = 4;

      if (vtklock->unlock_handler)
        vtklock->unlock_handler();
    }
    else if (scroll == GTK_SCROLL_JUMP)
      return FALSE;
  }

  return TRUE;
}

static gboolean
visual_tklock_map_cb(GtkWidget *widget, GdkEvent *event, vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  if (!tklock_grab_try(vtklock->window->window, TRUE,
                       GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK,
                       vtklock->window->window))
  {
    SYSTEMUI_ERROR("GRAB FAILED (systemui grab), visual tklock can't be "
                   "enabled, request display unblank");
    tklock_unlock(vtklock->systemui_conn);
  }
  else if (gtk_grab_get_current())
    gtk_grab_add(vtklock->window);

  return TRUE;
}

void
visual_tklock_present_view(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  update_timestamp(&vtklock->ts);

  g_signal_connect_after(vtklock->window, "map-event",
                         G_CALLBACK(visual_tklock_map_cb), vtklock);

  gtk_widget_realize(vtklock->window);
  gdk_flush();

  ipm_show_window(vtklock->window, vtklock->priority);
  gdk_window_invalidate_rect(vtklock->window->window, NULL, TRUE);
  gdk_window_process_all_updates();
  gdk_flush();

  if (!vtklock->update_timestamp_id)
  {
    vtklock->update_timestamp_id =
        g_timeout_add(1000, update_timestamp, &vtklock->ts);
  }
}

static int
convert_str_to_index(const char *str)
{
  if (!strcmp("chat-message", str))
    return 0;

  if (!strcmp("sms-message", str))
    return 0;

  if (!strcmp("auth-request", str))
    return 1;

  if (!strcmp("chat-invitation", str))
    return 2;

  if (!strcmp("missed-call", str))
    return 3;

  if (!strcmp("email-message", str))
    return 4;

  if (!strcmp("voice-mail", str))
    return 5;

  SYSTEMUI_WARNING("Unknown string! return -1");

  return -1;
}

static int
get_missed_events_cb(void *user_data, int numcols, char **column_text,
                     char **column_name)
{
  vtklock_t *vtklock = (vtklock_t *)user_data;
  int index;

  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock != NULL);

  if (numcols != 3)
  {
    SYSTEMUI_WARNING("select returned error values count");
    return -1;
  }

  if (!column_text[0] || !column_text[1] || !column_text[2])
  {
    SYSTEMUI_WARNING("select return error values");
    return -1;
  }

  index = convert_str_to_index(column_text[0]);

  if (index == -1)
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
  int i, j;
  gchar *db_fname;
  struct stat stat_buf;

  SYSTEMUI_DEBUG_FN;

  memset(&stat_buf, 0, sizeof(stat_buf));

  db_fname = g_build_filename(g_get_home_dir(),
                              ".config/hildon-desktop/notifications.db", NULL);

  if (db_fname)
  {
    if (stat(db_fname, &stat_buf))
      SYSTEMUI_NOTICE("error in reading db file info [%s]", db_fname);
  }
  else
    SYSTEMUI_ERROR("g_build_filename returned NULL");

  if (vtklock->db_mtime == stat_buf.st_mtime)
    goto out;

  event_count = 0;

  for (i = 0; i < G_N_ELEMENTS(vtklock->event); i++)
  {
    vtklock->event[i].count = 0;
    vtklock->event[i].hint = 0;
    event_idx[i] = i;
  }

  if (!db_fname)
    goto out;

  if (sqlite3_open_v2(db_fname, &pdb, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
  {
    SYSTEMUI_WARNING("error in opening db [%s]", db_fname);
    goto db_close_out;
  }

  sql = sqlite3_mprintf(
        "SELECT H.value, H2.value, COUNT(*) "
        "FROM notifications N, hints H, hints H2 "
        "WHERE N.id=H.nid AND H.id='category' and H2.id = 'time' and H2.nid = H.nid "
        "GROUP BY  H.value "
        "ORDER BY H2.value;");

  if (sqlite3_exec(
        pdb, sql, get_missed_events_cb, vtklock, &errmsg) != SQLITE_OK)
  {
     SYSTEMUI_WARNING("Unable to get data about missed events from db: %s",
                      errmsg);
     sqlite3_free(errmsg);
  }

  sqlite3_free(sql);

  /* Lame 'ORDER BY count DESC' loop */
  for (i = 0; i < G_N_ELEMENTS(vtklock->event); i++)
  {
    for (j = i; j < G_N_ELEMENTS(vtklock->event); j++)
    {
      if (vtklock->event[i].count < vtklock->event[j].count)
      {
        guint tmp = event_idx[i];

        event_idx[i] = event_idx[j];
        event_idx[j] = tmp;
      }
    }
  }

  vtklock->db_mtime = stat_buf.st_mtime;

db_close_out:
  sqlite3_close(pdb);

out:
  g_free(db_fname);
}

static DBusHandlerResult
handle_time_changed(DBusConnection *connection, DBusMessage *message,
                    void *user_data)
{
  vtklock_t *vtklock = user_data;

  SYSTEMUI_DEBUG_FN;

  if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL &&
      !g_strcmp0(dbus_message_get_member(message), CLOCKD_TIME_CHANGED))
  {
    g_assert(vtklock != NULL);

    time_get_synced();
    update_timestamp(&vtklock->ts);
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
install_dbus_handlers(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock->systemui_conn != NULL);
  g_assert(vtklock->dbus_filter_installed == FALSE);

  dbus_bus_add_match(vtklock->systemui_conn, DBUS_CLOCKD_MATCH_RULE, NULL);

  if (!dbus_connection_add_filter(vtklock->systemui_conn, handle_time_changed,
                                  vtklock, NULL))
  {
    SYSTEMUI_WARNING("failed to install dbus message filter");
  }
  else
    vtklock->dbus_filter_installed = TRUE;
}

static void
remove_dbus_handlers(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  g_assert(vtklock->systemui_conn != NULL);

  if (vtklock->dbus_filter_installed == FALSE)
    return;

  dbus_bus_remove_match(vtklock->systemui_conn, DBUS_CLOCKD_MATCH_RULE, NULL);
  dbus_connection_remove_filter(
        vtklock->systemui_conn, handle_time_changed, vtklock);
  vtklock->dbus_filter_installed = FALSE;
}

void
visual_tklock_destroy_lock(vtklock_t *vtklock)
{
  SYSTEMUI_DEBUG_FN;

  if (!vtklock)
    return;

  remove_dbus_handlers(vtklock);

  if (vtklock->update_timestamp_id)
  {
    g_source_remove(vtklock->update_timestamp_id);
    vtklock->update_timestamp_id = 0;
  }

  ipm_hide_window(vtklock->window);
  gtk_widget_unrealize(vtklock->window);
  gtk_widget_destroy(vtklock->window);
  vtklock->slider_adjustment = NULL;
  vtklock->window = NULL;
  vtklock->ts.date_label = NULL;
  vtklock->ts.time_label = NULL;
  vtklock->slider = NULL;
}

void
visual_tklock_destroy(vtklock_t *vtklock)
{
  if (!vtklock)
    return;

  visual_tklock_destroy_lock(vtklock);
  g_slice_free(vtklock_t, vtklock);
}

vtklock_t *
visual_tklock_new(DBusConnection *conn)
{
  vtklock_t *vtklock = g_slice_new0(vtklock_t);

  SYSTEMUI_DEBUG_FN;

  if (vtklock)
  {
    g_assert(conn != NULL);

    vtklock->systemui_conn = conn;
    visual_tklock_create_view_whimsy(vtklock);
    vtklock->priority = 290;
    vtklock->update_timestamp_id = 0;
  }
  else
    SYSTEMUI_ERROR("failed to allocate memory");

  return vtklock;
}

void
visual_tklock_set_unlock_handler(vtklock_t *vtklock, void (*handler)())
{
  g_assert(vtklock != NULL);

  vtklock->unlock_handler = handler;
}

void
visual_tklock_disable_lock(vtklock_t *vtklock)
{
  g_assert(vtklock != NULL);
  remove_dbus_handlers(vtklock);

  if (vtklock->update_timestamp_id)
  {
    g_source_remove(vtklock->update_timestamp_id);
    vtklock->update_timestamp_id = 0;
  }

  ipm_hide_window(vtklock->window);
  gtk_widget_unrealize(vtklock->window);
}

static void
vtklock_window_set_no_transitions(GtkWidget *window)
{
  g_return_if_fail(window != NULL);

  set_gdk_property(window,
                   gdk_atom_intern_static_string(
                     "_HILDON_WM_ACTION_NO_TRANSITIONS"),
                   TRUE);
}

static void
vtklock_window_set_supports_portrait(GtkWidget *window)
{
  g_return_if_fail(window != NULL);

  set_gdk_property(window,
                   gdk_atom_intern_static_string(
                     "_HILDON_PORTRAIT_MODE_SUPPORT"),
                   TRUE);
  set_gdk_property(window,
                   gdk_atom_intern_static_string(
                     "_HILDON_PORTRAIT_MODE_REQUEST"),
                   TRUE);
}

static GtkWidget *
make_timestamp_box(vtklockts *ts, gboolean portrait)
{
  GtkWidget *box, *time_box, *date_box;
  GtkWidget *time_label, *date_label;

  PangoFontDescription *font_desc;

  g_assert(ts != NULL && ts->time_label == NULL && ts->date_label == NULL);

  if (portrait)
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

  time_label = gtk_label_new("");
  gtk_widget_modify_font(time_label, font_desc);

  date_label = gtk_label_new("");
  hildon_helper_set_logical_color(date_label, GTK_RC_FG, GTK_STATE_NORMAL,
                                  "SecondaryTextColor");
  hildon_helper_set_logical_color(date_label, GTK_RC_FG, GTK_STATE_PRELIGHT,
                                  "SecondaryTextColor");
  pango_font_description_free(font_desc);

  if (portrait)
  {
    gtk_label_set_angle(GTK_LABEL(date_label), 270.0);
    gtk_label_set_angle(GTK_LABEL(time_label), 270.0);
  }

  gtk_box_pack_start(GTK_BOX(time_box), time_label, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(date_box), date_label, TRUE, TRUE, 0);

  if (portrait)
  {
    gtk_box_pack_start(GTK_BOX(box), date_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), time_box, FALSE, FALSE, 0);
  }
  else
  {
    gtk_box_pack_start(GTK_BOX(box), time_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), date_box, FALSE, FALSE, 0);
  }

  ts->time_label = time_label;
  ts->date_label = date_label;

  return box;
}

static GtkWidget *
visual_tklock_create_slider(gboolean portrait)
{
  GtkWidget *slider;
  gint width;

  if (!portrait)
    width = (gdk_screen_get_width(gdk_screen_get_default()) * 440) / 800;
  else
    width = (gdk_screen_get_height(gdk_screen_get_default()) * 440) / 800;

  SYSTEMUI_DEBUG_FN;

  slider = portrait ? hildon_gtk_vscale_new() : hildon_gtk_hscale_new();
  g_object_set(slider, "jump-to-position", FALSE, NULL);

  gtk_widget_set_name(slider, portrait ?
                        "sui-tklock-slider-portrait" : "sui-tklock-slider");

  if (portrait)
    gtk_widget_set_size_request(slider, -1, width);
  else
    gtk_widget_set_size_request(slider, width, -1);

  gtk_range_set_update_policy(GTK_RANGE(slider), GTK_UPDATE_DISCONTINUOUS);
  gtk_range_set_range(GTK_RANGE(slider),0.0, 40.0);
  gtk_range_set_value(GTK_RANGE(slider), 3.0);

  return slider;
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

  if (index < G_N_ELEMENTS(icon_names))
    return icon_names[index];
  else
    return NULL;
}

static GtkWidget *
make_event_pair_box(int idx, int evcnt, gboolean portrait)
{
  GtkWidget *count_label,  *packer;
  char count_str[11];
  PangoFontDescription *font;
  GdkPixbuf *pixbuf;
  GtkWidget *image;
  const char *icon_name = get_icon_name(idx);

  g_assert(icon_name != NULL);

  font = pango_font_description_new();
  pango_font_description_set_family(font, "Nokia Sans");
  pango_font_description_set_absolute_size(font, 25 * PANGO_SCALE);

  g_assert(g_snprintf(count_str, sizeof(count_str) - 1, "%d", evcnt) != 0);

  count_label = gtk_label_new(count_str);

  g_assert(count_label);
  gtk_widget_modify_font(count_label, font);
  pango_font_description_free(font);

  pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), icon_name,
                                      48, GTK_ICON_LOOKUP_NO_SVG, NULL);

  if (portrait)
  {
    GdkPixbuf *rotated;

    gtk_label_set_angle(GTK_LABEL(count_label), 270.0);
    rotated = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
    g_object_unref(pixbuf);
    pixbuf = rotated;
  }

  g_assert(pixbuf != NULL);

  image = gtk_image_new_from_pixbuf(pixbuf);

  g_assert(image != NULL);

  g_object_unref(pixbuf);

  if (portrait)
    packer = gtk_vbox_new(TRUE, 0);
  else
    packer = gtk_hbox_new(TRUE, 0);

  g_assert(packer != NULL);

  gtk_box_pack_start(GTK_BOX(packer), image, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(packer), count_label, TRUE, TRUE, 0);

  return packer;
}

static GtkWidget *
make_missed_events_line(vtklock_t *vtklock, gboolean portrait)
{
  GtkWidget *align, *packer;
  int i;

  if (!event_count)
    return NULL;

  if (portrait)
    packer = gtk_vbox_new(TRUE, 40);
  else
    packer = gtk_hbox_new(TRUE, 40);

  g_assert(packer != NULL);

  for (i = 0; i < G_N_ELEMENTS(vtklock->event); i++)
  {
    int idx = event_idx[i];
    int evcnt = vtklock->event[idx].count;

    if (!evcnt)
      continue;

    gtk_box_pack_start(GTK_BOX(packer),
                       make_event_pair_box(idx, evcnt, portrait),
                       TRUE, TRUE, 0);
  }

  if (portrait)
    align = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
  else
    align = gtk_alignment_new(0.5, 0.0, 0.0, 0.0);

  gtk_container_add(GTK_CONTAINER(align), packer);

  return align;
}

static void
fill_background(vtklock_t *vtklock, gboolean portrait, gboolean fake)
{
  GdkPixbuf *pixbuf;
  GdkPixmap *bg_pixmap = NULL;

  if (portrait)
    pixbuf = gdk_pixbuf_new_from_file(LOCKSLIDER_PORTRAIT_BACKGROUND, NULL);
  else
    pixbuf = gdk_pixbuf_new_from_file(LOCKSLIDER_BACKGROUND, NULL);

  if (pixbuf)
  {
    int pw = gdk_pixbuf_get_width(pixbuf);
    int ph = gdk_pixbuf_get_height(pixbuf);
    GdkScreen *screen = gdk_screen_get_default();
    gint w = gdk_screen_get_width(screen);
    gint h = gdk_screen_get_height(screen);
    GtkStyle *style;

    if (fake)
    {

      GdkPixbuf *pixbuf_rotated;

      pixbuf_rotated = gdk_pixbuf_rotate_simple(pixbuf,
                                                GDK_PIXBUF_ROTATE_CLOCKWISE);
      g_object_unref(pixbuf);
      pixbuf = pixbuf_rotated;

    }
    /* TODO - check the condition in portrait mode */
    if (pw != w || ph != h)
    {
      GdkPixbuf *pixbuf_scaled =
          gdk_pixbuf_scale_simple(pixbuf, w, h, GDK_INTERP_BILINEAR);

      g_object_unref(pixbuf);
      pixbuf = pixbuf_scaled;
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
    style = gtk_style_copy(gtk_rc_get_style(vtklock->window));
    style->bg_pixmap[0] = bg_pixmap;
    gtk_widget_set_style(vtklock->window, style);
    g_object_unref(style);
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

void
visual_tklock_create_view_whimsy(vtklock_t *vtklock)
{
  GtkWidget *icon_packer_align = NULL;
  GtkWidget *label_align;
  GtkWidget *window_align;
  GtkWidget *label_packer;
  GtkWidget *slider_align;
  GtkWidget *timestamp_packer_align;
  GtkWidget *label;
  GtkWidget *timestamp_packer;
  gboolean force_fake_portrait;
  GtkRequisition sr;
  GConfClient *gc;

  SYSTEMUI_DEBUG_FN;

  if (vtklock->window)
    return;

  get_missed_events_from_db(vtklock);

  force_fake_portrait = gdk_screen_height() > gdk_screen_width();

  vtklock->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(vtklock->window), "visual_tklock");
  gtk_window_set_decorated(GTK_WINDOW(vtklock->window), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(vtklock->window), TRUE);

  gc = gconf_client_get_default();

  /* check if autorotation is enabled */
  if (gc && gconf_client_get_bool(gc, "/system/systemui/tklock/auto_rotation", NULL) )
  {
    /* Check if we have force_fake_portrait lockslider background */
    if (access("/etc/hildon/theme/backgrounds/lockslider-portrait.png", R_OK) == 0)
    {
      hildon_gtk_window_set_portrait_flags(GTK_WINDOW(vtklock->window),
                                           HILDON_PORTRAIT_MODE_SUPPORT);
      g_signal_connect(G_OBJECT(vtklock->window), "configure-event",
                       G_CALLBACK(configure_event_cb), vtklock);
      fill_background(vtklock, force_fake_portrait, FALSE);
      force_fake_portrait = FALSE;
    }
    else
      fill_background(vtklock, FALSE, force_fake_portrait);
  }
  else
    fill_background(vtklock, FALSE, force_fake_portrait);

  if (gc)
    g_object_unref(gc);

  vtklock->slider = visual_tklock_create_slider(force_fake_portrait);
  vtklock->slider_status = 1;

  slider_align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  gtk_container_add(GTK_CONTAINER(slider_align), vtklock->slider);

  vtklock->slider_adjustment =
      gtk_range_get_adjustment(GTK_RANGE(vtklock->slider));

  g_assert(vtklock->slider_adjustment != NULL);

  reset_slider(vtklock);

  label = gtk_label_new(dgettext("osso-system-lock", "secu_swipe_to_unlock"));
  hildon_helper_set_logical_font(label, "SystemFont");

  if (force_fake_portrait)
    gtk_label_set_angle(GTK_LABEL(label), 270.0);

  timestamp_packer = make_timestamp_box(&vtklock->ts, force_fake_portrait);

  g_assert(timestamp_packer != NULL);

  update_timestamp(&vtklock->ts);

  if (force_fake_portrait)
    timestamp_packer_align = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
  else
    timestamp_packer_align = gtk_alignment_new(0.5, 1.0, 0.0, 0.0);

  gtk_container_add(GTK_CONTAINER(timestamp_packer_align), timestamp_packer);

  icon_packer_align = make_missed_events_line(vtklock, force_fake_portrait);
  window_align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);

  if (force_fake_portrait)
  {
    label_align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(label_align), label);
    label_packer = gtk_hbox_new(FALSE, 24);
  }
  else
  {
    label_align = gtk_alignment_new(0.5, 0.0, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(label_align), label);
    label_packer = gtk_vbox_new(FALSE, 24);
  }

  if (force_fake_portrait)
  {
    gtk_box_pack_end(
          GTK_BOX(label_packer), timestamp_packer_align, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(label_packer), slider_align, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(label_packer), label_align, FALSE, FALSE, 0);

    if (event_count)
    {
      gtk_box_pack_end(
            GTK_BOX(label_packer), icon_packer_align, FALSE, FALSE, 0);
    }

    window_align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(window_align), 8, 24, 1, 0);
  }
  else
  {
    gtk_box_pack_start(
          GTK_BOX(label_packer), timestamp_packer_align, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(label_packer), slider_align, FALSE, FALSE, 0);

    if (event_count)
    {
      gtk_box_pack_end(
            GTK_BOX(label_packer), icon_packer_align, FALSE, FALSE, 0);
    }

    gtk_box_pack_end(GTK_BOX(label_packer), label_align, FALSE, FALSE, 0);
    window_align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(window_align), 0, 0, 0, 16);
  }

  gtk_container_add(GTK_CONTAINER(window_align), label_packer);
  gtk_container_add(GTK_CONTAINER(vtklock->window), window_align);

  g_signal_connect(vtklock->slider, "change-value",
                   G_CALLBACK(change_value_cb), vtklock);
  g_signal_connect(vtklock->slider, "value-changed",
                   G_CALLBACK(value_changed_cb), vtklock);
  g_signal_connect(vtklock->window, "key-press-event",
                   G_CALLBACK(vtklock_key_press_event_cb), vtklock);
  g_signal_connect(vtklock->window, "key-release-event",
                   G_CALLBACK(vtklock_key_press_event_cb), vtklock);

  gtk_widget_show_all(window_align);

  gtk_widget_size_request(vtklock->slider, &sr);

  if (event_count)
  {
    if (force_fake_portrait)
    {
      gtk_alignment_set_padding(GTK_ALIGNMENT(icon_packer_align),
                                0,
                                0,
                                30,
                                60 - sr.width / 2);
    }
    else
    {
      gtk_alignment_set_padding(GTK_ALIGNMENT(icon_packer_align),
                                30,
                                60 - sr.height / 2,
                                0,
                                0);
    }
  }
  else
  {
    GtkRequisition r;

    gtk_widget_size_request(label, &r);

    if (force_fake_portrait)
    {
      gtk_alignment_set_padding(GTK_ALIGNMENT(label_align),
                                0,
                                0,
                                (abs(480 - sr.width) / 2 - 48)-r.width,
                                0);
    }
    else
    {
      gtk_alignment_set_padding(GTK_ALIGNMENT(label_align),
                                0,
                                (abs(480 - sr.height) / 2 - 48) - r.height,
                                0,
                                0);
    }
  }

  gtk_widget_realize(vtklock->window);

  vtklock_window_set_no_transitions(vtklock->window);

  gtk_window_fullscreen(GTK_WINDOW(vtklock->window));
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(vtklock->window), TRUE);
  hildon_gtk_window_set_do_not_disturb(GTK_WINDOW(vtklock->window), TRUE);

  if (force_fake_portrait)
    vtklock_window_set_supports_portrait(vtklock->window);

  gtk_widget_show_all(vtklock->window);

  install_dbus_handlers(vtklock);
}
