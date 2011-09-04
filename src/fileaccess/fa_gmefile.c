/*
 *  Browsing of GME files as virtual directories
 *  Copyright (C) 2010 Andreas Öman
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

#include <gme/gme.h>

#include "showtime.h"
#include "backend/backend.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "fa_audio.h"
#include "fa_libav.h"
#include "misc/string.h"
#include "media.h"

/**
 *
 */
static int
gmefile_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  fa_stat_t fs;
  char *p, *fpath = mystrdupa(url);
  char name[32];
  char turl[URL_MAX];
  int tracks, i;
  fa_dir_entry_t *fde;
  const char *title;
  char *buf;
  Music_Emu *emu;
  gme_info_t *info;
  gme_err_t err;

  if((p = strrchr(fpath, '/')) == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return -1;
  }

  *p = 0;

  if((buf = fa_quickload(fpath, &fs, NULL, errbuf, errlen)) == NULL)
    return -1;

  err = gme_open_data(buf, fs.fs_size, &emu, gme_info_only);
  free(buf);
  if(err != NULL)
    return 0;

  tracks = gme_track_count(emu);
  
  for(i = 0; i < tracks; i++) {

    snprintf(turl, sizeof(turl), "gmeplayer:%s/%d", fpath, i + 1);

    err = gme_track_info(emu, &info, i);

    if(err == NULL && info->song[0]) {
      title = info->song;
    } else {
      snprintf(name, sizeof(name), "Track %02d", i + 1);
      title = name;
    }

      
    fde = fa_dir_add(fd, turl, title, CONTENT_AUDIO);

    fde->fde_probestatus = FDE_PROBE_DEEP;

    fde->fde_metadata = prop_create_root("metadata");
    prop_set_string(prop_create(fde->fde_metadata, "title"), title);

    if(err == NULL) {
      if(info->game[0])
	prop_set_string(prop_create(fde->fde_metadata, "album"), info->game);
      if(info->author[0])
	prop_set_string(prop_create(fde->fde_metadata, "artist"), info->author);

      prop_set_float(prop_create(fde->fde_metadata, "duration"), 
		     info->play_length / 1000.0);

      gme_free_info(info);
    }
  }

  gme_delete(emu);
  return 0;
}

/**
 * Standard unix stat
 */
static int
gmefile_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
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


static fa_protocol_t fa_protocol_gmefile = {
  .fap_name = "gmefile",
  .fap_scan =  gmefile_scandir,
  .fap_stat  = gmefile_stat,
};
FAP_REGISTER(gmefile);

#define CHUNK_SIZE 1024

/**
 *
 */
static void
seekflush(media_pipe_t *mp, media_buf_t **mbp)
{
  mp_flush(mp, 0);
  
  if(*mbp != NULL) {
    media_buf_free_unlocked(mp, *mbp);
    *mbp = NULL;
  }
}


/**
 *
 */
static void
deltaseek(media_pipe_t *mp, media_buf_t **mbp, Music_Emu *emu, int delta)
{
  int pos = gme_tell(emu) + delta;
  if(pos < 0)
    pos = 0;

  gme_seek(emu, pos);
  seekflush(mp, mbp);
}


/**
 *
 */
static event_t *
fa_gme_playfile_internal(media_pipe_t *mp, void *buf, size_t size,
			 char *errbuf, size_t errlen, int hold, int track)
{
  media_queue_t *mq = &mp->mp_audio;
  Music_Emu *emu;
  gme_err_t err;
  int lost_focus = 0;
  int sample_rate = 48000;
  media_buf_t *mb = NULL;
  event_t *e;

  err = gme_open_data(buf, size, &emu, sample_rate);
  if(err != NULL) {
    snprintf(errbuf, errlen, "Unable to load file -- %s", err);
    return NULL;
  }

  gme_start_track(emu, track);

  mp_set_playstatus_by_hold(mp, hold, NULL);
  mp->mp_audio.mq_stream = 0;
  mp_configure(mp, MP_PLAY_CAPS_PAUSE | MP_PLAY_CAPS_SEEK,
	       MP_BUFFER_SHALLOW);
  mp_become_primary(mp);
  

  while(1) {

    if(gme_track_ended(emu)) {
      e = event_create_type(EVENT_EOF);
      break;
    }

    if(mb == NULL) {
      mb = media_buf_alloc_unlocked(mp, sizeof(int16_t) * CHUNK_SIZE * mb->mb_channels);
      mb->mb_data_type = MB_AUDIO;
      mb->mb_channels = 2;
      mb->mb_rate = sample_rate;
      mb->mb_time = gme_tell(emu) * 1000;
      gme_play(emu, CHUNK_SIZE * mb->mb_channels, mb->mb_data);
    }

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }
    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
      mp_flush(mp, 0);
      break;


    } else if(event_is_type(e, EVENT_SEEK)) {

      event_ts_t *ets = (event_ts_t *)e;
      gme_seek(emu, ets->pts / 1000);
      seekflush(mp, &mb);
      
    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      deltaseek(mp, &mb, emu, -60000);

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      deltaseek(mp, &mb, emu, -15000);

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      deltaseek(mp, &mb, emu, 60000);

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      deltaseek(mp, &mb, emu, 15000);

    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      lost_focus = 0;
      mp_set_playstatus_by_hold(mp, hold, NULL);

    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	mp_send_cmd_head(mp, mq, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold, NULL);
      }

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      lost_focus = 0;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_action(e, ACTION_PREV_TRACK) ||
	      event_is_action(e, ACTION_NEXT_TRACK) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp, 0);
      break;
    }
    event_release(e);
  }  

  gme_delete(emu);

  if(mb != NULL)
    media_buf_free_unlocked(mp, mb);

  if(hold) { 
    // If we were paused, release playback again.
    mp_send_cmd(mp, mq, MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, 0, NULL);
  }
  return e;
}


/**
 *
 */
event_t *
fa_gme_playfile(media_pipe_t *mp, AVIOContext *avio,
		char *errbuf, size_t errlen, int hold)
{
  uint8_t *mem;
  size_t size;
  event_t *e;

  if((mem = fa_libav_load_and_close(avio, &size)) == NULL) {
    snprintf(errbuf, errlen, "Unable to read data from file");
    return NULL;
  }

  e = fa_gme_playfile_internal(mp, mem, size, errbuf, errlen, hold, 0);
  free(mem);
  return e;
}

/**
 *
 */
static event_t *
be_gmeplayer_play(const char *url0, media_pipe_t *mp, 
		  char *errbuf, size_t errlen, int hold,
		  const char *mimetype)
{
  event_t *e;
  char *url, *p;
  int track;
  void *mem;
  struct fa_stat fs;

  url0 += strlen("gmeplayer:");

  url = mystrdupa(url0);
  p = strrchr(url, '/');
  if(p == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return NULL;
  }

  *p++= 0;
  track = atoi(p) - 1;

  if((mem = fa_quickload(url, &fs, NULL, errbuf, errlen)) == NULL)
    return NULL;

  e = fa_gme_playfile_internal(mp, mem, fs.fs_size,
			       errbuf, errlen, hold, track);
  free(mem);
  return e;
}


/**
 *
 */
static int
be_gmeplayer_canhandle(const char *url)
{
  return !strncmp(url, "gmeplayer:", strlen("gmeplayer:"));
}


/**
 *
 */
static backend_t be_gmeplayer = {
  .be_canhandle = be_gmeplayer_canhandle,
  .be_play_audio = be_gmeplayer_play,
};

BE_REGISTER(gmeplayer);
