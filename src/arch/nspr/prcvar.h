#ifndef PRCVAR_H__
#define PRCVAR_H__

#include "prtypes.h"

PRCondVar *PR_NewCondVar(PRLock *lock);
void PR_DestroyCondVar(PRCondVar *cvar);

PRStatus PR_WaitCondVar(PRCondVar *cvar, PRIntervalTime timeout);

#define PR_NotifyCondVar(c)    hts_cond_signal(&(c)->cond)
#define PR_NotifyAllCondVar(c) hts_cond_broadcast(&(c)->cond)


#endif /* PRCVAR_H__ */
