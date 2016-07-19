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
#include "fileaccess.h"
#include "prop/prop.h"
#include "fa_proto.h"
#include "fa_vfs.h"

LIST_HEAD(vfs_mapping_list, vfs_mapping);

static struct vfs_mapping_list vfs_exported_mappings;
static hts_mutex_t vfs_mutex;

static const char *READMETXT =
  "This is "APPNAMEUSER"'s exported file system\n"
  "\n"
  "Items on home screen and in 'Local network' that contain files and\n"
  "folders will appear here\n";

/**
 *
 */
typedef struct vfs_mapping {
  LIST_ENTRY(vfs_mapping) vm_link;
  rstr_t *vm_vdir;
  int vm_vdirlen;
  char *vm_url;

  prop_sub_t *vm_url_sub;
  prop_sub_t *vm_name_sub;

  int vm_is_fs;
  int vm_exported;

} vfs_mapping_t;



/**
 *
 */
static int
vm_compar(const vfs_mapping_t *a, const vfs_mapping_t *b)
{
  return strcmp(rstr_get(a->vm_vdir), rstr_get(b->vm_vdir));
}


/**
 *
 */
static void
update_export(vfs_mapping_t *vm)
{
  int export = vm->vm_is_fs && vm->vm_vdir && rstr_get(vm->vm_vdir)[0];

  if(!export) {
    if(!vm->vm_exported)
      return;

    LIST_REMOVE(vm, vm_link);
    vm->vm_exported = 0;

  } else {

    if(vm->vm_exported)
      LIST_REMOVE(vm, vm_link);
    else
      vm->vm_exported = 1;

    LIST_INSERT_SORTED(&vfs_exported_mappings, vm, vm_link,
                       vm_compar, vfs_mapping_t);
  }
}


/**
 *
 */
static vfs_mapping_t *
find_mapping(const char *path, const char **remain)
{
  int plen = strlen(path);
  vfs_mapping_t *vm;

  LIST_FOREACH(vm, &vfs_exported_mappings, vm_link) {
    if(plen < vm->vm_vdirlen)
      continue;

    if(memcmp(path, rstr_get(vm->vm_vdir), vm->vm_vdirlen))
      continue;
    if(path[vm->vm_vdirlen] == 0) {
      *remain = NULL;
      return vm;
    }
    if(path[vm->vm_vdirlen] == '/') {
      *remain = path + vm->vm_vdirlen + 1;
      return vm;
    }
  }
  return NULL;
}


/**
 *
 */
static int
resolve_mapping(const char *path, char *newpath, size_t newpathlen)
{
  const char *remain;
  hts_mutex_lock(&vfs_mutex);

  vfs_mapping_t *vm = find_mapping(path, &remain);
  if(vm == NULL) {
    hts_mutex_unlock(&vfs_mutex);
    return -1;
  }
  if(remain)
    snprintf(newpath, newpathlen, "%s/%s", vm->vm_url, remain);
  else
    snprintf(newpath, newpathlen, "%s", vm->vm_url);

  hts_mutex_unlock(&vfs_mutex);
  return 0;
}


/**
 *
 */
static int
vfs_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
            char *errbuf, size_t errlen, int flags)
{
  char newpath[1024];

  if(!strcmp(url, "/")) {
    vfs_mapping_t *vm;

    hts_mutex_lock(&vfs_mutex);

    if(LIST_FIRST(&vfs_exported_mappings) == NULL)
      fa_dir_add(fd, "vfs:///README.TXT", "README.TXT", CONTENT_FILE);

    LIST_FOREACH(vm, &vfs_exported_mappings, vm_link) {
      fa_dir_add(fd, vm->vm_url, rstr_get(vm->vm_vdir), CONTENT_DIR);
    }
    hts_mutex_unlock(&vfs_mutex);
    return 0;
  }

  url++;

  if(resolve_mapping(url, newpath, sizeof(newpath))) {
    snprintf(errbuf, errlen, "No such file or directory");
    return -1;
  }
  return fa_scandir2(fd, newpath, errbuf, errlen, flags);
}


/**
 *
 */
static int
resolve_mapping2(const char *url, char *newpath, size_t newpathlen,
                 char *errbuf, size_t errlen)
{
  if(*url != '/') {
    snprintf(errbuf, errlen, "No such file or directory");
    return -1;
  }

  url++;

  if(resolve_mapping(url, newpath, newpathlen)) {
    snprintf(errbuf, errlen, "Invalid virtual directory");
    return -1;
  }

  if(*newpath == 0) {
    snprintf(errbuf, errlen, "Invalid virtual directory");
    return -1;
  }
  return 0;
}



/**
 *
 */
static fa_handle_t *
vfs_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
         int flags, struct fa_open_extra *foe)
{
  char newpath[1024];

  if(!strcmp(url, "/README.TXT"))
    return memfile_make(READMETXT, strlen(READMETXT));

  if(resolve_mapping2(url, newpath, sizeof(newpath), errbuf, errlen))
    return NULL;
  return fa_open_ex(newpath, errbuf, errlen, flags, foe);
}


/**
 *
 */
static int
vfs_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
         int flags, char *errbuf, size_t errlen)
{
  char newpath[1024];

  memset(fs, 0, sizeof(struct fa_stat));


  if(!strcmp(url, "/README.TXT")) {
    fs->fs_type = CONTENT_FILE;
    fs->fs_size = strlen(READMETXT);
    return 0;
  }


  if(!strcmp(url, "/")) {
    fs->fs_type = CONTENT_DIR;
    return 0;
  }

  url++;

  if(resolve_mapping(url, newpath, sizeof(newpath))) {
    snprintf(errbuf, errlen, "No such file or directory");
    return -1;
  }
  return fa_stat_ex(newpath, fs, errbuf, errlen, flags);
}



/**
 *
 */
static int
vfs_makedir(fa_protocol_t *fap, const char *url)
{
  char newpath[1024];

  if(resolve_mapping2(url, newpath, sizeof(newpath), NULL, 0))
    return -1;

  return fa_makedir(newpath);
}


/**
 *
 */
static int
vfs_unlink(const fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  char newpath[1024];

  if(resolve_mapping2(url, newpath, sizeof(newpath), errbuf, errlen))
    return -1;

  return fa_unlink(newpath, errbuf, errlen);
}


/**
 *
 */
static int
vfs_rmdir(const fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  char newpath[1024];

  if(resolve_mapping2(url, newpath, sizeof(newpath), errbuf, errlen))
    return -1;

  return fa_rmdir(newpath, errbuf, errlen);
}


/**
 *
 */
static int
vfs_rename(const fa_protocol_t *fap, const char *old, const char *new,
           char *errbuf, size_t errlen)
{
  char newpath[1024];
  char oldpath[1024];

  if(resolve_mapping2(new, newpath, sizeof(newpath), errbuf, errlen))
    return -1;

  if(resolve_mapping2(old, oldpath, sizeof(oldpath), errbuf, errlen))
    return -1;

  return fa_rename(oldpath, newpath, errbuf, errlen);
}

/**
 *
 */
static void
vfs_mapping_set_url(void *opaque, rstr_t *str)
{
  vfs_mapping_t *vm = opaque;
  const char *url = rstr_get(str);
  char realurl[1024];

  if(url != NULL && !strncmp(url, "search:", strlen("search:")))
    url += strlen("search:");

  if(url != NULL) {
    vm->vm_is_fs = fa_can_handle(url, NULL, 0);

    if(vm->vm_is_fs) {
      if(!fa_normalize(url, realurl, sizeof(realurl)))
        url = realurl;
    } else {
      url = NULL;
    }
  } else {
    vm->vm_is_fs = 0;
  }

  mystrset(&vm->vm_url, url);

  update_export(vm);
}

/**
 *
 */
static void
vfs_mapping_set_title(void *opaque, rstr_t *str)
{
  vfs_mapping_t *vm = opaque;

  rstr_set(&vm->vm_vdir, str);
  if(str)
    vm->vm_vdirlen = strlen(rstr_get(str));

  update_export(vm);
}


/**
 *
 */
static void
vfs_add_node(prop_t *p)
{
  vfs_mapping_t *vm = calloc(1, sizeof(vfs_mapping_t));

  prop_tag_set(p, &vfs_exported_mappings, vm);

  vm->vm_url_sub =
    prop_subscribe(0,
                   PROP_TAG_NAMED_ROOT, p, "node",
                   PROP_TAG_NAME("node", "url"),
                   PROP_TAG_CALLBACK_RSTR, vfs_mapping_set_url, vm,
                   PROP_TAG_MUTEX, &vfs_mutex,
                   NULL);

  vm->vm_name_sub =
    prop_subscribe(0,
                   PROP_TAG_NAMED_ROOT, p, "node",
                   PROP_TAG_NAME("node", "title"),
                   PROP_TAG_CALLBACK_RSTR, vfs_mapping_set_title, vm,
                   PROP_TAG_MUTEX, &vfs_mutex,
                   NULL);
}


/**
 *
 */
static void
vfs_del_node(vfs_mapping_t *vm)
{
  prop_unsubscribe(vm->vm_url_sub);
  prop_unsubscribe(vm->vm_name_sub);

  if(vm->vm_exported)
    LIST_REMOVE(vm, vm_link);
  rstr_release(vm->vm_vdir);
  free(vm->vm_url);
  free(vm);
}


/**
 *
 */
static void
vfs_service_callback(void *opaque, prop_event_t event, ...)
{
  prop_vec_t *pv;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;
  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_BEFORE:
    vfs_add_node(va_arg(ap, prop_t *));
    break;

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    for(int i = 0; i < pv->pv_length; i++)
      vfs_add_node(pv->pv_vec[i]);
    break;

  case PROP_DEL_CHILD:
    vfs_del_node(prop_tag_clear(va_arg(ap, prop_t *),
                                &vfs_exported_mappings));
    break;
  }
}

/**
 *
 */
static void
vfs_init(void)
{
  hts_mutex_init(&vfs_mutex);

  prop_subscribe(0,
                 PROP_TAG_NAME("global", "services", "all"),
                 PROP_TAG_CALLBACK, vfs_service_callback, NULL,
                 PROP_TAG_MUTEX, &vfs_mutex,
                 NULL);


}


/**
 *
 */
fa_protocol_t fa_protocol_vfs = {
  .fap_name  = "vfs",
  .fap_init  = vfs_init,
  .fap_scan  = vfs_scandir,
  .fap_open  = vfs_open,
  .fap_stat  = vfs_stat,
  .fap_makedir = vfs_makedir,
  .fap_unlink   = vfs_unlink,
  .fap_rmdir    = vfs_rmdir,
  .fap_rename   = vfs_rename,
};

FAP_REGISTER(vfs);
