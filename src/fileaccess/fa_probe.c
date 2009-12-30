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

#include "showtime.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "navigator.h"
#include "scrappers/scrappers.h"


#define METADATA_HASH_SIZE 101
#define METADATA_CACHE_SIZE 1000

LIST_HEAD(metadata_list, metadata);
TAILQ_HEAD(metadata_queue, metadata);

static struct metadata_queue metadata_entries;
static int metadata_nentries;
static struct metadata_list metadata_hash[METADATA_HASH_SIZE];
static hts_mutex_t metadata_mutex;

/**
 *
 */
typedef struct metadata {
  char *md_url;
  time_t md_mtime;

  char *md_redirect;

  LIST_ENTRY(metadata) md_hash_link;
  TAILQ_ENTRY(metadata) md_queue_link;

  int md_type;
  float md_duration;
  int md_tracks;
  time_t md_time;

  rstr_t *md_title;
  rstr_t *md_album;
  rstr_t *md_artist;
  rstr_t *md_format;
  rstr_t *md_videoinfo;
  rstr_t *md_audioinfo;

} metadata_t;




static void
metadata_clean(metadata_t *md)
{
  rstr_release(md->md_title);
  rstr_release(md->md_album);
  rstr_release(md->md_artist);
  rstr_release(md->md_format);
  rstr_release(md->md_videoinfo);
  rstr_release(md->md_audioinfo);

  free(md->md_url);
  free(md->md_redirect);
}


/**
 *
 */
static void
metadata_destroy(metadata_t *md)
{
  metadata_clean(md);
  TAILQ_REMOVE(&metadata_entries, md, md_queue_link);
  LIST_REMOVE(md, md_hash_link);
  free(md);
}



static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
static const uint8_t isosig[8] = {0x1, 0x43, 0x44, 0x30, 0x30, 0x31, 0x1, 0x0};
static const uint8_t gifsig[6] = {'G', 'I', 'F', '8', '9', 'a'};


/**
 *
 */
static char *
ffmpeg_metadata_get(AVMetadata *m, const char *key)
{
  AVMetadataTag *tag;
  int len;
  char *ret;
  const char *str;
  
  if((tag = av_metadata_get(m, key, NULL, AV_METADATA_IGNORE_SUFFIX)) == NULL)
    return NULL;

  str = tag->value;
  len = strlen(str);
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
  if(*ret == 0 || !strncasecmp(ret, "http://", 7)) {
    free(ret);
    return NULL;
  }
  return ret;
}


/**
 *
 */
static rstr_t *
ffmpeg_metadata_get_str(AVMetadata *m, const char *key)
{
  return rstr_alloc(ffmpeg_metadata_get(m, key));
}

/**
 * Obtain details from playlist
 */
static void
fa_probe_playlist(metadata_t *md, const char *url, uint8_t *pb, size_t pbsize)
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
  
  md->md_title = rstr_alloc(tmp1);
  
  t = strstr((char *)pb, "NumberOfEntries=");
  
  if(t != NULL)
    md->md_tracks = atoi(t + 16);
}


/**
 * Probe SPC files
 */
static void
fa_probe_spc(metadata_t *md, uint8_t *pb)
{
  char buf[33];
  buf[32] = 0;

  if(memcmp("v0.30", pb + 0x1c, 4))
    return;

  if(pb[0x23] != 0x1a)
    return;

  memcpy(buf, pb + 0x2e, 32);
  md->md_title = rstr_alloc(buf);

  memcpy(buf, pb + 0x4e, 32);
  md->md_album = rstr_alloc(buf);

  memcpy(buf, pb + 0xa9, 3);
  buf[3] = 0;

  md->md_duration = atoi(buf);
}


/**
 * 
 */
static void
metdata_set_redirect(metadata_t *md, const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);

  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  md->md_redirect = strdup(buf);
}


/**
 * Probe file by checking its header
 */
static int
fa_probe_header(metadata_t *md, const char *url, fa_handle_t *fh)
{
  uint8_t pb[256];
  off_t psiz;
  uint16_t flags;

  memset(pb, 0, sizeof(pb));
  psiz = fa_read(fh, pb, sizeof(pb));

  if(psiz == 256 && 
     !memcmp(pb, "SNES-SPC700 Sound File Data", 27)) {
    fa_probe_spc(md, pb); 
    md->md_type = CONTENT_AUDIO;
    return 1;
  }

  if(pb[0] == 'R'  && pb[1] == 'a'  && pb[2] == 'r' && pb[3] == '!' &&
     pb[4] == 0x1a && pb[5] == 0x07 && pb[6] == 0x0 && pb[9] == 0x73) {

    flags = pb[10] | pb[11] << 8;
    if((flags & 0x101) == 1) {
      /* Don't include slave volumes */
      md->md_type = CONTENT_UNKNOWN;
      return 1;
    }

    metdata_set_redirect(md, "rar://%s|", url);
    md->md_type = CONTENT_ARCHIVE;
    return 1;
  }

  if(pb[0] == 0x50 && pb[1] == 0x4b && pb[2] == 0x03 && pb[3] == 0x04) {
    metdata_set_redirect(md, "zip://%s|", url);
    md->md_type = CONTENT_ARCHIVE;
    return 1;
  }

  if(!strncasecmp((char *)pb, "[playlist]", 10)) {
    /* Playlist */
    fa_probe_playlist(md, url, pb, sizeof(pb));
    md->md_type = CONTENT_PLAYLIST;
    return 1;
  }

  if(pb[6] == 'J' && pb[7] == 'F' && pb[8] == 'I' && pb[9] == 'F') {
    /* JPEG image */
    md->md_type = CONTENT_IMAGE;
    return 1;
  }

  if(pb[6] == 'E' && pb[7] == 'x' && pb[8] == 'i' && pb[9] == 'f') {
    md->md_type = CONTENT_IMAGE;
    return 1;
  }

  if(!memcmp(pb, pngsig, 8)) {
    /* PNG */
    md->md_type = CONTENT_IMAGE;
    return 1;
  }

  if(!memcmp(pb, gifsig, sizeof(gifsig))) {
    /* GIF */
    md->md_type = CONTENT_IMAGE;
    return 1;
  }
  return 0;
}

/**
 * Check if file is an iso image
 */
int
fa_probe_iso(metadata_t *md, fa_handle_t *fh)
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

  if(md != NULL)
    md->md_title = rstr_alloc(pb + 40);
  return 0;
}


/**
 * 
 */
static void
fa_lavf_load_meta(metadata_t *md, AVFormatContext *fctx, const char *url)
{
  int i;
  AVCodecContext *avctx;
  AVCodec *codec;
  const char *t;
  char tmp1[1024];
  char *p;
  int has_video = 0;
  int has_audio = 0;

  av_metadata_conv(fctx, NULL, fctx->iformat->metadata_conv);

  if(md != NULL) {

    /* Format meta info */

    if(fctx->title[0] == 0) {
      t = strrchr(url, '/');
      t = t ? t + 1 : url;
      i = strlen(t);
      p = alloca(i + 1);
      memcpy(p, t, i + 1);
    
      if(i > 4 && p[i - 4] == '.')
	p[i - 4] = 0;

      md->md_title = rstr_alloc(p);
    } else {
      md->md_title = ffmpeg_metadata_get_str(fctx->metadata, "title");
    }

    md->md_artist = ffmpeg_metadata_get_str(fctx->metadata, "artist") ?:
      ffmpeg_metadata_get_str(fctx->metadata, "author");

    md->md_album = ffmpeg_metadata_get_str(fctx->metadata, "album");

    md->md_format = rstr_alloc(fctx->iformat->long_name);

    if(fctx->duration != AV_NOPTS_VALUE)
      md->md_duration = (float)fctx->duration / 1000000;
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

    if(md != NULL) {

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
	if(md->md_videoinfo == NULL)
	  md->md_videoinfo = rstr_alloc(tmp1);
	break;
      case CODEC_TYPE_AUDIO:
	if(md->md_audioinfo == NULL)
	  md->md_audioinfo = rstr_alloc(tmp1);
	break;
      
      default:
	continue;
      }
    }
  }
  
  md->md_type = CONTENT_FILE;
  if(has_video)
    md->md_type = CONTENT_VIDEO;
  else if(has_audio)
    md->md_type = CONTENT_AUDIO;
}
  

/**
 *
 */
static int
fa_probe_fill_cache(metadata_t *md, const char *url, char *errbuf, 
		    size_t errsize)
{
  const char *url0 = url;
  AVFormatContext *fctx;
  char tmp1[1024];
  fa_handle_t *fh;

  if((fh = fa_open(url, errbuf, errsize)) == NULL)
    return -1;

  if(fa_probe_header(md, url0, fh)) {
    fa_close(fh);
    return 0;
  }

  if(!fa_probe_iso(md, fh)) {
    fa_close(fh);
    return 0;
  }

  fa_close(fh);

  /* Okay, see if lavf can find out anything about the file */

  snprintf(tmp1, sizeof(tmp1), "showtime:%s", url0);

  if(av_open_input_file(&fctx, tmp1, NULL, 0, NULL) != 0) {
    snprintf(errbuf, errsize, "Unable to open file (ffmpeg)");
    return -1;
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    snprintf(errbuf, errsize, "Unable to handle file contents");
    return -1;
  }

  fa_lavf_load_meta(md, fctx, url);
  av_close_input_file(fctx);  
  return 0;
}


/**
 *
 */
static int
fa_probe_set_from_cache(const metadata_t *md, prop_t *proproot, 
			char *newurl, size_t newurlsize)
{
  if(md->md_redirect != NULL && newurl != NULL)
    av_strlcpy(newurl, md->md_redirect, newurlsize);

  if(md->md_title)
    prop_set_rstring(prop_create(proproot, "title"),  md->md_title);

  if(md->md_artist)
    prop_set_rstring(prop_create(proproot, "artist"), md->md_artist);
  
  if(md->md_album)
    prop_set_rstring(prop_create(proproot, "album"),  md->md_album);

  if(md->md_audioinfo)
    prop_set_rstring(prop_create(proproot, "audioinfo"),  md->md_audioinfo);

  if(md->md_videoinfo)
    prop_set_rstring(prop_create(proproot, "videoinfo"),  md->md_videoinfo);

  if(md->md_format)
    prop_set_rstring(prop_create(proproot, "format"),  md->md_format);

  if(md->md_duration)
    prop_set_float(prop_create(proproot, "duration"),  md->md_duration);

  if(md->md_tracks)
    prop_set_int(prop_create(proproot, "tracks"),  md->md_tracks);

  return md->md_type;
}




/**
 * Probe a file for its type
 */
unsigned int
fa_probe(prop_t *proproot, const char *url, char *newurl, size_t newurlsize,
	 char *errbuf, size_t errsize, struct stat *st)
{
  struct stat st0;
  unsigned int hash, r;
  metadata_t *md;

  if(st  == NULL) {
    if(fa_stat(url, &st0, errbuf, errsize))
      return CONTENT_UNKNOWN;
    st = &st0;
  }

  hash = mystrhash(url) % METADATA_HASH_SIZE;

  hts_mutex_lock(&metadata_mutex);
  
  LIST_FOREACH(md, &metadata_hash[hash], md_hash_link)
    if(md->md_mtime == st->st_mtime && !strcmp(md->md_url, url))
      break;

  if(md != NULL) {
    TAILQ_REMOVE(&metadata_entries, md, md_queue_link);
    TAILQ_INSERT_TAIL(&metadata_entries, md, md_queue_link);

  } else {
    if(metadata_nentries == METADATA_CACHE_SIZE) {
      md = TAILQ_FIRST(&metadata_entries);
      metadata_destroy(md);
    }

    md = calloc(1, sizeof(metadata_t));
    LIST_INSERT_HEAD(&metadata_hash[hash], md, md_hash_link);
    TAILQ_INSERT_TAIL(&metadata_entries, md, md_queue_link);
    md->md_mtime = st->st_mtime;
    md->md_url = strdup(url);

    if(fa_probe_fill_cache(md, url, errbuf, errsize)) {
      metadata_destroy(md);
      hts_mutex_unlock(&metadata_mutex);
      return CONTENT_UNKNOWN;
    }
  }

  r = fa_probe_set_from_cache(md, proproot, newurl, newurlsize);

  hts_mutex_unlock(&metadata_mutex);
  return r;
}


/**
 *
 */
void
fa_probe_load_metaprop(prop_t *p, AVFormatContext *fctx, const char *url)
{
  metadata_t md = {0};

  fa_lavf_load_meta(&md, fctx, url);
  fa_probe_set_from_cache(&md, p, NULL, 0);
  metadata_clean(&md);
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

  type = CONTENT_DIR;

  snprintf(path, sizeof(path), "%s/VIDEO_TS", url);
  if(fa_stat(path, &buf, NULL, 0) == 0 && S_ISDIR(buf.st_mode)) {
    type = CONTENT_DVD;
  } else {
    snprintf(path, sizeof(path), "%s/video_ts", url);
    if(fa_stat(path, &buf, NULL, 0) == 0 && S_ISDIR(buf.st_mode))
      type = CONTENT_DVD;
  }

  return type;
}


/**
 *
 */
void
fa_probe_init(void)
{
  TAILQ_INIT(&metadata_entries);
  hts_mutex_init(&metadata_mutex);
}
