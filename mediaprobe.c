/*
 *  Functions for probing file contents
 *  Copyright (C) 2007 Andreas Öman
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

#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <ffmpeg/common.h>

#include "showtime.h"
#include "mediaprobe.h"

/*
 *
 */

static char *
strcpy_and_trim(char *str)
{
  int len = strlen(str);
  char *ret;

  if(len == 0)
    return NULL;

  ret = malloc(len + 1);

  memcpy(ret, str, len);
  ret[len] = 0;

  while(len > 0) {
    len--;
    if(ret[len] <= ' ' || ret[len] == '-')
      ret[len] = 0;
    else
      break;
  }
  return ret;
}


static const char *
utf8dup(const char *in)
{
  int ol = 0;
  const char *x = in;
  uint8_t tmp;
  char *r, *y;

  while(*x) {
    PUT_UTF8(*x, tmp, ol++;);
    x++;
  }

  y = r = malloc(ol + 1);
  x = in;
  while(*x) {
    PUT_UTF8(*x, tmp, *y++ = tmp;);
    x++;
  }
  *y = 0;
  return (const char *)r;
}

/*
 *
 */

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

int 
mediaprobe(const char *filename, mediainfo_t *mi, int fast)
{
  int i, fd;
  //  rpc_t *rpc = &b->b_rpc;
  //  static const char *midhost;
  AVFormatContext *fctx;
  const char *t;
  char probebuf[128];
  char tmp1[300];
  char *p;

  fd = open(filename, O_RDONLY);
  if(fd == -1)
    return 1;

  i = read(fd, probebuf, sizeof(probebuf));

  if(i == sizeof(probebuf)) {
    if(!strncasecmp(probebuf, "[playlist]", 10)) {

      probebuf[sizeof(probebuf) - 1] = 0;
      mi->mi_type = MI_PLAYLIST_PLS;

      t = strrchr(filename, '/');
      t = t ? t + 1 : filename;

      i = 0;
      while(*t && *t != '.')
	tmp1[i++] = *t++;
      tmp1[i] = 0;

      mi->mi_title = strdup(tmp1);

      t = strstr(probebuf, "NumberOfEntries=");
      mi->mi_track = t != NULL ? atoi(t + 16) : 0;
      close(fd);
      return 0;
    }

    if(
       (probebuf[6] == 'J' && probebuf[7] == 'F' &&
	probebuf[8] == 'I' && probebuf[9] == 'F') ||
       (probebuf[6] == 'E' && probebuf[7] == 'x' &&
	probebuf[8] == 'i' && probebuf[9] == 'f') ||
       !memcmp(probebuf, pngsig, 8)) {

      mi->mi_type = MI_IMAGE;
      mi->mi_title = strdup(filename);
      close(fd);
      return 0;
    }
  }

  if(fast) {
    close(fd);
    return 1;
  }

  if(lseek(fd, 0x8000, SEEK_SET) == 0x8000) {
    i = read(fd, probebuf, sizeof(probebuf));
    if(i == sizeof(probebuf)) {
      if(probebuf[0] == 0x01 &&
	 probebuf[1] == 0x43 &&
	 probebuf[2] == 0x44 &&
	 probebuf[3] == 0x30 &&
	 probebuf[4] == 0x30 &&
	 probebuf[5] == 0x31 &&
	 probebuf[6] == 0x01 &&
	 probebuf[7] == 0x00) {

	p = &probebuf[40];
	while(*p > 32 && p != &probebuf[72]) {
	  p++;
	}
	*p = 0;

	mi->mi_type = MI_ISO;
	mi->mi_title = strdup(&probebuf[40]);
	close(fd);
	return 0;
      }
    }
  }
  


  close(fd);

  /* Okay, see if lavc can find out anything about the file */

  fflock();
  
  if(av_open_input_file(&fctx, filename, NULL, 0, NULL) != 0) {
    ffunlock();
    return 1;
  }

  
  dump_format(fctx, 0, filename, 0);

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    ffunlock();
    return 1;
  }

  mi->mi_type = MI_AUDIO;

  for(i = 0; i < fctx->nb_streams; i++) {
    if(fctx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO)
      mi->mi_type = MI_VIDEO;
  }

  if(fctx->title[0] == 0) {
    t = strrchr(filename, '/');
    t = t ? t + 1 : filename;
    i = strlen(t);
    p = alloca(i + 1);
    memcpy(p, t, i + 1);
    
    if(i > 4 && p[i - 4] == '.')
      p[i - 4] = 0;
    mi->mi_title = utf8dup(p);
  } else {
    mi->mi_title = strcpy_and_trim(fctx->title);
  }
  mi->mi_author = strcpy_and_trim(fctx->author);
  mi->mi_album = strcpy_and_trim(fctx->album);

  mi->mi_track = fctx->track;
  if(fctx->duration == AV_NOPTS_VALUE) {
    mi->mi_duration = 0;
  } else {
    mi->mi_duration = fctx->duration / AV_TIME_BASE;
  }
  av_close_input_file(fctx);  
  ffunlock();

  return 0;
}


void 
mediaprobe_free(mediainfo_t *mi)
{
  free((void *)mi->mi_title);
  free((void *)mi->mi_author);
  free((void *)mi->mi_album);
}


void 
mediaprobe_dup(mediainfo_t *dst, mediainfo_t *src)
{
  dst->mi_type = src->mi_type;
  dst->mi_track = src->mi_track;
  dst->mi_duration = src->mi_duration;
  
  dst->mi_title = src->mi_title ? strdup(src->mi_title) : NULL;
  dst->mi_author = src->mi_author ? strdup(src->mi_author) : NULL;
  dst->mi_album = src->mi_album ? strdup(src->mi_album) : NULL;
}
