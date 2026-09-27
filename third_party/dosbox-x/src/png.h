/* png.h shim - libpng is a Phase-1 non-goal (BMX screenshots are BMP).
 * The only consumer is the BIOS boot-logo PNG decoder in ints/bios.cpp,
 * which null-checks png_create_read_struct/png_create_info_struct and
 * skips the logo when they fail. Every stub here returns NULL/0, so the
 * logo path is safely skipped and `textsplash` stays on. */
#ifndef BMX_PNG_H_SHIM_
#define BMX_PNG_H_SHIM_

#include <stddef.h>
#include <stdio.h>

typedef unsigned int png_uint_32;
typedef struct png_struct_def { int unused; } png_struct;
typedef png_struct *png_structp;
typedef struct png_info_def { int unused; } png_info;
typedef png_info *png_infop;
typedef unsigned char *png_bytep;
typedef const unsigned char *png_const_bytep;
typedef struct { unsigned char red, green, blue; } png_color;
typedef png_color *png_colorp;

#define PNG_LIBPNG_VER_STRING "1.6.0-bmx-stub"
#define PNG_COLOR_MASK_PALETTE 1
#define PNG_COLOR_MASK_COLOR 2

#ifdef __cplusplus
extern "C" {
#endif

static inline png_structp png_create_read_struct(const char *ver, void *err,
                                                 void *err_fn, void *warn_fn) {
  (void)ver;
  (void)err;
  (void)err_fn;
  (void)warn_fn;
  return NULL;
}
static inline png_infop png_create_info_struct(png_structp ctx) {
  (void)ctx;
  return NULL;
}
static inline void png_destroy_read_struct(png_structp *ctx, png_infop *info,
                                           png_infop *end) {
  (void)ctx;
  (void)info;
  (void)end;
}
static inline void png_set_user_limits(png_structp ctx, png_uint_32 w,
                                       png_uint_32 h) {
  (void)ctx;
  (void)w;
  (void)h;
}
static inline void png_init_io(png_structp ctx, FILE *fp) {
  (void)ctx;
  (void)fp;
}
static inline void png_set_read_fn(png_structp ctx, void *io,
                                   void (*fn)(png_structp, png_bytep, size_t)) {
  (void)ctx;
  (void)io;
  (void)fn;
}
static inline void png_read_info(png_structp ctx, png_infop info) {
  (void)ctx;
  (void)info;
}
static inline png_uint_32 png_get_IHDR(png_structp ctx, png_infop info,
                                       png_uint_32 *w, png_uint_32 *h,
                                       int *depth, int *ctype, int *ilace,
                                       int *compr, int *filter) {
  (void)ctx;
  (void)info;
  (void)w;
  (void)h;
  (void)depth;
  (void)ctype;
  (void)ilace;
  (void)compr;
  (void)filter;
  return 0;
}
static inline void png_set_strip_16(png_structp ctx) { (void)ctx; }
static inline void png_set_packing(png_structp ctx) { (void)ctx; }
static inline png_uint_32 png_get_PLTE(png_structp ctx, png_infop info,
                                       png_colorp *pal, int *count) {
  (void)ctx;
  (void)info;
  (void)pal;
  (void)count;
  return 0;
}
static inline void png_read_rows(png_structp ctx, png_bytep *rows,
                                 png_bytep display, png_uint_32 n) {
  (void)ctx;
  (void)rows;
  (void)display;
  (void)n;
}

#ifdef __cplusplus
}
#endif

#endif /* BMX_PNG_H_SHIM_ */
