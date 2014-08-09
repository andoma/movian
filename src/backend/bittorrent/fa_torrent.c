#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_proto.h"

#include "bittorrent.h"
#include "misc/str.h"


/**
 *
 */
static torrent_file_t *
torrent_resolve_file(const char *url, char *errbuf, size_t errlen)
{
  torrent_t *to = torrent_open_url(&url, errbuf, errlen);
  if(to == NULL || url == NULL)
    return NULL;

  torrent_file_t *tf;
  TAILQ_FOREACH(tf, &to->to_files, tf_torrent_link) {
    if(!strcmp(url, tf->tf_fullpath))
      return tf;
  }
  torrent_release(to);
  return NULL;
}


/**
 *
 */
static int
torrent_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
                char *errbuf, size_t errlen)
{
  torrent_t *to = torrent_open_url(&url, errbuf, errlen);
  if(to == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return -1;
  }

  struct torrent_file_queue *tfq;

  if(url == NULL) {
    tfq = &to->to_root;
  } else {
    torrent_file_t *tf;
    TAILQ_FOREACH(tf, &to->to_files, tf_torrent_link) {
      if(!strcmp(url, tf->tf_fullpath))
        break;
    }

    if(tf == NULL || tf->tf_size) {
      snprintf(errbuf, errlen, "Not such directory");
      hts_mutex_unlock(&bittorrent_mutex);
      return -1;
    }
    tfq = &tf->tf_files;
  }

  char hashstr[41];
  bin2hex(hashstr, sizeof(hashstr), to->to_info_hash, 20);

  torrent_file_t *tf;
  TAILQ_FOREACH(tf, tfq, tf_parent_link) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "torrentfile://%s/%s",
             hashstr, tf->tf_fullpath);
    fa_dir_add(fd, buf, tf->tf_name,
               tf->tf_size ? CONTENT_FILE : CONTENT_DIR);
  }
  hts_mutex_unlock(&bittorrent_mutex);
  return 0;
}


/**
 *
 */
static fa_handle_t *
torrent_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
             int flags, struct fa_open_extra *foe)
{
  torrent_file_t *tf = torrent_resolve_file(url, errbuf, errlen);
  if(tf == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return NULL;
  }

  torrent_fh_t *tfh = calloc(1, sizeof(torrent_fh_t));

  if(foe != NULL) {
    tfh->tfh_fa_stats = prop_ref_inc(foe->foe_stats);
    prop_set(tfh->tfh_fa_stats, "bitrateValid", PROP_SET_INT, 1);
    prop_set(tfh->tfh_fa_stats, "infoValid", PROP_SET_INT, 1);
  }
  tfh->tfh_file = tf;
  torrent_t *to = tf->tf_torrent;
  LIST_INSERT_HEAD(&tf->tf_fhs, tfh, tfh_torrent_file_link);
  LIST_INSERT_HEAD(&to->to_fhs, tfh, tfh_torrent_link);
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
                       tf->tf_offset + tfh->tfh_fpos, size,
		       tfh);

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

  LIST_REMOVE(tfh, tfh_torrent_file_link);
  LIST_REMOVE(tfh, tfh_torrent_link);

  torrent_release(tfh->tfh_file->tf_torrent);

  hts_mutex_unlock(&bittorrent_mutex);

  prop_ref_dec(tfh->tfh_fa_stats);
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
  torrent_file_t *tf = torrent_resolve_file(url, errbuf, errlen);
  if(tf == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return -1;
  }

  memset(fs, 0, sizeof(struct fa_stat));
  fs->fs_size = tf->tf_size;
  fs->fs_mtime = 0;
  fs->fs_type = tf->tf_size ? CONTENT_FILE : CONTENT_DIR;

  torrent_release(tf->tf_torrent);

  hts_mutex_unlock(&bittorrent_mutex);

  return 0;
}


/**
 *
 */
static void
torrent_deadline(fa_handle_t *fh, int deadline)
{
  torrent_fh_t *tfh = (torrent_fh_t *)fh;
  tfh->tfh_deadline = showtime_get_ts() + deadline;
}


typedef struct torrent_fh_ref {
  fa_handle_t h;
  torrent_t *to;
} torrent_fh_ref_t;


/**
 *
 */
static fa_handle_t *
torrent_reference(fa_protocol_t *fap, const char *url)
{
  torrent_t *to = torrent_open_url(&url, NULL, 0);
  if(to == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return NULL;
  }

  torrent_fh_ref_t *tfr = malloc(sizeof(torrent_fh_ref_t));
  tfr->h.fh_proto = fap;
  tfr->to = to;
  to->to_refcount++;
  hts_mutex_unlock(&bittorrent_mutex);
  return &tfr->h;
}


/**
 *
 */
static void
torrent_unreference(fa_handle_t *fh)
{
  torrent_fh_ref_t *tfr = (torrent_fh_ref_t *)fh;
  hts_mutex_lock(&bittorrent_mutex);
  torrent_release(tfr->to);
  hts_mutex_unlock(&bittorrent_mutex);
  free(fh);
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
  .fap_deadline    = torrent_deadline,
  .fap_reference   = torrent_reference,
  .fap_unreference = torrent_unreference,
};
FAP_REGISTER(torrent);
