/*
 *  Image loader, used by libglw
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
#include <errno.h>

#include <libavformat/avformat.h>
#include <libavutil/avstring.h>


#ifdef HAVE_LIBEXIF
#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-loader.h>
#endif

#include "showtime.h"
#include "fileaccess.h"
#include "fa_imageloader.h"

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
/**
 *
 */
static const char *
filename_by_url(const char *url)
{
  static char fname[300];
  char *ls;
  struct stat st;
  CURL *ch;
  FILE *fp;
  static const char *dlcache;
  
  if(dlcache == NULL)
    dlcache = "/tmp/showtime_dlcache"; 
  /* config_get_str("dlcache", "/tmp/showtime_dlcache"); */

  mkdir(dlcache, 0777);

  ls = strrchr(url, '/');
  if(ls == NULL)
    return NULL;

  ls++;

  snprintf(fname, sizeof(fname), "%s/%s", dlcache, ls);

  if(stat(fname, &st) == -1) {

    fp = fopen(fname, "w+");
    if(fp == NULL) 
      return NULL;
    ch = curl_easy_init();
    curl_easy_setopt(ch, CURLOPT_URL, url);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, fp);
    
    curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 5);
    curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1);
    
    curl_easy_perform(ch);
    curl_easy_cleanup(ch);
    fclose(fp);
  }

  snprintf(fname, sizeof(fname), "file://%s/%s", dlcache, ls);
  return fname;
}
#endif


/**
 *
 */
int
fa_imageloader(fa_image_load_ctrl_t *ctrl)
{
  fa_protocol_t *fap;
  void *fh;
  const char *filename = ctrl->url;
  char p[16];
  int is_exif = 0;
  int r;

  if(!strncmp(filename, "thumb://", 8)) {
    filename = filename + 8;
    ctrl->want_thumb = 1;
  }

#ifdef HAVE_LIBCURL
  if(!strncmp(filename, "http://", 7))
    filename = filename_by_url(filename);
#endif

  if(filename == NULL)
    return -1;

  if((filename = fa_resolve_proto(filename, &fap)) == NULL)
    return -1;

  if((fh = fap->fap_open(filename)) == NULL)
    return -1;

  if(fap->fap_read(fh, p, sizeof(p)) != sizeof(p)) {
    fprintf(stderr, "%s: file too short\n", filename);
    fap->fap_close(fh);
    return -1;
  }

  /* figure format */

  if(p[6] == 'J' && p[7] == 'F' && p[8] == 'I' && p[9] == 'F') {
    ctrl->codecid = CODEC_ID_MJPEG;
  } else if(p[6] == 'E' && p[7] == 'x' && p[8] == 'i' && p[9] == 'f') {
    ctrl->codecid = CODEC_ID_MJPEG;
    is_exif = 1;
  } else if(!memcmp(pngsig, p, 8)) {
    ctrl->codecid = CODEC_ID_PNG;
  } else {
    fprintf(stderr, "%s: format not known\n", filename);
    fap->fap_close(fh);
    return -1;
  }
  

#ifdef HAVE_LIBEXIF
  if(is_exif && ctrl->want_thumb) {
    unsigned char exifbuf[1024];
    int v, x;
    ExifLoader *l;
    ExifData *ed;

    l = exif_loader_new();

    v = exif_loader_write(l, (unsigned char *)p, sizeof(p));
    while(v) {
      if((x = fap->fap_read(fh, exifbuf, sizeof(exifbuf))) < 1)
	break;
      v = exif_loader_write(l, exifbuf, x);
    }

    ed = exif_loader_get_data(l);
    exif_loader_unref (l);

    if(ed != NULL && ed->data != NULL) {
      fap->fap_close(fh);
      ctrl->data = malloc(ed->size);
      memcpy(ctrl->data, ed->data, ed->size);
      ctrl->datasize = ed->size;
      exif_data_unref(ed);
      ctrl->got_thumb = 1;
      return 0;
    }
  }
#endif
  fap->fap_seek(fh, SEEK_SET, 0);

  ctrl->datasize = fap->fap_fsize(fh);
  ctrl->data = malloc(ctrl->datasize);
  
  r = fap->fap_read(fh, ctrl->data, ctrl->datasize);

  fap->fap_close(fh);

  if(r != ctrl->datasize) {
    free(ctrl->data);
    return -1;
  }
  return 0;
}
