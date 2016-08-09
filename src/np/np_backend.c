#include "main.h"
#include "np.h"
#include "nativeplugin.h"
#include "backend/backend.h"
#include "media/media.h"
#include "metadata/playinfo.h"

static void
np_backend_destroy(struct backend *be)
{
  np_context_release(be->be_opaque);
  free(be->be_prefix);
  free(be);
}

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

static event_t *
np_backend_play_audio(const char *url, struct media_pipe *mp,
                      char *errbuf, size_t errlen, int paused,
                      const char *mimetype, void *opaque)
{
  np_context_t *np = opaque;
  ir_unit_t *iu = np->np_unit;
  int handle;
  media_queue_t *mq = &mp->mp_audio;
  media_buf_t *mb = NULL;
  event_t *e;
  int registered_play = 0;
  int64_t duration = 0;

  ir_function_t *np_audio_open  = vmir_find_function(iu, "np_audio_open");
  ir_function_t *np_audio_play  = vmir_find_function(iu, "np_audio_play");
  ir_function_t *np_audio_close = vmir_find_function(iu, "np_audio_close");
  ir_function_t *np_audio_seek  = vmir_find_function(iu, "np_audio_seek");

  if(np_audio_open == NULL ||
     np_audio_play == NULL ||
     np_audio_close == NULL) {
    snprintf(errbuf, errlen, "Plugin does not expose audio play API");
    return NULL;
  }

  np_lock(np);

  int pfd = np_fd_from_prop(iu, prop_ref_inc(mp->mp_prop_root));

  uint32_t url_vm = vmir_mem_strdup(iu, url);
  vmir_vm_function_call(iu, np_audio_open, &handle,
                        url_vm, pfd);
  vmir_mem_free(iu, url_vm);

  if(handle == 0) {
    snprintf(errbuf, errlen, "Open failed");
    vmir_fd_close(iu, pfd);
    np_unlock(np);
    return NULL;
  }

  np_unlock(np);

  mp->mp_audio.mq_stream = 0;
  int mp_flags = MP_CAN_PAUSE;
  if(np_audio_seek != NULL)
    mp_flags |= MP_CAN_SEEK;
  mp_configure(mp, mp_flags, MP_BUFFER_SHALLOW, 0, "tracks");
  mp_become_primary(mp);

  np_audiobuffer_t *nab;

  uint32_t nab_vm = vmir_mem_alloc(iu, sizeof(np_audiobuffer_t), &nab);

  while(1) {

    if(mb == NULL) {

      uint32_t sampleptr;

      np_lock(np);

      vmir_vm_function_call(iu, np_audio_play, &sampleptr, handle, nab_vm);

      np_unlock(np);

      if(sampleptr == 0) {
	mb = MB_EOF;
      } else {

        int bufsize = nab->channels * nab->samples * sizeof(int16_t);
	mb = media_buf_alloc_unlocked(mp, bufsize);
	mb->mb_data_type = MB_AUDIO;
	mb->mb_channels = nab->channels;
	mb->mb_rate = nab->samplerate;
	mb->mb_user_time = mb->mb_pts = nab->timestamp;
	mb->mb_drive_clock = 1;

        if(nab->duration > 0 && nab->duration != duration)
          mp_set_duration(mp, nab->duration);

	if(!registered_play && mb->mb_pts > PLAYINFO_AUDIO_PLAY_THRESHOLD) {
	  registered_play = 1;
	  playinfo_register_play(url, 1);
	}
        memcpy(mb->mb_data, np->np_mem + sampleptr, bufsize);
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

    } else if(np_audio_seek != NULL && event_is_type(e, EVENT_SEEK)) {

      event_ts_t *ets = (event_ts_t *)e;

      seekflush(mp, &mb);
      vmir_vm_function_call(iu, np_audio_seek, NULL, handle, ets->ts);

    } else if(event_is_action(e, ACTION_SKIP_BACKWARD) ||
	      event_is_action(e, ACTION_SKIP_FORWARD) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp);
      break;
    }
    event_release(e);
  }

  vmir_mem_free(iu, nab_vm);

  np_lock(np);
  vmir_vm_function_call(iu, np_audio_close, NULL, handle);
  np_unlock(np);

  if(mb != NULL && mb != MB_EOF)
    media_buf_free_unlocked(mp, mb);

  vmir_fd_close(iu, pfd);
  return e;
}


static int
np_backend_open_page(prop_t *p, const char *url, int sync, void *opaque)
{
  np_context_t *np = opaque;
  ir_unit_t *iu = np->np_unit;
  ir_function_t *np_page_open  = vmir_find_function(iu, "np_page_open");
  if(np_page_open == NULL) {
    nav_open_error(p, "Plugin does not export page API");
    return 0;
  }

  np_lock(np);

  int pfd = np_fd_from_prop(iu, prop_ref_inc(p));

  uint32_t url_vm = vmir_mem_strdup(iu, url);
  vmir_vm_function_call(iu, np_page_open, NULL,
                        pfd, url_vm, sync);

  vmir_mem_free(iu, url_vm);
  vmir_fd_close(iu, pfd);
  np_unlock(np);
  return 0;
}



static int
np_register_backend(void *ret, const void *regs, struct ir_unit *iu)
{
  np_context_t *np = vmir_get_opaque(iu);
  if(np->np_backend != NULL)
    return 0;

  const char *prefix = vmir_vm_ptr(&regs, iu);
  backend_t *be = calloc(1, sizeof(backend_t));
  atomic_set(&be->be_refcount, 1);
  be->be_prefix = strdup(prefix);
  be->be_opaque = np_context_retain(np);
  be->be_destroy = np_backend_destroy;
  be->be_flags = BACKEND_DYNAMIC;
  be->be_play_audio = np_backend_play_audio;
  be->be_open2 = np_backend_open_page;
  backend_register_dynamic(be);
  np->np_backend = be;
  return 0;
}


static void
np_backend_unload(np_context_t *np)
{
  if(np->np_backend == NULL)
    return;
  backend_unregister_dynamic(np->np_backend);
  backend_release(np->np_backend);
}


static const vmir_function_tab_t np_backend_funcs[] = {
  {"np_register_uri_prefix",  &np_register_backend},
};

NP_MODULE("backend", np_backend_funcs, NULL, np_backend_unload);
