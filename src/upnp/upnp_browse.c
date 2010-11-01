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
#include "event.h"
#include "playqueue.h"
#include "misc/string.h"
#include "api/lastfm.h"
#include "api/soap.h"

#include "upnp.h"




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
  int h, m, s;

  str = htsmsg_get_str_multi(item, "res", "attrib", "duration", NULL);
  if(str != NULL && sscanf(str, "%d:%d:%d", &h, &m, &s) == 3)
    prop_set_float(prop_create(meta, "duration"), h * 3600 + m * 60 + s);
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
nodes_from_meta(htsmsg_t *meta, prop_t *root, const char *trackid,
		prop_t **trackptr)
{
  htsmsg_t *items, *item;
  htsmsg_field_t *f;
  const char *cls, *id;

  items = htsmsg_get_map_multi(meta, "tags", "DIDL-Lite", "tags", NULL);
  if(items == NULL)
    return;

  HTSMSG_FOREACH(f, items) {
    if(strcmp(f->hmf_name, "item") ||
       (item = htsmsg_get_map_by_field(f)) == NULL)
      continue;
    
    id = htsmsg_get_str_multi(item, "attrib", "id", NULL);
    if(id == NULL)
      continue;

    if((item = htsmsg_get_map(item, "tags")) == NULL)
      continue;
    
    cls = htsmsg_get_str_multi(item, 
			       "urn:schemas-upnp-org:metadata-1-0/upnp/class",
			       "cdata", NULL);
    if(cls == NULL)
      continue;


    prop_t *c = prop_create(NULL, NULL);
    prop_set_string(prop_create(c, "url"),
		    htsmsg_get_str_multi(item, "res", "cdata", NULL));

    prop_t *m = prop_create(c, "metadata");
    item_set_duration(m, item);

    if(!strncmp(cls, "object.item.audioItem", strlen("object.item.audioItem")))
      make_audioItem(c, m, item);
    else {
      prop_destroy(c);
      continue;
    }

    if(prop_set_parent(c, root))
      prop_destroy(c);
    else if(!strcmp(trackid, id) && *trackptr == NULL)
      *trackptr = c;
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
    return -1;
  }

  meta = htsmsg_xml_deserialize(strdup(result), errbuf, sizeof(errbuf));
  if(meta == NULL) {
    TRACE(TRACE_ERROR, "UPNP", 
	  "Browse %s via %s -- XML error %s", uri, id, errbuf);
    return -1;
  }

  nodes_from_meta(meta, nodes, trackid, trackptr);
  htsmsg_destroy(meta);
  return 0;
}
