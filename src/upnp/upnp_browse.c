/*
 *  Showtime UPNP
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

#include "networking/http_server.h"
#include "htsmsg/htsmsg_xml.h"
#include "htsmsg/htsmsg_json.h"
#include "event.h"
#include "playqueue.h"
#include "misc/string.h"
#include "api/lastfm.h"
#include "api/soap.h"
#include "prop/prop_nodefilter.h"
#include "upnp.h"
#include "fileaccess/fileaccess.h"



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

  int ub_images;

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
  prop_set_string(prop_create(c, propname), s);
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
    
    str = htsmsg_get_str_multi(res, "attrib", "duration", NULL);
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
  const char *artist, *album;
  prop_set_string(prop_create(c, "type"), "audio");

  item_set_str(m, item, "title",
	       "http://purl.org/dc/elements/1.1/title");

  artist = item_set_str(m, item, "artist",
			"urn:schemas-upnp-org:metadata-1-0/upnp/artist");

  album = item_set_str(m, item, "album",
		       "urn:schemas-upnp-org:metadata-1-0/upnp/album");

  if(!item_set_str(m, item, "album_art",
		   "urn:schemas-upnp-org:metadata-1-0/upnp/albumArtURI")) {
    
    if(artist != NULL && album != NULL) {
      lastfm_albumart_init(prop_create(m, "album_art"),
			   rstr_alloc(artist), rstr_alloc(album));
    }
  }
}


/**
 *
 */
static void
make_videoItem(prop_t *c, prop_t *m, htsmsg_t *item,
	       const char *baseurl, const char *id)
{
  char url[URL_MAX];

  prop_set_string(prop_create(c, "type"), "video");

  item_set_str(m, item, "title", "http://purl.org/dc/elements/1.1/title");

  snprintf(url, sizeof(url), "%s:%s", baseurl, id);

  prop_set_string(prop_create(c, "url"), url);
}


/**
 *
 */
static void
make_imageItem(prop_t *c, prop_t *m, htsmsg_t *item)
{
  prop_set_string(prop_create(c, "type"), "image");

  item_set_str(m, item, "title",
	       "http://purl.org/dc/elements/1.1/title");

}


/**
 *
 */
static void
add_item(htsmsg_t *item, prop_t *root, const char *trackid, prop_t **trackptr,
	 prop_sub_t *skip, const char *baseurl, upnp_browse_t *ub,
	 void *db)
{
  const char *cls, *id, *url;

  id = htsmsg_get_str_multi(item, "attrib", "id", NULL);
  if(id == NULL)
    return;

  if((item = htsmsg_get_map(item, "tags")) == NULL)
    return;
    
  cls = htsmsg_get_str_multi(item, 
			     "urn:schemas-upnp-org:metadata-1-0/upnp/class",
			     "cdata", NULL);
  if(cls == NULL)
    return;

  url = htsmsg_get_str_multi(item, "res", "cdata", NULL);

  prop_t *c = prop_create_root(NULL);
		  

  prop_t *m = prop_create(c, "metadata");
  item_set_duration(m, item);

  if(!strncmp(cls, "object.item.audioItem",
	      strlen("object.item.audioItem"))) {
    prop_set_string(prop_create(c, "url"), url);
    make_audioItem(c, m, item);
    if(db != NULL)
      metadb_bind_url_to_prop(db, url, c);
  } else if(!strncmp(cls, "object.item.videoItem",
		     strlen("object.item.videoItem"))) {
    make_videoItem(c, m, item, baseurl, id);
    if(db != NULL)
      metadb_bind_url_to_prop(db, url, c);
  } else if(!strncmp(cls, "object.item.imageItem",
		     strlen("object.item.imageItem"))) {
    prop_set_string(prop_create(c, "url"), url);
    make_imageItem(c, m, item);
    if(ub != NULL)
      ub->ub_images++;
  } else {
    TRACE(TRACE_DEBUG, "UPNP", "Cant handle upnp:class %s (%s)", cls, url);
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

  const char *id = htsmsg_get_str_multi(item, "attrib", "id", NULL);
  if(id == NULL)
    return;

  snprintf(url, sizeof(url), "%s:%s", baseurl, id);

  prop_t *c = prop_create_root(NULL);
  prop_set_string(prop_create(c, "url"), url);

  prop_t *m = prop_create(c, "metadata");

  if((item = htsmsg_get_map(item, "tags")) == NULL)
    return;

  item_set_str(m, item, "title",
	       "http://purl.org/dc/elements/1.1/title");

  cls = htsmsg_get_str_multi(item, 
			     "urn:schemas-upnp-org:metadata-1-0/upnp/class",
			     "cdata", NULL);
  const char *type = cls ? cls_to_type(cls) : "directory";
  prop_set_string(prop_create(c, "type"), type);

  if(prop_set_parent_ex(c, root, NULL, skip))
    prop_destroy(c);
}

/**
 *
 */
static void
nodes_from_meta(htsmsg_t *meta, prop_t *root, const char *trackid,
		prop_t **trackptr, const char *baseurl, prop_sub_t *skip,
		upnp_browse_t *ub)
{
  htsmsg_t *items;
  htsmsg_field_t *f;

  items = htsmsg_get_map_multi(meta, "tags", "DIDL-Lite", "tags", NULL);
  if(items == NULL)
    return;

  void *db = metadb_get();

  HTSMSG_FOREACH(f, items) {
    if(!strcmp(f->hmf_name, "item")) {
      htsmsg_t *item = htsmsg_get_map_by_field(f);
      if(item != NULL)
	add_item(item, root, trackid, trackptr, skip, baseurl, ub, db);
    } else if(baseurl != NULL && !strcmp(f->hmf_name, "container")) {
      htsmsg_t *container = htsmsg_get_map_by_field(f);
      if(container != NULL)
	add_container(container, root, baseurl, skip);
    }
  }
  metadb_close(db);
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
  htsmsg_destroy(in);
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
    htsmsg_destroy(out);
    return -1;
  }

  meta = htsmsg_xml_deserialize(strdup(result), errbuf, sizeof(errbuf));
  if(meta == NULL) {
    TRACE(TRACE_ERROR, "UPNP", 
	  "Browse %s via %s -- XML error %s", uri, id, errbuf);
    htsmsg_destroy(out);
    return -1;
  }

  nodes_from_meta(meta, nodes, trackid, trackptr, NULL, NULL, NULL);
  htsmsg_destroy(meta);
  htsmsg_destroy(out);
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
  htsmsg_add_str(in, "SortCriteria", "");

  r = soap_exec(ub->ub_control_url, "ContentDirectory", 1, "Browse", in, &out,
		errbuf, sizeof(errbuf));
  htsmsg_destroy(in);

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
  meta = htsmsg_xml_deserialize(strdup(result), errbuf, sizeof(errbuf));
  if(meta == NULL)
    return browse_fail(ub, "Malformed XML: %s", errbuf);

  nodes_from_meta(meta, ub->ub_items, NULL, NULL, 
		  ub->ub_base_url, ub->ub_itemsub, ub);

  TRACE(TRACE_DEBUG, "UPNP", "Browsed %d of %d items",
	ub->ub_loaded_entries, ub->ub_total_entries);

  if(ub->ub_images * 4 > ub->ub_loaded_entries)
    prop_set_string(ub->ub_contents, "images");

  htsmsg_destroy(meta);
  if(ub->ub_loaded_entries < ub->ub_total_entries)
    prop_have_more_childs(ub->ub_items);
  htsmsg_destroy(out);
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

  char *url = ub->ub_url;

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

  ub->ub_base_url = strdup(ub->ub_url);
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
browse_directory(upnp_browse_t *ub)
{
  prop_courier_t *pc;
  struct prop_nf *pnf;

  prop_set_string(ub->ub_type, "directory");

  pnf = prop_nf_create(ub->ub_nodes, ub->ub_items, ub->ub_filter, NULL,
		       PROP_NF_AUTODESTROY);
  prop_set_int(ub->ub_canFilter, 1);
  prop_nf_release(pnf);

  pc = prop_courier_create_waitable();
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
  prop_courier_destroy(pc);

}


/**
 *
 */
static void
minidlna_get_srt(const char *url, htsmsg_t *sublist)
{
  struct http_header_list in, out;
  const char *s;

  LIST_INIT(&in);
  LIST_INIT(&out);
  
  http_header_add(&in, "getCaptionInfo.sec", "1");

  if(!http_request(url, NULL, NULL, NULL, NULL, 0, NULL, 0,
		   0, &out, &in, NULL)) {
    if((s = http_header_get(&out, "CaptionInfo.sec")) != NULL) {

      htsmsg_t *sub = htsmsg_create_map();
      htsmsg_add_str(sub, "url", s);
      htsmsg_add_str(sub, "source", "MiniDLNA");
      htsmsg_add_msg(sublist, NULL, sub);
    }
  }
  http_headers_free(&in);
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
  if(!http_request(srt, NULL, NULL, NULL, NULL, 0, NULL, 0,
		   0, &out, NULL, NULL)) {
    const char *s;
    if((s = http_header_get(&out, "Content-Type")) != NULL) {
      if(!strcasecmp(s, "application/x-srt")) {
	htsmsg_t *sub = htsmsg_create_map();
	htsmsg_add_str(sub, "url", srt);
	htsmsg_add_str(sub, "source", "HTTP probe");
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
  htsmsg_t *tags, *res;
  htsmsg_field_t *f;
  const char *url = NULL, *mimetype = NULL, *title;
  char *str, *vpstr;
  size_t len;

  if((tags = htsmsg_get_map(item, "tags")) == NULL)
    return browse_fail(ub, "UPNP Video playback: No tags in item");

  HTSMSG_FOREACH(f, tags) {
    if((res = htsmsg_get_map_by_field_if_name(f, "res")) == NULL)
      continue;

    const char *pi = htsmsg_get_str_multi(res, "attrib", "protocolInfo", NULL);

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
      url = htsmsg_get_str_multi(res, "cdata", NULL);
      mimetype = contentformat;
      break;
    }
  }


  if(url == NULL)
    return browse_fail(ub, "UPNP Video playback: No playable URL");

  title = htsmsg_get_str_multi(tags, "http://purl.org/dc/elements/1.1/title",
			       "cdata", NULL);

  // Construct videoparam JSON blob

  htsmsg_t *vp = htsmsg_create_map();

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
  
  str = htsmsg_json_serialize_to_str(vp, 0);
  len = strlen(str);
  vpstr = malloc(len + strlen("videoparams:") + 1);
  strcpy(vpstr, "videoparams:");
  strcpy(vpstr + strlen("videoparams:"), str);
  free(str);

  prop_set_string(ub->ub_source, vpstr);
  free(vpstr);

  prop_set_int(ub->ub_direct_close, 1);
  prop_set_string(ub->ub_type, "video");
  prop_set_int(ub->ub_loading, 0);
}


/**
 *
 */
static void
browse_item(upnp_browse_t *ub, htsmsg_t *item)
{
  const char *cls;
  cls = htsmsg_get_str_multi(item, "tags",
			     "urn:schemas-upnp-org:metadata-1-0/upnp/class",
			     "cdata", NULL);

  if(cls == NULL)
    return browse_fail(ub, "Missing <class> in item tag");

  if(!strncmp(cls, "object.item.videoItem",
	      strlen("object.item.videoItem"))) {
    browse_video_item(ub, item);
  } else {
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
  name = htsmsg_get_str_multi(container, "tags",
			      "http://purl.org/dc/elements/1.1/title",
			      "cdata", NULL);

  cls = htsmsg_get_str_multi(container, "tags",
			     "urn:schemas-upnp-org:metadata-1-0/upnp/class",
			     "cdata", NULL);
  
  if(name)
    prop_set_string(ub->ub_title, name);
  
  if(!strcmp(cls, "object.container.album.musicAlbum"))
    prop_set_string(ub->ub_contents, "albumTracks");

  browse_directory(ub);
}


/**
 *
 */
static void 
browse_self(upnp_browse_t *ub)
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
  htsmsg_destroy(in);

  if(r)
    return browse_fail(ub, "%s", errbuf);

  if(out == NULL)
    return browse_fail(ub, "Malformed SOAP response, no returned variabled");

  if((result = htsmsg_get_str(out, "Result")) == NULL) {
    htsmsg_destroy(out);
    return browse_fail(ub, "No SOAP result");
  }
  meta = htsmsg_xml_deserialize(strdup(result), errbuf, sizeof(errbuf));
  if(meta == NULL) {
    htsmsg_destroy(out);
    return browse_fail(ub, "Malformed XML: %s", errbuf);
  }

  if((x = htsmsg_get_map_multi(meta, 
			       "tags", "DIDL-Lite",
			       "tags", "container",
			       NULL)) != NULL) {
    browse_container(ub, x);
 
  } else if((x = htsmsg_get_map_multi(meta, 
				      "tags", "DIDL-Lite",
				      "tags", "item",
				      NULL)) != NULL) {
    browse_item(ub, x);
  } else {
    browse_fail(ub, "Browsing something that is neither item nor container");
  }
  htsmsg_destroy(meta);
  htsmsg_destroy(out);
}



/**
 *
 */
static void *
upnp_browse_thread(void *aux)
{
  upnp_browse_t *ub = aux;

  if(upnp_browse_resolve(ub)) {
    ub_destroy(ub);
    return NULL;
  }

  // Check what we are browsing
  browse_self(ub);


  ub_destroy(ub);
  return NULL;
}


/**
 *
 */
int
be_upnp_browse(prop_t *page, const char *url)
{

  upnp_browse_t *ub = calloc(1, sizeof(upnp_browse_t));
  ub->ub_url = strdup(url);
  ub->ub_page = prop_ref_inc(page);

  ub->ub_source = prop_ref_inc(prop_create(page, "source"));
  ub->ub_direct_close = prop_ref_inc(prop_create(page, "directClose"));

  prop_t *model = prop_create(page, "model");

  ub->ub_type = prop_ref_inc(prop_create(model, "type"));
  
  ub->ub_contents = prop_ref_inc(prop_create(model, "contents"));
  ub->ub_error = prop_ref_inc(prop_create(model, "error"));
  ub->ub_nodes = prop_ref_inc(prop_create(model, "nodes"));
  ub->ub_items = prop_ref_inc(prop_create(model, "items"));
  ub->ub_loading = prop_ref_inc(prop_create(model, "loading"));
  prop_set_int(ub->ub_loading, 1);

  ub->ub_filter = prop_ref_inc(prop_create(model, "filter"));
  ub->ub_canFilter = prop_ref_inc(prop_create(model, "canFilter"));

  prop_t *metadata = prop_create(model, "metadata");

  ub->ub_title = prop_ref_inc(prop_create(metadata, "title"));
  hts_thread_create_detached("upnpbrowse", upnp_browse_thread, ub,
			     THREAD_PRIO_LOW);
  return 0;
}
