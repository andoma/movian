/*
 *  Copyright (C) 2013 Andreas Öman
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
#include <string.h>
#include <unistd.h>

#include "showtime.h"
#include "bittorrent.h"
#include "settings.h"
#include "htsmsg/htsmsg_store.h"

static int allow_update = 0;


static void
set_torrent_cache_path(void *opaque, const char *str)
{
  rstr_release(btg.btg_cache_path);
  btg.btg_cache_path = rstr_alloc(str);
  if(allow_update)
    torrent_diskio_scan();
}




static void
set_torrent_free_percentage(void *opaque, int v)
{
  btg.btg_free_space_percentage = v;
  if(allow_update)
    torrent_diskio_scan();
}


void
torrent_settings_init(void)
{
  htsmsg_t *store = htsmsg_store_load("bittorrent") ?: htsmsg_create_map();
  prop_t *s = gconf.settings_bittorrent;

  char defpath[1024];

  int freespace = 10;

#if RPISTOS
  freespace = 75;
#endif

  snprintf(defpath, sizeof(defpath), "%s/bittorrentcache", gconf.cache_path);

  setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Enable bittorrent")),
                 SETTING_MUTEX(&bittorrent_mutex),
                 SETTING_WRITE_BOOL(&btg.btg_enabled),
                 SETTING_VALUE(1),
                 SETTING_HTSMSG("enable", store, "bittorrent"),
                 NULL);

  setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Max usage of free space for caching torrents")),
                 SETTING_MUTEX(&bittorrent_mutex),
                 SETTING_CALLBACK(set_torrent_free_percentage, NULL),
                 SETTING_VALUE(freespace),
                 SETTING_RANGE(1, 90),
                 SETTING_UNIT_CSTR("%"),
                 SETTING_HTSMSG("freepercentage", store, "bittorrent"),
                 NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Torrent cache path")),
                 SETTING_MUTEX(&bittorrent_mutex),
                 SETTING_CALLBACK(set_torrent_cache_path, NULL),
                 SETTING_VALUE(defpath),
                 SETTING_HTSMSG("path", store, "bittorrent"),
                 NULL);


  settings_create_separator(s, _p("Status"));

  btg.btg_torrent_status = prop_create_root(NULL);
  settings_create_info(s, NULL, btg.btg_torrent_status);

  btg.btg_disk_status = prop_create_root(NULL);
  settings_create_info(s, NULL, btg.btg_disk_status);

  allow_update = 1;
  torrent_diskio_scan();

}
