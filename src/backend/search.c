/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "showtime.h"
#include "settings.h"
#include "event.h"
#include "navigator.h"
#include "prop/prop_nodefilter.h"
#include "backend/backend.h"
#include "backend/backend_prop.h"
#include "backend/search.h"


/**
 *
 */
int
search_class_create(prop_t *parent, prop_t **nodesp, prop_t **entriesp,
		    const char *title, const char *icon)
{
  prop_t *p = prop_create_root(NULL);
  prop_t *m = prop_create(p, "metadata");
  prop_t *n, *e;
  
  prop_set(p, "url", PROP_ADOPT_RSTRING, backend_prop_make(p, NULL));

  prop_set(m, "title", PROP_SET_STRING, title);
  if(icon != NULL)
    prop_set(m, "icon", PROP_SET_STRING, icon);
  prop_set(p, "type", PROP_SET_STRING, "directory");
      
  n = prop_create(p, "nodes");
  e = prop_create(p, "entries");
  prop_set_int(e, 0);

  *nodesp = prop_ref_inc(n);
  *entriesp = prop_ref_inc(e);

  if(prop_set_parent(p, parent)) {
    prop_destroy(p);
    return 1;
  }
  return 0;
}


/**
 *
 */
prop_t *
search_get_settings(void)
{
  static prop_t *p;

  if(p == NULL)
    p = settings_add_dir(NULL, _p("Search"), "search", NULL, NULL,
			 "settings:search");
  return p;
}


/**
 *
 */
static int
search_canhandle(const char *url)
{
  return !strncmp(url, "search:", strlen("search:"));
}


/**
 *
 */
static int
search_open(prop_t *page, const char *url0, int sync)
{
  const char *url;
  prop_t *model, *meta, *source;
  char title[256];
  if((url = strchr(url0, ':')) == NULL)
    abort();
  url++;

  if(!backend_open(page, url, sync))
    return 0;
  
  model = prop_create_r(page, "model");
  prop_set(model, "type", PROP_SET_STRING, "directory");
  
  meta = prop_create_r(model, "metadata");
  rstr_t *fmt = _("Search result for: %s");
  snprintf(title, sizeof(title), rstr_get(fmt), url);
  rstr_release(fmt);
  prop_set(meta, "title", PROP_SET_STRING, title);


  source = prop_create_r(page, "source");
  prop_t *model_nodes  = prop_create_r(model, "nodes");
  prop_t *source_nodes = prop_create_r(source, "nodes");
  prop_t *loading      = prop_create_r(model, "loading");
  struct prop_nf *pnf;

  pnf = prop_nf_create(model_nodes, source_nodes,
		       NULL, PROP_NF_AUTODESTROY);

  prop_nf_sort(pnf, "node.metadata.title", 0, 2, NULL, 1);

  prop_nf_pred_int_add(pnf, "node.entries",
		       PROP_NF_CMP_EQ, 0, NULL, 
		       PROP_NF_MODE_EXCLUDE);

  prop_nf_release(pnf);

  backend_search(source, url, loading);

  prop_ref_dec(model);
  prop_ref_dec(meta);
  prop_ref_dec(source);
  prop_ref_dec(model_nodes);
  prop_ref_dec(source_nodes);
  prop_ref_dec(loading);
  return 0;
}

/**
 *
 */
static backend_t be_search = {
  .be_canhandle = search_canhandle,
  .be_open = search_open,
};

BE_REGISTER(search);
