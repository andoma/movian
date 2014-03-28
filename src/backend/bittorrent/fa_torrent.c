#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_proto.h"

#include "bittorrent.h"
#include "misc/str.h"



typedef struct torrent_fh {
  fa_handle_t h;
  LIST_ENTRY(torrent_fh) tfh_link;
  uint64_t tfh_fpos;
  torrent_file_t *tfh_file;

} torrent_fh_t;

/**
 *
 */
static torrent_file_t *
torrent_resolve_file(const char *url)
{
  char buf[41];
  uint8_t infohash[20];

  if(strlen(url) < 41)
    return NULL;
  memcpy(buf, url, 40);
  buf[40] = 0;

  if(hex2bin(infohash, sizeof(infohash), buf) != 20)
    return NULL;

  torrent_t *to = torrent_create(infohash, NULL, NULL, NULL);

  torrent_file_t *tf;
  TAILQ_FOREACH(tf, &to->to_files, tf_torrent_link) {
    if(!strcmp(url + 41, tf->tf_fullpath))
      return tf;
  }
  return NULL;
}


/**
 *
 */
static int
torrent_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url0,
                char *errbuf, size_t errlen)
{
  snprintf(errbuf, errlen, "Not implemented yet");
  return -1;
}


/**
 *
 */
static fa_handle_t *
torrent_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
             int flags, struct fa_open_extra *foe)
{
  hts_mutex_lock(&bittorrent_mutex);

  torrent_file_t *tf = torrent_resolve_file(url);
  if(tf == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    snprintf(errbuf, errlen, "Invalid URL");
    return NULL;
  }

  torrent_fh_t *tfh = calloc(1, sizeof(torrent_fh_t));

  tfh->tfh_file = tf;
  LIST_INSERT_HEAD(&tf->tf_fhs, tfh, tfh_link);
  hts_mutex_unlock(&bittorrent_mutex);
  tfh->h.fh_proto = fap;
  return &tfh->h;
}


/**
 *
 */
static int
torrent_read(fa_handle_t *fh, void *buf, size_t size)
{
  torrent_fh_t *tfh = (torrent_fh_t *)fh;

  hts_mutex_lock(&bittorrent_mutex);

  torrent_file_t *tf = tfh->tfh_file;
  uint64_t fsize = tf->tf_size;

  if(tfh->tfh_fpos >= fsize) {
    hts_mutex_unlock(&bittorrent_mutex);
    return 0;
  }

  if(tfh->tfh_fpos + size > fsize)
    size = fsize - tfh->tfh_fpos;

  if(size == 0) {
    hts_mutex_unlock(&bittorrent_mutex);
    return 0;
  }

  int r = torrent_load(tf->tf_torrent, buf,
                       tf->tf_offset + tfh->tfh_fpos, size);

  hts_mutex_unlock(&bittorrent_mutex);
  tfh->tfh_fpos += r;
  return r;
}


/**
 *
 */
static int64_t
torrent_seek(fa_handle_t *fh, int64_t pos, int whence)
{
  torrent_fh_t *tfh = (torrent_fh_t *)fh;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = tfh->tfh_fpos + pos;
    break;

  case SEEK_END:
    np = tfh->tfh_file->tf_size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  tfh->tfh_fpos = np;
  return np;

}

/**
 *
 */
static void
torrent_close(fa_handle_t *fh)
{
  torrent_fh_t *tfh = (torrent_fh_t *)fh;
  hts_mutex_lock(&bittorrent_mutex);

  LIST_REMOVE(tfh, tfh_link);

  hts_mutex_unlock(&bittorrent_mutex);
  free(tfh);
}


/**
 *
 */
static int64_t
torrent_fsize(fa_handle_t *fh)
{
  torrent_fh_t *tfh = (torrent_fh_t *)fh;
  return tfh->tfh_file->tf_size;
}


/**
 *
 */
static int
torrent_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
             char *errbuf, size_t errlen, int non_interactive)
{
  hts_mutex_lock(&bittorrent_mutex);

  torrent_file_t *tf = torrent_resolve_file(url);
  if(tf == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    snprintf(errbuf, errlen, "Invalid URL");
    return -1;
  }

  memset(fs, 0, sizeof(struct fa_stat));
  fs->fs_size = tf->tf_size;
  fs->fs_mtime = 0;
  fs->fs_type = tf->tf_size ? CONTENT_FILE : CONTENT_DIR;

  hts_mutex_unlock(&bittorrent_mutex);

  return 0;
}



/**
 *
 */
static fa_protocol_t fa_protocol_torrent = {
  .fap_name        = "torrentfile",
  .fap_scan        = torrent_scandir,
  .fap_open        = torrent_open,
  .fap_close       = torrent_close,
  .fap_read        = torrent_read,
  .fap_seek        = torrent_seek,
  .fap_fsize       = torrent_fsize,
  .fap_stat        = torrent_stat,
};
FAP_REGISTER(torrent);
