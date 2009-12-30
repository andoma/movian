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

#include <stdarg.h>
#include <stdio.h>

#include "showtime.h"
#include "prop.h"
#include "notifications.h"
#include "misc/callout.h"

static prop_t *notify_prop_entries;

/**
 *
 */
void
notifications_init(void)
{
  prop_t *root = prop_create(prop_get_global(), "notifications");
  
  notify_prop_entries = prop_create(root, "nodes");

}

/**
 *
 */
static void
notify_timeout(callout_t *c, void *aux)
{
  prop_t *p = aux;
  prop_destroy(p);
  free(c);
}

/**
 *
 */
void *
notify_add(notify_type_t type, const char *icon, int delay,
	   const char *fmt, ...)
{
  char msg[256];
  prop_t *p;
  const char *typestr;
  int tl;
  va_list ap, apx;

  switch(type) {
  case NOTIFY_INFO:    typestr = "info";    tl = TRACE_INFO;  break;
  case NOTIFY_WARNING: typestr = "warning"; tl = TRACE_INFO;  break;
  case NOTIFY_ERROR:   typestr = "error";   tl = TRACE_ERROR; break;
  default: return NULL;
  }
  
  va_start(ap, fmt);
  va_copy(apx, ap);

  tracev(tl, "notify", fmt, ap);

  vsnprintf(msg, sizeof(msg), fmt, apx);

  va_end(ap);
  va_end(apx);

  p = prop_create(NULL, NULL);

  prop_set_string(prop_create(p, "text"), msg);
  prop_set_string(prop_create(p, "type"), typestr);

  if(icon != NULL)
    prop_set_string(prop_create(p, "icon"), icon);

  if(prop_set_parent(p, notify_prop_entries))
    abort();

  prop_select(p, 0);
  
  if(delay != 0) {
    callout_arm(NULL, notify_timeout, p, delay);
    return NULL;
  }

  return p;
}

/**
 *
 */
void
notify_destroy(void *p)
{
  prop_destroy(p);
}
