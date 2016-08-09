#include <limits.h>

#include "main.h"
#include "np.h"
#include "fileaccess/fileaccess.h"
#include "nativeplugin.h"

LIST_HEAD(np_context_list, np_context);

static hts_mutex_t np_list_mutex;
static struct np_context_list np_contexts;
static int np_num_contexts;




/**
 *
 */
static void
np_context_destroy(np_context_t *np)
{
  free(np->np_mem);
  vmir_destroy(np->np_unit);
  free(np->np_path);
  free(np->np_storage);
  free(np->np_id);
  free(np);
}


/**
 *
 */
void
np_context_release(void *aux)
{
  np_context_t *np = aux;
  if(atomic_dec(&np->np_lockmgr.lm_refcount))
    return;
  np_context_destroy(np);
}


/**
 *
 */
np_context_t **
np_get_all_contexts(void)
{
  np_context_t **v = malloc(sizeof(np_context_t *) * (np_num_contexts + 1));
  np_context_t *ec;
  int i = 0;
  hts_mutex_lock(&np_list_mutex);
  LIST_FOREACH(ec, &np_contexts, np_link)
    v[i++] = np_context_retain(ec);
  hts_mutex_unlock(&np_list_mutex);
  v[i] = NULL;
  return v;
}


/**
 *
 */
static np_context_t *
np_context_find(const char *id)
{
  np_context_t *np = NULL;
  hts_mutex_lock(&np_list_mutex);
  LIST_FOREACH(np, &np_contexts, np_link) {
    if(!strcmp(np->np_id, id)) {
      np = np_context_retain(np);
      break;
    }
  }
  hts_mutex_unlock(&np_list_mutex);
  return np;
}


/**
 *
 */
void
np_lock(np_context_t *np)
{
  hts_mutex_lock(&np->np_lockmgr.lm_mutex);
}


/**
 *
 */
void
np_unlock(np_context_t *np)
{
  hts_mutex_unlock(&np->np_lockmgr.lm_mutex);
}


/**
 *
 */
void
np_context_vec_free(np_context_t **v)
{
  for(int i = 0; v[i] != NULL; i++)
    np_context_release(v[i]);
  free(v);
}

/**
 *
 */
int
np_fa_probe(fa_handle_t *fh, const void *buf, size_t len,
            metadata_t *md, const char *url)
{
  np_context_t **v = np_get_all_contexts();
  int retval = -1;
  for(int i = 0; v[i] != NULL; i++) {
    np_context_t *np = v[i];
    ir_unit_t *iu = np->np_unit;
    np_lock(np);

    ir_function_t *probe  = vmir_find_function(iu, "np_file_probe");
    if(probe != NULL) {
      fa_seek(fh, SEEK_SET, 0);

      int fd = vmir_fd_create_fh(iu, (intptr_t)fh, 0);
      if(fd != -1) {

        uint32_t buf_vm = vmir_mem_copy(iu, buf, len);
        uint32_t url_vm = vmir_mem_strdup(iu, url);

        int metadata_handle = vmir_fd_create(iu, (intptr_t)md,
                                             NP_FD_METADATA, NULL);
        vmir_vm_function_call(iu, probe, &retval,
                              fd, buf_vm, len, metadata_handle, url_vm);

        vmir_fd_close(iu, fd);
        vmir_fd_close(iu, metadata_handle);

        vmir_mem_free(iu, buf_vm);
        vmir_mem_free(iu, url_vm);
      }
    }
    np_unlock(np);
    if(retval == 0) {
      break;
    }
  }
  np_context_vec_free(v);
  return retval;
}



static int
np_metadata_set(void *ret, const void *regs, struct ir_unit *iu)
{
  int mh = vmir_vm_arg32(&regs);
  metadata_t *md = (metadata_t *)vmir_fd_get(iu, mh, NP_FD_METADATA);
  if(md == NULL)
    return 0;
  int which = vmir_vm_arg32(&regs);

  switch(which) {
  case NP_METADATA_CONTENT_TYPE:
    md->md_contenttype = vmir_vm_arg32(&regs);
    break;
  case NP_METADATA_TITLE:
    rstr_release(md->md_title);
    md->md_title = rstr_alloc(vmir_vm_ptr(&regs, iu));
    break;
  case NP_METADATA_ALBUM:
    rstr_release(md->md_album);
    md->md_album = rstr_alloc(vmir_vm_ptr(&regs, iu));
    break;
  case NP_METADATA_ARTIST:
    rstr_release(md->md_artist);
    md->md_artist = rstr_alloc(vmir_vm_ptr(&regs, iu));
    break;
  case NP_METADATA_DURATION:
    md->md_duration = vmir_vm_arg_dbl(&regs);
    break;
  case NP_METADATA_REDIRECT:
    free(md->md_redirect);
    md->md_redirect = strdup(vmir_vm_ptr(&regs, iu));
    break;
  }
  return 0;
}







static int
np_rand(void *ret, const void *rf, struct ir_unit *iu)
{
  vmir_vm_ret32(ret, rand());
  return 0;
}

static int
np_localtime_r(void *ret, const void *rf, struct ir_unit *iu)
{
  vmir_vm_ret32(ret, 0);
  return 0;
}


static int
np_time(void *ret, const void *rf, struct ir_unit *iu)
{
  time_t now;
  time(&now);
  int32_t *p = vmir_vm_ptr_nullchk(&rf, iu);
  if(p != NULL)
    *p = now;

  vmir_vm_ret32(ret, now);
  return 0;
}

static const vmir_function_tab_t np_funcs[] = {
  {"np_metadata_set",      &np_metadata_set},
  {"time",                 &np_time},
  {"rand",                 &np_rand},
  {"localtime_r",          &np_localtime_r},
};

NP_MODULE("global", np_funcs, NULL, NULL);


static LIST_HEAD(, np_module) np_modules;

void
np_register_module(np_module_t *m)
{
  LIST_INSERT_HEAD(&np_modules, m, link);
}


static vm_ext_function_t *
np_fn_resolver(const char *function, void *opaque)
{
  const np_module_t *m;
  vm_ext_function_t *f;

  LIST_FOREACH(m, &np_modules, link) {
    f = vmir_function_tab_lookup(function, m->tab, m->tabsize);
    if(f != NULL)
      return f;
  }
  return vmir_default_external_function_resolver(function, opaque);
}


static void
np_logger(ir_unit_t *iu, vmir_log_level_t level, const char *str)
{
  np_context_t *np = vmir_get_opaque(iu);
  int mylevel;
  switch(level) {
  case VMIR_LOG_FAIL:
    mylevel = TRACE_EMERG;
    break;
  case VMIR_LOG_ERROR:
    mylevel = TRACE_ERROR;
    break;
  case VMIR_LOG_INFO:
    mylevel = TRACE_INFO;
    break;
  default:
    mylevel = TRACE_DEBUG;
    break;
  }
  TRACE(mylevel, np->np_id, "%s", str);
}



static int
np_context_create(const char *id, int flags, const char *url,
                  const char *storage, char *errbuf, size_t errlen,
                  int memory_size, int stack_size)
{
  buf_t *buf = fa_load(url,
                       FA_LOAD_ERRBUF(errbuf, errlen),
                       NULL);
  if(buf == NULL)
    return -1;

  np_context_t *np = calloc(1, sizeof(np_context_t));
  np->np_id = strdup(id);
  char normalize[PATH_MAX];

  if(!fa_normalize(url, normalize, sizeof(normalize)))
    url = normalize;

  char path[PATH_MAX];

  fa_stat_t st;

  if(!fa_parent(path, sizeof(path), url) &&
     !fa_stat(path, &st, NULL, 0)) {
    np->np_path = strdup(path);
  }


  if(np->np_path == NULL)
    TRACE(TRACE_ERROR, id,
          "Unable to get parent directory for %s -- No loadPath set",
          url);

  if(storage != NULL)
    np->np_storage = strdup(storage);

  np->np_mem = malloc(memory_size);
  np->np_unit = vmir_create(np->np_mem, memory_size, 0, stack_size, np);
  vmir_set_logger(np->np_unit, np_logger);

  const np_module_t *m;
  LIST_FOREACH(m, &np_modules, link) {
    if(m->ctxinit)
      m->ctxinit(np);
  }

  vmir_set_external_function_resolver(np->np_unit, np_fn_resolver);

  if(vmir_load(np->np_unit, buf_data(buf), buf_size(buf))) {
    TRACE(TRACE_ERROR, id, "Unable to parse bitcode %s", url);
    np_context_destroy(np);
    return -1;
  }

  lockmgr_init(&np->np_lockmgr, np_context_release);
  hts_mutex_lock(&np_list_mutex);
  np_num_contexts++;
  LIST_INSERT_HEAD(&np_contexts, np, np_link);
  hts_mutex_unlock(&np_list_mutex);

  vmir_run(np->np_unit, NULL, 0, NULL);

  np_context_release(np);
  return 0;
}


/**
 *
 */
static void
np_context_unload(np_context_t *np)
{
  const np_module_t *m;

  hts_mutex_lock(&np_list_mutex);
  np_num_contexts--;
  LIST_REMOVE(np, np_link);
  hts_mutex_unlock(&np_list_mutex);

  LIST_FOREACH(m, &np_modules, link) {
    if(m->unload)
      m->unload(np);
  }
}

/**
 *
 */
static void
np_init(void)
{
  hts_mutex_init(&np_list_mutex);
  if(gconf.load_np) {
    char errbuf[512];
    if(np_context_create("cmdline", 0, gconf.load_np, "/tmp",
                         errbuf, sizeof(errbuf),
                         64 * 1024 * 1024, 128 * 1024) < 0) {
      TRACE(TRACE_ERROR, "cmdline", "Failed to load native plugin %s -- %s",
            gconf.load_np, errbuf);
    }
  }
}


INITME(INIT_GROUP_API, np_init, NULL, 0);


/**
 *
 */
int
np_plugin_load(const char *id, const char *url,
               char *errbuf, size_t errlen,
               int version, int flags,
               int memory_size, int stack_size)
{
  char storage[PATH_MAX];

  snprintf(storage, sizeof(storage),
           "%s/plugins/%s", gconf.persistent_path, id);

  if(np_context_create(id, flags, url, storage, errbuf, errlen,
                       memory_size, stack_size) < 0)
    return -1;

  return 0;
}


/**
 *
 */
void
np_plugin_unload(const char *id)
{
  np_context_t *np = np_context_find(id);
  if(np != NULL) {
    np_context_unload(np);
    np_context_release(np);
  }
}
