//
// bmx-shim/sys/ioctl.h
//
// BMX bare metal has no terminal/device ioctl interface; the vendored KEGS
// core never calls ioctl() on the compiled paths (only defc.h includes
// this header). Present so the include resolves; purposefully minimal.
//

#ifndef BMX_APPLE2_SYS_IOCTL_H
#define BMX_APPLE2_SYS_IOCTL_H

#define FIONREAD 0x541B

#endif
