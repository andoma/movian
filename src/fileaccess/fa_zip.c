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
/**
 * Based on:
 *
 * http://www.pkware.com/documents/casestudies/APPNOTE.TXT
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "main.h"
#include "fileaccess.h"
#include "fa_zlib.h"
#include "main.h"
#include "usage.h"


static HTS_MUTEX_DECL(zip_global_mutex);


LIST_HEAD(zip_file_list, zip_file);
LIST_HEAD(zip_archive_list, zip_archive);

static struct zip_archive_list zip_archives;

/**
 *
 */
typedef struct zip_hdr_disk_trailer {
  uint8_t magic[4]; /* 0x06054b50 */
  uint8_t disk[2];
  uint8_t finaldisk[2];
  uint8_t entries[2];
  uint8_t totalentries[2];
  uint8_t rootsize[4];
  uint8_t rootoffset[4];
  uint8_t commentsize[2];
  uint8_t comment[0];
} __attribute__((packed)) zip_hdr_disk_trailer_t;

/**
 *
 */
typedef struct zip_hdr_file_header {
  uint8_t magic[4];
  uint8_t version_made_by[2];
  uint8_t version_needed[2];
  uint8_t gpflags[2];
  uint8_t method[2];
  uint8_t last_mod_file_time[2];
  uint8_t last_mod_file_date[2];
  uint8_t crc[4];
  uint8_t compressed_size[4];
  uint8_t uncompressed_size[4];
  uint8_t filename_len[2];
  uint8_t extra_len[2];
  uint8_t comment_len[2];
  uint8_t disk_number_start[2];
  uint8_t attribs_internal[2];
  uint8_t attribs_external[4];
  uint8_t lfh_offset[4];
  uint8_t filename[0];

} __attribute__((packed)) zip_hdr_file_header_t;

/**
 *
 */
typedef struct zip_local_file_header {
  uint8_t magic[4];
  uint8_t version_needed[2];
  uint8_t gpflags[2];
  uint8_t method[2];
  uint8_t last_mod_file_time[2];
  uint8_t last_mod_file_date[2];
  uint8_t crc[4];
  uint8_t compressed_size[4];
  uint8_t uncompressed_size[4];
  uint8_t filename_len[2];
  uint8_t extra_len[2];
} __attribute__((packed)) zip_local_file_header_t;




#define ZIPHDR_GET16(hdr, field) \
 (((uint16_t)(hdr)->field[1]) << 8 | ((uint16_t)(hdr)->field[0]))

#define ZIPHDR_GET32(hdr, field) \
 (((uint32_t)(hdr)->field[3] << 24) | ((uint32_t)(hdr)->field[2] << 16) | \
  ((uint32_t)(hdr)->field[1] << 8)  | ((uint32_t)(hdr)->field[0]       ))


/**
 *
 */
typedef struct zip_archive {

  hts_mutex_t za_mutex;

  int za_refcount;
  char *za_url;

  struct zip_file *za_root;

  LIST_ENTRY(zip_archive) za_link;

  time_t za_mtime;

} zip_archive_t;


/**
 *
 */
typedef struct zip_file {
  struct zip_file_list zf_files;
  zip_archive_t *zf_archive;

  char *zf_name;
  char *zf_fullname;

  int zf_type;
  int zf_method;

  int64_t zf_uncompressed_size;
  int64_t zf_compressed_size;
  int64_t zf_lhpos;

  LIST_ENTRY(zip_file) zf_link;
} zip_file_t;




/**
 *
 */
static zip_file_t *
zip_archive_find_file(zip_archive_t *za, zip_file_t *parent,
		      const char *name, int create)
{
  zip_file_t *zf;
  const char *s, *n = name;
  char *b;
  int l;
  int must_be_dir = 0;

  if(parent == NULL)
    return NULL;

  s = strchr(name, '/');
  if(s == NULL)
    s = strchr(name, '\\');
  if(s != NULL) {
    l = s - name;
    s++;
    must_be_dir = *s == 0;
    n = b = alloca(l + 1);
    memcpy(b, name, l);
    b[l] = 0;
  }

  LIST_FOREACH(zf, &parent->zf_files, zf_link)
    if(!strcasecmp(n, zf->zf_name))
      break;

  if(zf == NULL) {

    if(create == 0)
      return NULL;

    zf = calloc(1, sizeof(zip_file_t));
    zf->zf_archive = za;
    zf->zf_name = strdup(n);
    zf->zf_type = s ? CONTENT_DIR : CONTENT_FILE;
    LIST_INSERT_HEAD(&parent->zf_files, zf, zf_link);
  }

  if(must_be_dir) {
    if(zf->zf_type != CONTENT_DIR)
      return NULL;
    return zf;
  }

  return s != NULL ? zip_archive_find_file(za, zf, s, create) : zf;
}



static void
zip_archive_destroy_file(zip_file_t *zf)
{
  zip_file_t *c;

  while((c = LIST_FIRST(&zf->zf_files)) != NULL)
    zip_archive_destroy_file(c);
  
  if(zf->zf_name != NULL) {
    free(zf->zf_name);
    free(zf->zf_fullname);
    LIST_REMOVE(zf, zf_link);
  }
  free(zf);
}



static void
zip_archive_scrub(zip_archive_t *za)
{
  if(za->za_root != NULL) {
    zip_archive_destroy_file(za->za_root);
    za->za_root = NULL;
  }
}

#define TRAILER_SCAN_SIZE 1024

/**
 *
 */
static int
zip_archive_load(zip_archive_t *za)
{
  zip_file_t *zf;
  fa_handle_t *fh;
  zip_hdr_disk_trailer_t *disktrailer;
  zip_hdr_file_header_t *fhdr;
  char *buf, *ptr;
  size_t scan_size;
  int64_t scan_off, asize;
  int i, l;

  int64_t cds_off;
  size_t cds_size;
  char *fname;
  struct fa_stat fs;

  if(fa_stat(za->za_url, &fs, NULL, 0))
    return -1;

  if(fs.fs_size < sizeof(zip_hdr_disk_trailer_t))
    return -1;

  asize = fs.fs_size;
  za->za_mtime = fs.fs_mtime;

  if((fh = fa_open(za->za_url, NULL, 0)) == NULL)
    return -1;

  scan_off = asize - TRAILER_SCAN_SIZE;
  if(scan_off < 0) {
    scan_size = asize;
    scan_off = 0;
  } else {
    scan_size = TRAILER_SCAN_SIZE;
  }

  buf = malloc(scan_size);

  fa_seek(fh, scan_off, SEEK_SET);

  if(fa_read(fh, buf, scan_size) != scan_size) {
    free(buf);
    fa_close(fh);
    return -1;
  }

  cds_size = 0; 
  cds_off = 0;

  for(i = scan_size - sizeof(zip_hdr_disk_trailer_t); i >= 0; i--) {
    if(buf[i + 0] == 'P' && buf[i + 1] == 'K' && 
       buf[i + 2] == 5   && buf[i + 3] == 6) {
      disktrailer = (void *)buf + i;
      cds_size = ZIPHDR_GET32(disktrailer, rootsize);
      cds_off  = ZIPHDR_GET32(disktrailer, rootoffset);
      break;
    }
  }

  if(i == -1) {
    free(buf);
    fa_close(fh);
    return -1;
  }

  free(buf);
  if((buf = malloc(cds_size)) == NULL) {
    fa_close(fh);
    return -1;
  }

  if(fa_seek(fh, cds_off, SEEK_SET) != cds_off ||
     fa_read(fh, buf, cds_size) != cds_size)
    memset(buf, 0, cds_off);

  int64_t displacement = 0;

  ptr = buf;
  fhdr = (zip_hdr_file_header_t *)ptr;
  if(fhdr->magic[0] != 'P' || fhdr->magic[1] != 'K' ||
     fhdr->magic[2] != 1   || fhdr->magic[3] != 2) {

    int64_t o2 = fs.fs_size - (cds_size + (TRAILER_SCAN_SIZE - i));
    
    fa_seek(fh, o2, SEEK_SET);
    if(fa_read(fh, buf, cds_size) != cds_size) {
      free(buf);
      fa_close(fh);
      return -1;
    }
    
    if(fhdr->magic[0] != 'P' || fhdr->magic[1] != 'K' ||
       fhdr->magic[2] != 1   || fhdr->magic[3] != 2) {
      free(buf);
      fa_close(fh);
      return -1;
    }
    displacement = o2 - cds_off;
  }


  za->za_root = calloc(1, sizeof(zip_file_t));
  za->za_root->zf_type = CONTENT_DIR;
  za->za_root->zf_archive = za;


  ptr = buf;
  while(cds_size > sizeof(zip_hdr_file_header_t)) {

    fhdr = (zip_hdr_file_header_t *)ptr;

    if(fhdr->magic[0] != 'P' || fhdr->magic[1] != 'K' ||
       fhdr->magic[2] != 1   || fhdr->magic[3] != 2) {
      break;
    }
    l = ZIPHDR_GET16(fhdr, filename_len);
    if(l == 0) {
      break;
    }

    fname = malloc(l + 1);

    memcpy(fname, fhdr->filename, l);
    fname[l] = 0;

    if(fname[l - 1] != '/') {
      /* Not a directory */
      if((zf = zip_archive_find_file(za, za->za_root, fname, 1)) != NULL) {
	zf->zf_uncompressed_size = ZIPHDR_GET32(fhdr, uncompressed_size);
	zf->zf_compressed_size   = ZIPHDR_GET32(fhdr, compressed_size);
	zf->zf_lhpos             = ZIPHDR_GET32(fhdr, lfh_offset) + displacement;
	zf->zf_method            = ZIPHDR_GET16(fhdr, method);
	
      }
    }

    free(fname);

    l = sizeof(zip_hdr_file_header_t) + 
      ZIPHDR_GET16(fhdr, filename_len) + 
      ZIPHDR_GET16(fhdr, extra_len) +
      ZIPHDR_GET16(fhdr, comment_len);

    cds_size -= l;
    ptr += l;
  }

  free(buf);
  fa_close(fh);
  return 0;
}



/**
 *
 */
static void
zip_archive_unref(zip_archive_t *za)
{
  hts_mutex_lock(&zip_global_mutex);

  za->za_refcount--;

  if(za->za_refcount == 0) {
    zip_archive_scrub(za);
    free(za->za_url);
    LIST_REMOVE(za, za_link);
    free(za);
  }

  hts_mutex_unlock(&zip_global_mutex);
}


/**
 *
 */
static zip_archive_t *
zip_archive_find(const char *url, const char **rp)
{
  zip_archive_t *za = NULL;
  char *u, *s;

  if(*url == 0)
    return NULL;

  hts_mutex_lock(&zip_global_mutex);
  u = mystrdupa(url);

  while(1) {
    LIST_FOREACH(za, &zip_archives, za_link) {
      if(!strcasecmp(za->za_url, u))
	break;
    }
    if(za != NULL)
      break;
    if((s = strrchr(u, '/')) == NULL)
      break;
    *s = 0;
  }

  if(za == NULL) {
    u = mystrdupa(url);

    while(1) {
      struct fa_stat fs;

      if(!fa_stat(u, &fs, NULL, 0) && fs.fs_type == CONTENT_FILE)
	break;

      if((s = strrchr(u, '/')) == NULL) {
	hts_mutex_unlock(&zip_global_mutex);
	return NULL;
      }
      *s = 0;
    }
  }
  const char *r = url + strlen(u);
  if(*r == '/')
    r++;
  *rp = r;

  if(za == NULL) {
    za = calloc(1, sizeof(zip_archive_t));
    hts_mutex_init(&za->za_mutex);
    
    za->za_url = strdup(u);
    LIST_INSERT_HEAD(&zip_archives, za, za_link);
  }

  za->za_refcount++;
  hts_mutex_unlock(&zip_global_mutex);

  hts_mutex_lock(&za->za_mutex);

  if(za->za_root == NULL && zip_archive_load(za)) {
    zip_archive_scrub(za);
  }
  hts_mutex_unlock(&za->za_mutex);

  return za;
}


/**
 *
 */
static zip_file_t *
zip_file_find(const char *url)
{
  const char *r;
  zip_file_t *rf;
  zip_archive_t *za = zip_archive_find(url, &r);
  if(za == NULL)
    return NULL;

  rf = *r ? zip_archive_find_file(za, za->za_root, r, 0) : za->za_root;

  if(rf == NULL)
    zip_archive_unref(za);

  return rf;
}

/**
 *
 */
static void
zip_file_unref(zip_file_t *zf)
{
  zip_archive_unref(zf->zf_archive);
}


/**
 *
 */
static int
zip_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
            char *errbuf, size_t errlen, int flags)
{
  zip_file_t *c, *zf;
  char buf[URL_MAX];

  if((zf = zip_file_find(url)) == NULL) {
    snprintf(errbuf, errlen, "Entry not found in archive");
    return -1;
  }
  if(zf->zf_type != CONTENT_DIR) {
    zip_file_unref(zf);
    snprintf(errbuf, errlen, "Entry is not a directory");
    return -1;
  }

  LIST_FOREACH(c, &zf->zf_files, zf_link) {
    snprintf(buf, sizeof(buf), "zip://%s/%s", url, c->zf_name);
    fa_dir_add(fd, buf, c->zf_name, c->zf_type);
  }

  zip_file_unref(zf);
  return 0;
}


typedef struct zip_ref {
  fa_handle_t h;
  zip_file_t *file;
} zip_ref_t;

/**
 *
 */
static fa_handle_t *
zip_reference(fa_protocol_t *fap, const char *url)
{
  zip_file_t *zf;
  zip_ref_t *zr;

  if((zf = zip_file_find(url)) == NULL)
    return NULL;

  zr = malloc(sizeof(zip_ref_t));
  zr->h.fh_proto = fap;
  zr->file = zf;
  return &zr->h;
}


/**
 *
 */
static void
zip_unreference(fa_handle_t *fh)
{
  zip_ref_t *zr = (zip_ref_t *)fh;
  
  zip_file_unref(zr->file);
  free(fh);
}






/**
 *
 */
typedef struct zip_fh {
  fa_handle_t h;

  zip_file_t *zfh_file;

  fa_handle_t *zfh_reader_handle;
  const fa_protocol_t *zfh_reader_proto;

  int64_t zfh_pos;

  /* Members for accessing original archive */
  void *zfh_archive_handle;
  int64_t zfh_archive_pos;
  int64_t zfh_file_start;

} zip_fh_t;



/**
 *
 */
static int
zip_file_read(fa_handle_t *handle, void *buf, size_t size)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  zip_file_t *zf = zfh->zfh_file;
  int64_t wpos;
  size_t r;

  if(zfh->zfh_pos < 0 || zfh->zfh_pos > zf->zf_compressed_size)
    return 0;

  if(zfh->zfh_pos + size > zf->zf_compressed_size)
    size = zf->zf_compressed_size - zfh->zfh_pos;

  assert(size >= 0);

  wpos = zfh->zfh_pos + zfh->zfh_file_start; // Real position in archive

  if(wpos != zfh->zfh_archive_pos) {
    // Not there, must seek
    if(fa_seek(zfh->zfh_archive_handle, wpos, SEEK_SET) != wpos) {
      // Can't go there in archive, bail out
      zfh->zfh_archive_pos = -1;
      return -1;
    }
  }
  
  r = fa_read(zfh->zfh_archive_handle, buf, size);

  if(r > 0)
    zfh->zfh_pos += r;
  return r;
}


/**
 *
 */
static int64_t
zip_file_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  zip_file_t *zf = zfh->zfh_file;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = zfh->zfh_pos + pos;
    break;

  case SEEK_END:
    np = zf->zf_compressed_size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  zfh->zfh_pos = np;
  return np;
}

/**
 *
 */
static void
zip_file_close(fa_handle_t *handle)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  fa_close(zfh->zfh_archive_handle);
}


/**
 *
 */
static int64_t
zip_file_fsize(fa_handle_t *handle)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  zip_file_t *zf = zfh->zfh_file;

  return zf->zf_compressed_size;
}



/**
 * Intermediate FAP for reading into ZIP files
 */
static fa_protocol_t zip_file_protocol = {
  .fap_name = "zipfile",
  .fap_read  = zip_file_read,
  .fap_seek  = zip_file_seek,
  .fap_close = zip_file_close,
  .fap_fsize = zip_file_fsize,
};



/**
 *
 */
static fa_handle_t *
zip_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	 int flags, struct fa_open_extra *foe)
{
  zip_file_t *zf;
  zip_fh_t *zfh;
  zip_archive_t *za;
  zip_local_file_header_t h;
 
  if((zf = zip_file_find(url)) == NULL) {
    snprintf(errbuf, errlen, "Entry not found in archive");
    return NULL;
  }

  if(zf->zf_type != CONTENT_FILE) {
    zip_file_unref(zf);
    snprintf(errbuf, errlen, "Entry is not a file");
    return NULL;
  }

  za = zf->zf_archive;
  zfh = calloc(1, sizeof(zip_fh_t));
  zfh->zfh_file = zf;
  zfh->h.fh_proto = fap;

  if((zfh->zfh_archive_handle = fa_open(za->za_url, errbuf, errlen)) == NULL) {
    zip_file_unref(zf);
    free(zfh);
    return NULL;
  }

  fa_seek(zfh->zfh_archive_handle, zf->zf_lhpos, SEEK_SET);
 
  if(fa_read(zfh->zfh_archive_handle, &h, sizeof(h)) != sizeof(h)) {
    snprintf(errbuf, errlen, "Truncated ZIP file");
    goto bad;
  }

  if(h.magic[0] != 'P' || h.magic[1] != 'K' ||
     h.magic[2] != 3   || h.magic[3] != 4) {
    snprintf(errbuf, errlen, "Bad ZIP magic");
    goto bad;
  }

  zfh->zfh_file_start = zf->zf_lhpos + sizeof(h) + 
    ZIPHDR_GET16(&h, filename_len) + ZIPHDR_GET16(&h, extra_len);

  switch(zf->zf_method) {

  case 0:
    /* No compression */
    zfh->zfh_reader_handle = &zfh->h;
    zfh->zfh_reader_proto = &zip_file_protocol;
    break;


  case 8:
    /* Inflate (zlib) */
    zfh->zfh_reader_handle = fa_inflate_init(&zip_file_protocol, &zfh->h,
					     zf->zf_uncompressed_size);
    if(zfh->zfh_reader_handle == NULL) {
      snprintf(errbuf, errlen, "Unable to initialize inflator");
      goto bad;
    }

    zfh->zfh_reader_proto = &fa_protocol_inflate;
    break;

  default:
    snprintf(errbuf, errlen, "Compression method %d not supported\n",
	     zf->zf_method);
    /* FALLTHRU */
  bad:
    fa_close(zfh->zfh_archive_handle);
    zip_file_unref(zf);
    free(zfh);
    return NULL;
  }

  return &zfh->h;
}


/**
 *
 */

static void 
zip_close(fa_handle_t *handle)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  
   zfh->zfh_reader_proto->fap_close(zfh->zfh_reader_handle);

  zip_file_unref(zfh->zfh_file); /* za may be destroyed here */
  free(zfh);
}


/**
 * Read from file
 */
static int
zip_read(fa_handle_t *handle, void *buf, size_t size)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  return zfh->zfh_reader_proto->fap_read(zfh->zfh_reader_handle, buf, size);
}


/**
 * Seek in file
 */
static int64_t
zip_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  return zfh->zfh_reader_proto->fap_seek(zfh->zfh_reader_handle, pos,
                                         whence, lazy);
}


/**
 * Return size of file
 */
static int64_t
zip_fsize(fa_handle_t *handle)
{
  zip_fh_t *zfh = (zip_fh_t *)handle;
  return zfh->zfh_reader_proto->fap_fsize(zfh->zfh_reader_handle);
}

/**
 * Standard unix stat
 */
static int
zip_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	 int flags, char *errbuf, size_t errlen)
{
  zip_file_t *zf;

  if((zf = zip_file_find(url)) == NULL) {
    snprintf(errbuf, errlen, "Entry not found in archive");
    return -1;
  }

  memset(fs, 0, sizeof(struct fa_stat));

  fs->fs_type = zf->zf_type;
  fs->fs_size = zf->zf_uncompressed_size;
  fs->fs_mtime = zf->zf_archive->za_mtime;

  zip_file_unref(zf);
  return 0;
}


static fa_protocol_t fa_protocol_zip = {
  .fap_name = "zip",
  .fap_scan =  zip_scandir,
  .fap_open  = zip_open,
  .fap_close = zip_close,
  .fap_read  = zip_read,
  .fap_seek  = zip_seek,
  .fap_fsize = zip_fsize,
  .fap_stat  = zip_stat,
  .fap_reference = zip_reference,
  .fap_unreference = zip_unreference,
};
FAP_REGISTER(zip);
