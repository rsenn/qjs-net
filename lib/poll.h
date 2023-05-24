/**
 * @file poll.h
 */
#ifndef QJSNET_LIB_LIB_POLL_H
#define QJSNET_LIB_LIB_POLL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef _WINSOCK2API_
enum {
  POLLIN = 0x0001,
#define POLLIN POLLIN
  POLLPRI = 0x0002,
#define POLLPRI POLLPRI
  POLLOUT = 0x0004,
#define POLLOUT POLLOUT
  POLLERR = 0x0008,
#define POLLERR POLLERR
  POLLHUP = 0x0010,
#define POLLHUP POLLHUP
  POLLNVAL = 0x0020,
#define POLLNVAL POLLNVAL
  POLLRDNORM = 0x0040,
#define POLLRDNORM POLLRDNORM
  POLLRDBAND = 0x0080,
#define POLLRDBAND POLLRDBAND
  POLLWRBAND = 0x0200,
#define POLLWRBAND POLLWRBAND
  POLLMSG = 0x0400,
#define POLLMSG POLLMSG
  /* POLLREMOVE is for /dev/epoll (/dev/misc/eventpoll),
   * a new event notification mechanism for 2.6 */
  POLLREMOVE = 0x1000,
#define POLLREMOVE POLLREMOVE
};

#if defined(__sparc__) || defined(__mips__)
#define POLLWRNORM POLLOUT
#else
#define POLLWRNORM 0x0100
#endif

#ifndef struct_pollfd_defined
#define struct_pollfd_defined 1
struct pollfd {
  int fd;
  short events;
  short revents;
};
#endif
#endif /* defined _WINSOCK2API_ */
  
typedef unsigned int nfds_t;

extern int poll(struct pollfd* ufds, nfds_t nfds, int timeout);

#if defined(_WIN32) && !defined(CYGWIN)
typedef int sigset_t[32];
#endif

#ifdef _GNU_SOURCE
#include <signal.h>
int ppoll(struct pollfd* fds, nfds_t nfds, const struct timespec* timeout, const sigset_t* sigmask);
#endif

#ifdef __cplusplus
}

#endif /* defined(__cplusplus) */

#endif /* QJSNET_LIB_LIB_POLL_H */
