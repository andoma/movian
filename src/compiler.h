#pragma once

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
#define attribute_noreturn
#else
#define attribute_noreturn __attribute__((noreturn))
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
#define attribute_unused
#else
#define attribute_unused __attribute__((unused))
#endif

#ifdef _MSC_VER
#define strdup _strdup
#define alloca _alloca

#define __builtin_constant_p(x) 0

#define snprintf sprintf_s
#define strtok_r strtok_s
#endif



#ifdef _MSC_VER

#pragma section(".CRT$XCU",read)
#define INITIALIZER(f) \
   static void __cdecl f(void); \
   __declspec(allocate(".CRT$XCU")) void (__cdecl*f##_)(void) = f; \
   static void __cdecl f(void)

#elif defined(__GNUC__)

#define INITIALIZER(f) \
   static void f(void) __attribute__((constructor)); \
   static void f(void)

#endif

#define HTS_GLUE(a, b) a ## b
#define HTS_JOIN(a, b) HTS_GLUE(a, b)
#ifndef _MSC_VER
#define ARRAYSIZE(x) (sizeof(x) / sizeof(x[0]))
#endif
