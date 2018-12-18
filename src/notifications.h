/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#ifndef NOTIFICATIONS_H__
#define NOTIFICATIONS_H__

#include "misc/rstr.h"
struct prop;

typedef enum {
  NOTIFY_INFO,
  NOTIFY_WARNING,
  NOTIFY_ERROR,
} notify_type_t;

void *notify_add(struct prop *root, notify_type_t type, const char *icon,
                 int delay, rstr_t *fmt, ...);

void notify_destroy(void *);

void notifications_init(void);

void notifications_fini(void);

// Displays popup defined by proptree 'p' and return event
struct event;
struct event *popup_display(struct prop *p);

#define MESSAGE_POPUP_OK        0x1000
#define MESSAGE_POPUP_CANCEL    0x2000
#define MESSAGE_POPUP_RICH_TEXT 0x4000

int message_popup(const char *message, int flags, const char **extra);

int text_dialog(const char *message, char** string, int flags);

struct prop *add_news(const char *id, const char *message,
                      const char *location, const char *caption);

#endif // NOTIFICATIONS_H__
