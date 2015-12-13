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

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include <stdio.h>
#include <sys/inotify.h>
#include <poll.h>

#include "main.h"
#include "misc/queue.h"
#include "misc/str.h"
#include "service.h"

LIST_HEAD(fsinfo_list, fsinfo);


typedef struct fsinfo {
  LIST_ENTRY(fsinfo) fi_link;
  char *fi_uuid;
  char *fi_devname;
  char *fi_type;
  char *fi_label;
  char *fi_mountpoint;

  int fi_status;

  service_t *fi_service;

} fsinfo_t;

#define FI_STATUS_UNMOUNTED  0
#define FI_STATUS_MOUNTED    1
#define FI_STATUS_MOUNT_FAIL 2

static struct fsinfo_list fsinfos;


static char *
cleanup(const char *s)
{
  if(*s != '\'')
    return strdup(s);
  s++;
  char *r = strdup(s);

  for(char *q = r; *q; q++) {
    if(*q == '\'') {
      *q = 0;
      break;
    }
  }
  return r;
}


/**
 *
 */
static int
try_mount_fs(fsinfo_t *fi)
{
  char mpoint[PATH_MAX];

  snprintf(mpoint, sizeof(mpoint), "/stos/media");
  mkdir(mpoint, 0755);

  snprintf(mpoint, sizeof(mpoint), "/stos/media/%s", fi->fi_label);
  int dupcnt = 1;

  while(1) {
    if(mkdir(mpoint, 0755) == 0)
      break;
    if(errno == EEXIST) {
      dupcnt++;
      snprintf(mpoint, sizeof(mpoint), "/stos/media/%s%d",
               fi->fi_label, dupcnt);
      continue;
    }
      TRACE(TRACE_ERROR, "Automount", "Failed to create mountpoint %s -- %s",
            mpoint, strerror(errno));
      fi->fi_status = FI_STATUS_MOUNT_FAIL;
      return -1;
  }

  int mountflags = MS_NOSUID | MS_NODEV | MS_NOATIME | MS_SYNCHRONOUS;

  if(mount(fi->fi_devname, mpoint, fi->fi_type, mountflags, "")) {
    if(errno == EBUSY) {
      // Assume it's already mounted OK
      TRACE(TRACE_DEBUG, "Automount", "%s already mounted",
            fi->fi_devname, mpoint);
    } else {

      TRACE(TRACE_ERROR, "Automount", "Failed to mount %s at %s -- %s",
            fi->fi_devname, mpoint, strerror(errno));
      fi->fi_status = FI_STATUS_MOUNT_FAIL;
      return -1;
    }

  } else {
    TRACE(TRACE_DEBUG, "Automount", "Mounted %s at %s",
          fi->fi_devname, mpoint);
  }

  fi->fi_mountpoint = strdup(mpoint);
  fi->fi_status = FI_STATUS_MOUNTED;
  return 0;
}



/**
 *
 */
static void
try_mount(void)
{
  fsinfo_t *fi;

  LIST_FOREACH(fi, &fsinfos, fi_link) {
    if(fi->fi_status != FI_STATUS_UNMOUNTED)
      continue;

    if(try_mount_fs(fi))
      continue;

    char url[1024];
    snprintf(url, sizeof(url), "file://%s", fi->fi_mountpoint);

    fi->fi_service = service_create_managed(fi->fi_uuid,
                                            fi->fi_label,
                                            url,
                                            "usb",
                                            NULL,
                                            0, 1,
					    SVC_ORIGIN_MEDIA, NULL);
  }
}


/**
 *
 */
static fsinfo_t *
parse_infofile(char *buf)
{
  char *uuid = NULL;
  char *devname = NULL;
  char *type = NULL;
  char *label = NULL;

  LINEPARSE(s, buf) {
    const char *v;
    if((v = mystrbegins(s, "DEVNAME=")) != NULL)
      devname = cleanup(v);
    else if((v = mystrbegins(s, "ID_FS_UUID=")) != NULL)
      uuid = cleanup(v);
    else if((v = mystrbegins(s, "ID_FS_TYPE=")) != NULL)
      type = cleanup(v);
    else if((v = mystrbegins(s, "ID_FS_LABEL=")) != NULL)
      label = cleanup(v);
  }

  if(uuid != NULL && devname != NULL) {

    fsinfo_t *fi = NULL;
    LIST_FOREACH(fi, &fsinfos, fi_link) {
      if(!strcmp(fi->fi_uuid, uuid)) {
        break;
      }
    }

    if(fi == NULL) {

      if(label == NULL)
        label = strdup("USB Drive");

      fi = calloc(1, sizeof(fsinfo_t));
      fi->fi_uuid    = uuid;
      fi->fi_devname = devname;
      fi->fi_type    = type;
      fi->fi_label   = label;

      LIST_INSERT_HEAD(&fsinfos, fi, fi_link);

      TRACE(TRACE_DEBUG, "Automount", "Added filesystem %s at %s (%s) [%s]",
            label ? label : "<noname>",
            devname,
            type ? type : "<unknown type>",
            uuid);

      return fi;
    }
  }

  free(devname);
  free(uuid);
  free(type);
  free(label);
  return NULL;

}


/**
 *
 */
static void
add_fs(const char *path, const char *infofile)
{
  char fullname[PATH_MAX];
  struct stat st;

  snprintf(fullname, sizeof(fullname), "%s/%s", path, infofile);
  int fd = open(fullname, O_RDONLY);
  if(fd == -1) {
    TRACE(TRACE_ERROR, "Automount", "Unable to open %s -- %s",
          infofile, strerror(errno));
    return;
  }

  if(fstat(fd, &st) < 0) {
    TRACE(TRACE_ERROR, "Automount", "Unable to stat %s -- %s",
          infofile, strerror(errno));
    close(fd);
    return;
  }

  char *buf = malloc(st.st_size + 1);
  if(buf == NULL) {
    close(fd);
    return;
  }
  buf[st.st_size] = 0;
  if(read(fd, buf, st.st_size) != st.st_size) {
    TRACE(TRACE_ERROR, "Automount", "Unable to read %s -- %s",
          infofile, strerror(errno));
    close(fd);
    free(buf);
    return;
  }
  close(fd);
  fsinfo_t *fi = parse_infofile(buf);

  free(buf);

  if(fi == NULL)
    return;
}

/**
 *
 */
static void
remove_fs(const char *uuid)
{
  fsinfo_t *fi;

  LIST_FOREACH(fi, &fsinfos, fi_link) {
    if(!strcmp(fi->fi_uuid, uuid))
      break;
  }
  if(fi == NULL)
    return;

  if(fi->fi_status == FI_STATUS_MOUNTED) {
    service_destroy(fi->fi_service);
    int r = umount2(fi->fi_mountpoint, MNT_DETACH);
    TRACE(TRACE_DEBUG, "Automount", "Unmounted %s -- %s",
          fi->fi_mountpoint, r < 0 ? strerror(errno) : "OK");
    rmdir(fi->fi_mountpoint);
  }

  TRACE(TRACE_DEBUG, "Automount", "Removed filesystem %s at %s (%s) [%s]",
        fi->fi_label ?: "<noname>",
        fi->fi_devname,
        fi->fi_type ?: "<unknown type>",
        fi->fi_uuid);

  LIST_REMOVE(fi, fi_link);
  free(fi->fi_uuid);
  free(fi->fi_devname);
  free(fi->fi_type);
  free(fi->fi_label);
  free(fi->fi_mountpoint);
  free(fi);
}


/**
 *
 */
static void
scan_current(const char *path)
{
  struct dirent **namelist;
  int n;

  n = scandir(path, &namelist, NULL, NULL);
  if(n < 0) {
    TRACE(TRACE_ERROR, "Automount", "Unable to scan %s -- %s",
          path, strerror(errno));
    return;
  }

  while(n--) {
    if(namelist[n]->d_name[0] != '.')
      add_fs(path, namelist[n]->d_name);
    free(namelist[n]);
  }
  free(namelist);
}


/**
 *
 */
static void
unmount_all(const char *path)
{
  char newpath[PATH_MAX];
  struct dirent **namelist;
  int n;

  n = scandir(path, &namelist, NULL, NULL);
  if(n < 0) {
    TRACE(TRACE_ERROR, "Automount", "Unable to scan %s -- %s",
          path, strerror(errno));
    return;
  }

  while(n--) {
    if(namelist[n]->d_name[0] == '.')
      continue;
    snprintf(newpath, sizeof(newpath), "%s/%s", path, namelist[n]->d_name);
    TRACE(TRACE_DEBUG, "Automount", "Unmounting %s", newpath);
    
    if(umount2(newpath, MNT_DETACH) < 0)
      TRACE(TRACE_ERROR, "Automount", "Unable to unmount %s -- %s",
            newpath, strerror(errno));
    if(rmdir(newpath) < 0)
      TRACE(TRACE_ERROR, "Automount", "Unable to remove %s -- %s",
            newpath, strerror(errno));
    free(namelist[n]);
  }
  free(namelist);
}

static const char *fsinfodir = "/stos/fsinfo";
static hts_thread_t automount_tid;
static int automount_pipe[2];

/**
 *
 */
static void *
automount_thread(void *aux)
{
  static int fd;
  struct inotify_event *e;
  char buf[1024];
  struct pollfd fds[2];

  mkdir(fsinfodir, 0755);

  unmount_all("/stos/media");

  if((fd = inotify_init()) == -1)
    return NULL;

  if(inotify_add_watch(fd, fsinfodir, IN_ONLYDIR | IN_CLOSE_WRITE |
		       IN_DELETE) == -1) {
    TRACE(TRACE_DEBUG, "Automount", "Unable to watch %s -- %s",
	  fsinfodir, strerror(errno));
    close(fd);
    return NULL;
  }


  fds[0].fd = fd;
  fds[0].events = POLLIN;

  fds[1].fd = automount_pipe[0];
  fds[1].events = POLLERR;

  scan_current(fsinfodir);

  while(1) {

    try_mount();
    int n = poll(fds, 2, -1);
    if(n < 0)
      break;
    if(fds[1].revents & (POLLERR | POLLHUP))
      break;
    if(fds[0].revents & (POLLERR | POLLHUP))
      break;

    n = read(fd, buf, sizeof(buf));
    if(n < 0)
      break;
    int off = 0;
    while(n > sizeof(struct inotify_event)) {

      e = (struct inotify_event *)&buf[off];

      if(e->len == 0)
        break;

      if(e->mask & IN_DELETE)
        remove_fs(e->name);

      if(e->mask & IN_CLOSE_WRITE)
        add_fs(fsinfodir, e->name);

      off += sizeof(struct inotify_event) + e->len;
      n   -= sizeof(struct inotify_event) + e->len;
    }
  }
  close(automount_pipe[0]);
  return NULL;
}


static void
stos_automount_start(void)
{
  if(pipe(automount_pipe) < 0) {
    automount_pipe[1] = -1;
    return;
  }

  hts_thread_create_joinable("automounter", &automount_tid,
                             automount_thread, NULL,
                             THREAD_PRIO_BGTASK);
}


static void
stos_automount_stop(void)
{
  if(automount_pipe[1] == -1)
    return;

  close(automount_pipe[1]);
  hts_thread_join(&automount_tid);
  unmount_all("/stos/media");
}

INITME(INIT_GROUP_IPC, stos_automount_start, stos_automount_stop, 0);
