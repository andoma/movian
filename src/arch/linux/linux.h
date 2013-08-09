#pragma once

void trap_init(void);

void linux_init_monitors(void);

int get_system_concurrency(void);

void linux_check_capabilities(void);

void linux_webpopup_init(void);

void linux_webpopup_check(void);

struct prop;

typedef struct linux_ui {
  void *(*start)(struct prop *nav);
  struct prop *(*stop)(void *ui);
} linux_ui_t;
