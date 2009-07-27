/*
 *  File backend directory scanner
 *  Copyright (C) 2008 - 2009 Andreas Öman
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
#include <stdio.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "playqueue.h"

typedef struct scanner {
  char *s_url;

  prop_t *s_nodes;
  prop_t *s_viewprop;
  prop_t *s_root;

  int s_stop;

} scanner_t;




/**
 *
 */
const char *type2str[] = {
  [CONTENT_DIR]      = "directory",
  [CONTENT_FILE]     = "file",
  [CONTENT_AUDIO]    = "audio",
  [CONTENT_ARCHIVE]  = "archive",
  [CONTENT_VIDEO]    = "video",
  [CONTENT_PLAYLIST] = "playlist",
  [CONTENT_DVD]      = "dvd",
  [CONTENT_IMAGE]    = "image",
};


/**
 *
 */
static void
set_type(prop_t *proproot, unsigned int type)
{
  if(type < sizeof(type2str) / sizeof(type2str[0]) && type2str[type] != NULL)
    prop_set_string(prop_create(proproot, "type"), type2str[type]);
}


/**
 *
 */
static void
scannercore(scanner_t *s)
{
  prop_t *metadata, *p, *p2;
  int r, destroyed = 0;
  nav_dir_t *nd;
  nav_dir_entry_t *nde;
  void *ref;
  int images;
  int album_score = 0;
  const char *str;
  char album_name[128];
  char artist_name[128];
  char buf[128];
  int trackidx;

  ref = fa_reference(s->s_url);

  if((nd = fa_scandir(s->s_url, NULL, 0)) == NULL) {
    fa_unreference(ref);
    return;
  }
  if(nd->nd_count == 0) {
    if(s->s_viewprop != NULL)
      prop_set_string(s->s_viewprop, "empty");
    nav_dir_free(nd);
    fa_unreference(ref);
    return;
  }

  nav_dir_sort(nd);

  if(s->s_viewprop != NULL) {
    /* Select a view based on filenames */
    images = 0;
    TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {
      if(s->s_stop)
	break;

      if((str = strchr(nde->nde_filename, '.')) == NULL)
	continue;
      str++;

      if(!strcasecmp(str, "jpg") || !strcmp(str, "jpeg"))
	images++;
    }

    if(images * 4 > nd->nd_count * 3)
      prop_set_string(s->s_viewprop, "images");
  }

  /* Add filename and type */
  TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {

    if(s->s_stop)
      break;

    p = prop_create(NULL, "node");
    prop_set_string(prop_create(p, "url"), nde->nde_url);
    set_type(p, nde->nde_type);

    metadata = prop_create(p, "metadata");
    prop_set_string(prop_create(metadata, "title"), nde->nde_filename);

    if(prop_set_parent(p, s->s_nodes))
      prop_destroy(p);
    else
      nde->nde_opaque = p;
  }

  images = 0;
  album_name[0] = 0;
  artist_name[0] = 0;
  /* Do full probe */
  TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {

    if(s->s_stop)
      break;

    if((p = nde->nde_opaque) == NULL)
      continue;

    metadata = prop_create(p, "metadata");

    if(nde->nde_type == CONTENT_DIR) {
      r = fa_probe_dir(metadata, nde->nde_url);
    } else {
      r = fa_probe(metadata, nde->nde_url, NULL, 0, NULL, 0);
    }

    set_type(p, r);
    nde->nde_type = r;

    switch(r) {
    case CONTENT_AUDIO:
      if((p2 = prop_get_by_names(metadata, "album", NULL)) == NULL ||
	 prop_get_string(p2, buf, sizeof(buf))) {
	album_score--;
	break;
      }

      if(album_name[0] == 0) {
	snprintf(album_name, sizeof(album_name), "%s", buf);
	album_score++;
      } else if(!strcasecmp(album_name, buf)) {
	album_score++;
      } else {
	album_score--;
	break;
      }
      
      if((p2 = prop_get_by_names(metadata, "artist", NULL)) == NULL ||
	 prop_get_string(p2, buf, sizeof(buf)))
	break;

      if(strstr(artist_name, buf))
	break;

      snprintf(artist_name + strlen(artist_name),
	       sizeof(artist_name) - strlen(artist_name),
	       "%s%s", artist_name[0] ? ", " : "", buf);
      break;

    case CONTENT_UNKNOWN:
      destroyed++;
      prop_destroy(p);
      nde->nde_opaque = NULL;
      break;

    case CONTENT_IMAGE:
      images++;
      break;

    default:
      album_score--;
    }
  }

  if(!s->s_stop && s->s_viewprop != NULL && s->s_root) {


    if(album_score > 0) {
      
      /* It is an album */
      prop_set_string(s->s_viewprop, "album");
      
      prop_set_string(prop_create(s->s_root, "album_name"), 
		      album_name);

      if(artist_name[0])
	prop_set_string(prop_create(s->s_root, "artist_name"), 
			artist_name);
      
      trackidx = 1;

      /* Remove everything that is not audio */
      TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {
	if(s->s_stop)
	  break;
	if((p = nde->nde_opaque) == NULL)
	  continue;
	
	if(nde->nde_type != CONTENT_AUDIO) {
	  prop_destroy(p);
	} else {
	  prop_set_int(prop_create(prop_create(p, "metadata"),
				   "trackindex"), trackidx);
	  trackidx++;
	}
      }
      
    } else if(nd->nd_count == destroyed) {
      prop_set_string(s->s_viewprop, "empty");
    } else if(images * 4 > nd->nd_count * 3) {
      prop_set_string(s->s_viewprop, "images");
    }
  }

  nav_dir_free(nd);
  fa_unreference(ref);
}


/**
 *
 */
static void *
scanner(void *aux)
{
  scanner_t *s = aux;

  printf("Starting scan of %s\n", s->s_url);
  scannercore(s);

  printf("Scan of %s completed\n", s->s_url);

  free(s->s_url);
  prop_ref_dec(s->s_root);
  prop_ref_dec(s->s_nodes);

  if(s->s_viewprop != NULL)
    prop_ref_dec(s->s_viewprop);

  free(s);
  return NULL;
}




void
fa_scanner(const char *url, prop_t *root, int flags)
{
  scanner_t *s = calloc(1, sizeof(scanner_t));

  s->s_url = strdup(url);

  s->s_root = root;
  prop_ref_inc(s->s_root);

  s->s_nodes = prop_create(root, "nodes");
  prop_ref_inc(s->s_nodes);

  if(flags & FA_SCANNER_DETERMINE_VIEW) {

    s->s_viewprop = prop_create(root, "view");
    prop_ref_inc(s->s_viewprop);
  }

  hts_thread_create_detached(scanner, s);
}
