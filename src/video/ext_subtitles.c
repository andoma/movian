/*
 *  Subtitling
 *  Copyright (C) 2007, 2010 Andreas Öman
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "showtime.h"
#include "ext_subtitles.h"
#include "fileaccess/fileaccess.h"
#include "misc/gz.h"
#include "htsmsg/htsmsg_xml.h"
#include "media.h"
#include "misc/dbl.h"
#include "misc/str.h"
#include "i18n.h"
#include "video_overlay.h"
#include "vobsub.h"

/**
 *
 */
static void
es_insert_text(ext_subtitles_t *es, const char *text,
	       int64_t start, int64_t stop, int tags)
{
  video_overlay_t *vo;
  vo = video_overlay_render_cleartext(text, start, stop, tags, 0);
  TAILQ_INSERT_TAIL(&es->es_entries, vo, vo_link);
}
	       

/**
 *
 */
typedef struct {
  const char *buf;
  size_t len;       // Remaining bytes in buf
  ssize_t ll;        // Length of current line
} linereader_t;


static void
linereader_init(linereader_t *lr, const char *buf, size_t len)
{
  lr->buf = buf;
  lr->len = len;
  lr->ll = -1;
}



/**
 * Find end of line
 */
static ssize_t
linereader_next(linereader_t *lr)
{
  ssize_t i;

  if(lr->ll != -1) {
    // Skip over previous line
    lr->buf += lr->ll;
    lr->len -= lr->ll;

    /* Skip over EOL */
    if(lr->len > 0 && lr->buf[0] == 13) {
      lr->len--;
      lr->buf++;
    }

    if(lr->len > 0 && lr->buf[0] == 10) {
      lr->len--;
      lr->buf++; 
    }

  }

  if(lr->len == 0) {
    /* At EOF */
    lr->ll = -1;
    return -1;
  }

  for(i = 0; i < lr->len; i++)
    if(lr->buf[i] == 10 || lr->buf[i] == 13)
      break;

  lr->ll = i;
  return i;
}


/**
 *
 */
static int
get_int(linereader_t *lr, int *vp)
{
  int i, r = 0;
  for(i = 0; i < lr->ll; i++) {
    if(lr->buf[i] < '0' || lr->buf[i] > '9')
      return -1;
    r = r * 10 + lr->buf[i] - '0';
  }
  *vp = r;
  return 0;
}


/**
 *
 */
static int64_t
get_srt_timestamp2(const char *buf)
{
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
static int
get_srt_timestamp(linereader_t *lr, int64_t *start, int64_t *stop)
{
  if(lr->ll < 29 || memcmp(lr->buf + 12, " --> ", 5))
    return -1;

  *start = get_srt_timestamp2(lr->buf);
  *stop  = get_srt_timestamp2(lr->buf + 17);
  return 0;
}


/**
 *
 */
static int
is_srt(const char *buf, size_t len)
{
  linereader_t lr;

  int n;
  int64_t start, stop;

  linereader_init(&lr, buf, len);

  if(linereader_next(&lr) < 0)
    return 0;
  if(get_int(&lr, &n))
    return 0;
  if(linereader_next(&lr) < 0)
    return 0;
  if(get_srt_timestamp(&lr, &start, &stop))
    return 0;

  if(stop < start)
    return 0;

  return 1;
}


/**
 *
 */
static int
is_ass(const char *buf, size_t len)
{
  if(strstr(buf, "[Script Info]") == NULL)
    return 0;
  if(strstr(buf, "[Events]") == NULL)
    return 0;
  return 1;
}

/**
 *
 */
static ext_subtitles_t *
load_srt(const char *url, const char *buf, size_t len, int force_utf8)
{
  int n;
  size_t tlen = 0;
  int64_t start, stop, pstart = -1, pstop = -1;
  linereader_t lr;
  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  char *txt = NULL, *tmp = NULL;
  size_t txtoff = 0;
  
  TAILQ_INIT(&es->es_entries);

  if(force_utf8 || utf8_verify(buf)) {
    linereader_init(&lr, buf, len);
  } else {
    TRACE(TRACE_INFO, "Subtitles",
	  "%s is not valid UTF-8. Decoding it as %s",
	  url, charset_get_name(i18n_get_srt_charset()));
    tmp = utf8_from_bytes(buf, len, i18n_get_srt_charset());
    linereader_init(&lr, tmp, strlen(tmp));
  }

  while(1) {
    if((n = linereader_next(&lr)) < 0)
      break;

    if(get_srt_timestamp(&lr, &start, &stop) == 0) {
      if(txt != NULL && pstart != -1 && pstop != -1) {
	txt[txtoff] = 0;
	es_insert_text(es, txt, pstart, pstop, 1);
	free(txt);
	txt = NULL;
	tlen = 0;
	txtoff = 0;
      }
      pstart = start;
      pstop  = stop;
      continue;
    }
    if(pstart == -1)
      continue;

    txt = realloc(txt, tlen + lr.ll + 1);
    memcpy(txt + tlen, lr.buf, lr.ll);
    txt[tlen + lr.ll] = 0x0a;
    if(lr.ll == 0 && tlen > 0)
      txtoff = tlen - 1;
    tlen += lr.ll + 1;
  }

  if(txt != NULL && pstart != -1 && pstop != -1) {
    txt[txtoff] = 0;
    es_insert_text(es, txt, pstart, pstop, 1);
  }
  free(txt);
  free(tmp);
  return es;
}


/**
 *
 */
static int
is_ttml(const char *buf, size_t len)
{
  if(len < 30)
    return 0;
  if(memcmp(buf, "<?xml", 5))
    return 0;
  if(strstr(buf, "http://www.w3.org/2006/10/ttaf1") == NULL)
    return 0;
  return 1;
}


/**
 *
 */
static int64_t
ttml_time_expression(const char *str)
{
  const char *endp = NULL;
  double t = my_str2double(str, &endp);

  if(!strcmp(endp, "h"))
    return t * 3600 * 1000000;
  if(!strcmp(endp, "m"))
    return t * 60 * 1000000;
  if(!strcmp(endp, "ms"))
    return t * 1000;
  if(!strcmp(endp, "s"))
    return t * 1000000;
  return -1;
}


/**
 * TTML docs here: http://www.w3.org/TR/ttaf1-dfxp/
 */
static ext_subtitles_t *
load_ttml(const char *url, char **buf, size_t len)
{
  char errbuf[256];
  htsmsg_t *xml = htsmsg_xml_deserialize(*buf, errbuf, sizeof(errbuf));
  htsmsg_t *subs;
  htsmsg_field_t *f;

  if(xml == NULL) {
    TRACE(TRACE_INFO, "Subtitles", "Unable to load TTML: %s", errbuf);
    return NULL;
  }

  *buf = NULL;

  subs = htsmsg_get_map_multi(xml, "tags",
			      "tt", "tags",
			      "body", "tags",
			      "div", "tags",
			      NULL);

  if(subs == NULL) {
    htsmsg_destroy(xml);
    return NULL;
  }

  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  TAILQ_INIT(&es->es_entries);

  HTSMSG_FOREACH(f, subs) {
    if(f->hmf_type == HMF_MAP) {
      htsmsg_t *n = &f->hmf_msg;
      const char *str, *txt;
      int64_t start, end;

      if((txt = htsmsg_get_str(n, "cdata")) == NULL)
	continue;

      if((str = htsmsg_get_str_multi(n, "attrib", "begin", NULL)) == NULL)
	continue;
      if((start = ttml_time_expression(str)) == -1)
	continue;

      if((str = htsmsg_get_str_multi(n, "attrib", "end", NULL)) == NULL)
	continue;
      if((end = ttml_time_expression(str)) == -1)
	continue;

      es_insert_text(es, txt, start, end, 0);
    }
  }
  return es;
}


/**
 *
 */
static int
vocmp(const void *A, const void *B)
{
  const video_overlay_t *a = *(const video_overlay_t **)A;
  const video_overlay_t *b = *(const video_overlay_t **)B;
  if(a->vo_start < b->vo_start)
    return -1;
  if(a->vo_start > b->vo_start)
    return 1;
  if(a->vo_stop < b->vo_stop)
    return -1;
  if(a->vo_stop > b->vo_stop)
    return 1;
  return 0;
}


/**
 *
 */
static void
es_sort(ext_subtitles_t *es)
{
  video_overlay_t *vo, **vec;
  int cnt = 0, i;

  TAILQ_FOREACH(vo, &es->es_entries, vo_link)
    cnt++;

  vec = malloc(sizeof(video_overlay_t *) * cnt);
  
  cnt = 0;
  TAILQ_FOREACH(vo, &es->es_entries, vo_link)
    vec[cnt++] = vo;
  
  qsort(vec, cnt, sizeof(video_overlay_t *), vocmp);

  TAILQ_INIT(&es->es_entries);
  for(i = 0; i < cnt; i++)
    TAILQ_INSERT_TAIL(&es->es_entries, vec[i], vo_link);
  free(vec);
}


/**
 *
 */
static ext_subtitles_t *
subtitles_create(const char *path, char *buf, size_t len)
{
  ext_subtitles_t *s = NULL;
  
  if(is_ttml(buf, len)) {
    s = load_ttml(path, &buf, len);
  } else {

    int force_utf8 = 0;
    char *b0 = buf;
    if(len > 2 && ((buf[0] == 0xff && buf[1] == 0xfe) ||
		   (buf[0] == 0xfe && buf[1] == 0xff))) {
      // UTF-16 BOM
      utf16_to_utf8(&buf, &len);
      force_utf8 = 1;
      b0 = buf;
    } else if(len > 3 && buf[0] == 0xef && buf[1] == 0xbb && buf[2] == 0xbf) {
      // UTF-8 BOM
      force_utf8 = 1;
      b0 += 3;
      len -= 3;
    }

    if(is_srt(b0, len))
      s = load_srt(path, b0, len, force_utf8);
    if(is_ass(b0, len))
      s = load_ssa(path, b0, len);
  }

  //  if(s)dump_subtitles(s);
  if(s)
    es_sort(s);
  free(buf);
  return s;
}


/**
 *
 */
void
subtitles_destroy(ext_subtitles_t *es)
{
  video_overlay_t *vo;

  while((vo = TAILQ_FIRST(&es->es_entries)) != NULL) {
    TAILQ_REMOVE(&es->es_entries, vo, vo_link);
    video_overlay_destroy(vo);
  }
  if(es->es_dtor)
    es->es_dtor(es);
  free(es);
}


/**
 *
 */
static void
vo_deliver(ext_subtitles_t *es, video_overlay_t *vo, video_decoder_t *vd,
	   int64_t pts)
{
  int64_t s = vo->vo_start;
  do {
    es->es_cur = vo;

    video_overlay_enqueue(vd, video_overlay_dup(vo));
    vo = TAILQ_NEXT(vo, vo_link);
  } while(vo != NULL && vo->vo_start == s && vo->vo_stop > pts);
}


/**
 *
 */
void
subtitles_pick(ext_subtitles_t *es, int64_t pts, video_decoder_t *vd)
{
  video_overlay_t *vo = es->es_cur;

  if(es->es_picker)
    return es->es_picker(es, pts, vd);

  if(vo != NULL && vo->vo_start <= pts && vo->vo_stop > pts)
    return; // Already sent
  
  if(vo != NULL) {
    vo = TAILQ_NEXT(vo, vo_link);
    if(vo != NULL && vo->vo_start <= pts && vo->vo_stop > pts) {
      vo_deliver(es, vo, vd, pts);
      return;
    }
  }

  TAILQ_FOREACH(vo, &es->es_entries, vo_link) {
    if(vo->vo_start <= pts && vo->vo_stop > pts) {
      vo_deliver(es, vo, vd, pts);
      return;
    }
  }
  es->es_cur = NULL;
}


/**
 *
 */
ext_subtitles_t *
subtitles_load(media_pipe_t *mp, const char *url)
{
  ext_subtitles_t *sub;
  char errbuf[256];
  size_t size;
  int datalen;
  const char *s;
  if((s = mystrbegins(url, "vobsub:")) != NULL) {
    sub = vobsub_load(s, errbuf, sizeof(errbuf), mp);
    if(sub == NULL) 
      TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- %s", 
	    s, errbuf);
    return sub;
  }

  char *data = fa_load(url, &size, NULL, errbuf, sizeof(errbuf),
		       DISABLE_CACHE, 0, NULL, NULL);

  if(data == NULL) {
    TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- %s", 
	  url, errbuf);
    return NULL;
  }

  if(gz_check(data, size)) {
    // is .gz compressed, inflate it

    char *inflated;
    size_t inflatedlen;

    inflated = gz_inflate(data, size,
			  &inflatedlen, errbuf, sizeof(errbuf));

    free(data);
    if(inflated == NULL) {
      TRACE(TRACE_ERROR, "Subtitles", "Unable to decompress %s -- %s", 
	    url, errbuf);
      return NULL;
    }
    data = inflated;
    datalen = inflatedlen;
  } else {
    datalen = size;
  }

  sub = subtitles_create(url, data, datalen);

  if(sub == NULL)
    TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- Unknown format", 
	  url);
  return sub;
}
