#include <windows.h>



typedef CRITICAL_SECTION hts_mutex_t;
typedef CONDITION_VARIABLE hts_cond_t;
typedef HANDLE hts_thread_t;

#define hts_mutex_init(m)   InitializeCriticalSection(m)
#define hts_mutex_lock(m)   EnterCriticalSection(m)
#define hts_mutex_unlock(m) LeaveCriticalSection(m)
#define hts_mutex_destroy(m) DeleteCriticalSection(m)

#define hts_cond_init(c, m)             InitializeConditionVariable(c)
#define hts_cond_destroy(c)             DeleteConditionVariable(c)
#define hts_cond_signal(c)              WakeConditionVariable(c)
#define hts_cond_broadcast(c)           WakeAllConditionVariable(c)
#define hts_cond_wait(c, m)             SleepConditionVariableCS(c, m, INFINITE)
#define hts_cond_wait_timeout(c, m, to) SleepConditionVariableCS(c, m, to)

#define HTS_MUTEX_DECL(name) \
  hts_mutex_t name; \
  INITIALIZER(global_mtx_init_ ## name) { \
  hts_mutex_init(&name); \
  }

extern void hts_thread_create_detached(const char *, void *(*)(void *), void *,
  int);

extern void hts_thread_create_joinable(const char *, hts_thread_t *,
  void *(*)(void *), void *, int);

#define THREAD_PRIO_BGTASK 0

