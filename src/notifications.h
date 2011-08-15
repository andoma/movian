/*
 *  Notifications
 *  Copyright (C) 2009 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NOTIFICATIONS_H__
#define NOTIFICATIONS_H__

typedef enum {
  NOTIFY_INFO,
  NOTIFY_WARNING,
  NOTIFY_ERROR,
} notify_type_t;

void *notify_add(prop_t *root, notify_type_t type, const char *icon, int delay,
		 const char *fmt, ...);

void notify_destroy(void *);

void notifications_init(void);

// Displays popup defined by proptree 'p' and return event
struct event;
struct event *popup_display(prop_t *p);

#define MESSAGE_POPUP_OK     0x1
#define MESSAGE_POPUP_CANCEL 0x2
#define MESSAGE_POPUP_RICH_TEXT 0x4

int message_popup(const char *message, int flags);

int text_dialog(const char *message, char** string, int flags);
 
#endif // NOTIFICATIONS_H__
