//
// amiga_bmx_stubs.cpp
//
// Stub implementations for the upstream TUs excluded from the BMX build:
//
//   - Misc/RemoteServers/HttpTransport.cpp (pulls in cpp-httplib, which
//     needs OpenSSL/Brotli/pthreads — unavailable on bare metal).
//     HttpTransport objects still exist by value inside the compiled
//     Rsh/Rpc/Prom servers; servers are never started on BMX so these
//     are dead-ends that must only link.
//   - VICE-only kernel seams also required by the common menu (mirrors
//     atarist_bridge.cpp tail).
//

#include "amiga_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vaconfig.h"
#include "HttpTransport.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <time.h>
#include <utime.h>

namespace vamiga {

// Servers are never started on BMX; the HTTP transport is dead code.
HttpTransport::~HttpTransport() {}

void HttpTransport::disconnect() {}

void HttpTransport::main(u16 port, const string &endpoint) {
  (void)port;
  (void)endpoint;
}

}  // namespace vamiga

// The real BSD socket API lives in circle-newlib (socketio.cpp) and is used
// as-is (servers are never started, so it never runs). Only genuinely
// missing symbols are stubbed below, with C linkage to match their
// declarations.
extern "C" {

struct ifaddrs;
int getifaddrs(struct ifaddrs **ifap) {
  if (ifap != NULL) {
    *ifap = NULL;
  }
  return -1;
}

void freeifaddrs(struct ifaddrs *ifa) {
  (void)ifa;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd,
           long offset) {
  (void)addr;
  (void)len;
  (void)prot;
  (void)flags;
  (void)fd;
  (void)offset;
  return (void *)-1;
}

int munmap(void *addr, size_t len) {
  (void)addr;
  (void)len;
  return -1;
}

int getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host,
                socklen_t hostlen, char *serv, socklen_t servlen, int flags) {
  (void)addr;
  (void)addrlen;
  (void)host;
  (void)hostlen;
  (void)serv;
  (void)servlen;
  (void)flags;
  return -1;
}

int truncate(const char *path, long length) {
  (void)path;
  (void)length;
  return -1;
}

// _fcntl() syscall for newlib (same as atarist_bmx_platform.c): nothing on
// BMX needs file-descriptor flags; decline.
int _fcntl(int fd, int cmd, int arg) {
  (void)fd;
  (void)cmd;
  (void)arg;
  return -1;
}

int utime(const char *path, const struct utimbuf *times) {
  (void)path;
  (void)times;
  return -1;
}

int chmod(const char *path, unsigned int mode) {
  (void)path;
  (void)mode;
  return -1;
}

int ftruncate(int fd, long length) {
  (void)fd;
  (void)length;
  return -1;
}

// DMS unpacker (external xdms on desktop; DMS is out of scope on BMX,
// DF0-ADF only). Non-zero = failure, so DMS images report an error.
unsigned short extractDMS(const unsigned char *in, size_t inSize,
                          unsigned char **out, size_t *outSize, int verbose) {
  (void)in;
  (void)inSize;
  (void)out;
  (void)outSize;
  (void)verbose;
  return 1;
}

}  // extern "C"

extern "C" {

// userport (kernel.cpp GPIO paths; an A500 has no userport CIA)
uint8_t circle_get_userport_ddr(void) {
  return 0;
}

uint8_t circle_get_userport(void) {
  return 0xff;
}

void circle_set_userport(uint8_t value) {
  (void)value;
}

// monitor peek (kernel debug path)
uint8_t mem_bank_peek(int bank, uint16_t addr, void *context) {
  (void)bank;
  (void)addr;
  (void)context;
  return 0;
}

// disk/tape image listing (common menu.c)
struct image_contents_s;

struct image_contents_s *diskcontents_filesystem_read(const char *file_name) {
  (void)file_name;
  return NULL;
}

struct image_contents_s *tapecontents_read(const char *file_name) {
  (void)file_name;
  return NULL;
}

typedef struct image_contents_s image_contents_t;
typedef struct image_contents_file_list_s image_contents_file_list_t;

void image_contents_destroy(image_contents_t *contents) {
  (void)contents;
}

char *image_contents_to_string(image_contents_t *contents, char out_charset) {
  (void)contents;
  (void)out_charset;
  return NULL;
}

char *image_contents_file_to_string(image_contents_file_list_t *p,
                                    char out_charset) {
  (void)p;
  (void)out_charset;
  return NULL;
}

// PETSCII helpers (common ui.c; Amiga uses ASCII, no conversion)
uint8_t charset_petscii_to_screencode(uint8_t code, unsigned int reverse_mode) {
  (void)reverse_mode;
  return code;
}

void vsync_suspend_speed_eval(void) {
  bmx::amiga::debug_log("Amiga: menu closed");
}

}  // extern "C"
