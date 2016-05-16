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
#include <unistd.h>
#include <assert.h>

#include "main.h"
#include "ecmascript.h"
#include "task.h"

#include "fileaccess/fa_proto.h"

static hts_mutex_t es_fa_mutex;
static hts_cond_t es_fa_cond;

INITIALIZER(es_fa_init)
{
  hts_mutex_init(&es_fa_mutex);
  hts_cond_init(&es_fa_cond, &es_fa_mutex);
}



typedef struct es_fap {
  es_resource_t super;
  char *name;
  fa_protocol_t fap;

  int internal_seek_mode;

} es_fap_t;




/**
 *
 */
static void
es_fap_destroy(es_resource_t *eres)
{
  es_fap_t *ef = (es_fap_t *)eres;
  fileaccess_unregister_dynamic(&ef->fap);
  free(ef->name);
  es_root_unregister(eres->er_ctx->ec_duk, eres);
  es_resource_unlink(&ef->super);
}


/**
 *
 */
static void
es_fap_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_fap_t *ef = (es_fap_t *)eres;
  snprintf(dst, dstsize, "%s", ef->name);
}


/**
 *
 */
static const es_resource_class_t es_resource_fap = {
  .erc_name = "faprovider",
  .erc_size = sizeof(es_fap_t),
  .erc_destroy = es_fap_destroy,
  .erc_info = es_fap_info,
};



static void
es_fap_fini(fa_protocol_t *fap)
{
  es_fap_t *ef = fap->fap_opaque;
  es_resource_release(&ef->super);
}




typedef struct es_fa_handle {
  fa_handle_t fh;

  atomic_t fah_refcount;

  char *fah_url;
  char *fah_errbuf;
  size_t fah_errsize;
  int fah_flags;

  int fah_status;

#define ES_FA_WORKING   0
#define ES_FA_CANCELLED 1
#define ES_FA_OK        2
#define ES_FA_ERROR     3

  int fah_return_value;

  es_fap_t *fah_ef;


  void *fah_readbuf;
  size_t fah_readlen;

  int64_t fah_size;
  int64_t fah_fpos;


  int fah_type;
  int fah_mtime;

} es_fa_handle_t;


static void
fah_release(es_fa_handle_t *fah)
{
  if(atomic_dec(&fah->fah_refcount))
    return;

  es_resource_release(&fah->fah_ef->super);
  free(fah->fah_url);
  free(fah);
}

static es_fa_handle_t *
fah_retain(es_fa_handle_t *fah)
{
  atomic_inc(&fah->fah_refcount);
  return fah;
}

ES_NATIVE_CLASS(fah, &fah_release);


static void
fah_fail(es_fa_handle_t *fah, duk_context *ctx)
{
  const char *msg = duk_get_string(ctx, -1);
  hts_mutex_lock(&es_fa_mutex);
  if(fah->fah_status == ES_FA_WORKING) {
    snprintf(fah->fah_errbuf, fah->fah_errsize, "%s",
             msg ? msg : "No message in JS exception");
    fah->fah_status = ES_FA_ERROR;
    hts_cond_broadcast(&es_fa_cond);
  }
  hts_mutex_unlock(&es_fa_mutex);
}

static void
fah_exception(es_fa_handle_t *fah, duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "message");
  fah_fail(fah, ctx);
  duk_pop(ctx);
}

static void
fah_ok(es_fa_handle_t *fah)
{
  hts_mutex_lock(&es_fa_mutex);
  if(fah->fah_status == ES_FA_WORKING) {
    fah->fah_status = ES_FA_OK;
    hts_cond_broadcast(&es_fa_cond);
  }
  hts_mutex_unlock(&es_fa_mutex);
}


static void
fah_ok_val(es_fa_handle_t *fah, int val)
{
  hts_mutex_lock(&es_fa_mutex);
  if(fah->fah_status == ES_FA_WORKING) {
    fah->fah_status = ES_FA_OK;
    fah->fah_return_value = val;
    hts_cond_broadcast(&es_fa_cond);
  }
  hts_mutex_unlock(&es_fa_mutex);
}





static void
es_fa_open(void *aux)
{
  es_fa_handle_t *fah = aux;
  es_fap_t *ef = fah->fah_ef;
  es_context_t *ec = ef->super.er_ctx;
  duk_context *ctx = ec->ec_duk;

  es_context_begin(ec);

  es_push_root(ctx, ef);
  duk_get_prop_string(ctx, -1, "open");
  es_push_native_obj(ctx, &es_native_fah, fah_retain(fah));
  duk_push_string(ctx, fah->fah_url);

  int rc = duk_pcall(ctx, 2);
  if(rc) {
    fah_exception(fah, ctx);
    es_dump_err(ctx);
  }

  es_context_end(ec, 0);
  fah_release(fah);
}




static fa_handle_t *
es_fap_open(struct fa_protocol *fap, const char *url,
            char *errbuf, size_t errsize, int flags,
            struct fa_open_extra *foe)
{
  es_fa_handle_t *fah = calloc(1, sizeof(es_fa_handle_t));
  fah->fah_size = -1;
  es_fap_t *ef = fap->fap_opaque;

  fah->fh.fh_proto = fap;

  fah->fah_ef = ef;
  es_resource_retain(&ef->super);

  fah->fah_url = strdup(url);
  fah->fah_errbuf = errbuf;
  fah->fah_errsize = errsize;
  fah->fah_flags = flags;

  atomic_set(&fah->fah_refcount, 2); // One to return, one for task

  hts_mutex_lock(&es_fa_mutex);
  fah->fah_status = ES_FA_WORKING;

  task_run(es_fa_open, fah);

  while(fah->fah_status == ES_FA_WORKING)
    hts_cond_wait(&es_fa_cond, &es_fa_mutex);

  fah->fah_errbuf = NULL;
  fah->fah_errsize = 0;

  hts_mutex_unlock(&es_fa_mutex);

  switch(fah->fah_status) {
  case ES_FA_OK:
    return &fah->fh;

  case ES_FA_CANCELLED:
    snprintf(errbuf, errsize, "Cancelled");
    // FALLTHRU
  case ES_FA_ERROR:
    fah_release(fah);
    break;
  }
  return NULL;
}


static int
es_faprovider_openRespond(duk_context *ctx)
{
  es_fa_handle_t *fah = es_get_native_obj(ctx, 0, &es_native_fah);

  if(duk_get_boolean(ctx, 1)) {
    es_root_register(ctx, 2, fah);
    fah_ok(fah);
  } else {
    fah_fail(fah, ctx);
  }
  return 0;
}






static void
es_fa_read(void *aux)
{
  es_fa_handle_t *fah = aux;
  es_fap_t *ef = fah->fah_ef;
  es_context_t *ec = ef->super.er_ctx;
  duk_context *ctx = ec->ec_duk;

  es_context_begin(ec);

  duk_set_top(ctx, 0);

  duk_push_external_buffer(ctx);
  es_root_register(ctx, -1, fah->fah_readbuf);

  duk_config_buffer(ctx, 0, fah->fah_readbuf, fah->fah_readlen);

  es_push_root(ctx, ef);
  duk_get_prop_string(ctx, -1, "read");
  es_push_native_obj(ctx, &es_native_fah, fah_retain(fah));
  es_push_root(ctx, fah);

  duk_push_buffer_object(ctx, 0, 0, fah->fah_readlen, DUK_BUFOBJ_UINT8ARRAY);
  duk_push_int(ctx, fah->fah_readlen);
  duk_push_number(ctx, fah->fah_fpos);

  int rc = duk_pcall(ctx, 5);
  if(rc) {
    fah_exception(fah, ctx);
    es_dump_err(ctx);
  }

  es_context_end(ec, 0);
  fah_release(fah);
}




static int
es_fap_read(fa_handle_t *fh, void *buf, size_t size)
{
  es_fa_handle_t *fah = (es_fa_handle_t *)fh;
  fah->fah_readbuf = buf;
  fah->fah_readlen = size;

  hts_mutex_lock(&es_fa_mutex);
  fah->fah_status = ES_FA_WORKING;

  task_run(es_fa_read, fah_retain(fah));

  while(fah->fah_status == ES_FA_WORKING)
    hts_cond_wait(&es_fa_cond, &es_fa_mutex);

  hts_mutex_unlock(&es_fa_mutex);

  es_fap_t *ef = fah->fah_ef;
  es_context_t *ec = ef->super.er_ctx;
  duk_context *ctx = ec->ec_duk;

  es_context_begin(ec);
  es_push_root(ctx, fah->fah_readbuf);
  duk_config_buffer(ctx, -1, NULL, 0);
  es_root_unregister(ctx, fah->fah_readbuf);
  es_context_end(ec, 0);


  switch(fah->fah_status) {
  case ES_FA_OK:
    fah->fah_fpos += fah->fah_return_value;
    return fah->fah_return_value;

  case ES_FA_CANCELLED:
  case ES_FA_ERROR:
    return -1;
  }
  return 0;
}


static int
es_faprovider_readRespond(duk_context *ctx)
{
  es_fa_handle_t *fah = es_get_native_obj(ctx, 0, &es_native_fah);
  int size = duk_get_int(ctx, 1);
  if(size < 0) {
    hts_mutex_lock(&es_fa_mutex);
    if(fah->fah_status == ES_FA_WORKING) {
      fah->fah_status = ES_FA_ERROR;
      hts_cond_broadcast(&es_fa_cond);
    }
    hts_mutex_unlock(&es_fa_mutex);
  } else {
    fah_ok_val(fah, duk_get_int(ctx, 1));
  }
  return 0;
}


static void
es_fa_close(void *aux)
{
  es_fa_handle_t *fah = aux;
  es_fap_t *ef = fah->fah_ef;
  es_context_t *ec = ef->super.er_ctx;
  duk_context *ctx = ec->ec_duk;

  es_context_begin(ec);

  es_push_root(ctx, ef);
  duk_get_prop_string(ctx, -1, "close");
  es_push_native_obj(ctx, &es_native_fah, fah_retain(fah));
  es_push_root(ctx, fah);

  int rc = duk_pcall(ctx, 2);

  if(rc) {
    fah_exception(fah, ctx);
    es_dump_err(ctx);
  }

  es_root_unregister(ctx, fah);
  es_context_end(ec, 1);
  fah_release(fah);
}




static void
es_fap_close(fa_handle_t *fh)
{
  es_fa_handle_t *fah = (es_fa_handle_t *)fh;

  hts_mutex_lock(&es_fa_mutex);
  fah->fah_status = ES_FA_WORKING;

  task_run(es_fa_close, fah_retain(fah));

  while(fah->fah_status == ES_FA_WORKING)
    hts_cond_wait(&es_fa_cond, &es_fa_mutex);

  hts_mutex_unlock(&es_fa_mutex);

  fah_release(fah);

}

static int
es_faprovider_closeRespond(duk_context *ctx)
{
  es_fa_handle_t *fah = es_get_native_obj(ctx, 0, &es_native_fah);
  fah_ok_val(fah, 0);
  return 0;
}






static int
es_faprovider_setSize(duk_context *ctx)
{
  es_fa_handle_t *fah = es_get_native_obj(ctx, 0, &es_native_fah);
  fah->fah_size = duk_get_number(ctx, 1);
  return 0;
}


static int64_t
es_fap_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  es_fa_handle_t *fah = (es_fa_handle_t *)handle;
  int np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = fah->fah_fpos + pos;
    break;

  case SEEK_END:
    if(fah->fah_size == -1)
      return -1;

    np = fah->fah_size + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;

  fah->fah_fpos = np;
  return np;
}


static int64_t
es_fap_fsize(fa_handle_t *handle)
{
  es_fa_handle_t *fah = (es_fa_handle_t *)handle;
  return fah->fah_size;
}







static void
es_fa_stat(void *aux)
{
  es_fa_handle_t *fah = aux;
  es_fap_t *ef = fah->fah_ef;
  es_context_t *ec = ef->super.er_ctx;
  duk_context *ctx = ec->ec_duk;

  es_context_begin(ec);

  duk_set_top(ctx, 0);

  es_push_root(ctx, ef);
  duk_get_prop_string(ctx, -1, "stat");
  es_push_native_obj(ctx, &es_native_fah, fah_retain(fah));
  duk_push_string(ctx, fah->fah_url);

  int rc = duk_pcall(ctx, 2);
  if(rc) {
    fah_exception(fah, ctx);
    es_dump_err(ctx);
  }

  es_context_end(ec, 0);
  fah_release(fah);
}



static int
es_fap_stat(struct fa_protocol *fap, const char *url,
            struct fa_stat *buf,
            int flags, char *errbuf, size_t errsize)
{
  es_fa_handle_t *fah = calloc(1, sizeof(es_fa_handle_t));
  es_fap_t *ef = fap->fap_opaque;

  fah->fah_url = strdup(url);

  fah->fh.fh_proto = fap;
  fah->fah_ef = ef;
  es_resource_retain(&ef->super);

  atomic_set(&fah->fah_refcount, 2);

  fah->fah_errbuf = errbuf;
  fah->fah_errsize = errsize;
  fah->fah_status = ES_FA_WORKING;
  task_run(es_fa_stat, fah);

  hts_mutex_lock(&es_fa_mutex);

  while(fah->fah_status == ES_FA_WORKING)
    hts_cond_wait(&es_fa_cond, &es_fa_mutex);

  fah->fah_errbuf = NULL;
  fah->fah_errsize = 0;

  hts_mutex_unlock(&es_fa_mutex);

  int r = fah->fah_return_value;
  if(!r) {
    buf->fs_size = fah->fah_size;
    buf->fs_type = fah->fah_type;
    buf->fs_mtime = fah->fah_mtime;
  }

  fah_release(fah);
  return r;
}


static int
es_faprovider_statRespond(duk_context *ctx)
{
  es_fa_handle_t *fah = es_get_native_obj(ctx, 0, &es_native_fah);


  if(duk_get_boolean(ctx, 1)) {
    hts_mutex_lock(&es_fa_mutex);
    if(fah->fah_status == ES_FA_WORKING) {
      fah->fah_status = ES_FA_OK;
      fah->fah_return_value = 0;

      fah->fah_size = duk_get_number(ctx, 2);
      fah->fah_type = CONTENT_FILE;

      const char *type = duk_get_string(ctx, 3);
      if(type) {
        if(!strcmp(type, "dir"))
          fah->fah_type = CONTENT_DIR;
      }
      fah->fah_mtime = duk_get_number(ctx, 4);
      hts_cond_broadcast(&es_fa_cond);
    }
    hts_mutex_unlock(&es_fa_mutex);
  } else {
    fah_fail(fah, ctx);
  }
  return 0;
}




static int
es_faprovider_register(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  const char *name = duk_require_string(ctx, 0);

  es_fap_t *ef = es_resource_alloc(&es_resource_fap);

  ef->fap.fap_opaque = ef;
  if(es_prop_is_true(ctx, 1, "cachable"))
    ef->fap.fap_flags |= FAP_ALLOW_CACHE,

  ef->fap.fap_fini  = es_fap_fini;
  ef->fap.fap_open  = es_fap_open;
  ef->fap.fap_seek  = es_fap_seek;
  ef->fap.fap_read  = es_fap_read;
  ef->fap.fap_close = es_fap_close;
  ef->fap.fap_fsize = es_fap_fsize;
  ef->fap.fap_stat  = es_fap_stat;


  ef->name = strdup(name);
  ef->fap.fap_name = ef->name;

  es_resource_retain(&ef->super); // Refcount owned by FAP
  fileaccess_register_dynamic(&ef->fap);

  es_resource_link(&ef->super, ec, 1);
  es_root_register(ctx, 1, ef);
  es_resource_push(ctx, &ef->super);
  return 1;
}



static const duk_function_list_entry fnlist_faprovider[] = {
  { "register",     es_faprovider_register,     2 },
  { "openRespond",  es_faprovider_openRespond,  3 },
  { "readRespond",  es_faprovider_readRespond,  2 },
  { "closeRespond", es_faprovider_closeRespond, 1 },
  { "statRespond",  es_faprovider_statRespond,  5 },
  { "setSize",      es_faprovider_setSize,      2 },
  { NULL, NULL, 0}
};

ES_MODULE("faprovider", fnlist_faprovider);
