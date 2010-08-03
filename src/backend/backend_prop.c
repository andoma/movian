/*
 *  Backend page -> prop
 *  Copyright (C) 2010 Andreas Öman
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

#include <stdio.h>

#include "showtime.h"
#include "backend.h"
#include "backend_prop.h"
#include "prop/prop.h"
#include "navigator.h"


LIST_HEAD(proppage_list, proppage);

static struct proppage_list proppages;
static hts_mutex_t pp_mutex;
static int pp_tally;


/**
 *
 */
typedef struct proppage {
  LIST_ENTRY(proppage) pp_link;

  prop_sub_t *pp_model_sub;
  prop_t *pp_model;

  char *pp_url;

} proppage_t;



/**
 *
 */
static void
pp_cb(void *opaque, prop_event_t event, ...)
{
  proppage_t *pp = opaque;

  if(event != PROP_DESTROYED) 
    return;

  printf("Metapage %s destroyed\n", pp->pp_url);

  LIST_REMOVE(pp, pp_link);
  prop_ref_dec(pp->pp_model);
  prop_unsubscribe(pp->pp_model_sub);
  free(pp->pp_url);
  free(pp);
}


/**
 *
 */
void
backend_prop_make(prop_t *model, char *url, size_t urllen)
{
  proppage_t *pp;

  hts_mutex_lock(&pp_mutex);

  pp = malloc(sizeof(proppage_t));

  pp_tally++;

  snprintf(url, urllen, "prop:%d", pp_tally);
  pp->pp_url = strdup(url);

  prop_ref_inc(model);
  pp->pp_model = model;

  pp->pp_model_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, pp_cb, pp,
		   PROP_TAG_MUTEX, &pp_mutex,
		   PROP_TAG_ROOT, model,
		   NULL);

  LIST_INSERT_HEAD(&proppages, pp, pp_link);
  hts_mutex_unlock(&pp_mutex);
}




/**
 *
 */
static nav_page_t *
be_prop_open(struct backend *be, struct navigator *nav,  const char *url,
	     const char *view, char *errbuf, size_t errlen)
{
  proppage_t *pp;
  nav_page_t *n;

  hts_mutex_lock(&pp_mutex);

  LIST_FOREACH(pp, &proppages, pp_link)
    if(!strcmp(pp->pp_url, url))
      break;

  if(pp == NULL) {
    n = BACKEND_NOURI;
  } else {
    n = nav_page_create(nav, url, view, NAV_PAGE_DONT_CLOSE_ON_BACK);
    prop_link(pp->pp_model, prop_create(n->np_prop_root, "model"));
  }
  hts_mutex_unlock(&pp_mutex);
  return n;
}


/**
 *
 */
static int
be_prop_init(void)
{
  hts_mutex_init(&pp_mutex);
  return 0;
}


/**
 *
 */
static backend_t be_prop = {
  .be_init = be_prop_init,
  .be_flags = BACKEND_OPEN_CHECKS_URI,
  .be_open = be_prop_open,
};

BE_REGISTER(prop);
