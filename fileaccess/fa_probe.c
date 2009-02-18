/*
 *  Functions for probing file contents
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

#include "config.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <libavutil/avstring.h>

#ifdef CONFIG_LIBEXIF
#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-loader.h>
#endif

#include "showtime.h"
#include "fileaccess.h"
#include "fa_probe.h"

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
static const uint8_t isosig[8] = {0x1, 0x43, 0x44, 0x30, 0x30, 0x31, 0x1, 0x0};

/**
 *
 */
static void
lavf_build_string_and_trim(prop_t *p, const char *pname, const char *str)
{
  int len = strlen(str);
  char *ret;

  if(len == 0)
    return;

  ret = alloca(len + 1);

  memcpy(ret, str, len);
  ret[len] = 0;

  while(len > 0) {
    len--;
    if(ret[len] <= ' ' || ret[len] == '-')
      ret[len] = 0;
    else
      break;
  }
  if(*ret == 0)
    return;

  prop_set_string(prop_create(p, pname), ret);
}

/**
 * Obtain details from playlist
 */
static void
fa_probe_playlist(prop_t *proproot, const char *url,
		  char *pb, size_t pbsize)
{
  const char *t;
  char tmp1[300];
  int i;

  t = strrchr(url, '/');
  t = t ? t + 1 : url;

  i = 0;
  while(*t && *t != '.')
    tmp1[i++] = *t++;
  tmp1[i] = 0;
  
  prop_set_string(prop_create(proproot, "title"), tmp1);
  
  t = strstr(pb, "NumberOfEntries=");
  
  if(t != NULL)
    prop_set_int(prop_create(proproot, "ntracks"), atoi(t + 16));
}

/**
 * Extract details from EXIF header
 */
#ifdef CONFIG_LIBEXIF
static void
fa_probe_exif(prop_t *proproot, fa_handle_t *fh, char *pb, size_t pbsize)
{
  unsigned char buf[4096];
  int x, v;
  ExifLoader *l;
  ExifData *ed;
  ExifEntry *e;

  l = exif_loader_new();

  v = exif_loader_write(l, (unsigned char *)pb, pbsize);
  while(v) {
    if((x = fa_read(fh, buf, sizeof(buf))) < 1)
      break;
    v = exif_loader_write(l, buf, x);
  }

  ed = exif_loader_get_data(l);
  exif_loader_unref (l);
  if(ed == NULL)
    return;

  e = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF],
			     EXIF_TAG_DATE_TIME_ORIGINAL);
  if(e != NULL) {
    char tid[100];
    struct tm tm;
    time_t t;
    
    exif_entry_get_value(e, tid, sizeof(tid));

    if(sscanf(tid, "%04d:%02d:%02d %02d:%02d:%02d",
	      &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
	      &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
      tm.tm_year -= 1900;
      tm.tm_mon--;
      t = mktime(&tm);
      if(t != (time_t)-1) {
	prop_set_int(prop_create(proproot, "date"), t);
      }
    }
  }
  exif_data_unref(ed);
}
#endif


/**
 * Probe file by checking its header
 */
static int
fa_probe_header(prop_t *proproot, const char *url, fa_handle_t *fh,
		char *newurl, size_t newurlsize)
{
  char pb[128];
  off_t psiz;
  uint16_t flags;

  memset(pb, 0, sizeof(pb));
  psiz = fa_read(fh, pb, sizeof(pb));

  if(pb[0] == 'R'  && pb[1] == 'a'  && pb[2] == 'r' && pb[3] == '!' &&
     pb[4] == 0x1a && pb[5] == 0x07 && pb[6] == 0x0 && pb[9] == 0x73) {

    flags = pb[10] | pb[11] << 8;
    if((flags & 0x101) == 1)
      return FA_UNKNOWN; /* Don't include slave volumes */


    if(newurl != NULL)
      snprintf(newurl, newurlsize, "rar://%s|", url);
    return FA_ARCHIVE;
  }

  if(pb[0] == 0x50 && pb[1] == 0x4b && pb[2] == 0x03 && pb[3] == 0x04) {
    if(newurl != NULL)
      snprintf(newurl, newurlsize, "zip://%s|", url);
    return FA_ARCHIVE;
  }

  if(!strncasecmp(pb, "[playlist]", 10)) {
    /* Playlist */
    if(proproot != NULL)
      fa_probe_playlist(proproot, url, pb, sizeof(pb));
    return FA_PLAYLIST;
  }

  if(pb[6] == 'J' && pb[7] == 'F' && pb[8] == 'I' && pb[9] == 'F') {
    /* JPEG image */
    return FA_IMAGE;
  }

  if(pb[6] == 'E' && pb[7] == 'x' && pb[8] == 'i' && pb[9] == 'f') {
    /* JPEG image with EXIF tag*/
#ifdef CONFIG_LIBEXIF
    if(proproot != NULL)
      fa_probe_exif(proproot, fh, pb, psiz);
#endif
    return FA_IMAGE;
  }

  if(!memcmp(pb, pngsig, 8)) {
    /* PNG */
    return FA_IMAGE;
  }
  return -1;
}

/**
 * Check if file is an iso image
 */
int
fa_probe_iso(prop_t *proproot, fa_handle_t *fh)
{
  char pb[128], *p;

  if(fa_seek(fh, 0x8000, SEEK_SET) != 0x8000)
    return -1;

  if(fa_read(fh, pb, sizeof(pb)) != sizeof(pb))
    return -1;

  if(memcmp(pb, isosig, 8))
    return -1;

  p = &pb[40];
  while(*p > 32 && p != &pb[72])
    p++;

  *p = 0;

  if(proproot != NULL)
    prop_set_string(prop_create(proproot, "title"), &pb[40]);
  return 0;
}
  



const char *type2str[] = {
  [FA_DIR]      = "directory",
  [FA_FILE]     = "file",
  [FA_AUDIO]    = "audio",
  [FA_ARCHIVE]  = "archive",
  [FA_AUDIO]    = "audio",
  [FA_VIDEO]    = "video",
  [FA_PLAYLIST] = "playlist",
  [FA_DVD]      = "dvd",
  [FA_IMAGE]    = "image",
};


void
fa_set_type(prop_t *proproot, unsigned int type)
{
  if(type < sizeof(type2str) / sizeof(type2str[0]) && type2str[type] != NULL)
    prop_set_string(prop_create(proproot, "type"), type2str[type]);
}



/**
 * 
 */
unsigned int
fa_lavf_load_meta(prop_t *proproot, AVFormatContext *fctx, const char *url)
{
  int i;
  AVCodecContext *avctx;
  AVCodec *codec;
  const char *t;
  char tmp1[1024];
  char *p;
  int has_video = 0;
  int has_audio = 0;

  if(proproot != NULL) {

    /* Format meta info */

    if(fctx->title[0] == 0) {
      t = strrchr(url, '/');
      t = t ? t + 1 : url;
      i = strlen(t);
      p = alloca(i + 1);
      memcpy(p, t, i + 1);
    
      if(i > 4 && p[i - 4] == '.')
	p[i - 4] = 0;
      prop_set_string(prop_create(proproot, "title"), p);
    } else {
      lavf_build_string_and_trim(proproot, "title", fctx->title);
    }

    lavf_build_string_and_trim(proproot, "author", fctx->author);
    lavf_build_string_and_trim(proproot, "album", fctx->album);

    if(fctx->track != 0)
      prop_set_int(prop_create(proproot, "track"), fctx->track);

    prop_set_string(prop_create(proproot, "mediaformat"),
		    fctx->iformat->long_name);

    if(fctx->duration != AV_NOPTS_VALUE)
      prop_set_float(prop_create(proproot, "duration"), 
		     (float)fctx->duration / 1000000);
  }

  /* Check each stream */

  for(i = 0; i < fctx->nb_streams; i++) {
    avctx = fctx->streams[i]->codec;
    codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type) {
    case CODEC_TYPE_VIDEO:
      has_video = !!codec;
      break;
    case CODEC_TYPE_AUDIO:
      has_audio = !!codec;
      break;
      
    default:
      continue;
    }

    if(proproot != NULL) {

      if(codec == NULL) {
	snprintf(tmp1, sizeof(tmp1), "Unsupported codec");
      } else {
	snprintf(tmp1, sizeof(tmp1), "%s", codec->long_name);

	if(avctx->codec_type == CODEC_TYPE_AUDIO) {
	  snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
		   ", %d Hz, %d chanels", avctx->sample_rate, avctx->channels);
	}

	if(avctx->width)
	  snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
		   ", %dx%d",
		   avctx->width, avctx->height);
      
	if(avctx->bit_rate)
	  snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
		   ", %d kb/s", avctx->bit_rate / 1000);
      }

      switch(avctx->codec_type) {
      case CODEC_TYPE_VIDEO:
	prop_set_string(prop_create(proproot, "videoinfo"), tmp1);
	break;
      case CODEC_TYPE_AUDIO:
	prop_set_string(prop_create(proproot, "audioinfo"), tmp1);
	break;
      
      default:
	continue;
      }
    }
  }

  if(has_video)
    return FA_VIDEO;
  else if(has_audio)
    return FA_AUDIO;

  return FA_FILE;
}

/**
 * Probe a file for its type
 */
unsigned int
fa_probe(prop_t *proproot, const char *url, char *newurl, size_t newurlsize)
{
  int r;
  AVFormatContext *fctx;
  char tmp1[1024];
  const char *url0 = url;
  fa_handle_t *fh;
  int type;

  if((fh = fa_open(url)) == NULL)
    return FA_UNKNOWN;

  if((r = fa_probe_header(proproot, url0, fh, newurl, newurlsize)) != -1) {
    fa_close(fh);
    fa_set_type(proproot, r);
    return r;
  }

  if(fa_probe_iso(proproot, fh) == 0) {
    fa_close(fh);
    fa_set_type(proproot, FA_DVD);
    return FA_DVD;
  }

  fa_close(fh);

  /* Okay, see if lavf can find out anything about the file */

  snprintf(tmp1, sizeof(tmp1), "showtime:%s", url0);

  fflock();
  
  if(av_open_input_file(&fctx, tmp1, NULL, 0, NULL) != 0) {
    ffunlock();
    return FA_UNKNOWN;
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    ffunlock();
    return FA_UNKNOWN;
  }

  type = fa_lavf_load_meta(proproot, fctx, url);

  av_close_input_file(fctx);  
  ffunlock();

  fa_set_type(proproot, type);
  return type;
}

/**
 * Probe a directory
 */
unsigned int
fa_probe_dir(prop_t *proproot, const char *url)
{
  char path[300];
  struct stat buf;
  int type;

  type = FA_DIR;

  snprintf(path, sizeof(path), "%s/VIDEO_TS", url);
  if(fa_stat(path, &buf) == 0 && S_ISDIR(buf.st_mode)) {
    type = FA_DVD;
  } else {
    snprintf(path, sizeof(path), "%s/video_ts", url);
    if(fa_stat(path, &buf) == 0 && S_ISDIR(buf.st_mode))
      type = FA_DVD;
  }

  if(proproot != NULL)
    fa_set_type(proproot, type);
  return type;
}
