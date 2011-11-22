/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2011 Andreas Öman
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
#include <unistd.h>

#include <libavformat/avformat.h>

#include "prop/prop.h"
#include "ext/sqlite/sqlite3.h"

#include "showtime.h"
#include "media.h"

#include "api/lastfm.h"

#include "metadata.h"
#include "fileaccess/fileaccess.h"

#include "db/db_support.h"

#include "video/video_settings.h"

/**
 *
 */
metadata_t *
metadata_create(void)
{
  metadata_t *md = calloc(1, sizeof(metadata_t));
  TAILQ_INIT(&md->md_streams);
  return md;
}

/**
 *
 */
void
metadata_destroy(metadata_t *md)
{
  metadata_stream_t *ms;
  rstr_release(md->md_title);
  rstr_release(md->md_album);
  rstr_release(md->md_artist);
  rstr_release(md->md_format);

  free(md->md_redirect);

  while((ms = TAILQ_FIRST(&md->md_streams)) != NULL) {
    TAILQ_REMOVE(&md->md_streams, ms, ms_link);
    rstr_release(ms->ms_title);
    rstr_release(ms->ms_info);
    rstr_release(ms->ms_isolang);
    rstr_release(ms->ms_codec);
    free(ms);
  }
  free(md);
}


/**
 *
 */
void
metadata_add_stream(metadata_t *md, const char *codec, int type,
		    int streamindex,
		    const char *title, const char *info, const char *isolang,
		    int disposition, int tracknum)
{
  metadata_stream_t *ms = malloc(sizeof(metadata_stream_t));
  ms->ms_title = rstr_alloc(title);
  ms->ms_info = rstr_alloc(info);
  ms->ms_isolang = rstr_alloc(isolang);
  ms->ms_codec = rstr_alloc(codec);
  ms->ms_type = type;
  ms->ms_disposition = disposition;
  ms->ms_streamindex = streamindex;
  ms->ms_tracknum = tracknum;
  TAILQ_INSERT_TAIL(&md->md_streams, ms, ms_link);
}



static const char *types[] = {
  [CONTENT_UNKNOWN]  = "unknown",
  [CONTENT_DIR]      = "directory",
  [CONTENT_FILE]     = "file",
  [CONTENT_AUDIO]    = "audio",
  [CONTENT_ARCHIVE]  = "archive",
  [CONTENT_VIDEO]    = "video",
  [CONTENT_PLAYLIST] = "playlist",
  [CONTENT_DVD]      = "dvd",
  [CONTENT_IMAGE]    = "image",
  [CONTENT_ALBUM]    = "album",
};


/**
 *
 */
const char *
content2type(contenttype_t ctype)
{
  if (ctype < 0 || ctype >= sizeof(types) / sizeof(types[0]))
    return NULL;

  return types[ctype];
}


contenttype_t
type2content(const char *str)
{
  int i;
  for(i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    if(!strcmp(str, types[i]))
      return i;
  }
  return CONTENT_UNKNOWN;
}


/**
 *
 */
static void
metadata_stream_make_prop(const metadata_stream_t *ms, prop_t *parent)
{
  char url[16];
  int score = 0;
  rstr_t *title;

  snprintf(url, sizeof(url), "libav:%d", ms->ms_streamindex);

  if(ms->ms_disposition & AV_DISPOSITION_DEFAULT)
    score += 10;
  
  if(ms->ms_title != NULL) {
    title = rstr_dup(ms->ms_title);
  } else {
    char buf[256];
    rstr_t *fmt = _("Track %d");

    snprintf(buf, sizeof(buf), rstr_get(fmt), ms->ms_tracknum);
    title = rstr_alloc(buf);
    rstr_release(fmt);
  }

  mp_add_trackr(parent,
		title,
		url,
		ms->ms_codec,
		ms->ms_info,
		ms->ms_isolang,
		NULL,
		_p("Embedded in file"),
	       score);

  rstr_release(title);
}


/**
 *
 */
void
metadata_to_proptree(const metadata_t *md, prop_t *proproot,
		     int overwrite_title)
{
  metadata_stream_t *ms;

  if(md->md_title != NULL)
    prop_set_rstring_ex(prop_create(proproot, "title"),
			NULL, md->md_title, !overwrite_title);

  if(md->md_artist) {
    prop_set_rstring(prop_create(proproot, "artist"), md->md_artist);

    lastfm_artistpics_init(prop_create(proproot, "artist_images"),
			   md->md_artist);
  }

  if(md->md_album) {
    prop_set_rstring(prop_create(proproot, "album"),  md->md_album);

    if(md->md_artist != NULL)
      lastfm_albumart_init(prop_create(proproot, "album_art"),
			   md->md_artist, md->md_album);
  }

  TAILQ_FOREACH(ms, &md->md_streams, ms_link) {

    prop_t *p;

    switch(ms->ms_type) {
    case AVMEDIA_TYPE_AUDIO:
      p = prop_create(proproot, "audiostreams");
      break;
    case AVMEDIA_TYPE_VIDEO:
      p = prop_create(proproot, "videostreams");
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      p = prop_create(proproot, "subtitlestreams");
      break;
    default:
      continue;
    }
    metadata_stream_make_prop(ms, p);
  }

  if(md->md_format != NULL)
    prop_set_rstring(prop_create(proproot, "format"), md->md_format);

  if(md->md_duration)
    prop_set_float(prop_create(proproot, "duration"), md->md_duration);

  if(md->md_tracks)
    prop_set_int(prop_create(proproot, "tracks"), md->md_tracks);

  if(md->md_time)
    prop_set_int(prop_create(proproot, "timestamp"), md->md_time);
}

