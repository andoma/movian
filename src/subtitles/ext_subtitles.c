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
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "main.h"
#include "ext_subtitles.h"
#include "fileaccess/fileaccess.h"
#include "misc/gz.h"
#include "htsmsg/htsmsg_xml.h"
#include "media/media.h"
#include "misc/dbl.h"
#include "misc/str.h"
#include "i18n.h"
#include "video_overlay.h"
#include "vobsub.h"
#include "text/text.h"
#include "subtitles.h"
#include "misc/minmax.h"


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
static void
srt_skip_preamble(const char **bufp, size_t *lenp)
{
  const char *buf = *bufp;
  size_t len = *lenp;

  // WEBVTT is srt (see #2752)
  if(len > 6 && !memcmp(buf, "WEBVTT", 6)) {
    buf += 6;
    len -= 6;
  }

  // Skip over any initial control characters (Issue #1885)
  while(len && *buf && *buf <= 32) {
    len--;
    buf++;
  }

  *bufp = buf;
  *lenp = len;
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

  srt_skip_preamble(&buf, &len);

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
load_srt(const char *url, const char *buf, size_t len)
{
  int n;
  size_t tlen = 0;
  int64_t start, stop, pstart = -1, pstop = -1;
  linereader_t lr;
  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  char *txt = NULL;
  size_t txtoff = 0;

  const int tag_flags = TEXT_PARSE_HTML_TAGS | TEXT_PARSE_HTML_ENTITIES |
    TEXT_PARSE_SLOPPY_TAGS | TEXT_PARSE_SUB_TAGS;

  srt_skip_preamble(&buf, &len);

  TAILQ_INIT(&es->es_entries);
  linereader_init(&lr, buf, len);
  while(1) {
    if((n = linereader_next(&lr)) < 0)
      break;

    if(get_srt_timestamp(&lr, &start, &stop) == 0) {
      if(txt != NULL && pstart != -1 && pstop != -1) {
	txt[txtoff] = 0;
	es_insert_text(es, txt, pstart, pstop, tag_flags);
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
    es_insert_text(es, txt, pstart, pstop, tag_flags);
  }
  free(txt);
  return es;
}


/**
 *
 */
static int
is_timedtext(buf_t *buf)
{
  if(buf_len(buf) < 30)
    return 0;
  if(memcmp(buf_cstr(buf), "<?xml", 5))
    return 0;
  if(strstr(buf_cstr(buf), "<transcript>") == NULL)
    return 0;
  return 1;
}


/**
 *
 */
static ext_subtitles_t *
load_timedtext(const char *url, buf_t *buf)
{
  char errbuf[256];
  htsmsg_field_t *f;
  htsmsg_t *xml = htsmsg_xml_deserialize_buf(buf, errbuf, sizeof(errbuf));

  if(xml == NULL) {
    TRACE(TRACE_INFO, "Subtitles", "Unable to load timed text: %s", errbuf);
    return NULL;
  }

  htsmsg_t *transcript = htsmsg_get_map_multi(xml, "transcript", NULL);

  if(transcript == NULL) {
    htsmsg_release(xml);
    return NULL;
  }

  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  TAILQ_INIT(&es->es_entries);

  HTSMSG_FOREACH(f, transcript) {
    if(f->hmf_type == HMF_STR && f->hmf_childs != NULL) {
      htsmsg_t *n = f->hmf_childs;
      const char *str;
      int64_t start, end;

      if((str = htsmsg_get_str(n, "start")) == NULL)
	continue;
      start = my_str2double(str, NULL) * 1000000.0;

      if((str = htsmsg_get_str(n, "dur")) == NULL)
	continue;
      end = start + my_str2double(str, NULL) * 1000000.0;

      char *txt = strdup(f->hmf_str);
      html_entities_decode(txt);
      es_insert_text(es, txt, start, end, 0);
      free(txt);
    }
  }
  return es;
}


/**
 *
 */
static int
is_ttml(buf_t *buf)
{
  if(buf_len(buf) < 30)
    return 0;
  if(memcmp(buf_cstr(buf), "<?xml", 5))
    return 0;
  if(strstr(buf_cstr(buf), "http://www.w3.org/2006/10/ttaf1") == NULL)
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
load_ttml(const char *url, buf_t *buf)
{
  char errbuf[256];
  htsmsg_t *subs;
  htsmsg_field_t *f;
  htsmsg_t *xml = htsmsg_xml_deserialize_buf(buf, errbuf, sizeof(errbuf));

  if(xml == NULL) {
    TRACE(TRACE_INFO, "Subtitles", "Unable to load TTML: %s", errbuf);
    return NULL;
  }

  subs = htsmsg_get_map_multi(xml, "tt", "body", "div", NULL);

  if(subs == NULL) {
    htsmsg_release(xml);
    return NULL;
  }

  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  TAILQ_INIT(&es->es_entries);

  HTSMSG_FOREACH(f, subs) {
    if(f->hmf_type == HMF_STR && f->hmf_childs != NULL) {
      htsmsg_t *n = f->hmf_childs;
      const char *str, *txt;
      int64_t start, end;

      txt = f->hmf_str;

      if((str = htsmsg_get_str(n, "begin")) == NULL)
	continue;
      if((start = ttml_time_expression(str)) == -1)
	continue;

      if((str = htsmsg_get_str(n, "end")) == NULL)
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
get_sub_mpl_timestamp(const char *buf, int *start, int *stop,
                      char left, char right)
{
  const char *b = buf;
  if(*b != left)
    return -1;
  *start = strtol(b + 1, (char **)&b, 10);
  if(b[0] != right || b[1] != left)
    return -1;
  *stop = strtol(b + 2, (char **)&b, 10);
  if(b[0] != right)
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
  return get_sub_mpl_timestamp(buf, &start, &stop, '{', '}') != -1;
}


/**
 *
 */
static int
is_mpl(const char *buf, size_t len)
{
  int start, stop;
  return get_sub_mpl_timestamp(buf, &start, &stop, '[', ']') != -1;
}


/**
 *
 */
static ext_subtitles_t *
load_sub_variant(const char *url, char *buf, size_t len, AVRational *fr,
                 int mpl)
{
  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  AVRational sub_default = {25, 1};
  AVRational mpl_default = {10, 1};
  AVRational fr0;

  const char left  = "{["[mpl];
  const char right = "}]"[mpl];

  if(fr == NULL || fr->num == 0 || fr->den == 0)
    fr = mpl ? &mpl_default : &sub_default;

  TAILQ_INIT(&es->es_entries);

  int tagflags = TEXT_PARSE_SUB_TAGS;

  if(mpl)
    tagflags |= TEXT_PARSE_SLASH_PREFIX;

  LINEPARSE(s, buf) {
    int start, stop;
    int x = get_sub_mpl_timestamp(s, &start, &stop, left, right);
    if(x <= 0)
      continue;

    s += x;

    if(!mpl && start == 1 && stop == 1) {
      // Set framerate
      fr0.num = my_str2double(s, NULL) * 1000000.0;
      fr0.den = 1000000;
      fr = &fr0;
      continue;
    }

    for(int i = 0, len = strlen(s); i < len; i++) {
      if(s[i] == '|')
        s[i] = '\n';
    }

    es_insert_text(es, s,
                   1000000LL * start * fr->den / fr->num,
                   1000000LL * stop  * fr->den / fr->num,
                   tagflags);
  }
  return es;
}


/**
 *
 */
static int
is_txt(const char *buf, size_t len)
{
  int x;
  return sscanf(buf, "%02d:%2d:%02d:%02d %02d:%02d:%02d:%02d ",
                &x, &x, &x, &x, &x, &x, &x, &x) == 8;
}


/**
 *
 */
static void
load_txt_line(ext_subtitles_t *es, const char *src, int len,
              unsigned int start, unsigned int stop)
{
  if(len < 24)
    return;

  src += 24;
  len -= 24;

  char *dst = alloca(len + 1);
  const char *txt = dst;

  while(len) {
    if(src[0] < 32)
      break;

    if(src[0] == '/' && src[1] == '/') {
      *dst++ = '\n';
      src += 2;
      len -= 2;
    } else {
      *dst++ = *src++;
      len--;
    }
  }
  *dst = 0;
  es_insert_text(es, txt, start * 10000LL , stop * 10000LL, 0);
}


/**
 *
 */
static ext_subtitles_t *
load_txt(const char *url, char *buf, size_t len)
{
  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  linereader_t lr;
  int n;

  TAILQ_INIT(&es->es_entries);
  linereader_init(&lr, buf, len);

  while(1) {
    if((n = linereader_next(&lr)) < 0)
      break;

    int s[8];

    if(sscanf(lr.buf, "%02d:%2d:%02d:%02d %02d:%02d:%02d:%02d ",
              &s[0], &s[1], &s[2], &s[3], &s[4], &s[5], &s[6], &s[7]) != 8)
      continue;

    unsigned int start = s[0] * 360000 + s[1] * 6000 + s[2] * 100 + s[3];
    unsigned int stop  = s[4] * 360000 + s[5] * 6000 + s[6] * 100 + s[7];
    load_txt_line(es, lr.buf, lr.ll, start, stop);
  }
  return es;
}


/**
 *
 */
static int
is_tmp(const char *buf, size_t len)
{
  int x;
  return sscanf(buf, "%02d:%2d:%02d:", &x, &x, &x) == 3;
}


/**
 *
 */
static void
load_tmp_line(ext_subtitles_t *es, const char *src, int len,
              unsigned int start)
{
  if(len < 9)
    return;

  src += 9;
  len -= 9;

  char *dst = alloca(len + 1);
  const char *txt = dst;

  int delay = len / 14.7f;

  while(len) {
    if(src[0] < 32)
      break;

    if(src[0] == '|') {
      *dst++ = '\n';
      src += 1;
    } else {
      *dst++ = *src++;
    }
    len--;
  }
  if(delay < 2)
    delay = 2;
  *dst = 0;
  es_insert_text(es, txt, start * 1000000LL, (start + delay) * 1000000LL,
                 TEXT_PARSE_SLASH_PREFIX);
}


/**
 *
 */
static ext_subtitles_t *
load_tmp(const char *url, char *buf, size_t len)
{
  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  linereader_t lr;
  int n;

  TAILQ_INIT(&es->es_entries);
  linereader_init(&lr, buf, len);

  while(1) {
    if((n = linereader_next(&lr)) < 0)
      break;

    int s[3];

    if(sscanf(lr.buf, "%02d:%2d:%02d:", &s[0], &s[1], &s[2]) != 3)
      continue;

    unsigned int start = s[0] * 3600 + s[1] * 60 + s[2];
    load_tmp_line(es, lr.buf, lr.ll, start);
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
es_sort(ext_subtitles_t *es, int trim_stop)
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

  if(trim_stop) {
    // Trim so no stop time is higher than next items start time
    for(i = 0; i < cnt - 1; i++)
      vec[i]->vo_stop = MIN(vec[i]->vo_stop, vec[i + 1]->vo_start);
  }

  TAILQ_INIT(&es->es_entries);
  for(i = 0; i < cnt; i++)
    TAILQ_INSERT_TAIL(&es->es_entries, vec[i], vo_link);
  free(vec);
}


/**
 *
 */
static buf_t *
convert_to_utf8(buf_t *src, const char *url)
{
  char how[256];
  buf_t *b = utf8_from_bytes(buf_cstr(src), buf_len(src), NULL,
                             how, sizeof(how));
  TRACE(TRACE_INFO, "Subtitles", "%s is not valid UTF-8. %s", url, how);
  buf_release(src);
  return b;
}


/**
 *
 */
static ext_subtitles_t *
subtitles_create(const char *path, buf_t *buf, AVRational *fr)
{
  int trim_stop = 0;
  ext_subtitles_t *s = NULL;
  if(is_ttml(buf)) {
    s = load_ttml(path, buf);
  } else if(is_timedtext(buf)) {
    s = load_timedtext(path, buf);
  } else {

    const uint8_t *u8 = buf_c8(buf);
    int off = 0;

    if(buf_len(buf) > 2 && ((u8[0] == 0xff && u8[1] == 0xfe) ||
                            (u8[0] == 0xfe && u8[1] == 0xff))) {
      // UTF-16 BOM
      buf = utf16_to_utf8(buf);
    } else if(buf_len(buf) > 3 &&
              u8[0] == 0xef && u8[1] == 0xbb && u8[2] == 0xbf) {
      // UTF-8 BOM
      off = 3;
    } else if(utf8_verify(buf_cstr(buf))) {
      // It's UTF-8 clean
    } else {
      buf = convert_to_utf8(buf, path);
    }

    buf = buf_make_writable(buf);
    char *b0 = buf_str(buf) + off;
    int len  = buf_len(buf) - off;

    if(is_srt(b0, len))
      s = load_srt(path, b0, len);
    else if(is_ass(b0, len))
      s = load_ssa(path, b0, len);
    else if(is_sub(b0, len))
      s = load_sub_variant(path, b0, len, fr, 0);
    else if(is_mpl(b0, len))
      s = load_sub_variant(path, b0, len, NULL, 1);
    else if(is_txt(b0, len))
      s = load_txt(path, b0, len);
    else if(is_tmp(b0, len)) {
      s = load_tmp(path, b0, len);
      trim_stop = 1;
    }
    buf_release(buf);
  }

  if(s)
    es_sort(s, trim_stop);
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
	   int64_t user_time, int64_t user_time_to_pts)
{
  int64_t s = vo->vo_start;
  do {
        es->es_cur = vo;

        video_overlay_t *dup = video_overlay_dup(vo);

        dup->vo_start += user_time_to_pts;
        dup->vo_stop  += user_time_to_pts;

        video_overlay_enqueue(mp, dup);
        //printf("delivery %" PRId64 " %" PRId64 " - %" PRId64 ": %ls\n",user_time,vo->vo_start,vo->vo_stop,(wchar_t*)vo->vo_text);
        vo = TAILQ_NEXT(vo, vo_link);
  } while(vo != NULL && vo->vo_start == s && vo->vo_stop > user_time);
}


/**
 *
 */
void
subtitles_pick(ext_subtitles_t *es, int64_t user_time, int64_t pts,
               media_pipe_t *mp)
{
  video_overlay_t *vo = es->es_cur;

  if(es->es_picker)
    return es->es_picker(es, pts);

  int64_t user_time_to_pts = pts - user_time;
  while(vo != NULL) {
    vo = TAILQ_NEXT(vo, vo_link);
    if(vo != NULL && vo->vo_start <= user_time && vo->vo_stop > user_time) {
      vo_deliver(es, vo, mp, user_time, user_time_to_pts);
      return;
    }
    if(vo != NULL && vo->vo_start > user_time)
      break;
  }

  vo = es->es_cur;

  if(vo != NULL && vo->vo_start <= user_time && vo->vo_stop > user_time) {
    return; // Already sent
  }

  TAILQ_FOREACH(vo, &es->es_entries, vo_link) {
    if(vo->vo_start <= user_time && vo->vo_stop > user_time && vo->vo_start>user_time-1000000) {//don't re-delivery long standing item
      vo_deliver(es, vo, mp, user_time, user_time_to_pts);
      return;
    }
    if(vo->vo_start>user_time)
      break;
  }
  es->es_cur = NULL;
}


static ext_subtitles_t *
subtitles_from_zipfile(media_pipe_t *mp, buf_t *b)
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
      ret = subtitles_load(mp, rstr_get(fde->fde_url));
      if(ret != NULL)
	break;
    }
    fa_dir_free(fd);
  } else {
    TRACE(TRACE_ERROR, "Subtitles", "Unable to open ZIP -- %s", errbuf);
  }

  memfile_unregister(id);
  buf_release(b);
  return ret;
}


/**
 *
 */
ext_subtitles_t *
subtitles_load(media_pipe_t *mp, const char *url)
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

  buf_t *b = fa_load(url,
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "Subtitles", "Unable to load %s -- %s", 
	  url, errbuf);
    return NULL;
  }

  if(b->b_size > 4 && !memcmp(buf_cstr(b), "PK\003\004", 4)) {
    TRACE(TRACE_DEBUG, "Subtitles", "%s is a ZIP archive, scanning...", url);
    return subtitles_from_zipfile(mp, b);
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

  uint8_t header[64];
  memcpy(header, b->b_ptr, MIN(b->b_size, 64));
  int size = b->b_size;

  sub = subtitles_create(url, b, &mp->mp_framerate);
  if(sub == NULL) {
    TRACE(TRACE_ERROR, "Subtitles",
	  "Unable to load %s -- Unknown format (%d bytes), dump of first 64 bytes follows",
	  url, size);
    hexdump("Subtitles", header, MIN(size, 64));
  } else {
    TRACE(TRACE_DEBUG, "Subtitles", "Loaded %s OK", url);
  }
  return sub;
}



/**
 *
 */
const char *
subtitles_probe(const char *url)
{
  const char *ret;
  buf_t *b = fa_load(url, NULL);

  if(is_txt(buf_cstr(b), buf_len(b)))
    ret = "TXT";
  else if(is_mpl(buf_cstr(b), buf_len(b)))
    ret = "MPL";
  else if(is_sub(buf_cstr(b), buf_len(b)))
    ret = "SUB";
  else if(is_tmp(buf_cstr(b), buf_len(b)))
    ret = "TMP";
  else if(is_timedtext(b))
    ret = "TimedText";
  else
    ret = NULL;

  buf_release(b);
  return ret;
}
