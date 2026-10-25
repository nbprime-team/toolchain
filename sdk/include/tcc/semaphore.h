/* semaphore.h —— HP Prime 移植的桩头文件：单线程，全部空实现。 */
#ifndef _HP_SEMAPHORE_H
#define _HP_SEMAPHORE_H

typedef struct { int dummy; } sem_t;

static inline int sem_init(sem_t *s, int pshared, unsigned value)
{
    (void)s; (void)pshared; (void)value;
    return 0;
}

static inline int sem_wait(sem_t *s)
{
    (void)s;
    return 0;
}

static inline int sem_post(sem_t *s)
{
    (void)s;
    return 0;
}

#endif
