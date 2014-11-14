/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

#include <unistd.h>
#include <assert.h>

#include "showtime.h"
#include "ecmascript.h"

#include <libavutil/sha.h>
#include <libavutil/mem.h>
#include <libavutil/md5.h>
typedef struct es_hash {
  enum {
    ES_HASH_SHA,
    ES_HASH_MD5,
  } mode;

  int digest_len;

  union {
    struct AVSHA *sha;
    struct AVMD5 *md5;
  };

} es_hash_t;


void
es_hash_release(struct es_hash *eh)
{
  switch(eh->mode) {
  case ES_HASH_SHA:
    av_freep(&eh->sha);
    break;
  case ES_HASH_MD5:
    av_freep(&eh->md5);
    break;
  }
  free(eh);
}



/**
 *
 */
static int
es_hashCreate(duk_context *ctx)
{
  const char *algo = duk_require_string(ctx, 0);
  es_hash_t *eh = malloc(sizeof(es_hash_t));

  if(!strcmp(algo, "sha1")) {
    eh->sha = av_sha_alloc();
    av_sha_init(eh->sha, 160);
    eh->mode = ES_HASH_SHA;
    eh->digest_len = 20;
  } else if(!strcmp(algo, "sha256")) {
    eh->sha = av_sha_alloc();
    av_sha_init(eh->sha, 256);
    eh->mode = ES_HASH_SHA;
    eh->digest_len = 32;
  } else if(!strcmp(algo, "sha512")) {
    eh->sha = av_sha_alloc();
    av_sha_init(eh->sha, 512);
    eh->mode = ES_HASH_SHA;
    eh->digest_len = 64;
  } else if(!strcmp(algo, "md5")) {
    eh->md5 = av_md5_alloc();
    av_md5_init(eh->md5);
    eh->mode = ES_HASH_MD5;
    eh->digest_len = 16;
  } else {
    free(eh);
    duk_error(ctx, DUK_ERR_ERROR, "Unknown hash algo %s", algo);
  }
  es_push_native_obj(ctx, ES_NATIVE_HASH, eh);
  return 1;
}


/**
 *
 */
static int
es_hashUpdate(duk_context *ctx)
{
  es_hash_t *eh = es_get_native_obj(ctx, 0, ES_NATIVE_HASH);
  duk_size_t bufsize;
  const void *buf;
  if(duk_is_buffer(ctx, 1)) {
    buf = duk_get_buffer(ctx, 1, &bufsize);
  } else {
    buf = duk_to_string(ctx, 1);
    bufsize = strlen(buf);
  }

  switch(eh->mode) {
  case ES_HASH_SHA:
    av_sha_update(eh->sha, buf, bufsize);
    break;
  case ES_HASH_MD5:
    av_md5_update(eh->md5, buf, bufsize);
    break;
  }
  return 0;
}


/**
 *
 */
static int
es_hashFinalize(duk_context *ctx)
{
  es_hash_t *eh = es_get_native_obj(ctx, 0, ES_NATIVE_HASH);

  void *digest = duk_push_buffer(ctx, eh->digest_len, 0);

  switch(eh->mode) {
  case ES_HASH_SHA:
    av_sha_final(eh->sha, digest);
    break;
  case ES_HASH_MD5:
    av_md5_final(eh->md5, digest);
    break;
  }
  return 1;
}




/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_crypto[] = {
  { "hashCreate",            es_hashCreate,       1 },
  { "hashUpdate",            es_hashUpdate,       2 },
  { "hashFinalize",          es_hashFinalize,     1 },
  { NULL, NULL, 0}
};

