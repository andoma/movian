#include "showtime.h"
#include "ipc.h"

void
ipc_init(void)
{
#ifdef CONFIG_LIRC
  lirc_start();
#endif

#if ENABLE_SERDEV
  if(gconf.enable_serdev)
    serdev_start();
#endif

#ifdef CONFIG_STDIN
  if(gconf.listen_on_stdin)
    stdin_start();
#endif
}
