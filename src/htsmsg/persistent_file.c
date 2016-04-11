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
#include "fileaccess/fileaccess.h"
#include "persistent.h"

#include "arch/arch.h"

#ifdef PS3
#define RENAME_CANT_OVERWRITE 1
#else
#define RENAME_CANT_OVERWRITE 0
#endif


#define SETTINGS_TRACE(fmt, ...) do {            \
  if(gconf.enable_settings_debug) \
    TRACE(TRACE_DEBUG, "Settings", fmt, ##__VA_ARGS__); \
} while(0)


/**
 *
 */
void
persistent_store_sync(void)
{
#ifdef STOS
  if(gconf.persistent_path)
    arch_sync_path(gconf.persistent_path);
#endif
}


/**
 *
 */
static int
buildpath(char *dst, size_t dstsize, const char *subdir, const char *key,
          const char *postfix)
{
  if(gconf.persistent_path == NULL)
     return -1;

  snprintf(dst, dstsize, "%s/%s/", gconf.persistent_path, subdir);

  char *n = dst + strlen(dst);

  snprintf(n, dstsize - strlen(dst), "%s%s", key, postfix);

  while(*n) {
    if(*n == ':' || *n == '?' || *n == '*' || *n > 127 || *n < 32)
      *n = '_';
    n++;
  }
  return 0;
}



/**
 *
 */
buf_t *
persistent_load(const char *group, const char *key, char *errbuf, size_t errlen)
{
  char fullpath[1024];

  if(buildpath(fullpath, sizeof(fullpath), group, key, "") < 0)
    return NULL;

  return fa_load(fullpath,
                 FA_LOAD_ERRBUF(errbuf, errlen),
                 NULL);
}


/**
 *
 */
void
persistent_remove(const char *group, const char *key)
{
  char fullpath[1024];

  if(buildpath(fullpath, sizeof(fullpath), group, key, ""))
    return;

  fa_unlink(fullpath, NULL, 0);
}


/**
 *
 */
void
persistent_write(const char *group, const char *key,
                 const void *data, int len)
{
  char fullpath[1024];
  char fullpath2[1024];
  char errbuf[512];
  int ok;

  if(buildpath(fullpath, sizeof(fullpath), group, key,
               RENAME_CANT_OVERWRITE ? "" : ".tmp"))
    return;

  char *x = strrchr(fullpath, '/');
  if(x != NULL) {
    *x = 0;
    if(fa_makedirs(fullpath, errbuf, sizeof(errbuf))) {
      TRACE(TRACE_ERROR, "Persistent", "Unable to create dir %s -- %s",
            fullpath, errbuf);
      return;
    }
    *x = '/';
  }

  fa_handle_t *fh =
    fa_open_ex(fullpath, errbuf, sizeof(errbuf), FA_WRITE, NULL);
  if(fh == NULL) {
    TRACE(TRACE_ERROR, "Persistent", "Unable to create \"%s\" - %s",
	    fullpath, errbuf);
    return;
  }

  ok = 1;

  if(fa_write(fh, data, len) != len) {
    TRACE(TRACE_ERROR, "Persistent", "Failed to write file %s",
          fullpath);
    ok = 0;
  }

  fa_close(fh);
  if(!ok) {
    fa_unlink(fullpath, NULL, 0);
    return;
  }
  const char *opath = fullpath;
  if(!RENAME_CANT_OVERWRITE) {

    if(buildpath(fullpath2, sizeof(fullpath2), group, key, ""))
      return;

    if(fa_rename(fullpath, fullpath2, errbuf, sizeof(errbuf))) {
      TRACE(TRACE_ERROR, "Settings", "Failed to rename \"%s\" -> \"%s\" - %s",
            fullpath, fullpath2, errbuf);
    }
    opath = fullpath2;
  }
  SETTINGS_TRACE("Wrote %d bytes to \"%s\"", len, opath);
}

