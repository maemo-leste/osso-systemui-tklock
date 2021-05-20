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

gboolean
tklock_grab_try(GdkWindow *window, GdkWindow *confine_to)
{
  GdkGrabStatus status;

  status = gdk_pointer_grab(window, FALSE,
                            GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
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
