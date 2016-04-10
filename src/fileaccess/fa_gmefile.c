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

#include "main.h"
#include "backend/backend.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "fa_audio.h"
#include "fa_libav.h"
#include "misc/str.h"
#include "media/media.h"
#include "metadata/playinfo.h"

/**
 *
 */
static int
gmefile_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
                char *errbuf, size_t errlen, int flags)
{
  char *p, *fpath = mystrdupa(url);
  char name[32];
  char turl[URL_MAX];
  int tracks, i;
  fa_dir_entry_t *fde;
  const char *title;
  Music_Emu *emu;
  gme_info_t *info;
  gme_err_t err;

  if((p = strrchr(fpath, '/')) == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return -1;
  }

  *p = 0;

  buf_t *b;
  if((b = fa_load(fpath, FA_LOAD_ERRBUF(errbuf, errlen), NULL)) == NULL)
    return -1;

  err = gme_open_data(b->b_ptr, b->b_size, &emu, gme_info_only);
  buf_release(b);
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

    fde->fde_probestatus = FDE_PROBED_CONTENTS;

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
	     int flags, char *errbuf, size_t errlen)
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
  mp_flush(mp);
  
  if(*mbp != NULL) {
    media_buf_free_unlocked(mp, *mbp);
    *mbp = NULL;
  }
}

#define MB_EOF ((void *)-1)

/**
 *
 */
static event_t *
fa_gme_playfile_internal(media_pipe_t *mp, const void *buf, size_t size,
			 char *errbuf, size_t errlen, int hold, int track,
			 const char *url)
{
  media_queue_t *mq = &mp->mp_audio;
  Music_Emu *emu;
  gme_err_t err;
  int sample_rate = 48000;
  media_buf_t *mb = NULL;
  event_t *e;
  int registered_play = 0;

  err = gme_open_data(buf, size, &emu, sample_rate);
  if(err != NULL) {
    snprintf(errbuf, errlen, "Unable to load file -- %s", err);
    return NULL;
  }

  gme_start_track(emu, track);

  mp->mp_audio.mq_stream = 0;
  mp_configure(mp, MP_CAN_PAUSE | MP_CAN_SEEK,
	       MP_BUFFER_SHALLOW, 0, "tracks");
  mp_become_primary(mp);
  

  while(1) {

    if(mb == NULL) {

      if(gme_track_ended(emu)) {
	mb = MB_EOF;
      } else {
	mb = media_buf_alloc_unlocked(mp, sizeof(int16_t) * CHUNK_SIZE * 2);
	mb->mb_data_type = MB_AUDIO;
	mb->mb_channels = 2;
	mb->mb_rate = sample_rate;
	mb->mb_pts = gme_tell(emu) * 1000;
	mb->mb_drive_clock = 1;

	if(!registered_play && mb->mb_pts > PLAYINFO_AUDIO_PLAY_THRESHOLD) {
	  registered_play = 1;
	  playinfo_register_play(url, 1);
	}

	gme_play(emu, CHUNK_SIZE * mb->mb_channels, (void *)mb->mb_data);
      }
    }

    if(mb == MB_EOF) {
      /* Wait for queues to drain */
      e = mp_wait_for_empty_queues(mp);

      if(e == NULL) {
	e = event_create_type(EVENT_EOF);
	break;
      }
    } else if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
      mp_flush(mp);
      break;

    } else if(event_is_type(e, EVENT_SEEK)) {

      event_ts_t *ets = (event_ts_t *)e;
      gme_seek(emu, ets->ts / 1000);
      seekflush(mp, &mb);
      
    } else if(event_is_action(e, ACTION_SKIP_BACKWARD) ||
	      event_is_action(e, ACTION_SKIP_FORWARD) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp);
      break;
    }
    event_release(e);
  }  

  gme_delete(emu);

  if(mb != NULL && mb != MB_EOF)
    media_buf_free_unlocked(mp, mb);

  return e;
}


/**
 *
 */
event_t *
fa_gme_playfile(media_pipe_t *mp, fa_handle_t *fh,
		char *errbuf, size_t errlen, int hold, const char *url)
{
  buf_t *b;
  event_t *e;

  if((b = fa_load_and_close(fh)) == NULL) {
    snprintf(errbuf, errlen, "Unable to read data from file");
    return NULL;
  }

  e = fa_gme_playfile_internal(mp, b->b_ptr, b->b_size, errbuf, errlen,
			       hold, 0, url);
  buf_release(b);
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

  url0 += strlen("gmeplayer:");

  url = mystrdupa(url0);
  p = strrchr(url, '/');
  if(p == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return NULL;
  }

  *p++= 0;
  track = atoi(p) - 1;
  buf_t *b;
  if((b = fa_load(url, FA_LOAD_ERRBUF(errbuf, errlen), NULL)) == NULL)
    return NULL;

  e = fa_gme_playfile_internal(mp, b->b_ptr, b->b_size,
			       errbuf, errlen, hold, track, url0);
  buf_release(b);
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
