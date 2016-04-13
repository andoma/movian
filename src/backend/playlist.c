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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "main.h"
#include "navigator.h"
#include "backend.h"
#include "usage.h"

#include "fileaccess/fileaccess.h"
#include "misc/str.h"
#include "misc/dbl.h"

/**
 *
 */
static int
playlist_canhandle(const char *url)
{
  return !strncmp(url, "playlist:", strlen("playlist:"));
}


/**
 *
 */
static int
playlist_open(prop_t *page, const char *url, int sync)
{
  char errbuf[512];
  url += strlen("playlist:");

  usage_page_open(sync, "Playlist");

  prop_t *model = prop_create_r(page, "model");
  prop_set(model, "type", PROP_SET_STRING, "directory");
  prop_set(model, "loading", PROP_SET_INT, 1);

  prop_t *meta = prop_create_r(model, "metadata");
  prop_set(meta, "title", PROP_ADOPT_RSTRING, fa_get_title(url));

  buf_t *buf = fa_load(url,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       NULL);

  prop_t *nodes = prop_create_r(model, "nodes");

  if(buf != NULL) {
    buf = buf_make_writable(buf);

    double duration = -1;
    const char *title = NULL;

    LINEPARSE(s, buf_str(buf)) {
      const char *v;
      if((v = mystrbegins(s, "#EXTINF:")) != NULL) {
        duration = my_str2double(v, NULL);

        title = strchr(v, ',');
        if(title != NULL)
          title++;
      } else if(s[0] != '#') {

        char *itemurl = url_resolve_relative_from_base(url, s);

        if(backend_canhandle(itemurl)) {

          prop_t *item = prop_create_root(NULL);
          prop_set(item, "url", PROP_SET_STRING, itemurl);
          prop_t *metadata = prop_create(item, "metadata");

          prop_set(item, "type", PROP_SET_STRING, "file");
          if(title != NULL) {
            prop_set(metadata, "title", PROP_SET_STRING, title);
          } else {
            prop_set(metadata, "title", PROP_ADOPT_RSTRING,
                     fa_get_title(itemurl));
          }

          if(duration > 0)
            prop_set(metadata, "duration", PROP_SET_FLOAT, duration);


          if(prop_set_parent(item, nodes))
            prop_destroy(item);
        }
        title = NULL;
      }
    }

    buf_release(buf);
  } else {
    nav_open_errorf(page, _("Unable to open playlist"));
  }

  prop_set(model, "loading", PROP_SET_INT, 0);

  prop_ref_dec(nodes);
  prop_ref_dec(model);
  prop_ref_dec(meta);
  return 0;
}


/**
 *
 */
static backend_t be_playlist = {
  .be_canhandle = playlist_canhandle,
  .be_open = playlist_open,
};

BE_REGISTER(playlist);
