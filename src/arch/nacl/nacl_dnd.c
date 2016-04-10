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

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_messaging.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_var_dictionary.h"
#include "ppapi/c/ppb_var_array_buffer.h"

#include "main.h"
#include "fileaccess/fa_proto.h"

#include "nacl_dnd.h"
#include "nacl.h"

#include "misc/minmax.h"

LIST_HEAD(fa_dnd_list, fa_dnd);

extern const PPB_Var *ppb_var;
extern const PPB_VarDictionary *ppb_vardict;
extern const PPB_Messaging *ppb_messaging;
extern const PPB_VarArrayBuffer *ppb_vararraybuf;

extern PP_Instance g_Instance;

static HTS_MUTEX_DECL(dnd_mutex);
static struct fa_dnd_list fa_dnds;
static int req_id_gen;

typedef struct fa_dnd {
  fa_handle_t fh;
  int64_t fpos;
  int64_t size;
  int fileid;
  int errcode;
  int reqid;
  hts_cond_t cond;
  LIST_ENTRY(fa_dnd) link;

  void *buf;
  int readsize;
  int resultsize;

} fa_dnd_t;



static fa_dnd_t *
get_dnd_from_reply(struct PP_Var var)
{
  fa_dnd_t *dnd;
  int reqid = nacl_var_dict_get_int64(var, "reqid", -1);

  hts_mutex_lock(&dnd_mutex);

  LIST_FOREACH(dnd, &fa_dnds, link) {
    if(dnd->reqid == reqid)
      break;
  }

  if(dnd == NULL)
    TRACE(TRACE_ERROR, "DND", "Got unexpected request %d", reqid);

  return dnd;
}


void
nacl_dnd_open_reply(struct PP_Var dict)
{
  fa_dnd_t *dnd = get_dnd_from_reply(dict);

  if(dnd != NULL) {
    dnd->errcode = nacl_var_dict_get_int64(dict, "error", 0);

    if(dnd->errcode == 0) {
      dnd->size = nacl_var_dict_get_int64(dict, "size", 0);
    }
    hts_cond_signal(&dnd->cond);
  }

  hts_mutex_unlock(&dnd_mutex);
}


void
nacl_dnd_read_reply(struct PP_Var dict)
{
  fa_dnd_t *dnd = get_dnd_from_reply(dict);

  if(dnd != NULL) {
    dnd->errcode = nacl_var_dict_get_int64(dict, "error", 0);

    if(dnd->errcode == 0) {
      struct PP_Var var_key = ppb_var->VarFromUtf8("buf", 3);
      struct PP_Var var_buf = ppb_vardict->Get(dict, var_key);
      ppb_var->Release(var_key);

      dnd->resultsize = 0;

      uint32_t srclen;
      if(ppb_vararraybuf->ByteLength(var_buf, &srclen)) {
        const void *src = ppb_vararraybuf->Map(var_buf);
        if(src != NULL) {
          dnd->resultsize = MIN(srclen, dnd->readsize);
          memcpy(dnd->buf, src, dnd->resultsize);
          ppb_vararraybuf->Unmap(var_buf);
        } else {
          TRACE(TRACE_ERROR, "DND", "MAP Failed");
        }
      } else {
        TRACE(TRACE_ERROR, "DND", "bytelength failed");
      }
      ppb_var->Release(var_buf);
    }

    hts_cond_signal(&dnd->cond);
  }

  hts_mutex_unlock(&dnd_mutex);

}


/**
 *
 */
static int
send_open(const char *filename)
{
  struct PP_Var var_dict = ppb_vardict->Create();

  nacl_dict_set_str(var_dict, "msgtype", "openfile");
  nacl_dict_set_str(var_dict, "filename", filename);

  int id = ++req_id_gen;
  nacl_dict_set_int(var_dict, "reqid", id);

  ppb_messaging->PostMessage(g_Instance, var_dict);
  ppb_var->Release(var_dict);
  return id;
}


/**
 *
 */
static int
send_read(int fh, int64_t fpos, int size)
{
  struct PP_Var var_dict = ppb_vardict->Create();

  nacl_dict_set_str(var_dict, "msgtype", "readfile");

  int id = ++req_id_gen;
  nacl_dict_set_int(var_dict, "reqid", id);

  nacl_dict_set_int64(var_dict, "fpos", fpos);
  nacl_dict_set_int(var_dict, "size", size);

  ppb_messaging->PostMessage(g_Instance, var_dict);
  ppb_var->Release(var_dict);
  return id;
}



static fa_handle_t *
dnd_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	int flags, struct fa_open_extra *foe)
{
  TRACE(TRACE_DEBUG, "DND", "Opening %s", url);

  fa_dnd_t *dnd = calloc(1, sizeof(fa_dnd_t));
  hts_cond_init(&dnd->cond, &dnd_mutex);

  hts_mutex_lock(&dnd_mutex);
  dnd->errcode = -1;
  LIST_INSERT_HEAD(&fa_dnds, dnd, link);

  dnd->reqid = send_open(url);

  while(dnd->errcode == -1)
    hts_cond_wait(&dnd->cond, &dnd_mutex);

  LIST_REMOVE(dnd, link);
  hts_mutex_unlock(&dnd_mutex);

  if(dnd->errcode) {
    snprintf(errbuf, errlen, "Failed to open file");
    hts_cond_destroy(&dnd->cond);
    free(dnd);
    return NULL;
  }

  dnd->fh.fh_proto = fap;

  return &dnd->fh;
}


/**
 *
 */
static void
dnd_close(fa_handle_t *fh)
{
  fa_dnd_t *dnd = (fa_dnd_t *)fh;
  hts_cond_destroy(&dnd->cond);
  free(fh);
}


static int
dnd_read(fa_handle_t *fh, void *buf, size_t size)
{
  fa_dnd_t *dnd = (fa_dnd_t *)fh;

  if(size < 1)
    return size;

  if(dnd->fpos + size > dnd->size)
    size = dnd->size - dnd->fpos;

  hts_mutex_lock(&dnd_mutex);
  dnd->errcode = -1;
  dnd->buf = buf;
  dnd->readsize = size;

  LIST_INSERT_HEAD(&fa_dnds, dnd, link);

  dnd->reqid = send_read(dnd->fileid, dnd->fpos, size);

  while(dnd->errcode == -1)
    hts_cond_wait(&dnd->cond, &dnd_mutex);

  LIST_REMOVE(dnd, link);
  hts_mutex_unlock(&dnd_mutex);
  dnd->fpos += dnd->resultsize;
  return dnd->resultsize;
}

/**
 *
 */
static int64_t
dnd_seek(fa_handle_t *fh, int64_t pos, int whence, int lazy)
{
  fa_dnd_t *dnd = (fa_dnd_t *)fh;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = dnd->fpos + pos;
    break;

  case SEEK_END:
    np = dnd->size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  dnd->fpos = np;
  return np;
}

/**
 *
 */
static int64_t
dnd_fsize(fa_handle_t *fh)
{
  fa_dnd_t *dnd = (fa_dnd_t *)fh;
  return dnd->size;
}


/**
 *
 */
static int
dnd_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
         int flags, char *errbuf, size_t errlen)
{
  // This could be handled in the generic fa layer

  fa_handle_t *fh = dnd_open(fap, url, errbuf, errlen, 0, NULL);
  if(fh == NULL)
    return -1;

  fs->fs_size = fa_fsize(fh);
  fs->fs_mtime = 0;
  fs->fs_type = CONTENT_FILE;
  fa_close(fh);
  return 0;
}


fa_protocol_t fa_protocol_dragndrop = {
  .fap_name  = "dragndrop",
  .fap_open  = dnd_open,
  .fap_close = dnd_close,
  .fap_read  = dnd_read,
  .fap_seek  = dnd_seek,
  .fap_fsize = dnd_fsize,
  .fap_stat  = dnd_stat,
};

FAP_REGISTER(dragndrop);
