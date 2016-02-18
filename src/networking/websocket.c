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

#include "main.h"
#include "htsmsg/htsbuf.h"
#include "websocket.h"
#include "misc/bytestream.h"

/**
 *
 */
void
websocket_append_hdr(htsbuf_queue_t *q, int opcode, size_t len)
{
  uint8_t hdr[14]; // max header length
  int hlen;
  hdr[0] = 0x80 | (opcode & 0xf);
  if(len <= 125) {
    hdr[1] = len;
    hlen = 2;
  } else if(len < 65536) {
    hdr[1] = 126;
    hdr[2] = len >> 8;
    hdr[3] = len;
    hlen = 4;
  } else {
    hdr[1] = 127;
    uint64_t u64 = len;
#if defined(__LITTLE_ENDIAN__)
    u64 = __builtin_bswap64(u64);
#endif
    memcpy(hdr + 2, &u64, sizeof(uint64_t));
    hlen = 10;
  }

  htsbuf_append(q, hdr, hlen);
}


/**
 *
 */
int
websocket_parse(htsbuf_queue_t *q,
                int (*cb)(void *opaque, int opcode, uint8_t *data, int len),
                void *opaque, websocket_state_t *ws)
{
  uint8_t hdr[14]; // max header length
  while(1) {
    int p = htsbuf_peek(q, &hdr, 14);
    const uint8_t *m;

    if(p < 2)
      return 0;
    uint8_t fin = hdr[0] & 0x80;
    int opcode  = hdr[0] & 0xf;
    int64_t len = hdr[1] & 0x7f;
    int hoff = 2;
    if(len == 126) {
      if(p < 4)
        return 0;
      len = hdr[2] << 8 | hdr[3];
      hoff = 4;
    } else if(len == 127) {
      if(p < 10)
        return 0;
      len = rd64_be(hdr + 2);
      hoff = 10;
    }

    if(hdr[1] & 0x80) {
      if(p < hoff + 4)
        return 0;
      m = hdr + hoff;

      hoff += 4;
    } else {
      m = NULL;
    }

    if(q->hq_size < hoff + len)
      return 0;

    htsbuf_drop(q, hoff);

    if(opcode & 0x8) {
      // Ctrl frame
      uint8_t *p = mymalloc(len);
      if(p == NULL)
        return 1;

      htsbuf_read(q, p, len);
      if(m != NULL) for(int i = 0; i < len; i++) p[i] ^= m[i&3];

      int err = cb(opaque, opcode, p, len);
      free(p);
      if(!err)
        continue;
      return 1;
    }

    ws->packet = myrealloc(ws->packet, ws->packet_size + len+1);
    if(ws->packet == NULL)
      return 1;

    uint8_t *d = ws->packet + ws->packet_size;
    d[len] = 0;
    htsbuf_read(q, d, len);

    if(m != NULL) for(int i = 0; i < len; i++) d[i] ^= m[i&3];

    if(opcode != 0)
      ws->opcode = opcode;

    ws->packet_size += len;

    if(!fin)
      continue;

    int err = cb(opaque, ws->opcode, ws->packet, ws->packet_size);
    ws->packet_size = 0;
    if(!err)
      continue;
    return 1;
  }
}


