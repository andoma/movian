/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "backend/backend.h"
#include "media.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "metadata/playinfo.h"



extern void *sidcxx_load(const void *data, int length, int subsong, char *errbuf, size_t errlen);
extern int sidcxx_play(void *W, void *out, int len);
extern void sidcxx_stop(void *W);

/**
 *
 */
static int
be_sid2player_canhandle(const char *url)
{
  if(!strncmp(url, "sidplayer:", strlen("sidplayer:")))
    return 10; // We're really good at those
  return 0;
}
  


#define CHUNK_SIZE 1024

/**
 * Play given track.
 *
 * We only expect this to be called from the playqueue system.
 */
static event_t *
be_sid2player_play(const char *url0, media_pipe_t *mp, 
		   char *errbuf, size_t errlen, int hold,
		   const char *mimetype)
{
  media_queue_t *mq = &mp->mp_audio;
  char *url, *p;
  int sample = 0;
  media_buf_t *mb = NULL;
  event_t *e;
  int subsong;
  int registered_play = 0;

  void *player;
  
  url0 += strlen("sidplayer:");

  url = mystrdupa(url0);
  p = strrchr(url, '/');
  if(p == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return NULL;
  }

  *p++= 0;
  subsong = atoi(p);

  buf_t *b;

  if((b = fa_load(url, FA_LOAD_ERRBUF(errbuf, errlen), NULL)) == NULL)
    return NULL;

  player = sidcxx_load(b->b_ptr, b->b_size, subsong, errbuf, errlen);
  buf_release(b);
  if(player == NULL)
    return NULL;

  mp_set_playstatus_by_hold(mp, hold, NULL);
  mp->mp_audio.mq_stream = 0;
  mp_configure(mp, MP_PLAY_CAPS_PAUSE, MP_BUFFER_NONE, 0, "tracks");
  mp_become_primary(mp);

  while(1) {

    if(mb == NULL) {

      mb = media_buf_alloc_unlocked(mp, sizeof(int16_t) * CHUNK_SIZE * 2);
      mb->mb_data_type = MB_AUDIO;
      mb->mb_channels = 2;
      mb->mb_rate = 44100;

      mb->mb_pts = sample * 1000000LL / mb->mb_rate;
      mb->mb_drive_clock = 1;

      if(!registered_play && mb->mb_pts > PLAYINFO_AUDIO_PLAY_THRESHOLD) {
	registered_play = 1;
	playinfo_register_play(url0, 1);
      }

      sample += CHUNK_SIZE;

      int16_t *samples = (void *)mb->mb_data;

      sidcxx_play(player, samples,
		  CHUNK_SIZE * sizeof(int16_t) * mb->mb_channels);

      // Crossmix 25% 

      int i, l, r, L, R;
      for(i = 0; i < CHUNK_SIZE; i++) {
	l = samples[i * 2 + 0];
	r = samples[i * 2 + 1];

	L = 3 * l + r * 2;
	R = 3 * r + l * 2;

	L = L / 4;
	R = R / 4;

	if(L > 32767)
	  L = 32767;
	if(L < -32768)
	  L = -32768;

	if(R > 32767)
	  R = 32767;
	if(R < -32768)
	  R = -32768;

	samples[i * 2 + 0] = L;
	samples[i * 2 + 1] = R;
      }
    }

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
      mp_flush(mp, 0);
      break;

    } else if(event_is_action(e, ACTION_SKIP_BACKWARD) ||
	      event_is_action(e, ACTION_SKIP_FORWARD) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp, 0);
      break;
    }
    event_release(e);
  }  

  sidcxx_stop(player);

  return e;
}



/**
 *
 */
static backend_t be_sid2player = {
  .be_canhandle = be_sid2player_canhandle,
  .be_play_audio = be_sid2player_play,
};

BE_REGISTER(sid2player);
