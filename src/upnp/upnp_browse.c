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
#include <stdio.h>

#include "networking/http_server.h"
#include "htsmsg/htsmsg_xml.h"
#include "htsmsg/htsmsg_json.h"
#include "event.h"
#include "playqueue.h"
#include "misc/str.h"
#include "api/lastfm.h"
#include "api/soap.h"
#include "prop/prop_nodefilter.h"
#include "upnp.h"
#include "fileaccess/http_client.h"
#include "db/kvstore.h"
#include "metadata/playinfo.h"
#include "metadata/metadata.h"
#include "navigator.h"
#include "usage.h"

/**
 * UPNP browse request
 */

typedef struct upnp_browse {
  int ub_run;
  int ub_load_more;

  char *ub_id;

  char *ub_url;
  char *ub_base_url;
  char *ub_control_url;
  char *ub_event_url;

  prop_t *ub_page;
  prop_t *ub_model;
  prop_t *ub_nodes;
  prop_t *ub_items;
  prop_t *ub_loading;
  prop_t *ub_type;
  prop_t *ub_source;
  prop_t *ub_direct_close;
  prop_t *ub_error;
  prop_t *ub_title;
  prop_t *ub_contents;
  prop_t *ub_filter;
  prop_t *ub_canFilter;

  prop_sub_t *ub_itemsub;

  int ub_loaded_entries;
  int ub_total_entries;

  prop_sub_t *ub_sortsub;
  const char *ub_sortcriteria;

} upnp_browse_t;


/**
 *
 */
static const char *
cls_to_type(const char *cls)
{
  if(!strcmp(cls, "object.container.album.musicAlbum"))
    return "album";
  else
    return "directory";
}


/**
 *
 */
static const char *
item_set_str(prop_t *c, htsmsg_t *item, const char *propname, const char *id)
{
  const char *s = htsmsg_get_str_multi(item, id, "cdata", NULL);
  prop_set(c, propname, PROP_SET_STRING, s);
  return s;
}


/**
 *
 */
static void
item_set_duration(prop_t *meta, htsmsg_t *item)
{
  const char *str;
  htsmsg_field_t *f;
  htsmsg_t *res;
  int h, m, s;
  HTSMSG_FOREACH(f, item) {
    if((res = htsmsg_get_map_by_field_if_name(f, "res")) == NULL)
      continue;

    str = htsmsg_get_str(res, "duration");
    if(str != NULL && sscanf(str, "%d:%d:%d", &h, &m, &s) == 3) {
      prop_set_float(prop_create(meta, "duration"), h * 3600 + m * 60 + s);
      break;
    }
  }
}


/**
 *
 */
static void
make_audioItem(prop_t *c, prop_t *m, htsmsg_t *item)
{
  const char *s;
  rstr_t *artist, *album;
  prop_set(c, "type", PROP_SET_STRING, "audio");

  item_set_str(m, item, "title", "title");

  s = item_set_str(m, item, "artist", "artist");
  artist = rstr_alloc(s);

  s = item_set_str(m, item, "album", "album");
  album = rstr_alloc(s);

  if(!item_set_str(m, item, "album_art", "albumArtURI")) {

    if(artist != NULL && album != NULL)
      metadata_bind_albumart(prop_create(m, "album_art"), artist, album);
  }

  if(artist != NULL)
    metadata_bind_artistpics(prop_create(m, "artist_images"), artist);

  rstr_release(artist);
  rstr_release(album);
}


/**
 *
 */
static void
make_videoItem(prop_t *c, prop_t *m, htsmsg_t *item, const char *url)
{
  prop_set_string(prop_create(c, "type"), "video");

  const char *title = htsmsg_get_str(item, "title");

  item_set_str(m, item, "title", "title");
  item_set_str(m, item, "icon", "albumArtURI");

  prop_set(c, "url",      PROP_SET_STRING, url);
  prop_set(c, "filename", PROP_SET_STRING, title);
}


/**
 *
 */
static void
make_imageItem(prop_t *c, prop_t *m, htsmsg_t *item)
{
  prop_set(c, "type", PROP_SET_STRING, "image");
  item_set_str(m, item, "icon",  "albumArtURI");
  item_set_str(m, item, "title", "title");
}


/**
 *
 */
static void
add_item(htsmsg_t *item, prop_t *root, const char *trackid, prop_t **trackptr,
	 prop_sub_t *skip, const char *baseurl)
{
  const char *cls, *id, *url;

  id = htsmsg_get_str(item, "id");
  if(id == NULL)
    return;

  cls = htsmsg_get_str(item, "class");
  if(cls == NULL)
    return;

  url = htsmsg_get_str(item, "res");
  if(url == NULL)
    return;

  prop_t *c = prop_create_root(NULL);

  prop_t *m = prop_create(c, "metadata");
  item_set_duration(m, item);

  if(!strncmp(cls, "object.item.audioItem",
	      strlen("object.item.audioItem"))) {
    prop_set_string(prop_create(c, "url"), url);
    make_audioItem(c, m, item);
    playinfo_bind_url_to_prop(url, c);
  } else if(!strncmp(cls, "object.item.videoItem",
		     strlen("object.item.videoItem"))) {

    char vurl[URL_MAX];
    snprintf(vurl, sizeof(vurl), "%s:%s", baseurl, id);
    make_videoItem(c, m, item, vurl);
    playinfo_bind_url_to_prop(url, c);
  } else if(!strncmp(cls, "object.item.imageItem",
		     strlen("object.item.imageItem"))) {
    prop_set_string(prop_create(c, "url"), url);
    make_imageItem(c, m, item);
  } else {
    UPNP_TRACE("Cant handle upnp:class %s (%s)", cls, url);
    prop_destroy(c);
    return;
  }

  if(prop_set_parent_ex(c, root, NULL, skip))
    prop_destroy(c);
  else if(trackid != NULL && !strcmp(trackid, id) && *trackptr == NULL)
    *trackptr = c;
}


/**
 *
 */
static void
add_container(htsmsg_t *item, prop_t *root, const char *baseurl,
	      prop_sub_t *skip)
{
  char url[URL_MAX];
  const char *cls;

  const char *id = htsmsg_get_str_multi(item, "id", NULL);
  if(id == NULL)
    return;

  snprintf(url, sizeof(url), "%s:%s", baseurl, id);

  prop_t *c = prop_create_root(NULL);
  prop_set(c, "url", PROP_SET_STRING, url);

  prop_t *m = prop_create(c, "metadata");

  item_set_str(m, item, "title", "title");

  cls = htsmsg_get_str(item, "class");

  const char *type = cls ? cls_to_type(cls) : "directory";
  prop_set(c, "type", PROP_SET_STRING, type);

  if(prop_set_parent_ex(c, root, NULL, skip))
    prop_destroy(c);
}

/**
 *
 */
static void
nodes_from_meta(htsmsg_t *meta, prop_t *root, const char *trackid,
		prop_t **trackptr, const char *baseurl, prop_sub_t *skip)
{
  htsmsg_t *items;
  htsmsg_field_t *f;

  items = htsmsg_get_map(meta, "DIDL-Lite");
  if(items == NULL)
    return;

  HTSMSG_FOREACH(f, items) {
    if(!strcmp(f->hmf_name, "item")) {
      htsmsg_t *item = htsmsg_get_map_by_field(f);
      if(item != NULL)
	add_item(item, root, trackid, trackptr, skip, baseurl);
    } else if(baseurl != NULL && !strcmp(f->hmf_name, "container")) {
      htsmsg_t *container = htsmsg_get_map_by_field(f);
      if(container != NULL)
	add_container(container, root, baseurl, skip);
    }
  }
}


/**
 *
 */
int
upnp_browse_children(const char *uri, const char *id, prop_t *nodes,
		     const char *trackid, prop_t **trackptr)
{
  int r;
  htsmsg_t *in = htsmsg_create_map(), *out;
  char errbuf[200];
  const char *result;
  htsmsg_t *meta;

  if(trackptr != NULL)
    *trackptr = NULL;

  htsmsg_add_str(in, "ObjectID", id);
  htsmsg_add_str(in, "BrowseFlag", "BrowseDirectChildren");
  htsmsg_add_str(in, "Filter", "*");
  htsmsg_add_u32(in, "StartingIndex", 0);
  htsmsg_add_u32(in, "RequestedCount", 0);
  htsmsg_add_str(in, "SortCriteria", "");
  r = soap_exec(uri, "ContentDirectory", 1, "Browse", in, &out,
		errbuf, sizeof(errbuf));
  htsmsg_release(in);
  if(r) {
    TRACE(TRACE_ERROR, "UPNP", 
	  "Browse %s via %s -- %s", id, uri, errbuf);
    return -1;
  }

  if(out == NULL) {
    TRACE(TRACE_ERROR, "UPNP", 
	  "Browse %s via %s -- No returned varibles", uri, id);
    return -1;
  }
  
  if((result = htsmsg_get_str(out, "Result")) == NULL) {
    TRACE(TRACE_ERROR, "UPNP", 
	  "Browse %s via %s -- No returned result", uri, id);
    htsmsg_release(out);
    return -1;
  }

  meta = htsmsg_xml_deserialize_cstr(result, errbuf, sizeof(errbuf));
  if(meta == NULL) {
    TRACE(TRACE_ERROR, "UPNP", 
	  "Browse %s via %s -- XML error %s", uri, id, errbuf);
    htsmsg_release(out);
    return -1;
  }

  nodes_from_meta(meta, nodes, trackid, trackptr, NULL, NULL);
  htsmsg_release(meta);
  htsmsg_release(out);
  return 0;
}



/**
 *
 */
static void
browse_fail(upnp_browse_t *ub, const char *fmt, ...)
{
  char buf[500];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  prop_set_string(ub->ub_error, buf);
  prop_set_string(ub->ub_type, "openerror");
  prop_set_int(ub->ub_loading, 0);
}


/**
 *
 */
static void 
browse_items(upnp_browse_t *ub)
{
  int r;
  htsmsg_t *in = htsmsg_create_map(), *out;
  char errbuf[200];
  const char *result, *str;
  htsmsg_t *meta;

  htsmsg_add_str(in, "ObjectID", ub->ub_id);
  htsmsg_add_str(in, "BrowseFlag", "BrowseDirectChildren");
  htsmsg_add_str(in, "Filter", "*");
  htsmsg_add_u32(in, "StartingIndex", ub->ub_loaded_entries);
  htsmsg_add_u32(in, "RequestedCount", 500);
  htsmsg_add_str(in, "SortCriteria", ub->ub_sortcriteria);

  r = soap_exec(ub->ub_control_url, "ContentDirectory", 1, "Browse", in, &out,
		errbuf, sizeof(errbuf));
  htsmsg_release(in);

  if(r)
    return browse_fail(ub, "%s", errbuf);

  if(out == NULL)
    return browse_fail(ub, "Malformed SOAP response, no returned variabled");
  
  str = htsmsg_get_str(out, "TotalMatches");
  if(str != NULL) {
    ub->ub_total_entries = atoi(str);
  } else {
    ub->ub_run = 0;
  }

  str = htsmsg_get_str(out, "NumberReturned");
  if(str != NULL) {
    ub->ub_loaded_entries = atoi(str) + ub->ub_loaded_entries;
  } else {
    ub->ub_run = 0;
  }

  if((result = htsmsg_get_str(out, "Result")) == NULL)
    return browse_fail(ub, "No SOAP result");

  meta = htsmsg_xml_deserialize_cstr(result, errbuf, sizeof(errbuf));
  if(meta == NULL)
    return browse_fail(ub, "Malformed XML: %s", errbuf);

  nodes_from_meta(meta, ub->ub_items, NULL, NULL, 
		  ub->ub_base_url, ub->ub_itemsub);

  UPNP_TRACE("Browsed %d of %d items",
	ub->ub_loaded_entries, ub->ub_total_entries);

  htsmsg_release(meta);
  prop_have_more_childs(ub->ub_items,
                        ub->ub_loaded_entries < ub->ub_total_entries);
  htsmsg_release(out);
}


/**
 *
 */
static void
ub_destroy(upnp_browse_t *ub)
{
  free(ub->ub_id);
  free(ub->ub_url);
  free(ub->ub_base_url);
  free(ub->ub_control_url);
  free(ub->ub_event_url);

  prop_ref_dec(ub->ub_page);
  prop_ref_dec(ub->ub_model);
  prop_ref_dec(ub->ub_nodes);
  prop_ref_dec(ub->ub_items);
  prop_ref_dec(ub->ub_loading);
  prop_ref_dec(ub->ub_type);
  prop_ref_dec(ub->ub_source);
  prop_ref_dec(ub->ub_direct_close);
  prop_ref_dec(ub->ub_error);
  prop_ref_dec(ub->ub_title);
  prop_ref_dec(ub->ub_contents);
  prop_ref_dec(ub->ub_filter);
  prop_ref_dec(ub->ub_canFilter);
  free(ub);
}


/**
 *
 */
static void
node_eventsub(void *opaque, prop_event_t event, ...)
{
  upnp_browse_t *ub = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    ub->ub_run = 0;
    break;

  case PROP_WANT_MORE_CHILDS:
    ub->ub_load_more = 1;
    break;
  }
}


static int
upnp_browse_resolve(upnp_browse_t *ub)
{
  upnp_device_t *ud = NULL;
  upnp_service_t *us;

  char *url = mystrdupa(ub->ub_url);
  char *base = url;

  url += strlen("upnp:");
  
  hts_mutex_lock(&upnp_lock);
  
  while(ud == NULL) {
    LIST_FOREACH(ud, &upnp_devices, ud_link) {
      if(ud->ud_uuid != NULL && 
	 !strncmp(ud->ud_uuid, url, strlen(ud->ud_uuid)))
	break;
    }

    if(ud != NULL)
      break;

    // Do SSDP M-SEARCH ?
    if(hts_cond_wait_timeout(&upnp_device_cond, &upnp_lock, 5000))
      break;
  }

  if(ud == NULL) {
    hts_mutex_unlock(&upnp_lock);
    nav_open_errorf(ub->ub_page, _("Device not found"));
    return 1;
  }
  
  url += strlen(ud->ud_uuid);
  if(*url != ':') {
    hts_mutex_unlock(&upnp_lock);
    nav_open_errorf(ub->ub_page, _("Malformed URI after device"));
    return 1;
  }

  url++;

  LIST_FOREACH(us, &ud->ud_services, us_link) {
    if(us->us_id != NULL && 
       !strncmp(us->us_id, url, strlen(us->us_id)))
      break;
  }

  if(us == NULL) {
    hts_mutex_unlock(&upnp_lock);
    nav_open_errorf(ub->ub_page, _("Service not found"));
    return 1;
  }
  
  url += strlen(us->us_id);
  if(*url != ':') {
    hts_mutex_unlock(&upnp_lock);
    nav_open_errorf(ub->ub_page, _("Malformed URI after service"));
    return 1;
  }

  *url = 0;
  url++;

  ub->ub_base_url = strdup(base);
  ub->ub_control_url = us->us_control_url ? strdup(us->us_control_url) : NULL;
  ub->ub_event_url   = us->us_event_url   ? strdup(us->us_event_url)   : NULL;
  ub->ub_id = strdup(url);

  hts_mutex_unlock(&upnp_lock);
  return 0;
}



/**
 *
 */
static void
set_sort_order(void *opaque, prop_event_t event, ...)
{
  upnp_browse_t *ub = opaque;
  va_list ap;
  prop_t *p;

  va_start(ap, event);

  switch(event) {
  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    const char *val = rstr_get(r);
    if(val != NULL) {
      if(!strcmp(val, "title"))
	ub->ub_sortcriteria = "";
      else if(!strcmp(val, "date"))
	ub->ub_sortcriteria = "-dc:date";
      else if(!strcmp(val, "dateold"))
	ub->ub_sortcriteria = "+dc:date";
    }
    kv_url_opt_set(ub->ub_url, KVSTORE_DOMAIN_SYS, "sortorder", 
		   KVSTORE_SET_STRING, val);
    rstr_release(r);

    ub->ub_loaded_entries = 0;
    ub->ub_load_more = 1;
    prop_destroy_childs(ub->ub_items);
    break;

  default:
    break;
  }
}


/**
 *
 */
static void
add_sort_option_type(upnp_browse_t *ub, prop_t *model, prop_courier_t *pc)
{
  prop_t *parent = prop_create(model, "options");
  prop_t *n = prop_create_root(NULL);
  prop_t *m = prop_create(n, "metadata");
  prop_t *options = prop_create(n, "options");

  prop_set_string(prop_create(n, "type"), "multiopt");
  prop_set_int(prop_create(n, "enabled"), 1);
  prop_link(_p("Sort on"), prop_create(m, "title"));
  

  prop_t *on_title = prop_create_root("title");
  prop_link(_p("Filename"), prop_create(on_title, "title"));
  if(prop_set_parent(on_title, options))
    abort();

  prop_t *on_date = prop_create_root("date");
  prop_link(_p("Date (newest first)"), prop_create(on_date, "title"));
  if(prop_set_parent(on_date, options))
    abort();

  prop_t *on_dateold = prop_create_root("dateold");
  prop_link(_p("Date (oldest first)"), prop_create(on_dateold, "title"));
  if(prop_set_parent(on_dateold, options))
    abort();

  rstr_t *cur = kv_url_opt_get_rstr(ub->ub_url, KVSTORE_DOMAIN_SYS, 
				    "sortorder");

  if(cur != NULL && !strcmp(rstr_get(cur), "date")) {
    prop_select(on_date);
    ub->ub_sortcriteria = "-dc:date";
  } else if(cur != NULL && !strcmp(rstr_get(cur), "dateold")) {
    prop_select(on_date);
    ub->ub_sortcriteria = "+dc:date";
  } else {
    prop_select(on_title);
    ub->ub_sortcriteria = "";
  }
  rstr_release(cur);
  ub->ub_sortsub = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
				  PROP_TAG_CALLBACK, set_sort_order, ub,
				  PROP_TAG_ROOT, options,
				  PROP_TAG_COURIER, pc,
				  NULL);
  
  if(prop_set_parent(n, parent))
    prop_destroy(n);
}




/**
 *
 */
static void
browse_directory(upnp_browse_t *ub, const char *title)
{
  prop_courier_t *pc;
  struct prop_nf *pnf;

  prop_set_string(ub->ub_type, "directory");

  pnf = prop_nf_create(ub->ub_nodes, ub->ub_items, ub->ub_filter,
		       PROP_NF_AUTODESTROY);
  prop_set_int(ub->ub_canFilter, 1);

  rstr_t *t = rstr_alloc(title);
  decorated_browse_create(ub->ub_model, pnf, ub->ub_items, t, 
                          DECO_FLAGS_NO_AUTO_SORTING,
                          ub->ub_url, "UPnP");
  rstr_release(t);
  prop_nf_release(pnf);

  pc = prop_courier_create_waitable();
  add_sort_option_type(ub, ub->ub_model, pc);

  ub->ub_run = 1;
  ub->ub_itemsub = prop_subscribe(PROP_SUB_TRACK_DESTROY,
				  PROP_TAG_CALLBACK, node_eventsub, ub,
				  PROP_TAG_ROOT, ub->ub_items,
				  PROP_TAG_COURIER, pc,
				  NULL);
  // initial browse
  browse_items(ub);

  prop_set_int(ub->ub_loading, 0);
  while(ub->ub_run) {

    prop_courier_wait_and_dispatch(pc);
    
    if(ub->ub_load_more) {
      ub->ub_load_more = 0;
      browse_items(ub);
    }
  }

  prop_unsubscribe(ub->ub_itemsub);
  prop_unsubscribe(ub->ub_sortsub);

  prop_courier_destroy(pc);
}


/**
 *
 */
static void
minidlna_get_srt(const char *url, htsmsg_t *sublist)
{
  struct http_header_list out;
  const char *s;

  LIST_INIT(&out);

  if(!http_req(url,
               HTTP_REQUEST_HEADER("getCaptionInfo.sec", "1"),
               HTTP_RESPONSE_HEADERS(&out),
               NULL)) {
    if((s = http_header_get(&out, "CaptionInfo.sec")) != NULL) {

      htsmsg_t *sub = htsmsg_create_map();
      htsmsg_add_str(sub, "url", s);
      htsmsg_add_str(sub, "source", "MiniDLNA");
      htsmsg_add_str(sub, "title", "SRT file");
      htsmsg_add_str(sub, "format", "SRT");
      htsmsg_add_msg(sublist, NULL, sub);
    }
  }
  http_headers_free(&out);
}



/**
 *
 */
static void
blind_srt_check(const char *url, htsmsg_t *sublist)
{
  struct http_header_list out;
  char *srt = mystrdupa(url);
  char *dot = strrchr(srt, '.');

  if(dot == NULL)
    return;

  strcpy(dot, ".srt");

  LIST_INIT(&out);
  if(!http_req(srt,
               HTTP_RESPONSE_HEADERS(&out),
               NULL)) {
    const char *s;
    if((s = http_header_get(&out, "Content-Type")) != NULL) {
      if(!strcasecmp(s, "application/x-srt")) {
	htsmsg_t *sub = htsmsg_create_map();
	htsmsg_add_str(sub, "url", srt);
	htsmsg_add_str(sub, "source", "HTTP probe");
	htsmsg_add_str(sub, "title", "SRT file");
	htsmsg_add_str(sub, "format", "SRT");
	htsmsg_add_msg(sublist, NULL, sub);
      }
    }
  }
}


/**
 *
 */
static void
browse_video_item(upnp_browse_t *ub, htsmsg_t *item)
{
  htsmsg_t *res;
  htsmsg_field_t *f;
  const char *url = NULL, *mimetype = NULL, *title;

  HTSMSG_FOREACH(f, item) {
    if(f->hmf_type != HMF_STR)
      continue;

    if((res = htsmsg_get_map_by_field_if_name(f, "res")) == NULL)
      continue;

    const char *pi = htsmsg_get_str(res, "protocolInfo");

    if(pi == NULL)
      continue;

    char *tmp = NULL, *str = mystrdupa(pi);

    const char *proto = strtok_r(str, ":", &tmp);
    if(proto == NULL || strcmp(proto, "http-get"))
      continue;

    strtok_r(NULL, ":", &tmp);
    const char *contentformat = strtok_r(NULL, ":", &tmp);
    const char *ai = strtok_r(NULL, ":", &tmp);

    if(ai == NULL || strstr(ai, "DLNA.ORG_PN=JPEG_TN") == NULL) {
      url = f->hmf_str;
      mimetype = contentformat;
      break;
    }
  }

  if(url == NULL)
    return browse_fail(ub, "UPNP Video playback: No playable URL");

  title = htsmsg_get_str(item, "title");

  // Construct videoparam JSON blob

  htsmsg_t *vp = htsmsg_create_map();
  htsmsg_add_str(vp, "canonicalUrl", url);

  htsmsg_add_u32(vp, "no_fs_scan", 1); /* Don't try to scan parent directory
					* for subtitles
					*/
  if(title != NULL)
    htsmsg_add_str(vp, "title", title);

  htsmsg_t *src = htsmsg_create_map();
  htsmsg_add_str(src, "url", url);
  if(mimetype != NULL)
    htsmsg_add_str(src, "mimetype", mimetype);

  htsmsg_t *sources = htsmsg_create_list();
  htsmsg_add_msg(sources, NULL, src);
  
  htsmsg_add_msg(vp, "sources", sources);

  htsmsg_t *subtitles = htsmsg_create_list();

  minidlna_get_srt(url, subtitles);
  blind_srt_check(url, subtitles);

  htsmsg_add_msg(vp, "subtitles", subtitles);
  
  rstr_t *rstr = htsmsg_json_serialize_to_rstr(vp, "videoparams:");
  prop_set_rstring(ub->ub_source, rstr);
  rstr_release(rstr);

  prop_set_int(ub->ub_direct_close, 1);
  prop_set_string(ub->ub_type, "video");
  prop_set_int(ub->ub_loading, 0);
}


/**
 *
 */
static void
browse_item(upnp_browse_t *ub, htsmsg_t *item, int sync)
{
  const char *cls;
  cls = htsmsg_get_str(item, "class");

  if(cls == NULL)
    return browse_fail(ub, "Missing <class> in item tag");

  if(!strncmp(cls, "object.item.videoItem",
	      strlen("object.item.videoItem"))) {
    usage_page_open(sync, "UPNP Video");
    browse_video_item(ub, item);
  } else {
    usage_page_open(sync, "UPNP Unknown-item");
    browse_fail(ub, "Don't know how to browse %s", cls);
  }
}


/**
 *
 */
static void
browse_container(upnp_browse_t *ub, htsmsg_t *container)
{
  const char *name, *cls;
  name = htsmsg_get_str(container, "title");

  cls = htsmsg_get_str(container, "class");

  if(name)
    prop_set_string(ub->ub_title, name);

  if(!strcmp(cls, "object.container.album.musicAlbum"))
    prop_set_string(ub->ub_contents, "albumTracks");

  browse_directory(ub, name);
}


/**
 *
 */
static void
browse_self(upnp_browse_t *ub, int sync)
{
  int r;
  htsmsg_t *in = htsmsg_create_map(), *out;
  char errbuf[200];
  const char *result;
  htsmsg_t *meta, *x;

  htsmsg_add_str(in, "ObjectID", ub->ub_id);
  htsmsg_add_str(in, "BrowseFlag", "BrowseMetadata");
  htsmsg_add_str(in, "Filter", "*");
  htsmsg_add_u32(in, "StartingIndex", 0);
  htsmsg_add_u32(in, "RequestedCount", 1);
  htsmsg_add_str(in, "SortCriteria", "");

  r = soap_exec(ub->ub_control_url, "ContentDirectory", 1, "Browse", in, &out,
		errbuf, sizeof(errbuf));
  htsmsg_release(in);

  if(r)
    return browse_fail(ub, "%s", errbuf);

  if(out == NULL)
    return browse_fail(ub, "Malformed SOAP response, no returned variabled");

  if((result = htsmsg_get_str(out, "Result")) == NULL) {
    htsmsg_release(out);
    return browse_fail(ub, "No SOAP result");
  }

  meta = htsmsg_xml_deserialize_cstr(result, errbuf, sizeof(errbuf));
  if(meta == NULL) {
    htsmsg_release(out);
    return browse_fail(ub, "Malformed XML: %s", errbuf);
  }

  if(!sync && (x = htsmsg_get_map_multi(meta, "DIDL-Lite", "container",
                                        NULL)) != NULL) {
    usage_page_open(sync, "UPNP Container");
    browse_container(ub, x);

  } else if((x = htsmsg_get_map_multi(meta, "DIDL-Lite", "item",
                                      NULL)) != NULL) {
    browse_item(ub, x, sync);
  } else {
    browse_fail(ub, "Browsing something that is neither item nor container");
    usage_page_open(sync, "UPNP bad-item");
  }
  htsmsg_release(meta);
  htsmsg_release(out);
}

/**
 *
 */
int
be_upnp_browse(prop_t *page, const char *url, int sync)
{
  upnp_browse_t *ub = calloc(1, sizeof(upnp_browse_t));
  ub->ub_url = strdup(url);

  ub->ub_page = prop_ref_inc(page);
  ub->ub_source = prop_create_r(ub->ub_page, "source");

  ub->ub_direct_close = prop_create_r(page, "directClose");

  ub->ub_model = prop_create_r(page, "model");

  ub->ub_type = prop_create_r(ub->ub_model, "type");
  
  ub->ub_contents = prop_create_r(ub->ub_model, "contents");
  ub->ub_error = prop_create_r(ub->ub_model, "error");
  ub->ub_nodes = prop_create_r(ub->ub_model, "nodes");
  ub->ub_items = prop_create_r(ub->ub_model, "source");
  ub->ub_loading = prop_create_r(ub->ub_model, "loading");
  prop_set_int(ub->ub_loading, 1);

  ub->ub_filter = prop_create_r(ub->ub_model, "filter");
  ub->ub_canFilter = prop_create_r(ub->ub_model, "canFilter");

  prop_t *metadata = prop_create(ub->ub_model, "metadata");

  ub->ub_title = prop_create_r(metadata, "title");

  if(!upnp_browse_resolve(ub)) {
    browse_self(ub, sync);
  } else {
    usage_page_open(sync, "UPNP bad-route");
  }

  ub_destroy(ub);
  return 0;
}
