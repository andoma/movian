/*
 *  Headweb backend
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
#include <unistd.h>

#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_xml.h"
#include "navigator.h"
#include "backend/backend.h"
#include "arch/threads.h"
#include "misc/string.h"
#include "service.h"
#include "settings.h"


#define HEADWEB_URL_ROOT "http://api.headweb.com/v4"
#define HEADWEB_QUERY_LIMIT 100

/**
 * Headweb API KEY.
 * Please dont steal Showtime's key..
 * Send an email to api@headweb.com and you'll get your own for free,
 * documentation can be found at http://opensource.headweb.com/api
 */
#define HEADWEB_APIKEY "2d6461dd322b4b84b5bac8c654ee6195"

static int headweb_is_enabled;

//static int headweb_req_counter;

typedef struct headweb_browse {

  int hb_run;

  prop_courier_t *hb_pc;

  char *hb_url;   // URL 

  prop_t *hb_nodes;
  prop_t *hb_loading;

  prop_sub_t *hb_nodesub;

  int hb_total_items;
  int hb_loaded_items;

  int hb_req_flags;

} headweb_browse_t;


/**
 *
 */
static void
headweb_browse_fill_content(headweb_browse_t *hb, htsmsg_t *c)
{
  prop_t *p, *m;
  const char *title, *streamid = NULL, *runtime = NULL, *str;
  htsmsg_field_t *f;
  htsmsg_t *s;
  int best_cover_area = 0;
  const char *best_cover = NULL;
  
  if((c = htsmsg_get_map(c, "tags")) == NULL)
    return;

  title = htsmsg_get_str_multi(c, "name", "cdata", NULL);

  if(title == NULL)
    return;
  
  HTSMSG_FOREACH(f, c) {
    if(strcmp(f->hmf_name, "cover") ||
       (s = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    const char *w = htsmsg_get_str_multi(s, "attrib", "width", NULL);
    const char *h = htsmsg_get_str_multi(s, "attrib", "height", NULL);
    const char *cover = htsmsg_get_str(s, "cdata");

    if(w == NULL || h == NULL || cover == NULL)
      continue;

    int a = atoi(w) * atoi(h);
    if(a < best_cover_area)
      continue;

    best_cover = cover;
    best_cover_area = a;
  }


  HTSMSG_FOREACH(f, c) {
    if(strcmp(f->hmf_name, "stream") ||
       (s = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    const char *type = htsmsg_get_str_multi(s, "tags", "type", "cdata", NULL);
    if(type == NULL || strcmp(type, "flash"))
      continue;

    streamid = htsmsg_get_str_multi(s, "attrib", "id", NULL);
    runtime = htsmsg_get_str_multi(s, "tags", "runtime", "cdata", NULL);
    break;
  }
   

  p = prop_create(NULL, NULL);
  m = prop_create(p, "metadata");

  prop_set_string(prop_create(p, "type"), "video");

  if(streamid != NULL)
    prop_set_stringf(prop_create(p, "url"), "headweb:stream:%s", streamid);

  if(runtime != NULL && atoi(runtime) > 0)
    prop_set_float(prop_create(m, "duration"), atoi(runtime));

  if(best_cover != NULL)
    prop_set_string(prop_create(m, "icon"), best_cover);

  prop_set_string(prop_create(m, "title"), title);
  
  prop_set_string(prop_create(m, "description"),
		  htsmsg_get_str_multi(c, "plot", "cdata", NULL));

  if((str = htsmsg_get_str_multi(c, "rating", "cdata", NULL)) != NULL) {
    float r = strtod_ex(str, '.', NULL);
    prop_set_float(prop_create(m, "rating"), r / 5.0);
  }
  
  if(prop_set_parent(p, hb->hb_nodes))
    prop_destroy(p);

}


/**
 *
 */
static void
headweb_browse_fill_genre(headweb_browse_t *hb, htsmsg_t *c)
{
  prop_t *p, *m;
  const char *id = htsmsg_get_str_multi(c, "attrib", "id", NULL);
  const char *title = htsmsg_get_str(c, "cdata");

  if(id == NULL || title == NULL)
    return;

  p = prop_create(NULL, NULL);
  m = prop_create(p, "metadata");

  prop_set_string(prop_create(p, "type"), "directory");
  prop_set_stringf(prop_create(p, "url"), "headweb:genre:%s:%s", id, title);
  prop_set_string(prop_create(m, "title"), title);

  if(prop_set_parent(p, hb->hb_nodes))
    prop_destroy(p);
}


/**
 *
 */
static void
headweb_browse_fill(headweb_browse_t *hb, htsmsg_t *xml)
{
  htsmsg_t *l, *a, *t, *c;
  htsmsg_field_t *f;

  if((l = htsmsg_get_map_multi(xml, "tags", "response", "tags", "list", 
			       NULL)) == NULL)
    return;
  
  if((a = htsmsg_get_map(l, "attrib")) != NULL) {
    const char *items  = htsmsg_get_str(a, "items");
    //    const char *limit  = htsmsg_get_str(a, "limit");
    //    const char *offset = htsmsg_get_str(a, "offset");

    if(items != NULL)
      hb->hb_total_items = atoi(items);
  }

  if((t = htsmsg_get_map(l, "tags")) == NULL)
    return;

  HTSMSG_FOREACH(f, t) {
    if((c = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    hb->hb_loaded_items++;

    if(!strcmp(f->hmf_name, "content"))
       headweb_browse_fill_content(hb, c);

    if(!strcmp(f->hmf_name, "genre"))
       headweb_browse_fill_genre(hb, c);
  }
}


/**
 *
 */
static void
headweb_browse_query(headweb_browse_t *hb)
{
  char *result;
  size_t resultsize;
  char errbuf[100];
  int n;
  htsmsg_t *xml;
  char offset[20];
  char limit[20];

  TRACE(TRACE_DEBUG, "Headweb", "Browse %s @ item #%d", hb->hb_url,
	hb->hb_loaded_items);
  
  snprintf(offset, sizeof(offset), "%d", hb->hb_loaded_items);
  snprintf(limit, sizeof(limit), "%d", HEADWEB_QUERY_LIMIT);

  n = http_request(hb->hb_url,
		   (const char *[]){"apikey", HEADWEB_APIKEY,
		       "offset", offset,
		       "limit", limit,
		       NULL, NULL},
		   &result, &resultsize, errbuf, sizeof(errbuf),
		   NULL, NULL, hb->hb_req_flags);

  if(n) {
    TRACE(TRACE_DEBUG, "HEADWEB", "HTTP query failed: %s",  errbuf);
    return;
  }

  if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL)
    return;

  headweb_browse_fill(hb, xml);
  htsmsg_destroy(xml);
}


/**
 *
 */
/**
 *
 */
static void
headweb_nodesub(void *opaque, prop_event_t event, ...)
{
  headweb_browse_t *hb = opaque;
  va_list ap;
  event_t *e;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    hb->hb_run = 0;
    break;

  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(e->e_type_x == EVENT_APPEND_REQUEST)
      headweb_browse_query(hb);
    break;
  }
  va_end(ap);
}




/**
 *
 */
static void *
headweb_browse_thread(void *aux)
{
  headweb_browse_t *hb = aux;
  headweb_browse_query(hb);

  prop_set_int(hb->hb_loading, 0);

  while(hb->hb_run)
    prop_courier_wait(hb->hb_pc);

  prop_courier_destroy(hb->hb_pc);
  return NULL;
}


/**
 *
 */
static void
headweb_browse_create(prop_t *source, int req_flags,
		      const char *title,
		      const char *fmt, ...)
{
  headweb_browse_t *hb = calloc(1, sizeof(headweb_browse_t));
  prop_t *m = prop_create(source, "metadata");
  char url[URL_MAX];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(url, sizeof(url), fmt, ap);
  va_end(ap);

  hb->hb_req_flags = req_flags;
  hb->hb_nodes = prop_create(source, "nodes");
  prop_ref_inc(hb->hb_nodes);

  if(title != NULL) {
    prop_set_string(prop_create(source, "type"), "directory");
    prop_set_string(prop_create(m, "title"), title);

    hb->hb_loading = prop_create(source, "loading");
    prop_ref_inc(hb->hb_loading);
  }

  hb->hb_total_items = -1; // Don't know yet

  hb->hb_url = strdup(url);

  hb->hb_pc = prop_courier_create_waitable();

  hb->hb_nodesub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, headweb_nodesub, hb,
		   PROP_TAG_ROOT, hb->hb_nodes,
		   PROP_TAG_COURIER, hb->hb_pc,
		   NULL);

  hb->hb_run = 1;
  hts_thread_create_detached("headweb browse", headweb_browse_thread, hb);
}


/**
 *
 */
static void
browse_genres(prop_t *src, const char *url)
{
  headweb_browse_create(src, HTTP_REQUEST_ESCAPE_PATH, "Headweb genres",
			HEADWEB_URL_ROOT"/genre/filter(-adult,stream)");
}


/**
 *
 */
static void
browse_genre(prop_t *src, const char *url)
{
  char *id, *title;

  url += strlen("headweb:genre:");
  id = mystrdupa(url);
  title = strchr(id, ':');
  if(title)
    *title++ = 0;

  headweb_browse_create(src, HTTP_REQUEST_ESCAPE_PATH, title ?: "Unnamed genre",
			HEADWEB_URL_ROOT"/genre/%s", id);

}


/**
 *
 */
static void
browse_contents(prop_t *src, const char *url)
{
  headweb_browse_create(src, HTTP_REQUEST_ESCAPE_PATH, "All contents",
			HEADWEB_URL_ROOT"/content/filter(-adult,stream)");
}

/**
 *
 */
static nav_page_t *
open_stream_decode_xml(struct navigator *nav, htsmsg_t *xml,
		        char *errbuf, size_t errlen)
{
  char newurl[URL_MAX];

  const char *pp = htsmsg_get_str_multi(xml, 
					"tags", "response",
					"tags", "auth", 
					"tags", "playerparams",
					"cdata", NULL);

  char *l = mystrdupa(pp);

  char *url = strstr(l, "cfg.stream.auth.url=");
  if(url == NULL) {
    snprintf(errbuf, errlen, "No Video URL in response");
    return NULL;
  }
  url += strlen("cfg.stream.auth.url=");

  char *streamid = strstr(l, "cfg.stream.auth.streamid=");
  if(streamid == NULL) {
    snprintf(errbuf, errlen, "No Video Stream ID in response");
    return NULL;
  }
  streamid += strlen("cfg.stream.auth.streamid=");

  url[strcspn(url, "&")] = 0;
  streamid[strcspn(streamid, "&")] = 0;

  http_deescape(url);
  http_deescape(streamid);

  snprintf(newurl, sizeof(newurl), "%s/%s", url, streamid);
  TRACE(TRACE_DEBUG, "Headweb", "Redirecting to %s", newurl);
  return backend_open_video(nav, newurl, NULL, errbuf, errlen);
}


/**
 *
 */
static nav_page_t *
open_stream(struct navigator *nav, const char *u, char *errbuf, size_t errlen)
{
  char *result;
  size_t resultsize;
  char url[URL_MAX];
  htsmsg_t *xml;
  int n;
  nav_page_t *np;

  u += strlen("headweb:stream:");

  snprintf(url, sizeof(url), HEADWEB_URL_ROOT"/stream/%s", u);
  

  n = http_request(url,
		   (const char *[]){"apikey", HEADWEB_APIKEY,
		       NULL, NULL},
		   &result, &resultsize, errbuf, errlen,
		   NULL, NULL, HTTP_REQUEST_ESCAPE_PATH);

  if(n)
    return NULL;

  if((xml = htsmsg_xml_deserialize(result, errbuf, errlen)) == NULL)
    return NULL;

  np = open_stream_decode_xml(nav, xml, errbuf, errlen);
  htsmsg_destroy(xml);
  return np;
}


#define strstart(a, b) strncmp(a, b, strlen(b))

/**
 *
 */
static nav_page_t *
be_headweb_open(struct navigator *nav, const char *url, const char *view,
		char *errbuf, size_t errlen)
{
  nav_page_t *np;
  prop_t *src;
  void (*f)(prop_t *, const char *);

  if(!strcmp(url, "headweb:genres")) {
    f = browse_genres;
  } else if(!strcmp(url, "headweb:contents")) {
    f = browse_contents;
  } else if(!strstart(url, "headweb:genre:")) {
    f = browse_genre;
  } else if(!strstart(url, "headweb:stream:")) {
    return open_stream(nav, url, errbuf, errlen);
  } else {
    snprintf(errbuf, errlen, "Invalid URL");
    return NULL;
  }

  np = nav_page_create(nav, url, view, sizeof(nav_page_t),
		       NAV_PAGE_DONT_CLOSE_ON_BACK);

  src = prop_create(np->np_prop_root, "source");
  prop_set_string(prop_create(np->np_prop_root, "view"), "list");
  prop_set_int(prop_create(src, "loading"), 1);

  f(src, url);
  return np;
}



static service_t *svc_genres;

/**
 *
 */
static void
headweb_enable(void)
{
  if(svc_genres == NULL)
    svc_genres = service_create("headweb genres", "Headweb genres",
				"headweb:genres",
				SVC_TYPE_VIDEO, NULL, 0);
}


/**
 *
 */
static void
headweb_disable(void)
{
  if(svc_genres != NULL) {
    service_destroy(svc_genres);
    svc_genres = NULL;
  }
}


/**
 *
 */
static void
headweb_set_enable(void *opaque, int value)
{
  headweb_is_enabled = value;

  if(value) 
    headweb_enable();
  else
    headweb_disable();
}


/**
 *
 */
static int
be_headweb_init(void)
{
  htsmsg_t *store = htsmsg_store_load("headweb") ?: htsmsg_create_map();
  prop_t *s;

  s = settings_add_dir(NULL, "headweb", "Headweb", "video");
  
  settings_create_info(s, 
		       "bundle://resources/headweb/headweb_logo.png",
		       "Headweb is a Swedish online video store.\n"
		       "For more information, visit http://www.headweb.se\n\n"
		       "The Showtime implemetation is still very much beta.\n");

  settings_create_bool(s, "enable", "Enable Headweb", 0, 
		       store, headweb_set_enable, NULL,
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, (void *)"headweb");
  return 0;
}


/**
 *
 */
static int
be_headweb_canhandle(const char *url)
{
  return !strncmp(url, "headweb:", strlen("headweb:"));
}


/**
 *
 */
static void
be_headweb_search(prop_t *source, const char *query, backend_search_type_t type)
{
  char q[500];

  if(!backend_search_video(type) || !headweb_is_enabled)
    return;

  path_escape(q, sizeof(q), query);
  headweb_browse_create(source, 0, NULL, 
			HEADWEB_URL_ROOT"/search/%s/filter(-adult,stream)", q);
}



/**
 *
 */
static backend_t be_headweb = {
  .be_init = be_headweb_init,
  .be_canhandle = be_headweb_canhandle,
  .be_open = be_headweb_open,
  .be_search = be_headweb_search,
};

BE_REGISTER(headweb);
