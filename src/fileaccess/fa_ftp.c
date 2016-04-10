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
#include <stdio.h>
#include <assert.h>

#include "main.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "keyring.h"
#include "networking/net.h"
#include "misc/str.h"
#include "misc/callout.h"
#include "misc/time.h"

#include "ftpparse.h"
#include "usage.h"

LIST_HEAD(ftp_connection_list, ftp_connection);

// Parked connections
static struct ftp_connection_list ftp_connections;
static hts_mutex_t ftp_global_mutex;
static atomic_t id_tally;

/**
 *
 */
typedef struct ftp_connection {
  LIST_ENTRY(ftp_connection) fc_link;

  int fc_id;

  tcpcon_t *fc_tc;
  char *fc_hostname;
  int fc_port;
  char *fc_url;

  int64_t fc_expire;

  char fc_no_mlst;

} ftp_connection_t;


/**
 *
 */
typedef struct ftp_file {
  fa_handle_t fh;
  ftp_connection_t *ff_fc;
  char *ff_pathx;
  int ff_port;

  int64_t ff_fpos;
  int64_t ff_size;

  tcpcon_t *ff_xfer;

  char ff_hostname[128];
  char ff_pathbuf[1024];

} ftp_file_t;

#define FTP_TRACE(x, ...) do {                 \
    if(gconf.enable_ftp_client_debug)         \
      TRACE(TRACE_DEBUG, "FTP", x, ##__VA_ARGS__);           \
  } while(0)

/**
 *
 */
static int
ftp_read_line(ftp_connection_t *fc, char *buf, size_t size)
{
  if(tcp_read_line(fc->fc_tc, buf, size) < 0) {
    FTP_TRACE("[%d]: Read error", fc->fc_id);
    return -1;
  }

  FTP_TRACE("[%d]: Recv: %s", fc->fc_id, buf);
  return 0;
}

/**
 *
 */
static int
fc_read_result(ftp_connection_t *fc, char *buf, size_t buflen)
{
  char resp[1024];
  char *end;
  int rc;

  do {
    if(ftp_read_line(fc, resp, sizeof(resp)) < 0)
      return -1;

    rc = strtol(resp, &end, 10);
  } while(*end == '-');

  const char *msg = strchr(resp, ' ');
  if(msg != NULL)
    msg++;

  if(buf != NULL && buflen) {
    if(msg != NULL) {
      snprintf(buf, buflen, "%s", msg);
    } else {
      *buf = 0;
    }
  }

  if(rc == 412) {
    TRACE(TRACE_ERROR, "FTP", "Disconnected: %s", msg ?: "<no reason>");
    return -1;
  }
  return rc;
}


/**
 *
 */
static void
fc_write(ftp_connection_t *fc, const char *fmt, ...)
{
  va_list ap;
  char buf[1024];

  va_start(ap, fmt);
  int r = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  FTP_TRACE("[%d]: Send: %s", fc->fc_id, buf);
  tcp_write_data(fc->fc_tc, buf, r);
}


/**
 *
 */
static void
fc_disconnect(ftp_connection_t *fc)
{
  free(fc->fc_hostname);
  free(fc->fc_url);
  tcp_close(fc->fc_tc);
  free(fc);
}


/**
 *
 */
static ftp_connection_t *
fc_connect(const char *hostname, int port,
           char *errbuf, size_t errlen,
           int non_interactive)
{
  tcpcon_t *tc;
  ftp_connection_t *fc;
  int64_t now = arch_get_ts();
  int id;

 again:
  hts_mutex_lock(&ftp_global_mutex);

  LIST_FOREACH(fc, &ftp_connections, fc_link) {
    if(!strcmp(fc->fc_hostname, hostname) && fc->fc_port == port)
      break;
  }

  if(fc != NULL) {
    LIST_REMOVE(fc, fc_link);
    hts_mutex_unlock(&ftp_global_mutex);

    // Verify connection still works
    if(now > fc->fc_expire) {
      fc_write(fc, "NOOP\n");
      int r = fc_read_result(fc, NULL, 0);
      if(r == 200)
        return fc;

      fc_disconnect(fc);
      goto again;
    }
    return fc;

  } else {
    hts_mutex_unlock(&ftp_global_mutex);
  }

  if((tc = tcp_connect(hostname, port, errbuf, errlen, 5000, 0, NULL)) == NULL)
    return NULL;

  fc = calloc(1, sizeof(ftp_connection_t));
  fc->fc_tc = tc;
  id = atomic_add_and_fetch(&id_tally, 1);
  fc->fc_id = id;

  // Check welcome message

  int r = fc_read_result(fc, NULL, 0);
  if(r != 220) {
    snprintf(errbuf, errlen, "Invalid welcome, expected 220 got %d", r);
    goto bad;
  }

  char *username;
  char *password;
  char buf1[256];
  int attempt = 0;
  char reason[256];
  strcpy(reason, "Login");
  snprintf(buf1, sizeof(buf1), "ftp://%s:%d", hostname, port);

  while(1) {

    if(attempt && non_interactive)
      goto bad;

    int r = keyring_lookup(buf1, &username, &password, NULL, NULL,
                           "FTP Client", reason,
                           (attempt > 0 ? KEYRING_QUERY_USER : 0) |
                           KEYRING_SHOW_REMEMBER_ME | KEYRING_REMEMBER_ME_SET);

    attempt++;
    switch(r) {
    case KEYRING_NOT_FOUND:
      continue;

    case KEYRING_USER_REJECTED:
      snprintf(errbuf, errlen, "Rejected by user");
      goto bad;

    case KEYRING_OK:
      break;
    }

    fc_write(fc, "USER %s\n", username);
    r = fc_read_result(fc, NULL, 0);
    fc_write(fc, "PASS %s\n", password);
    r = fc_read_result(fc, reason, sizeof(reason));
    if(r == 230)
      break;
  }

  fc_write(fc, "TYPE I\n");
  r = fc_read_result(fc, reason, sizeof(reason));
  if(r != 200) {
    snprintf(errbuf, errlen, "Unable to set binary mode -- %s", reason);
    goto bad;
  }

  fc->fc_hostname = strdup(hostname);
  fc->fc_port = port;

  if(port != 21)
    snprintf(buf1, sizeof(buf1), "ftp://%s:%d", hostname, port);
  else
    snprintf(buf1, sizeof(buf1), "ftp://%s", hostname);
  fc->fc_url = strdup(buf1);

  usage_event("FTP Client", 1, NULL);

  return fc;

 bad:
  tcp_close(tc);
  free(fc);
  return NULL;
}


/**
 *
 */
static void
ftp_file_release(ftp_file_t *ff, int drop)
{
  ftp_connection_t *fc = ff->ff_fc;

  assert(ff->ff_xfer == NULL);

  if(fc != NULL) {

    if(drop) {

      fc_disconnect(fc);

    } else {

      fc->fc_expire = arch_get_ts() + 10000000;
      hts_mutex_lock(&ftp_global_mutex);
      LIST_INSERT_HEAD(&ftp_connections, fc, fc_link);
      hts_mutex_unlock(&ftp_global_mutex);
    }
  }
  free(ff);
}


/**
 *
 */
static tcpcon_t *
ftp_open_data_transfer(ftp_connection_t *fc)
{
  char buf[128];
  int d[6];
  char host[32];
  fc_write(fc, "PASV\n");

  int r = fc_read_result(fc, buf, sizeof(buf));
  if(r != 227)
    return NULL;

  if(sscanf(buf, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
            d,d+1,d+2,d+3,d+4,d+5) != 6) {
    return NULL;
  }

  snprintf(host, sizeof(host), "%d.%d.%d.%d", d[0], d[1], d[2], d[3]);
  int port = d[4] * 256 + d[5];
  tcpcon_t *tc = tcp_connect(host, port, buf, sizeof(buf), 5000, 0, NULL);
  if(tc == NULL) {
    TRACE(TRACE_ERROR, "FTP", "Data channel connection failed to %s:%d -- %s",
          host, port, buf);
  } else {
    FTP_TRACE("[%d]: Data channel connected to %s:%d", fc->fc_id, host, port);
  }
  return tc;
}


/**
 *
 */
static int
ff_reconnect(ftp_file_t *ff, char *errbuf, int errlen, int non_interactive)
{
  if(ff->ff_fc != NULL)
    return 0;

  ff->ff_fc = fc_connect(ff->ff_hostname, ff->ff_port,
			 errbuf, errlen, non_interactive);

  return ff->ff_fc == NULL;
}



/**
 *
 */
static ftp_file_t *
ftp_file_init(const char *url, char *errbuf, int errlen, int non_interactive)
{
  ftp_file_t *ff = malloc(sizeof(ftp_file_t));
  char proto[16];

  ff->ff_port = 0;

  url_split(proto, sizeof(proto), NULL, 0,
	    ff->ff_hostname, sizeof(ff->ff_hostname), &ff->ff_port,
            ff->ff_pathbuf, sizeof(ff->ff_pathbuf), url);

  if(ff->ff_port < 0)
    ff->ff_port = 21;

  if(ff->ff_pathbuf[0] == '/')
    ff->ff_pathx = ff->ff_pathbuf + 1;
  else
    ff->ff_pathx = ff->ff_pathbuf;

  ff->ff_fpos = 0;
  ff->ff_size = -1;
  ff->ff_xfer = NULL;
  ff->ff_fc = NULL;

  if(ff_reconnect(ff, errbuf, errlen, non_interactive)) {
    free(ff);
    return NULL;
  }
  return ff;
}


/**
 *
 */
static int64_t
ftp_file_size(ftp_file_t *ff)
{
  char ret[64];
  if(ff->ff_size != -1)
    return ff->ff_size;

  fc_write(ff->ff_fc, "SIZE %s\n", ff->ff_pathx);
  int r = fc_read_result(ff->ff_fc, ret, sizeof(ret));

  if(r == 213)
    ff->ff_size = strtoll(ret, NULL, 10);
  return ff->ff_size;
}

#if 0
/**
 *
 */
static int
ftp_list_dir_MLSD(ftp_connection_t *fc, const char *path, fa_dir_t *fd)
{
  tcpcon_t *tc = ftp_open_data_transfer(ff->ff_fc);

  fc_write(fc, "MLST %s\n", ff->ff_pathx);
  int r = fc_read_result(ff->ff_fc, ret, sizeof(ret));

}
#endif


/**
 *
 */
static int
ftp_add_dir_entry(ftp_connection_t *fc, const char *path, fa_dir_t *fd,
                  char *n)
{
  char buf[512];
  char fname[512];
  struct ftpparse fp;

  if(!ftpparse(&fp, n, strlen(n)))
    return 1;

  if(fp.namelen == 0)
    return 0;

  int type;
  if(fp.flagtrycwd)
    type = CONTENT_DIR;
  else
    type = CONTENT_FILE;

  snprintf(fname, sizeof(fname), "%.*s", fp.namelen, fp.name);

  snprintf(buf, sizeof(buf), "%s/%s%s%s", fc->fc_url, path,
           *path ? "/" : "", fname);

  fa_dir_entry_t *fde = fa_dir_add(fd, buf, fname, type);
  if(fde != NULL) {
    fde->fde_statdone = 1;
    fde->fde_stat.fs_mtime = fp.mtime;
    fde->fde_stat.fs_size = fp.size;
    fde->fde_stat.fs_type = type;
  }
  return 0;
}


/**
 *
 */
static int
ftp_list_dir_LIST(ftp_connection_t *fc, const char *path, fa_dir_t *fd)
{
  tcpcon_t *tc = ftp_open_data_transfer(fc);
  if(tc == NULL)
    return -1;

  char resp[1024];

  fc_write(fc, "LIST %s\n", path[0] ? path : ".");

  while(1) {
    if(tcp_read_line(tc, resp, sizeof(resp)) < 0)
      break;
    FTP_TRACE("[%dd]: Recv: %s", fc->fc_id, resp);
    ftp_add_dir_entry(fc, path, fd, resp);
  }
  FTP_TRACE("[%dd]: Closed", fc->fc_id);

  tcp_close(tc);

  while(1) {
    int r = fc_read_result(fc, NULL, 0);
    if(r == -1)
      return -1;

    if(r < 200)
      continue;
    if(r >= 400)
      return -1;
    return 0;
  }
}


/**
 *
 */
static int
ftp_list_dir_STAT(ftp_connection_t *fc, const char *path, fa_dir_t *fd)
{
  char resp[1024];
  char *n;

  fc_write(fc, "STAT %s\n", path[0] ? path : ".");

  while(1) {

    if(ftp_read_line(fc, resp, sizeof(resp)) < 0)
      return -1;

    n = resp;
    if(!strncmp(n, "211-", 4))
      n += 4;

    if(ftp_add_dir_entry(fc, path, fd, n)) {
      char *end;
      int rc = strtol(resp, &end, 10);
      if(rc && *end == ' ')
        return rc < 211 || rc > 213;
    }
  }
}


/**
 *
 */
static int
ftp_list_dir(ftp_connection_t *fc, const char *path, fa_dir_t *fd)
{
  if(0)
    return ftp_list_dir_STAT(fc, path, fd);
  return ftp_list_dir_LIST(fc, path, fd);
}

/**
 *
 */
static int
ftp_scandir(fa_protocol_t *fap, fa_dir_t *fd,
            const char *url, char *errbuf, size_t errlen, int flags)
{
  ftp_file_t *ff = ftp_file_init(url, errbuf, errlen, 0);
  if(ff == NULL)
    return 1;

  int r = ftp_list_dir(ff->ff_fc, ff->ff_pathx, fd);
  ftp_file_release(ff, 0);
  return r;
}


/**
 *
 */
static fa_handle_t *
ftp_open(struct fa_protocol *fap, const char *url,
         char *errbuf, size_t errsize, int flags,
         struct fa_open_extra *foe)
{
  ftp_file_t *ff = ftp_file_init(url, errbuf, errsize, 0);
  if(ff == NULL)
    return NULL;

  ftp_file_size(ff);

  ff->fh.fh_proto = fap;
  return &ff->fh;
}


/**
 *
 */
static void
ftp_close(fa_handle_t *fh)
{
  ftp_file_t *ff = (ftp_file_t *)fh;
  int drop = 0;
  FTP_TRACE("[%dd]: Close, xfer:%s", ff->ff_fc->fc_id,
            ff->ff_xfer ? "yes" : "no");
  if(ff->ff_xfer != NULL) {
    tcp_close(ff->ff_xfer);
    ff->ff_xfer = NULL;
    fc_read_result(ff->ff_fc, NULL, 0);
    drop = 1;
  }
  // Disconnect control channel if we got a permanent error
  ftp_file_release(ff, drop);
}


/**
 * Read from file. Same semantics as POSIX read(2)
 */
static int
ftp_read(fa_handle_t *fh, void *buf, size_t size0)
{
  ftp_file_t *ff = (ftp_file_t *)fh;
  int64_t size = size0;

  if(ff_reconnect(ff, NULL, 0, 0))
    return -1;

  if(ff->ff_fpos + size > ff->ff_size)
    size = ff->ff_size - ff->ff_fpos;

  if(size <= 0)
    return 0;

  if(ff->ff_xfer == NULL) {
    ff->ff_xfer = ftp_open_data_transfer(ff->ff_fc);
    if(ff->ff_xfer == NULL)
      return -1;

    fc_write(ff->ff_fc, "REST %"PRId64"\n", ff->ff_fpos);
    int r = fc_read_result(ff->ff_fc, NULL, 0);
    if(r != 350)
      return -1;

    fc_write(ff->ff_fc, "RETR %s\n", ff->ff_pathx);
    r = fc_read_result(ff->ff_fc, NULL, 0);
    if(r != 150) {
      tcp_close(ff->ff_xfer);
      ff->ff_xfer = NULL;
      return -1;
    }
  }

  size_t rval = 0;
  while(size > 0) {
    int r = tcp_read_data_nowait(ff->ff_xfer, buf, size);
    FTP_TRACE("[%dd]: Recv data: %d", ff->ff_fc->fc_id, r);
    if(r < 0) {
      tcp_close(ff->ff_xfer);
      ff->ff_xfer = NULL;
      fc_read_result(ff->ff_fc, NULL, 0);
      return -1;
    }

    if(r == 0)
      break;

    buf += r;
    size -= r;
    rval += r;
  }
  ff->ff_fpos += rval;
  return rval;
}


/**
 * Seek in file. Same semantics as POSIX lseek(2)
 */
static int64_t
ftp_seek(fa_handle_t *fh, int64_t pos, int whence, int lazy)
{
  ftp_file_t *ff = (ftp_file_t *)fh;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = ff->ff_fpos + pos;
    break;

  case SEEK_END:
    if(ff->ff_size == -1)
      return -1;
    np = ff->ff_size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  FTP_TRACE("After seek pos = %"PRId64" np = %"PRId64"", ff->ff_fpos, np);

  if(ff->ff_fpos == np)
    return ff->ff_fpos;

  if(ff->ff_xfer != NULL) {
    assert(ff->ff_fc != NULL);
    FTP_TRACE("[%dd]: Closing due to seek", ff->ff_fc->fc_id);
    tcp_close(ff->ff_xfer);
    ff->ff_xfer = NULL;
    int r = fc_read_result(ff->ff_fc, NULL, 0);
    FTP_TRACE("[%d]: Read terminated with %d", ff->ff_fc->fc_id, r);

    if(r == -1) {
      fc_disconnect(ff->ff_fc);
      ff->ff_fc = NULL;
    }

    if(r >= 500)
      return -1;
  }

  ff->ff_fpos = np;
  FTP_TRACE("After seek pos = %"PRId64"", ff->ff_fpos);
  return ff->ff_fpos;
}


/**
 * Return size of file
 */
static int64_t
ftp_fsize(fa_handle_t *fh)
{
  ftp_file_t *ff = (ftp_file_t *)fh;

  if(ff_reconnect(ff, NULL, 0, 0))
    return -1;

  return ftp_file_size(ff);
}


static int
mkint(const char *s, int len)
{
  int i;
  int r = 0;
  for(i = 0; i < len; i++)
    r = r * 10 + s[i] - '0';
  return r;
}

/**
 * stat(2) file
 *
 * If non_interactive is set, this is probe request and it must not
 * ask for any user input (access credentials, etc)
 */
static int
ftp_stat(struct fa_protocol *fap, const char *url, struct fa_stat *fs,
         int flags, char *errbuf, size_t errsize)
{
  int non_interactive = flags & FA_NON_INTERACTIVE ? 1 : 0;
  ftp_file_t *ff = ftp_file_init(url, errbuf, errsize, non_interactive);

  if(ff == NULL)
    return FAP_ERROR;

  memset(fs, 0, sizeof(struct fa_stat));

  char resp[1024];
  ftp_connection_t *fc = ff->ff_fc;

  if(!fc->fc_no_mlst) {

    fc_write(fc, "MLST %s\n", ff->ff_pathx);
    if(ftp_read_line(fc, resp, sizeof(resp)) < 0)
      goto bad;
    if(mystrbegins(resp, "550"))
      goto bad;

    if(mystrbegins(resp, "250-")) {
      int rval = FAP_ERROR;
      if(ftp_read_line(fc, resp, sizeof(resp)) < 0)
        goto bad;
      const char *t = mystrstr(resp, "type=");
      const char *m = mystrstr(resp, "modify=");
      const char *s = mystrstr(resp, "size=");

      if(t != NULL && m != NULL) {
        t += strlen("type=");
        m += strlen("modify=");
        if(s != NULL)
          s += strlen("size=");

        if(!strncmp(t, "cdir", 4) || !strncmp(t, "dir", 3)) {
          fs->fs_type = CONTENT_DIR;
        } else if(!strncmp(t, "file", 4) && s != NULL) {
          fs->fs_type = CONTENT_FILE;
          fs->fs_size = strtoll(s, NULL, 10);
        } else {
          goto bad;
        }

        if(strlen(m) >= 14 && !mktime_utc(&fs->fs_mtime,
                                          mkint(m,     4),
                                          mkint(m +  4, 2) - 1,
                                          mkint(m +  6, 2),
                                          mkint(m +  8, 2),
                                          mkint(m + 10, 2),
                                          mkint(m + 12, 2))) {
          rval = FAP_OK;
        }
      }

      if(ftp_read_line(fc, resp, sizeof(resp)) < 0)
        goto bad;
      ftp_file_release(ff, 0);
      return rval;
    } else {
      FTP_TRACE("[%d]: Server does not undestand MLST, not using that anymore",
                fc->fc_id);
      fc->fc_no_mlst = 1;
    }
  }
  if(ff->ff_pathx[0] == 0) {
    fs->fs_type = CONTENT_DIR;
  } else {
    // Get parent dir
    char *r = strrchr(ff->ff_pathx, '/');
    if(r != NULL) {
      if(r[1] == 0)
        *r = 0;
      r = strrchr(ff->ff_pathx, '/');
    }
    const char *dir;
    const char *fname;

    if(r == NULL) {
      fname = ff->ff_pathx;
      dir = "";
    } else {
      *r = 0;
      fname = r + 1;
      dir = ff->ff_pathx;
    }


    fa_dir_t *fd = fa_dir_alloc();
    if(ftp_list_dir(ff->ff_fc, dir, fd)) {
      fa_dir_free(fd);
      goto bad;
    }

    fa_dir_entry_t *fde;

    RB_FOREACH(fde, &fd->fd_entries, fde_link)
      if(!strcmp(rstr_get(fde->fde_filename), fname))
        break;

    if(fde == NULL) {
      fa_dir_free(fd);
      goto bad;
    }

    *fs = fde->fde_stat;
    fa_dir_free(fd);
  }
  ftp_file_release(ff, 0);
  return FAP_OK;
 bad:
  ftp_file_release(ff, 0);
  return FAP_ERROR;
}


/**
 *
 */
static void
ftp_init(void)
{
  hts_mutex_init(&ftp_global_mutex);
  ftpparse_init();
}


/**
 * Main FTP protocol dispatch
 */
static fa_protocol_t fa_protocol_ftp = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL | FAP_ALLOW_CACHE,
  .fap_init  = ftp_init,
  .fap_name  = "ftp",
  .fap_scan  = ftp_scandir,
  .fap_open  = ftp_open,
  .fap_close = ftp_close,
  .fap_read  = ftp_read,
  .fap_seek  = ftp_seek,
  .fap_fsize = ftp_fsize,
  .fap_stat  = ftp_stat,
};
FAP_REGISTER(ftp);
