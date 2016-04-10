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
#include "main.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "usage.h"


#define RAR_HEADER_MAIN   0x73
#define RAR_HEADER_FILE   0x74
#define RAR_HEADER_NEWSUB 0x7a
#define RAR_HEADER_ENDARC 0x7b




#define  MHD_VOLUME         0x0001
#define  MHD_COMMENT        0x0002
#define  MHD_LOCK           0x0004
#define  MHD_SOLID          0x0008
#define  MHD_PACK_COMMENT   0x0010
#define  MHD_NEWNUMBERING   0x0010
#define  MHD_AV             0x0020
#define  MHD_PROTECT        0x0040
#define  MHD_PASSWORD       0x0080
#define  MHD_FIRSTVOLUME    0x0100
#define  MHD_ENCRYPTVER     0x0200

#define  LHD_SPLIT_BEFORE   0x0001
#define  LHD_SPLIT_AFTER    0x0002
#define  LHD_PASSWORD       0x0004
#define  LHD_COMMENT        0x0008
#define  LHD_SOLID          0x0010

#define  LHD_WINDOWMASK     0x00e0
#define  LHD_WINDOW64       0x0000
#define  LHD_WINDOW128      0x0020
#define  LHD_WINDOW256      0x0040
#define  LHD_WINDOW512      0x0060
#define  LHD_WINDOW1024     0x0080
#define  LHD_WINDOW2048     0x00a0
#define  LHD_WINDOW4096     0x00c0
#define  LHD_DIRECTORY      0x00e0

#define  LHD_LARGE          0x0100
#define  LHD_UNICODE        0x0200
#define  LHD_SALT           0x0400
#define  LHD_VERSION        0x0800
#define  LHD_EXTTIME        0x1000
#define  LHD_EXTFLAGS       0x2000

#define  SKIP_IF_UNKNOWN    0x4000
#define  LONG_BLOCK         0x8000

#define  EARC_NEXT_VOLUME   0x0001 // not last volume
#define  EARC_DATACRC       0x0002
#define  EARC_REVSPACE      0x0004
#define  EARC_VOLNUMBER     0x0008


static hts_mutex_t rar_global_mutex;

TAILQ_HEAD(rar_segment_queue, rar_segment);
LIST_HEAD(rar_volume_list, rar_volume);
LIST_HEAD(rar_file_list, rar_file);
LIST_HEAD(rar_archive_list, rar_archive);

static struct rar_archive_list rar_archives;

/**
 *
 */
typedef struct rar_archive {

  hts_mutex_t ra_mutex;

  int ra_refcount;
  char *ra_url;

  struct rar_volume_list ra_volumes;
  struct rar_file *ra_root;

  LIST_ENTRY(rar_archive) ra_link;

  time_t ra_mtime;

} rar_archive_t;


/**
 *
 */
typedef struct rar_volume {
  char *rv_url;
  LIST_ENTRY(rar_volume) rv_link;
} rar_volume_t;


/**
 *
 */
typedef struct rar_file {
  struct rar_segment_queue rf_segments;
  struct rar_file_list rf_files;

  char *rf_name;
  
  int rf_type;

  int64_t rf_size;
  rar_archive_t *rf_archive;

  char rf_method;
  char rf_unpver;

  LIST_ENTRY(rar_file) rf_link;

} rar_file_t;


/**
 *
 */
typedef struct rar_segment {
  rar_volume_t *rs_volume;
  int64_t rs_offset;
  int64_t rs_voffset;
  int64_t rs_size;
  TAILQ_ENTRY(rar_segment) rs_link;
} rar_segment_t;


/**
 *
 */
static rar_file_t *
rar_archive_find_file(rar_archive_t *ra, rar_file_t *parent,
		      const char *name, int create,
		      char unpver, char method)
{
  rar_file_t *rf;
  const char *s, *n = name;
  char *b;
  int l;
    
  if(parent == NULL)
    return NULL;

  s = strchr(name, '/');
  if(s == NULL)
    s = strchr(name, '\\');
  if(s != NULL) {
    l = s - name;
    s++;
    if(*s == 0)
      return NULL; 
    n = b = alloca(l + 1);
    memcpy(b, name, l);
    b[l] = 0;
  }

  LIST_FOREACH(rf, &parent->rf_files, rf_link)
    if(!strcasecmp(n, rf->rf_name))
      break;

  if(rf == NULL) {

    if(create == 0)
      return NULL;

    rf = calloc(1, sizeof(rar_file_t));
    rf->rf_archive = ra;
    TAILQ_INIT(&rf->rf_segments);
    rf->rf_name = strdup(n);
    rf->rf_type = s ? CONTENT_DIR : CONTENT_FILE;
    LIST_INSERT_HEAD(&parent->rf_files, rf, rf_link);
    if(rf->rf_type == CONTENT_FILE) {
      rf->rf_unpver = unpver;
      rf->rf_method = method;
    }
  } 

  return s != NULL ? rar_archive_find_file(ra, rf, s, create,
					   unpver, method) : rf;
}



static void
rar_archive_destroy_file(rar_file_t *rf)
{
  rar_file_t *c;
  rar_segment_t *rs;

  
  while((rs = TAILQ_FIRST(&rf->rf_segments)) != NULL) {
    TAILQ_REMOVE(&rf->rf_segments, rs, rs_link);
    free(rs);
  }

  while((c = LIST_FIRST(&rf->rf_files)) != NULL)
    rar_archive_destroy_file(c);
  
  if(rf->rf_name != NULL) {
    free(rf->rf_name);
    LIST_REMOVE(rf, rf_link);
  }
  free(rf);
}



static void
rar_archive_scrub(rar_archive_t *ra)
{
  rar_volume_t *rv;

  if(ra->ra_root != NULL) {
    rar_archive_destroy_file(ra->ra_root);
    ra->ra_root = NULL;
  }

  while((rv = LIST_FIRST(&ra->ra_volumes)) != NULL) {
    LIST_REMOVE(rv, rv_link);
    free(rv->rv_url);
    free(rv);
  }
}



/**
 *
 */
static int
rar_archive_load(rar_archive_t *ra)
{
  char filename[URL_MAX], *fname, *s, *s2;
  uint8_t buf[16], *hdr = NULL;
  void *fh = NULL;
  int volume_index = -1, size, x;
  unsigned int nsize;
  uint8_t method, unpver;
  uint16_t flags;
  uint32_t u32;
  uint64_t packsize, unpsize;
  int64_t voff;
  rar_volume_t *rv;
  rar_file_t *rf;
  rar_segment_t *rs;
  struct fa_stat fs;

  ra->ra_root = calloc(1, sizeof(rar_file_t));
  ra->ra_root->rf_type = CONTENT_DIR;
  ra->ra_root->rf_archive = ra;

 open_volume:

  snprintf(filename, sizeof(filename), "%s", ra->ra_url);

  if(volume_index >= 0) {

    if((s = strrchr(filename, '.')) == NULL) {
      return -1;
    }
    
    /* find second last . */
    for(s2 = s-1; s2 >= filename; s2--)
      if(*s2 == '.')
        break;
    if(s2 >= filename && !strcmp(s2, ".part1.rar")) {
      /* first was part01 */
      if(volume_index == 0)
        volume_index = 2;
      sprintf(s2, ".part%d.rar", volume_index);
    } else if(s2 >= filename && !strcmp(s2, ".part01.rar")) {
      if(volume_index == 0)
        volume_index = 2;
      sprintf(s2, ".part%02d.rar", volume_index);
    } else if(s2 >= filename && !strcmp(s2, ".part001.rar")) {
      if(volume_index == 0)
        volume_index = 2;
      sprintf(s2, ".part%03d.rar", volume_index);
    } else {
      s++;
      sprintf(s, "r%02d", volume_index);
    }    
  }

  volume_index++;

  if((fh = fa_open(filename, NULL, 0)) == NULL)
    return -1;

  /* Read & Verify RAR file signature */
  if(fa_read(fh, buf, 7) != 7)
    goto err;

  if(buf[0] != 'R' || buf[1] != 'a' || buf[2] != 'r' || buf[3] != '!' ||
     buf[4] != 0x1a || buf[5] != 0x07 || buf[6] != 0x0) 
    goto err;

  /* Next we expect a MAIN_HEAD header (13 bytes) */
  if(fa_read(fh, buf, 13) != 13)
    goto err;

  if(ra->ra_mtime == 0 && !fa_stat(fh, &fs, NULL, 0))
    ra->ra_mtime = fs.fs_mtime;

  /* 2 bytes CRC */
  
  if(buf[2] != RAR_HEADER_MAIN)
    goto err;

  size =       buf[5] | buf[6] << 8;

  if(size != 13)
    goto err;

  rv = calloc(1, sizeof(rar_volume_t));
  LIST_INSERT_HEAD(&ra->ra_volumes, rv, rv_link);
  rv->rv_url = strdup(filename);

  voff = 13 + 7;

  while(1) {
    
    /* Read a header */
    
    if(fa_read(fh, buf, 7) != 7)
      break;

    flags = buf[3] | buf[4] << 8;
    size  = buf[5] | buf[6] << 8;
    if(size < 7)
      break;
    
    size -= 7;

    /* Read rest of header */
    hdr = malloc(size);
    if(fa_read(fh, hdr, size) != size)
      break;

    voff += 7 + size;

    if(buf[2] == RAR_HEADER_FILE || buf[2] == RAR_HEADER_NEWSUB) {

      packsize = (uint32_t)(hdr[0] | hdr[1] << 8 | hdr[2] << 16 | hdr[3] << 24);
      unpsize  = (uint32_t)(hdr[4] | hdr[5] << 8 | hdr[6] << 16 | hdr[7] << 24);
      /* Skip HostOS    1 byte  */
      /* Skip FileCRC   4 bytes */
      /* Skip FileTime  4 bytes */
      unpver   = hdr[17];
      method   = hdr[18];
      nsize    = hdr[19] | hdr[20] << 8;
      /* Skip FileAttr  4 bytes */
      x = 25;

      if(flags & LHD_LARGE) {
	u32 = hdr[x+0] | hdr[x+1] << 8 | hdr[x+2] << 16 | hdr[x+3] << 24;
	packsize |= (uint64_t)u32 << 32;

	u32 = hdr[x+4] | hdr[x+5] << 8 | hdr[x+6] << 16 | hdr[x+7] << 24;
	unpsize  |= (uint64_t)u32 << 32;

	x+= 8;
      }
      fname = malloc(nsize + 1);
      memcpy(fname, hdr + x, nsize);
      fname[nsize] = 0;

      if(((flags & LHD_WINDOWMASK) != LHD_DIRECTORY) &&
	 (rf = rar_archive_find_file(ra, ra->ra_root, fname, 1,
				     unpver, method)) != NULL) {
	rs = malloc(sizeof(rar_segment_t));
	rs->rs_volume = rv;
	rs->rs_offset = rf->rf_size;
	rs->rs_voffset = voff;
	rs->rs_size = packsize;
	rf->rf_size += packsize;
	TAILQ_INSERT_TAIL(&rf->rf_segments, rs, rs_link);
      }

      free(fname);

      fa_seek(fh, packsize, SEEK_CUR);
      voff += packsize;

    } else if(buf[2] == RAR_HEADER_ENDARC) {

      x = 0;

      if(flags & EARC_DATACRC) {
	if(size < 4)
	  break;
	x += 4;
      }
      
      if(flags & EARC_VOLNUMBER) {
	if(size < 4)
	  break;
	u32 = hdr[x+0] | hdr[x+1] << 8 | hdr[x+2] << 16 | hdr[x+3] << 24;
      }

      free(hdr);

      fa_close(fh);
      fh = NULL;

      if(flags & EARC_NEXT_VOLUME) {
	goto open_volume; 
      }
      return 0;
    }
    free(hdr);
  }

 err:
  fa_close(fh);
  return -1;
}

/**
 *
 */
static void
rar_archive_unref(rar_archive_t *ra)
{
  hts_mutex_lock(&rar_global_mutex);

  ra->ra_refcount--;

  if(ra->ra_refcount == 0) {
    rar_archive_scrub(ra);
    free(ra->ra_url);
    LIST_REMOVE(ra, ra_link);
    hts_mutex_destroy(&ra->ra_mutex);
    free(ra);
  }

  hts_mutex_unlock(&rar_global_mutex);
}


/**
 *
 */
static rar_archive_t *
rar_archive_find(const char *url, const char **rp)
{
  rar_archive_t *ra = NULL;
  char *u, *s;

  if(*url == 0)
    return NULL;

  hts_mutex_lock(&rar_global_mutex);

  u = mystrdupa(url);

  while(1) {
    LIST_FOREACH(ra, &rar_archives, ra_link) {
      if(!strcasecmp(ra->ra_url, u))
	break;
    }
    if(ra != NULL)
      break;
    if((s = strrchr(u, '/')) == NULL)
      break;
    *s = 0;
  }

  if(ra == NULL) {
    u = mystrdupa(url);

    while(1) {
      struct fa_stat fs;

      if(!fa_stat(u, &fs, NULL, 0) && fs.fs_type == CONTENT_FILE)
	break;

      if((s = strrchr(u, '/')) == NULL) {
	hts_mutex_unlock(&rar_global_mutex);
	return NULL;
      }
      *s = 0;
    }
  }
  const char *r = url + strlen(u);
  if(*r == '/')
    r++;
  *rp = r;

  if(ra == NULL) {
    ra = calloc(1, sizeof(rar_archive_t));
    hts_mutex_init(&ra->ra_mutex);
    
    ra->ra_url = strdup(u);
    LIST_INSERT_HEAD(&rar_archives, ra, ra_link);
  }

  ra->ra_refcount++;
  hts_mutex_unlock(&rar_global_mutex);

  hts_mutex_lock(&ra->ra_mutex);

  if(ra->ra_root == NULL && rar_archive_load(ra)) {
    rar_archive_scrub(ra);
  }
  hts_mutex_unlock(&ra->ra_mutex);

  return ra;
}


/**
 *
 */
static rar_file_t *
rar_file_find(const char *url)
{
  const char *r;
  rar_archive_t *ra = rar_archive_find(url, &r);
  rar_file_t *rf;
  if(ra == NULL)
    return NULL;

  rf = *r ? rar_archive_find_file(ra, ra->ra_root, r, 0, 0, 0) : ra->ra_root;

  if(rf == NULL)
    rar_archive_unref(ra);

  return rf;
}

/**
 *
 */
static void
rar_file_unref(rar_file_t *rf)
{
  rar_archive_unref(rf->rf_archive);
}


/**
 *
 */
static int
rar_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url0,
            char *errbuf, size_t errlen, int flags)
{
  rar_file_t *c, *rf;
  char buf[URL_MAX];
  char *url = mystrdupa(url0);
  int n;

  for(n = strlen(url)-1; n > 0; n--) {
    if(url[n] == '/')
      url[n] = 0;
    else
      break;
  }

  if((rf = rar_file_find(url)) == NULL) {
    snprintf(errbuf, errlen, "Entry not found in archive");
    return -1;
  }
  if(rf->rf_type != CONTENT_DIR) {
    rar_file_unref(rf);
    snprintf(errbuf, errlen, "Entry is not a directory");
    return -1;
  }

  LIST_FOREACH(c, &rf->rf_files, rf_link) {
    snprintf(buf, sizeof(buf), "rar://%s/%s", url, c->rf_name);
    fa_dir_add(fd, buf, c->rf_name, c->rf_type);
  }

  rar_file_unref(rf);
  return 0;
}


typedef struct rar_ref {
  fa_handle_t h;
  rar_file_t *file;
} rar_ref_t;

/**
 *
 */
static fa_handle_t *
rar_reference(fa_protocol_t *fap, const char *url)
{
  rar_file_t *rf;
  rar_ref_t *zr;

  if((rf = rar_file_find(url)) == NULL)
    return NULL;

  zr = malloc(sizeof(rar_ref_t));
  zr->h.fh_proto = fap;
  zr->file = rf;
  return &zr->h;
}


/**
 *
 */
static void
rar_unreference(fa_handle_t *fh)
{
  rar_ref_t *zr = (rar_ref_t *)fh;
  
  rar_file_unref(zr->file);
  free(fh);
}


/**
 *
 */
typedef struct rar_fd {
  fa_handle_t h;
  rar_file_t *rfd_file;
  rar_segment_t *rfd_segment;
  void *rfd_fh;
  int64_t rfd_fpos;
} rar_fd_t;


/**
 *
 */
static fa_handle_t *
rar_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	 int flags, struct fa_open_extra *foe)
{
  rar_file_t *rf;
  rar_fd_t *rfd;

  if((rf = rar_file_find(url)) == NULL) {
    snprintf(errbuf, errlen, "Entry not found in archive");
    return NULL;
  }

  if(rf->rf_type != CONTENT_FILE) {
    rar_file_unref(rf);
    snprintf(errbuf, errlen, "Entry is not a file");
    return NULL;
  }

  if(rf->rf_method != '0') {
    rar_file_unref(rf);
    snprintf(errbuf, errlen,
	     "Compressed files in RAR archives is not supported");
    return NULL;
  }

  rfd = calloc(1, sizeof(rar_fd_t));
  rfd->rfd_file = rf;

  rfd->h.fh_proto = fap;
  return &rfd->h;
}


/**
 *
 */
static void 
rar_close(fa_handle_t *handle)
{
  rar_fd_t *rfd = (rar_fd_t *)handle;

  if(rfd->rfd_fh != NULL)
    fa_close(rfd->rfd_fh);

  rar_file_unref(rfd->rfd_file);
  free(rfd);
}


/**
 * Read from file
 */
static int
rar_read(fa_handle_t *handle, void *buf, size_t size)
{
  rar_fd_t *rfd = (rar_fd_t *)handle;
  rar_file_t *rf = rfd->rfd_file;
  rar_segment_t *rs;
  size_t c = 0, r, w;
  int64_t o;
  int x;

  if(rfd->rfd_fpos + size > rf->rf_size)
    size = rf->rf_size - rfd->rfd_fpos;

  while(c < size) {
    if((rs = rfd->rfd_segment) == NULL || 
       rfd->rfd_fpos < rs->rs_offset ||
       rfd->rfd_fpos >= rs->rs_offset + rs->rs_size) {
      
      if(rfd->rfd_fh != NULL) {
	fa_close(rfd->rfd_fh);
	rfd->rfd_fh = NULL;
      }

      TAILQ_FOREACH(rs, &rf->rf_segments, rs_link) {
	if(rfd->rfd_fpos < rs->rs_offset + rs->rs_size)
	  break;
      }
      if(rs == NULL)
	return -1;
    }

    w = size - c;
    r = rs->rs_offset + rs->rs_size - rfd->rfd_fpos;

    if(w < r)
      r = w;
    
    if(rfd->rfd_fh == NULL) {
      rfd->rfd_fh = fa_open(rs->rs_volume->rv_url, NULL, 0);
      if(rfd->rfd_fh == NULL)
	return -2;
    }

    o = rfd->rfd_fpos - rs->rs_offset + rs->rs_voffset;

    if(fa_seek(rfd->rfd_fh, o, SEEK_SET) < 0) {
      return -1;
    }
    x = fa_read(rfd->rfd_fh, buf + c, r);
    
    if(x != r)
      return -1;

    rfd->rfd_fpos += x;
    c += x;
  }
  return c;
}


/**
 * Seek in file
 */
static int64_t
rar_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  rar_fd_t *rfd = (rar_fd_t *)handle;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = rfd->rfd_fpos + pos;
    break;

  case SEEK_END:
    np = rfd->rfd_file->rf_size + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;

  rfd->rfd_fpos = np;
  return np;
}


/**
 * Return size of file
 */
static int64_t
rar_fsize(fa_handle_t *handle)
{
  rar_fd_t *rfd = (rar_fd_t *)handle;

  return rfd->rfd_file->rf_size;
}

/**
 * Standard unix stat
 */
static int
rar_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	 int flags, char *errbuf, size_t errlen)
{
  rar_file_t *rf;

  if((rf = rar_file_find(url)) == NULL) {
    snprintf(errbuf, errlen, "Entry not found in archive");
    return -1;
  }

  memset(fs, 0, sizeof(struct fa_stat));

  fs->fs_type = rf->rf_type;
  fs->fs_size = rf->rf_size;
  fs->fs_mtime = rf->rf_archive->ra_mtime;

  rar_file_unref(rf);
  return 0;
}


/**
 *
 */
static int
rar_get_parts(fa_dir_t *fd, const char *url,
	      char *errbuf, size_t errsize)
{
  const char *r;
  rar_archive_t *ra = rar_archive_find(url, &r);
  rar_volume_t *rv;

  if(ra == NULL)
    return -1;

  LIST_FOREACH(rv, &ra->ra_volumes, rv_link)
    fa_dir_add(fd, rv->rv_url, rv->rv_url, CONTENT_FILE);
  return 0;
}


/**
 *
 */
static void
rar_init(void)
{
  hts_mutex_init(&rar_global_mutex);
}


static fa_protocol_t fa_protocol_rar = {
  .fap_init = rar_init,
  .fap_name = "rar",
  .fap_scan =  rar_scandir,
  .fap_open  = rar_open,
  .fap_close = rar_close,
  .fap_read  = rar_read,
  .fap_seek  = rar_seek,
  .fap_fsize = rar_fsize,
  .fap_stat  = rar_stat,
  .fap_reference = rar_reference,
  .fap_unreference = rar_unreference,
  .fap_get_parts = rar_get_parts,
  
};
FAP_REGISTER(rar);
