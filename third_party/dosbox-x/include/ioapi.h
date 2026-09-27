/* ioapi.h shim - minizip file-function API. ZIP-drive support is Phase-3
 * (backed by third_party/miniz later); dos_programs.cpp only needs the
 * types and fill helper to compile. */
#ifndef BMX_IOAPI_H_SHIM_
#define BMX_IOAPI_H_SHIM_

#include <stdio.h>

typedef void *fopen64_filefunc(void *opaque, const void *filename, int mode);
typedef unsigned long (*fread64_filefunc)(void *opaque, void *stream,
                                          void *buf, unsigned long size);
typedef unsigned long (*fwrite64_filefunc)(void *opaque, void *stream,
                                           const void *buf,
                                           unsigned long size);
typedef long (*ftell64_filefunc)(void *opaque, void *stream);
typedef long (*fseek64_filefunc)(void *opaque, void *stream,
                                 unsigned long offset, int origin);
typedef int (*fclose_filefunc)(void *opaque, void *stream);
typedef int (*ferror_filefunc)(void *opaque, void *stream);

typedef struct zlib_filefunc64_def_s {
  fopen64_filefunc *zopen64_file;
  fread64_filefunc *zread_file;
  fwrite64_filefunc *zwrit_file;
  ftell64_filefunc *ztell_file;
  fseek64_filefunc *zseek_file;
  fclose_filefunc *zclose_file;
  ferror_filefunc *zerror_file;
  void *opaque;
} zlib_filefunc64_def;

void fill_fopen64_filefunc(zlib_filefunc64_def *pzlib_filefunc_def);

#endif /* BMX_IOAPI_H_SHIM_ */
