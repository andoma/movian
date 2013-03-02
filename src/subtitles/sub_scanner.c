/*
 *  Scanning of external subtitles
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

#include "showtime.h"
#include "prop/prop.h"
#include "arch/threads.h"
#include "fileaccess/fileaccess.h"
#include "misc/isolang.h"
#include "media.h"
#include "vobsub.h"
#include "backend/backend.h"
#include "js/js.h"

#include "sub_scanner.h"
#include "video/video_settings.h"
#include "backend/backend.h"


/**
 *
 */
static int
fs_sub_match(const char *video, const char *sub)
{
  sub = strrchr(sub, '/');
  if(sub == NULL)
    return 0;
  sub++;

  int vl = strlen(video);
  int sl = strlen(sub);

  if(sl >= vl && sub[vl] == '.' && !strncasecmp(sub, video, vl))
    return 1;

  char *x = strrchr(sub, '.');
  if(x != NULL) {
    size_t off = x - sub;
    if(vl > off) {
      if((video[off] == '.' || video[off] == ' ') && 
	 !strncasecmp(sub, video, off))
	return 1;
    }
  }
  return 0;
}


#define LOCAL_EXTRA_SCORE 3

/**
 *
 */
static void
fs_sub_scan_dir(sub_scanner_t *ss, const char *url, const char *video)
{
  char *postfix;
  fa_dir_t *fd;
  fa_dir_entry_t *fde;
  char errbuf[256];

  TRACE(TRACE_DEBUG, "Video", "Scanning for subs in %s for %s", url, video);

  if((fd = fa_scandir(url, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "Video", "Unable to scan %s for subtitles: %s",
	  url, errbuf);
    return;
  }

  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(ss->ss_stop)
      break;

    if(fde->fde_type == CONTENT_DIR &&
       !strcasecmp(rstr_get(fde->fde_filename), "subs")) {
      fs_sub_scan_dir(ss, rstr_get(fde->fde_url), video);
      continue;
    }
    const char *filename = rstr_get(fde->fde_filename);
    postfix = strrchr(filename, '.');
    if(postfix == NULL)
      continue;

    if(!strcasecmp(postfix, ".srt")) {
      const char *lang = NULL;
      if(postfix - filename > 4 && postfix[-4] == '.') {
	char b[4];
	memcpy(b, postfix - 3, 3);
	b[3] = 0;
	lang = iso_639_2_lang(b);
      }

      int score = fs_sub_match(video, rstr_get(fde->fde_url));
      TRACE(TRACE_DEBUG, "Video", "SRT %s score=%d", 
	    rstr_get(fde->fde_url), score); 

      if(score == 0 && !subtitle_settings.include_all_subs)
	continue;

      hts_mutex_lock(&ss->ss_mutex);
      mp_add_track(ss->ss_proproot, rstr_get(fde->fde_filename),
		   rstr_get(fde->fde_url),
		   "SRT", NULL, lang, NULL, _p("External file"),
		   score + LOCAL_EXTRA_SCORE);
      hts_mutex_unlock(&ss->ss_mutex);
      
    }

    
    if(!strcasecmp(postfix, ".ass") || !strcasecmp(postfix, ".ssa")) {
      const char *lang = NULL;
      if(postfix - filename > 4 && postfix[-4] == '.') {
	char b[4];
	memcpy(b, postfix - 3, 3);
	b[3] = 0;
	lang = iso_639_2_lang(b);
      }

      int score = fs_sub_match(video, rstr_get(fde->fde_url));
      TRACE(TRACE_DEBUG, "Video", "SSA/ASS %s score=%d",
	    rstr_get(fde->fde_url), score); 

      if(score == 0 && !subtitle_settings.include_all_subs)
	continue;

      hts_mutex_lock(&ss->ss_mutex);
      mp_add_track(ss->ss_proproot, rstr_get(fde->fde_filename),
		   rstr_get(fde->fde_url),
		   "ASS / SSA", NULL, lang, NULL, _p("External file"),
		   score + LOCAL_EXTRA_SCORE);
      hts_mutex_unlock(&ss->ss_mutex);
    }

    if(!strcasecmp(postfix, ".idx")) {
      int score = fs_sub_match(video, rstr_get(fde->fde_url));
      TRACE(TRACE_DEBUG, "Video", "VOBSUB %s score=%d", 
	    rstr_get(fde->fde_url),
	    score + LOCAL_EXTRA_SCORE);

      if(score == 0 && !subtitle_settings.include_all_subs)
	continue;

      hts_mutex_lock(&ss->ss_mutex);
      vobsub_probe(rstr_get(fde->fde_url), rstr_get(fde->fde_filename),
		   score, ss->ss_proproot, NULL);
      hts_mutex_unlock(&ss->ss_mutex);
    }
  }
  fa_dir_free(fd);
}


/**
 *
 */
void
sub_scanner_release(sub_scanner_t *ss)
{
  if(atomic_add(&ss->ss_refcount, -1) > 1)
    return;

  rstr_release(ss->ss_title);
  rstr_release(ss->ss_imdbid);
  free(ss->ss_url);
  hts_mutex_destroy(&ss->ss_mutex);
  free(ss);
}


/**
 *
 */
void
sub_scanner_retain(sub_scanner_t *ss)
{
  atomic_add(&ss->ss_refcount, 1);
}



/**
 *
 */
static void *
sub_scanner_thread(void *aux)
{
  sub_scanner_t *ss = aux;

  if(!(ss->ss_beflags & BACKEND_VIDEO_NO_FS_SCAN)) {
    char parent[URL_MAX];
    char *fname = mystrdupa(ss->ss_url);

    fname = strrchr(fname, '/') ?: fname;
    fname++;
    char *dot = strrchr(fname, '.');
    if(dot)
      *dot = 0;

    fa_parent(parent, sizeof(parent), ss->ss_url);
    fs_sub_scan_dir(ss, parent, fname);
  }

  if(!ss->ss_stop)
    js_sub_query(ss);

  sub_scanner_release(ss);
  return NULL;
}



/**
 *
 */
sub_scanner_t *
sub_scanner_create(const char *url, prop_t *proproot,
		   const video_args_t *va, int duration)
{
  int noscan = va->title == NULL && va->imdb == NULL && !va->hash_valid;

  TRACE(TRACE_DEBUG, "Subscanner",
        "%s subtitle scan for %s (imdbid:%s) "
        "year:%d season:%d episode:%d duration:%d opensubhash:%016llx",
        noscan ? "No" : "Starting",
        va->title ?: "<unknown>",
        va->imdb ?: "<unknown>",
        va->year,
        va->season,
        va->episode,
        duration,
        va->opensubhash);

  if(noscan)
    return NULL;

  sub_scanner_t *ss = calloc(1, sizeof(sub_scanner_t));
  hts_mutex_init(&ss->ss_mutex);
  ss->ss_refcount = 2; // one for thread, one for caller
  ss->ss_url = url ? strdup(url) : NULL;
  ss->ss_beflags = va->flags;
  ss->ss_title = rstr_alloc(va->title);
  ss->ss_imdbid = rstr_alloc(va->imdb);
  ss->ss_proproot = prop_ref_inc(proproot);
  ss->ss_fsize = va->filesize;
  ss->ss_year   = va->year;
  ss->ss_season = va->season;
  ss->ss_episode = va->episode;
  ss->ss_duration = duration;

  ss->ss_hash_valid = va->hash_valid;
  ss->ss_opensub_hash = va->opensubhash;
  memcpy(ss->ss_subdbhash, va->subdbhash, 16);

  hts_thread_create_detached("subscanner", sub_scanner_thread, ss,
			     THREAD_PRIO_LOW);
  return ss;
}

/**
 *
 */
void
sub_scanner_destroy(sub_scanner_t *ss)
{
  if(ss == NULL)
    return;
  ss->ss_stop = 1;
  hts_mutex_lock(&ss->ss_mutex);
  prop_ref_dec(ss->ss_proproot);
  ss->ss_proproot = NULL;
  hts_mutex_unlock(&ss->ss_mutex);
  sub_scanner_release(ss);
}
