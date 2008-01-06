/*
 *  Radio
 *  Copyright (C) 2007 Andreas Öman
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

#define _GNU_SOURCE
#include <string.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <libglw/glw.h>

#include <curl/curl.h>


#include "showtime.h"
#include "input.h"
#include "play_file.h"
#include "miw.h"
#include "radio.h"
#include "layout/layout.h"

typedef enum {
    RC_ICECAST

  } rc_type_t;

typedef struct radio_channel {
  TAILQ_ENTRY(radio_channel) rc_link;

  const char *rc_url;
  const char *rc_provider;
  const char *rc_name;

  rc_type_t rc_type;

  glw_t *rc_status_container;
  glw_t *rc_meta_text1;
  glw_t *rc_play_caption;

  enum {
    RC_STOPPED,
    RC_LOADING,
    RC_PLAYING,
    RC_ERROR,
  } rc_status;


} radio_channel_t;



typedef struct radio {

  TAILQ_HEAD(, radio_channel) r_channels;
  glw_t *r_list;

  appi_t *r_ai;

  radio_channel_t *r_cur_channel;
  radio_channel_t *r_req_channel;

  pthread_cond_t r_cond;
  pthread_mutex_t r_mutex;

} radio_t;

static int 
radio_channel_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}


static void *radio_player_loop(void *aux);

/*
 *
 */

static void
radio_channel_update_status(radio_channel_t *rc, int status)
{
  glw_t *p;

  if(rc->rc_status == status)
    return;

  rc->rc_status = status;

  p = rc->rc_status_container;

  switch(status) {
  case RC_STOPPED:
    glw_destroy_childs(p);
    break;

  case RC_LOADING:
    glw_create(GLW_BITMAP, 
	       GLW_ATTRIB_PARENT, glw_create(GLW_ROTATOR,
					     GLW_ATTRIB_PARENT, p,
					     NULL),
	       GLW_ATTRIB_FILENAME, "icon://loading.png",
	       NULL);
    break;

  case RC_PLAYING:
    glw_create(GLW_BITMAP, 
	       GLW_ATTRIB_PARENT, p, 
	       GLW_ATTRIB_FILENAME, "icon://media-playback-start.png",
	       NULL);
    break;

  case RC_ERROR:
    glw_create(GLW_BITMAP, 
	       GLW_ATTRIB_PARENT, p, 
	       GLW_ATTRIB_FILENAME, "icon://error.png",
	       NULL);
    break;
  }
}



/*
 *
 */

static void
radio_channel_add(radio_t *r, rc_type_t type, const char *provider,
		  const char *name, const char *logo, const char *url)
{
  radio_channel_t *rc = calloc(1, sizeof(radio_channel_t));
  glw_t *c, *content, *y, *z;

  glw_t *le;
  
  rc->rc_url = strdup(url);
  rc->rc_type = type;
  rc->rc_provider = strdup(provider);
  rc->rc_name = strdup(name);

  content = glw_create(GLW_CONTAINER_Y,
		       NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, content,
	     NULL);

  y = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, content,
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 GLW_ATTRIB_FILENAME, "icon://plate.png",
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, content,
	     NULL);

  z = glw_create(GLW_CONTAINER_Z,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  rc->rc_status_container = glw_create(GLW_XFADER,
				       GLW_ATTRIB_ALPHA, 0.25,
				       GLW_ATTRIB_PARENT, z,
				       NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, z,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, name,
	     NULL);
		 
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, provider,
	     NULL);

  rc->rc_meta_text1 = glw_create(GLW_TEXT_BITMAP,
				 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
				 GLW_ATTRIB_PARENT, y,
				 GLW_ATTRIB_CAPTION, "",
				 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

  /* */


  c = glw_create(GLW_NAV_ENTRY,
		 GLW_ATTRIB_PARENT, r->r_list,
		 GLW_ATTRIB_CONTENT, content,
		 GLW_ATTRIB_SIGNAL_HANDLER,  radio_channel_callback, rc, 0,
		 NULL);

  le = glw_create(GLW_BITMAP,
		  GLW_ATTRIB_PARENT, c,
		  GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		  GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		  NULL);
  
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, le,
	     GLW_ATTRIB_CAPTION, name,
	     NULL);

  TAILQ_INSERT_TAIL(&r->r_channels, rc, rc_link);
}


/*
 *
 */
static void
radio_channel_configure(radio_t *r, struct config_head *head)
{
  const char *provider, *title, *url, *type;
  int typei;

  if((url = config_get_str_sub(head, "url", NULL)) == NULL)
    return;

  if((type = config_get_str_sub(head, "type", NULL)) == NULL)
    return;

  if((provider = config_get_str_sub(head, "provider", NULL)) == NULL)
    provider = "";

  if((title = config_get_str_sub(head, "title", NULL)) == NULL)
    title = url;

  if(!strcasecmp(type, "icecast"))
    typei = RC_ICECAST;
  else
    return;

  radio_channel_add(r, typei, provider, title, NULL, url);
  
}


/*
 *
 */


static void
radio_channels_configure(radio_t *r)
{
  config_entry_t *ce;

  TAILQ_FOREACH(ce, &config_list, ce_link) {
    if(ce->ce_type == CFG_SUB && !strcasecmp("radiochannel", ce->ce_key)) {
      radio_channel_configure(r, &ce->ce_sub);
    }
  }
}

/*
 *
 */
static void *
radio_thread(void *aux)
{
  appi_t *ai = aux;
  radio_t *r = calloc(1, sizeof(radio_t));
  inputevent_t ie;
  pthread_t ptid;

  pthread_cond_init(&r->r_cond, NULL);
  pthread_mutex_init(&r->r_mutex, NULL);

  r->r_ai = ai;
  TAILQ_INIT(&r->r_channels);


  ai->ai_widget = r->r_list = 
    glw_create(GLW_NAV,
	       GLW_ATTRIB_Y_SLICES, 15,
	       GLW_ATTRIB_SIGNAL_HANDLER, appi_widget_post_key, ai, 0,
	       NULL);
  
  radio_channels_configure(r);

  if(TAILQ_FIRST(&r->r_list->glw_childs) == NULL) {
    while(1) 
      sleep(1);
  }

  pthread_create(&ptid, NULL, radio_player_loop, r);

  while(1) {

    input_getevent(&ai->ai_ic, 1, &ie, NULL);

    switch(ie.type) {

    default:
      break;

    case INPUT_KEY:
      switch(ie.u.key) {
      default:
	break;

      case INPUT_KEY_CLEAR:
      case INPUT_KEY_STOP:
	pthread_mutex_lock(&r->r_mutex);
	r->r_req_channel = NULL;
	pthread_cond_signal(&r->r_cond);
	pthread_mutex_unlock(&r->r_mutex);
	break;
      

      case INPUT_KEY_UP:
	glw_nav_signal(r->r_list, GLW_SIGNAL_UP);
	break;

      case INPUT_KEY_DOWN:
	glw_nav_signal(r->r_list, GLW_SIGNAL_DOWN);
	break;

      case INPUT_KEY_ENTER:
	glw_nav_signal(r->r_list, GLW_SIGNAL_CLICK);
	pthread_mutex_lock(&r->r_mutex);
	r->r_req_channel = glw_get_opaque(r->r_list->glw_selected,
					  radio_channel_callback);
	radio_channel_update_status(r->r_req_channel, RC_LOADING);
	pthread_cond_signal(&r->r_cond);
	pthread_mutex_unlock(&r->r_mutex);
	media_pipe_acquire_audio(&ai->ai_mp);
	break;
	
      case INPUT_KEY_LEFT:
      case INPUT_KEY_BACK:
      case INPUT_KEY_CLOSE:
	layout_hide(ai);
	break;
      }
    }
  }
  return NULL;
}

/*
 *
 */

void 
radio_spawn(void)
{
  appi_t *ai = appi_spawn("Radio", "icon://radio.png");

  pthread_create(&ai->ai_tid, NULL, radio_thread, ai);
}


/*
 *
 */

static void
set_play_caption(radio_channel_t *rc, const char *metatxt)
{
  char tmp[200];

  if(metatxt != NULL) {
    snprintf(tmp, sizeof(tmp), "%s - %s - %s", 
	     rc->rc_provider, rc->rc_name, metatxt);
  } else {
    snprintf(tmp, sizeof(tmp), "%s - %s", rc->rc_provider, rc->rc_name);
  }
  /* Title */
  
  glw_set(rc->rc_play_caption,
	  GLW_ATTRIB_CAPTION, tmp,
	  NULL);

}
/*
 *
 */

static glw_t *
radio_create_miw(radio_channel_t *rc, media_pipe_t *mp)
{
  glw_t *x, *c;


  c = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);
  
  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  glw_create(GLW_BITMAP, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_FILENAME, "icon://media-playback-start.png",
	     NULL);

  rc->rc_play_caption = 
    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_WEIGHT, 24.0f,
	       GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	       GLW_ATTRIB_PARENT, x,
	       GLW_ATTRIB_CAPTION, "",
	       NULL);

  set_play_caption(rc, NULL);

  return c;


}




/****************************************************************************
 *
 *   Icecast support
 *
 */




typedef struct icecast_player {

  radio_t *ip_r;
  radio_channel_t *ip_rc;

  codecwrap_t *ip_cw;

  int ip_icy_metaint;
  int ip_icy_metacnt;
  int ip_icy_metalen;

  char *ip_icy_meta_input;

} icecast_player_t;



static size_t
icecast_write(void *ptr, size_t size, size_t nmemb, void *aux)
{
  media_buf_t *mb;
  icecast_player_t *ip = aux;
  radio_t *r = ip->ip_r;
  appi_t *ai = r->r_ai;
  size_t s = size * nmemb;
  int rlen, ilen;
  uint8_t *outbuf;
  int outlen;
  char *x, *y;
  radio_channel_t *rc = ip->ip_rc;
  media_pipe_t *mp = &ai->ai_mp;
  codecwrap_t *cw = ip->ip_cw;
  
  if(r->r_cur_channel != r->r_req_channel) {
    return 0;
  }

  if(mp->mp_info_widget == NULL) {
    media_pipe_acquire_audio(mp);
    mp_set_playstatus(mp, MP_PLAY);
    mp->mp_info_widget = radio_create_miw(rc, mp);
  }

  while(s > 0) {

    if(ip->ip_icy_meta_input != NULL) {
      
      ilen = ip->ip_icy_metalen - ip->ip_icy_metacnt;
      if(s < ilen)
	ilen = s;

      memcpy(ip->ip_icy_meta_input + ip->ip_icy_metacnt, ptr, ilen);

      ip->ip_icy_metacnt += ilen;
      s -= ilen;
      ptr += ilen;

      if(ip->ip_icy_metalen > ip->ip_icy_metacnt)
	continue;

      x = strstr(ip->ip_icy_meta_input, "StreamTitle='");
      if(x != NULL) {
	x += strlen("StreamTitle='");

	y = strstr(x, "';Stream");
	if(y == NULL) 
	  y = strrchr(x, '\'');

	if(y != NULL) {
	  *y = 0;
	  
	  glw_set(rc->rc_meta_text1,
		  GLW_ATTRIB_CAPTION, x,
		  NULL);

	  set_play_caption(rc, x);
	}
      }

      free(ip->ip_icy_meta_input);
      ip->ip_icy_meta_input = NULL;
      
      ip->ip_icy_metacnt = ip->ip_icy_metaint;
    }


    if(ip->ip_icy_metacnt == 0) {
      ip->ip_icy_metalen = *(char *)ptr * 16;
      if(ip->ip_icy_metalen > 0) {
	ip->ip_icy_meta_input = malloc(ip->ip_icy_metalen + 1);
	ip->ip_icy_meta_input[ip->ip_icy_metalen] = 0;
      } else {
	ip->ip_icy_metacnt = ip->ip_icy_metaint;
      }
      ptr++;
      s--;
      continue;
    }

    ilen = s > ip->ip_icy_metacnt ? ip->ip_icy_metacnt : s;

    wrap_lock_codec(cw);

    rlen = av_parser_parse(cw->parser_ctx, cw->codec_ctx,
			   &outbuf, &outlen, ptr, ilen, 0, 0);

    if(outlen > 0) {

      mb = media_buf_alloc();

      mb->mb_cw = wrap_codec_ref(cw);
      mb->mb_data_type = MB_AUDIO;
      mb->mb_data = malloc(outlen);
      mb->mb_size = outlen;
      mb->mb_pts = AV_NOPTS_VALUE;
      wrap_unlock_codec(cw);
      memcpy(mb->mb_data, outbuf, outlen);
      wrap_unlock_codec(cw);
      mb_enqueue(mp, &mp->mp_audio, mb);
      wrap_lock_codec(cw);

      radio_channel_update_status(rc, RC_PLAYING);

    }

    ptr += rlen;
    s -= rlen;
    ip->ip_icy_metacnt -= rlen;

    wrap_unlock_codec(cw);
  }

  return size * nmemb;
}



static size_t
icecast_header(void *ptr, size_t size, size_t nmemb, void *aux)
{
  icecast_player_t *ip = aux;
  char *h;
  size_t s = size * nmemb;

  h = alloca(s + 1);
  memcpy(h, ptr, s);
  h[s] = 0;

  if(!strncmp(h, "icy-metaint: ", strlen("icy-metaint: "))) {
    ip->ip_icy_metaint = atoi(h + strlen("icy-metaint: "));
    ip->ip_icy_metacnt = ip->ip_icy_metaint;
  }
  return size * nmemb;
}



static void
icecast_play(radio_t *r, radio_channel_t *rc, media_pipe_t *mp)
{
  icecast_player_t *ip = calloc(1, sizeof(icecast_player_t));
  formatwrap_t *fw;
  char errbuf[CURL_ERROR_SIZE];
  int err;
  CURL *curl_handle;
  struct curl_slist *headerlist, *http200aliases;
  glw_t *w;

  ip->ip_r = r;
  ip->ip_rc = rc;

  radio_channel_update_status(rc, RC_LOADING);

  while(1) {
  
    fw = wrap_format_create(NULL, 1);

    ip->ip_cw = wrap_codec_create(CODEC_ID_MP3, CODEC_TYPE_AUDIO, 1, fw, NULL);

    mp->mp_audio.mq_stream = 0;

    headerlist = curl_slist_append(NULL, "Icy-MetaData:1");
    http200aliases = curl_slist_append(NULL, "ICY 200 OK");

    curl_handle = curl_easy_init();
 
    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1);

    /* set URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, rc->rc_url);

    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headerlist);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Winamp/2.x");
    curl_easy_setopt(curl_handle, CURLOPT_HTTP200ALIASES, http200aliases);

    /* send all data to this function  */

    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, ip);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, icecast_write);

    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, ip);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, icecast_header);

    curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 20);
    curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, 5);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5);
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1);

    curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, errbuf);

    mp_set_playstatus(mp, MP_PLAY);

    err = curl_easy_perform(curl_handle);
    media_pipe_release_audio(mp);

    w = mp->mp_info_widget;

    mp->mp_info_widget = NULL;

    mp_flush(mp);

    curl_slist_free_all(headerlist);
    curl_slist_free_all(http200aliases);

    curl_easy_cleanup(curl_handle);

    wrap_codec_deref(ip->ip_cw, 0);
    wrap_format_wait(fw);

    r->r_cur_channel = NULL;

    if(w != NULL)
      glw_destroy(w);

    if(err && mp_get_playstatus(mp) == MP_PLAY && 
       rc == r->r_req_channel) {
      radio_channel_update_status(rc, RC_ERROR);
      glw_set(rc->rc_meta_text1,
	      GLW_ATTRIB_COLOR, GLW_COLOR_RED,
	      GLW_ATTRIB_CAPTION, errbuf,
	      NULL);
      sleep(1);
      continue;
    }
    break;
  }

  free(ip);

}



/*
 *
 */

static void *
radio_player_loop(void *aux)
{
  radio_t *r = aux;
  appi_t *ai = r->r_ai;
  media_pipe_t *mp = &ai->ai_mp;
  radio_channel_t *rc;

  pthread_mutex_lock(&r->r_mutex);

  while(1) {

    while(r->r_req_channel == NULL)
      pthread_cond_wait(&r->r_cond, &r->r_mutex);

    r->r_cur_channel = r->r_req_channel;

    pthread_mutex_unlock(&r->r_mutex);
    
    rc = r->r_cur_channel;

    switch(rc->rc_type) {
    case RC_ICECAST:
      icecast_play(r, rc, mp);
      break;
    }

    pthread_mutex_lock(&r->r_mutex);


    if(rc != r->r_req_channel) {
      radio_channel_update_status(rc, RC_STOPPED);
      glw_set(rc->rc_meta_text1,
	      GLW_ATTRIB_CAPTION, "",
	      NULL);
    }
  }
}

