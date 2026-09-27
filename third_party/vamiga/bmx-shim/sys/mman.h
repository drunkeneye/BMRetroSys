//
// bmx-shim/sys/mman.h -- bare-metal parse+link shim (never called at
// runtime; the HTTP servers are never started on BMX).
//

#ifndef BMX_VAMIGA_SYS_MMAN_H
#define BMX_VAMIGA_SYS_MMAN_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

extern void *mmap(void *addr, size_t len, int prot, int flags, int fd,
                  off_t offset);
extern int munmap(void *addr, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // BMX_VAMIGA_SYS_MMAN_H
