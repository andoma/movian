
#include "backend/backend.h"
#include "media.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"



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
  int sample = 0, lost_focus = 0;
  media_buf_t *mb = NULL;
  event_t *e;
  int subsong;
  int registered_play = 0;

  void *player;
  void *data;
  struct fa_stat fs;

  
  url0 += strlen("sidplayer:");

  url = mystrdupa(url0);
  p = strrchr(url, '/');
  if(p == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return NULL;
  }

  *p++= 0;
  subsong = atoi(p);

  if((data = fa_quickload(url, &fs, NULL, errbuf, errlen)) == NULL)
    return NULL;

  player = sidcxx_load(data, fs.fs_size, subsong, errbuf, errlen);
  free(data);
  if(player == NULL)
    return NULL;

  mp_set_playstatus_by_hold(mp, hold, NULL);
  mp->mp_audio.mq_stream = 0;
  mp_configure(mp, MP_PLAY_CAPS_PAUSE, MP_BUFFER_NONE);
  mp_become_primary(mp);

  while(1) {

    if(mb == NULL) {

      mb = media_buf_alloc_unlocked(mp, sizeof(int16_t) * CHUNK_SIZE * 2);
      mb->mb_data_type = MB_AUDIO;
      mb->mb_channels = 2;
      mb->mb_rate = 44100;

      mb->mb_time = sample * 1000000LL / mb->mb_rate;

      if(!registered_play && mb->mb_time > METADB_AUDIO_PLAY_THRESHOLD) {
	registered_play = 1;
	metadb_register_play(url0, 1, CONTENT_AUDIO);
      }

      sample += CHUNK_SIZE;

      int16_t *samples = mb->mb_data;

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
