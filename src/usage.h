#pragma once

#include "config.h"

#if ENABLE_USAGEREPORT

void usage_init(void);

void usage_fini(void);

void usage_report_send(int stored);

void usage_inc_counter(const char *id, int value);

void usage_inc_plugin_counter(const char *plugin, const char *id, int value);

#else

#define usage_init()

#define usage_fini()

#define usage_report_send(stored)

#define usage_inc_counter(id, value)

#define usage_inc_plugin_counter(plugin, id, value)

#endif

