/*
 *  Functions for exposing rar archives as virtual file systems
 *  Copyright (C) 2008 Andreas Öman
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

#include "config.h"

#include <assert.h>
#include <alloca.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "showtime.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "misc/string.h"

/**
 *
 */
static int
sidfile_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  void *fh = NULL;
  char *p, *fpath = mystrdupa(url);
  char buf[128];
  char name[32];
  char turl[URL_MAX];
  int tracks, i;
  fa_dir_entry_t *fde;
  rstr_t *album, *artist;
 

  if((p = strrchr(fpath, '/')) == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return -1;
  }

  *p = 0;
  if((fh = fa_open(fpath, errbuf, errlen)) == NULL)
    return -1;

  if(fa_read(fh, buf, 128) != 128) {
    snprintf(errbuf, errlen, "Unable to read file");
    fa_close(fh);
    return -1;
  }

  album = rstr_alloc(utf8_from_bytes((char *)buf + 0x16, 32, NULL));
  artist = rstr_alloc(utf8_from_bytes((char *)buf + 0x36, 32, NULL));

  tracks = buf[0xf];
  for(i = 0; i < tracks; i++) {

    snprintf(name, sizeof(name), "Track %02d", i + 1);
    snprintf(turl, sizeof(turl), "sidplayer:%s/%d", fpath, i + 1);
    fde = fa_dir_add(fd, turl, name, CONTENT_AUDIO);

    fde->fde_probestatus = FDE_PROBE_DEEP;

    fde->fde_metadata = prop_create_root("metadata");
    prop_set_string(prop_create(fde->fde_metadata, "title"), name);
    prop_set_rstring(prop_create(fde->fde_metadata, "album"), album);
    prop_set_rstring(prop_create(fde->fde_metadata, "artist"), artist);
  }

  rstr_release(album);
  rstr_release(artist);

  fa_close(fh);
  return 0;
}

/**
 * Standard unix stat
 */
static int
sidfile_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	     char *errbuf, size_t errlen, int non_interactive)
{
  char *p = strrchr(url, '/');
  
  if(p == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return -1;
  }

  memset(fs, 0, sizeof(struct fa_stat));

  p++;
  if(*p == 0) {
    fs->fs_type = CONTENT_DIR;
  } else {
    return -1;
  }

  return -1;
}


static fa_protocol_t fa_protocol_sidfile = {
  .fap_name = "sidfile",
  .fap_scan =  sidfile_scandir,
  .fap_stat  = sidfile_stat,
};
FAP_REGISTER(sidfile);
