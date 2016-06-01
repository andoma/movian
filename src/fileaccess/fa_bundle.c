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
#include "config.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "main.h"
#include "fileaccess.h"
#include "filebundle.h"

#include "fa_proto.h"

static hts_mutex_t memfile_mutex;
LIST_HEAD(memfile_list, memfile);
static struct memfile_list memfiles;
static int tally;
struct filebundle *filebundles;

/**
 *
 */
typedef struct memfile {
  LIST_ENTRY(memfile) mf_link;
  const uint8_t *mf_data;
  size_t mf_size;
  int mf_id;
} memfile_t;


/**
 *
 */
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

  while(*url == '/')
    url++;

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
       int flags, struct fa_open_extra *foe)
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
b_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
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
b_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
          char *errbuf, size_t errlen, int flags)
{
  fa_dir_entry_t *fde, *last = NULL;
  struct filebundle *fb;
  char buf[PATH_MAX];
  char buf2[PATH_MAX];
  const struct filebundle_entry *fbe;
  const char *u, *u2;
  char *s;
  int ok = 0;

  while(*url == '/')
    url++;

  if(*url == 0) {
    if(fd != NULL) {
      for(fb = filebundles; fb != NULL; fb = fb->next) {
	snprintf(buf2, sizeof(buf2), "%s", fb->prefix);
	if((s = strchr(buf2, '/')) != NULL)
	  *s = 0;
      
	RB_FOREACH(fde, &fd->fd_entries, fde_link)
	  if(!strcmp(rstr_get(fde->fde_filename), buf2))
	    break;
	if(fde != NULL)
	  continue;

	snprintf(buf, sizeof(buf), "bundle://%s", buf2);
	fa_dir_add(fd, buf, buf2, CONTENT_DIR);
      }
    }
    return 0;
  }

  char *x = mystrdupa(url);
  int l = strlen(x);
  url = x;
  while(l > 0 && x[l - 1] == '/') {
    x[l - 1] = 0;
    l--;
  }

  for(fb = filebundles; fb != NULL; fb = fb->next) {
    if(!strncmp(url, fb->prefix, strlen(url))) {

      if(fb->prefix[strlen(url)] == '/') {
	if(fd != NULL) {
	  int len = strlen(url)+1;
	  snprintf(buf2, sizeof(buf2), "%s", fb->prefix + len);
	  if((s = strchr(buf2, '/')) != NULL)
	    *s = 0;

	  RB_FOREACH(fde, &fd->fd_entries, fde_link)
	    if(!strcmp(rstr_get(fde->fde_filename), buf2))
	      break;
	  if(fde != NULL)
	    continue;

	  snprintf(buf, sizeof(buf), "bundle://%.*s%s", len, fb->prefix, buf2);
	  last = fa_dir_add(fd, buf, buf2, CONTENT_DIR);
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

	    if(last != NULL && !strcmp(rstr_get(last->fde_filename), buf2))
	      continue;
	    snprintf(buf, sizeof(buf), "bundle://%s/%s", fb->prefix, buf2);
	  } else {
	    snprintf(buf, sizeof(buf), "bundle://%s/%s", fb->prefix,
		     fbe->filename);
	  }
	  last = fa_dir_add(fd, buf, buf2, s ? CONTENT_DIR : CONTENT_FILE);
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

	  if(last != NULL && !strcmp(rstr_get(last->fde_filename), buf2))
	    continue;
	  
	  snprintf(buf, sizeof(buf), "bundle://%s/%.*s/%s", fb->prefix,
		   (int)strlen(u), fbe->filename, buf2);
	} else {
	  snprintf(buf, sizeof(buf), "bundle://%s/%s", fb->prefix,
		   fbe->filename);
	}
	last = fa_dir_add(fd, buf, buf2, s ? CONTENT_DIR : CONTENT_FILE);
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
       int flags, char *errbuf, size_t errlen)
{
  const struct filebundle_entry *fbe;

  memset(fs, 0, sizeof(struct fa_stat));

  if((fbe = resolve_file(url)) != NULL) {
    fs->fs_type = CONTENT_FILE;
    fs->fs_size = fbe->size;
    return FAP_OK;
  }

  if(b_scandir(fap, NULL, url, errbuf, errlen, flags))
    return FAP_ERROR;

  fs->fs_type = CONTENT_DIR;
  return FAP_OK;
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






/**
 *
 */
static void
mf_init(void)
{
  hts_mutex_init(&memfile_mutex);
}


static const memfile_t *
find_memfile(const char *url)
{
  const memfile_t *mf;
  char *endptr;
  int id = strtol(url, &endptr, 10);
  if(endptr == url || *endptr != 0)
    return NULL;

  hts_mutex_lock(&memfile_mutex);
  LIST_FOREACH(mf, &memfiles, mf_link)
    if(mf->mf_id == id)
      break;
  hts_mutex_unlock(&memfile_mutex);
  return mf;
}


/**
 *
 */
static fa_handle_t *
mf_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	int flags, struct fa_open_extra *foe)
{
  const memfile_t *mf = find_memfile(url);
  if(mf == NULL) {
    snprintf(errbuf, errlen, "No such file or directory");
    return NULL;
  }

  fa_bundle_fh_t *fh = calloc(1, sizeof(fa_bundle_fh_t));
  fh->ptr = mf->mf_data;
  fh->size = mf->mf_size;
  fh->h.fh_proto = fap;
  return &fh->h;
}


/**
 *
 */
static int
mf_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	int flags, char *errbuf, size_t errlen)
{
  const memfile_t *mf = find_memfile(url);
  memset(fs, 0, sizeof(struct fa_stat));

  if(mf == NULL) {
    snprintf(errbuf, errlen, "No such file or directory");
    return FAP_ERROR;
  }

  fs->fs_type = CONTENT_FILE;
  fs->fs_size = mf->mf_size;
  return FAP_OK;
}


/**
 *
 */
static fa_protocol_t fa_protocol_memfile = {
  .fap_name  = "memfile",
  .fap_init  = mf_init,
  .fap_open  = mf_open,
  .fap_close = b_close,
  .fap_read  = b_read,
  .fap_seek  = b_seek,
  .fap_fsize = b_fsize,
  .fap_stat  = mf_stat,
};
FAP_REGISTER(memfile);


/**
 *
 */
int 
memfile_register(const void *data, size_t len)
{
  memfile_t *mf = malloc(sizeof(memfile_t));
  mf->mf_data = data;
  mf->mf_size = len;
  hts_mutex_lock(&memfile_mutex);
  mf->mf_id = ++tally;
  LIST_INSERT_HEAD(&memfiles, mf, mf_link);
  hts_mutex_unlock(&memfile_mutex);
  return mf->mf_id;
}


/**
 *
 */
void
memfile_unregister(int id)
{
  memfile_t *mf;
  hts_mutex_lock(&memfile_mutex);
  LIST_FOREACH(mf, &memfiles, mf_link)
    if(mf->mf_id == id)
      break;
  if(mf != NULL)
    LIST_REMOVE(mf, mf_link);
  hts_mutex_unlock(&memfile_mutex);
  free(mf);
}


/**
 *
 */
fa_handle_t *
memfile_make(const void *mem, size_t len)
{
  fa_bundle_fh_t *fh = calloc(1, sizeof(fa_bundle_fh_t));
  fh->ptr = mem;
  fh->size = len;
  fh->h.fh_proto = &fa_protocol_memfile;
  return &fh->h;
}
