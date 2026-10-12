//
// bmx-shim/sys/shm.h
//
// BMX bare metal has no System V shared memory; the vendored KEGS core
// never calls shmget/shmat on the compiled paths (in-process malloc ring
// instead). Present so the include resolves; purposefully minimal.
//

#ifndef BMX_APPLE2_SYS_SHM_H
#define BMX_APPLE2_SYS_SHM_H

#include <sys/ipc.h>

#endif
