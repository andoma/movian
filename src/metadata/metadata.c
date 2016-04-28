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
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <sqlite3.h>

#include "prop/prop.h"
#include "prop/prop_concat.h"

#include "main.h"
#include "media/media.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/str.h"

#include "metadata.h"
#include "metadata_sources.h"

#include "fileaccess/fileaccess.h"

#include "db/db_support.h"
#include "db/kvstore.h"

#include "video/video_settings.h"

#include "settings.h"
#include "subtitles/subtitles.h"


/**
 *
 */
metadata_t *
metadata_create(void)
{
  metadata_t *md = calloc(1, sizeof(metadata_t));
  TAILQ_INIT(&md->md_streams);
  TAILQ_INIT(&md->md_cast);
  TAILQ_INIT(&md->md_crew);
  md->md_rating = -1;
  md->md_rating_count = -1;
  md->md_idx = -1;
  return md;
}


/**
 *
 */
static void
destroy_persons(struct metadata_person_queue *q)
{
  metadata_person_t *mp;

  while((mp = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, mp, mp_link);
    rstr_release(mp->mp_name);
    rstr_release(mp->mp_character);
    rstr_release(mp->mp_department);
    rstr_release(mp->mp_job);
    rstr_release(mp->mp_portrait);
    free(mp);
  }
}


/**
 *
 */
void
metadata_destroy(metadata_t *md)
{
  if(md->md_parent != NULL)
    metadata_destroy(md->md_parent);

  rstr_release(md->md_title);
  rstr_release(md->md_album);
  rstr_release(md->md_artist);
  rstr_release(md->md_format);
  rstr_release(md->md_genre);
  rstr_release(md->md_description);
  rstr_release(md->md_tagline);
  rstr_release(md->md_imdb_id);
  rstr_release(md->md_icon);
  rstr_release(md->md_backdrop);
  rstr_release(md->md_banner_wide);
  rstr_release(md->md_manufacturer);
  rstr_release(md->md_equipment);
  rstr_release(md->md_ext_id);

  free(md->md_redirect);

  metadata_stream_t *ms;

  while((ms = TAILQ_FIRST(&md->md_streams)) != NULL) {
    TAILQ_REMOVE(&md->md_streams, ms, ms_link);
    rstr_release(ms->ms_title);
    rstr_release(ms->ms_info);
    rstr_release(ms->ms_isolang);
    rstr_release(ms->ms_codec);
    free(ms);
  }

  destroy_persons(&md->md_cast);
  destroy_persons(&md->md_crew);
  free(md);
}


/**
 *
 */
void
metadata_add_stream(metadata_t *md, const char *codec, int type,
		    int streamindex,
		    const char *title, const char *info, const char *isolang,
		    int disposition, int tracknum, int channels)
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
  ms->ms_channels = channels;
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
  [CONTENT_PLUGIN]   = "plugin",
  [CONTENT_FONT]     = "font",
  [CONTENT_SHARE]    = "share",
  [CONTENT_DOCUMENT] = "document",
};


/**
 *
 */
const char *
content2type(contenttype_t ctype)
{
  if(ctype >= sizeof(types) / sizeof(types[0]))
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
metadata_stream_make_prop(const metadata_stream_t *ms, prop_t *parent,
                          int score, int autosel)
{
  char url[16];
  rstr_t *title;

  snprintf(url, sizeof(url), "libav:%d", ms->ms_streamindex);

  if(ms->ms_disposition & 1) // default
    score += 10;
  else
    score += 5;

  if(ms->ms_channels > 2)
    score++;
  if(ms->ms_channels > 0)
    score++;

  if(ms->ms_title != NULL) {
    title = rstr_dup(ms->ms_title);
  } else {
    char buf[256];
    rstr_t *fmt = _("Track %d");

    snprintf(buf, sizeof(buf), rstr_get(fmt), ms->ms_tracknum);
    title = rstr_alloc(buf);
    rstr_release(fmt);
  }

  prop_t *p = mp_add_trackr(parent,
                            title,
                            url,
                            ms->ms_codec,
                            ms->ms_info,
                            ms->ms_isolang,
                            NULL,
                            _p("Embedded in file"),
                            score,
                            autosel);
  prop_ref_dec(p);

  rstr_release(title);
}


/**
 *
 */
void
metadata_to_proptree(const metadata_t *md, prop_t *proproot,
		     int cleanup_streams)
{
  metadata_stream_t *ms;
  int ac = 0, vc = 0, sc = 0, *pc;

  if(md->md_title != NULL)
    prop_set(proproot, "title", PROP_SET_RSTRING, md->md_title);

  if(md->md_artist) {
    prop_set(proproot, "artist", PROP_SET_RSTRING, md->md_artist);

    metadata_bind_artistpics(prop_create(proproot, "artist_images"),
			     md->md_artist);
  }

  if(md->md_icon != NULL)
    prop_set(proproot, "icon", PROP_SET_RSTRING, md->md_icon);

  if(md->md_album) {
    prop_set(proproot, "album", PROP_SET_RSTRING, md->md_album);

    if(md->md_artist != NULL)
      metadata_bind_albumart(prop_create(proproot, "album_art"),
			     md->md_artist, md->md_album);
  }

  TAILQ_FOREACH(ms, &md->md_streams, ms_link) {

    prop_t *p;
    int score = 0;
    int autosel = 1;
    switch(ms->ms_type) {
    case MEDIA_TYPE_AUDIO:
      p = prop_create(proproot, "audiostreams");
      pc = &ac;
      break;
    case MEDIA_TYPE_VIDEO:
      p = prop_create(proproot, "videostreams");
      pc = &vc;
      break;
    case MEDIA_TYPE_SUBTITLE:
      score   = subtitles_embedded_score();
      autosel = subtitles_embedded_autosel();
      p = prop_create(proproot, "subtitlestreams");
      pc = &sc;
      break;
    default:
      continue;
    }
    if(cleanup_streams && *pc == 0) {
      prop_destroy_childs(p);
      *pc = 1;
    }
    if(score == -1)
      continue;
    metadata_stream_make_prop(ms, p, score, autosel);
  }

  if(md->md_format != NULL)
    prop_set(proproot, "format", PROP_SET_RSTRING, md->md_format);

  if(md->md_duration)
    prop_set(proproot, "duration", PROP_SET_FLOAT, md->md_duration);

  if(md->md_tracks)
    prop_set(proproot, "tracks", PROP_SET_INT, md->md_tracks);

  if(md->md_track)
    prop_set(proproot, "track", PROP_SET_INT, md->md_track);

  if(md->md_time)
    prop_set(proproot, "timestamp", PROP_SET_INT, md->md_time);

  if(md->md_manufacturer != NULL)
    prop_set(proproot, "manufacturer", PROP_SET_RSTRING, md->md_manufacturer);

  if(md->md_equipment != NULL)
    prop_set(proproot, "equipment", PROP_SET_RSTRING, md->md_equipment);

  if(md->md_tagline != NULL)
    prop_set(proproot, "tagline", PROP_SET_RSTRING, md->md_tagline);

  if(md->md_description != NULL)
    prop_set(proproot, "description", PROP_SET_RSTRING, md->md_description);
}



/**
 *
 */
void
metadata_init(void)
{
  mlp_init();
  metadata_sources_init();
}



const char *
metadata_qtypestr(int qtype)
{
  switch(qtype) {
  case METADATA_QTYPE_FILENAME:
    return "filename";
  case METADATA_QTYPE_IMDB:
    return "IMDB ID";
  case METADATA_QTYPE_CUSTOM_IMDB:
    return "Custom IMDB ID";
  case METADATA_QTYPE_DIRECTORY:
    return "Folder name";
  case METADATA_QTYPE_CUSTOM:
    return "Custom query";
  case METADATA_QTYPE_EPISODE:
    return "Filename as TV episode";
  case METADATA_QTYPE_MOVIE:
    return "Movie title";
  case METADATA_QTYPE_TVSHOW:
    return "Title, Season, Episode";
  default:
    return "???";
  }
}
