#pragma once

void usage_init(void);

void usage_fini(void);

void usage_report_send(int stored);

void usage_inc_counter(const char *id, int value);

void usage_inc_plugin_counter(const char *plugin, const char *id, int value);
