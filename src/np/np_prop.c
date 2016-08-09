#include "main.h"
#include "np.h"
#include "prop/prop.h"
#include "nativeplugin.h"

static prop_t *
prop_from_fd(ir_unit_t *iu, int fd)
{
  return (prop_t *)vmir_fd_get(iu, fd, NP_FD_PROP);
}

static void
np_prop_release(ir_unit_t *iu, intptr_t handle)
{
  prop_ref_dec((prop_t *)handle);
}

int
np_fd_from_prop(ir_unit_t *iu, prop_t *p)
{
  return vmir_fd_create(iu, (intptr_t)p, NP_FD_PROP, np_prop_release);
}


static int
np_prop_create(void *ret, const void *rf, struct ir_unit *iu)
{
  prop_t *parent = prop_from_fd(iu, vmir_vm_arg32(&rf));
  const char *name = vmir_vm_ptr(&rf, iu);
  prop_t *child = prop_create_r(parent, name);
  vmir_vm_ret32(ret, np_fd_from_prop(iu, child));
  return 0;
}

static int
np_prop_create_root(void *ret, const void *rf, struct ir_unit *iu)
{
  prop_t *child = prop_ref_inc(prop_create_root(NULL));
  vmir_vm_ret32(ret, np_fd_from_prop(iu, child));
  return 0;
}

static int
np_prop_set(void *ret, const void *rf, struct ir_unit *iu)
{
  prop_t *parent = prop_from_fd(iu, vmir_vm_arg32(&rf));
  const char *name = vmir_vm_ptr(&rf, iu);

  switch(vmir_vm_arg32(&rf)) {
  case NP_PROP_SET_STRING:
    prop_set(parent, name, PROP_SET_STRING, vmir_vm_ptr(&rf, iu));
    break;
  case NP_PROP_SET_INT:
    prop_set(parent, name, PROP_SET_INT, vmir_vm_arg32(&rf));
    break;
  case NP_PROP_SET_FLOAT:
    prop_set(parent, name, PROP_SET_FLOAT, vmir_vm_arg_dbl(&rf));
    break;
  }
  return 0;
}



static int
np_prop_append(void *ret, const void *rf, struct ir_unit *iu)
{
  prop_t *parent = prop_from_fd(iu, vmir_vm_arg32(&rf));
  prop_t *child = prop_from_fd(iu, vmir_vm_arg32(&rf));

  if(prop_set_parent(child, parent))
    prop_destroy(child);

  return 0;
}


static const vmir_function_tab_t np_prop_funcs[] = {
  {"np_prop_create",       &np_prop_create},
  {"np_prop_create_root",  &np_prop_create_root},
  {"np_prop_set",          &np_prop_set},
  {"np_prop_append",       &np_prop_append},
};


NP_MODULE("prop", np_prop_funcs, NULL, NULL);
