/*
 *  Shoutcast backend
 *  Copyright (C) 2012 Henrik Andersson
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

#include "backend/backend.h"
#include "media.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsbuf.h"
#include "networking/net.h"

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define SC_MAX_PLAYLIST_STREAMS   20

#define SC_CHUNK_SIZE             8192*2

typedef struct sc_shoutcast { 
  int sc_initialized;
  int sc_hold;

  tcpcon_t *sc_tc;

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

  htsbuf_queue_t *sc_stream_titles;
  int sc_stream_title_byte_offset;
  int sc_stream_title_last_byte_offset;

  int sc_stream_bitrate;
  int sc_stream_metaint;
  int sc_stream_chunk_size;

  htsbuf_queue_t *sc_stream_buffer;

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
  char title[512];
  uint32_t offs, len;
  size_t rs = 0;
  sc_shoutcast_t *sc = (sc_shoutcast_t*)opaque;

  hts_mutex_lock(&sc->sc_stream_buffer_mutex);

  rs = htsbuf_read(sc->sc_stream_buffer, buf, buf_size);

  // update stream title if reached the point in buffer
  if (sc->sc_stream_titles && htsbuf_peek(sc->sc_stream_titles, &offs, sizeof(uint32_t)) != 0)
  {
    // TRACE(TRACE_DEBUG,"shoutcast", "We have stream titles on queue.");
    if (sc->sc_stream_title_byte_offset == -1)
    {
      // TRACE(TRACE_DEBUG,"shoutcast", "New stream title lets count down bytes processed.");
      htsbuf_drop(sc->sc_stream_titles, sizeof(uint32_t));
      sc->sc_stream_title_byte_offset = offs;
    }
    else
      sc->sc_stream_title_byte_offset -= rs;

    if (sc->sc_stream_title_byte_offset <= 0)
    {
      // TRACE(TRACE_DEBUG,"shoutcast", "Time to show streamtitle.");

      // read stream title from buf
      htsbuf_read(sc->sc_stream_titles, &len, sizeof(uint32_t));
      len = MIN(512, len);
      htsbuf_read(sc->sc_stream_titles, &title, len);
      title[511] = '\0';
      prop_t *tp = prop_get_by_name(PNVEC("global", "media", "current", "metadata","title"), 1, NULL);
      prop_set_string(tp, title);
      sc->sc_stream_title_byte_offset = -1;

      // if no more streamtitles on queue reset last offset
      if (htsbuf_peek(sc->sc_stream_titles, &offs, sizeof(uint32_t)) == 0)
	sc->sc_stream_title_last_byte_offset = 0;
    }
  }


  // signal that data has been drained from buffer
  hts_cond_signal(&sc->sc_stream_buffer_drained_cond);

  hts_mutex_unlock(&sc->sc_stream_buffer_mutex);

  // TRACE(TRACE_DEBUG, "shoutcast", "sc_read_packet(%x, %d)", buf, s);

  return rs;
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

  // start playback loop if initialized
  while(sc->sc_initialized && !sc->sc_stop_playback) {

    if (sc->sc_mb == NULL) {
      sc->sc_mb = media_buf_alloc_unlocked(sc->sc_mp, sizeof(int16_t)*SC_CHUNK_SIZE*2);
      sc->sc_mb->mb_data_type = MB_AUDIO;
      sc->sc_mb->mb_channels = sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec->channels;
      sc->sc_mb->mb_rate = sc->sc_fctx->streams[sc->sc_audio_stream_idx]->codec->sample_rate;
      
      sc->sc_mb->mb_pts = sample * 1000000LL / sc->sc_mb->mb_rate;
    
      if(registered_play && sc->sc_mb->mb_pts > METADB_AUDIO_PLAY_THRESHOLD) {
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
    }

    event_release(sc->sc_event);
  }

exit_playback_thread:
  // notify http stream reader about stop_playing
  sc->sc_playback_thread_id = 0;
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

    sc->sc_stream_title_byte_offset = -1;
    sc->sc_stream_titles = NULL;

    sc->sc_fctx = avformat_alloc_context();

    // Probe a copy of stream buffer
    probe_buffer = av_malloc(sc->sc_stream_chunk_size + FF_INPUT_BUFFER_PADDING_SIZE);
    memset(probe_buffer, 0, sc->sc_stream_chunk_size + FF_INPUT_BUFFER_PADDING_SIZE);
    htsbuf_peek(sc->sc_stream_buffer, probe_buffer, sc->sc_stream_chunk_size);
    probe = avio_alloc_context(probe_buffer, sc->sc_stream_chunk_size, 
			       0, NULL, NULL, NULL, NULL);
    res = av_probe_input_buffer(probe, &sc->sc_fctx->iformat, NULL, NULL, 0,0);
    av_free(probe);
    if (res < 0) {
      TRACE(TRACE_DEBUG, "shoutcast", "Failed to probe stream format.");
      return 1;
    }


    // Initialize av stram io and decoder
    void *buffer = av_malloc(sc->sc_stream_chunk_size + FF_INPUT_BUFFER_PADDING_SIZE);
    sc->sc_ioctx = avio_alloc_context(buffer, sc->sc_stream_chunk_size, 
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
    mp_configure(sc->sc_mp, MP_PLAY_CAPS_PAUSE, MP_BUFFER_NONE, 0);
    mp_become_primary(sc->sc_mp);    

    sc->sc_initialized = 1;

    // all good lets start playback thread
    hts_thread_create_joinable("shoutcast playback", &sc->sc_playback_thread_id, 
			       sc_playback_thread, sc, THREAD_PRIO_NORMAL);
    
    return 0;
}

static int sc_stream_data(sc_shoutcast_t *sc, char *buf, int bufsize)
{
  // If playback has stopped, end reading stream..
  if (sc->sc_stop_playback == 1)
    return -1;

  if (bufsize == 0)
    return 0;
  
  // Initialize stream buffer if not done
  if (sc->sc_stream_buffer == NULL) {
    sc->sc_stream_buffer = malloc(sizeof(htsbuf_queue_t));
    htsbuf_queue_init(sc->sc_stream_buffer, sc->sc_stream_chunk_size * 4 * 4);
  }

  // Fill up the stream buffer
  if (sc->sc_stream_buffer->hq_size < sc->sc_stream_buffer->hq_maxsize - bufsize)
  {
    hts_mutex_lock(&sc->sc_stream_buffer_mutex);
    htsbuf_append(sc->sc_stream_buffer, buf, bufsize);
    TRACE(TRACE_DEBUG,"shoutcast", "Buffering %.2f%%",
	  100.0f * (sc->sc_stream_buffer->hq_size / (float)sc->sc_stream_buffer->hq_maxsize));
    hts_mutex_unlock(&sc->sc_stream_buffer_mutex);
    return 0;
  }

  // Only do init one time on first buffer fill
  if (sc->sc_initialized == 0) {
    int res = sc_initialize(sc);
    return res;
  }
 
  // wait for signal that we can add data to stream buffer
  hts_mutex_lock(&sc->sc_stream_buffer_mutex);
  while(1)
  {
    if (sc->sc_stop_playback)
      return -1;

    if ((sc->sc_stream_buffer->hq_maxsize - sc->sc_stream_buffer->hq_size) > bufsize)
      break;

    hts_cond_wait(&sc->sc_stream_buffer_drained_cond, &sc->sc_stream_buffer_mutex);
  }

  // Add stream chunk to end of buffer
  htsbuf_append(sc->sc_stream_buffer, buf, bufsize);

  hts_mutex_unlock(&sc->sc_stream_buffer_mutex);
  return 0;
}

static int sc_stream_read_headers(sc_shoutcast_t *sc, char *errbuf, size_t errlen)
{
  int li;
  int code = 200;
  char line[256], tmp[256], *ps;
  
  for (li = 0; ;li++) {
    if (tcp_read_line(sc->sc_tc, line, sizeof(line)) < 0) {
      snprintf(errbuf,errlen,"tcp read < 0");
      return -1;
    }

    if(!line[0])
      break;

    TRACE(TRACE_DEBUG,"shoutcast", "Header line: '%s'", line);

    // get metaint
    if ( !strncmp(line, "icy-br", 6) && ((ps = strchr(line,':')) != NULL)) {
      while(*ps == ' ' || *ps == ':') ps++;
      sc->sc_stream_bitrate = atoi(ps);
    }

    // get bitrate
    if ( !strncmp(line, "icy-metaint", 11) && ((ps = strchr(line,':')) != NULL)) {
      while(*ps == ' ' || *ps == ':') ps++;
      sc->sc_stream_metaint = atoi(ps);
    }

    if (li == 0 && sscanf(line, "%s %d", tmp, &code) != 2)
      return -1;
    else continue;

  }

  return code;
}

/**
 *
 */
static void sc_parse_metadata(sc_shoutcast_t *sc, char *md, int mdlen)
{
  char *ps, *pe;
  uint32_t offs,size;
  TRACE(TRACE_DEBUG,"shoutcast","metadata: '%s'", md);
  
  if((ps = strstr(md,"StreamTitle='")) != NULL)
  {
    ps += strlen("StreamTitle='");
    if ((pe = strstr(ps,"';")) == NULL)
      return;
    *pe = '\0';

    if (strlen(ps) == 0)
      return;

    if (sc->sc_stream_titles == NULL) {
      sc->sc_stream_titles = malloc(sizeof(htsbuf_queue_t));
      htsbuf_queue_init(sc->sc_stream_titles, 0);
    }

    // stream title with byte offset to queue
    offs = sc->sc_stream_buffer->hq_size - sc->sc_stream_title_last_byte_offset;
    sc->sc_stream_title_last_byte_offset = offs;
    size = strlen(ps)+1;
    htsbuf_append(sc->sc_stream_titles, &offs, sizeof(uint32_t));
    htsbuf_append(sc->sc_stream_titles, &size, sizeof(uint32_t));
    htsbuf_append(sc->sc_stream_titles, ps, strlen(ps)+1);
    // TRACE(TRACE_DEBUG,"shoutcast", "Wrote new stream title to queue %d bytes.", sc->sc_stream_titles->hq_size);
  }
}

/**
 *
 */
static int sc_stream_start(sc_shoutcast_t *sc, char *errbuf, size_t errlen)
{
  char *url, *tmp;
  char *ps,*pe;
  char *doc;
  char *hostname;
  int ssl = 0;
  int port = 80;
  htsbuf_queue_t q;

  if (!strncmp(sc->sc_stream_url,"https://", 5))
    ssl = 1, port = 443;

  if (strlen(sc->sc_stream_url) < strlen(ssl ? "https://" : "http://"))
    return -1;

  url = strdup(sc->sc_stream_url + strlen((ssl ? "https://" : "http://")));

  // Get doc from url
  if ((ps = strchr(url, '/')) != NULL)
    doc = strdup(ps);
  else
    doc = strdup("/");

  // Get port from url
  if ((ps = strchr(url, ':')) != NULL) {
    pe = strchr(ps, '/');
    if (!pe)
      pe = ps + strlen(ps);
    
    if (pe) {
      tmp = strndup(ps+1, pe-ps-1);
      port = atoi(tmp);
      free(tmp);
    }
  }

  // Get host from url
  if ((pe = strchr(url, ':')) == NULL)
    pe = strchr(url, '/');
  if (!pe)
    return -1;
  hostname = strndup(url, pe-url);

  free(url);

  // Connect
  if((sc->sc_tc = tcp_connect(hostname, port, errbuf, errlen, 5000, ssl)) == NULL) {
    return -1;
  }
  tcp_huge_buffer(sc->sc_tc);

  // Send http request with ICY metatdata header
  htsbuf_queue_init(&q,0);
  htsbuf_qprintf(&q, "GET %s HTTP/1.1\r\n", doc);
  htsbuf_qprintf(&q, "Icy-MetaData: 1\r\n");
  htsbuf_qprintf(&q, "\r\n");

  free(hostname);
  free(doc);

  //htsbuf_dump_raw_stderr(&q);

  tcp_write_queue(sc->sc_tc, &q);

  // Read and parse headers
  if(sc_stream_read_headers(sc, errbuf, errlen) != 200)
    return -1;

  TRACE(TRACE_DEBUG,"shoutcast", "bitrate %d, metaint %d",sc->sc_stream_bitrate, sc->sc_stream_metaint);

  // start reading stream
  char md[4097];
  char md_len;

  // use hints to set chunk size
  sc->sc_stream_chunk_size = sc->sc_stream_metaint;

  if (sc->sc_stream_chunk_size == 0 && sc->sc_stream_bitrate == 0)
    sc->sc_stream_chunk_size = 8192;

  if (sc->sc_stream_chunk_size == 0)
    sc->sc_stream_chunk_size = (((sc->sc_stream_bitrate * 1024) / 8) / 4);

  char *buf = malloc(sc->sc_stream_chunk_size);
  
  while(!sc->sc_stop_playback) {
    // read stream buf
    if (tcp_read_data(sc->sc_tc, buf, sc->sc_stream_chunk_size, NULL, NULL) < 0)
      break;

    // push chunk to stream buffer handler
    if (sc_stream_data(sc, buf, sc->sc_stream_chunk_size) != 0)
      break;
    
    // if icy inline meta data read it.
    if (sc->sc_stream_metaint) {
      md_len = 0;
      if (tcp_read_data(sc->sc_tc, &md_len, 1, NULL, NULL) < 0)
	break;
      
      if (md_len) {
	if (tcp_read_data(sc->sc_tc, md, md_len*16, NULL, NULL) < 0)
	  break;

	sc_parse_metadata(sc, md, md_len*16);
      } 

    }
  }

  free(buf);

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

  sc->sc_samples = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

  hts_mutex_init(&sc->sc_stream_buffer_mutex);
  hts_cond_init(&sc->sc_stream_buffer_drained_cond, &sc->sc_stream_buffer_mutex);

  url0 += strlen("shoutcast:");

  // First get headers and check if we got a playlist url
  current_stream_idx = playlist_stream_cnt = 0;
  n = http_request(url0, NULL, 
		  &result, &ressize, errbuf, errlen, 
		   NULL, NULL, 0, &sc->sc_headers, NULL, NULL,
		  NULL, NULL);

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

  // connect, write and read headers then start read stream
  n = sc_stream_start(sc, errbuf, errlen);

  if(n) {
    TRACE(TRACE_DEBUG, "shoutcast", "Failed to open stream -- %s", errbuf);
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

  htsbuf_queue_flush(sc->sc_stream_buffer);
  free(sc->sc_stream_buffer);
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
