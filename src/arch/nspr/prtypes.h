#ifndef PRTYPES_H__
#define PRTYPES_H__

#include <stdint.h>
#include "arch/threads.h"

typedef hts_mutex_t PRLock;

typedef struct {
  hts_mutex_t *mtx;
  hts_cond_t cond;
} PRCondVar;

typedef int PRIntervalTime;

typedef enum { PR_FAILURE = -1, PR_SUCCESS = 0 } PRStatus;

typedef int32_t PRInt32;

typedef unsigned int PRUintn;

typedef int PRThread;

#define PR_INTERVAL_NO_TIMEOUT -1

typedef void (*PRThreadPrivateDTOR)(void *priv);

#endif /* PRTYPES_H__ */
