//
// bmx-shim/sys/ipc.h
//
// BMX bare metal has no System V IPC; the vendored KEGS core never calls
// shmget/shmat on the compiled paths (in-process malloc ring instead).
// Present so the include resolves; purposefully minimal.
//

#ifndef BMX_APPLE2_SYS_IPC_H
#define BMX_APPLE2_SYS_IPC_H

#define IPC_CREAT 01000
#define IPC_PRIVATE 0

#endif
