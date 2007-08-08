/*
 *  RSS supporting functions
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

#define _GNU_SOURCE

#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>


#include <curl/curl.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "showtime.h"

#include "rss.h"

static void rss_parse_root(xmlNode *n, rssfeed_t *rss);

static void rss_dump(rssfeed_t *rss) __attribute__((unused));


static size_t
rss_http_write(void *ptr, size_t size, size_t nmemb, void *aux)
{
  rssfeed_t *rss = aux;
  size_t chunk = size * nmemb;

  rss->xmldoc = realloc(rss->xmldoc, rss->xmllen + chunk );

  memcpy(rss->xmldoc + rss->xmllen, ptr, chunk);
  
  rss->xmllen += chunk;

  return chunk;
}



rssfeed_t *
rss_load(const char *url)
{
  rssfeed_t *rss = calloc(1, sizeof(rssfeed_t));
  CURL *ch;
  xmlDoc *doc = NULL;
  xmlNode *root_element = NULL;

  TAILQ_INIT(&rss->channels);

  ch = curl_easy_init();
  curl_easy_setopt(ch, CURLOPT_URL, url);


#if 0
  curl_easy_setopt(ch, CURLOPT_HEADERDATA, rss);
  curl_easy_setopt(ch, CURLOPT_HEADERFUNCTION, rss_http_header);
#endif

  curl_easy_setopt(ch, CURLOPT_WRITEDATA, rss);
  curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, rss_http_write);
    
  curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 5);
  curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1);
  curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1);
    
  curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

  //  curl_easy_setopt(ch, CURLOPT_VERBOSE, 1);

  curl_easy_perform(ch);
  curl_easy_cleanup(ch);



  doc = xmlParseMemory(rss->xmldoc, rss->xmllen);
  if(doc == NULL) {
    free(rss->xmldoc);
    free(rss);
    return NULL;
  }


  root_element = xmlDocGetRootElement(doc);
  
  rss_parse_root(root_element, rss);

  xmlFreeDoc(doc);

  xmlCleanupParser();

  //  rss_dump(rss);

  return rss;
}

void 
rss_free(rssfeed_t *rss)
{
  rss_channel_t *rch;
  rss_item_t *ri;

  while((rch = TAILQ_FIRST(&rss->channels)) != NULL) {
    TAILQ_REMOVE(&rss->channels, rch, link);

    while((ri = TAILQ_FIRST(&rch->items)) != NULL) {
      TAILQ_REMOVE(&rch->items, ri, link);

      free(ri->desc);
      free(ri->title);
      free(ri);

    }
    free(rch->title);
    free(rch->desc);
    free(rch->itemvec);
    free(rch);
  }
  free(rss);
}


#define XML_FOREACH(n) for(; (n) != NULL; (n) = (n)->next)

static void
rss_parse_item(xmlNode *n, rss_channel_t *rch)
{
  rss_item_t *ri;

  ri = calloc(1, sizeof(rss_item_t));

  TAILQ_INSERT_TAIL(&rch->items, ri, link);

  XML_FOREACH(n) {
    if(n->type != XML_ELEMENT_NODE)
      continue;

    if(!strcmp((char *)n->name, "enclosure")) {
      free((void *)ri->media);
      ri->media = (char *)xmlGetProp(n, (unsigned char *)"url");
    } else if(!strcmp((char *)n->name, "title")) {
      free((void *)ri->title);
      ri->title = strdup((char *)xmlNodeGetContent(n));
    } else if(!strcmp((char *)n->name, "description")) {
      free((void *)ri->desc);
      ri->desc = strdup((char *)xmlNodeGetContent(n));
    }
  }
}



static void
rss_parse_image(xmlNode *n, rss_channel_t *rch)
{
  XML_FOREACH(n) {
    if(n->type != XML_ELEMENT_NODE)
      continue;

    if(!strcmp((char *)n->name, "url")) {
      free((void *)rch->image);
      rch->image = strdup((char *)xmlNodeGetContent(n));
    }
  }
}

static void
rss_parse_channel(xmlNode *n, rssfeed_t *rss)
{
  rss_channel_t *rch;
  rss_item_t *ri;
  int i;

  rch = calloc(1, sizeof(rss_channel_t));
  TAILQ_INSERT_TAIL(&rss->channels, rch, link);
  TAILQ_INIT(&rch->items);

  XML_FOREACH(n) {
    if(n->type != XML_ELEMENT_NODE)
      continue;

    if(!strcmp((char *)n->name, "title")) {
      free((void *)rch->title);
      rch->title = strdup((char *)xmlNodeGetContent(n));
    } else if(!strcmp((char *)n->name, "description")) {
      free((void *)rch->desc);
      rch->desc = strdup((char *)xmlNodeGetContent(n));
    } else if(!strcmp((char *)n->name, "image")) {
      rss_parse_image(n->children, rch);
    } else if(!strcmp((char *)n->name, "item")) {
      rss_parse_item(n->children, rch);
      rch->nitems++;
    }
  }

  if(rch->nitems == 0)
    return;

  i = 0;
  rch->itemvec = malloc(rch->nitems * sizeof(rss_item_t *));

  TAILQ_FOREACH(ri, &rch->items, link) 
    rch->itemvec[i++] = ri;

}

static void
rss_parse_rss(xmlNode *n, rssfeed_t *rss)
{
  XML_FOREACH(n) {
    if(n->type == XML_ELEMENT_NODE && !strcmp((char *)n->name, "channel"))
      rss_parse_channel(n->children, rss);
  }
}

static void
rss_parse_root(xmlNode *n, rssfeed_t *rss)
{
  XML_FOREACH(n) {
    if(n->type == XML_ELEMENT_NODE && !strcmp((char *)n->name, "rss"))
      rss_parse_rss(n->children, rss);
  }
}



static void 
rss_dump(rssfeed_t *rss)
{
  rss_channel_t *rch;
  rss_item_t *ri;

  TAILQ_FOREACH(rch, &rss->channels, link) {
    printf("%s\n", rch->title);

    TAILQ_FOREACH(ri, &rch->items, link) {
      printf("\t%s\n\t%s\n",  ri->title, ri->desc);
    }
  }
}
