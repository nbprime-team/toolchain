/* sys/mman.h —— HP Prime 移植的桩头文件：这里没有虚拟内存。 */
#ifndef _HP_SYS_MMAN_H
#define _HP_SYS_MMAN_H
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_PRIVATE 2
#define MAP_FIXED 16
#define MAP_ANONYMOUS 32
#define MAP_FAILED ((void *)-1)
void *mmap(void *addr, unsigned long length, int prot, int flags, int fd, long offset);
int munmap(void *addr, unsigned long length);
int mprotect(void *addr, unsigned long length, int prot);
#endif
