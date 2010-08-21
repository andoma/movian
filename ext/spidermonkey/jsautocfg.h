#if defined(__i386__)
#include "jsautocfg_x86.h"
#elif defined(__x86_64__)
#include "jsautocfg_x86_64.h"
#else
#error "Need config for spidermonkey"
#endif
