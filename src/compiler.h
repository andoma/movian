/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
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



#ifdef __GNUC__

#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#endif

#ifndef unlikely
#define unlikely(x)     __builtin_expect((x),0)
#endif

#else

#ifndef likely
#define likely(x)   (x)
#endif

#ifndef unlikely
#define unlikely(x) (x)
#endif

#endif

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

