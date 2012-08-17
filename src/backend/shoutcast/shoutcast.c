#include "backend/backend.h"
#include "media.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define SC_MAX_PLAYLIST_STREAMS   20

#define SC_STREAM_BUFFER_SIZE     256*1024
#define SC_CHUNK_SIZE             8192*2
#define SC_AVIO_BUFFER_SIZE       8192*2

typedef struct sc_shoutcast { 
  int sc_initialized;
  int sc_hold;

  event_t *sc_event;
  media_pipe_t *sc_mp;
  media_buf_t *sc_mb;
  media_queue_t *sc_mq;

  uint8_t *sc_samples;
  int sc_samples_left;

  AVFormatContext *sc_fctx;
  AVIOContext *sc_ioctx;
  AVCodec *sc_decoder;
  AVPacket sc_pkt;
  int sc_audio_stream_idx;

  int sc_stop_playback;

  hts_thread_t sc_playback_thread_id;
  hts_mutex_t sc_stream_buffer_mutex;
  hts_cond_t sc_stream_buffer_drained_cond;

  char *sc_stream_url;
  char *sc_playlist_streams[20];

  struct {
    void *data;
    int size;
    int fillsize;
  } sc_stream_buffer;

  struct http_header_list sc_headers;
} sc_shoutcast_t;

/**
 *
 */
static int
be_shoutcast_canhandle(const char *url)
{
  if(!strncmp(url, "shoutcast:", strlen("shoutcast:")))
    return 10; // We're really good at those
  return 0;
}

static int 
sc_parse_playlist_pls(sc_shoutcast_t *sc, char *content)
{
  char *ps, *pe;
  int idx = 0;
  char buf[128];
  char *playlist = strdup(content);

  while(idx <= SC_MAX_PLAYLIST_STREAMS) {
    idx += 1;

    sprintf(buf, "File%d=", idx);
    ps = strstr(content, buf);
    if (!ps)
      break;

    ps += strlen(buf);
    pe = strchr(ps,'\n');
    if (!pe)
      break;

    if ((pe-1)[0] == '\r')
      pe--;

    pe[0]='\0';
    sc->sc_playlist_streams[idx] = strdup(ps);
  }

  free(playlist);
  return idx;
}

static int 
sc_parse_playlist_m3u(sc_shoutcast_t *sc, char *content)
{
  char *ps, *pe;
  int idx = 0;
  char *playlist = strdup(content);
  ps = playlist;
  while(idx <= SC_MAX_PLAYLIST_STREAMS) {
    idx += 1;
    pe = strchr(ps,'\n');
    if (!pe)
      break;

    if ((pe-1)[0] == '\r')
      pe--;

    pe[0]='\0';
    sc->sc_playlist_streams[idx] = strdup(ps);
    ps = pe + 1;
    if (ps[0] == '\n')
      ps++;
  }

  free(playlist);
  return idx;
}

/**
 *
 */
static int sc_avio_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
  sc_shoutcast_t *sc = (sc_shoutcast_t*)opaque;
  int s = buf_size;

  hts_mutex_unlock(&sc->sc_stream_buffer_mutex);

  if(s > sc->sc_stream_buffer.fillsize)
    s = sc->sc_stream_buffer.fillsize;
  
  void *data = sc->sc_stream_buffer.data; 
  memcpy(buf, sc->sc_stream_buffer.data, s);

  // drain data from buffer
  sc->sc_stream_buffer.fillsize -= s;
  sc->sc_stream_buffer.data += s;
  memmove(data, sc->sc_stream_buffer.data, sc->sc_stream_buffer.fillsize);
  sc->sc_stream_buffer.data = data;

  // signal that data has been drained from buffer
  hts_cond_signal(&sc->sc_stream_buffer_drained_cond);

  hts_mutex_unlock(&sc->sc_stream_buffer_mutex);

  // TRACE(TRACE_DEBUG, "shoutcast", "sc_read_packet(%x, %d)", buf, s);

  return s;
}

/**
 *
 */
static int sc_decode_stream(sc_shoutcast_t *sc, int16_t *samples, int size)
{
  int res, tlen = 0;
  int lsize = size;
  int processed_size = 0;
  int discard_packet = 0;

  // TRACE(TRACE_DEBUG, "shoutcast", "Decode stream and fill audio buffer with %d bytes samples.", size);

  // if we still have decoded samples left, consume them
  if(sc->sc_samples_left) {
    int s = sc->sc_samples_left;

    if(sc->sc_samples_left > size)
      s = size;

    memcpy(samples, sc->sc_samples, s);

    sc->sc_samples_left -= s;
    processed_size += s;

    if (processed_size == size)
      return processed_size;

    samples += (processed_size/sizeof(int16_t));

  }

  while(1) {

    // Read frame from stream
    if (discard_packet || sc->sc_pkt.size == 0)
    {
      discard_packet = 0;
      res = av_read_frame(sc->sc_fctx, &sc->sc_pkt);      
      if (res < 0) {
	TRACE(TRACE_DEBUG, "shoutcast", "error while reading frame from stream.");
	return -1;
      }
    }

    if (sc->sc_pkt.stream_index == sc->sc_audio_stream_idx) {

      // TRACE(TRACE_DEBUG,"shoutcast", "Audio packet size %d bytes", sc->sc_pkt.size);

      while(sc->sc_pkt.size > 0)
      {
	void *inbuf = sc->sc_pkt.data;
	lsize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
	// Decode audio frames
	res = avcodec_decode_audio3(sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec,
				    (int16_t *)sc->sc_samples, &lsize, &sc->sc_pkt);
	if (res < 0) {
	  TRACE(TRACE_DEBUG, "shoutcast", "error decoding frame, discarding packet.");
	  discard_packet = 1;
	  break;
	}

	// consume data from packet
	if (res > 0)
	{
	  sc->sc_pkt.size -= res;
	  if (sc->sc_pkt.size > 0) {
	    sc->sc_pkt.data += res;
	    memmove(inbuf, sc->sc_pkt.data, sc->sc_pkt.size);
	    sc->sc_pkt.data = inbuf;
	  }
	}

	// copy over decoded audio samples to main buffer
	if (lsize) {
	  sc->sc_samples_left = lsize;
	  int s = (size - processed_size);
	  if(s > lsize)
	    s = lsize;
 
	  memcpy(samples, sc->sc_samples, s);
	  sc->sc_samples_left -= s;
	  processed_size += s;

	  samples += ((s / sizeof(int16_t)));

	  // consume samples
	  if (sc->sc_samples_left)
	  {
	    void *data = sc->sc_samples;
	    sc->sc_samples += s;
	    memmove(data, sc->sc_samples, sc->sc_samples_left);
	    sc->sc_samples = data;
	  }
	}

	//TRACE(TRACE_DEBUG,"shoutcast", "%d/%d samples data filled ", processed_size, size);

	if(processed_size == size) {
	  //TRACE(TRACE_DEBUG,"shoutcast", "Samples buffer filled");
	  return processed_size;
	}
      }
      
    } else {
      // discard non audio packets
      discard_packet = 1;
    }
  }
  return tlen;
}

/**
 *
 */
static void *sc_playback_thread(void *aux)
{
  int res;
  int sample = 0;
  int registered_play = 0;
  int16_t dummy[512];
  sc_shoutcast_t *sc = (sc_shoutcast_t *) aux;


  TRACE(TRACE_DEBUG,"shoutcast_playback", "thread started");

  // Decode a few dummy samples to get channels and sample_rate
  res = sc_decode_stream(sc, dummy, 512);  
  if (res < 0)
    goto exit_playback_thread;

  TRACE(TRACE_DEBUG, "shoutcast_playback", "Detected %d Khz %dch audio format.", 
	sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec->sample_rate,
	sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec->channels);

  // start playback loop if intiialized
  while(sc->sc_initialized && !sc->sc_stop_playback) {

    if (sc->sc_mb == NULL) {
      sc->sc_mb = media_buf_alloc_unlocked(sc->sc_mp, sizeof(int16_t)*SC_CHUNK_SIZE*2);
      sc->sc_mb->mb_data_type = MB_AUDIO;
      sc->sc_mb->mb_channels = sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec->channels;
      sc->sc_mb->mb_rate = sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec->sample_rate;

      sc->sc_mb->mb_time = sample * 1000000LL / sc->sc_mb->mb_rate;
    
      if(registered_play && sc->sc_mb->mb_time > METADB_AUDIO_PLAY_THRESHOLD) {
	registered_play = 1;
	metadb_register_play(sc->sc_stream_url, 1, CONTENT_AUDIO);
      }
    
      sample += SC_CHUNK_SIZE;

      int bsize = sizeof(int16_t)*SC_CHUNK_SIZE*2;
      res = sc_decode_stream(sc, sc->sc_mb->mb_data, bsize);
      if (res < 0) {
	mp_flush(sc->sc_mp, 0);
	break;
      }
      sc->sc_mb->mb_size = res;
    }
  
    if((sc->sc_event = mb_enqueue_with_events(sc->sc_mp, sc->sc_mq, sc->sc_mb)) == NULL) {
      sc->sc_mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(sc->sc_event, EVENT_PLAYQUEUE_JUMP)) {
      mp_flush(sc->sc_mp, 0);
      break;
    } else if(event_is_action(sc->sc_event, ACTION_PLAYPAUSE) ||
	      event_is_action(sc->sc_event, ACTION_PLAY) ||
	      event_is_action(sc->sc_event, ACTION_PAUSE)) {
      
      sc->sc_hold = action_update_hold_by_event(sc->sc_hold, sc->sc_event);
      mp_send_cmd_head(sc->sc_mp, sc->sc_mq, sc->sc_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(sc->sc_mp, sc->sc_hold, NULL);
      
    } else if(event_is_type(sc->sc_event, EVENT_INTERNAL_PAUSE)) {
      sc->sc_hold = 1;
      mp_send_cmd_head(sc->sc_mp, sc->sc_mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(sc->sc_mp, sc->sc_hold, sc->sc_event->e_payload);
      
    } else if(event_is_action(sc->sc_event, ACTION_SKIP_BACKWARD) ||
	      event_is_action(sc->sc_event, ACTION_SKIP_FORWARD) ||
	      event_is_action(sc->sc_event, ACTION_STOP)) {
      mp_flush(sc->sc_mp, 0);
      break;
    }   
    
    event_release(sc->sc_event);
  }

exit_playback_thread:
  // notify http stream reader about stop_playing
  sc->sc_stop_playback = 1;

  hts_mutex_lock(&sc->sc_stream_buffer_mutex);
  hts_cond_signal(&sc->sc_stream_buffer_drained_cond);
  hts_mutex_unlock(&sc->sc_stream_buffer_mutex);

  TRACE(TRACE_DEBUG,"shoutcast_playback", "Exiting thread");

  return NULL;
}

/**
 *
 */
static int sc_initialize(sc_shoutcast_t *sc)
{
    int res;
    void *probe_buffer;
    AVIOContext *probe;

    //http_header_t *hh;
    // debug output of stream headers
    // LIST_FOREACH(hh, &sc->sc_headers, hh_link) {
    //  TRACE(TRACE_DEBUG,"shoutcast", "%s: %s", hh->hh_key, hh->hh_value);
    // }

    sc->sc_fctx = avformat_alloc_context();

    // Probe a copy of stream buffer
    probe_buffer = av_malloc(SC_AVIO_BUFFER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
    memset(probe_buffer, 0, SC_AVIO_BUFFER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(probe_buffer, sc->sc_stream_buffer.data, SC_AVIO_BUFFER_SIZE);
    probe = avio_alloc_context(probe_buffer, SC_AVIO_BUFFER_SIZE, 
			       0, NULL, NULL, NULL, NULL);
    res = av_probe_input_buffer(probe, &sc->sc_fctx->iformat, NULL, NULL, 0,0);
    av_free(probe);
    if (res < 0) {
      TRACE(TRACE_DEBUG, "shoutcast", "Failed to probe stream format.");
      return 1;
    }


    // Initialize av stram io and decoder
    void *buffer = av_malloc(SC_AVIO_BUFFER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
    sc->sc_ioctx = avio_alloc_context(buffer, SC_AVIO_BUFFER_SIZE, 
				   0, sc, sc_avio_read_packet, NULL, NULL);
    sc->sc_fctx->pb = sc->sc_ioctx;

    res = avformat_open_input(&sc->sc_fctx, "shoutcast", sc->sc_fctx->iformat, NULL);
    if (res < 0) {
      TRACE(TRACE_DEBUG, "shoutcast", "Failed to open stream.");
      return 1;
    }

    res = av_find_best_stream(sc->sc_fctx, AVMEDIA_TYPE_AUDIO, -1, -1, &sc->sc_decoder, 0); 
    if (res < 0) {
      if (res == AVERROR_STREAM_NOT_FOUND)
	TRACE(TRACE_DEBUG, "shoutcast", "Audio stream not found.");
      else if (res == AVERROR_DECODER_NOT_FOUND)
	TRACE(TRACE_DEBUG, "shoutcast", "Failed to find decoder for audio stream.");
      return 1;
    }
    sc->sc_audio_stream_idx = res;


    res = avformat_find_stream_info(sc->sc_fctx, NULL);
    if (res < 0) {
      TRACE(TRACE_DEBUG, "shoutcast", "Failed to find stream info.");
      return 1;
    }


    TRACE(TRACE_DEBUG,"shoutcast", "Detected stream format: %.2f kb/s, %s",
	  sc->sc_fctx->bit_rate/1000.0f, sc->sc_decoder->long_name);

    res = avcodec_open2(sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec,
			sc->sc_decoder, NULL);
    if (res < 0)
    {
      TRACE(TRACE_DEBUG, "shoutcast", "Failed to open codec.");
      return -1;
    }    
    

    av_init_packet(&sc->sc_pkt);

    // init media pipe
    mp_set_playstatus_by_hold(sc->sc_mp, sc->sc_hold, NULL);
    sc->sc_mp->mp_audio.mq_stream = 0;
    mp_configure(sc->sc_mp, MP_PLAY_CAPS_PAUSE, MP_BUFFER_NONE);
    mp_become_primary(sc->sc_mp);    

    sc->sc_initialized = 1;

    // all good lets start playback thread
    hts_thread_create_joinable("shoutcast playback", &sc->sc_playback_thread_id, 
			       sc_playback_thread, sc, THREAD_PRIO_NORMAL);
    
    return 0;
}

static int sc_stream_data(void *opaque, void *data, size_t size)
{
  sc_shoutcast_t *sc = (sc_shoutcast_t*)opaque;

  // If playback has stopped, end reading stream..
  if (sc->sc_stop_playback == 1)
    return -1;

  // Fill up the stream buffer
  if (sc->sc_stream_buffer.fillsize < SC_STREAM_BUFFER_SIZE - size)
  {
    hts_mutex_lock(&sc->sc_stream_buffer_mutex);
    memcpy(sc->sc_stream_buffer.data + sc->sc_stream_buffer.fillsize, data, size);
    sc->sc_stream_buffer.fillsize += size;
    TRACE(TRACE_DEBUG,"shoutcast", "Buffering  %.2f%%", 
	  100.0f * (sc->sc_stream_buffer.fillsize / (float)sc->sc_stream_buffer.size));
    hts_mutex_unlock(&sc->sc_stream_buffer_mutex);
    return 0;
  }

  // Only do init one time on first buffer fill
  if (sc->sc_initialized == 0) {
    int res = sc_initialize(sc);
    return res;
  }
 
  // TRACE(TRACE_DEBUG,"shoutcast", "Got chunk of stream %d bytes",size);

  // wait for signal that we can add data to stream buffer
  hts_mutex_lock(&sc->sc_stream_buffer_mutex);
  while ( sc->sc_stop_playback || (SC_STREAM_BUFFER_SIZE - sc->sc_stream_buffer.fillsize) < size)
    hts_cond_wait(&sc->sc_stream_buffer_drained_cond, &sc->sc_stream_buffer_mutex);

  // If playback has stopped, exit..
  if (sc->sc_stop_playback == 1)
    return -1;

  // Add stream chunk to end of buffer
  memcpy(sc->sc_stream_buffer.data + sc->sc_stream_buffer.fillsize, data, size);
  sc->sc_stream_buffer.fillsize += size;

  return 0;
}

/**
 *
 */
static event_t *
be_shoutcast_play(const char *url0, media_pipe_t *mp, 
		   char *errbuf, size_t errlen, int hold,
		   const char *mimetype)
{
  int n;
  char *result;
  size_t ressize;
  int playlist_stream_cnt, current_stream_idx;
  sc_shoutcast_t *sc;
  http_header_t *hh;

  sc = malloc(sizeof(sc_shoutcast_t));
  memset(sc, 0, sizeof(sc_shoutcast_t));
  sc->sc_mp = mp;
  sc->sc_mq = &sc->sc_mp->mp_audio;
  sc->sc_hold = hold;

  sc->sc_stream_buffer.data = malloc(SC_STREAM_BUFFER_SIZE);
  sc->sc_stream_buffer.size = SC_STREAM_BUFFER_SIZE;

  sc->sc_samples = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

  hts_mutex_init(&sc->sc_stream_buffer_mutex);
  hts_cond_init(&sc->sc_stream_buffer_drained_cond, &sc->sc_stream_buffer_mutex);

  url0 += strlen("shoutcast:");

  // First get headers and check if we got a playlist url
  current_stream_idx = playlist_stream_cnt = 0;
  n = http_request(url0, NULL, 
		  &result, &ressize, errbuf, errlen, 
		   NULL, NULL, FA_DONOTREUSE, &sc->sc_headers, NULL, NULL,
		  NULL, NULL, NULL);

  if(n) {
    TRACE(TRACE_ERROR, "shoutcast", "Failed top open url %d: %s",n, errbuf);
    return NULL;
  }

  LIST_FOREACH(hh, &sc->sc_headers, hh_link) {
    // TRACE(TRACE_DEBUG,"shoutcast", "%s: %s", hh->hh_key, hh->hh_value);
    if (!strcmp(hh->hh_key, "Content-Type")) {
      if (!strncmp(hh->hh_value, "audio/x-scpls", strlen("audio/x-scpls")))
	playlist_stream_cnt = sc_parse_playlist_pls(sc, result);
      else if (!strncmp(hh->hh_value, "audio/x-mpegurl", strlen("audio/x-mpegurl")))
	playlist_stream_cnt = sc_parse_playlist_m3u(sc, result);
      else if (strncmp(hh->hh_value, "audio/", 6)) {
	TRACE(TRACE_ERROR, "shoutcast", "Unhandled content type: %s",hh->hh_value);
	return NULL;
      }
    }
  }
  free(result);

retry_next_stream:
  free(sc->sc_stream_url);
  current_stream_idx += 1;
  if (current_stream_idx == playlist_stream_cnt) {
    TRACE(TRACE_DEBUG, "shoutcast", "No more streams in playlist.");
    return NULL;
  }

  if(playlist_stream_cnt)
    sc->sc_stream_url = strdup(sc->sc_playlist_streams[current_stream_idx]);
  else
    sc->sc_stream_url = strdup(url0);

  TRACE(TRACE_DEBUG, "shoutcast", "Starting stream playback of url '%s'", sc->sc_stream_url);

  // http connect and request stream
  n = http_request(sc->sc_stream_url, NULL, 
		  &result, &ressize, errbuf, errlen, 
		   NULL, NULL, 0, &sc->sc_headers, NULL, NULL,
		  NULL, sc_stream_data, sc);

  if(n) {
    TRACE(TRACE_DEBUG, "shoutcast", "Failed to open stream %s", errbuf);
    if (current_stream_idx < playlist_stream_cnt)
      goto retry_next_stream;

    return NULL;
  }

  if(sc->sc_playback_thread_id != 0)
    hts_thread_join(&sc->sc_playback_thread_id);

  // cleanup and free allocated memory
  event_t *ev = sc->sc_event;
  if(sc->sc_initialized) {
      avcodec_close(sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec);
      avformat_free_context(sc->sc_fctx);
      av_free(sc->sc_ioctx);
  }

  free(sc->sc_stream_buffer.data);
  free(sc->sc_samples);
  free(sc);

  return ev;
}

/**
 *
 */
static backend_t be_shoutcast = {
  .be_canhandle = be_shoutcast_canhandle,
  .be_play_audio = be_shoutcast_play,
};

BE_REGISTER(shoutcast);
