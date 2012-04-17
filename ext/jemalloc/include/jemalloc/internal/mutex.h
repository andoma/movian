/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

typedef hts_mutex_t malloc_mutex_t;

#define MALLOC_MUTEX_INITIALIZE(qual, nam)				\
	qual hts_mutex_t nam;						\
	static void __attribute__((constructor)) hts_mutex_init ## nam(void) \
	{								\
		hts_mutex_init(&nam);					\
	}


#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

#  define isthreaded true

bool	malloc_mutex_init(malloc_mutex_t *mutex);
void	malloc_mutex_destroy(malloc_mutex_t *mutex);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#ifndef JEMALLOC_ENABLE_INLINE
void	malloc_mutex_lock(malloc_mutex_t *mutex);
bool	malloc_mutex_trylock(malloc_mutex_t *mutex);
void	malloc_mutex_unlock(malloc_mutex_t *mutex);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_MUTEX_C_))
JEMALLOC_INLINE void
malloc_mutex_lock(malloc_mutex_t *mutex)
{
	hts_mutex_lock(mutex);
}

JEMALLOC_INLINE void
malloc_mutex_unlock(malloc_mutex_t *mutex)
{
	hts_mutex_unlock(mutex);
}
#endif

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/
