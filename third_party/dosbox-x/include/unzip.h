/* unzip.h shim - minizip reader API. Reading ZIPs as drives is Phase-3;
 * dos_programs.cpp + zipcppstdbuf.h only need the types, constants and
 * failure-returning entry points to compile and link. */
#ifndef BMX_UNZIP_H_SHIM_
#define BMX_UNZIP_H_SHIM_

#include "ioapi.h"
#include <ctime>

typedef void *unzFile;

#define UNZ_OK 0
#define UNZ_END_OF_LIST_OF_FILE (-100)
#define UNZ_ERRNO (-1)
#define UNZ_EOF 0
#define UNZ_PARAMERROR (-2)
#define UNZ_BADZIPFILE (-3)
#define UNZ_INTERNALERROR (-4)
#define UNZ_CRCERROR (-5)

typedef struct unz_file_info64_s {
  unsigned long version;
  unsigned long version_needed;
  unsigned long flag;
  unsigned long compression_method;
  unsigned long dosDate;
  unsigned long crc;
  unsigned long long compressed_size;
  unsigned long long uncompressed_size;
  unsigned long size_filename;
  unsigned long size_file_extra;
  unsigned long size_file_comment;
  unsigned long disk_num_start;
  unsigned long internal_fa;
  unsigned long external_fa;
  struct tm tmu_date;
} unz_file_info64;

unzFile unzOpen2_64(const void *path, zlib_filefunc64_def *pzlib_filefunc_def);
int unzClose(unzFile zf);
int unzLocateFile(unzFile zf, const char *filename, int casesens);
int unzGetCurrentFileInfo64(unzFile zf, unz_file_info64 *info,
                            char *filename, unsigned long filename_size,
                            void *extrafield, unsigned long extrafield_size,
                            char *comment, unsigned long comment_size);
int unzOpenCurrentFile(unzFile zf);
int unzCloseCurrentFile(unzFile zf);
int unzReadCurrentFile(unzFile zf, void *buf, unsigned int len);

#endif /* BMX_UNZIP_H_SHIM_ */
