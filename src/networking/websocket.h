/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */

#include "misc/prng.h"

struct htsbuf_queue;

typedef struct websocket_state {
  uint8_t opcode;
  int packet_size;
  void *packet;
  prng_t maskgen;
} websocket_state_t;

void websocket_append_hdr(struct htsbuf_queue *q, int opcode, size_t len,
                          const uint8_t *mask);

void websocket_append(struct htsbuf_queue *q, int opcode,
                      uint8_t *data, size_t len,
                      websocket_state_t *state);

/**
 * Return-values
 *  0 - Not enough data in input buffer, call again when more is available
 *  1 - Fatal error, disconnect
 */
int websocket_parse(struct htsbuf_queue *q,
                    int (*cb)(void *opaque, int opcode,
                              uint8_t *data, int len),
                    void *opaque, websocket_state_t *state);


#define WS_STATUS_ABNORMAL_CLOSE 1006
