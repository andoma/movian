#include "config.h"
#include "ipc/ipc.h"

void
ipc_init(void)
{
#ifdef CONFIG_DBUS
  extern void dbus_start(void);
  dbus_start();
#endif

#ifdef CONFIG_LIRC
  extern void lirc_start(void);
  lirc_start();
#endif
}
