/*
 *  File browser
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "showtime.h"
#include "fileaccess.h"
#include "filebundle.h"

#include "fa_proto.h"

struct filebundle *filebundles;


 typedef struct fa_bundle_fh {
  fa_handle_t h;
  const unsigned char *ptr;
  int size;
  int pos;
} fa_bundle_fh_t;


static const struct filebundle_entry *
resolve_file(const char *url)
{
  struct filebundle *fb;
  const struct filebundle_entry *fbe;
  const char *u;

  for(fb = filebundles; fb != NULL; fb = fb->next) {
    if(strncmp(url, fb->prefix, strlen(fb->prefix)))
      continue;

    u = url + strlen(fb->prefix);
    if(*u != '/')
      continue;
    u++;

    for(fbe = fb->entries; fbe->filename != NULL; fbe++) {
      if(!strcmp(fbe->filename, u))
	return fbe;
    }
  }
  return NULL;
}


/**
 * Open file
 */
static fa_handle_t *
b_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
       int flags, struct prop *stats)
{
  const struct filebundle_entry *fbe;
  fa_bundle_fh_t *fh;

  if((fbe = resolve_file(url)) == NULL) {
    snprintf(errbuf, errlen, "File not found");
    return NULL;
  }
  fh = calloc(1, sizeof(fa_bundle_fh_t));
  fh->ptr = fbe->data;
  fh->size = fbe->size;

  fh->h.fh_proto = fap;
  return &fh->h;
}


/**
 * Close file
 */
static void
b_close(fa_handle_t *fh0)
{
  free(fh0);
}


/**
 * Read from file
 */
static int
b_read(fa_handle_t *fh0, void *buf, size_t size)
{
  fa_bundle_fh_t *fh = (fa_bundle_fh_t *)fh0;

  if(size < 1)
    return size;

  if(fh->pos + size > fh->size)
    size = fh->size - fh->pos;
  memcpy(buf, fh->ptr + fh->pos, size);
  fh->pos += size;
  return size;
}


/**
 * Seek in file
 */
static int64_t
b_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  fa_bundle_fh_t *fh = (fa_bundle_fh_t *)handle;
  int np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = fh->pos + pos;
    break;

  case SEEK_END:
    np = fh->size + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;

  fh->pos = np;
  return np;
}



/**
 * Return size of file
 */
static int64_t
b_fsize(fa_handle_t *handle)
{
  fa_bundle_fh_t *fh = (fa_bundle_fh_t *)handle;
  return fh->size;
}


/**
 *
 */
static int
b_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  fa_dir_entry_t *fde;
  struct filebundle *fb;
  char buf[PATH_MAX];
  char buf2[PATH_MAX];
  const struct filebundle_entry *fbe;
  const char *u, *u2;
  char *s;
  int ok = 0;

  if(*url == 0) {
    if(fd != NULL) {
      for(fb = filebundles; fb != NULL; fb = fb->next) {
	snprintf(buf2, sizeof(buf2), "%s", fb->prefix);
	if((s = strchr(buf2, '/')) != NULL)
	  *s = 0;
      
	TAILQ_FOREACH(fde, &fd->fd_entries, fde_link)
	  if(!strcmp(fde->fde_filename, buf2))
	    break;
	if(fde != NULL)
	  continue;

	snprintf(buf, sizeof(buf), "bundle://%s", buf2);
	fa_dir_add(fd, buf, buf2, CONTENT_DIR);
      }
    }
    return 0;
  }

  for(fb = filebundles; fb != NULL; fb = fb->next) {

    if(!strncmp(url, fb->prefix, strlen(url))) {

      if(fb->prefix[strlen(url)] == '/') {
	if(fd != NULL) {
	  int len = strlen(url)+1;
	  snprintf(buf2, sizeof(buf2), "%s", fb->prefix + len);
	  if((s = strchr(buf2, '/')) != NULL)
	    *s = 0;

	  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link)
	    if(!strcmp(fde->fde_filename, buf2))
	      break;
	  if(fde != NULL)
	    continue;

	  snprintf(buf, sizeof(buf), "bundle://%.*s%s", len, fb->prefix, buf2);
	  fa_dir_add(fd, buf, buf2, CONTENT_DIR);
	}
	ok = 1;
	continue;
      } else {

	ok = 1;

	if(fd == NULL)
	  continue;

	for(fbe = fb->entries; fbe->filename != NULL; fbe++) {
	  snprintf(buf2, sizeof(buf2), "%s", fbe->filename);
	  if((s = strchr(buf2, '/')) != NULL) {
	    *s = 0;

	    fde = TAILQ_LAST(&fd->fd_entries, fa_dir_entry_queue);
	    if(fde != NULL && !strcmp(fde->fde_filename, buf2))
	      continue;
	    snprintf(buf, sizeof(buf), "bundle://%s/%s", fb->prefix, buf2);
	  } else {
	    snprintf(buf, sizeof(buf), "bundle://%s/%s", fb->prefix,
		     fbe->filename);
	  }
	  fa_dir_add(fd, buf, buf2, s ? CONTENT_DIR : CONTENT_FILE);
	}
      }

    } else if(!strncmp(url, fb->prefix, strlen(fb->prefix))) {

      u = url + strlen(fb->prefix);
      if(*u != '/')
	continue;
      u++;

      for(fbe = fb->entries; fbe->filename != NULL; fbe++) {
	if(strncmp(u, fbe->filename, strlen(u)))
	  continue;
	u2 = fbe->filename + strlen(u);
	if(*u2 != '/')
	  continue;
	ok = 1;
	u2++;
	
	if(fd == NULL)
	  continue;

	snprintf(buf2, sizeof(buf2), "%s", u2);
	if((s = strchr(buf2, '/')) != NULL) {
	  *s = 0;

	  fde = TAILQ_LAST(&fd->fd_entries, fa_dir_entry_queue);
	  if(fde != NULL && !strcmp(fde->fde_filename, buf2))
	    continue;
	  
	  snprintf(buf, sizeof(buf), "bundle://%s/%.*s/%s", fb->prefix,
		   (int)strlen(u), fbe->filename, buf2);
	} else {
	  snprintf(buf, sizeof(buf), "bundle://%s/%s", fb->prefix,
		   fbe->filename);
	}
	printf("Adding URL %s\n", buf);
	fa_dir_add(fd, buf, buf2, s ? CONTENT_DIR : CONTENT_FILE);
      }
    }
  }

  if(!ok)
    snprintf(errbuf, errlen, "No such directory");
  return !ok;
}



/**
 * Standard unix stat
 */
static int
b_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
       char *errbuf, size_t errlen, int non_interactive)
{
  const struct filebundle_entry *fbe;

  memset(fs, 0, sizeof(struct fa_stat));

  if((fbe = resolve_file(url)) != NULL) {
    fs->fs_type = CONTENT_FILE;
    fs->fs_size = fbe->size;
    return FAP_STAT_OK;
  }

  if(b_scandir(NULL, url, errbuf, errlen))
    return FAP_STAT_ERR;

  fs->fs_type = CONTENT_DIR;
  return FAP_STAT_OK;
}


static fa_protocol_t fa_protocol_bundle = {
  .fap_name  = "bundle",
  .fap_scan  = b_scandir,
  .fap_open  = b_open,
  .fap_close = b_close,
  .fap_read  = b_read,
  .fap_seek  = b_seek,
  .fap_fsize = b_fsize,
  .fap_stat  = b_stat,
};
FAP_REGISTER(bundle);
