#include "config.h"
#include "ipc.h"

void
ipc_init(void)
{
#ifdef CONFIG_DBUS
  dbus_start();
#endif

#ifdef CONFIG_LIRC
  lirc_start();
#endif
}
