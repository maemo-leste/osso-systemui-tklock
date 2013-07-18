/**
   @file eventeater.h

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
#ifndef _SYSTEMUI_TKLOCK_EVENT_EATER_H
#define _SYSTEMUI_TKLOCK_EVENT_EATER_H

void
ee_create_window(tklock_plugin_data *plugin_data);

void
ee_destroy_window();


#endif  /* _SYSTEMUI_TKLOCK_EVENT_EATER_H */
