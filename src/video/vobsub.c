/*
 *  VOBSUB (.idx and .sub) parser
 *  Copyright (C) 2012 Andreas Öman
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

#include <limits.h>

#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "vobsub.h"
#include "misc/string.h"
#include "media.h"
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
    clut[i++] = v;
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
	     int score, struct prop *prop, const char *subfile)
{
  char *buf;
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

  buf = fa_load(url, NULL, NULL, errbuf, sizeof(errbuf),
		DISABLE_CACHE, 0, NULL, NULL);
  if(buf == NULL) {
    TRACE(TRACE_ERROR, "VOBSUB", "Unable to load %s -- %s", url, errbuf);
    return;
  }
  
  char *s = buf;
  int l;
  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    const char *p;
    s[l] = 0;
    if((p = mystrbegins(s, "id:")) != NULL) {
      while(*p == ' ')
	p++;
      if(strlen(p) >= 2) {
	const char *lang = dvd_langcode_to_string(p[0] << 8 | p[1]);

	htsmsg_t *m = htsmsg_create_map();
	htsmsg_add_str(m, "idx", url);
	htsmsg_add_str(m, "sub", subfile);

	if((p = strstr(p, "index:")) != NULL)
	  htsmsg_add_u32(m, "index", atoi(p + strlen("index:")));

	rstr_t *u = htsmsg_json_serialize_to_rstr(m, "vobsub:");
	

	mp_add_track(prop, filename, rstr_get(u),
		     "VobSub", NULL, lang, NULL, _p("External file"), score);
	rstr_release(u);

	if(p == NULL)
	  break; // No indexing found, can only add one
      }
    }
  }
  free(buf);
}


TAILQ_HEAD(vobsub_entry_queue, vobsub_entry);

/**
 *
 */
typedef struct vobsub_entry {
  TAILQ_ENTRY(vobsub_entry) ve_link;
  int64_t ve_start;
  int ve_fpos;
} vobsub_entry_t;


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
} vobsub_t;



/**
 *
 */
static int64_t
ve_stop(const vobsub_entry_t *ve)
{
  ve = TAILQ_NEXT(ve, ve_link);
  return ve != NULL ? ve->ve_start : INT64_MAX;
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
demux_pes(vobsub_t *vs, video_decoder_t *vd,
	  uint32_t sc, const uint8_t *buf, int len, int64_t pts)
{
  uint8_t flags, hlen, x;
  int64_t dts = AV_NOPTS_VALUE;

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
      void *buf = malloc(outlen);
      memcpy(buf, outbuf, outlen);
      dvdspu_enqueue(vd, buf, outlen, vs->vs_clut, vs->vs_width,
		     vs->vs_height, pts);
    }
    pts = AV_NOPTS_VALUE;
    dts = AV_NOPTS_VALUE;
    buf += rlen;
    len -= rlen;
  }


}



/**
 *
 */
static void
demux_block(vobsub_t *vs, const uint8_t *buf, int len, video_decoder_t *vd,
	    int64_t pts)
{ 
  uint32_t startcode, pes_len;

  if(buf[13] & 7)
    return; /* Stuffing is not supported */

  buf += 14;
  len -= 14;

  while(len > 0) {

    if(len < 4)
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
      demux_pes(vs, vd, startcode, buf, pes_len, pts);
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
demux_blocks(vobsub_t *vs, const uint8_t *buf, int size, video_decoder_t *vd,
	     int64_t pts)
{
  while(size >= 2048) {
    demux_block(vs, buf, 2048, vd, pts);
    buf += 2048;
    size -= 2048;
  }
}


/**
 *
 */
static void
ve_deliver(vobsub_t *vs, vobsub_entry_t *ve, video_decoder_t *vd)
{
  vs->vs_cur = ve;

  const vobsub_entry_t *nxt = TAILQ_NEXT(ve, ve_link);
  int fstart = ve->ve_fpos;
  int fend = nxt ? nxt->ve_fpos : vs->vs_stop;
  int size = fend - fstart;

  if(fa_seek(vs->vs_sub, fstart, SEEK_SET) != fstart)
    return;

  void *buf = mymalloc(size);
  if(buf == NULL)
    return;

  if(fa_read(vs->vs_sub, buf, size) == size)
    demux_blocks(vs, buf, size, vd, ve->ve_start);

  free(buf);
}


/**
 *
 */
static void
vobsub_picker(struct ext_subtitles *es, int64_t pts,
	      struct video_decoder *vd)
{
  vobsub_t *vs = (vobsub_t *)es;
  vobsub_entry_t *ve = vs->vs_cur;

  if(ve != NULL && ve->ve_start <= pts && ve_stop(ve) > pts)
    return; // Already sent
  
  if(ve != NULL) {
    ve = TAILQ_NEXT(ve, ve_link);
    if(ve != NULL && ve->ve_start <= pts && ve_stop(ve) > pts) {
      ve_deliver(vs, ve, vd);
      return;
    }
  }

  TAILQ_FOREACH(ve, &vs->vs_entries, ve_link) {
    if(ve->ve_start <= pts && ve_stop(ve) > pts) {
      ve_deliver(vs, ve, vd);
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
    return AV_NOPTS_VALUE;

  if(buf[2] != ':' || buf[5] != ':' || buf[8] != ':')
    return AV_NOPTS_VALUE;

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
  av_parser_close(vs->vs_parser);
  av_free(vs->vs_ctx);
  fa_close(vs->vs_sub);
}


/**
 *
 */
struct ext_subtitles *
vobsub_load(const char *json, char *errbuf, size_t errlen)
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

  char *buf;

  if((buf = fa_load(idxfile, NULL, NULL, errbuf, errlen,
		    DISABLE_CACHE, 0, NULL, NULL)) == NULL)
    return NULL;

  vobsub_t *vs = calloc(1, sizeof(vobsub_t));

  vs->vs_parser = av_parser_init(CODEC_ID_DVD_SUBTITLE);
  vs->vs_ctx = avcodec_alloc_context();

  if((vs->vs_sub = fa_open(subfile, errbuf, errlen)) == NULL) {
    free(buf);
    free(vs);
    return NULL;
  }

  TAILQ_INIT(&vs->vs_entries);
  
  char *s = buf;
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
	ve->ve_start = ts;
	ve->ve_fpos  = fpos;
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

  free(buf);

  vs->vs_es.es_dtor = vobsub_dtor;
  vs->vs_es.es_picker = vobsub_picker;
  return &vs->vs_es;
}
