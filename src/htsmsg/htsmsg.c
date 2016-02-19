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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "arch/atomic.h"
#include "misc/buf.h"
#include "htsmsg.h"

#include "main.h"
/**
 *
 */
void
htsmsg_field_destroy(htsmsg_t *msg, htsmsg_field_t *f)
{
  TAILQ_REMOVE(&msg->hm_fields, f, hmf_link);

  htsmsg_release(f->hmf_childs);

  switch(f->hmf_type) {
  case HMF_STR:
  case HMF_BIN:
    if(f->hmf_flags & HMF_ALLOCED)
      free(f->hmf_str);
    break;

  default:
    break;
  }
  if(f->hmf_flags & HMF_NAME_ALLOCED)
    free(f->hmf_name);
  rstr_release(f->hmf_namespace);
  free(f);
}

/**
 *
 */
htsmsg_field_t *
htsmsg_field_add(htsmsg_t *msg, const char *name, int type, int flags)
{
  htsmsg_field_t *f = malloc(sizeof(htsmsg_field_t));
  f->hmf_childs = NULL;
  f->hmf_namespace = NULL;
  TAILQ_INSERT_TAIL(&msg->hm_fields, f, hmf_link);

  if(msg->hm_islist) {
    assert(name == NULL);
  } else {
    assert(name != NULL);
  }

  if(flags & HMF_NAME_ALLOCED)
    f->hmf_name = name ? strdup(name) : NULL;
  else
    f->hmf_name = (char *)name;

  f->hmf_type = type;
  f->hmf_flags = flags;
  return f;
}


/*
 *
 */
htsmsg_field_t *
htsmsg_field_find(htsmsg_t *msg, const char *name)
{
  htsmsg_field_t *f;

  if(-((unsigned long)(intptr_t)name) < 4096) {
    unsigned int num = -(intptr_t)name - 1;
    HTSMSG_FOREACH(f, msg) {
      if(!num--)
	return f;
    }
    return NULL;
  }

  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link) {
    if(f->hmf_name != NULL && !strcmp(f->hmf_name, name))
      return f;
  }
  return NULL;
}



/**
 *
 */
int
htsmsg_delete_field(htsmsg_t *msg, const char *name)
{
  htsmsg_field_t *f;

  if((f = htsmsg_field_find(msg, name)) == NULL)
    return HTSMSG_ERR_FIELD_NOT_FOUND;
  htsmsg_field_destroy(msg, f);
  return 0;
}


/*
 *
 */
htsmsg_t *
htsmsg_create_map(void)
{
  htsmsg_t *msg = calloc(1, sizeof(htsmsg_t));
  msg->hm_refcount = 1;
  TAILQ_INIT(&msg->hm_fields);
  return msg;
}

/*
 *
 */
htsmsg_t *
htsmsg_create_list(void)
{
  htsmsg_t *msg = calloc(1, sizeof(htsmsg_t));
  msg->hm_refcount = 1;
  TAILQ_INIT(&msg->hm_fields);
  msg->hm_islist = 1;
  return msg;
}


/**
 *
 */
void
htsmsg_release(htsmsg_t *msg)
{
  htsmsg_field_t *f;

  if (msg == NULL)
    return;

  msg->hm_refcount--;
  if(msg->hm_refcount > 0)
    return;


  while((f = TAILQ_FIRST(&msg->hm_fields)) != NULL)
    htsmsg_field_destroy(msg, f);

  buf_release(msg->hm_backing_store);
  free(msg);
}

/**
 *
 */
htsmsg_t *
htsmsg_retain(htsmsg_t *msg)
{
  msg->hm_refcount++;
  return msg;
}


/**
 *
 */
void
htsmsg_add_u32(htsmsg_t *msg, const char *name, uint32_t u32)
{
  htsmsg_field_t *f = htsmsg_field_add(msg, name, HMF_S64, HMF_NAME_ALLOCED);
  f->hmf_s64 = u32;
}

/*
 *
 */
void
htsmsg_add_s64(htsmsg_t *msg, const char *name, int64_t s64)
{
  htsmsg_field_t *f = htsmsg_field_add(msg, name, HMF_S64, HMF_NAME_ALLOCED);
  f->hmf_s64 = s64;
}

/*
 *
 */
void
htsmsg_add_s32(htsmsg_t *msg, const char *name, int32_t s32)
{
  htsmsg_field_t *f = htsmsg_field_add(msg, name, HMF_S64, HMF_NAME_ALLOCED);
  f->hmf_s64 = s32;
}


/**
 *
 */
void
htsmsg_s32_inc(htsmsg_t *msg, const char *name, int32_t s32)
{
  htsmsg_field_t *f = htsmsg_field_find(msg, name);
  if(f != NULL && f->hmf_type != HMF_S64) {
    htsmsg_field_destroy(msg, f);
    f = NULL;
  }

  if(f == NULL) {
    htsmsg_add_s32(msg, name, s32);
  } else {
    f->hmf_s64 += s32;
  }
}


/*
 *
 */
void
htsmsg_add_dbl(htsmsg_t *msg, const char *name, double dbl)
{
  htsmsg_field_t *f = htsmsg_field_add(msg, name, HMF_DBL, HMF_NAME_ALLOCED);
  f->hmf_dbl = dbl;
}



/*
 *
 */
void
htsmsg_add_str(htsmsg_t *msg, const char *name, const char *str)
{
  htsmsg_field_t *f = htsmsg_field_add(msg, name, HMF_STR, 
				        HMF_ALLOCED | HMF_NAME_ALLOCED);
  f->hmf_str = strdup(str);
}

/*
 *
 */
void
htsmsg_add_bin(htsmsg_t *msg, const char *name, const void *bin, size_t len)
{
  htsmsg_field_t *f = htsmsg_field_add(msg, name, HMF_BIN, 
				       HMF_ALLOCED | HMF_NAME_ALLOCED);
  void *v;
  f->hmf_bin = v = malloc(len);
  f->hmf_binsize = len;
  memcpy(v, bin, len);
}

/*
 *
 */
void
htsmsg_add_binptr(htsmsg_t *msg, const char *name, void *bin, size_t len)
{
  htsmsg_field_t *f = htsmsg_field_add(msg, name, HMF_BIN, HMF_NAME_ALLOCED);
  f->hmf_bin = bin;
  f->hmf_binsize = len;
}


/*
 *
 */
void
htsmsg_add_msg(htsmsg_t *msg, const char *name, htsmsg_t *sub)
{
  htsmsg_field_t *f;

  f = htsmsg_field_add(msg, name, sub->hm_islist ? HMF_LIST : HMF_MAP,
		       HMF_NAME_ALLOCED);

  f->hmf_childs = sub;
}



/*
 *
 */
void
htsmsg_add_msg_extname(htsmsg_t *msg, const char *name, htsmsg_t *sub)
{
  htsmsg_field_t *f;

  f = htsmsg_field_add(msg, name, sub->hm_islist ? HMF_LIST : HMF_MAP, 0);
  f->hmf_childs = sub;
}



/**
 *
 */
int
htsmsg_get_s64(htsmsg_t *msg, const char *name, int64_t *s64p)
{
  htsmsg_field_t *f;

  if((f = htsmsg_field_find(msg, name)) == NULL)
    return HTSMSG_ERR_FIELD_NOT_FOUND;

  switch(f->hmf_type) {
  default:
    return HTSMSG_ERR_CONVERSION_IMPOSSIBLE;
  case HMF_STR:
    *s64p = strtoll(f->hmf_str, NULL, 0);
    break;
  case HMF_S64:
    *s64p = f->hmf_s64;
    break;
  }
  return 0;
}


/*
 *
 */
int
htsmsg_get_u32(htsmsg_t *msg, const char *name, uint32_t *u32p)
{
  int r;
  int64_t s64;

  if((r = htsmsg_get_s64(msg, name, &s64)) != 0)
    return r;

  if(s64 < 0 || s64 > 0xffffffffLL)
    return HTSMSG_ERR_CONVERSION_IMPOSSIBLE;
  
  *u32p = s64;
  return 0;
}

/**
 *
 */
int
htsmsg_get_u32_or_default(htsmsg_t *msg, const char *name, uint32_t def)
{
  uint32_t u32;
    return htsmsg_get_u32(msg, name, &u32) ? def : u32;
}


/**
 *
 */
int32_t
htsmsg_get_s32_or_default(htsmsg_t *msg, const char *name, int32_t def)
{
  int32_t s32;
  return htsmsg_get_s32(msg, name, &s32) ? def : s32;
}



/*
 *
 */
int
htsmsg_get_s32(htsmsg_t *msg, const char *name, int32_t *s32p)
{
  int r;
  int64_t s64;

  if((r = htsmsg_get_s64(msg, name, &s64)) != 0)
    return r;

  if(s64 < -0x80000000LL || s64 > 0x7fffffffLL)
    return HTSMSG_ERR_CONVERSION_IMPOSSIBLE;
  
  *s32p = s64;
  return 0;
}


/*
 *
 */
int
htsmsg_get_dbl(htsmsg_t *msg, const char *name, double *dblp)
{
  htsmsg_field_t *f;

  if((f = htsmsg_field_find(msg, name)) == NULL)
    return HTSMSG_ERR_FIELD_NOT_FOUND;

  if(f->hmf_type == HMF_DBL) {
    *dblp = f->hmf_dbl;
  } else if(f->hmf_type == HMF_S64) {
    *dblp = f->hmf_s64;
  } else {
    return HTSMSG_ERR_CONVERSION_IMPOSSIBLE;
  }
  return 0;
}


/*
 *
 */
int
htsmsg_get_bin(htsmsg_t *msg, const char *name, const void **binp,
	       size_t *lenp)
{
  htsmsg_field_t *f;

  if((f = htsmsg_field_find(msg, name)) == NULL)
    return HTSMSG_ERR_FIELD_NOT_FOUND;

  if(f->hmf_type == HMF_STR) {
    *binp = f->hmf_str;
    *lenp = strlen(f->hmf_str);
    return 0;
  }

  if(f->hmf_type != HMF_BIN)
    return HTSMSG_ERR_CONVERSION_IMPOSSIBLE;

  *binp = f->hmf_bin;
  *lenp = f->hmf_binsize;
  return 0;
}

/**
 *
 */
const char *
htsmsg_field_get_string(htsmsg_field_t *f)
{
  char buf[40];
  
  switch(f->hmf_type) {
  default:
    return NULL;
  case HMF_STR:
    break;
  case HMF_S64:
    snprintf(buf, sizeof(buf), "%"PRId64, f->hmf_s64);
    f->hmf_str = strdup(buf);
    f->hmf_type = HMF_STR;
    break;
  }
  return f->hmf_str;
}

/*
 *
 */
const char *
htsmsg_get_str(htsmsg_t *msg, const char *name)
{
  htsmsg_field_t *f;

  if((f = htsmsg_field_find(msg, name)) == NULL)
    return NULL;
  return htsmsg_field_get_string(f);

}

/*
 *
 */
htsmsg_t *
htsmsg_get_map(htsmsg_t *msg, const char *name)
{
  htsmsg_field_t *f;

  if((f = htsmsg_field_find(msg, name)) == NULL || f->hmf_type != HMF_MAP)
    return NULL;

  return f->hmf_childs;
}

/**
 *
 */
htsmsg_t *
htsmsg_get_map_multi(htsmsg_t *msg, ...)
{
  va_list ap;
  const char *n;
  va_start(ap, msg);

  while(msg != NULL && (n = va_arg(ap, char *)) != NULL)
    msg = htsmsg_get_map(msg, n);
  return msg;
}

/**
 *
 */
const char *
htsmsg_get_str_multi(htsmsg_t *msg, ...)
{
  va_list ap;
  const char *n;
  htsmsg_field_t *f;
  va_start(ap, msg);

  while((n = va_arg(ap, char *)) != NULL) {
    assert(msg != NULL);
    if((f = htsmsg_field_find(msg, n)) == NULL)
      return NULL;
    else if(f->hmf_type == HMF_STR)
      return f->hmf_str;
    else if(f->hmf_childs != NULL)
      msg = f->hmf_childs;
    else
      return NULL;
  }
  return NULL;
}



/*
 *
 */
htsmsg_t *
htsmsg_get_list(htsmsg_t *msg, const char *name)
{
  htsmsg_field_t *f;

  if((f = htsmsg_field_find(msg, name)) == NULL || f->hmf_type != HMF_LIST)
    return NULL;

  return f->hmf_childs;
}

/**
 *
 */
htsmsg_t *
htsmsg_detach_submsg(htsmsg_field_t *f)
{
  assert(f->hmf_childs != NULL);
  return htsmsg_retain(f->hmf_childs);
}


/**
 *
 */
static void
htsmsg_print0(const char *prefix, htsmsg_t *msg, int indent)
{
  htsmsg_field_t *f;

  char tmp[64];
  const char *payload;
  const char *type;
  const char *sep;

  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link) {
    payload = "";
    sep = "} { ";
    switch(f->hmf_type) {
    case HMF_MAP:
      type = "map";
      break;
    case HMF_LIST:
      type = "list";
      sep = "] [ ";
      break;
    case HMF_STR:
      type = "str";
      payload = f->hmf_str;
      break;

    case HMF_BIN:
      type = "bin";
      snprintf(tmp, sizeof(tmp), "[%zd bytes data]", f->hmf_binsize);
      payload = tmp;
      break;

    case HMF_S64:
      type = "int";
      snprintf(tmp, sizeof(tmp), "%" PRId64, f->hmf_s64);
      payload = tmp;
      break;

    case HMF_DBL:
      type = "int";
      snprintf(tmp, sizeof(tmp), "%f", f->hmf_dbl);
      payload = tmp;
      break;
    default:
      abort();
    }

    TRACE(TRACE_DEBUG, prefix, "%*.s\"%s\"%s%s%s%s = (%s)%s%s",
          indent, "",
          f->hmf_name ? f->hmf_name : "",
          f->hmf_namespace ? " [in " : "",
          rstr_get(f->hmf_namespace) ?: "",
          f->hmf_namespace ? "] " : "",
          f->hmf_flags & HMF_XML_ATTRIBUTE ? " [XML-Attribute]" : "",
          type,
          payload,
          f->hmf_childs != NULL ? sep+1 : "");

    if(f->hmf_childs != NULL) {
      htsmsg_print0(prefix, f->hmf_childs, indent + 2);
      TRACE(TRACE_DEBUG, prefix, "%*.s%c", indent, "", sep[0]);
    }
  }
}

/**
 *
 */
void
htsmsg_print(const char *prefix, htsmsg_t *msg)
{
  htsmsg_print0(prefix, msg, 0);
}


/**
 *
 */
static void
htsmsg_copy_i(htsmsg_t *src, htsmsg_t *dst)
{
  htsmsg_field_t *f;
  htsmsg_t *sub;

  TAILQ_FOREACH(f, &src->hm_fields, hmf_link) {

    switch(f->hmf_type) {

    case HMF_MAP:
    case HMF_LIST:
      sub = f->hmf_type == HMF_LIST ?
	htsmsg_create_list() : htsmsg_create_map();
      htsmsg_copy_i(f->hmf_childs, sub);
      htsmsg_add_msg(dst, f->hmf_name, sub);
      break;
      
    case HMF_STR:
      htsmsg_add_str(dst, f->hmf_name, f->hmf_str);
      break;

    case HMF_S64:
      htsmsg_add_s64(dst, f->hmf_name, f->hmf_s64);
      break;

    case HMF_BIN:
      htsmsg_add_bin(dst, f->hmf_name, f->hmf_bin, f->hmf_binsize);
      break;

    case HMF_DBL:
      htsmsg_add_dbl(dst, f->hmf_name, f->hmf_dbl);
      break;
    }
  }
}

htsmsg_t *
htsmsg_copy(htsmsg_t *src)
{
  htsmsg_t *dst = src->hm_islist ? htsmsg_create_list() : htsmsg_create_map();
  htsmsg_copy_i(src, dst);
  return dst;
}

/**
 *
 */
htsmsg_t *
htsmsg_get_map_in_list(htsmsg_t *m, int num)
{
  htsmsg_field_t *f;

  HTSMSG_FOREACH(f, m) {
    if(!--num)
      return htsmsg_get_map_by_field(f);
  }
  return NULL;
}

htsmsg_t *
htsmsg_get_map_by_field_if_name(htsmsg_field_t *f, const char *name)
{
  if(f->hmf_name == NULL || strcmp(f->hmf_name, name))
    return NULL;
  return f->hmf_childs;
}


/**
 *
 */
int
htsmsg_get_children(htsmsg_t *msg)
{
  htsmsg_field_t *f;
  int cnt = 0;
  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link)
    cnt++;
  return cnt;
}
