#pragma once

#include "arch/atomic.h"
#include "misc/queue.h"
#include "ext/vmir/src/vmir.h"
#include "fileaccess/fileaccess.h"
#include "misc/lockmgr.h"


LIST_HEAD(np_resource_list, np_resource);



typedef struct np_context {
  lockmgr_t np_lockmgr;

  LIST_ENTRY(np_context) np_link;
  struct np_resource_list np_resources;

  char *np_id;
  char *np_storage;
  char *np_path;

  ir_unit_t *np_unit;

  void *np_mem;

  // ----------------

  struct backend *np_backend;

} np_context_t;


void np_lock(np_context_t *np);

void np_unlock(np_context_t *np);


typedef struct np_resource {
  np_context_t *nr_context;
  LIST_ENTRY(np_resource) nr_link;
} np_resource_t;




int np_plugin_load(const char *id, const char *url,
                   char *errbuf, size_t errlen,
                   int version, int flags,
                   int memory_size, int stack_size);

void np_plugin_unload(const char *id);


static __inline np_context_t * attribute_unused_result
np_context_retain(np_context_t *np)
{
  atomic_inc(&np->np_lockmgr.lm_refcount);
  return np;
}

void np_context_release(void *aux);

np_context_t **np_get_all_contexts(void);

void np_context_vec_free(np_context_t **v);

int np_fd_from_prop(ir_unit_t *iu, prop_t *p);

struct metadata;

int np_fa_probe(fa_handle_t *fh, const void *buf, size_t len,
                struct metadata *md, const char *url);


#define NP_FD_METADATA   VMIR_FD_TYPE_USER + 0
#define NP_FD_PROP       VMIR_FD_TYPE_USER + 1


/**
 * API modules
 */
typedef struct np_module {
  LIST_ENTRY(np_module) link;
  const char *name;
  const vmir_function_tab_t *tab;
  size_t tabsize;
  void (*ctxinit)(np_context_t *ctx);
  void (*unload)(np_context_t *ctx);
} np_module_t;

void np_register_module(np_module_t *m);

#define NP_MODULE(nam, fn, ctxini, unloadr)                             \
  static np_module_t HTS_JOIN(npmoduledef, __LINE__) = {                \
    .name = nam,                                                        \
    .tab = fn,                                                          \
    .tabsize = ARRAYSIZE(fn),                                           \
    .ctxinit = ctxini,                                                  \
    .unload = unloadr,                                                  \
  };                                                                    \
  INITIALIZER(HTS_JOIN(npmoduledefinit, __LINE__))                      \
  { np_register_module(&HTS_JOIN(npmoduledef, __LINE__));}
