/* KikiOS signal.h stub for TLSe */
#ifndef _SIGNAL_H
#define _SIGNAL_H

#define SIG_IGN  ((void (*)(int))1)
#define SIG_DFL  ((void (*)(int))0)
#define SIG_ERR  ((void (*)(int))-1)

#define SIGABRT 6
#define SIGFPE  8
#define SIGILL  4
#define SIGINT  2
#define SIGSEGV 11
#define SIGTERM 15
#define SIGPIPE 13

// Stub
static inline void (*signal(int sig, void (*handler)(int)))(int) {
    (void)sig;
    (void)handler;
    return SIG_IGN;
}

static inline int raise(int sig) { (void)sig; return 0; }

#endif
