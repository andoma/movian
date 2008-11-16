/*
 *  Navigator
 *  Copyright (C) 2008 Andreas Öman
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <libglw/glw.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"
#include "event.h"

static hts_mutex_t nav_mutex;

static struct nav_backend_list nav_backends;
static struct nav_page_queue nav_pages;
static struct nav_page_queue nav_history;

static nav_page_t *nav_page_current;

static hts_prop_t *nav_prop_root;
static hts_prop_t *nav_prop_path;
static hts_prop_t *nav_prop_pages;
static hts_prop_t *nav_prop_curpage;
static glw_event_queue_t nav_geq;

static void *navigator_thread(void *aux);
static int nav_input_event(glw_event_t *ge, void *opaque);


/**
 *
 */
static void
nav_init_be(nav_backend_t *be)
{
  LIST_INSERT_HEAD(&nav_backends, be, nb_global_link);
}



/**
 *
 */
void
nav_init(void)
{
  hts_thread_t tid;

  hts_mutex_init(&nav_mutex);

  TAILQ_INIT(&nav_pages);
  TAILQ_INIT(&nav_history);
  event_handler_register("navigator", nav_input_event, EVENTPRI_NAV, NULL);

  nav_prop_root    = hts_prop_create(hts_prop_get_global(), "nav");
  nav_prop_path    = hts_prop_create(nav_prop_root, "path");
  nav_prop_pages   = hts_prop_create(nav_prop_root, "pages");
  nav_prop_curpage = hts_prop_create(nav_prop_root, "currentpage");

  glw_event_initqueue(&nav_geq);

#define NAV_INIT_BE(name) \
 {extern nav_backend_t be_ ## name; nav_init_be(&be_ ## name);}

  NAV_INIT_BE(page);
  NAV_INIT_BE(file);
  NAV_INIT_BE(settings);

  hts_thread_create(&tid, navigator_thread, NULL);
}


/**
 *
 */
static void
nav_remove_from_history(nav_page_t *np)
{
  np->np_inhistory = 0;
  TAILQ_REMOVE(&nav_history, np, np_history_link);
}


/**
 *
 */
void
nav_close(nav_page_t *np)
{
#if 0
  LIST_REMOVE(np, np_global_link);
  hts_prop_destroy(np->np_prop_root);
  free(np);
#endif
}


/**
 *
 */
void
nav_open(const char *url)
{
  nav_page_t *np, *np2;
  nav_backend_t *nb;
  char errbuf[128];

  /* First, if a page is already open, go directly to it */

  printf("Opening %s\n", url);

  TAILQ_FOREACH(np, &nav_pages, np_global_link) {
    if(!strcmp(np->np_url, url)) {
      printf("Selecting %s\n", np->np_url);
      hts_prop_select(np->np_prop_root, 0);

      hts_prop_link(np->np_prop_root, nav_prop_curpage);
      break;
    }
  }

  if(np == NULL) {

    LIST_FOREACH(nb, &nav_backends, nb_global_link)
      if(nb->nb_canhandle(url))
	break;
  
    if(nb == NULL)
      return;

    np = nb->nb_open(url, errbuf, sizeof(errbuf));
  
    if(np == NULL)
      return;

    hts_prop_set_parent(np->np_prop_root, nav_prop_pages);
    hts_prop_link(np->np_prop_root, nav_prop_curpage);
  }

  if(np->np_inhistory == 0) {
    if(nav_page_current != NULL) {
      
      /* Destroy any previous "future" histories,
       * this happens if we back a few times and then jumps away
       * in another "direction"
       */
      
      while((np2 = TAILQ_NEXT(nav_page_current, np_history_link)) != NULL)
	nav_remove_from_history(np2);
    }

    TAILQ_INSERT_TAIL(&nav_history, np, np_history_link);
    np->np_inhistory = 1;
  }

  nav_page_current = np;
}


/**
 *
 */
static void
nav_back(void)
{
  nav_page_t *np = nav_page_current;

  if(np == NULL || 
     (np = TAILQ_PREV(np, nav_page_queue, np_history_link)) == NULL)
    return;
  
  nav_open(np->np_url);
}


/**
 *
 */
void *
nav_page_create(struct nav_backend *be, const char *url, size_t allocsize)
{
  nav_page_t *np = calloc(1, allocsize);

  glw_event_initqueue(&np->np_geq);
  np->np_url = strdup(url);
  np->np_be = be;

  TAILQ_INSERT_TAIL(&nav_pages, np, np_global_link);

  np->np_prop_root = hts_prop_create(NULL, "page");

  hts_prop_set_string(hts_prop_create(np->np_prop_root, "url"), url);
  return np;
}


/**
 *
 */
static int
nav_input_event(glw_event_t *ge, void *opaque)
{
  glw_event_sys_t *sys;

  switch(ge->ge_type) {
  case GEV_SYS:
    sys = (glw_event_sys_t *)ge;
    if(!strcmp(sys->target, "navigator"))
      break;
    return 0;

  case GEV_BACKSPACE:
    break;

  default:
    return 0;
  }

  glw_event_enqueue(&nav_geq, ge);
  return 1;
}


/**
 *
 */
static void *
navigator_thread(void *aux)
{
  glw_event_t *ge;
  glw_event_sys_t *sys;
 
  while(1) {
    ge = glw_event_get(-1, &nav_geq);

    switch(ge->ge_type) {
    default:
      break;
      
    case GEV_BACKSPACE:
      nav_back();
      break;


    case GEV_SYS:
      sys = (glw_event_sys_t *)ge;
    
      if(!strcmp(sys->method, "open"))
	nav_open(sys->argument);
      
      if(!strcmp(sys->method, "back"))
	nav_back();
      break;
    }

    glw_event_unref(ge);
  }
}


