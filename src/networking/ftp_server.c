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
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#include <netinet/in.h>

#include "main.h"
#include "asyncio.h"
#include "net.h"
#include "arch/threads.h"

#include "settings.h"

#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_proto.h"
#include "htsmsg/htsmsg_store.h"
#include "usage.h"

static asyncio_fd_t *ftp_server_fd;

typedef struct ftp_connection {
  tcpcon_t *fc_tc;
  int fc_authorized;

  char *fc_wd;

  char fc_type;
  char fc_eol[3];

  int fc_accept_socket;

  net_addr_t fc_local_addr;
  net_addr_t fc_remote_addr;

  char *fc_username;

  char *fc_pending_RNFR;

} ftp_connection_t;


#define PRE(code) (-(code))


extern fa_protocol_t fa_protocol_vfs;

static char *ftp_username;
static char *ftp_password;

/**
 *
 */
static int
ftp_server_stat(const char *url, struct fa_stat *fs,
                char *errbuf, size_t errlen)
{
  return fa_protocol_vfs.fap_stat(&fa_protocol_vfs, url, fs, errbuf, errlen, 1);
}


/**
 *
 */
static fa_handle_t *
ftp_server_open(const char *url, char *errbuf, size_t errlen, int flags)
{
  return fa_protocol_vfs.fap_open(&fa_protocol_vfs, url, errbuf, errlen,
                                  flags, NULL);
}


/**
 *
 */
static fa_err_code_t
ftp_server_makedirs(const char *url)
{
  return fa_protocol_vfs.fap_makedir(&fa_protocol_vfs, url);
}


/**
 *
 */
static int
ftp_server_unlink(const char *url, char *errbuf, size_t errlen)
{
  return fa_protocol_vfs.fap_unlink(&fa_protocol_vfs, url, errbuf, errlen);
}


/**
 *
 */
static int
ftp_server_rmdir(const char *url, char *errbuf, size_t errlen)
{
  return fa_protocol_vfs.fap_rmdir(&fa_protocol_vfs, url, errbuf, errlen);
}


/**
 *
 */
static int
ftp_server_rename(const char *old, const char *new, char *errbuf, size_t errlen)
{
  return fa_protocol_vfs.fap_rename(&fa_protocol_vfs, old, new, errbuf, errlen);
}


/**
 *
 */
static fa_dir_t *
ftp_server_scandir(const char *url, char *errbuf, size_t errlen)
{
  fa_dir_t *fd = fa_dir_alloc();

  if(fa_protocol_vfs.fap_scan(&fa_protocol_vfs, fd, url, errbuf, errlen,
                              FA_NON_INTERACTIVE)) {
    fa_dir_free(fd);
    return NULL;
  }
  return fd;
}


/**
 *
 */
static void
set_wd(ftp_connection_t *fc, const char *path)
{
  mystrset(&fc->fc_wd, path);
}


/**
 *
 */
static void
set_type(ftp_connection_t *fc, char type)
{
  fc->fc_type = type;
  if(type == 'I')
    strcpy(fc->fc_eol, "\n'");
  if(type == 'A')
    strcpy(fc->fc_eol, "\r\n");
}


/**
 *
 */
static void
ftp_write(ftp_connection_t *fc, int code, const char *fmt, ...)
{
  char buf[2048];
  va_list ap;
  int len;

  if(code) {
    len = snprintf(buf, sizeof(buf), "%d%s", abs(code), code < 0 ? "-" : " ");
  } else {
    buf[0] = ' ';
    len = 1;
  }
  va_start(ap, fmt);
  len += vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
  va_end(ap);

  if(gconf.enable_ftp_server_debug)
    TRACE(code >= 400 ? TRACE_ERROR : TRACE_DEBUG,
          "FTP-SERVER", "SEND: %s", buf);

  len += snprintf(buf + len, sizeof(buf) - len, "\r\n");
  tcp_write_data(fc->fc_tc, buf, len);
}


/**
 *
 */
static int
cmd_QUIT(ftp_connection_t *fc, char *args)
{
  ftp_write(fc, 221, "Thank you for using the "APPNAMEUSER" FTP service");
  return 1;
}


/**
 *
 */
static int
cmd_USER(ftp_connection_t *fc, char *args)
{
  fc->fc_authorized = 0;
  mystrset(&fc->fc_username, args);
  ftp_write(fc, 331, "Password required");
  return 0;
}


/**
 *
 */
static int
cmd_PASS(ftp_connection_t *fc, char *args)
{
  if(fc->fc_username == NULL) {
    ftp_write(fc, 503, "Login with USER first.");
    return 0;
  }

  if(*ftp_username &&
     (strcmp(fc->fc_username, ftp_username) || strcmp(args, ftp_password))) {
    ftp_write(fc, 530, "Login incorrect.");
    mystrset(&fc->fc_username, NULL);
  } else {
    fc->fc_authorized = 1;
    ftp_write(fc, 230, "User logged in");
  }
  return 0;
}


/**
 *
 */
static int
cmd_SYST(ftp_connection_t *fc, char *args)
{
  ftp_write(fc, 215, "UNIX Type: L8 Version: %s %s",
             htsversion, arch_get_system_type());
  return 0;
}


/**
 *
 */
static int
cmd_FEAT(ftp_connection_t *fc, char *args)
{
  ftp_write(fc, PRE(211), "Features supported");
  ftp_write(fc, 0, "UTF8");
  ftp_write(fc, 0, "SIZE");
  ftp_write(fc, 211, "End");
  return 0;
}


/**
 *
 */
static int
cmd_TYPE(ftp_connection_t *fc, char *args)
{
  if(*args == 'A' || *args == 'I') {
    set_type(fc, *args);
    ftp_write(fc, 200, "Type set to %c.", fc->fc_type);
  } else {
    ftp_write(fc, 500, "Type '%c' not understood", *args);
  }
  return 0;
}


/**
 *
 */
static int
cmd_PWD(ftp_connection_t *fc, char *args)
{
  ftp_write(fc, 257, "\"%s\" is the current directory.", fc->fc_wd);
  return 0;
}


/**
 *
 */
static int
construct_path(char *dst, size_t dstlen,
               ftp_connection_t *fc, const char *path)
{
  int at_root = !strcmp(fc->fc_wd, "/");

  if(!strcmp(path, ".")) {
    snprintf(dst, dstlen, "%s", fc->fc_wd);

  } else if(!strcmp(path, "..")) {

    if(at_root) {
      ftp_write(fc, 550, "%s: Can't go further up", path);
      return 1;
    }

    snprintf(dst, dstlen, "%s", fc->fc_wd);

    char *r = strrchr(dst, '/');
    assert(r != NULL);
    *r = 0;

    if(*dst == 0)
      // did chdir(..) to root, restore root path
      strcpy(dst, "/");

  } else {

    int r = strcspn(path, "\\:?*|<>");
    if(path[r]) {
      ftp_write(fc, 550, "%s: Path contains invalid character: '%c'",
                path, path[r]);
      return 1;
    }

    if(strstr(path, "/..")) {
      ftp_write(fc, 550, "%s: Path contains invalid component: '/..'", path);
      return 1;
    }

    if(*path == '/') {
      snprintf(dst, dstlen, "%s", path);
    } else {
      snprintf(dst, dstlen, "%s%s%s", fc->fc_wd,
               at_root ? "" : "/", path);
    }
  }

  int l = strlen(dst) - 1;

  while(l > 0 && dst[l] == '/')
    dst[l--] = 0;
  return 0;
}


/**
 *
 */
static int
cmd_CWD(ftp_connection_t *fc, char *args)
{
  char newpath[1024];
  char errbuf[256];

  if(args == NULL) {
    strcpy(newpath, "/");
  } else {
    if(construct_path(newpath, sizeof(newpath), fc, args))
      return 0;
  }

  struct fa_stat fs;
  int err = ftp_server_stat(newpath, &fs, errbuf, sizeof(errbuf));

  if(!err && !content_dirish(fs.fs_type)) {
    snprintf(errbuf, sizeof(errbuf), "Not a directory");
    err = 1;
  }

  if(gconf.enable_ftp_server_debug) {

    if(!err)
      TRACE(TRACE_DEBUG, "FTP-SERVER", "CHDIR: '%s' OK", newpath);
    else
      TRACE(TRACE_DEBUG, "FTP-SERVER", "CHDIR: '%s' Failed -- %s",
            newpath, errbuf);
  }

  if(err) {
    ftp_write(fc, 550, "%s: %s", newpath, errbuf);
    return 0;
  }

  set_wd(fc, newpath);

  ftp_write(fc, 250, "CWD command successful.");
  return 0;
}


/**
 *
 */
static int
cmd_PASV(ftp_connection_t *fc, char *args)
{
  struct sockaddr_in si = {0};
  socklen_t sl = sizeof(struct sockaddr_in);
  int fd;

  if(fc->fc_accept_socket != -1) {
    close(fc->fc_accept_socket);
    fc->fc_accept_socket = -1;
  }

  if((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    return 1;

  // XXX: We should bind on same interface as connection arrives
  si.sin_family = AF_INET;

  if(bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
    TRACE(TRACE_ERROR, "FTP-SERVER", "Unable to bind -- %s", strerror(errno));
    close(fd);
    return 1;
  }

  if(getsockname(fd, (struct sockaddr *)&si, &sl) == -1) {
    TRACE(TRACE_ERROR, "FTP-SERVER", "Unable to bind");
    close(fd);
    return 1;
  }

  listen(fd, 1);

  fc->fc_accept_socket = fd;

  uint16_t port = ntohs(si.sin_port);

  ftp_write(fc, 227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
            fc->fc_local_addr.na_addr[0],
            fc->fc_local_addr.na_addr[1],
            fc->fc_local_addr.na_addr[2],
            fc->fc_local_addr.na_addr[3],
            (port >> 8)  & 0xff,
            port        & 0xff);
  return 0;
}


/**
 *
 */
static tcpcon_t *
get_data_channel(ftp_connection_t *fc)
{
  if(fc->fc_accept_socket != -1) {
    // passive mode, accept()
    struct sockaddr_in sin;
    socklen_t slen = sizeof(struct sockaddr_in);
    int fd = accept(fc->fc_accept_socket, (struct sockaddr *)&sin, &slen);
    if(fd == -1) {
      return NULL;
    }

    close(fc->fc_accept_socket);
    fc->fc_accept_socket= -1;

    return tcp_from_fd(fd);
  }


  return NULL;
}


/**
 *
 */
static int
cmd_LIST(ftp_connection_t *fc, char *args)
{
  tcpcon_t *tc = get_data_channel(fc);
  char pathbuf[1024];
  char errbuf[256];

  const char *path;
  if(tc == NULL) {
    ftp_write(fc, 425, "Can't build data connection");
    return 0;
  }

  if(args) {

    // Clean up arguments that some clients wanna pass to "/bin/ls"

    while(*args == ' ')
      args++;

    while(*args == '-') {
      while(*args != ' ' && *args)
        args++;
      while(*args == ' ')
        args++;
    }

    if(*args == 0)
      args = NULL;
  }

  if(args) {
    construct_path(pathbuf, sizeof(pathbuf), fc, args);
    path = pathbuf;
  } else {
    path = fc->fc_wd;
  }

  if(gconf.enable_ftp_server_debug)
    TRACE(TRACE_DEBUG, "FTP-SERVER", "Listing '%s'", path);

  fa_dir_t *fd = ftp_server_scandir(path, errbuf, sizeof(errbuf));
  if(fd == NULL) {
    ftp_write(fc, 400, "No such directory");
    tcp_close(tc);
    return 0;
  }


  ftp_write(fc, 130, "Opening ASCII mode data connection for 'ls'");
  fa_dir_entry_t *fde;
  RB_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(!fde->fde_statdone)
      fa_stat_ex(rstr_get(fde->fde_url), &fde->fde_stat, NULL, 0,
                 FA_NON_INTERACTIVE);

    tcp_printf(tc,
               "%crwx------  1 nobody nobody %10"PRIu64" May  5 11:20 %s\r\n",
               content_dirish(fde->fde_type) ? 'd' : '-',
               fde->fde_stat.fs_size,
               rstr_get(fde->fde_filename));
  }

  fa_dir_free(fd);
  tcp_close(tc);

  ftp_write(fc, 226, "Transfer complete");
  return 0;
}


/**
 *
 */
static int
cmd_SIZE(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];
  char errbuf[256];

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  struct fa_stat fs;
  int err = ftp_server_stat(pathbuf, &fs, errbuf, sizeof(errbuf));
  if(err) {
    ftp_write(fc, 550, "%s: %s", args, errbuf);
  } else if(fs.fs_type == CONTENT_FILE) {
    ftp_write(fc, 213, "%"PRIu64, fs.fs_size);
  } else {
    ftp_write(fc, 550, "%s: not a plain file.", args);
  }
  return 0;
}


/**
 *
 */
static int
cmd_RETR(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];
  char errbuf[256];

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  fa_handle_t *fh = ftp_server_open(pathbuf, errbuf, sizeof(errbuf), 0);
  if(fh == NULL) {
    ftp_write(fc, 550, "%s: %s", args, errbuf);
    return 0;
  }

  ftp_write(fc, 150,
            "Opening BINARY mode data connetion for '%s'", args);

  tcpcon_t *tc = get_data_channel(fc);
  if(tc == NULL) {
    ftp_write(fc, 425, "Can't build data connection");
    return 0;
  }

  const int bufsize = 65536;

  char *readbuf = malloc(bufsize);

  int r;
  int error = 0;
  while((r = fa_read(fh, readbuf, bufsize)) > 0) {
    if(tcp_write_data(tc, readbuf, r)) {
      error = 1;
      break;
    }
  }

  free(readbuf);
  tcp_close(tc);
  fa_close(fh);

  if(error) {
    ftp_write(fc, 400, "Write error");  // XXX errorcode
  } else {
    ftp_write(fc, 226, "Transfer complete");
  }

  return 0;
}


/**
 *
 */
static int
cmd_STOR(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];
  char errbuf[256];

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  fa_handle_t *fh = ftp_server_open(pathbuf, errbuf, sizeof(errbuf),
                                    FA_WRITE);
  if(fh == NULL) {
    ftp_write(fc, 550, "%s: %s", args, errbuf);
    return 0;
  }

  ftp_write(fc, 150,
            "Opening BINARY mode data connetion for '%s'", args);

  tcpcon_t *tc = get_data_channel(fc);
  if(tc == NULL) {
    ftp_write(fc, 425, "Can't build data connection");
    return 0;
  }

  const int bufsize = 65536;
  char *writebuf = malloc(bufsize);
  int error = 0;
  int r;

  while((r = tcp_read_data_nowait(tc, writebuf, bufsize)) > 0) {
    if(fa_write(fh, writebuf, r) != r) {
      error = 1;
      break;
    }
  }

  free(writebuf);
  tcp_close(tc);
  fa_close(fh);

  if(error) {
    ftp_write(fc, 400, "Write error");  // XXX errorcode
  } else {
    ftp_write(fc, 226, "Transfer complete");
  }

  return 0;
}


/**
 *
 */
static int
cmd_MKD(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  fa_err_code_t err = ftp_server_makedirs(pathbuf);

  if(err) {
    ftp_write(fc, 550, "%s: error %d", args, err);
    return 0;
  }
  ftp_write(fc, 257, "\"%s\" directory created.", args);
  return 0;
}


/**
 *
 */
static int
cmd_DELE(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];
  char errbuf[256];

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  if(ftp_server_unlink(pathbuf, errbuf, sizeof(errbuf))) {
    ftp_write(fc, 550, "%s: %s", args, errbuf);
    return 0;
  }
  ftp_write(fc, 250, "DELE command successful.");
  return 0;
}


/**
 *
 */
static int
cmd_RMD(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];
  char errbuf[256];

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  if(ftp_server_rmdir(pathbuf, errbuf, sizeof(errbuf))) {
    ftp_write(fc, 550, "%s: %s", args, errbuf);
    return 0;
  }
  ftp_write(fc, 250, "RMD command successful.");
  return 0;
}


/**
 *
 */
static int
cmd_RNFR(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];
  char errbuf[256];

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  struct fa_stat fs;
  int err = ftp_server_stat(pathbuf, &fs, errbuf, sizeof(errbuf));
  if(err) {
    ftp_write(fc, 550, "%s: %s", args, errbuf);
    return 0;
  }

  mystrset(&fc->fc_pending_RNFR, pathbuf);
  ftp_write(fc, 350, "File exists, ready for destination name");
  return 0;
}


/**
 *
 */
static int
cmd_RNTO(ftp_connection_t *fc, char *args)
{
  char pathbuf[1024];
  char errbuf[256];

  if(fc->fc_pending_RNFR == NULL) {
    ftp_write(fc, 503, "Bad sequence of commands");
    return 0;
  }

  construct_path(pathbuf, sizeof(pathbuf), fc, args);

  int r = ftp_server_rename(fc->fc_pending_RNFR, pathbuf,
                            errbuf, sizeof(errbuf));

  mystrset(&fc->fc_pending_RNFR, NULL);

  if(r) {
    ftp_write(fc, 550, "%s: %s", args, errbuf);
  } else {
    ftp_write(fc, 250, "RNTO command successful.");
  }
  return 0;
}


/**
 *
 */
static int
cmd_OPTS(ftp_connection_t *fc, char *args)
{
  if(!strcasecmp(args, "UTF8 ON")) {
    ftp_write(fc, 200, "UTF8 set to on");
  } else if(!strcasecmp(args, "UTF8 OFF")) {
    ftp_write(fc, 200, "UTF8 set to off");
  } else {
    ftp_write(fc, 500, "%s: Not understood", args);
  }
  return 0;
}



#define FTPCMD_NEED_ARGS 0x1
#define FTPCMD_AUTH_REQ  0x2

struct {
  const char cmd[5];
  int (*fn)(ftp_connection_t *fc, char *args);
  int flags;
} ftpcmds[] = {
  { "QUIT", cmd_QUIT, 0},
  { "USER", cmd_USER, FTPCMD_NEED_ARGS},
  { "PASS", cmd_PASS, FTPCMD_NEED_ARGS},

  { "SYST", cmd_SYST, FTPCMD_AUTH_REQ},
  { "FEAT", cmd_FEAT, FTPCMD_AUTH_REQ},
  { "PWD",  cmd_PWD,  FTPCMD_AUTH_REQ},
  { "XPWD", cmd_PWD,  FTPCMD_AUTH_REQ},
  { "CWD",  cmd_CWD,  FTPCMD_AUTH_REQ},
  { "XCWD", cmd_CWD,  FTPCMD_AUTH_REQ},
  { "PASV", cmd_PASV, FTPCMD_AUTH_REQ},
  { "LIST", cmd_LIST, FTPCMD_AUTH_REQ},
  { "SIZE", cmd_SIZE, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "TYPE", cmd_TYPE, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "RETR", cmd_RETR, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "STOR", cmd_STOR, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "MKD",  cmd_MKD,  FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "XMKD", cmd_MKD,  FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "DELE", cmd_DELE, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "RMD",  cmd_RMD,  FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "XRMD", cmd_RMD,  FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "RNFR", cmd_RNFR, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "RNTO", cmd_RNTO, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
  { "OPTS", cmd_OPTS, FTPCMD_AUTH_REQ | FTPCMD_NEED_ARGS},
};



/**
 *
 */
static void *
ftp_session(void *aux)
{
  ftp_connection_t *fc = aux;
  char buf[1024];

  net_fmt_host(buf, sizeof(buf), &fc->fc_local_addr);

  ftp_write(fc, 220, "%s FTP server (%s  Version %s) ready.",
            buf, gconf.system_name, htsversion);

  while(1) {
    if(tcp_read_line(fc->fc_tc, buf, sizeof(buf)))
      break;

    if(gconf.enable_ftp_server_debug)
      TRACE(TRACE_DEBUG, "FTP-SERVER", "RECV: %s", buf);

    char *args = strchr(buf, ' ');
    if(args != NULL) {
      *args++ = 0;
      while(*args == ' ')
        args++;
    }

    int r = -1;
    for(int i = 0; i < sizeof(ftpcmds) / sizeof(ftpcmds[0]); i++) {
      if(!strcasecmp(ftpcmds[i].cmd, buf)) {

        if(ftpcmds[i].flags & FTPCMD_AUTH_REQ && !fc->fc_authorized) {
          ftp_write(fc, 530, "Please login first");
          r = 0;
        } else if(ftpcmds[i].flags & FTPCMD_NEED_ARGS && args == NULL) {
          ftp_write(fc, 500, "'%s': Arguments required", buf);
          r = 0;
        } else {
          r = ftpcmds[i].fn(fc, args);
        }
        break;
      }
    }

    if(r == -1) {
      ftp_write(fc, 500, "'%s': Command not understood", buf);
      continue;
    }

    if(r)
      break;
  }

  tcp_close(fc->fc_tc);
  free(fc->fc_username);
  free(fc->fc_pending_RNFR);
  free(fc->fc_wd);
  if(fc->fc_accept_socket != -1)
    close(fc->fc_accept_socket);
  free(fc);
  return NULL;
}


/**
 *
 */
static void
ftp_accept(void *opaque, int fd, const net_addr_t *local_addr,
           const net_addr_t *remote_addr)
{
  ftp_connection_t *fc = calloc(1, sizeof(ftp_connection_t));
  fc->fc_tc = tcp_from_fd(fd);
  fc->fc_local_addr  = *local_addr;
  fc->fc_remote_addr = *remote_addr;
  fc->fc_accept_socket = -1;
  set_wd(fc, "/");
  set_type(fc, 'A');

  usage_event("FTP Server", 1, NULL);

  hts_thread_create_detached("FTP-session", ftp_session, fc,
			     THREAD_PRIO_MODEL);

}


static int ftp_server_port, ftp_server_enable;




static void
enable_disable(void)
{
  if(ftp_server_port && ftp_server_enable) {

    if(ftp_server_fd && asyncio_get_port(ftp_server_fd) == ftp_server_port)
      return;

    if(ftp_server_fd != NULL)
      asyncio_del_fd(ftp_server_fd);

    ftp_server_fd = asyncio_listen("ftp-server", ftp_server_port,
                                   ftp_accept, NULL, 0);
  } else {
    if(ftp_server_fd == NULL)
      return;

    asyncio_del_fd(ftp_server_fd);
    ftp_server_fd = NULL;
  }
}


/**
 *
 */
static void
set_enable(void *opaque, int v)
{
  ftp_server_enable = v;
  enable_disable();
}


/**
 *
 */
static void
set_port(void *opaque, const char *str)
{
  ftp_server_port = atoi(str);
  enable_disable();
}


/**
 *
 */
static void
set_username(void *opauqe, const char *str)
{
  mystrset(&ftp_username, str);
}


/**
 *
 */
static void
set_password(void *opauqe, const char *str)
{
  mystrset(&ftp_password, str);
}






/**
 *
 */
static void
ftp_server_init(void)
{
  htsmsg_t *s = htsmsg_store_load("ftpserver") ?: htsmsg_create_map();

  settings_create_separator(gconf.settings_network, _p("FTP server"));

  setting_create(SETTING_BOOL, gconf.settings_network, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Enable FTP server")),
                 SETTING_VALUE(0),
                 SETTING_CALLBACK(set_enable, NULL),
                 SETTING_HTSMSG("enable", s, "ftpserver"),
                 SETTING_COURIER(asyncio_courier),
                 NULL);

  setting_create(SETTING_STRING, gconf.settings_network,
                 SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Server TCP port")),
                 SETTING_VALUE("2121"),
                 SETTING_CALLBACK(set_port, NULL),
                 SETTING_HTSMSG("port", s, "ftpserver"),
                 SETTING_COURIER(asyncio_courier),
                 NULL);

  setting_create(SETTING_STRING, gconf.settings_network, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Username")),
                 SETTING_VALUE(""),
                 SETTING_CALLBACK(set_username, NULL),
                 SETTING_HTSMSG("username", s, "ftpserver"),
                 SETTING_COURIER(asyncio_courier),
                 NULL);

  setting_create(SETTING_STRING, gconf.settings_network,
                 SETTINGS_INITIAL_UPDATE | SETTINGS_PASSWORD,
                 SETTING_TITLE(_p("Password")),
                 SETTING_VALUE(""),
                 SETTING_CALLBACK(set_password, NULL),
                 SETTING_HTSMSG("password", s, "ftpserver"),
                 SETTING_COURIER(asyncio_courier),
                 NULL);
}

INITME(INIT_GROUP_ASYNCIO, ftp_server_init, NULL, 0);
