/* zip.h shim - minizip writer API. ZIP creation/mounting is Phase-3;
 * dos_programs.cpp + zipcppstdbuf.h only need the types, constants and
 * failure-returning entry points to compile and link. */
#ifndef BMX_ZIP_H_SHIM_
#define BMX_ZIP_H_SHIM_

#include "ioapi.h"
#include <ctime>

typedef void *zipFile;

#define APPEND_STATUS_CREATE 0
#define APPEND_STATUS_CREATEAFTER 1
#define APPEND_STATUS_ADDINZIP 2

#define ZIP_OK 0
#define ZIP_ERRNO (-1)
#define ZIP_PARAMERROR (-2)
#define ZIP_BADZIPFILE (-3)
#define ZIP_INTERNALERROR (-4)

typedef struct tm zip_fileinfo_tm;
typedef struct zip_fileinfo_s {
  zip_fileinfo_tm tmz_date;
  unsigned long dosDate;
  unsigned long internal_fa;
  unsigned long external_fa;
} zip_fileinfo;

zipFile zipOpen2_64(const void *path, int append,
                    const char **globalcomment,
                    zlib_filefunc64_def *pzlib_filefunc_def);
int zipClose(zipFile zf, const char *globalcomment);
int zipOpenNewFileInZip(zipFile zf, const char *filename,
                        const zip_fileinfo *zipfi, const void *extrafield_local,
                        unsigned int size_extrafield_local,
                        const void *extrafield_global,
                        unsigned int size_extrafield_global,
                        const char *comment, int method, int level);
int zipWriteInFileInZip(zipFile zf, const void *buf, unsigned int len);
int zipCloseFileInZip(zipFile zf);

/* zlib method/level constants used by savestates.cpp's zipOutOpenFile
 * (defined in dosboxx_builtin_stubs.cpp; Phase-3 reports failure). */
#define Z_DEFLATED 8
#define Z_DEFAULT_STRATEGY 0
#define DEF_MEM_LEVEL 8
#define MAX_WBITS 15

int zipOpenNewFileInZip3_64(zipFile zf, const char *filename,
                            const zip_fileinfo *zipfi,
                            const void *extrafield_local,
                            unsigned int size_extrafield_local,
                            const void *extrafield_global,
                            unsigned int size_extrafield_global,
                            const char *comment, int method, int level, int raw,
                            int windowBits, int memLevel, int strategy,
                            const char *password, unsigned long crcFile,
                            int zip64);

#endif /* BMX_ZIP_H_SHIM_ */
