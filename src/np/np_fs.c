#include "main.h"
#include "np.h"


struct vmirstat {
  uint32_t  dev;
  uint32_t  ino;
  uint32_t  mode;
  uint32_t  nlink;
  uint32_t  uid;
  uint32_t  gid;
  uint32_t  rdev;
  uint64_t  size;
  uint32_t  blksize;
  uint32_t  blocks;
  uint32_t  atime;
  uint32_t  mtime;
  uint32_t  ctime;
};


static int
np_stat(void *ret, const void *rf, struct ir_unit *iu)
{
  const char *path = vmir_vm_ptr(&rf, iu);
  struct vmirstat *buf = vmir_vm_ptr(&rf, iu);
  struct fa_stat st;

  if(fa_stat(path, &st, NULL, 0)) {
    vmir_vm_ret32(ret, -1);
    return 0;
  }

  memset(buf, 0, sizeof(struct vmirstat));
  buf->mtime = st.fs_mtime;
  buf->size = st.fs_size;
  switch(st.fs_type) {
  case CONTENT_DIR:
  case CONTENT_SHARE:
    buf->mode = 0040000;
    break;
  default:
    buf->mode = 0100000;
    break;
  }
  buf->mode |= 0777; // rwxrwxrwx
  vmir_vm_ret32(ret, 0);
  return 0;
}


static int
np_fstat(void *ret, const void *rf, struct ir_unit *iu)
{
  uint32_t fd = vmir_vm_arg32(&rf);
  struct vmirstat *buf = vmir_vm_ptr(&rf, iu);

  memset(buf, 0, sizeof(struct vmirstat));
  fa_handle_t *fh = (fa_handle_t *)vmir_fd_get(iu, fd, VMIR_FD_TYPE_FILEHANDLE);
  if(fh == NULL) {
    vmir_vm_ret32(ret, -1);
    return 0;
  }

  buf->size = fa_fsize(fh);
  buf->nlink = 1;
  buf->mode = 0777 | 0100000; // rwxrwxrwx regular file
  vmir_vm_ret32(ret, 0);
  return 0;
}


static int
np_opendir(void *ret, const void *rf, struct ir_unit *iu)
{
  vmir_vm_ret32(ret, 0);
  return 0;
}

static int
np_readdir(void *ret, const void *rf, struct ir_unit *iu)
{
  vmir_vm_ret32(ret, 0);
  return 0;
}

static int
np_closedir(void *ret, const void *rf, struct ir_unit *iu)
{
  vmir_vm_ret32(ret, 0);
  return 0;
}



static vmir_errcode_t
np_fs_open(void *opaque, const char *path,
           vmir_openflags_t flags, intptr_t *fh)
{
  int myflags = 0;
  if(flags & VMIR_FS_OPEN_WRITE) {
    myflags |= FA_WRITE;
    if(flags & VMIR_FS_OPEN_APPEND) {
      myflags |= FA_APPEND;
    }
  } else {
    myflags |= FA_BUFFERED_BIG;
  }

  fa_handle_t *f = fa_open_ex(path, NULL, 0, myflags, NULL);
  if(f == NULL)
    return VMIR_ERR_FS_ERROR;
  *fh = (intptr_t)f;
  return 0;
}

static void
np_fs_close(void *opaque, intptr_t fh)
{
  fa_close((fa_handle_t *)fh);
}

static ssize_t
np_fs_read(void *opaque, intptr_t fh, void *buf, size_t count)
{
  return fa_read((fa_handle_t *)fh, buf, count);
}

static ssize_t
np_fs_write(void *opaque, intptr_t fh, const void *buf, size_t count)
{
  return fa_write((fa_handle_t *)fh, buf, count);
}

static int64_t
np_fs_seek(void *opaque, intptr_t fh, int64_t offset, int whence)
{
  return fa_seek((fa_handle_t *)fh, offset, whence);
}



static const vmir_fsops_t np_fsops =  {
  .open  = np_fs_open,
  .close = np_fs_close,
  .read  = np_fs_read,
  .write = np_fs_write,
  .seek  = np_fs_seek,
};


static const vmir_function_tab_t np_fs_funcs[] = {
  {"stat",                 &np_stat},
  {"lstat",                &np_stat},
  {"fstat",                &np_fstat},
  {"opendir",              &np_opendir},
  {"readdir",              &np_readdir},
  {"closedir",             &np_closedir},
};


static void
np_fs_init(np_context_t *np)
{
  vmir_set_fsops(np->np_unit, &np_fsops);
}

NP_MODULE("fs", np_fs_funcs, np_fs_init, NULL);
