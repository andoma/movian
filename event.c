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
#include <libhts/hts_strtab.h>

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
 *
 */
static void
event_default_dtor(event_t *e)
{
  free(e);
}

/**
 *
 */
void *
event_create(event_type_t type, size_t size)
{
  event_t *e = malloc(size);
  e->e_dtor = event_default_dtor;
  e->e_refcount = 1;
  e->e_mapped = 0;
  e->e_type = type;
  return e;
}

/**
 *
 */
void *
event_create_unicode(int sym)
{
  event_unicode_t *e = malloc(sizeof(event_unicode_t));
  e->h.e_dtor = event_default_dtor;
  e->h.e_refcount = 1;
  e->h.e_type = EVENT_UNICODE;
  e->sym = sym;
  return e;
}


/**
 *
 */
void
event_enqueue(event_queue_t *eq, event_t *e)
{
  atomic_add(&e->e_refcount, 1);
  hts_mutex_lock(&eq->eq_mutex);
  TAILQ_INSERT_TAIL(&eq->eq_q, e, e_link);
  hts_cond_signal(&eq->eq_cond);
  hts_mutex_unlock(&eq->eq_mutex);
}


/**
 *
 * @param timeout Timeout in milliseconds
 */
event_t *
event_get(int timeout, event_queue_t *eq)
{
  event_t *e;

  hts_mutex_lock(&eq->eq_mutex);

  if(timeout == 0) {
    e = TAILQ_FIRST(&eq->eq_q);
  } else if(timeout == -1) {
    while((e = TAILQ_FIRST(&eq->eq_q)) == NULL)
      hts_cond_wait(&eq->eq_cond, &eq->eq_mutex);
  } else {
    while((e = TAILQ_FIRST(&eq->eq_q)) == NULL) {
      if(hts_cond_wait_timeout(&eq->eq_cond, &eq->eq_mutex, timeout))
	break;
    }
  }

  if(e != NULL)
    TAILQ_REMOVE(&eq->eq_q, e, e_link);
  hts_mutex_unlock(&eq->eq_mutex);
  return e;
}


/**
 *
 */
void
event_unref(event_t *e)
{
  if(atomic_add(&e->e_refcount, -1) == 1)
    e->e_dtor(e);
}


/**
 *
 */
void
event_initqueue(event_queue_t *eq)
{
  TAILQ_INIT(&eq->eq_q);
  hts_cond_init(&eq->eq_cond);
  hts_mutex_init(&eq->eq_mutex);
}


/**
 *
 */
void
event_flushqueue(event_queue_t *eq)
{
  event_t *e;

  hts_mutex_lock(&eq->eq_mutex);

  while((e = TAILQ_FIRST(&eq->eq_q)) != NULL) {
    TAILQ_REMOVE(&eq->eq_q, e, e_link);
    event_unref(e);
  }
  hts_mutex_unlock(&eq->eq_mutex);
}










/**
 *  Root input handler & events (from keyboard, remote controls, etc)
 */
typedef struct event_handler {
  const char *name;
  void *opaque;
  int pri;
  int (*callback)(event_t *ge, void *opaque);
  LIST_ENTRY(event_handler) link;
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
void *
event_handler_register(const char *name, int (*callback)(event_t *ge,
							 void *opaque), 
		       eventpri_t pri, void *opaque)
{
  event_handler_t *ih;
  ih = malloc(sizeof(event_handler_t));

  ih->name = name;
  ih->pri = pri;
  ih->callback = callback;
  ih->opaque = opaque;
  hts_mutex_lock(&ehmutex);
  LIST_INSERT_SORTED(&event_handlers, ih, link, ihcmp);
  hts_mutex_unlock(&ehmutex);
  return ih;
}

/**
 *
 */
void
event_handler_unregister(void *IH)
{
  event_handler_t *ih = IH;

  hts_mutex_lock(&ehmutex);
  LIST_REMOVE(ih, link);
  hts_mutex_unlock(&ehmutex);
  free(ih);
}



/**
 *
 */
void
event_post(event_t *e)
{
  event_handler_t *eh;
  int r;

  hts_mutex_lock(&ehmutex);

  LIST_FOREACH(eh, &event_handlers, link) {
    
    r = eh->callback(e, eh->opaque);
    if(r)
      break;
  }

  hts_mutex_unlock(&ehmutex);

  event_unref(e);
}


/**
 *
 */
void
event_post_simple(event_type_t type)
{
  event_post(event_create(type, sizeof(event_t)));
}


/**
 * Destroy a sys signal
 */
void
event_generic_dtor(event_t *e)
{
  event_generic_t *g = (void *)e;
  free(g->target);
  free(g->method);
  free(g->argument);
  free(g);
}




/**
 *
 */

static struct strtab eventnames[] = {
  { "Up",                    EVENT_UP },
  { "Down",                  EVENT_DOWN },
  { "Left",                  EVENT_LEFT },
  { "Right",                 EVENT_RIGHT },
  { "Enter",                 EVENT_ENTER },
  { "Incr",                  EVENT_INCR },
  { "Decr",                  EVENT_DECR },
  { "Ok",                    EVENT_OK },
  { "Cancel",                EVENT_CANCEL },
  { "Close",                 EVENT_KEY_CLOSE },
  { "Stop",                  EVENT_KEY_STOP },
  { "PlayPause",             EVENT_KEY_PLAYPAUSE },
  { "Play",                  EVENT_KEY_PLAY },
  { "Pause",                 EVENT_KEY_PAUSE },
  { "VolumeUp",              EVENT_KEY_VOLUME_UP },
  { "VolumeDown",            EVENT_KEY_VOLUME_DOWN },
  { "VolumeMuteToggle",      EVENT_KEY_VOLUME_MUTE_TOGGLE },
  { "Menu",                  EVENT_KEY_MENU },
  { "Back",                  EVENT_BACKSPACE },
  { "Select",                EVENT_KEY_SELECT },
  { "Eject",                 EVENT_KEY_EJECT },
  { "Power",                 EVENT_KEY_POWER },
  { "Previous",              EVENT_KEY_PREV },
  { "Next",                  EVENT_KEY_NEXT },
  { "SeekForward",           EVENT_KEY_SEEK_FORWARD },
  { "SeekReverse",           EVENT_KEY_SEEK_BACKWARD },
  { "Quit",                  EVENT_KEY_QUIT },
  { "MainMenu",              EVENT_KEY_MAINMENU },
  { "ChangeView",            EVENT_KEY_SWITCH_VIEW },
  { "Channel+",              EVENT_KEY_CHANNEL_PLUS },
  { "Channel-",              EVENT_KEY_CHANNEL_MINUS },  
};



const char *
event_code2str(event_type_t code)
{
  return val2str(code, eventnames);
}

event_type_t
event_str2code(const char *str)
{
  return str2val(str, eventnames);
}

