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
#include <malloc.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/mman.h>

#include "ppapi/c/pp_errors.h"

#include "main.h"
#include "arch/halloc.h"
#include "misc/callout.h"
#include "misc/md5.h"
#include "misc/str.h"
#include "misc/prng.h"
#include "prop/prop.h"

#include "arch/posix/posix.h"
#include "arch/arch.h"

#include "nacl.h"

/**
 *
 */
const char *
arch_get_system_type(void)
{
  return "NaCl";
}



/**
 *
 */
void
arch_sync_path(const char *path)
{
}


/**
 *
 */
size_t
arch_malloc_size(void *ptr)
{
  return malloc_usable_size(ptr);
}

/**
 *
 */
int64_t
arch_get_ts(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}


/**
 *
 */
int64_t
arch_get_avtime(void)
{
  return arch_get_ts();
}


/**
 *
 */
void *
halloc(size_t size)
{
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(p == MAP_FAILED)
    return NULL;
  return p;
}

/**
 *
 */
void
hfree(void *ptr, size_t size)
{
  munmap(ptr, size);
}


void *
mymalloc(size_t size)
{
  return malloc(size);
}

void *
myrealloc(void *ptr, size_t size)
{
  return realloc(ptr, size);
}

void *
mycalloc(size_t count, size_t size)
{
  return calloc(count, size);
}

void *
mymemalign(size_t align, size_t size)
{
  void *p;
  return posix_memalign(&p, align, size) ? NULL : p;
}


void
arch_localtime(const time_t *now, struct tm *tm)
{
  localtime_r(now, tm);
}


const char *
pepper_errmsg(int err)
{
  switch(err) {
  case PP_OK: return "OK";
  case PP_OK_COMPLETIONPENDING: return "Completion pending";
  case PP_ERROR_FAILED: return "Unspecified error";
  case PP_ERROR_ABORTED: return "Aborted";
  case PP_ERROR_BADARGUMENT: return "Bad argument";
  case PP_ERROR_BADRESOURCE: return "Bad resource";
  case PP_ERROR_NOINTERFACE: return "No interface";
  case PP_ERROR_NOACCESS: return "No access";
  case PP_ERROR_NOMEMORY: return "No memory";
  case PP_ERROR_NOSPACE:  return "No space";
  case PP_ERROR_NOQUOTA: return "Out of quota";
  case PP_ERROR_INPROGRESS: return "In progress";
  case PP_ERROR_NOTSUPPORTED: return "Not supported";
  case PP_ERROR_BLOCKS_MAIN_THREAD: return "Blocks main thread";
  case PP_ERROR_MALFORMED_INPUT: return "Malformed input";
  case PP_ERROR_RESOURCE_FAILED: return "Resource failed";
  case PP_ERROR_FILENOTFOUND: return "File not found";
  case PP_ERROR_FILEEXISTS: return "File exists";
  case PP_ERROR_FILETOOBIG: return "File too big";
  case PP_ERROR_FILECHANGED: return "File changed";
  case PP_ERROR_NOTAFILE: return "Not a file";
  case PP_ERROR_TIMEDOUT: return "Timeout";
  case PP_ERROR_USERCANCEL: return "User cancelled";
  case PP_ERROR_NO_USER_GESTURE: return "No pending user gesture";
  case PP_ERROR_CONTEXT_LOST: return "Graphics context lost";
  case PP_ERROR_NO_MESSAGE_LOOP: return "No message loop";
  case PP_ERROR_WRONG_THREAD: return "Wrong thread";
  case PP_ERROR_CONNECTION_CLOSED: return "Connection closed";
  case PP_ERROR_CONNECTION_RESET: return "Connection reset";
  case PP_ERROR_CONNECTION_REFUSED: return "Connection refused";
  case PP_ERROR_CONNECTION_ABORTED: return "Connection aborted";
  case PP_ERROR_CONNECTION_FAILED: return "Connection failed";
  case PP_ERROR_CONNECTION_TIMEDOUT: return "Connection timed out";
  case PP_ERROR_ADDRESS_INVALID: return "Invalid address";
  case PP_ERROR_ADDRESS_UNREACHABLE: return "Address unreachable";
  case PP_ERROR_ADDRESS_IN_USE: return "Address in use";
  case PP_ERROR_MESSAGE_TOO_BIG: return "Messagse too big";
  case PP_ERROR_NAME_NOT_RESOLVED: return "Name not resolved";
  default:
    return "Unmapped error";
  }
}
