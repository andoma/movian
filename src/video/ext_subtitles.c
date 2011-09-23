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

#include "showtime.h"
#include "ext_subtitles.h"
#include "fileaccess/fileaccess.h"
#include "misc/gz.h"
#include "htsmsg/htsmsg_xml.h"
#include "libavcodec/avcodec.h"
#include "media.h"
#include "misc/dbl.h"
#include "misc/string.h"
#include "i18n.h"
#include "video_overlay.h"

/**
 *
 */
static int
ese_cmp(const ext_subtitle_entry_t *a, const ext_subtitle_entry_t *b)
{
  if(a->ese_start > b->ese_start)
    return 1;
  if(a->ese_start < b->ese_start)
    return -1;
  return 0;
}


/**
 *
 */
static void
ese_insert(ext_subtitles_t *es, char *txt, int64_t start, int64_t stop)
{
  ext_subtitle_entry_t *ese = malloc(sizeof(ext_subtitle_entry_t));
  ese->ese_start = start;
  ese->ese_stop = stop;
  ese->ese_text = txt;
  if(RB_INSERT_SORTED(&es->es_entries, ese, ese_link, ese_cmp)) {
    // Collision
    free(ese);
    free(txt);
  }
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
  if(lr->ll != 29 || memcmp(lr->buf + 12, " --> ", 5))
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
static void
ext_srt_decode(struct video_decoder *vd, struct ext_subtitles *es,
	       ext_subtitle_entry_t *ese)
{
  video_overlay_render_cleartext(vd, ese->ese_text,
				 ese->ese_start, ese->ese_stop,
				 1);
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
  
  RB_INIT(&es->es_entries);

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
	ese_insert(es, txt, pstart, pstop);
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
    ese_insert(es, txt, pstart, pstop);
    txt = NULL;
  }
  free(txt);
  free(tmp);
  TRACE(TRACE_DEBUG, "Subtitles", "Loaded %s as SRT, %d pages", url,
	es->es_entries.entries);
  es->es_decode = ext_srt_decode;
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
 *
 */
static void
ext_ttlm_decode(struct video_decoder *vd, struct ext_subtitles *es,
		ext_subtitle_entry_t *ese)
{
  video_overlay_render_cleartext(vd, ese->ese_text,
				 ese->ese_start, ese->ese_stop,
				 0);
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
  RB_INIT(&es->es_entries);

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

      ese_insert(es, strdup(txt), start, end);
    }
  }
  TRACE(TRACE_DEBUG, "Subtitles", "Loaded %s as TTML, %d pages", url,
	es->es_entries.entries);

  es->es_decode = ext_ttlm_decode;
  return es;
}


#if 0
/**
 *
 */
static void
dump_subtitles(subtitles_t *s)
{
  subtitle_entry_t *se;

  RB_FOREACH(se, &es->es_entries, se_link) {
    printf("PAGE: %lld -> %lld\n--\n%s\n--\n", se->se_start, se->se_stop, se->se_text);
  }
}
#endif

/**
 *
 */
static ext_subtitles_t *
subtitles_create(const char *path, char **bufp, size_t len)
{
  ext_subtitles_t *s = NULL;

  if(is_ttml(*bufp, len)) {
    s = load_ttml(path, bufp, len);
  } else {

    int force_utf8 = 0;
    const char *buf = *bufp;

    if(len > 3 && buf[0] == 0xef && buf[1] == 0xbb && buf[2] == 0xbf) {
      // UTF-8 BOM
      force_utf8 = 1;
      buf += 3;
      len -= 3;
    }

    if(is_srt(buf, len))
      s = load_srt(path, buf, len, force_utf8);
  }

  //  if(s)dump_subtitles(s);
  return s;
}


/**
 *
 */
static void
subtitle_entry_destroy(ext_subtitle_entry_t *ese)
{
  if(ese->ese_link.left != NULL)
    subtitle_entry_destroy(ese->ese_link.left);
  if(ese->ese_link.right != NULL)
    subtitle_entry_destroy(ese->ese_link.right);
  free(ese->ese_text);
  free(ese);
}



/**
 *
 */
void
subtitles_destroy(ext_subtitles_t *es)
{
  if(es->es_entries.root != NULL)
    subtitle_entry_destroy(es->es_entries.root);

  free(es);
}


/**
 *
 */
ext_subtitle_entry_t *
subtitles_pick(ext_subtitles_t *es, int64_t pts)
{
  ext_subtitle_entry_t skel, *ese = es->es_cur;

  if(ese != NULL && ese->ese_start <= pts && ese->ese_stop > pts)
    return NULL; // Already sent
  
  if(ese != NULL) {
    ese = RB_NEXT(ese, ese_link);
    if(ese != NULL && ese->ese_start <= pts && ese->ese_stop > pts)
      return es->es_cur = ese;
  }
  
  skel.ese_start = pts;
  ese = RB_FIND_LE(&es->es_entries, &skel, ese_link, ese_cmp);
  if(ese == NULL || ese->ese_stop <= pts) {
    es->es_cur = NULL;
    return NULL;
  }

  return es->es_cur = ese;
}


/**
 *
 */
ext_subtitles_t *
subtitles_load(const char *url)
{
  ext_subtitles_t *sub;
  char errbuf[256];
  struct fa_stat fs;
  int datalen;
  char *data = fa_quickload(url, &fs, NULL, errbuf, sizeof(errbuf));

  if(data == NULL) {
    TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- %s", 
	  url, errbuf);
    return NULL;
  }

  if(gz_check(data, fs.fs_size)) {
    // is .gz compressed, inflate it

    char *inflated;
    size_t inflatedlen;

    inflated = gz_inflate(data, fs.fs_size,
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
    datalen = fs.fs_size;
  }

  sub = subtitles_create(url, &data, datalen);

  free(data);

  if(sub == NULL)
    TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- Unknown format", 
	  url);
  return sub;
}
