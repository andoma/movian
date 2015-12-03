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
#include <stdio.h>
#include <limits.h>

#include <libavutil/mem.h>

#include "main.h"
#include "fileaccess/fileaccess.h"
#include "vobsub.h"
#include "misc/isolang.h"
#include "media/media.h"
#include "htsmsg/htsmsg_json.h"
#include "ext_subtitles.h"

/**
 *
 */
void
vobsub_decode_palette(uint32_t *clut, const char *str)
{
  char *end;
  int i = 0;
  while(*str && i < 16) {
    int v = strtol(str, &end, 16);
    clut[i++] = (v & 0xff0000) >> 16 | (v & 0xff00) | (v & 0xff) << 16;
    str = end;
    while(*str == ' ')
      str++;
    if(*str == ',')
      str++;
  }
}


/**
 *
 */
void
vobsub_decode_size(int *width, int *height, const char *str)
{
  int w = atoi(str);
  if((str = strchr(str, 'x')) == NULL)
    return;
  str++;
  int h = atoi(str);
  if(w > 0 && h > 0) {
    *width = w;
    *height = h;
  }
}


/**
 *
 */
void
vobsub_probe(const char *url, const char *filename,
	     int score, struct prop *prop, const char *subfile,
             int autosel)
{
  buf_t *b;
  char errbuf[256];
  struct fa_stat st;

  if(subfile == NULL) {
    char *sf = mystrdupa(url);
    subfile = sf;
    sf = strrchr(sf, '.');
    if(sf == NULL || strlen(sf) != 4)
      return;
    strcpy(sf, ".sub");
  }

  if(fa_stat(subfile, &st, errbuf, sizeof(errbuf))) {
    TRACE(TRACE_ERROR, "VOBSUB", "Unable to stat sub file: %s -- %s",
	  subfile, errbuf);
    return;
  }

  b = fa_load(url,
               FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
               FA_LOAD_CACHE_CONTROL(DISABLE_CACHE),
               NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "VOBSUB", "Unable to load %s -- %s", url, errbuf);
    return;
  }
  b = buf_make_writable(b);
  char *s = buf_str(b);
  int l;
  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    const char *p;
    s[l] = 0;
    if((p = mystrbegins(s, "id:")) != NULL) {
      while(*p == ' ')
	p++;
      if(strlen(p) >= 2) {
	const char *lang = p;

	htsmsg_t *m = htsmsg_create_map();
	htsmsg_add_str(m, "idx", url);
	htsmsg_add_str(m, "sub", subfile);

	if((p = strstr(p, "index:")) != NULL)
	  htsmsg_add_u32(m, "index", atoi(p + strlen("index:")));

	rstr_t *u = htsmsg_json_serialize_to_rstr(m, "vobsub:");

	mp_add_track(prop, filename, rstr_get(u),
		     "VobSub", NULL, lang, NULL, _p("External file"), score,
                     autosel);
	rstr_release(u);

	if(p == NULL)
	  break; // No indexing found, can only add one
      }
    }
  }
  buf_release(b);
}


TAILQ_HEAD(vobsub_entry_queue, vobsub_entry);

/**
 *
 */
typedef struct vobsub_entry {
  TAILQ_ENTRY(vobsub_entry) ve_link;
  int64_t ve_pts;
  int ve_fpos;
} vobsub_entry_t;


TAILQ_HEAD(vobsub_cmd_queue, vobsub_cmd);
/**
 *
 */
typedef struct vobsub_cmd {
  TAILQ_ENTRY(vobsub_cmd) vc_link;
  int vc_start;
  int vc_size;
  int64_t vc_pts;

} vobsub_cmd_t;

/**
 *
 */
typedef struct vobsub {
  ext_subtitles_t vs_es;
  fa_handle_t *vs_sub;
  struct vobsub_entry_queue vs_entries;
  vobsub_entry_t *vs_cur;
  int vs_stop;
  struct AVCodecParserContext *vs_parser;
  struct AVCodecContext *vs_ctx;
  uint32_t vs_clut[16];
  int vs_width;
  int vs_height;

  hts_mutex_t vs_mutex;
  hts_cond_t vs_cond;
  struct vobsub_cmd_queue vs_cmds;

  media_pipe_t *vs_mp;
  hts_thread_t vs_tid;

} vobsub_t;



/**
 *
 */
static int64_t
ve_stop(const vobsub_entry_t *ve)
{
  ve = TAILQ_NEXT(ve, ve_link);
  return ve != NULL ? ve->ve_pts : INT64_MAX;
}




#define getu32(b, l) ({						\
  uint32_t x = (b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]);	\
  b+=4;								\
  l-=4; 							\
  x;								\
})

#define getu16(b, l) ({						\
  uint16_t x = (b[0] << 8 | b[1]);	                        \
  b+=2;								\
  l-=2; 							\
  x;								\
})

#define getu8(b, l) ({						\
  uint8_t x = b[0];	                                        \
  b+=1;								\
  l-=1; 							\
  x;								\
})


#define getpts(b, l) ({					\
  int64_t _pts;						\
  _pts = (int64_t)((getu8(b, l) >> 1) & 0x07) << 30;	\
  _pts |= (int64_t)(getu16(b, l) >> 1) << 15;		\
  _pts |= (int64_t)(getu16(b, l) >> 1);			\
  _pts;							\
})



/**
 *
 */
static void
demux_pes(const vobsub_t *vs, media_pipe_t *mp,
	  uint32_t sc, const uint8_t *buf, int len, int64_t pts)
{
  uint8_t flags, hlen, x;
  int64_t dts = PTS_UNSET;

  x     = getu8(buf, len);
  flags = getu8(buf, len);
  hlen  = getu8(buf, len);
  
  if(len < hlen)
    return;

  if((x & 0xc0) != 0x80)
    /* no MPEG 2 PES */
    return;

  if((flags & 0xc0) == 0xc0) {
    if(hlen < 10)
      return;

    pts = getpts(buf, len);
    dts = getpts(buf, len);

    hlen -= 10;
  } else if((flags & 0xc0) == 0x80) {
    if(hlen < 5)
      return;

    dts = pts = getpts(buf, len);
    hlen -= 5;
  }

  buf += hlen;
  len -= hlen;

  if(sc == 0x1bd) {
    if(len < 1)
      return;
      
    sc = getu8(buf, len);
  }


  if(sc < 0x20 || sc > 0x3f)
    return;
  

  while(len > 0) {
    uint8_t *outbuf;
    int outlen;
    int rlen = av_parser_parse2(vs->vs_parser, vs->vs_ctx,
				&outbuf, &outlen, buf, len, 
				pts, dts, 0);
    if(outlen) {
      media_buf_t *mb = media_buf_alloc_unlocked(mp, outlen + 18*4);
      mb->mb_data_type = MB_CTRL_DVD_SPU2;
      mb->mb_dts = dts;
      mb->mb_pts = pts;
      uint32_t *d = (uint32_t *)mb->mb_data;
      d[16] = vs->vs_width;
      d[17] = vs->vs_height;
      memcpy(mb->mb_data, vs->vs_clut, 16 * 4);
      memcpy(mb->mb_data + 18*4, outbuf, outlen);
      mb_enqueue_always(mp, &mp->mp_video, mb);
    }
    pts = PTS_UNSET;
    dts = PTS_UNSET;
    buf += rlen;
    len -= rlen;
  }


}



/**
 *
 */
static void
demux_block(const vobsub_t *vs, const uint8_t *buf, int len,
	    media_pipe_t *mp, int64_t pts)
{ 
  uint32_t startcode, pes_len;

  if(buf[13] & 7)
    return; /* Stuffing is not supported */

  buf += 14;
  len -= 14;

  while(len > 0) {

    if(len < 6)
      break;

    startcode = getu32(buf, len);
    pes_len   = getu16(buf, len); 

    if(pes_len < 3)
      break;

    switch(startcode) {
    case 0x1bd:
    case 0x1bf:
    case 0x1c0 ... 0x1df:
    case 0x1e0 ... 0x1ef:
      demux_pes(vs, mp, startcode, buf, pes_len, pts);
      len -= pes_len;
      buf += pes_len;
      break;

    default:
      break;
    }
  }
}


/**
 *
 */
static void
demux_blocks(const vobsub_t *vs, const uint8_t *buf, int size,
	     media_pipe_t *mp, int64_t pts)
{
  while(size >= 2048) {
    demux_block(vs, buf, 2048, mp, pts);
    buf += 2048;
    size -= 2048;
  }
}


/**
 *
 */
static void
ve_load(vobsub_t *vs, int start, int size, int64_t pts)
{
  if(fa_seek(vs->vs_sub, start, SEEK_SET) != start)
    return;

  void *buf = mymalloc(size);
  if(buf == NULL)
    return;

  if(fa_read(vs->vs_sub, buf, size) == size)
    demux_blocks(vs, buf, size, vs->vs_mp, pts);

  free(buf);
}

/**
 *
 */
static void *
vobsub_thread(void *aux)
{
  vobsub_t *vs = aux;
  vobsub_cmd_t *vc;
  int run = 1;
  hts_mutex_lock(&vs->vs_mutex);

  while(run) {
    while((vc = TAILQ_FIRST(&vs->vs_cmds)) == NULL)
      hts_cond_wait(&vs->vs_cond, &vs->vs_mutex);
    TAILQ_REMOVE(&vs->vs_cmds, vc, vc_link);
    hts_mutex_unlock(&vs->vs_mutex);

    if(vc->vc_size == 0) {
      run = 0;
    } else {
      ve_load(vs, vc->vc_start, vc->vc_size, vc->vc_pts);
    }

    free(vc);
    hts_mutex_lock(&vs->vs_mutex);

  }
  hts_mutex_unlock(&vs->vs_mutex);
  return NULL;
}



/**
 *
 */
static void
vs_send_cmd(vobsub_t *vs, int start, int size, int64_t pts)
{
  vobsub_cmd_t *vc = malloc(sizeof(vobsub_cmd_t));
  vc->vc_start = start;
  vc->vc_size  = size;
  vc->vc_pts   = pts;
  hts_mutex_lock(&vs->vs_mutex);
  TAILQ_INSERT_TAIL(&vs->vs_cmds, vc, vc_link);
  hts_cond_signal(&vs->vs_cond);
  hts_mutex_unlock(&vs->vs_mutex);
}


/**
 *
 */
static void
ve_deliver(vobsub_t *vs, vobsub_entry_t *ve)
{
  vs->vs_cur = ve;

  const vobsub_entry_t *nxt = TAILQ_NEXT(ve, ve_link);
  int fend = nxt ? nxt->ve_fpos : vs->vs_stop;
  int size = fend - ve->ve_fpos;
  if(size > 0)
    vs_send_cmd(vs, ve->ve_fpos, size, ve->ve_pts);
}


/**
 *
 */
static void
vobsub_picker(struct ext_subtitles *es, int64_t pts)
{
  vobsub_t *vs = (vobsub_t *)es;
  vobsub_entry_t *ve = vs->vs_cur;

  if(ve != NULL && ve->ve_pts <= pts && ve_stop(ve) > pts)
    return; // Already sent
  
  if(ve != NULL) {
    ve = TAILQ_NEXT(ve, ve_link);
    if(ve != NULL && ve->ve_pts <= pts && ve_stop(ve) > pts) {
      ve_deliver(vs, ve);
      return;
    }
  }

  TAILQ_FOREACH(ve, &vs->vs_entries, ve_link) {
    if(ve->ve_pts <= pts && ve_stop(ve) > pts) {
      ve_deliver(vs, ve);
      return;
    }
  }
  vs->vs_cur = NULL;
}


/**
 *
 */
static int64_t
vobsub_get_ts(const char *buf)
{
  if(strlen(buf) < 12)
    return PTS_UNSET;

  if(buf[2] != ':' || buf[5] != ':' || buf[8] != ':')
    return PTS_UNSET;

  return 1000LL * (
    (buf[ 0] - '0') * 36000000LL +
    (buf[ 1] - '0') *  3600000LL +
    (buf[ 3] - '0') *   600000LL +
    (buf[ 4] - '0') *    60000LL + 
    (buf[ 6] - '0') *    10000LL + 
    (buf[ 7] - '0') *     1000LL +
    (buf[ 9] - '0') *      100LL +
    (buf[10] - '0') *       10LL +
    (buf[11] - '0'));
}


/**
 *
 */
static void
vobsub_dtor(ext_subtitles_t *es)
{
  vobsub_t *vs = (vobsub_t *)es;

  vs_send_cmd(vs, 0, 0, 0); // Tell worker to terminate
  hts_thread_join(&vs->vs_tid); // And collect it

  hts_cond_destroy(&vs->vs_cond);
  hts_mutex_destroy(&vs->vs_mutex);

  mp_release(vs->vs_mp);

  av_parser_close(vs->vs_parser);
  av_free(vs->vs_ctx);
  fa_close(vs->vs_sub);
}


/**
 *
 */
struct ext_subtitles *
vobsub_load(const char *json, char *errbuf, size_t errlen,
	    media_pipe_t *mp)
{
  htsmsg_t *m = htsmsg_json_deserialize(json);
  int idx;
  
  if(m == NULL) {
    snprintf(errbuf, errlen, "Unable to decode JSON");
    return NULL;
  }
  
  const char *idxfile = htsmsg_get_str(m, "idx");
  const char *subfile = htsmsg_get_str(m, "sub");
  idx = htsmsg_get_u32_or_default(m, "index", 0);
  
  if(idxfile == NULL || subfile == NULL) {
    snprintf(errbuf, errlen, "Missing message fields");
    return NULL;
  }

  buf_t *b;

  if((b = fa_load(idxfile,
                   FA_LOAD_ERRBUF(errbuf, errlen),
                   FA_LOAD_CACHE_CONTROL(DISABLE_CACHE),
                   NULL)) == NULL)
    return NULL;

  vobsub_t *vs = calloc(1, sizeof(vobsub_t));

  vs->vs_parser = av_parser_init(AV_CODEC_ID_DVD_SUBTITLE);
  vs->vs_ctx = avcodec_alloc_context3(NULL);

  if((vs->vs_sub = fa_open(subfile, errbuf, errlen)) == NULL) {
    buf_release(b);
    free(vs);
    return NULL;
  }

  TAILQ_INIT(&vs->vs_entries);
  
  b = buf_make_writable(b);
  char *s = buf_str(b);
  int l;
  int parse_ts = 0;
  int write_stop = 0;

  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    const char *p;
    s[l] = 0;

    if((p = mystrbegins(s, "palette:")) != NULL)
      vobsub_decode_palette(vs->vs_clut, p);
    if((p = mystrbegins(s, "size:")) != NULL)
      vobsub_decode_size(&vs->vs_width, &vs->vs_height, p);

    if((p = mystrbegins(s, "id:")) != NULL) {
      p = strstr(p, "index:");
      if(p == NULL && idx == -1)
	parse_ts = 1;
      else if(p != NULL && atoi(p + strlen("index:")) == idx)
	parse_ts = 1;
      else
	parse_ts = 0;
      continue;
    }

    if((p = mystrbegins(s, "timestamp:")) != NULL) {
      while(*p == 32)
	p++;

      int64_t ts = vobsub_get_ts(p);
      if(ts == AV_NOPTS_VALUE)
	continue;
      if((p = strstr(p, "filepos:")) == NULL)
	continue;
      int fpos = strtol(p + strlen("filepos:"), NULL, 16);
      
      if(parse_ts) {

	vobsub_entry_t *ve = malloc(sizeof(vobsub_entry_t));
	ve->ve_pts  = ts;
	ve->ve_fpos = fpos;
	TAILQ_INSERT_TAIL(&vs->vs_entries, ve, ve_link);
      } else {
	if(write_stop)
	  vs->vs_stop = fpos;
	write_stop = 0;
      }
    }
  }
  
  if(write_stop)
    vs->vs_stop = fa_fsize(vs->vs_sub);

  buf_release(b);

  vs->vs_es.es_dtor = vobsub_dtor;
  vs->vs_es.es_picker = vobsub_picker;

  vs->vs_mp = mp_retain(mp);

  TAILQ_INIT(&vs->vs_cmds);

  hts_mutex_init(&vs->vs_mutex);
  hts_cond_init(&vs->vs_cond, &vs->vs_mutex);
  
  hts_thread_create_joinable("vobsub loader", &vs->vs_tid,
			     vobsub_thread, vs, THREAD_PRIO_METADATA);
  return &vs->vs_es;
}
