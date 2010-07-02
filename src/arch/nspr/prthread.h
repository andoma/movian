#ifndef PRTHREAD_H__
#define PRTHREAD_H__

#include "prtypes.h"

PRThread *PR_GetCurrentThread(void);

PRStatus PR_NewThreadPrivateIndex(PRUintn *newIndex, 
				  PRThreadPrivateDTOR destructor);

PRStatus PR_SetThreadPrivate(PRUintn key, void *priv);

void *PR_GetThreadPrivate(PRUintn key);

#endif /* PRTHREAD_H__ */
