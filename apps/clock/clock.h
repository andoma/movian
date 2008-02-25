/*
 *  Browser for apple movie trailers, currently not working
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <libglw/glw.h>

#include <curl/curl.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "showtime.h"
#include "input.h"
#include "play_file.h"
#include "movt.h"
#include "miw.h"
#include "layout/layout.h"

#define XML_FOREACH(n) for(; (n) != NULL; (n) = (n)->next)

typedef struct movt {
  media_pipe_t mt_mp;

  char *mt_xmldoc;
  int mt_xmllen;

  glw_t *mt_vlist;

} movt_t;



static size_t
movt_http_write(void *ptr, size_t size, size_t nmemb, void *aux)
{
  movt_t *rss = aux;
  size_t chunk = size * nmemb;

  rss->mt_xmldoc = realloc(rss->mt_xmldoc, rss->mt_xmllen + chunk);
  memcpy(rss->mt_xmldoc + rss->mt_xmllen, ptr, chunk);
  rss->mt_xmllen += chunk;
  return chunk;
}


static void
parse_poster(xmlNode *n, movt_t *mt, glw_t *parent)
{
  XML_FOREACH(n) {
    if(!strcmp((char *)n->name, "location")) {
     glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, (char *)xmlNodeGetContent(n),
		 GLW_ATTRIB_PARENT, parent,
		 NULL);
      return;
    }
  }
}

static void
parse_preview(xmlNode *n, movt_t *mt, glw_t *parent)
{
  XML_FOREACH(n) {
    if(!strcmp((char *)n->name, "large")) {
      glw_set(parent, 
	      GLW_ATTRIB_OPAQUE, strdup((char *)xmlNodeGetContent(n)),
	      NULL);
      return;
    }
  }
}



static void
parse_info(xmlNode *n, movt_t *mt, glw_t *parent)
{
  glw_t *title,  *desc, *x, *y, *runtime, *reldate;

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, parent,
		 NULL);

  title = glw_create(GLW_CONTAINER,
		     GLW_ATTRIB_PARENT, x,
		     NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, x,
		 NULL);
  
  reldate = glw_create(GLW_CONTAINER,
		       GLW_ATTRIB_PARENT, y,
		       NULL);

  runtime = glw_create(GLW_CONTAINER,
		       GLW_ATTRIB_PARENT, y,
		       NULL);

  desc = glw_create(GLW_CONTAINER_X,
		    GLW_ATTRIB_PARENT, parent,
		    NULL);


  XML_FOREACH(n) {

    if(!strcmp((char *)n->name, "title")) {
     glw_create(GLW_TEXT_VECTOR,
		GLW_ATTRIB_CAPTION, (char *)xmlNodeGetContent(n),
		GLW_ATTRIB_PARENT, title,
		GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		NULL);
     continue;
    }

    if(!strcmp((char *)n->name, "runtime")) {
     glw_create(GLW_TEXT_VECTOR,
		GLW_ATTRIB_CAPTION, (char *)xmlNodeGetContent(n),
		GLW_ATTRIB_PARENT, runtime,
		GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		NULL);
     continue;
    }

    if(!strcmp((char *)n->name, "releasedate")) {
     glw_create(GLW_TEXT_VECTOR,
		GLW_ATTRIB_CAPTION, (char *)xmlNodeGetContent(n),
		GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		GLW_ATTRIB_PARENT, reldate,
		GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		NULL);
     continue;
    }

    if(!strcmp((char *)n->name, "description")) {
     glw_create(GLW_TEXT_BITMAP,
		GLW_ATTRIB_CAPTION, (char *)xmlNodeGetContent(n),
		GLW_ATTRIB_PARENT, desc,
		GLW_ATTRIB_LINES, 4,
		GLW_ATTRIB_COLOR, GLW_COLOR_WHITE,
		GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		NULL);
     continue;
    }


  }
}

static void
parse_movie_info(xmlNode *n, movt_t *mt)
{
  glw_t *c;
  glw_t *poster, *info;

  c = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, mt->mt_vlist,
		 NULL);

  poster = glw_create(GLW_CONTAINER_Y,
		      GLW_ATTRIB_PARENT, c,
		      NULL);

  info = glw_create(GLW_CONTAINER_Y, 
		    GLW_ATTRIB_PARENT, c,
		    GLW_ATTRIB_WEIGHT, 6.0f,
		    NULL);

  XML_FOREACH(n) {
    if(n->type != XML_ELEMENT_NODE)
      continue;

    
    if(!strcmp((char *)n->name, "poster"))
      parse_poster(n->children, mt, poster);
    else if(!strcmp((char *)n->name, "info"))
      parse_info(n->children, mt, info);
    else if(!strcmp((char *)n->name, "preview"))
      parse_preview(n->children, mt, c);
  }
}



static void
parse_records(xmlNode *n, movt_t *mt)
{
  XML_FOREACH(n) {
    if(n->type == XML_ELEMENT_NODE && !strcmp((char *)n->name, "movieinfo"))
      parse_movie_info(n->children, mt);
   }
}


static void
parse_root(xmlNode *n, movt_t *mt)
{
  XML_FOREACH(n) {
    if(n->type == XML_ELEMENT_NODE && !strcmp((char *)n->name, "records"))
      parse_records(n->children, mt);
  }
}


static int
movt_load(movt_t *mt, const char *url)
{

  CURL *ch;
  CURLcode err;
  xmlDoc *doc = NULL;
  xmlNode *root_element = NULL;

  ch = curl_easy_init();
  curl_easy_setopt(ch, CURLOPT_URL, url);

  curl_easy_setopt(ch, CURLOPT_WRITEDATA, mt);
  curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, movt_http_write);
    
  curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 5);
  curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1);
  curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1);
    
  curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

  curl_easy_setopt(ch, CURLOPT_VERBOSE, 1);

  err = curl_easy_perform(ch);
  curl_easy_cleanup(ch);

  if(err)
    return 1;

  doc = xmlParseMemory(mt->mt_xmldoc, mt->mt_xmllen);
  if(doc == NULL) {
    free(mt->mt_xmldoc);
    return 1;
  }


  root_element = xmlDocGetRootElement(doc);
  
  parse_root(root_element, mt);

  free(mt->mt_xmldoc);

  return 0;
}





/*
 *
 */

static void *
movt_thread(void *aux)
{
  appi_t *ai = aux;
  movt_t *mt = calloc(1, sizeof(movt_t));
  inputevent_t ie;
  glw_t *w, *loading, *errw = NULL;
  char *movurl;

  mt->mt_vlist = glw_create(GLW_SLIST_Y,
			    GLW_ATTRIB_SHOW_CHILDS, 5,
			    NULL);

  while(1) {

    loading = miw_loading(ai->ai_widget, "...loading trailers");

    if(!movt_load(mt, "http://www.apple.com/trailers/home/xml/current.xml"))
      break;
    
    glw_destroy(loading);
    errw = glw_create(GLW_TEXT_VECTOR,
		      GLW_ATTRIB_PARENT, ai->ai_widget,
		      GLW_ATTRIB_COLOR, GLW_COLOR_RED,
		      GLW_ATTRIB_CAPTION, "Loading failed",
		      NULL);
    sleep(1);
    glw_destroy(errw);
  }

  glw_destroy(loading);

  glw_set(mt->mt_vlist, 
	  GLW_ATTRIB_PARENT, ai->ai_widget,
	  NULL);


  while(1) {

    input_getevent(&ai->ai_ic, 1, &ie, NULL);

    switch(ie.type) {

    default:
      break;

    case INPUT_PAD:
      pad_nav_slist(mt->mt_vlist, &ie);
      break;
      
    case INPUT_KEY:
	
      switch(ie.u.key) {
      default:
	break;

      case INPUT_KEY_UP:
	glw_child_prev(mt->mt_vlist);
	break;
	
      case INPUT_KEY_DOWN:
	glw_child_next(mt->mt_vlist);
	break;

      case INPUT_KEY_ENTER:
	w = mt->mt_vlist->glw_selected;
	if(w == NULL)
	  break;

	movurl = (char *)glw_get_opaque(w);
	if(movurl == NULL)
	  break;

	//	play_file(movurl, ai, &ai->ai_ic, NULL, NULL);
	break;
	
      case INPUT_KEY_BACK:
	layout_hide(ai);
	break;
      }
    }
  }
  return NULL;
}



/*
 *
 */

void 
movt_spawn(appi_t *ai)
{
  pthread_create(&ai->ai_tid, NULL, movt_thread, ai);
}

app_t app_trailers = {
  .app_name = "Apple Movie Trailers",
  .app_icon = "icon://video.png",
  .app_spawn = movt_spawn
};
