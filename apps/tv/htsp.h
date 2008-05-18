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

#ifndef HTSP_H_
#define HTSP_H_


#include <libhts/htsmsg.h>
#include <libhts/htsmsg_binary.h>

#include "tv.h"

TAILQ_HEAD(htsp_msg_queue, htsp_msg);
LIST_HEAD(htsp_mux_list, htsp_mux);

/**
 *
 */
typedef struct htsp_msg {
  htsmsg_t *hm_msg;	
  uint32_t hm_seq;
  TAILQ_ENTRY(htsp_msg) hm_link;
} htsp_msg_t;


/**
 *
 */
typedef struct htsp_connection {

  int hc_seq_tally;
  pthread_mutex_t hc_tally_lock;

  int hc_connected;

  char *hc_hostname;
  int hc_port;

  pthread_t hc_com_tid;
  pthread_t hc_worker_tid;

  int hc_fd;

  struct tv *hc_tv;

  pthread_mutex_t hc_worker_lock;
  pthread_cond_t hc_worker_cond;
  struct htsp_msg_queue hc_worker_queue;

  pthread_mutex_t hc_rpc_lock;
  pthread_cond_t hc_rpc_cond;
  struct htsp_msg_queue hc_rpc_queue;

  struct htsp_mux_list hc_muxes;

} htsp_connection_t;

htsp_connection_t *htsp_create(const char *url, struct tv *tv);

int htsp_subscribe(htsp_connection_t *hc, tv_channel_t *ch);

#endif /* HTSP_H_ */
