/*
 * tklock-grab.h
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

#ifndef __TKLOCK_GRAB_H__
#define __TKLOCK_GRAB_H__

gboolean tklock_grab_try(GdkWindow *window, gboolean owner_events,
                         GdkEventMask event_mask, GdkWindow *confine_to);
void tklock_grab_release();
void tklock_unlock(DBusConnection *conn);

#endif /* __TKLOCK_GRAB_H__ */
