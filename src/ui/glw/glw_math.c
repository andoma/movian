#include "glw.h"

#if ENABLE_GLW_MATH_SSE
#include "glw_math_sse.c"
#else
#include "glw_math_c.c"
#endif
