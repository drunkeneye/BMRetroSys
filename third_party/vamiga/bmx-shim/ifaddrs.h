//
// bmx-shim/ifaddrs.h -- bare-metal parse+link shim (never called at
// runtime; the HTTP servers are never started on BMX).
//

#ifndef BMX_VAMIGA_IFADDRS_H
#define BMX_VAMIGA_IFADDRS_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs {
  struct ifaddrs *ifa_next;
  char *ifa_name;
  unsigned int ifa_flags;
  struct sockaddr *ifa_addr;
  struct sockaddr *ifa_netmask;
  struct sockaddr *ifa_dstaddr;
  void *ifa_data;
};

extern int getifaddrs(struct ifaddrs **ifap);
extern void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif

#endif  // BMX_VAMIGA_IFADDRS_H
