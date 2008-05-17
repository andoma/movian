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
#include <pthread.h>

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



static void
htsp_fatal_error(htsp_connection_t *hc, const char *err)
{
  printf("%s\n", err);
  //  hc->hc_event_callback(hc->hc_opaque, HTSP_EVENT_FATAL_ERROR, (void *)err);
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
  unsigned int len;
  uint32_t l, seq;
  int r, fd = hc->hc_fd;
  uint32_t noaccess;
  htsmsg_t *reply;
  htsp_msg_t *hm = NULL;

  /* Generate a sequence number for our message */

  pthread_mutex_lock(&hc->hc_tally_lock);
  seq = ++hc->hc_seq_tally;
  pthread_mutex_unlock(&hc->hc_tally_lock);

  htsmsg_add_u32(m, "seq", seq);

  // again:

  if(htsmsg_binary_serialize(m, &buf, &len, -1) < 0) {
    htsmsg_destroy(m);
    return NULL;
  }

  if(async) {
    /* Async, set up a struct that will be signalled when we get a reply */

    hm = malloc(sizeof(htsp_msg_t));
    hm->hm_msg = NULL;
    hm->hm_seq = seq;
    pthread_mutex_lock(&hc->hc_rpc_lock);
    TAILQ_INSERT_TAIL(&hc->hc_rpc_queue, hm, hm_link);
    pthread_mutex_unlock(&hc->hc_rpc_lock);
  }

  if(write(fd, buf, len) < 0) {
    free(buf);
    htsmsg_destroy(m);
    
    if(hm != NULL) {
      pthread_mutex_lock(&hc->hc_rpc_lock);
      TAILQ_REMOVE(&hc->hc_rpc_queue, hm, hm_link);
      pthread_mutex_unlock(&hc->hc_rpc_lock);
      free(hm);
    }

    return NULL;
  }
  free(buf);


  if(hm != NULL) {
    pthread_mutex_lock(&hc->hc_rpc_lock);
    while(1) {
      if(hc->hc_connected == 0) {
	htsmsg_destroy(m);
	pthread_mutex_unlock(&hc->hc_rpc_lock);
	return NULL;
      }

      if(hm->hm_msg != NULL)
	break;

      pthread_cond_wait(&hc->hc_rpc_cond, &hc->hc_rpc_lock);
    }
      
    TAILQ_REMOVE(&hc->hc_rpc_queue, hm, hm_link);
    pthread_mutex_unlock(&hc->hc_rpc_lock);
    reply = hm->hm_msg;
    free(hm);

  } else {

    r = read(fd, &l, 4);
    if(r != 4) {
      htsmsg_destroy(m);
      return NULL;
    }

    l = ntohl(l);
    buf = malloc(l);

    if(read(fd, buf, l) != l) {
      free(buf);
      htsmsg_destroy(m);
      return NULL;
    }
  
    reply = htsmsg_binary_deserialize(buf, l, buf); /* uses 'buf' */
  }

  if(!htsmsg_get_u32(reply, "_noaccess", &noaccess) && noaccess) {
    /* Access denied, we need to try with new credentials */

    abort();  /* not supported right now */

  }


  htsmsg_destroy(m); /* Destroy original message */
  return reply;
}

/**
 *
 */
static void
htsp_channelGroupAdd(tv_t *tv, htsp_connection_t *hc, htsmsg_t *m)
{
  tv_ch_group_t *tcg;
  const char *group = htsmsg_get_str(m, "channelGroupName");
  if(group == NULL)
    return;

  pthread_mutex_lock(&tv->tv_ch_mutex);
  tcg = tv_channel_group_find(tv, group, 1);
  pthread_mutex_unlock(&tv->tv_ch_mutex);
}


/**
 * We display up to three current/upcoming events
 */
static void
htsp_load_current_events(htsp_connection_t *hc, tv_channel_t *ch, 
			 uint32_t event)
{
  htsmsg_t *m;
  int i;
  uint32_t start, stop;
  const char *title;

  for(i = 0; i < 3 && event != 0; i++) {

    m = htsmsg_create();

    htsmsg_add_str(m, "method", "eventInfo");
    htsmsg_add_u32(m, "tag", event);

    m = htsp_reqreply(hc, m, 1);
    if(m == NULL)
      break;

    if(htsmsg_get_u32(m, "next", &event))
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
htsp_channelAdd(tv_t *tv, htsp_connection_t *hc, htsmsg_t *m)
{
  tv_ch_group_t *tcg;
  tv_channel_t *ch;
  const char *group = htsmsg_get_str(m, "channelGroupName");
  const char *chan  = htsmsg_get_str(m, "channelName");
  const char *icon  = htsmsg_get_str(m, "channelIcon");
  uint32_t event;

  if(group == NULL || chan == NULL)
    return;

  pthread_mutex_lock(&tv->tv_ch_mutex);

  tcg = tv_channel_group_find(tv, group, 1);

  ch = tv_channel_find(tv, tcg, chan, 1);
  htsmsg_get_u32(m, "channelTag", &ch->ch_tag);

  if(htsmsg_get_u32(m, "currentEvent", &event))
    event = 0;

  pthread_mutex_unlock(&tv->tv_ch_mutex);

  tv_channel_set_icon(ch, icon);

  htsp_load_current_events(hc, ch, event);
}




/**
 *
 */
static void
htsp_channelUpdate(tv_t *tv, htsp_connection_t *hc, htsmsg_t *m)
{
  uint32_t tag;
  tv_channel_t *ch;
  uint32_t event;

  if(htsmsg_get_u32(m, "channelTag", &tag))
    return;

  if((ch = tv_channel_find_by_tag(tv, tag)) == NULL)
    return;

 if(htsmsg_get_u32(m, "currentEvent", &event))
    event = 0;

  htsp_load_current_events(hc, ch, event);
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

  pthread_mutex_lock(&hc->hc_worker_lock);

  while(hc->hc_connected) {
    
    while(hc->hc_connected && (hm = TAILQ_FIRST(&hc->hc_worker_queue)) == NULL)
      pthread_cond_wait(&hc->hc_worker_cond, &hc->hc_worker_lock);

    if(hm != NULL) 
      TAILQ_REMOVE(&hc->hc_worker_queue, hm, hm_link);
    else
      continue;

    pthread_mutex_unlock(&hc->hc_worker_lock);

    m = hm->hm_msg;
    free(hm);

    if((method = htsmsg_get_str(m, "method")) != NULL) {

      if(!strcmp(method, "channelGroupAdd")) {
	htsp_channelGroupAdd(tv, hc, m);
      } else if(!strcmp(method, "channelAdd")) {
	htsp_channelAdd(tv, hc, m);
      } else if(!strcmp(method, "channelUpdate")) {
	htsp_channelUpdate(tv, hc, m);
      }
    }

    htsmsg_destroy(m);
    pthread_mutex_lock(&hc->hc_worker_lock);
  }

  pthread_mutex_unlock(&hc->hc_worker_lock);
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

  if(!htsmsg_get_u32(m, "seq", &seq) && seq != 0) {
    /* Reply .. */

    pthread_mutex_lock(&hc->hc_rpc_lock);
    TAILQ_FOREACH(hm, &hc->hc_rpc_queue, hm_link)
      if(seq == hm->hm_seq)
	break;

    if(hm != NULL) {
      hm->hm_msg = m;
      pthread_cond_broadcast(&hc->hc_rpc_cond);
      m = NULL;
    } else {
      printf("Warning, unmatched sequence\n");
      abort();
    }
    pthread_mutex_unlock(&hc->hc_rpc_lock);

    if(m != NULL)
      htsmsg_destroy(m);

    return;
  }

  /* Unsolicited message */
  /* Async updates are sent to another worker thread */

  hm = malloc(sizeof(htsp_msg_t));

  hm->hm_msg = m;

  pthread_mutex_lock(&hc->hc_worker_lock);
  TAILQ_INSERT_TAIL(&hc->hc_worker_queue, hm, hm_link);
  pthread_cond_signal(&hc->hc_worker_cond);
  pthread_mutex_unlock(&hc->hc_worker_lock);
  

}


/**
 *
 */
static void *
htsp_com_thread(void *aux)
{
  htsp_connection_t *hc = aux;
  int fd = -1, r, slen;
  const char *errtxt;
  struct sockaddr_storage dst;
  htsmsg_t *m;
  uint32_t msglen;
  void *msgbuf;

  for(;;sleep(5)) {

    if(fd != -1) {
      close(fd);
      fd = -1;
    }

    r = htsp_resolve(hc->hc_hostname, &errtxt, &dst, hc->hc_port);
    if(r == -1) {
      htsp_fatal_error(hc, errtxt);
      continue;
    }
    
    fd = socket(dst.ss_family, SOCK_STREAM, 0);
    if(fd == -1) {
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

    r = connect(fd, (struct sockaddr *)&dst, slen);
    if(r == -1) {
      htsp_fatal_syserr(hc, errno);
      continue;
    }

    hc->hc_fd = fd;

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
    pthread_create(&hc->hc_worker_tid, NULL, htsp_worker_thread, hc);

    //    hc->hc_event_callback(hc->hc_opaque, HTSP_EVENT_CONNECTED, NULL);

    /* Receive loop */

    while(1) {
      if(read(fd, &msglen, 4) != 4) {
	htsp_fatal_syserr(hc, errno);
	break;
      }

      msglen = ntohl(msglen);
      msgbuf = malloc(msglen);
      
      if(read(fd, msgbuf, msglen) != msglen) {
	free(msgbuf);
	htsp_fatal_syserr(hc, errno);
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

    pthread_cond_broadcast(&hc->hc_rpc_cond);
    pthread_cond_broadcast(&hc->hc_worker_cond);
  
    printf("HTSP disconnected, waiting for worker_thread\n");
    pthread_join(hc->hc_worker_tid, NULL);

  }
}





/**
 *
 */
htsp_connection_t *
htsp_create(const char *url, tv_t *tv)
{
  char hname[100];
  int i, port;
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

  printf("Connecting to %s port %d\n", hname, port);

  hc = calloc(1, sizeof(htsp_connection_t));
  hc->hc_tv = tv;
  hc->hc_hostname = strdup(hname);
  hc->hc_port = port;


  pthread_mutex_init(&hc->hc_worker_lock, NULL);
  pthread_cond_init(&hc->hc_worker_cond, NULL);
  TAILQ_INIT(&hc->hc_worker_queue);

  pthread_mutex_init(&hc->hc_rpc_lock, NULL);
  pthread_cond_init(&hc->hc_rpc_cond, NULL);
  TAILQ_INIT(&hc->hc_rpc_queue);

  pthread_create(&hc->hc_com_tid,    NULL, htsp_com_thread, hc);
  return hc;
}


