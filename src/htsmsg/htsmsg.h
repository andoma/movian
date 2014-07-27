/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#ifndef HTSMSG_H_
#define HTSMSG_H_

#include <stdlib.h>
#include <inttypes.h>
#include "misc/queue.h"
#include "misc/buf.h"

#define HTSMSG_ERR_FIELD_NOT_FOUND       -1
#define HTSMSG_ERR_CONVERSION_IMPOSSIBLE -2

TAILQ_HEAD(htsmsg_field_queue, htsmsg_field);

typedef struct htsmsg {
  struct htsmsg_field_queue hm_fields;
  buf_t *hm_backing_store;
  uint8_t hm_islist;
  int hm_refcount;
} htsmsg_t;


#define HMF_MAP  1
#define HMF_S64  2
#define HMF_STR  3
#define HMF_BIN  4
#define HMF_LIST 5
#define HMF_DBL  6

typedef struct htsmsg_field {
  TAILQ_ENTRY(htsmsg_field) hmf_link;
  char *hmf_name;
  uint8_t hmf_type;
  uint8_t hmf_flags;

#define HMF_ALLOCED       0x1
#define HMF_NAME_ALLOCED  0x2
#define HMF_XML_ATTRIBUTE 0x4 // XML attribute

  union {
    int64_t  s64;
    char *str;
    struct {
      void *data;
      size_t len;
    } bin;
    double dbl;
  } u;

  htsmsg_t *hmf_childs;
  struct rstr *hmf_namespace;
} htsmsg_field_t;

#define hmf_s64     u.s64
#define hmf_str     u.str
#define hmf_bin     u.bin.data
#define hmf_binsize u.bin.len
#define hmf_dbl     u.dbl

static __inline htsmsg_t *
htsmsg_get_map_by_field(htsmsg_field_t *f)
{
  return f->hmf_childs;
}

#define htsmsg_get_list_by_field(f) \
 ((f)->hmf_type == HMF_LIST ? &(f)->hmf_msg : NULL)

#define HTSMSG_FOREACH(f, msg) TAILQ_FOREACH(f, &(msg)->hm_fields, hmf_link)

#define HTSMSG_INDEX(i) ((const char *)(intptr_t)(-(i+1)))

/**
 * Create a new map
 */
htsmsg_t *htsmsg_create_map(void);

/**
 * Create a new list
 */
htsmsg_t *htsmsg_create_list(void);

/**
 * Remove a given field from a msg
 */
void htsmsg_field_destroy(htsmsg_t *msg, htsmsg_field_t *f);

/**
 * Add an integer field where source is unsigned 32 bit.
 */
void htsmsg_add_u32(htsmsg_t *msg, const char *name, uint32_t u32);

/**
 * Add an integer field where source is signed 32 bit.
 */
void htsmsg_add_s32(htsmsg_t *msg, const char *name,  int32_t s32);

/**
 * Increase an integer field where source is signed 32 bit.
 */
void htsmsg_s32_inc(htsmsg_t *msg, const char *name,  int32_t s32);

/**
 * Add an integer field where source is signed 64 bit.
 */
void htsmsg_add_s64(htsmsg_t *msg, const char *name,  int64_t s64);

/**
 * Add a string field.
 */
void htsmsg_add_str(htsmsg_t *msg, const char *name, const char *str);

/**
 * Add an field where source is a list or map message.
 */
void htsmsg_add_msg(htsmsg_t *msg, const char *name, htsmsg_t *sub);

/**
 * Add an field where source is a double
 */
void htsmsg_add_dbl(htsmsg_t *msg, const char *name, double dbl);

/**
 * Add an field where source is a list or map message.
 *
 * This function will not strdup() \p name but relies on the caller
 * to keep the string allocated for as long as the message is valid.
 */
void htsmsg_add_msg_extname(htsmsg_t *msg, const char *name, htsmsg_t *sub);

/**
 * Add an binary field. The data is copied to a malloced storage
 */
void htsmsg_add_bin(htsmsg_t *msg, const char *name, const void *bin,
		    size_t len);

/**
 * Add an binary field. The data is not copied, instead the caller
 * is responsible for keeping the data valid for as long as the message
 * is around.
 */
void htsmsg_add_binptr(htsmsg_t *msg, const char *name, void *bin,
		       size_t len);

/**
 * Get an integer as an unsigned 32 bit integer.
 *
 * @return HTSMSG_ERR_FIELD_NOT_FOUND - Field does not exist
 *         HTSMSG_ERR_CONVERSION_IMPOSSIBLE - Field is not an integer or
 *              out of range for the requested storage.
 */
int htsmsg_get_u32(htsmsg_t *msg, const char *name, uint32_t *u32p);

/**
 * Get an integer as an signed 32 bit integer.
 *
 * @return HTSMSG_ERR_FIELD_NOT_FOUND - Field does not exist
 *         HTSMSG_ERR_CONVERSION_IMPOSSIBLE - Field is not an integer or
 *              out of range for the requested storage.
 */
int htsmsg_get_s32(htsmsg_t *msg, const char *name,  int32_t *s32p);

/**
 * Get an integer as an signed 64 bit integer.
 *
 * @return HTSMSG_ERR_FIELD_NOT_FOUND - Field does not exist
 *         HTSMSG_ERR_CONVERSION_IMPOSSIBLE - Field is not an integer or
 *              out of range for the requested storage.
 */
int htsmsg_get_s64(htsmsg_t *msg, const char *name,  int64_t *s64p);

/**
 * Get pointer to a binary field. No copying of data is performed.
 *
 * @param binp Pointer to a void * that will be filled in with a pointer
 *             to the data
 * @param lenp Pointer to a size_t that will be filled with the size of
 *             the data
 *
 * @return HTSMSG_ERR_FIELD_NOT_FOUND - Field does not exist
 *         HTSMSG_ERR_CONVERSION_IMPOSSIBLE - Field is not a binary blob.
 */
int htsmsg_get_bin(htsmsg_t *msg, const char *name, const void **binp,
		   size_t *lenp);

/**
 * Get a field of type 'list'. No copying is done.
 *
 * @return NULL if the field can not be found or not of list type.
 *         Otherwise a htsmsg is returned.
 */
htsmsg_t *htsmsg_get_list(htsmsg_t *msg, const char *name);

/**
 * Get a field of type 'string'. No copying is done.
 *
 * @return NULL if the field can not be found or not of string type.
 *         Otherwise a pointer to the data is returned.
 */
const char *htsmsg_get_str(htsmsg_t *msg, const char *name);

/**
 * Get a field of type 'map'. No copying is done.
 *
 * @return NULL if the field can not be found or not of map type.
 *         Otherwise a htsmsg is returned.
 */
htsmsg_t *htsmsg_get_map(htsmsg_t *msg, const char *name);

/**
 * Traverse a hierarchy of htsmsg's to find a specific child.
 */
htsmsg_t *htsmsg_get_map_multi(htsmsg_t *msg, ...)
  attribute_null_sentinel;

/**
 * Traverse a hierarchy of htsmsg's to find a specific child.
 */
const char *htsmsg_get_str_multi(htsmsg_t *msg, ...)
  attribute_null_sentinel;

/**
 * Get a field of type 'double'.
 *
 * @return HTSMSG_ERR_FIELD_NOT_FOUND - Field does not exist
 *         HTSMSG_ERR_CONVERSION_IMPOSSIBLE - Field is not an integer or
 *              out of range for the requested storage.
 */
int htsmsg_get_dbl(htsmsg_t *msg, const char *name, double *dblp);

/**
 * Given the field \p f, return a string if it is of type string, otherwise
 * return NULL
 */
const char *htsmsg_field_get_string(htsmsg_field_t *f);

/**
 * Return the field \p name as an u32.
 *
 * @return An unsigned 32 bit integer or NULL if the field can not be found
 *         or if conversion is not possible.
 */
int htsmsg_get_u32_or_default(htsmsg_t *msg, const char *name, uint32_t def);

/**
 * Return the field \p name as an s32.
 *
 * @return A signed 32 bit integer or NULL if the field can not be found
 *         or if conversion is not possible.
 */
int32_t htsmsg_get_s32_or_default(htsmsg_t *msg, const char *name, 
				  int32_t def);

/**
 * Remove the given field called \p name from the message \p msg.
 */
int htsmsg_delete_field(htsmsg_t *msg, const char *name);

/**
 * Detach will remove the given field (and only if it is a list or map)
 * from the message and make it a 'standalone message'. This means
 * the the contents of the sub message will stay valid if the parent is
 * destroyed. The caller is responsible for freeing this new message.
 */
htsmsg_t *htsmsg_detach_submsg(htsmsg_field_t *f);

/**
 * Print a message to stdout. 
 */
void htsmsg_print(htsmsg_t *msg);

/**
 * Create a new field. Primarily intended for htsmsg internal functions.
 */
htsmsg_field_t *htsmsg_field_add(htsmsg_t *msg, const char *name,
				 int type, int flags);

/**
 * Get a field, return NULL if it does not exist
 */
htsmsg_field_t *htsmsg_field_find(htsmsg_t *msg, const char *name);


/**
 * Clone a message.
 */
htsmsg_t *htsmsg_copy(htsmsg_t *src);

#define HTSMSG_FOREACH(f, msg) TAILQ_FOREACH(f, &(msg)->hm_fields, hmf_link)


/**
 * Refcounting
 */

void htsmsg_release(htsmsg_t *m);

htsmsg_t *htsmsg_retain(htsmsg_t *m) attribute_unused_result;

/**
 * Misc
 */
htsmsg_t *htsmsg_get_map_in_list(htsmsg_t *m, int num);

htsmsg_t *htsmsg_get_map_by_field_if_name(htsmsg_field_t *f, const char *name);

int htsmsg_get_children(htsmsg_t *msg);


static __inline void
htsmsg_set_backing_store(htsmsg_t *m, buf_t *b)
{
  if(m->hm_backing_store == NULL) {
    m->hm_backing_store = buf_retain(b);
  } else {
    assert(m->hm_backing_store == b);
  }
}

#endif /* HTSMSG_H_ */
