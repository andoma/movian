/*
 *  Playqueue
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


#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include "showtime.h"
#include "navigator.h"
#include "playqueue.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_probe.h"
#include "media.h"

#define PLAYQUEUE_URI "playqueue:"

nav_backend_t be_playqueue;

static prop_t *playqueue_root;

static void *player_thread(void *aux);


static event_queue_t player_eventqueue;

/**
 *
 */
static hts_mutex_t playqueue_mutex;


TAILQ_HEAD(playqueue_entry_queue, playqueue_entry);

static struct playqueue_entry_queue playqueue_entries;

typedef struct playqueue_entry {

  int pqe_refcount;

  /**
   * Read only members
   */
  char *pqe_uri;
  char *pqe_parent;
  prop_t *pqe_root;
  prop_t *pqe_meta;
  int pqe_enq;

  /**
   * playqueue_mutex must be held when accessing these
   */
  TAILQ_ENTRY(playqueue_entry) pqe_link;
  int pqe_linked;

} playqueue_entry_t;

playqueue_entry_t *pqe_current;


/**
 *
 */
static hts_mutex_t playqueue_request_mutex;
static hts_cond_t  playqueue_request_cond;

TAILQ_HEAD(playqueue_request_queue, playqueue_request);

static struct playqueue_request_queue playqueue_requests;

typedef struct playqueue_request {
  TAILQ_ENTRY(playqueue_request) pqr_link;
  char *pqr_uri;
  char *pqr_parent;
  prop_t *pqr_meta;
  int pqr_enq;

} playqueue_request_t;

/**
 *
 */
typedef struct playqueue_event {
  event_t h;
  int jump;
  playqueue_entry_t *pqe;
  
} playqueue_event_t;

/**
 *
 */
static void
pqe_unref(playqueue_entry_t *pqe)
{
  if(atomic_add(&pqe->pqe_refcount, -1) != 1)
    return;

  assert(pqe->pqe_linked == 0);

  free(pqe->pqe_uri);
  free(pqe->pqe_parent);
  prop_destroy(pqe->pqe_root);

  free(pqe);
}

/**
 *
 */
static void
pqe_ref(playqueue_entry_t *pqe)
{
  atomic_add(&pqe->pqe_refcount, 1);
}

/**
 *
 */
static event_t *
pqe_event_create(playqueue_entry_t *pqe, int jump)
{
   playqueue_event_t *e;

   e = event_create(EVENT_PLAYQUEUE, sizeof(playqueue_event_t));
   
   e->jump = jump;
   e->pqe = pqe;
   pqe_ref(pqe);

   return &e->h;
}


/**
 *
 */
static void
playqueue_clear(void)
{
  playqueue_entry_t *pqe;

  while((pqe = TAILQ_FIRST(&playqueue_entries)) != NULL) {
    TAILQ_REMOVE(&playqueue_entries, pqe, pqe_link);
    pqe->pqe_linked = 0;
    pqe_unref(pqe);
  }
}


/**
 * Load siblings to the 'justadded' track.
 *
 * We do this by scanning the parent directory of the track.
 *
 * The idea is that even if a user just comes to as with a single URL
 * we are able to grab info about all tracks on the album.
 *
 */
static void
playqueue_load_siblings(const char *uri, playqueue_entry_t *justadded)
{
  fa_dir_t *fd;
  int before = 1;
  playqueue_entry_t *pqe;
  fa_dir_entry_t *fde;
  prop_t *media;
  int r;

  if((fd = fa_scandir(uri)) == NULL)
    return;

  fa_dir_sort(fd);
    
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(!strcmp(fde->fde_url, justadded->pqe_uri)) {
      before = 0;
      continue;
    }

    if(fde->fde_type == FA_DIR)
      continue;
    
    media = prop_create(NULL, "media");
    r = fa_probe(media, fde->fde_url, NULL, 0);

    if(r != FA_AUDIO) {
      prop_destroy(media);
      continue;
    }

    pqe = malloc(sizeof(playqueue_entry_t));
    pqe->pqe_uri    = strdup(fde->fde_url);
    pqe->pqe_parent = strdup(uri);
    pqe->pqe_root   = prop_create(NULL, NULL);
    pqe->pqe_enq    = 0;
    pqe->pqe_refcount = 1;
    pqe->pqe_linked = 1;
    pqe->pqe_meta = media;
    prop_set_parent(media, pqe->pqe_root);
    
    prop_set_string(prop_create(pqe->pqe_root, "url"), pqe->pqe_uri);

    if(before) {
      TAILQ_INSERT_BEFORE(justadded, pqe, pqe_link);
      prop_set_parent_ex(pqe->pqe_root, playqueue_root, 
			 justadded->pqe_root, NULL);
    } else {
      TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);
      prop_set_parent(pqe->pqe_root, playqueue_root);
    }

  }
  fa_dir_free(fd);
}





/**
 * Load playqueue based on the given uri.
 *
 * This function is responsible for freeing (or using) the
 * supplied meta prop tree.
 *
 * If enq is set we don't clear the playqueue, instead we insert the
 * entry after the current track (or after the last enqueued track)
 *
 * That way users may 'stick in' track in the current playqueue
 */
static void
playqueue_load(const char *uri, const char *parent, prop_t *meta, int enq)
{
  playqueue_entry_t *pqe, *prev;
  event_t *e;

  hts_mutex_lock(&playqueue_mutex);

  TAILQ_FOREACH(pqe, &playqueue_entries, pqe_link) {
    if(!strcmp(pqe->pqe_uri, uri) && !strcmp(pqe->pqe_parent, parent)) {
      /* Already in, go to it */
      e = pqe_event_create(pqe, 1);
      event_enqueue(&player_eventqueue, e);
      event_unref(e);

      hts_mutex_unlock(&playqueue_mutex);

      if(meta != NULL)
	prop_destroy(meta);
      return;
    }
  }


  pqe = malloc(sizeof(playqueue_entry_t));
  pqe->pqe_uri    = strdup(uri);
  pqe->pqe_parent = strdup(parent);
  pqe->pqe_root   = prop_create(NULL, NULL);
  pqe->pqe_enq    = enq;
  pqe->pqe_refcount = 1;
  pqe->pqe_linked = 1;
  pqe->pqe_meta = meta;
  prop_set_parent(meta, pqe->pqe_root);

  prop_set_string(prop_create(pqe->pqe_root, "url"), uri);

  if(enq) {

    prev = pqe_current;

    /* Skip past any previously enqueued entries */
    while(prev != NULL && prev->pqe_enq)
      prev = TAILQ_NEXT(prev, pqe_link);

    if(prev == NULL) {
      TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);
    } else {
      TAILQ_INSERT_AFTER(&playqueue_entries, prev, pqe, pqe_link);
    }
    
    abort(); /* Not fully implemented */

  }

  
  /* Clear out the current playqueue */
  playqueue_clear();

  /* Enqueue our new entry */
  TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);
  prop_set_parent(pqe->pqe_root, playqueue_root);

  /* Tick player to play it */
  e = pqe_event_create(pqe, 1);
  event_enqueue(&player_eventqueue, e);
  event_unref(e);

  /* Scan dir (if provided) for additional tracks (siblings) */
  playqueue_load_siblings(parent, pqe);

  hts_mutex_unlock(&playqueue_mutex);
}

/**
 * Dequeue requests
 */
static void *
playqueue_thread(void *aux)
{
  playqueue_request_t *pqr;

  hts_mutex_lock(&playqueue_request_mutex);

  while(1) {
    
    while((pqr = TAILQ_FIRST(&playqueue_requests)) == NULL)
      hts_cond_wait(&playqueue_request_cond, &playqueue_request_mutex);

    TAILQ_REMOVE(&playqueue_requests, pqr, pqr_link);
    
    hts_mutex_unlock(&playqueue_request_mutex);

    playqueue_load(pqr->pqr_uri, pqr->pqr_parent, pqr->pqr_meta,
		   pqr->pqr_enq);
    
    free(pqr->pqr_parent);
    free(pqr->pqr_uri);
    free(pqr);

    hts_mutex_lock(&playqueue_request_mutex);
  }
}



/**
 * We don't want to hog caller, so we dispatch the request to a worker thread.
 */
void
playqueue_play(const char *uri, const char *parent, prop_t *meta,
	       int enq)
{
  playqueue_request_t *pqr = malloc(sizeof(playqueue_request_t));
  char *x;

  pqr->pqr_uri = strdup(uri);

  if(parent == NULL) {
    pqr->pqr_parent = strdup(uri);
    if((x = strrchr(pqr->pqr_parent, '/')) != NULL)
      *x = 0;
    
  } else {
    pqr->pqr_parent = strdup(parent);
  }
  pqr->pqr_meta = meta;
  pqr->pqr_enq = enq;

  hts_mutex_lock(&playqueue_request_mutex);
  TAILQ_INSERT_TAIL(&playqueue_requests, pqr, pqr_link);
  hts_cond_signal(&playqueue_request_cond);
  hts_mutex_unlock(&playqueue_request_mutex);
}



/**
 *
 */
void
playqueue_init(void)
{
  hts_thread_t tid;

  hts_mutex_init(&playqueue_mutex);

  event_initqueue(&player_eventqueue);

  hts_mutex_init(&playqueue_request_mutex);
  hts_cond_init(&playqueue_request_cond);
  TAILQ_INIT(&playqueue_entries);
  TAILQ_INIT(&playqueue_requests);
   
  playqueue_root = prop_create(prop_get_global(), "playqueue");

  hts_thread_create(&tid, playqueue_thread, NULL);
  hts_thread_create(&tid, player_thread, NULL);
}




/**
 *
 */
static int
be_playqueue_open(const char *url0, nav_page_t **npp, 
		  char *errbuf, size_t errlen)
{
  nav_page_t *n;
  prop_t *type, *nodes;

  *npp = n = nav_page_create(&be_playqueue, url0, sizeof(nav_page_t));

  type  = prop_create(n->np_prop_root, "type");
  prop_set_string(type, "playqueue");

  nodes = prop_create(n->np_prop_root, "nodes");

  prop_link(playqueue_root, nodes);
  return 0;
}


/**
 *
 */
static int
be_playqueue_canhandle(const char *url)
{
  return !strncmp(url, PLAYQUEUE_URI, strlen(PLAYQUEUE_URI));
}



/**
 *
 */
nav_backend_t be_playqueue = {
  .nb_canhandle = be_playqueue_canhandle,
  .nb_open = be_playqueue_open,
};


/**
 *
 */
static playqueue_entry_t *
playqueue_advance(playqueue_entry_t *pqe, int prev)
{
  playqueue_entry_t *r;

  hts_mutex_lock(&playqueue_mutex);

  if(pqe->pqe_linked) {

    if(prev) {
      r = TAILQ_PREV(pqe, playqueue_entry_queue, pqe_link);
    } else {
      r = TAILQ_NEXT(pqe, pqe_link);
    }

  } else {
    r = NULL;
  }

  if(r != NULL)
    pqe_ref(r);

  pqe_unref(pqe);

  hts_mutex_unlock(&playqueue_mutex);
  return r;
}



/**
 *
 */
static playqueue_entry_t *
playtrack(playqueue_entry_t *pqe, media_pipe_t *mp, event_queue_t *eq)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  AVPacket pkt;
  formatwrap_t *fw;
  int i;
  media_buf_t *mb;
  media_queue_t *mq;
  int64_t pts4seek = 0;
  int streams;
  int64_t cur_pos_pts = AV_NOPTS_VALUE;
  int curtime;
  codecwrap_t *cw;
  event_ts_t *et;
  int64_t pts;
  char faurl[1000];
  event_t *e;
  playqueue_event_t *pe;

  snprintf(faurl, sizeof(faurl), "showtime:%s", pqe->pqe_uri);

  if(av_open_input_file(&fctx, faurl, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file %s\n", pqe->pqe_uri);
    return 0;
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return 0;
  }

  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(fctx);

  cw = NULL;
  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(ctx->codec_type != CODEC_TYPE_AUDIO)
      continue;

    cw = wrap_codec_create(ctx->codec_id, ctx->codec_type, 0, fw, ctx, 0);
    mp->mp_audio.mq_stream = i;
    break;
  }

  curtime = -1;

  while(1) {

    if(mp->mp_playstatus == MP_PLAY && mp_is_audio_silenced(mp)) {
      mp_set_playstatus(mp, MP_PAUSE, 0);
      media_update_playstatus_prop(mp->mp_prop_playstatus, MP_PAUSE);
    }


    e = event_get(mp_is_paused(mp) ? -1 : 0, eq);

    if(e != NULL) {
      switch(e->e_type) {
	
      default:
	break;

      case EVENT_AUDIO_CLOCK:
	et = (void *)e;
	if(et->pts != AV_NOPTS_VALUE) {

	  pts = et->pts - fctx->start_time;
	  pts /= AV_TIME_BASE;

	  if(curtime != pts) {
	    curtime = pts;

	    prop_set_int(mp->mp_prop_currenttime, pts);

#if 0
	    hts_mutex_lock(&playlistlock);
	  
	    if(pqe->pqe_pl != NULL)
	      glw_prop_set_int(pqe->pqe_pl->pl_prop_time_current,
			       pqe->pqe_time_offset + pts);
	  
	    hts_mutex_unlock(&playlistlock);
#endif
	  }
	}
	break;

      case EVENT_PLAYQUEUE:
	pe = (playqueue_event_t *)e;
	if(!pe->jump) {
	  /* Entry added without request to start playing it at once.
	     Just ignore this */
	  pqe_unref(pe->pqe);
	  break;
	}

	/* Switch to track in request */
	pqe_unref(pqe);
	pqe = pe->pqe;

	mp_flush(mp);
	event_unref(e);
	goto out;


      case EVENT_SEEK_FAST_BACKWARD:
	av_seek_frame(fctx, -1, pts4seek - 60000000, AVSEEK_FLAG_BACKWARD);
	goto seekflush;

      case EVENT_SEEK_BACKWARD:
	av_seek_frame(fctx, -1, pts4seek - 15000000, AVSEEK_FLAG_BACKWARD);
	goto seekflush;

      case EVENT_SEEK_FAST_FORWARD:
	av_seek_frame(fctx, -1, pts4seek + 60000000, 0);
	goto seekflush;

      case EVENT_SEEK_FORWARD:
	av_seek_frame(fctx, -1, pts4seek + 15000000, 0);
	goto seekflush;

      case EVENT_RESTART_TRACK:
	av_seek_frame(fctx, -1, 0, AVSEEK_FLAG_BACKWARD);

      seekflush:
	mp_flush(mp);
	event_flushqueue(eq);
	break;
	
      case EVENT_PLAYPAUSE:
      case EVENT_PLAY:
      case EVENT_PAUSE:
	mp_playpause(mp, e->e_type);

	media_update_playstatus_prop(mp->mp_prop_playstatus,
				     mp->mp_playstatus);
	break;

      case EVENT_PREV:
	pqe = playqueue_advance(pqe, 1);
	mp_flush(mp);
	event_unref(e);
	goto out;
      
      case EVENT_NEXT:
	pqe = playqueue_advance(pqe, 0);
	mp_flush(mp);
	event_unref(e);
	goto out;

      case EVENT_STOP:
	pqe_unref(pqe);
	pqe = NULL;
	mp_flush(mp);
	event_unref(e);
	goto out;
      }
      event_unref(e);
    }
    
    if(mp_is_paused(mp))
      continue;

    mb = media_buf_alloc();

    i = av_read_frame(fctx, &pkt);

    if(i < 0) {
      /* End of stream (or some other type of error), next track */
      pqe = playqueue_advance(pqe, 0);
      break;
    }

    mb->mb_data = NULL;
    mb->mb_size = pkt.size;

    if(pkt.pts != AV_NOPTS_VALUE) {
      mb->mb_pts = av_rescale_q(pkt.pts,
				fctx->streams[pkt.stream_index]->time_base,
				AV_TIME_BASE_Q);
      pts4seek = mb->mb_pts;
    } else {
      mb->mb_pts = AV_NOPTS_VALUE;
    }
    
    mb->mb_duration = av_rescale_q(pkt.duration,
				   fctx->streams[pkt.stream_index]->time_base,
				   AV_TIME_BASE_Q);

    if(pkt.stream_index == mp->mp_audio.mq_stream) {
      ctx = cw->codec_ctx;

      if(mb->mb_pts != AV_NOPTS_VALUE) {
	if(fctx->start_time != AV_NOPTS_VALUE)
	  cur_pos_pts = mb->mb_pts - fctx->start_time;
	else
	  cur_pos_pts = mb->mb_pts;
      }

      if(cur_pos_pts != AV_NOPTS_VALUE)
	mb->mb_mts = cur_pos_pts / 1000;
      else
	mb->mb_mts = -1;

      mb->mb_data_type = MB_AUDIO;
      mb->mb_cw = wrap_codec_ref(cw);

      mb->mb_data = malloc(pkt.size +  FF_INPUT_BUFFER_PADDING_SIZE);
      memset(mb->mb_data + pkt.size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
 
      memcpy(mb->mb_data, pkt.data, pkt.size);
      mq = &mp->mp_audio;
    } else {
      mq = NULL;
    }

    if(mq != NULL) {
      mb_enqueue(mp, mq, mb);
      av_free_packet(&pkt);
      continue;
    }
    
    av_free_packet(&pkt);

    if(mb->mb_data != NULL)
      free(mb->mb_data);
    free(mb);

  }

 out:
  streams = fctx->nb_streams;

  wrap_codec_deref(cw);

  wrap_format_destroy(fw);

  media_update_playstatus_prop(mp->mp_prop_playstatus, MP_STOP);
  return pqe;
}




/**
 *
 */
static int
pqe_event_handler(event_t *e, void *opaque)
{
  
  switch(e->e_type) {

  case EVENT_SEEK_FAST_BACKWARD:
  case EVENT_SEEK_BACKWARD:
  case EVENT_SEEK_FAST_FORWARD:
  case EVENT_SEEK_FORWARD:
  case EVENT_PLAYPAUSE:
  case EVENT_PLAY:
  case EVENT_PAUSE:
  case EVENT_STOP:
  case EVENT_PREV:
  case EVENT_NEXT:
  case EVENT_RESTART_TRACK:
    break;
  default:
    return 0;
  }

  event_enqueue(&player_eventqueue, e);
  return 1;
}


/**
 * Thread for actual playback
 */
static void *
player_thread(void *aux)
{
  media_pipe_t *mp = mp_create("playqueue");
  playqueue_entry_t *pqe = NULL;
  playqueue_event_t *pe;
  event_t *e;
  void *eh;

  while(1) {
    
    while(pqe == NULL) {

      prop_unlink(mp->mp_prop_meta);

      /* Got nothing to play, enter STOP mode */
      mp_set_playstatus(mp, MP_STOP, 0);
      e = event_get(-1, &player_eventqueue);
      
      if(e->e_type == EVENT_PLAYQUEUE) {
	pe = (playqueue_event_t *)e;
	pqe = pe->pqe;
      }
      event_unref(e);
    }

    mp_set_playstatus(mp, MP_PLAY, 0);

    prop_link(pqe->pqe_meta, mp->mp_prop_meta);
    
    eh = event_handler_register("playqueue", pqe_event_handler,
				EVENTPRI_MEDIACONTROLS_PLAYQUEUE, NULL);

    pqe = playtrack(pqe, mp, &player_eventqueue);

    event_handler_unregister(eh);

  }
}
