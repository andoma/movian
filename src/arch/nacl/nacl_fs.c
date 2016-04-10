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
#include "fileaccess/fa_proto.h"

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/pp_directory_entry.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_file_system.h"
#include "ppapi/c/ppb_file_ref.h"
#include "ppapi/c/ppb_file_io.h"
#include "ppapi/c/ppb_var.h"

#include "nacl.h"

extern const PPB_Core *ppb_core;
extern const PPB_FileSystem *ppb_filesystem;
extern const PPB_FileRef *ppb_fileref;
extern const PPB_FileIO *ppb_fileio;
extern const PPB_Var *ppb_var;

extern PP_Resource g_persistent_fs;
extern PP_Resource g_cache_fs;

extern PP_Instance g_Instance;

typedef struct fa_pepper {
  fa_handle_t fh;
  int64_t fpos;
  PP_Resource res;
  int for_writing;
} fa_pepper_t;


/**
 *
 */
static inline void
check_main_thread(void)
{
  if(ppb_core->IsMainThread())
    *(int *)7 = 0;
}


/**
 *
 */
static PP_Resource
get_res(const fa_protocol_t *fap, const char *path)
{
  if(!strcmp(fap->fap_name, "cache"))
    return ppb_fileref->Create(g_cache_fs, path);

  return ppb_fileref->Create(g_persistent_fs, path);
}


static fa_err_code_t
map_err(int32_t i)
{
  switch(i) {
  case 0: return 0;

  case PP_ERROR_FILENOTFOUND: return FAP_NOENT;
  case PP_ERROR_FILEEXISTS:   return FAP_EXIST;

  default:
    TRACE(TRACE_DEBUG, "MAP", "Mapping error %d to default", i);
    return FAP_ERROR;
  }
}


struct dir_items {
  struct PP_DirectoryEntry *data;
  int element_count;
};

static void *
get_data_buf(void* user_data, uint32_t count, uint32_t size)
{
  struct dir_items *output = (struct dir_items *)user_data;
  output->element_count = count;

  if(size) {
    output->data = malloc(count * size);
    if(output->data == NULL)
      output->element_count = 0;

  } else {
    output->data = NULL;
  }
  return output->data;
}




/**
 *
 */
static void
add_item(fa_protocol_t *fap, const char *dir, fa_dir_t *fd,
         const char *str, int len, PP_FileType type)
{
  char out[2048];
  char *fname = alloca(len + 1);
  memcpy(fname, str, len);
  fname[len] = 0;

  snprintf(out, sizeof(out), "%s://%s/%s", fap->fap_name, dir, fname);

  fa_dir_add(fd, out, fname,
             type == PP_FILETYPE_DIRECTORY ? CONTENT_DIR : CONTENT_FILE);
}


/**
 *
 */
static int
fs_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
           char *errbuf, size_t errlen, int flags)
{
  PP_Resource file_res = get_res(fap, url);
  if(file_res == 0) {
    snprintf(errbuf, errlen, "Unable to create path ref");
    return -1;
  }

  struct dir_items array = {};
  struct PP_ArrayOutput output = {&get_data_buf, &array};

  int r = ppb_fileref->ReadDirectoryEntries(file_res, output,
                                            PP_BlockUntilComplete());
  if(r) {
    snprintf(errbuf, errlen, "%s", pepper_errmsg(r));
    free(array.data);
    return -1;
  }


  for(int i = 0; i < array.element_count; i++) {
    struct PP_Var var = ppb_fileref->GetName(array.data[i].file_ref);
    uint32_t len;
    const char *s = ppb_var->VarToUtf8(var, &len);
    if(s != NULL)
      add_item(fap, url, fd, s, len, array.data[i].file_type);
    ppb_var->Release(var);
    ppb_core->ReleaseResource(array.data[i].file_ref);
  }

  free(array.data);
  return 0;
}


/**
 * Open file
 */
static fa_handle_t *
fs_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	int flags, struct fa_open_extra *foe)
{
  check_main_thread();

  PP_Resource file_res = get_res(fap, url);
  if(file_res == 0) {
    snprintf(errbuf, errlen, "Unable to create path ref");
    return NULL;
  }

  PP_Resource io_res = ppb_fileio->Create(g_Instance);
  PP_FileOpenFlags ppflags = PP_FILEOPENFLAG_READ;
  if(flags & FA_WRITE) {

    ppflags |= PP_FILEOPENFLAG_WRITE | PP_FILEOPENFLAG_CREATE;

    if(!(flags & FA_APPEND)) {
      ppflags |= PP_FILEOPENFLAG_TRUNCATE;
    }
  }

  int x = ppb_fileio->Open(io_res, file_res, ppflags, PP_BlockUntilComplete());
  ppb_core->ReleaseResource(file_res);

  if(x) {
    ppb_core->ReleaseResource(io_res);
    snprintf(errbuf, errlen, "%s", pepper_errmsg(x));
    return NULL;
  }
  fa_pepper_t *fp = calloc(1, sizeof(fa_pepper_t));
  fp->res = io_res;
  fp->for_writing = !!(flags & FA_WRITE);
  fp->fh.fh_proto = fap;

  if(flags & FA_APPEND)
    fap->fap_seek(&fp->fh, 0, SEEK_END, 0);

  return &fp->fh;
}


/**
 *
 */
static void
fs_close(fa_handle_t *fh)
{
  check_main_thread();
  fa_pepper_t *fp = (fa_pepper_t *)fh;

  if(fp->for_writing)
    ppb_fileio->Flush(fp->res, PP_BlockUntilComplete());

  ppb_fileio->Close(fp->res);

  ppb_core->ReleaseResource(fp->res);

  free(fh);
}


/**
 *
 */
static int
fs_read(fa_handle_t *fh, void *buf, size_t size)
{
  check_main_thread();
  fa_pepper_t *fp = (fa_pepper_t *)fh;

  int x = ppb_fileio->Read(fp->res, fp->fpos, buf, size,
                           PP_BlockUntilComplete());
  if(x > 0)
    fp->fpos += x;
  return x;
}


/**
 *
 */
static int
fs_write(fa_handle_t *fh, const void *buf, size_t size)
{
  check_main_thread();
  fa_pepper_t *fp = (fa_pepper_t *)fh;

  int x = ppb_fileio->Write(fp->res, fp->fpos, buf, size,
                            PP_BlockUntilComplete());

  if(x > 0)
    fp->fpos += x;
  return x;
}


/**
 *
 */
static int64_t
fs_seek(fa_handle_t *fh, int64_t pos, int whence, int lazy)
{
  check_main_thread();
  fa_pepper_t *fp = (fa_pepper_t *)fh;
  int64_t np;
  struct PP_FileInfo info;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = fp->fpos + pos;
    break;

  case SEEK_END:
    if(ppb_fileio->Query(fp->res, &info, PP_BlockUntilComplete()))
      return -1;
    np = info.size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  fp->fpos = np;
  return np;
}


/**
 *
 */
static int64_t
fs_fsize(fa_handle_t *fh)
{
  check_main_thread();
  fa_pepper_t *fp = (fa_pepper_t *)fh;
  struct PP_FileInfo info;

  if(ppb_fileio->Query(fp->res, &info, PP_BlockUntilComplete()))
    return -1;
  return info.size;
}


/**
 *
 */
static int
fs_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	int flags, char *errbuf, size_t errlen)
{
  check_main_thread();
  PP_Resource file_res = get_res(fap, url);
  struct PP_FileInfo info;

  if(file_res == 0) {
    snprintf(errbuf, errlen, "Unable to create path ref");
    return FAP_ERROR;
  }

  int r = ppb_fileref->Query(file_res, &info, PP_BlockUntilComplete());
  ppb_core->ReleaseResource(file_res);
  if(r) {
    snprintf(errbuf, errlen, "%s", pepper_errmsg(r));
    return map_err(r);
  }
  fs->fs_size = info.size;
  fs->fs_mtime = info.last_modified_time;
  fs->fs_type = info.type == PP_FILETYPE_REGULAR ? CONTENT_FILE : CONTENT_DIR;
  return 0;
}


/**
 *
 */
static int
fs_unlink(const fa_protocol_t *fap, const char *url,
          char *errbuf, size_t errlen)
{
  check_main_thread();
  PP_Resource res = get_res(fap, url);
  if(res == 0) {
    snprintf(errbuf, errlen, "Unable to create path ref");
    return -1;
  }

  int x = ppb_fileref->Delete(res, PP_BlockUntilComplete());
  ppb_core->ReleaseResource(res);
  if(x) {
    snprintf(errbuf, errlen, "%s", pepper_errmsg(x));
    return -1;
  }
  return 0;
}



/**
 *
 */
static int
fs_rename(const fa_protocol_t *fap, const char *old, const char *new,
          char *errbuf, size_t errlen)
{
  check_main_thread();
  PP_Resource old_res = get_res(fap, old);
  if(old_res == 0) {
    snprintf(errbuf, errlen, "Unable to create path ref");
    return -1;
  }
  PP_Resource new_res = get_res(fap, new);
  if(new_res == 0) {
    ppb_core->ReleaseResource(old_res);
    snprintf(errbuf, errlen, "Unable to create path ref");
    return -1;
  }

  int x = ppb_fileref->Rename(old_res, new_res, PP_BlockUntilComplete());
  ppb_core->ReleaseResource(new_res);
  ppb_core->ReleaseResource(old_res);
  if(x) {
    snprintf(errbuf, errlen, "%s", pepper_errmsg(x));
    return -1;
  }
  return 0;
}


/**
 *
 */
static int
fs_ftruncate(fa_handle_t *fh, uint64_t newsize)
{
  check_main_thread();
  fa_pepper_t *fp = (fa_pepper_t *)fh;

  int x = ppb_fileio->SetLength(fp->res, newsize, PP_BlockUntilComplete());
  return x ? -1 : 0;
}

/**
 *
 */
static fa_err_code_t
fs_mkdir(fa_protocol_t *fap, const char *url)
{
  check_main_thread();
  PP_Resource res = get_res(fap, url);
  fa_err_code_t r;
  if(res == 0)
    return FAP_ERROR;

  r = map_err(ppb_fileref->MakeDirectory(res, 0, PP_BlockUntilComplete()));
  ppb_core->ReleaseResource(res);
  return r;
}


/**
 *
 */
static fa_err_code_t
fs_fsinfo(struct fa_protocol *fap, const char *url, fa_fsinfo_t *ffi)
{
  nacl_fsinfo(&ffi->ffi_size, &ffi->ffi_avail, fap->fap_name);
  return 0;
}


fa_protocol_t fa_protocol_cache = {
  .fap_name  = "cache",
  .fap_scan  = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_write = fs_write,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
  .fap_unlink= fs_unlink,
  .fap_rmdir = fs_unlink,
  .fap_rename = fs_rename,
  .fap_ftruncate = fs_ftruncate,
  .fap_makedir = fs_mkdir,
  .fap_fsinfo = fs_fsinfo,
};

FAP_REGISTER(cache);


fa_protocol_t fa_protocol_persistent = {
  .fap_name  = "persistent",
  .fap_scan  = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_write = fs_write,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
  .fap_unlink= fs_unlink,
  .fap_rmdir = fs_unlink,
  .fap_rename = fs_rename,
  .fap_ftruncate = fs_ftruncate,
  .fap_makedir = fs_mkdir,
  .fap_fsinfo = fs_fsinfo,
};

FAP_REGISTER(persistent);
