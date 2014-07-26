#ifdef _MSC_VER
#define attribute_printf(a, b)
#else
#define attribute_printf(a, b) __attribute__((format(printf, a, b)))
#endif

#ifdef _MSC_VER
#define attribute_malloc
#else
#define attribute_malloc __attribute__((malloc))
#endif

#ifdef _MSC_VER
#define attribute_unused_result
#else
#define attribute_unused_result __attribute__((warn_unused_result))
#endif

#ifdef _MSC_VER
#define attribute_null_sentinel
#else
#define attribute_null_sentinel __attribute__((__sentinel__(0)))
#endif


#ifdef _MSC_VER
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#else
#include <sys/param.h>
#endif