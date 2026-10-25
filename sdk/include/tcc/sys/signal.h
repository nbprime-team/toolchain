/* sys/signal.h —— HP Prime 移植对 newlib 头的补充：加上 tccrun.c 需要的部分
 * （FPE_* 错误码，以及 sa_sigaction 成员别名）。 */
#ifndef _HP_SYS_SIGNAL_H
#define _HP_SYS_SIGNAL_H

#include_next <sys/signal.h>

#ifndef FPE_INTDIV
#define FPE_INTDIV 1
#define FPE_FLTDIV 2
#define FPE_INTOVF 3
#define FPE_FLTOVF 4
#endif

#ifndef sa_sigaction
/* newlib 的裸机 struct sigaction 没有 sa_sigaction 成员；TCC 的 sig_error
 * 只会经由我们的空实现 sigaction() 安装，因此把它别名到 sa_handler 是安全的。 */
#define sa_sigaction sa_handler
#endif

#endif
