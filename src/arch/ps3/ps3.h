
void my_trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void thread_reaper(void *aux);

void load_syms(void);
