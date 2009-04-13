/*
 *  HTSP client
 *  Copyright (C) 2008,2009 Andreas Öman
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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <htsmsg/htsmsg.h>
#include <htsmsg/htsmsg_binary.h>
#include <arch/threads.h>
#include <arch/atomic.h>
#include <libavutil/sha1.h>

#include "showtime.h"
#include "networking/net.h"
#include "navigator.h"
#include "keyring.h"
#include "media.h"

#define HTSP_PROTO_VERSION 1 // Protocol version we implement

/* XXX: From lavf */
extern void url_split(char *proto, int proto_size,
		      char *authorization, int authorization_size,
		      char *hostname, int hostname_size,
		      int *port_ptr,
		      char *path, int path_size,
		      const char *url);


static hts_mutex_t htsp_global_mutex;
LIST_HEAD(htsp_connection_list, htsp_connection);
TAILQ_HEAD(htsp_msg_queue, htsp_msg);
LIST_HEAD(htsp_subscription_list, htsp_subscription);
LIST_HEAD(htsp_subscription_stream_list, htsp_subscription_stream);

static struct htsp_connection_list htsp_connections;

/**
 *
 */
typedef struct htsp_connection {
  LIST_ENTRY(htsp_connection) hc_global_link;
  int hc_fd;

  uint8_t hc_challenge[32];

  int hc_is_async;

  int hc_refcount;

  char *hc_hostname;
  int hc_port;

  int hc_seq_generator;
  int hc_sid_generator; /* Subscription ID */

  prop_t *hc_prop_root;
  prop_t *hc_prop_channels;
  prop_t *hc_prop_tags;

  hts_mutex_t hc_rpc_mutex;
  hts_cond_t hc_rpc_cond;
  struct htsp_msg_queue hc_rpc_queue;

  hts_mutex_t hc_worker_mutex;
  hts_cond_t hc_worker_cond;
  struct htsp_msg_queue hc_worker_queue;


  hts_mutex_t hc_subscription_mutex;
  struct htsp_subscription_list hc_subscriptions;

} htsp_connection_t;


/**
 *
 */
typedef struct htsp_msg {
  htsmsg_t *hm_msg;
  int hm_error;
  uint32_t hm_seq;
  TAILQ_ENTRY(htsp_msg) hm_link;
} htsp_msg_t;


/**
 *
 */
typedef struct htsp_page {
  nav_page_t h;
  
  htsp_connection_t *hp_hc;
  

} htsp_page_t;


/**
 *
 */
typedef struct htsp_subscription {
  LIST_ENTRY(htsp_subscription) hs_link;

  uint32_t hs_sid;
  media_pipe_t *hs_mp;
  
  struct htsp_subscription_stream_list hs_streams;

} htsp_subscription_t;


/**
 * Defines a component (audio, video, etc) inside a tv stream
 */
typedef struct htsp_subscription_stream {
  LIST_ENTRY(htsp_subscription_stream) hss_link;

  int hss_index;
  codecwrap_t *hss_cw;
  media_queue_t *hss_mq;
  int hss_data_type;
  char *hss_title;

} htsp_subscription_stream_t;




static void htsp_subscriptionStart(htsp_connection_t *hc, htsmsg_t *m);
static void htsp_subscriptionStop(htsp_connection_t *hc, htsmsg_t *m);
static void htsp_subscriptionStatus(htsp_connection_t *hc, htsmsg_t *m);
static void htsp_queueStatus(htsp_connection_t *hc, htsmsg_t *m);
static void htsp_mux_input(htsp_connection_t *hc, htsmsg_t *m);

static htsmsg_t *htsp_reqreply(htsp_connection_t *hc, htsmsg_t *m);



static htsmsg_t *
htsp_recv(htsp_connection_t *hc)
{
  void *buf;
  int fd = hc->hc_fd;
  uint8_t len[4];
  uint32_t l;

  if(tcp_read(fd, len, 4, 1) < 0)
    return NULL;
  
  l = (len[0] << 24) | (len[1] << 16) | (len[2] << 8) | len[3];
  buf = malloc(l);

  if(tcp_read(fd, buf, l, 1) < 0) {
    free(buf);
    return NULL;
  }
  
  return htsmsg_binary_deserialize(buf, l, buf); /* consumes 'buf' */
}


/**
 *
 */
static htsmsg_t *
htsp_reqreply(htsp_connection_t *hc, htsmsg_t *m)
{
  void *buf;
  size_t len;
  uint32_t seq;
  int r, fd = hc->hc_fd;
  uint32_t noaccess;
  htsmsg_t *reply;
  htsp_msg_t *hm = NULL;
  int retry = 0;
  char id[100];
  char *username;
  char *password;
  struct AVSHA1 *shactx = alloca(av_sha1_size);
  uint8_t d[20];

  /* Generate a sequence number for our message */
  seq = atomic_add(&hc->hc_seq_generator, 1);
  htsmsg_add_u32(m, "seq", seq);

 again:

  snprintf(id, sizeof(id), "htsp://%s:%d", hc->hc_hostname, hc->hc_port);

  r = keyring_lookup(id, &username, &password, NULL, !!retry,
		     "TV client", "Access denied");

  if(r == -1) {
    /* User rejected */
    return NULL;
  }

  if(r == 0) {
    /* Got auth credentials */
    htsmsg_delete_field(m, "username");
    htsmsg_delete_field(m, "digest");

    if(username != NULL) 
      htsmsg_add_str(m, "username", username);

    if(password != NULL) {
      av_sha1_init(shactx);
      av_sha1_update(shactx, (const uint8_t *)password, strlen(password));
      av_sha1_update(shactx, hc->hc_challenge, 32);
      av_sha1_final(shactx, d);
      htsmsg_add_bin(m, "digest", d, 20);
    }

    free(username);
    free(password);
  }

  
  


  if(htsmsg_binary_serialize(m, &buf, &len, -1) < 0) {
    htsmsg_destroy(m);
    return NULL;
  }

  if(hc->hc_is_async) {
    /* Async, set up a struct that will be signalled when we get a reply */

    hm = malloc(sizeof(htsp_msg_t));
    hm->hm_msg = NULL;
    hm->hm_seq = seq;
    hm->hm_error = 0;
    hts_mutex_lock(&hc->hc_rpc_mutex);
    TAILQ_INSERT_TAIL(&hc->hc_rpc_queue, hm, hm_link);
    hts_mutex_unlock(&hc->hc_rpc_mutex);
  }

  if(tcp_write(fd, buf, len)) {
    free(buf);
    htsmsg_destroy(m);
    
    if(hm != NULL) {
      hts_mutex_lock(&hc->hc_rpc_mutex);
      TAILQ_REMOVE(&hc->hc_rpc_queue, hm, hm_link);
      hts_mutex_unlock(&hc->hc_rpc_mutex);
      free(hm);
    }
    return NULL;
  }
  free(buf);


  if(hm != NULL) {
    hts_mutex_lock(&hc->hc_rpc_mutex);
    while(1) {
      if(hm->hm_error != 0) {
	r = hm->hm_error;
	TAILQ_REMOVE(&hc->hc_rpc_queue, hm, hm_link);
	hts_mutex_unlock(&hc->hc_rpc_mutex);
	free(hm);

	htsmsg_destroy(m);
	return NULL;
      }

      if(hm->hm_msg != NULL)
	break;

      hts_cond_wait(&hc->hc_rpc_cond, &hc->hc_rpc_mutex);
    }
      
    TAILQ_REMOVE(&hc->hc_rpc_queue, hm, hm_link);
    hts_mutex_unlock(&hc->hc_rpc_mutex);
    reply = hm->hm_msg;
    free(hm);

  } else {

    if((reply = htsp_recv(hc)) == NULL) {
      htsmsg_destroy(m);
      return NULL;
    }
  }

  
  if(!htsmsg_get_u32(reply, "noaccess", &noaccess) && noaccess) {
    retry++;
    goto again;
  }

  htsmsg_destroy(m); /* Destroy original message */
  return reply;
}


/**
 *
 */
static int
htsp_login(htsp_connection_t *hc)
{
  const void *ch;
  size_t chlen;
  htsmsg_t *m;

  m = htsmsg_create_map();
  htsmsg_add_str(m, "clientname", "HTS Showtime");
  htsmsg_add_u32(m, "htspversion", 1);
  htsmsg_add_str(m, "method", "hello");
  

  if((m = htsp_reqreply(hc, m)) == NULL) {
    return -1;
  }


  if(htsmsg_get_bin(m, "challenge", &ch, &chlen) || chlen != 32) {
    htsmsg_destroy(m);
    return -1;
  }
  memcpy(hc->hc_challenge, ch, 32);

  htsmsg_destroy(m);


  m = htsmsg_create_map();
  htsmsg_add_str(m, "method", "login");
  htsmsg_add_u32(m, "htspversion", HTSP_PROTO_VERSION);

  if((m = htsp_reqreply(hc, m)) == NULL) {
    return -1;
  }

  htsmsg_destroy(m);

  return 0;
}




/**
 *
 */
static void
htsp_channelAddUpdate(htsp_connection_t *hc, htsmsg_t *m, int create)
{
  uint32_t id;
  char txt[200];
  prop_t *p, *metadata;

  if(htsmsg_get_u32(m, "channelId", &id))
    return;

  snprintf(txt, sizeof(txt), "%d", id);

  p = prop_create(create ? NULL : hc->hc_prop_channels, txt);

  snprintf(txt, sizeof(txt), "htsp://%s:%d/channel/%d",
	   hc->hc_hostname, hc->hc_port, id);

  prop_set_string(prop_create(p, "url"), txt);

  metadata = prop_create(p, "metadata");

  prop_set_string(prop_create(p, "type"), "tvchannel");
  prop_set_string(prop_create(metadata, "icon"), 
		  htsmsg_get_str(m, "channelIcon"));
  prop_set_string(prop_create(metadata, "title"),
		  htsmsg_get_str(m, "channelName"));


  if(create && prop_set_parent(p, hc->hc_prop_channels))
    prop_destroy(p);
}


/**
 *
 */
static void
htsp_tagAddUpdate(htsp_connection_t *hc, htsmsg_t *m)
{
  const char *id;
  //  htsmsg_t *members;
  prop_t *p, *channels;

  if((id = htsmsg_get_str(m, "tagId")) == NULL)
    return;
  
  p = prop_create(hc->hc_prop_tags, id);

  prop_set_string(prop_create(p, "title"), htsmsg_get_str(m, "tagName"));
  prop_set_string(prop_create(p, "icon"), htsmsg_get_str(m, "tagIcon"));
  prop_set_int(prop_create(p, "titledIcon"),
	       htsmsg_get_u32_or_default(m, "tagTitledIcon", 0));

  channels = prop_create(p, "channels");
#if 0
  if((members = htsmsg_get_array(m, "members")) != NULL)
    htsp_tag_update_membership(tv, tt, members);
#endif
}



/**
 *
 * We keep another thread for dispatching unsolicited (asynchronous)
 * messages. Reason is that these messages may in turn cause additional
 * inqueries to the HTSP server and we don't want to block the main input
 * thread with this. Not to mention if the request needs to be retried
 * with new authorization credentials.
 *
 */
static void *
htsp_worker_thread(void *aux)
{
  htsp_connection_t *hc = aux;

  htsp_msg_t *hm;
  htsmsg_t *m;
  const char *method;


  while(1) {
    
    hts_mutex_lock(&hc->hc_worker_mutex);

    while((hm = TAILQ_FIRST(&hc->hc_worker_queue)) == NULL)
      hts_cond_wait(&hc->hc_worker_cond, &hc->hc_worker_mutex);

    TAILQ_REMOVE(&hc->hc_worker_queue, hm, hm_link);
    hts_mutex_unlock(&hc->hc_worker_mutex);

    m = hm->hm_msg;
    free(hm);

    if(m == NULL)
      break;

    if((method = htsmsg_get_str(m, "method")) != NULL) {

      if(!strcmp(method, "channelAdd"))
	htsp_channelAddUpdate(hc, m, 1);
      else if(!strcmp(method, "tagAdd"))
	htsp_tagAddUpdate(hc, m);
      else if(!strcmp(method, "subscriptionStart"))
	htsp_subscriptionStart(hc, m);
      else if(!strcmp(method, "subscriptionStop"))
	htsp_subscriptionStop(hc, m);
      else if(!strcmp(method, "subscriptionStatus"))
	htsp_subscriptionStatus(hc, m);
      else if(!strcmp(method, "queueStatus"))
	htsp_queueStatus(hc, m);
      else
	TRACE(TRACE_INFO, "HTSP", "Unknown async method '%s' received",
		method);
    }

    htsmsg_destroy(m);
  }
  return NULL;
}




/**
 *
 */
static void
htsp_msg_dispatch(htsp_connection_t *hc, htsmsg_t *m)
{
  uint32_t seq;
  htsp_msg_t *hm;
  const char *method;

  /**
   * Grab streaming input at once
   */
  if((method = htsmsg_get_str(m, "method")) != NULL &&
     !strcmp(method, "muxpkt")) {
    htsp_mux_input(hc, m);
    htsmsg_destroy(m);
    return;
  }

  if(!htsmsg_get_u32(m, "seq", &seq) && seq != 0) {
    /* Reply .. */

    hts_mutex_lock(&hc->hc_rpc_mutex);
    TAILQ_FOREACH(hm, &hc->hc_rpc_queue, hm_link)
      if(seq == hm->hm_seq)
	break;

    if(hm != NULL) {
      hm->hm_msg = m;
      hts_cond_broadcast(&hc->hc_rpc_cond);
      m = NULL;
    } else {
      printf("Warning, unmatched sequence\n");
      abort();
    }
    hts_mutex_unlock(&hc->hc_rpc_mutex);

    if(m != NULL)
      htsmsg_destroy(m);

    return;
  }

  /* Unsolicited meta message */
  /* Async updates are sent to another worker thread */

  hm = malloc(sizeof(htsp_msg_t));

  hm->hm_msg = m;

  hts_mutex_lock(&hc->hc_worker_mutex);
  TAILQ_INSERT_TAIL(&hc->hc_worker_queue, hm, hm_link);
  hts_cond_signal(&hc->hc_worker_cond);
  hts_mutex_unlock(&hc->hc_worker_mutex);
}


/**
 *
 */
static void *
htsp_thread(void *aux)
{
  htsp_connection_t *hc = aux;
  htsmsg_t *m;

  while(1) {

    m = htsmsg_create_map();

    htsmsg_add_str(m, "method", "enableAsyncMetadata");
    m = htsp_reqreply(hc, m);
    if(m == NULL) {
      return NULL;
    }
    htsmsg_destroy(m);

    hc->hc_is_async = 1;

    while(1) {

      if((m = htsp_recv(hc)) == NULL)
	break;
      htsp_msg_dispatch(hc, m);
    }
  }
  return NULL;
}



/**
 *
 */
static htsp_connection_t *
htsp_connection_find(const char *url, char *path, size_t pathlen,
		     char *errbuf, size_t errlen)
{
  htsp_connection_t *hc;
  int fd, port;
  char hostname[128];

  url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
	    path, pathlen, url);

  if(port < 0)
    port = 9982;

  hts_mutex_lock(&htsp_global_mutex);

  LIST_FOREACH(hc, &htsp_connections, hc_global_link) {
    if(!strcmp(hc->hc_hostname, hostname) && hc->hc_port == port) {
      hc->hc_refcount++;
      hts_mutex_unlock(&htsp_global_mutex);
      return hc;
    }
  }

  fd = tcp_connect(hostname, port, errbuf, errlen, 3000);
  if(fd == -1) {
    hts_mutex_unlock(&htsp_global_mutex);
    return NULL;
  }

  hc = calloc(1, sizeof(htsp_connection_t));

  hc->hc_prop_root = prop_create(NULL, NULL);
  hc->hc_prop_channels = prop_create(hc->hc_prop_root, "channels");
  hc->hc_prop_tags = prop_create(hc->hc_prop_root, "tags");
  
  hts_mutex_init(&hc->hc_rpc_mutex);
  hts_cond_init(&hc->hc_rpc_cond);
  TAILQ_INIT(&hc->hc_rpc_queue);

  hts_mutex_init(&hc->hc_worker_mutex);
  hts_cond_init(&hc->hc_worker_cond);
  TAILQ_INIT(&hc->hc_worker_queue);

  hc->hc_fd = fd;
  hc->hc_seq_generator = 1;
  hc->hc_sid_generator = 1;
  hc->hc_hostname = strdup(hostname);
  hc->hc_port = port;

  hc->hc_refcount = 1;

  LIST_INSERT_HEAD(&htsp_connections, hc, hc_global_link);

  htsp_login(hc);

  hts_thread_create_detached(htsp_thread, hc);
  hts_thread_create_detached(htsp_worker_thread, hc);

  hts_mutex_unlock(&htsp_global_mutex);
  return hc;
}

/**
 *
 */
static void
htsp_connection_deref(htsp_connection_t *hc)
{
  abort();
}


/**
 *
 */
static void
htsp_close_page(nav_page_t *np)
{
  
}


/**
 * XXX: Same as file_open_video()
 */
static int
htsp_open_channel(const char *url, nav_page_t **npp)
{
  nav_page_t *np;
  prop_t *p;

  np = nav_page_create(url, sizeof(nav_page_t), NULL, 0);

  p = np->np_prop_root;
  prop_set_string(prop_create(p, "type"), "video");
  *npp = np;
  return 0;
}


/**
 *
 */
static int
be_htsp_open(const char *url, nav_page_t **npp, char *errbuf, size_t errlen)
{
  htsp_connection_t *hc;
  htsp_page_t *hp;
  prop_t *p;
  char path[128];

  if((hc = htsp_connection_find(url, path, sizeof(path), 
				errbuf, errlen)) == NULL) {
    return -1;
  }

#if 0
  if(path[0] == 0) {
    hp = nav_page_create(url, sizeof(htsp_page_t), htsp_close_page,
			 NAV_PAGE_DONT_CLOSE_ON_BACK);
    p = hp->h.np_prop_root;
  
    prop_set_string(prop_create(p, "type"), "directory");
    prop_link(hc->hc_prop_tags, prop_create(p, "nodes"));

  } else if(!strncmp(path, "/tag/", 5)) {

    src = prop_get_by_name(hc->hc_prop_tags, 
			   (const char *[]){"self", path+5, "nodes", NULL});

    hp = nav_page_create(url, sizeof(htsp_page_t), htsp_close_page,
			 NAV_PAGE_DONT_CLOSE_ON_BACK);
    p = hp->h.np_prop_root;
  
    prop_set_string(prop_create(p, "type"), "directory");
    if(src != NULL)
      prop_link(src, prop_create(p, "nodes"));

  } else {

    snprintf(errbuf, errlen, "Invalid URL");
    return -1;
  }
#endif

  if(!strncmp(path, "/channel/", strlen("/channel/")))
    return htsp_open_channel(url, npp);

  hp = nav_page_create(url, sizeof(htsp_page_t), htsp_close_page,
		       NAV_PAGE_DONT_CLOSE_ON_BACK);
  p = hp->h.np_prop_root;
  
  prop_set_string(prop_create(p, "type"), "directory");
  prop_set_string(prop_create(p, "view"), "list");
  prop_link(hc->hc_prop_channels, prop_create(p, "nodes"));

  *npp = &hp->h;
  return 0;
}


/**
 *
 */
static event_t *
htsp_subscriber(htsp_connection_t *hc, htsp_subscription_t *hs, 
		int chid, char *errbuf, size_t errlen)
{
  event_t *e;
  htsmsg_t *m;
  media_pipe_t *mp = hs->hs_mp;
  const char *err;

  m = htsmsg_create_map();

  htsmsg_add_str(m, "method", "subscribe");
  htsmsg_add_u32(m, "channelId", chid);
  htsmsg_add_u32(m, "subscriptionId", hs->hs_sid);

  if((m = htsp_reqreply(hc, m)) == NULL) {
    snprintf(errbuf, errlen, "Connection with server lost");
    return NULL;
  }

  htsmsg_print(m);

  if((err = htsmsg_get_str(m, "error")) != NULL) {
    snprintf(errbuf, errlen, "From server: %s", err);
    htsmsg_destroy(m);
    return NULL;
  }

  htsmsg_destroy(m);

  do {
    e = mp_dequeue_event(mp);

    switch(e->e_type) {

    case EVENT_EXIT:
    case EVENT_PLAY_URL:
      break;

    default:
      event_unref(e);
      e = NULL;
      break;
    }
  } while(e == NULL);
  
  m = htsmsg_create_map();

  htsmsg_add_str(m, "method", "unsubscribe");
  htsmsg_add_u32(m, "subscriptionId", hs->hs_sid);

  if((m = htsp_reqreply(hc, m)) != NULL)
    htsmsg_destroy(m);

  return e;
}


/**
 *
 */
static void
htsp_free_streams(htsp_subscription_t *hs)
{
  htsp_subscription_stream_t *hss;
  
  while((hss = LIST_FIRST(&hs->hs_streams)) != NULL) {
    LIST_REMOVE(hss, hss_link);
    wrap_codec_deref(hss->hss_cw);
    free(hss->hss_title);
    free(hss);
  }
}


/**
 *
 */
static event_t *
be_htsp_playvideo(const char *url, media_pipe_t *mp,
		  char *errbuf, size_t errlen)
{
  htsp_connection_t *hc;
  char path[100];
  htsp_subscription_t *hs;
  event_t *e;

  if((hc = htsp_connection_find(url, path, sizeof(path), 
				errbuf, errlen)) == NULL) {
    return NULL;
  }

  if(strncmp(path, "/channel/", strlen("/channel/"))) {
    htsp_connection_deref(hc);
    snprintf(errbuf, errlen, "Invalid URL");
    return NULL;
  }

  hs = calloc(1, sizeof(htsp_subscription_t));

  hs->hs_sid = atomic_add(&hc->hc_sid_generator, 1);
  hs->hs_mp = mp;

  hts_mutex_lock(&hc->hc_subscription_mutex);
  LIST_INSERT_HEAD(&hc->hc_subscriptions, hs, hs_link);
  hts_mutex_unlock(&hc->hc_subscription_mutex);

  e = htsp_subscriber(hc, hs, atoi(path + strlen("/channel/")), 
		      errbuf, errlen);

  mp_hibernate(mp);

  hts_mutex_lock(&hc->hc_subscription_mutex);
  LIST_REMOVE(hs, hs_link);
  hts_mutex_unlock(&hc->hc_subscription_mutex);

  htsp_free_streams(hs);
  free(hs);
  return e;
}

/**
 * Leaves 'hc_subscription_mutex' locked if we successfully find a subscription
 */
static htsp_subscription_t *
htsp_find_subscription_by_msg(htsp_connection_t *hc, htsmsg_t *m)
{
  uint32_t sid;
  htsp_subscription_t *hs;

  if(htsmsg_get_u32(m, "subscriptionId", &sid))
    return NULL;

  hts_mutex_lock(&hc->hc_subscription_mutex);
  LIST_FOREACH(hs, &hc->hc_subscriptions, hs_link)
    if(hs->hs_sid == sid)
      return hs;

  hts_mutex_unlock(&hc->hc_subscription_mutex);
  return NULL;
}


/**
 * Transport input
 */
static void
htsp_mux_input(htsp_connection_t *hc, htsmsg_t *m)
{
  htsp_subscription_t *hs;
  htsp_subscription_stream_t *hss;
  uint32_t stream;
  media_pipe_t *mp;
  const void *bin;
  size_t binlen;
  media_buf_t *mb;

  if(htsmsg_get_u32(m, "stream", &stream)  ||
     htsmsg_get_bin(m, "payload", &bin, &binlen))
    return;
  
  if((hs = htsp_find_subscription_by_msg(hc, m)) == NULL)
    return;

  mp = hs->hs_mp;

  if(stream == mp->mp_audio.mq_stream || stream == mp->mp_video.mq_stream) {

    LIST_FOREACH(hss, &hs->hs_streams, hss_link)
      if(hss->hss_index == stream)
	break;
      
    if(hss != NULL) {

      mb = media_buf_alloc();
      mb->mb_data_type = hss->hss_data_type;
      mb->mb_stream = hss->hss_index;

      if(htsmsg_get_u32(m, "duration", &mb->mb_duration))
	mb->mb_duration = 0;

      if(htsmsg_get_s64(m, "dts", &mb->mb_dts))
	mb->mb_dts = AV_NOPTS_VALUE;

      if(htsmsg_get_s64(m, "pts", &mb->mb_pts))
	mb->mb_pts = AV_NOPTS_VALUE;

      mb->mb_cw = wrap_codec_ref(hss->hss_cw);

      mb->mb_data = malloc(binlen + FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(mb->mb_data, bin, binlen);
      memset(mb->mb_data + binlen, 0, FF_INPUT_BUFFER_PADDING_SIZE);
  
      mb->mb_size = binlen;

      if(mb_enqueue_no_block(mp, hss->hss_mq, mb))
	media_buf_free(mb);

    }
  }
  hts_mutex_unlock(&hc->hc_subscription_mutex);
}


/**
 *
 */
static void
htsp_subscriptionStart(htsp_connection_t *hc, htsmsg_t *m)
{
  media_pipe_t *mp;
  htsp_subscription_t *hs;
  htsmsg_field_t *f;
  htsmsg_t *sub, *streams;
  const char *type;
  uint32_t idx, s;
  enum CodecID   codec_id;
  enum CodecType codec_type;
  const char *nicename, *lang;
  codecwrap_t *cw;

  int vstream = -1; /* Initial video stream */
  int astream = -1; /* Initial audio stream */

  int ascore = 0;   /* Discriminator for chosing best stream */
  int vscore = 0;   /* Discriminator for chosing best stream */

  htsp_subscription_stream_t *hss;

  char buf[64];

  if((hs = htsp_find_subscription_by_msg(hc, m)) == NULL)
    return;

  mp = hs->hs_mp;

  /**
   * Parse each stream component and add it as a stream at our end
   */

  if((streams = htsmsg_get_list(m, "streams")) != NULL) {
    HTSMSG_FOREACH(f, streams) {
      if(f->hmf_type != HMF_MAP)
	continue;
      sub = &f->hmf_msg;

      if((type = htsmsg_get_str(sub, "type")) == NULL)
	continue;

      if(htsmsg_get_u32(sub, "index", &idx))
	continue;

      if(!strcmp(type, "AC3")) {
	codec_id = CODEC_ID_AC3;
	codec_type = CODEC_TYPE_AUDIO;
	nicename = "AC-3";
	s = 2;
      } else if(!strcmp(type, "MPEG2AUDIO")) {
	codec_id = CODEC_ID_MP2;
	codec_type = CODEC_TYPE_AUDIO;
	nicename = "MPEG";
	s = 1;
      } else if(!strcmp(type, "MPEG2VIDEO")) {
	codec_id = CODEC_ID_MPEG2VIDEO;
	codec_type = CODEC_TYPE_VIDEO;
	nicename = "MPEG-2";
	s = 1;
      } else if(!strcmp(type, "H264")) {
	codec_id = CODEC_ID_H264;
	codec_type = CODEC_TYPE_VIDEO;
	nicename = "H264";
	s = 2;
      } else {
	continue;
      }

      /**
       * Try to create the codec
       */

      cw = wrap_codec_create(codec_id, codec_type, 0, NULL, NULL,
			     codec_id == CODEC_ID_H264);
      if(cw == NULL)
	continue; /* We should print something i guess .. */
    
      hss = calloc(1, sizeof(htsp_subscription_stream_t));
      hss->hss_index = idx;
      hss->hss_cw = cw;

      switch(codec_type) {
      default:
	break;

      case CODEC_TYPE_VIDEO:
	hss->hss_mq = &mp->mp_video;
	hss->hss_data_type = MB_VIDEO;

	if(s > vscore) {
	  vscore = s;
	  vstream = idx;
	}
	break;

      case CODEC_TYPE_AUDIO:
	hss->hss_mq = &mp->mp_audio;
	hss->hss_data_type = MB_AUDIO;

	if(s > ascore) {
	  ascore = s;
	  astream = idx;
	}
	break;
      }

      lang = htsmsg_get_str(sub, "language");
      if(lang != NULL) {
	snprintf(buf, sizeof(buf), "%s (%s)", nicename, lang);
	hss->hss_title = strdup(buf);
      } else {
	hss->hss_title = strdup(nicename);
      }
      LIST_INSERT_HEAD(&hs->hs_streams, hss, hss_link);
    }
  }
  mp->mp_audio.mq_stream = astream;
  mp->mp_video.mq_stream = vstream;

  mp_prepare(mp, MP_GRAB_AUDIO);

  hts_mutex_unlock(&hc->hc_subscription_mutex);
}


/**
 *
 */
static void
htsp_subscriptionStop(htsp_connection_t *hc, htsmsg_t *m)
{
  htsp_subscription_t *hs;

  if((hs = htsp_find_subscription_by_msg(hc, m)) == NULL)
    return;

  htsp_free_streams(hs);
  hts_mutex_unlock(&hc->hc_subscription_mutex);
}


/**
 *
 */
static void
htsp_subscriptionStatus(htsp_connection_t *hc, htsmsg_t *m)
{
  htsp_subscription_t *hs;

  if((hs = htsp_find_subscription_by_msg(hc, m)) == NULL)
    return;

  hts_mutex_unlock(&hc->hc_subscription_mutex);
}


/**
 *
 */
static void
htsp_queueStatus(htsp_connection_t *hc, htsmsg_t *m)
{
  htsp_subscription_t *hs;
  uint32_t u32;
  uint32_t drops;

  if((hs = htsp_find_subscription_by_msg(hc, m)) == NULL)
    return;

  drops = 0;
  if(!htsmsg_get_u32(m, "Bdrops", &u32))
    drops += u32;
  if(!htsmsg_get_u32(m, "Pdrops", &u32))
    drops += u32;
  if(!htsmsg_get_u32(m, "Idrops", &u32))
    drops += u32;

  hts_mutex_unlock(&hc->hc_subscription_mutex);
}



/**
 *
 */
static int
be_htsp_canhandle(const char *url)
{
  return !strncmp(url, "htsp://", strlen("htsp://"));
}


/**
 *
 */
static int
htsp_init(void)
{
  hts_mutex_init(&htsp_global_mutex);
  return 0;
}



/**
 *
 */
nav_backend_t be_htsp = {
  .nb_init       = htsp_init,
  .nb_canhandle  = be_htsp_canhandle,
  .nb_open       = be_htsp_open,
  .nb_play_video = be_htsp_playvideo,
};
