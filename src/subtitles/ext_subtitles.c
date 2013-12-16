/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include "text/text.h"
#include "subtitles.h"

/**
 *
 */
static void
es_insert_text(ext_subtitles_t *es, const char *text,
	       int64_t start, int64_t stop, int tags)
{
  video_overlay_t *vo;
  vo = video_overlay_render_cleartext(text, start, stop, tags, 0);
  if(vo != NULL)
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
    char how[256];
    tmp = utf8_from_bytes(buf, len, NULL, how, sizeof(how));
    TRACE(TRACE_INFO, "Subtitles", "%s is not valid UTF-8. %s", url, how);
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
get_sub_timestamp(const char *buf, int *start, int *stop)
{
  const char *b = buf;
  if(*b != '{')
    return -1;
  *start = strtol(b + 1, (char **)&b, 10);
  if(b[0] != '}' || b[1] != '{')
    return -1;
  *stop = strtol(b + 2, (char **)&b, 10);
  if(b[0] != '}')
    return -1;

  return b + 1 - buf;
}


/**
 *
 */
static int
is_sub(const char *buf, size_t len)
{
  int start, stop;
  return get_sub_timestamp(buf, &start, &stop) != -1;
}


/**
 *
 */
static ext_subtitles_t *
load_sub(const char *url, char *buf, size_t len, int force_utf8,
         AVRational *fr)
{
  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  char *tmp = NULL;
  AVRational fr0 = {25, 1};

  if(fr == NULL) {
    fr = &fr0;
  }

  TAILQ_INIT(&es->es_entries);

  if(force_utf8 || utf8_verify(buf)) {
  } else {
    char how[256];
    tmp = utf8_from_bytes(buf, len, NULL, how, sizeof(how));
    TRACE(TRACE_INFO, "Subtitles", "%s is not valid UTF-8. %s", url, how);
    buf = tmp;
  }


  LINEPARSE(s, buf) {
    int start, stop;
    int x = get_sub_timestamp(s, &start, &stop);
    if(x <= 0)
      continue;

    s += x;

    if(start == 1 && stop == 1) {
      // Set framerate
      fr0.num = my_str2double(s, NULL) * 1000000.0;
      fr0.den = 1000000;
      fr = &fr0;
    }

    int reset_color    = 0;
    int reset_bold     = 0;
    int reset_italic   = 0;
    int reset_font     = 0;
    uint32_t outbuf[1024];

    outbuf[0] = TR_CODE_COLOR         | subtitle_settings.color;
    outbuf[1] = TR_CODE_SHADOW        | subtitle_settings.shadow_displacement;
    outbuf[2] = TR_CODE_SHADOW_COLOR  | subtitle_settings.shadow_color;
    outbuf[3] = TR_CODE_OUTLINE       | subtitle_settings.outline_size;
    outbuf[4] = TR_CODE_OUTLINE_COLOR | subtitle_settings.outline_color;
    int outptr = 5;

    while(*s && outptr <= 1000) {
      if(*s == '{') {
        s++;

        int doreset = 0;

        switch(*s) {
        default:
          break;

        case 'y':
          doreset = 1;
          // FALLTHRU
        case 'Y':
          s++;
          while(*s != '}' && *s) {
            if(*s == 'b') {
              outbuf[outptr++] = TR_CODE_BOLD_ON;
              reset_bold = doreset;
            } else if(*s == 'i') {
              outbuf[outptr++] = TR_CODE_ITALIC_ON;
              reset_italic = doreset;
            }
            s++;
          }
          break;

        case 'c':
          reset_color = 1;
          // FALLTHRU
        case 'C':
          if(strlen(s) < 9)
            break;
          s+= 3;
          outbuf[outptr++] = TR_CODE_COLOR |
            (hexnibble(s[0]) << 20) |
            (hexnibble(s[1]) << 16) |
            (hexnibble(s[2]) << 12) |
            (hexnibble(s[3]) <<  8) |
            (hexnibble(s[4]) <<  4) |
            (hexnibble(s[5])      );
          break;
        }

        while(*s != '}' && *s)
          s++;

        if(*s)
          s++;
        continue;

      } else if(*s == '|') {
        outbuf[outptr++] = '\n';

        if(reset_color) {
          outbuf[outptr++] = TR_CODE_COLOR | subtitle_settings.color;;
          reset_color = 0;
        }

        if(reset_bold) {
          outbuf[outptr++] = TR_CODE_BOLD_OFF;
          reset_bold = 0;
        }

        if(reset_italic) {
          outbuf[outptr++] = TR_CODE_ITALIC_OFF;
          reset_italic = 0;
        }

        if(reset_font) {
          outbuf[outptr++] = TR_CODE_FONT_RESET;
          reset_italic = 0;
        }

      } else {
        outbuf[outptr++] = *s;
      }
      s++;
    }


    video_overlay_t *vo = calloc(1, sizeof(video_overlay_t));
    vo->vo_type = VO_TEXT;
    vo->vo_text = malloc(outptr * sizeof(uint32_t));
    memcpy(vo->vo_text, outbuf, outptr * sizeof(uint32_t));
    vo->vo_text_length = outptr;
    vo->vo_padding_left = -1;  // auto padding
    vo->vo_start = 1000000LL * start * fr->den / fr->num;
    vo->vo_stop =  1000000LL * stop  * fr->den / fr->num;
    TAILQ_INSERT_TAIL(&es->es_entries, vo, vo_link);

  }
  free(tmp);
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
subtitles_create(const char *path, char *buf, size_t len, AVRational *fr)
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
    if(is_sub(b0, len))
      s = load_sub(path, b0, len, force_utf8, fr);
  }

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
vo_deliver(ext_subtitles_t *es, video_overlay_t *vo, media_pipe_t *mp,
	   int64_t pts)
{
  int64_t s = vo->vo_start;
  do {
    es->es_cur = vo;

    video_overlay_enqueue(mp, video_overlay_dup(vo));
    vo = TAILQ_NEXT(vo, vo_link);
  } while(vo != NULL && vo->vo_start == s && vo->vo_stop > pts);
}


/**
 *
 */
void
subtitles_pick(ext_subtitles_t *es, int64_t pts, media_pipe_t *mp)
{
  video_overlay_t *vo = es->es_cur;

  if(es->es_picker)
    return es->es_picker(es, pts);

  if(vo != NULL) {
    vo = TAILQ_NEXT(vo, vo_link);
    if(vo != NULL && vo->vo_start <= pts && vo->vo_stop > pts) {
      vo_deliver(es, vo, mp, pts);
      return;
    }
  }

  vo = es->es_cur;

  if(vo != NULL && vo->vo_start <= pts && vo->vo_stop > pts) {
    return; // Already sent
  }

  TAILQ_FOREACH(vo, &es->es_entries, vo_link) {
    if(vo->vo_start <= pts && vo->vo_stop > pts) {
      vo_deliver(es, vo, mp, pts);
      return;
    }
  }
  es->es_cur = NULL;
}


static ext_subtitles_t *
subtitles_from_zipfile(media_pipe_t *mp, buf_t *b, AVRational *fr)
{
  ext_subtitles_t *ret = NULL;
  char errbuf[256];
  char url[64];

  int id = memfile_register(b->b_ptr, b->b_size);
  snprintf(url, sizeof(url), "zip://memfile://%d", id);
  fa_dir_t *fd = fa_scandir(url, errbuf, sizeof(errbuf));
  if(fd != NULL) {
    fa_dir_entry_t *fde;
    RB_FOREACH(fde, &fd->fd_entries, fde_link) {
      TRACE(TRACE_DEBUG, "Subtitles", "  Probing %s", rstr_get(fde->fde_url));
      ret = subtitles_load(mp, rstr_get(fde->fde_url), fr);
      if(ret != NULL)
	break;
    }
    fa_dir_free(fd);
  } else {
    TRACE(TRACE_ERROR, "Subtitles", "Unable to open ZIP -- %s", errbuf);
  }

  memfile_unregister(id);
  return ret;
}


/**
 *
 */
ext_subtitles_t *
subtitles_load(media_pipe_t *mp, const char *url, AVRational *fr)
{
  ext_subtitles_t *sub;
  char errbuf[256];

  const char *s;
  if((s = mystrbegins(url, "vobsub:")) != NULL) {
    sub = vobsub_load(s, errbuf, sizeof(errbuf), mp);
    if(sub == NULL) 
      TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- %s", 
	    s, errbuf);
    return sub;
  }

  TRACE(TRACE_DEBUG, "Subtitles", "Trying to load %s", url);

  buf_t *b = fa_load(url, NULL, errbuf, sizeof(errbuf),
                     DISABLE_CACHE, 0, NULL, NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- %s", 
	  url, errbuf);
    return NULL;
  }

  if(b->b_size > 4 && !memcmp(buf_cstr(b), "PK\003\004", 4)) {
    TRACE(TRACE_DEBUG, "Subtitles", "%s is a ZIP archive, scanning...", url);
    return subtitles_from_zipfile(mp, b, fr);
  }
  
  if(gz_check(b)) {
    // is .gz compressed, inflate it

    b = gz_inflate(b, errbuf, sizeof(errbuf));
    if(b == NULL) {
      TRACE(TRACE_ERROR, "Subtitles", "Unable to decompress %s -- %s", 
	    url, errbuf);
      return NULL;
    }
  }

  b = buf_make_writable(b);
  sub = subtitles_create(url, b->b_ptr, b->b_size, fr);

  if(sub == NULL)
    TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- Unknown format", 
	  url);
  return sub;
}
