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

static prop_t *notify_prop_root;

/**
 *
 */
void
notifications_init(void)
{
  notify_prop_root = prop_create(prop_get_global(), "notifications");
}

/**
 *
 */
static void
notify_timeout(deferred_t *d, void *aux)
{
  prop_t *p = aux;
  prop_destroy(p);
  free(d);
}

/**
 *
 */
void *
notify_add(notify_type_t type, const char *icon, int delay,
	   const char *fmt, ...)
{
  deferred_t *d;
  char msg[256];
  prop_t *p;
  const char *typestr;

  va_list ap;

  switch(type) {
  case NOTIFY_INFO:    typestr = "info";    break;
  case NOTIFY_WARNING: typestr = "warning"; break;
  case NOTIFY_ERROR:   typestr = "error";   break;
  default: return NULL;
  }
  
  va_start(ap, fmt);

  vsnprintf(msg, sizeof(msg), fmt, ap);

  va_end(ap);

  p = prop_create(NULL, NULL);

  prop_set_string(prop_create(p, "text"), msg);
  prop_set_string(prop_create(p, "type"), typestr);

  if(icon != NULL)
    prop_set_string(prop_create(p, "icon"), icon);

  if(prop_set_parent(p, notify_prop_root))
    abort();

  if(delay != 0) {
    d = calloc(1, sizeof(deferred_t));
    deferred_arm(d, notify_timeout, p, delay);
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
