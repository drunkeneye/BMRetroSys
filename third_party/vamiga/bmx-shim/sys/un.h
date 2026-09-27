//
// bmx-shim/sys/un.h -- bare-metal parse shim (never used at runtime).
//

#ifndef BMX_VAMIGA_SYS_UN_H
#define BMX_VAMIGA_SYS_UN_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIX_PATH_MAX 108

struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[UNIX_PATH_MAX];
};

#ifdef __cplusplus
}
#endif

#endif  // BMX_VAMIGA_SYS_UN_H
