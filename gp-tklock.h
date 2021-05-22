/**
   @file gp-tklock.h

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

#ifndef __GP_LOCK_H_INCLUDED__
#define __GP_LOCK_H_INCLUDED__

typedef enum
{
  TKLOCK_GRAB_DISABLED,
  TKLOCK_GRAB_ENABLED,
  TKLOCK_GRAB_FAILED
} tklock_grab_status;

typedef enum
{
  TKLOCK_ONE_INPUT_DISABLED,
  TKLOCK_ONE_INPUT_BUTTON_PRESSED,
  TKLOCK_ONE_INPUT_BUTTON_RELEASED
} tklock_one_input_status;

typedef struct
{
  GtkWidget *window;
  guint grab_notify;
  tklock_grab_status grab_status;
  gboolean one_input;
  tklock_one_input_status one_input_status;
  void (*one_input_mode_finished_handler)();
  gulong btn_press_id;
  gulong btn_release_id;
  DBusConnection *systemui_conn;
  gboolean disabled;
} gp_tklock_t;

void gp_tklock_create_window(gp_tklock_t *gp_tklock);
void gp_tklock_enable_lock(gp_tklock_t *gp_tklock);
gp_tklock_t *gp_tklock_init(DBusConnection *conn);
void gp_tklock_destroy_lock(gp_tklock_t *gp_tklock);
void gp_tklock_disable_lock(gp_tklock_t *gp_tklock, gboolean release_gdk_grabs);
void gp_tklock_destroy(gp_tklock_t *gp_tklock);
void gp_tklock_set_one_input_mode_handler(gp_tklock_t *gp_tklock,
                                          void (*handler)());

#endif /* __GP_LOCK_H_INCLUDED__ */
