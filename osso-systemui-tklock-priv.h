/**
   @file osso-systemui-tklock-priv.h

   @brief Maemo systemui tklock plugin private definitions

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
#ifndef _SYSTEMUI_TKLOCK_PRIVATE_H
#define _SYSTEMUI_TKLOCK_PRIVATE_H

#include <systemui/tklock-dbus-names.h>

typedef struct {
  GtkWidget *time_label;
  GtkWidget *date_label;
} vtklockts;

typedef struct {
  guint count;
  guint hint;
}event_t;

typedef struct {
  GtkWidget *window;
  vtklockts ts;
  GtkWidget *slider;
  guint slider_status;
  gdouble slider_value;
  GtkAdjustment *slider_adjustment;
  DBusConnection *systemui_conn;
  int priority;
  guint update_date_time_cb_tag;
  void(*unlock_handler)();
  event_t event[6];
  gulong slider_value_changed_id;
  gulong slider_change_value_id;
}vtklock_t;

typedef struct{
  system_ui_data *data;
  void (*one_input_mode_finished_handler)();
  guint one_input_mode_event;
  tklock_mode mode;
  vtklock_t *vtklock;
} tklock_plugin_data;

#endif /* _SYSTEMUI_TKLOCK_PRIVATE_H */
