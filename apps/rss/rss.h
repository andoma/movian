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

#ifndef RSS_H
#define RSS_H

TAILQ_HEAD(rss_channel_queue, rss_channel);
TAILQ_HEAD(rss_item_queue, rss_item);

typedef struct rss_item {
  TAILQ_ENTRY(rss_item) link;
  char *title;
  char *desc;
  char *media;

} rss_item_t;

typedef struct rss_channel {
  TAILQ_ENTRY(rss_channel) link;
  char *title;
  char *desc;
  char *image;

  struct rss_item_queue items;

  rss_item_t **itemvec;
  int nitems;

} rss_channel_t;

typedef struct rssfeed {
  char *xmldoc;
  
  int xmllen;
  
  struct rss_channel_queue channels;
  

} rssfeed_t;


rssfeed_t *rss_load(const char *url);

void rss_free(rssfeed_t *rss);

#endif /* RSS_H */
