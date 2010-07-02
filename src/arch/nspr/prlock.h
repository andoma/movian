#ifndef PRLOCK_H__
#define PRLOCK_H__

#include "prtypes.h"

PRLock *PR_NewLock(void);
void PR_DestroyLock(PRLock *);

#define PR_Lock(m)   hts_mutex_lock(m)
#define PR_Unlock(m) hts_mutex_unlock(m)

#endif /* PRLOCK_H__ */
