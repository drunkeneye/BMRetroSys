//
// cpc_dsk.h
//
// CPC DSK image model: regular ("MV - CPCEMU") and extended DSK parsing,
// sector lookup by ID, in-memory owned sector data, serialize-back for
// write support. Copy protections based on weak/fuzzy bits are NOT
// modelled (stable data is returned); anything else structural (extra
// sectors, odd sizes, deleted marks) is.
//

#ifndef cpc_dsk_h
#define cpc_dsk_h

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace cpc {

struct DskSector {
  uint8_t c, h, r, n;  // ID field
  uint8_t st1, st2;    // FDC status captured at format time
  uint8_t *data;       // owned, len bytes
  uint32_t len;
};

struct DskTrack {
  uint8_t track, side;
  uint8_t gap3, filler;
  DskSector *sectors;  // owned array
  int num_sectors;
};

struct DskImage {
  DskTrack *tracks;  // [track * sides + side], owned
  int num_tracks;
  int num_sides;
  bool dirty;
  char path[256];  // source file for write-back ("" if none)
};

// Parse a DSK file buffer (regular or extended). Returns NULL with a log
// line on any structural problem (never a half-built image).
DskImage *dsk_parse(const uint8_t *buf, size_t size, const char *path);
// Free everything.
void dsk_free(DskImage *img);
// Serialize back to DSK bytes (regular layout when uniform, extended
// otherwise). Returns malloc'd buffer + length (NULL on failure).
uint8_t *dsk_serialize(const DskImage *img, size_t *out_len);
// Sector lookup by full ID (C,H,R,N) on a track, like the 765's own
// search (a size mismatch counts as absent). NULL when absent.
DskSector *dsk_find_sector(DskImage *img, int track, int side, uint8_t c,
                           uint8_t h, uint8_t r, uint8_t n);
// Reformat a track (WRITE ID support): sectors defined by chrn_list
// (4 bytes each: C,H,R,N), data filled with fill. Keeps geometry.
bool dsk_format_track(DskImage *img, int track, int side,
                      const uint8_t *chrn_list, int count, uint8_t fill);
// AMSDOS boot choice: scan the disc directory and pick a boot file --
// DISC.BAS when present, else the first *.BAS, else the first *.BIN
// (demo loaders), else the first file at all. Writes "NAME" or
// "NAME.EXT" (no quotes) and returns 0; -1 when no file is found.
int dsk_boot_file(DskImage *img, char *out, size_t out_size);

}  // namespace cpc
}  // namespace bmx

#endif
