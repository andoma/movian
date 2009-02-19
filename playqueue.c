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

#define PLAYQUEUE_URL "playqueue:"

static prop_t *playqueue_root;

static void *player_thread(void *aux);

static media_pipe_t *playqueue_mp;

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
  char *pqe_url;
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
  char *pqr_url;
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

  free(pqe->pqe_url);
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
playqueue_load_siblings(const char *url, playqueue_entry_t *justadded)
{
  fa_dir_t *fd;
  int before = 1;
  playqueue_entry_t *pqe;
  fa_dir_entry_t *fde;
  prop_t *media;
  int r;

  if((fd = fa_scandir(url)) == NULL)
    return;

  fa_dir_sort(fd);
    
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(!strcmp(fde->fde_url, justadded->pqe_url)) {
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
    pqe->pqe_url    = strdup(fde->fde_url);
    pqe->pqe_parent = strdup(url);
    pqe->pqe_root   = prop_create(NULL, NULL);
    pqe->pqe_enq    = 0;
    pqe->pqe_refcount = 1;
    pqe->pqe_linked = 1;
    pqe->pqe_meta = media;
    prop_set_parent(media, pqe->pqe_root);
    
    prop_set_string(prop_create(pqe->pqe_root, "url"), pqe->pqe_url);

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
 * Load playqueue based on the given url.
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
playqueue_load(const char *url, const char *parent, prop_t *meta, int enq)
{
  playqueue_entry_t *pqe, *prev;
  event_t *e;

  hts_mutex_lock(&playqueue_mutex);

  TAILQ_FOREACH(pqe, &playqueue_entries, pqe_link) {
    if(!strcmp(pqe->pqe_url, url) && !strcmp(pqe->pqe_parent, parent)) {
      /* Already in, go to it */
      e = pqe_event_create(pqe, 1);
      mp_enqueue_event(playqueue_mp, e);
      event_unref(e);

      hts_mutex_unlock(&playqueue_mutex);

      if(meta != NULL)
	prop_destroy(meta);
      return;
    }
  }


  pqe = malloc(sizeof(playqueue_entry_t));
  pqe->pqe_url    = strdup(url);
  pqe->pqe_parent = strdup(parent);
  pqe->pqe_root   = prop_create(NULL, NULL);
  pqe->pqe_enq    = enq;
  pqe->pqe_refcount = 1;
  pqe->pqe_linked = 1;
  pqe->pqe_meta = meta;
  prop_set_parent(meta, pqe->pqe_root);

  prop_set_string(prop_create(pqe->pqe_root, "url"), url);

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
  mp_enqueue_event(playqueue_mp, e);
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

    playqueue_load(pqr->pqr_url, pqr->pqr_parent, pqr->pqr_meta,
		   pqr->pqr_enq);
    
    free(pqr->pqr_parent);
    free(pqr->pqr_url);
    free(pqr);

    hts_mutex_lock(&playqueue_request_mutex);
  }
}



/**
 * We don't want to hog caller, so we dispatch the request to a worker thread.
 */
void
playqueue_play(const char *url, const char *parent, prop_t *meta,
	       int enq)
{
  playqueue_request_t *pqr = malloc(sizeof(playqueue_request_t));
  char *x;

  pqr->pqr_url = strdup(url);

  if(parent == NULL) {
    pqr->pqr_parent = strdup(url);
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
static void
playqueue_init(void)
{
  hts_mutex_init(&playqueue_mutex);

  playqueue_mp = mp_create("playqueue");

  hts_mutex_init(&playqueue_request_mutex);
  hts_cond_init(&playqueue_request_cond);
  TAILQ_INIT(&playqueue_entries);
  TAILQ_INIT(&playqueue_requests);
   
  playqueue_root = prop_create(prop_get_global(), "playqueue");

  hts_thread_create_detached(playqueue_thread, NULL);
  hts_thread_create_detached(player_thread, NULL);
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

  *npp = n = nav_page_create(url0, sizeof(nav_page_t), NULL,
			     NAV_PAGE_DONT_CLOSE_ON_BACK);

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
  return !strncmp(url, PLAYQUEUE_URL, strlen(PLAYQUEUE_URL));
}



/**
 *
 */
nav_backend_t be_playqueue = {
  .nb_init = playqueue_init,
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
static int64_t
rescale(AVFormatContext *fctx, int64_t ts, int si)
{
  if(ts == AV_NOPTS_VALUE)
    return AV_NOPTS_VALUE;

  return av_rescale_q(ts, fctx->streams[si]->time_base, AV_TIME_BASE_Q);
}


/**
 *
 */
static playqueue_entry_t *
playtrack(playqueue_entry_t *pqe, media_pipe_t *mp)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  AVPacket pkt;
  formatwrap_t *fw;
  int i, r, si;
  media_buf_t *mb = NULL;
  media_queue_t *mq;
  event_seek_t *es;
  int64_t ts, pts4seek = 0;
  codecwrap_t *cw;
  char faurl[1000];
  event_t *e;
  playqueue_event_t *pe;
  int run = 1;
  int hold = 0;

  snprintf(faurl, sizeof(faurl), "showtime:%s", pqe->pqe_url);

  if(av_open_input_file(&fctx, faurl, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file %s\n", pqe->pqe_url);
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

  mp_prepare(mp, MP_GRAB_AUDIO);
  mq = &mp->mp_audio;

  while(run) {

    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      if((r = av_read_frame(fctx, &pkt)) < 0) {
	pqe = playqueue_advance(pqe, 0);
	printf("Read error, pqe = %p\n", pqe);
	break;
      }

      si = pkt.stream_index;

      if(si == mp->mp_audio.mq_stream) {
	/* Current audio stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_AUDIO;

      } else {
	/* Check event queue ? */
	av_free_packet(&pkt);
	continue;
      }


      mb->mb_pts      = rescale(fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(fctx, pkt.dts,      si);
      mb->mb_duration = rescale(fctx, pkt.duration, si);

      mb->mb_cw = wrap_codec_ref(cw);

      /* Move the data pointers from ffmpeg's packet */

      mb->mb_stream = pkt.stream_index;

      av_dup_packet(&pkt);

      mb->mb_data = pkt.data;
      pkt.data = NULL;

      mb->mb_size = pkt.size;
      pkt.size = 0;

      if(mb->mb_pts != AV_NOPTS_VALUE) {
	mb->mb_time = mb->mb_pts - fctx->start_time;
	pts4seek = mb->mb_pts;
      } else
	mb->mb_time = AV_NOPTS_VALUE;


      av_free_packet(&pkt);
    }

    /*
     * Try to send the buffer.  If mb_enqueue() returns something we
     * catched an event instead of enqueueing the buffer. In this case
     * 'mb' will be left untouched.
     */

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }      

    switch(e->e_type) {
	
    default:

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
      run = 0;
      break;
      
    case EVENT_SEEK:
      es = (event_seek_t *)e;
      
      ts = es->ts + fctx->start_time;

      if(ts < fctx->start_time)
	ts = fctx->start_time;

      av_seek_frame(fctx, -1, ts, AVSEEK_FLAG_BACKWARD);
      goto seekflush;

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

      if(mb != NULL) {
	media_buf_free(mb);
	mb = NULL;
      }
      break;
	
    case EVENT_PLAYPAUSE:
    case EVENT_PLAY:
    case EVENT_PAUSE:

      hold =  mp_update_hold_by_event(hold, e->e_type);
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      break;

    case EVENT_PREV:
      pqe = playqueue_advance(pqe, 1);
      mp_flush(mp);
      run = 0;
      break;
      
    case EVENT_NEXT:
      pqe = playqueue_advance(pqe, 0);
      mp_flush(mp);
      run = 0;
      break;

    case EVENT_STOP:
      pqe_unref(pqe);
      pqe = NULL;
      mp_flush(mp);
      run = 0;
      break;
    }
    event_unref(e);
  }

  if(mb != NULL)
    media_buf_free(mb);

  wrap_codec_deref(cw);
  wrap_format_deref(fw);

  //  media_update_playstatus_prop(mp->mp_prop_playstatus, MP_STOP);
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

  mp_enqueue_event(playqueue_mp, e);
  return 1;
}


/**
 * Thread for actual playback
 */
static void *
player_thread(void *aux)
{
  media_pipe_t *mp = playqueue_mp;
  playqueue_entry_t *pqe = NULL;
  playqueue_event_t *pe;
  event_t *e;
  void *eh;

  while(1) {
    
    while(pqe == NULL) {

      prop_unlink(mp->mp_prop_meta);

      /* Got nothing to play, enter STOP mode */
      //      mp_set_playstatus(mp, MP_STOP, 0);

      mp_hibernate(mp);
      e = mp_dequeue_event(playqueue_mp);

      if(e->e_type == EVENT_PLAYQUEUE) {
	pe = (playqueue_event_t *)e;
	pqe = pe->pqe;
      }
      event_unref(e);
    }

    prop_link(pqe->pqe_meta, mp->mp_prop_meta);
    eh = event_handler_register("playqueue", pqe_event_handler,
				EVENTPRI_MEDIACONTROLS_PLAYQUEUE, NULL);
    pqe = playtrack(pqe, mp);
    event_handler_unregister(eh);
  }
}
