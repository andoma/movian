#if defined(__i386__)
#include "jsautocfg_x86.h"
#elif defined(__x86_64__)
#include "jsautocfg_x86_64.h"
#elif defined(__arm__)
#include "jsautocfg_arm.h"
#elif defined(__powerpc64__)
#include "jsautocfg_powerpc64.h"
#else
#error "Need config for spidermonkey"
#endif
