/*
 *  HTSP Client
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

#define _GNU_SOURCE

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

#include "htsp.h"
#include "showtime.h"
#include "keyring.h"
#include "event.h"

static void htsp_mux_input(tv_t *tv, htsmsg_t *m);

static void htsp_subscriptionStart(tv_t *tv, htsmsg_t *m);

static void htsp_subscriptionStop(tv_t *tv, htsmsg_t *m);

static void htsp_subscriptionStatus(tv_t *tv, htsmsg_t *m);

static void htsp_queueStatus(tv_t *tv, htsmsg_t *m);

static int htsp_subscribe(void *opaque, tv_channel_t *ch, char *errbuf,
			  size_t errsize);

static void htsp_unsubscribe(void *opaque, tv_channel_t *ch);

static void
htsp_fatal_error(htsp_connection_t *hc, const char *err)
{
  tv_t *tv = hc->hc_tv;
  glw_prop_set_string(tv->tv_prop_backend_error, err);
}


static void
htsp_fatal_syserr(htsp_connection_t *hc, int err)
{
  htsp_fatal_error(hc, strerror(err));
}


/**
 * Resolver, FIXME: move somewere else
 */
static int
htsp_resolve(const char *hname, const char **errp, void *sa, int port)
{
  int res, herr;
  size_t hstbuflen;
  char *tmphstbuf;
  struct hostent hostbuf, *hp;
  const char *errtxt = NULL;
  struct sockaddr_in *s_in = (struct sockaddr_in *)sa;

  hstbuflen = 1024;
  tmphstbuf = malloc(hstbuflen);

  while((res = gethostbyname_r(hname, &hostbuf, tmphstbuf,
			       hstbuflen, &hp, &herr)) == ERANGE) {
    hstbuflen *= 2;
    tmphstbuf = realloc (tmphstbuf, hstbuflen);
  }
  
  if(res != 0) {
    errtxt = "resolver: internal error\n";
  } else if(herr != 0) {
    switch(herr) {
    case HOST_NOT_FOUND:
      errtxt = "The specified host is unknown";
      break;
    case NO_ADDRESS:
      errtxt = "The requested name is valid but does not have an IP address";
      break;
      
    case NO_RECOVERY:
      errtxt = "A non-recoverable name server error occurred";
      break;
      
    case TRY_AGAIN:
      errtxt = "A temporary error occurred on an authoritative name server";
      break;
    
    default:
      errtxt = "Unknown error";
      break;
    }
  } else if(hp == NULL) {
    errtxt = "resolver: internal error\n";
  } else {
    switch(hp->h_addrtype) {
      
    case AF_INET:
      memset(s_in, 0, sizeof(struct sockaddr_in));
      s_in->sin_family = AF_INET;
      s_in->sin_port = htons(port);
      memcpy(&s_in->sin_addr, hp->h_addr, 4);
      break;
    
    default:
      errtxt = "Unsupported address family";
      break;
    }
  }

  if(errtxt != NULL && errp != NULL)
    *errp = errtxt;

  free(tmphstbuf);

  return errtxt ? -1 : 0;
}


/**
 *
 */
static htsmsg_t *
htsp_reqreply(htsp_connection_t *hc, htsmsg_t *m, int async)
{
  void *buf;
  size_t len;
  uint32_t l, seq;
  int r, fd = hc->hc_fd;
  uint32_t noaccess;
  htsmsg_t *reply;
  htsp_msg_t *hm = NULL;
  int retry = 0;
  char *username;
  char *password;

  /* Generate a sequence number for our message */

  hts_mutex_lock(&hc->hc_tally_lock);
  seq = ++hc->hc_seq_tally;
  hts_mutex_unlock(&hc->hc_tally_lock);

  htsmsg_add_u32(m, "seq", seq);

 again:

  r = keyring_lookup(hc->hc_url, &username, &password, NULL, !!retry,
		     "TV client", "Access denied");
  if(r == -1) {
    /* User rejected */
    glw_event_enqueue(&hc->hc_tv->tv_ai->ai_geq, 
		      glw_event_create(EVENT_RECONFIGURE, 
				       sizeof(glw_event_t)));
    htsmsg_destroy(m);
    errno = EACCES;
    return NULL;
  }

  if(r == 0) {
    /* Got auth credentials */
    htsmsg_delete_field(m, "username");
    htsmsg_delete_field(m, "password");

    if(username != NULL) 
      htsmsg_add_str(m, "username", username);

    if(password != NULL) 
      htsmsg_add_str(m, "password", password);

    free(username);
    free(password);
  }


  if(htsmsg_binary_serialize(m, &buf, &len, -1) < 0) {
    htsmsg_destroy(m);
    errno = ENOMEM;
    return NULL;
  }

  if(async) {
    /* Async, set up a struct that will be signalled when we get a reply */

    hm = malloc(sizeof(htsp_msg_t));
    hm->hm_msg = NULL;
    hm->hm_seq = seq;
    hts_mutex_lock(&hc->hc_rpc_lock);
    TAILQ_INSERT_TAIL(&hc->hc_rpc_queue, hm, hm_link);
    hts_mutex_unlock(&hc->hc_rpc_lock);
  }

  if(write(fd, buf, len) < 0) {
    free(buf);
    htsmsg_destroy(m);
    
    if(hm != NULL) {
      hts_mutex_lock(&hc->hc_rpc_lock);
      TAILQ_REMOVE(&hc->hc_rpc_queue, hm, hm_link);
      hts_mutex_unlock(&hc->hc_rpc_lock);
      free(hm);
    }
    errno = EIO;
    return NULL;
  }
  free(buf);


  if(hm != NULL) {
    hts_mutex_lock(&hc->hc_rpc_lock);
    while(1) {
      if(hc->hc_connected == 0) {
	htsmsg_destroy(m);
	hts_mutex_unlock(&hc->hc_rpc_lock);
	errno = ECONNRESET;
	return NULL;
      }

      if(hm->hm_msg != NULL)
	break;

      hts_cond_wait(&hc->hc_rpc_cond, &hc->hc_rpc_lock);
    }
      
    TAILQ_REMOVE(&hc->hc_rpc_queue, hm, hm_link);
    hts_mutex_unlock(&hc->hc_rpc_lock);
    reply = hm->hm_msg;
    free(hm);

  } else {

    r = recv(fd, &l, 4, MSG_WAITALL);
    if(r != 4) {
      htsmsg_destroy(m);
      return NULL;
    }

    l = ntohl(l);
    buf = malloc(l);

    if(recv(fd, buf, l, MSG_WAITALL) != l) {
      free(buf);
      htsmsg_destroy(m);
      errno = ECONNRESET;
      return NULL;
    }
  
    reply = htsmsg_binary_deserialize(buf, l, buf); /* uses 'buf' */
  }

  if(!htsmsg_get_u32(reply, "_noaccess", &noaccess) && noaccess) {
    /* Access denied, we need to try with new credentials */
    htsmsg_destroy(reply);
    retry++;
    goto again;
  }

  htsmsg_destroy(m); /* Destroy original message */
  return reply;
}

/**
 *
 */
static void
htsp_tag_update_membership(tv_t *tv, tv_tag_t *tt, htsmsg_t *m)
{
  htsmsg_field_t *f;
  tv_channel_t *ch;

  tv_tag_mark_ctms(tt);

  HTSMSG_FOREACH(f, m) {
    if(f->hmf_type == HMF_S64 &&
       (ch = tv_channel_find(tv, f->hmf_s64, 0)) != NULL)
      tv_tag_map_channel(tv, tt, ch);
  }

  tv_tag_delete_marked_ctms(tt);
}

/**
 *
 */
static void
htsp_tagAddUpdate(tv_t *tv, htsp_connection_t *hc, htsmsg_t *m, int add)
{
  tv_tag_t *tt;
  htsmsg_t *members;
  const char *id;
  uint32_t u32;

  if((id = htsmsg_get_str(m, "tagId")) == NULL)
    return;

  hts_mutex_lock(&tv->tv_ch_mutex);
  if((tt = tv_tag_find(tv, id, add)) != NULL) {
    tv_tag_set_title(tt, htsmsg_get_str(m, "tagName"));
    tv_tag_set_icon(tt, htsmsg_get_str(m, "tagIcon"));

    if(!htsmsg_get_u32(m, "tagTitledIcon", &u32))
	tv_tag_set_titled_icon(tt, u32);

    if((members = htsmsg_get_array(m, "members")) != NULL)
      htsp_tag_update_membership(tv, tt, members);
  }

  hts_mutex_unlock(&tv->tv_ch_mutex);
}


/**
 *
 */
static void
htsp_tagDelete(tv_t *tv, htsp_connection_t *hc, htsmsg_t *m)
{
  tv_tag_t *tt;
  const char *id;

  if((id = htsmsg_get_str(m, "tagId")) == NULL)
    return;

  hts_mutex_lock(&tv->tv_ch_mutex);
  if((tt = tv_tag_find(tv, id, 0)) != NULL)
    tv_tag_destroy(tv, tt);

  hts_mutex_unlock(&tv->tv_ch_mutex);
}

/**
 * We display up to three current/upcoming events
 */
static void
htsp_load_current_events(htsp_connection_t *hc, tv_channel_t *ch, 
			 uint32_t event)
{
  htsmsg_t *m;
  int i = 0;
  uint32_t start, stop;
  const char *title;

  if(event != 0) for(i = 0; i < 3 && event != 0; i++) {

    m = htsmsg_create();

    htsmsg_add_str(m, "method", "getEvent");
    htsmsg_add_u32(m, "eventId", event);


    m = htsp_reqreply(hc, m, 1);
    if(m == NULL)
      break;

    if(htsmsg_get_u32(m, "nextEventId", &event))
      event = 0;

    title = htsmsg_get_str(m, "title");
    
    if(htsmsg_get_u32(m, "start", &start))
      start = 0;

    if(htsmsg_get_u32(m, "stop", &stop))
      stop = 0;

    if(title != NULL && start != 0 && stop != 0)
      tv_channel_set_current_event(ch, i, title, start, stop);

    htsmsg_destroy(m);  
  }

  for(; i < 3; i++)
    tv_channel_set_current_event(ch, i, NULL, 0, 0);
}



/**
 *
 */
static void
htsp_channelAddUpdate(tv_t *tv, htsp_connection_t *hc, htsmsg_t *m, int add)
{
  tv_channel_t *ch;
  uint32_t id, eventId;

  if(htsmsg_get_u32(m, "channelId", &id))
    return;

  hts_mutex_lock(&tv->tv_ch_mutex);

  if((ch = tv_channel_find(tv, id, add)) != NULL) {
    tv_channel_set_title(ch, htsmsg_get_str(m, "channelName"));
    tv_channel_set_icon(ch, htsmsg_get_str(m, "channelIcon"));
    
    if(!htsmsg_get_u32(m, "eventId", &eventId))
      htsp_load_current_events(hc, ch, eventId);
  }

  hts_mutex_unlock(&tv->tv_ch_mutex);
}

/**
 *
 */
static void
htsp_channelDelete(tv_t *tv, htsp_connection_t *hc, htsmsg_t *m)
{
  tv_channel_t *ch;
  uint32_t id;

  if(htsmsg_get_u32(m, "channelId", &id))
    return;

  hts_mutex_lock(&tv->tv_ch_mutex);

  if((ch = tv_channel_find(tv, id, 0)) != NULL)
    tv_channel_destroy(tv, ch);

  hts_mutex_unlock(&tv->tv_ch_mutex);
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
  tv_t *tv = hc->hc_tv;

  hts_mutex_lock(&hc->hc_worker_lock);

  while(hc->hc_connected) {
    
    while(hc->hc_connected && (hm = TAILQ_FIRST(&hc->hc_worker_queue)) == NULL)
      hts_cond_wait(&hc->hc_worker_cond, &hc->hc_worker_lock);

    if(hm != NULL) {
      TAILQ_REMOVE(&hc->hc_worker_queue, hm, hm_link);
    } else {
      continue;
    }

    hts_mutex_unlock(&hc->hc_worker_lock);

    m = hm->hm_msg;
    free(hm);

    if((method = htsmsg_get_str(m, "method")) != NULL) {

      if(!strcmp(method, "tagAdd")) {
	htsp_tagAddUpdate(tv, hc, m, 1);
      } else if(!strcmp(method, "tagUpdate")) {
	htsp_tagAddUpdate(tv, hc, m, 0);
      } else if(!strcmp(method, "tagDelete")) {
	htsp_tagDelete(tv, hc, m);
      } else if(!strcmp(method, "channelAdd")) {
	htsp_channelAddUpdate(tv, hc, m, 1);
      } else if(!strcmp(method, "channelUpdate")) {
	htsp_channelAddUpdate(tv, hc, m, 0);
      } else if(!strcmp(method, "channelDelete")) {
	htsp_channelDelete(tv, hc, m);
      } else if(!strcmp(method, "subscriptionStart")) {
	htsp_subscriptionStart(tv, m);
      } else if(!strcmp(method, "subscriptionStop")) {
	htsp_subscriptionStop(tv, m);
      } else if(!strcmp(method, "subscriptionStatus")) {
	htsp_subscriptionStatus(tv, m);
      } else if(!strcmp(method, "queueStatus")) {
	htsp_queueStatus(tv, m);
      } else {
	fprintf(stderr, "HTSP: Unknown async method '%s' received\n",
		method);
      }
    }

    htsmsg_destroy(m);
    hts_mutex_lock(&hc->hc_worker_lock);
  }

  hts_mutex_unlock(&hc->hc_worker_lock);
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
    htsp_mux_input(hc->hc_tv, m);
    htsmsg_destroy(m);
    return;
  }

  if(!htsmsg_get_u32(m, "seq", &seq) && seq != 0) {
    /* Reply .. */

    hts_mutex_lock(&hc->hc_rpc_lock);
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
    hts_mutex_unlock(&hc->hc_rpc_lock);

    if(m != NULL)
      htsmsg_destroy(m);

    return;
  }

  /* Unsolicited meta message */
  /* Async updates are sent to another worker thread */

  hm = malloc(sizeof(htsp_msg_t));

  hm->hm_msg = m;

  hts_mutex_lock(&hc->hc_worker_lock);
  TAILQ_INSERT_TAIL(&hc->hc_worker_queue, hm, hm_link);
  hts_cond_signal(&hc->hc_worker_cond);
  hts_mutex_unlock(&hc->hc_worker_lock);
}


/**
 *
 */
static void *
htsp_com_thread(void *aux)
{
  htsp_connection_t *hc = aux;
  tv_t *tv = hc->hc_tv;
  int r, slen;
  const char *errtxt;
  struct sockaddr_storage dst;
  htsmsg_t *m;
  uint32_t msglen;
  void *msgbuf;
  hc->hc_fd = -1;

  tv->tv_be_opaque = hc;

  for(;;sleep(1)) {

    if(hc->hc_run == 0)
      break;

    if(hc->hc_fd != -1) {
      close(hc->hc_fd);
      hc->hc_fd = -1;
    }

    r = htsp_resolve(hc->hc_hostname, &errtxt, &dst, hc->hc_port);
    if(r == -1) {
      htsp_fatal_error(hc, errtxt);
      continue;
    }
    
    hc->hc_fd = socket(dst.ss_family, SOCK_STREAM, 0);
    if(hc->hc_fd == -1) {
      htsp_fatal_error(hc, "Can not create socket");
      continue;
    }

    switch(dst.ss_family) {
    case AF_INET:
      slen = sizeof(struct sockaddr_in);
      break;
    default:
      continue;
    }

    r = connect(hc->hc_fd, (struct sockaddr *)&dst, slen);
    if(r == -1) {
      htsp_fatal_syserr(hc, errno);
      continue;
    }

    if(hc->hc_run == 0)
      break;

    htsp_fatal_error(hc, NULL);  /* Reset any pending fatal error */
 
    /* Ok, connected, now, switch session into async mode */

    m = htsmsg_create();
    htsmsg_add_str(m, "method", "async");
    m = htsp_reqreply(hc, m, 0);
    if(m == NULL) {
      htsp_fatal_syserr(hc, errno);
      continue;
    }
    htsmsg_destroy(m);

    hc->hc_connected = 1;
    hts_thread_create(&hc->hc_worker_tid, htsp_worker_thread, hc);

   tv->tv_be_subscribe   = htsp_subscribe;
    tv->tv_be_unsubscribe = htsp_unsubscribe;
    
    /* Receive loop */

    while(hc->hc_run) {
      if(recv(hc->hc_fd, &msglen, 4, MSG_WAITALL) != 4) {
	htsp_fatal_error(hc, "Read error");
	break;
      }

      msglen = ntohl(msglen);
      msgbuf = malloc(msglen);
      
      if(recv(hc->hc_fd, msgbuf, msglen, MSG_WAITALL) != msglen) {
	free(msgbuf);
	htsp_fatal_error(hc, "Read error");
	break;
      }
      
      m = htsmsg_binary_deserialize(msgbuf, msglen, msgbuf);
      if(m == NULL) {
	htsp_fatal_error(hc, "Protocol corruption");
	free(msgbuf);
	break;
      }
      htsp_msg_dispatch(hc, m);
    }

    hc->hc_connected = 0;
    hc->hc_tv->tv_be_subscribe   = NULL;
    hc->hc_tv->tv_be_unsubscribe = NULL;

    hts_cond_broadcast(&hc->hc_rpc_cond);
    hts_cond_broadcast(&hc->hc_worker_cond);
  
    hts_thread_join(hc->hc_worker_tid);

    hts_mutex_lock(&tv->tv_ch_mutex);
    tv_remove_all(tv);
    hts_mutex_unlock(&tv->tv_ch_mutex);

    if(hc->hc_run == 0)
      break;
  }

  if(hc->hc_fd != -1) 
    close(hc->hc_fd);

  return NULL;
}





/**
 *
 */
htsp_connection_t *
htsp_create(const char *url, tv_t *tv)
{
  char hname[100];
  int i, port;
  const char *url0 = url;
  htsp_connection_t *hc;

  if(strncmp("htsp://", url, 7))
    return NULL;

  url += 7;
  i = 0;
  while(1) {
    if(*url < 32)
      return NULL;
    if(*url == ':')
      break;
    hname[i++] = *url++;
    if(i == sizeof(hname) - 1)
      return NULL;
  }
  url++; /* skip past ':' */

  hname[i] = 0;
  port = atoi(url);

  hc = calloc(1, sizeof(htsp_connection_t));
  hc->hc_url = strdup(url0);  /* Copy original URL name for
				 authentication purposes */
  hc->hc_run = 1;
  hc->hc_tv = tv;
  hc->hc_hostname = strdup(hname);
  hc->hc_port = port;


  hts_mutex_init(&hc->hc_worker_lock);
  hts_cond_init(&hc->hc_worker_cond);
  TAILQ_INIT(&hc->hc_worker_queue);

  hts_mutex_init(&hc->hc_rpc_lock);
  hts_cond_init(&hc->hc_rpc_cond);
  TAILQ_INIT(&hc->hc_rpc_queue);

  hts_thread_create(&hc->hc_com_tid, htsp_com_thread, hc);
  return hc;
}


/**
 *
 */
void
htsp_destroy(htsp_connection_t *hc)
{
  hc->hc_run = 0;
  if(hc->hc_fd != -1)
    shutdown(hc->hc_fd, SHUT_RDWR);

  hts_thread_join(hc->hc_com_tid);
  free(hc->hc_hostname);
  free(hc->hc_url);
  free(hc);
}





/**
 * Transport input
 */
static void
htsp_mux_input(tv_t *tv, htsmsg_t *m)
{
  uint32_t chid, stream;
  tv_channel_stream_t *tcs;
  tv_channel_t *ch;
  media_pipe_t *mp;
  const void *bin;
  size_t binlen;
  media_buf_t *mb;
  int r;

  if(htsmsg_get_u32(m, "channelId", &chid) ||
     htsmsg_get_u32(m, "stream", &stream)  ||
     htsmsg_get_bin(m, "payload", &bin, &binlen))
    return;
  
  pthread_mutex_lock(&tv->tv_running_mutex);
  
  LIST_FOREACH(ch, &tv->tv_running_channels, ch_running_link) 
    if(ch->ch_id == chid)
      break;

  if(ch != NULL) {
    mp = ch->ch_mp;

    if(stream == mp->mp_audio.mq_stream || stream == mp->mp_video.mq_stream) {

      LIST_FOREACH(tcs, &ch->ch_streams, tcs_link)
	if(tcs->tcs_index == stream)
	  break;
      
      if(tcs != NULL) {

	mb = media_buf_alloc();
	mb->mb_data_type = tcs->tcs_data_type;
	mb->mb_stream = tcs->tcs_index;

	if(htsmsg_get_u32(m, "duration", &mb->mb_duration))
	  mb->mb_duration = 0;

	if(htsmsg_get_s64(m, "dts", &mb->mb_dts))
	  mb->mb_dts = AV_NOPTS_VALUE;

	if(htsmsg_get_s64(m, "pts", &mb->mb_pts))
	  mb->mb_pts = AV_NOPTS_VALUE;

	mb->mb_cw = wrap_codec_ref(tcs->tcs_cw);

	mb->mb_data = malloc(binlen + FF_INPUT_BUFFER_PADDING_SIZE);
	memcpy(mb->mb_data, bin, binlen);
	memset(mb->mb_data + binlen, 0, FF_INPUT_BUFFER_PADDING_SIZE);
  
	mb->mb_size = binlen;

	avgstat_add(&ch->ch_avg_bitrate, binlen, walltime);

	if(mb_enqueue_no_block(mp, tcs->tcs_mq, mb)) {
	  media_buf_free(mb);
	}
      }
    }

    r = avgstat_read_and_expire(&ch->ch_avg_bitrate, walltime) / 10;
    glw_prop_set_int(ch->ch_prop_sub_bitrate, r / 1000 * 8);
    
  }
  pthread_mutex_unlock(&tv->tv_running_mutex);
}


/**
 *
 */
static tv_channel_t *
htsp_obtain_channel_by_msg(tv_t *tv, htsmsg_t *m)
{
  uint32_t id;
  tv_channel_t *ch;
  
  if(htsmsg_get_u32(m, "channelId", &id))
    return NULL;
  
  hts_mutex_lock(&tv->tv_ch_mutex);
  if((ch = tv_channel_find(tv, id, 0)) == NULL) {
    hts_mutex_unlock(&tv->tv_ch_mutex);
    return NULL;
  }
  return ch;
}


/**
 * Subscription started at headend
 */
static void
htsp_subscriptionStart(tv_t *tv, htsmsg_t *m)
{
  tv_channel_t *ch;
  htsmsg_field_t *f;
  htsmsg_t *sub, *streams;
  const char *type;
  uint32_t index, s;
  codecwrap_t *cw;
  enum CodecID   codec_id;
  enum CodecType codec_type;
  tv_channel_stream_t *tcs;

  int vstream = -1; /* Initial video stream */
  int astream = -1; /* Initial audio stream */

  int ascore = 0;   /* Discriminator for chosing best stream */
  int vscore = 0;   /* Discriminator for chosing best stream */

  media_pipe_t *mp;
  char buf[40];
  const char *nicename, *lang;

  if((ch = htsp_obtain_channel_by_msg(tv, m)) == NULL)
    return;

  if(ch->ch_subscribed == 0) {
    /* We may receive a subscriptionStart for a channel if we've manage
       to unsubscribe while our subscription request was on it's way
       to tvheadend and back.

       If this ever happens we are sure to just sent away a subscriptionStop
       message. This will make sure the headend actually stop.

       All in all, we can just drop out without doing anything.
    */

    hts_mutex_unlock(&tv->tv_ch_mutex);
    return;
  }

  if(ch->ch_running) {
    fprintf(stderr, "Warning, Ignoring subscriptionStart on already "
	    "running subscription, is tvheadend buggy?\n");
    hts_mutex_unlock(&tv->tv_ch_mutex);
    return;
  }
  
  mp = ch->ch_mp;
  ch->ch_fw = wrap_format_create(NULL);

  /**
   * Parse each stream component and add it as a stream at our end
   */

  if((streams = htsmsg_get_array(m, "streams")) != NULL) {
    HTSMSG_FOREACH(f, streams) {
      if(f->hmf_type != HMF_MSG)
	continue;
      sub = &f->hmf_msg;

      if((type = htsmsg_get_str(sub, "type")) == NULL)
	continue;

      if(htsmsg_get_u32(sub, "index", &index))
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

      cw = wrap_codec_create(codec_id, codec_type, 0, ch->ch_fw, NULL,
			     codec_id == CODEC_ID_H264);
      if(cw == NULL)
	continue; /* We should print something i guess .. */
    
      tcs = calloc(1, sizeof(tv_channel_stream_t));
      tcs->tcs_index = index;
      tcs->tcs_cw = cw;

      switch(codec_type) {
      default:
	break;

      case CODEC_TYPE_VIDEO:
	tcs->tcs_mq = &mp->mp_video;
	tcs->tcs_data_type = MB_VIDEO;

	if(s > vscore) {
	  vscore = s;
	  vstream = index;
	}
	break;

      case CODEC_TYPE_AUDIO:
	tcs->tcs_mq = &mp->mp_audio;
	tcs->tcs_data_type = MB_AUDIO;

	if(s > ascore) {
	  ascore = s;
	  astream = index;
	}
	break;
      }

      lang = htsmsg_get_str(sub, "language");
      if(lang != NULL) {
	snprintf(buf, sizeof(buf), "%s (%s)", nicename, lang);
	tcs->tcs_title = strdup(buf);
      } else {
	tcs->tcs_title = strdup(nicename);
      }

      LIST_INSERT_HEAD(&ch->ch_streams, tcs, tcs_link);
    }
  }

  mp->mp_audio.mq_stream = astream;
  mp->mp_video.mq_stream = vstream;

  mp_set_playstatus(mp, MP_PLAY, ch->ch_playstatus_start_flags);
  
  ch->ch_running = 1;

  pthread_mutex_lock(&tv->tv_running_mutex);
  LIST_INSERT_HEAD(&tv->tv_running_channels, ch, ch_running_link);
  pthread_mutex_unlock(&tv->tv_running_mutex);

  hts_mutex_unlock(&tv->tv_ch_mutex);
}



/**
 * Subscription started at headend
 */
static void
htsp_subscriptionStatus(tv_t *tv, htsmsg_t *m)
{
  tv_channel_t *ch;

  if((ch = htsp_obtain_channel_by_msg(tv, m)) == NULL)
    return;
  glw_prop_set_string(ch->ch_prop_sub_status, htsmsg_get_str(m, "status"));
  hts_mutex_unlock(&tv->tv_ch_mutex);
}




/**
 * Subscription started at headend
 */
static void
htsp_queueStatus(tv_t *tv, htsmsg_t *m)
{
  tv_channel_t *ch;
  uint32_t u32;
  uint32_t drops;

  if((ch = htsp_obtain_channel_by_msg(tv, m)) == NULL)
    return;
  
  drops = 0;
  if(!htsmsg_get_u32(m, "Bdrops", &u32))
    drops += u32;
  if(!htsmsg_get_u32(m, "Pdrops", &u32))
    drops += u32;
  if(!htsmsg_get_u32(m, "Idrops", &u32))
    drops += u32;

  glw_prop_set_int(ch->ch_prop_sub_backend_queuedrops, drops);

  if(!htsmsg_get_u32(m, "bytes", &u32))
    glw_prop_set_int(ch->ch_prop_sub_backend_queuesize, u32 / 1000);

  if(!htsmsg_get_u32(m, "delay", &u32))
    glw_prop_set_int(ch->ch_prop_sub_backend_queuedelay, u32 / 1000);

  hts_mutex_unlock(&tv->tv_ch_mutex);
}


/**
 * Subscription stopped at headend
 */
static void
htsp_subscriptionStop(tv_t *tv, htsmsg_t *m)
{
  tv_channel_t *ch;

  if((ch = htsp_obtain_channel_by_msg(tv, m)) == NULL)
    return;

  tv_channel_stop(tv, ch);

  glw_prop_set_string(ch->ch_prop_sub_status, htsmsg_get_str(m, "status"));

  hts_mutex_unlock(&tv->tv_ch_mutex);
}



/**
 *
 */
static int
htsp_subscribe(void *opaque, tv_channel_t *ch, char *errbuf, size_t errsize)
{
  htsmsg_t *m = htsmsg_create();
  const char *err;
  int r = 0;

  htsmsg_add_str(m, "method", "subscribe");
  htsmsg_add_u32(m, "channelId", ch->ch_id);

  m = htsp_reqreply(opaque, m, 1);

  if(m == NULL) {
    snprintf(errbuf, errsize, "Connection with server lost");
    return -1;
  }

  if((err = htsmsg_get_str(m, "_error")) != NULL) {
    snprintf(errbuf, errsize, "Server error: %s", err);
    r = -1;
  }

  htsmsg_destroy(m);
  return -1;
}


/**
 *
 */
static void
htsp_unsubscribe(void *opaque, tv_channel_t *ch)
{
  htsmsg_t *m = htsmsg_create();

  htsmsg_add_u32(m, "channelId", ch->ch_id);
  htsmsg_add_str(m, "method", "unsubscribe");

  m = htsp_reqreply(opaque, m, 1);
  if(m != NULL)
    htsmsg_destroy(m);

  return;
}
