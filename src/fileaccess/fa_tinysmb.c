/*
 *  File access using libsmbclient
 *  Copyright (C) 2010 Andreas Öman
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

#include <stdio.h>
#include <string.h>

#include "showtime.h"
#include "fa_proto.h"
#include "keyring.h"

#include <sys/statvfs.h>
#include <smb.h>



TAILQ_HEAD(smb_connection_queue , smb_connection);

static struct smb_connection_queue smb_connections;
static int smb_parked_connections;
static hts_mutex_t smb_connections_mutex;



typedef struct smb_connection {
  TAILQ_ENTRY(smb_connection) sc_link;

  SMBCONN sc_conn;

  char *sc_host;
  char *sc_share;

} smb_connection_t;

/**
 *
 */
static void
smb_init(void)
{
  TAILQ_INIT(&smb_connections);
  hts_mutex_init(&smb_connections_mutex);
}


/**
 *
 */
static const char *
smberr2str(int code)
{
  switch(code) {
  case SMB_SUCCESS: return "Success";
  case SMB_BAD_PROTOCOL:  return "Bad protocol";
  case SMB_BAD_COMMAND:   return "Bad command";
  case SMB_PROTO_FAIL:    return "Protocol failure";
  case SMB_NOT_USER:      return "Not user";
  case SMB_BAD_KEYLEN:    return "Bad key length";
  case SMB_BAD_DATALEN:   return "Bad data length";
  case SMB_BAD_LOGINDATA: return "Access denied";
  default:                return "Error";
  }
}

/**
 *
 */
static void
makesmbname(char *s)
{
  while(*s) {
    if(*s == '/')
      *s = '\\';
    s++;
  }
}


/**
 *
 */
static smb_connection_t *
get_connection(const char *url, const char **fname,
	       char *errbuf, size_t errlen, int *non_interactive)
{
  char host[HOSTNAME_MAX];
  char share[SMB_MAXPATH];
  char buf1[512];
  char *username;
  char *password;
  SMBCONN conn;
  int smb_err, kr_ret, retry = 0, i;
  smb_connection_t *sc;

  for(i = 0; *url != '/' && *url != '\\' && url && i < sizeof(host) - 1; i++)
    host[i] = *url++;
  host[i] = 0;

  if(i == 0) {
    snprintf(errbuf, errlen, "No hostname in URL");
    return NULL;
  }

  while(*url == '/' || *url == '\\')
    url++;

  for(i = 0; *url != '/' && *url != '\\'&& *url && i < sizeof(share) - 1; i++)
    share[i] = *url++;
  share[i] = 0;

  if(i == 0) {
    snprintf(errbuf, errlen, "No service/share in URL");
    return NULL;
  }

  while(*url == '/' || *url == '\\')
    url++;

  *fname = url;

  hts_mutex_lock(&smb_connections_mutex);

  TAILQ_FOREACH(sc, &smb_connections, sc_link) {
    if(!strcmp(sc->sc_host, host) && !strcmp(sc->sc_share, share)) {
      TAILQ_REMOVE(&smb_connections, sc, sc_link);
      smb_parked_connections--;
      hts_mutex_unlock(&smb_connections_mutex);
      return sc;
    }
  }
  hts_mutex_unlock(&smb_connections_mutex);
 
  snprintf(buf1, sizeof(buf1), "Samba share: %s on %s", share, host);

  while(1) {

    if(retry && non_interactive) {
      *non_interactive = FAP_STAT_NEED_AUTH;
      return NULL;
    }

    kr_ret = keyring_lookup(buf1, &username, &password, NULL, 
			    retry,
			    "SMB Client", "Unable to connect");
    if(kr_ret == -1) {
      /* Rejected */
      snprintf(errbuf, errlen, "Authentication rejected by user");
      return NULL;
    }

    smb_err = SMB_Connect(&conn, 
			  kr_ret == 0 ? username : "GUEST",
			  kr_ret == 0 ? password : "",
			  share, host);

    if(kr_ret == 0) {
      free(username);
      free(password);
    }

    if(smb_err != SMB_SUCCESS) {
      if(retry == 0) {
	retry++;
	continue;
      }
      TRACE(TRACE_ERROR, "SMB", 
	    "SMB_Connect() host=\"%s\", service=\"%s\", username=\"%s\", "
	    "password=\"%s\" failed: %s", host, share, username, password, 
	    smberr2str(smb_err));

      snprintf(errbuf, errlen, "SMB Connection failed: %s", 
	       smberr2str(smb_err));
      return NULL;
    }
    sc = calloc(1, sizeof(smb_connection_t));
    sc->sc_conn = conn;
    sc->sc_host = strdup(host);
    sc->sc_share = strdup(share);
    return sc;
  }
}

/**
 *
 */
static void
destroy_connection(smb_connection_t *sc)
{
  SMB_Close(sc->sc_conn);
  free(sc->sc_host);
  free(sc->sc_share);
  free(sc);
}



/**
 *
 */
static void
release_connection(smb_connection_t *sc)
{
  hts_mutex_lock(&smb_connections_mutex);
  TAILQ_INSERT_TAIL(&smb_connections, sc, sc_link);

  if(smb_parked_connections == 5) {
    sc = TAILQ_FIRST(&smb_connections);
    TAILQ_REMOVE(&smb_connections, sc, sc_link);
    destroy_connection(sc);
  } else {
    smb_parked_connections++;
  }

  hts_mutex_unlock(&smb_connections_mutex);
}


/**
 *
 */
typedef struct smb_handle {
  fa_handle_t h;

  SMBFILE file;
  
  smb_connection_t *sc;

  int64_t size;
  int64_t pos;

} smb_handle_t;


/**
 *
 */
static int
smb_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  const char *fname;
  smb_connection_t *sc;
  int r;
  char path[SMB_MAXPATH];
  SMBDIRENTRY entry = {0};
  char eurl[URL_MAX];

  if((sc = get_connection(url, &fname, errbuf, errlen, NULL)) == NULL)
    return -1;

  snprintf(path, sizeof(path), "%s/*", fname);
  makesmbname(path);
  
  r = SMB_FindFirst(path, 
		    SMB_SRCH_DIRECTORY |
		    SMB_SRCH_SYSTEM |
		    SMB_SRCH_HIDDEN |
		    SMB_SRCH_READONLY |
		    SMB_SRCH_ARCHIVE, 
		    &entry, sc->sc_conn);

  if(r != SMB_SUCCESS) {
    snprintf(errbuf, errlen, "SMB Error: %s", smberr2str(r));
    release_connection(sc);
    return -1;
  }

  if(!(entry.attributes & SMB_SRCH_DIRECTORY)) {
    snprintf(errbuf, errlen, "Not a directory");
    release_connection(sc);
    return -1;
  }

  do {

    if(entry.name[0] != '.') {

      snprintf(eurl, sizeof(eurl), "smb://%s/%s/%s/%s",
	       sc->sc_host, sc->sc_share, fname, entry.name);

      fa_dir_add(fd, eurl, entry.name, 
		 entry.attributes & 0x10 ? CONTENT_DIR : CONTENT_FILE);
    }
  } while(SMB_FindNext(&entry, sc->sc_conn) == SMB_SUCCESS);
  
  fa_dir_sort(fd);
  release_connection(sc);
  return 0;
}

/**
 * Open file
 */
static fa_handle_t *
smb_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  int r;
  const char *fname;
  char *smbname;
  smb_connection_t *sc;
  SMBFILE file;
  SMBDIRENTRY sde;

  if((sc = get_connection(url, &fname, errbuf, errlen, NULL)) == NULL)
    return NULL;
  
  smbname = mystrdupa(fname);
  makesmbname(smbname);

  if((r = SMB_PathInfo(smbname, &sde, sc->sc_conn)) != SMB_SUCCESS) {
    snprintf(errbuf, errlen, "SMB Error: %s", smberr2str(r));
    release_connection(sc);
    return NULL;
  }
 
  file = SMB_OpenFile(smbname, SMB_OPEN_READING, SMB_OF_OPEN, sc->sc_conn);
  if(file == NULL) {
    snprintf(errbuf, errlen, "Unable to open file");
    release_connection(sc);
    return NULL;
  }

  smb_handle_t *fh = malloc(sizeof(smb_handle_t));
  fh->file = file;
  fh->sc = sc;
  fh->pos = 0;
  fh->size = sde.size;
  fh->h.fh_proto = fap;
  return &fh->h;
}  
  

/**
 * Close file
 */
static void
smb_close(fa_handle_t *fh0)
{
  smb_handle_t *fh = (smb_handle_t *)fh0;
  SMB_CloseFile(fh->file);
  release_connection(fh->sc);
}

/**
 * Read from file
 */
static int
smb_read(fa_handle_t *fh0, void *buf, size_t size)
{
  smb_handle_t *fh = (smb_handle_t *)fh0;
  int r;

  r = SMB_ReadFile(buf, size, fh->pos, fh->file);
  if(r < 0)
    return -1;

  fh->pos += r;
  return r;
}

/**
 * Seek in file
 */
static off_t
smb_seek(fa_handle_t *fh0, off_t pos, int whence)
{
  smb_handle_t *fh = (smb_handle_t *)fh0;
  int64_t np;
  
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
static off_t
smb_fsize(fa_handle_t *fh0)
{
  smb_handle_t *fh = (smb_handle_t *)fh0;
  return fh->size;
}



/**
 * Standard unix stat
 */
static int
smb_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	 char *errbuf, size_t errlen, int non_interactive)
{
  const char *fname;
  char *smbname;
  smb_connection_t *sc;
  int r;
  SMBDIRENTRY sde;
  int statcode = -1;

  if((sc = get_connection(url, &fname, errbuf, errlen, 
			  non_interactive ? &statcode : NULL)) == NULL)
    return statcode;

  smbname = mystrdupa(fname);
  makesmbname(smbname);

  if((r = SMB_PathInfo(smbname, &sde, sc->sc_conn)) != SMB_SUCCESS) {
    snprintf(errbuf, errlen, "SMB Error: %s", smberr2str(r));
    release_connection(sc);
    return -1;
  }
  
  memset(buf, 0, sizeof(struct stat));
  buf->st_size = sde.size;
  buf->st_mode = sde.attributes & 0x10 ? S_IFDIR : S_IFREG;
  release_connection(sc);
  return 0;
}


static fa_protocol_t fa_protocol_tinysmb = {
  .fap_name  = "smb",
  .fap_init  = smb_init,
  .fap_scan  = smb_scandir,
  .fap_open  = smb_open,
  .fap_close = smb_close,
  .fap_read  = smb_read,
  .fap_seek  = smb_seek,
  .fap_fsize = smb_fsize,
  .fap_stat  = smb_stat,
};
FAP_REGISTER(tinysmb);
