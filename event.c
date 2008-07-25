/*
 *  Input handling
 *  Copyright (C) 2007 Andreas Öman
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

#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "showtime.h"
#include "event.h"

static hts_mutex_t ehmutex;
static LIST_HEAD(, event_handler) event_handlers;


/**
 *
 */
void
event_init(void)
{
  hts_mutex_init(&ehmutex);
}

/**
 *  Root input handler & events (from keyboard, remote controls, etc)
 */

typedef struct event_handler {
  LIST_ENTRY(event_handler) link;
  int pri;
  int (*callback)(glw_event_t *ge);
} event_handler_t;


/**
 *
 */
static int 
ihcmp(event_handler_t *a, event_handler_t *b)
{
  return b->pri - a->pri;
}


/**
 *
 */
void
event_handler_register(int pri, int (*callback)(glw_event_t *ge))
{
  event_handler_t *ih;
  ih = malloc(sizeof(event_handler_t));

  ih->pri = pri;
  ih->callback = callback;
  hts_mutex_lock(&ehmutex);
  LIST_INSERT_SORTED(&event_handlers, ih, link, ihcmp);
  hts_mutex_unlock(&ehmutex);
}




/**
 *
 */
void
event_post(glw_event_t *ge)
{
  event_handler_t *eh;

  hts_mutex_lock(&ehmutex);

  LIST_FOREACH(eh, &event_handlers, link) {
    if(eh->callback(ge))
      break;
  }
  hts_mutex_unlock(&ehmutex);

  glw_event_unref(ge);
}


/**
 *
 */
void
event_post_simple(event_type_t type)
{
  glw_event_t *ge = glw_event_create(type, sizeof(glw_event_t));
  event_post(ge);
}
