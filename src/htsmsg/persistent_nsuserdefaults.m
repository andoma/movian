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

#import <Foundation/Foundation.h>

#include "main.h"
#include "persistent.h"
#include "misc/buf.h"

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
  [[NSUserDefaults standardUserDefaults] synchronize];
}


/**
 *
 */
buf_t *
persistent_load(const char *group, const char *key, char *errbuf, size_t errlen)
{
  char k[1024];
  snprintf(k, sizeof(k), "%s/%s", group, key);

  NSString *s = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithUTF8String:k]];

  const char *str = [s UTF8String];
  if(str == NULL)
    return NULL;
  buf_t *b = buf_create_and_copy(strlen(str), str);
  return b;
}


/**
 *
 */
void
persistent_remove(const char *group, const char *key)
{
  char k[1024];
  snprintf(k, sizeof(k), "%s/%s", group, key);

  [[NSUserDefaults standardUserDefaults] removeObjectForKey:[NSString stringWithUTF8String:k]];
}


/**
 *
 */
void
persistent_write(const char *group, const char *key,
                 const void *data, int len)
{
  char k[1024];
  snprintf(k, sizeof(k), "%s/%s", group, key);

  NSString *val = [NSString stringWithUTF8String:data];
  [[NSUserDefaults standardUserDefaults] setObject:val forKey:[NSString stringWithUTF8String:k]];
}

