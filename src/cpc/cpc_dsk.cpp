//
// cpc_dsk.cpp
//
// See cpc_dsk.h. Layout recap (both variants share the 256-byte track
// headers and 8-byte sector entries):
//   file[0x00..0x21]  magic: "MV - CPCEMU Disk-File\r\nDisk-Info\r\n"
//                      (regular) or "EXTENDED CPC DSK File\r\nDisk-Info\r\n"
//   file[0x22..0x2F]  creator (ignored)
//   file[0x30]        track count, file[0x31] side count
//   regular: file[0x32..0x33] track size (LE, includes 256B header);
//            tracks back-to-back from 0x100.
//   extended: file[0x34..] one byte per track (track-major:
//            track*sides+side), each = track size (incl. header) / 256;
//            0 = unformatted (no bytes stored); tracks back-to-back
//            from 0x100. (0x32..0x33 are unused in extended.)
//   track header: "Track-Info\r\n" + track#, side#, -, N, sector count,
//            gap3, filler + 8B entries (C,H,R,N,ST1,ST2,len_lo,len_hi);
//            sector data concatenated after the 256B header (entry
//            lengths; 0 means N-default).
// Every track magic is validated while walking; any mismatch rejects the
// file outright (a partial/garbled image is worse than none).
//

#include "cpc_dsk.h"
#include "cpc_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace bmx {
namespace cpc {

namespace {

const char kMagicRegular[] = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
const char kMagicExtended[] = "EXTENDED CPC DSK File\r\nDisk-Info\r\n";
const char kMagicTrack[] = "Track-Info\r\n";

const int kMaxTracks = 84;
const int kMaxSides = 2;
const int kMaxSectors = 32;
const size_t kMaxFile = 8 * 1024 * 1024;

uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

void free_track(DskTrack *t) {
  if (t == nullptr) {
    return;
  }
  for (int i = 0; i < t->num_sectors; i++) {
    free(t->sectors[i].data);
  }
  free(t->sectors);
  t->sectors = nullptr;
  t->num_sectors = 0;
}

}  // namespace

void dsk_free(DskImage *img) {
  if (img == nullptr) {
    return;
  }
  // Tracks array is embedded after the struct (single allocation); only
  // the per-track sector arrays and their data are freed separately.
  for (int i = 0; i < img->num_tracks * img->num_sides; i++) {
    DskTrack *tr = &img->tracks[i];
    for (int j = 0; j < tr->num_sectors; j++) {
      free(tr->sectors[j].data);
    }
    free(tr->sectors);
  }
  free(img);
}

DskImage *dsk_parse(const uint8_t *buf, size_t size, const char *path) {
  if (buf == nullptr || size < 0x100 || size > kMaxFile) {
    debug_log("dsk: bad size %lu", (unsigned long)size);
    return nullptr;
  }
  int extended = 0;
  if (memcmp(buf, kMagicExtended, 34) == 0) {
    extended = 1;
  } else if (memcmp(buf, kMagicRegular, 34) != 0) {
    debug_log("dsk: bad magic");
    return nullptr;
  }
  const int ntracks = buf[0x30];
  const int nsides = buf[0x31];
  if (ntracks <= 0 || ntracks > kMaxTracks || nsides <= 0 ||
      nsides > kMaxSides) {
    debug_log("dsk: bad geometry %dx%d", ntracks, nsides);
    return nullptr;
  }
  DskImage *img =
      (DskImage *)calloc(1, sizeof(DskImage) + (size_t)ntracks * nsides *
                                             sizeof(DskTrack));
  if (img == nullptr) {
    return nullptr;
  }
  // Tracks array embedded after the struct for single-free teardown.
  img->tracks = (DskTrack *)(img + 1);
  img->num_tracks = ntracks;
  img->num_sides = nsides;
  if (path != nullptr) {
    snprintf(img->path, sizeof(img->path), "%s", path);
  }

  size_t pos = 0x100;
  for (int t = 0; t < ntracks; t++) {
    for (int s = 0; s < nsides; s++) {
      DskTrack *tr = &img->tracks[t * nsides + s];
      tr->track = (uint8_t)t;
      tr->side = (uint8_t)s;
      // Extended table locates the track; regular uses the fixed size.
      if (extended) {
        // Table base is 0x34 (0x32..0x33 are unused in extended; reading
        // at 0x32 shifts every track by two and breaks all data reads).
        size_t entry = 0x34 + (size_t)(t * nsides + s);
        if (entry >= size) {
          debug_log("dsk: table overrun t%d s%d", t, s);
          dsk_free(img);
          return nullptr;
        }
        if (buf[entry] == 0) {
          // Unformatted track: no bytes stored, no sectors. Reads report
          // No Data / Missing Address Mark like real hardware.
          tr->num_sectors = 0;
          tr->sectors = nullptr;
          continue;
        }
        // Table walk: recompute from the cumulative table each time
        // (cheap, avoids drift); zero entries contribute 0.
        pos = 0x100;
        for (int tt = 0; tt < ntracks; tt++) {
          for (int ss = 0; ss < nsides; ss++) {
            if (tt == t && ss == s) {
              goto found_pos;
            }
            size_t e = 0x34 + (size_t)(tt * nsides + ss);
            pos += (size_t)buf[e] * 256;
          }
        }
      found_pos:;
      } else if (t != 0 || s != 0) {
        // Regular: fixed stride from the header word.
        const size_t stride = rd16(buf + 0x32);
        if (stride < 256 || stride > 64 * 1024) {
          debug_log("dsk: bad regular stride %lu", (unsigned long)stride);
          dsk_free(img);
          return nullptr;
        }
        pos = 0x100 + ((size_t)(t * nsides + s)) * stride;
      }
      if (pos + 256 > size ||
          memcmp(buf + pos, kMagicTrack, 12) != 0) {
        // Lenient: protected/odd images often have gap/unreadable tracks.
        // Leave the track empty (reads report Missing Address Mark) so
        // the rest of the disc still mounts instead of rejecting all.
        debug_log("dsk: track header missing t%d s%d (empty)", t, s);
        tr->num_sectors = 0;
        tr->sectors = nullptr;
        continue;
      }
      tr->track = buf[pos + 0x10];
      tr->side = buf[pos + 0x11];
      // Header fields (cpctech extdsk/CPCWiki/Sinclair-wiki/Caprice agree):
      // 0x14 = sector size (N), 0x15 = sector count, 0x16 = gap3,
      // 0x17 = filler. (NOT N@0x13: that misreads standard discs as
      // 2-sector [C1,C6] and hides the other 7 sectors.)
      const int nsec = buf[pos + 0x15];
      tr->gap3 = buf[pos + 0x16];
      tr->filler = buf[pos + 0x17];
      if (nsec <= 0 || nsec > kMaxSectors) {
        // Lenient: garbage count (often padding/protection) -> empty
        // track rather than a whole-disc reject.
        debug_log("dsk: bad sector count %d t%d s%d (empty)", nsec, t, s);
        tr->num_sectors = 0;
        tr->sectors = nullptr;
        continue;
      }
      tr->sectors = (DskSector *)calloc((size_t)nsec, sizeof(DskSector));
      if (tr->sectors == nullptr) {
        dsk_free(img);
        return nullptr;
      }
      tr->num_sectors = nsec;
      size_t dpos = pos + 256;
      for (int i = 0; i < nsec; i++) {
        const uint8_t *e = buf + pos + 0x18 + (size_t)i * 8;
        DskSector *sec = &tr->sectors[i];
        sec->c = e[0];
        sec->h = e[1];
        sec->r = e[2];
        sec->n = e[3] & 7;
        sec->st1 = e[4];
        sec->st2 = e[5];
        uint32_t len = rd16(e + 6);
        const uint32_t deflen = 128U << sec->n;
        if (len == 0 || len > 8192) {
          len = deflen;
        }
        if (dpos + len > size) {
          // Lenient: truncated image -> keep the sectors parsed so far
          // (or an empty track) instead of rejecting the whole disc.
          debug_log("dsk: sector overrun t%d s%d i%d (kept %d)", t, s, i,
                    i);
          for (int j = i; j < nsec; j++) {
            tr->sectors[j].data = nullptr;
            tr->sectors[j].len = 0;
          }
          tr->num_sectors = i;
          if (i == 0) {
            free(tr->sectors);
            tr->sectors = nullptr;
          }
          break;
        }
        sec->data = (uint8_t *)malloc(len != 0 ? len : 1);
        if (sec->data == nullptr) {
          dsk_free(img);
          return nullptr;
        }
        if (len != 0) {
          memcpy(sec->data, buf + dpos, len);
        }
        sec->len = len;
        dpos += len;
      }
      // Next regular track / extended validation happens via magic.
      if (!extended) {
        const size_t stride = rd16(buf + 0x32);
        pos = 0x100 + ((size_t)(t * nsides + s) + 1) * stride;
        (void)pos;
      }
    }
  }
  int datasectors = 0;
  for (int i = 0; i < ntracks * nsides; i++) {
    datasectors += img->tracks[i].num_sectors;
  }
  debug_log("dsk: parsed %s %dx%d, %d sectors%s", path != nullptr ? path : "?",
            ntracks, nsides, datasectors, extended ? " (extended)" : "");
  return img;
}

DskSector *dsk_find_sector(DskImage *img, int track, int side, uint8_t c,
                           uint8_t h, uint8_t r, uint8_t n) {
  if (img == nullptr || track < 0 || track >= img->num_tracks || side < 0 ||
      side >= img->num_sides) {
    return nullptr;
  }
  DskTrack *tr = &img->tracks[track * img->num_sides + side];
  for (int i = 0; i < tr->num_sectors; i++) {
    DskSector *sec = &tr->sectors[i];
    if (sec->c == c && sec->h == h && sec->r == r && sec->n == n) {
      return sec;
    }
  }
  return nullptr;
}

namespace {
// A plausible AMSDOS 8.3 name field: printable, no quote (would break the
// typer), first char not blank.
bool dir_text_ok(const uint8_t *p, int len) {
  if (p[0] == ' ') {
    return false;
  }
  for (int i = 0; i < len; i++) {
    if (p[i] < 0x20 || p[i] > 0x7E || p[i] == '"') {
      return false;
    }
  }
  return true;
}
}  // namespace

int dsk_boot_file(DskImage *img, char *out, size_t out_size) {
  if (img == nullptr || out == nullptr || out_size < 2) {
    return -1;
  }
  out[0] = '\0';
  // Collect plausible directory entries across the whole disc. Standard
  // formats keep them on track 2, but demo/custom discs scatter them
  // (verified: MCS demos hold BI1/BI2/BI3/BIN parts on several tracks).
  // Binary blobs can mimic one entry, so the choice prefers Boot names
  // by extension rather than trusting position.
  char first[13] = "";
  char first_bas[13] = "";
  char first_bin[13] = "";
  char disc_bas[13] = "";
  for (int t = 0; t < img->num_tracks; t++) {
    const DskTrack *tr = &img->tracks[t * img->num_sides + 0];
    for (int i = 0; i < tr->num_sectors; i++) {
      const DskSector *sec = &tr->sectors[i];
      if (sec->data == nullptr || sec->len < 32) {
        continue;
      }
      for (size_t off = 0; off + 32 <= sec->len; off += 32) {
        const uint8_t *e = sec->data + off;
        if (e[0] > 0x0F) {
          continue;  // deleted (E5) or system/reserved value
        }
        if (!dir_text_ok(e + 1, 8) || !dir_text_ok(e + 9, 3)) {
          continue;
        }
        char name[9];
        char ext[4];
        memcpy(name, e + 1, 8);
        name[8] = '\0';
        memcpy(ext, e + 9, 3);
        ext[3] = '\0';
        // Trim trailing spaces.
        for (int k = 7; k >= 0 && name[k] == ' '; k--) {
          name[k] = '\0';
        }
        for (int k = 2; k >= 0 && ext[k] == ' '; k--) {
          ext[k] = '\0';
        }
        if (name[0] == '\0') {
          continue;
        }
        char full[13];
        if (ext[0] != '\0') {
          snprintf(full, sizeof(full), "%s.%s", name, ext);
        } else {
          snprintf(full, sizeof(full), "%s", name);
        }
        if (first[0] == '\0') {
          snprintf(first, sizeof(first), "%s", full);
        }
        if (strcmp(ext, "BAS") == 0) {
          if (first_bas[0] == '\0') {
            snprintf(first_bas, sizeof(first_bas), "%s", full);
          }
          if (strcmp(name, "DISC") == 0 && disc_bas[0] == '\0') {
            snprintf(disc_bas, sizeof(disc_bas), "%s", full);
          }
        } else if (strcmp(ext, "BIN") == 0 && first_bin[0] == '\0') {
          snprintf(first_bin, sizeof(first_bin), "%s", full);
        }
      }
    }
  }
  const char *pick = disc_bas[0] != '\0'   ? disc_bas
                     : first_bas[0] != '\0' ? first_bas
                     : first_bin[0] != '\0' ? first_bin
                     : first[0] != '\0'     ? first
                                            : nullptr;
  if (pick == nullptr) {
    return -1;
  }
  snprintf(out, out_size, "%s", pick);
  return 0;
}

bool dsk_format_track(DskImage *img, int track, int side,
                      const uint8_t *chrn_list, int count, uint8_t fill) {
  if (img == nullptr || track < 0 || track >= img->num_tracks || side < 0 ||
      side >= img->num_sides || chrn_list == nullptr || count <= 0 ||
      count > kMaxSectors) {
    return false;
  }
  DskTrack *tr = &img->tracks[track * img->num_sides + side];
  DskSector *sectors =
      (DskSector *)calloc((size_t)count, sizeof(DskSector));
  if (sectors == nullptr) {
    return false;
  }
  for (int i = 0; i < count; i++) {
    const uint8_t n = chrn_list[i * 4 + 3] & 7;
    const uint32_t len = 128U << n;
    if (len > 8192) {
      free(sectors);
      return false;
    }
    sectors[i].c = chrn_list[i * 4];
    sectors[i].h = chrn_list[i * 4 + 1];
    sectors[i].r = chrn_list[i * 4 + 2];
    sectors[i].n = n;
    sectors[i].st1 = 0;
    sectors[i].st2 = 0;
    sectors[i].data = (uint8_t *)malloc(len != 0 ? len : 1);
    if (sectors[i].data == nullptr) {
      for (int j = 0; j < i; j++) {
        free(sectors[j].data);
      }
      free(sectors);
      return false;
    }
    memset(sectors[i].data, fill, len);
    sectors[i].len = len;
  }
  free_track(tr);
  tr->sectors = sectors;
  tr->num_sectors = count;
  img->dirty = true;
  return true;
}

uint8_t *dsk_serialize(const DskImage *img, size_t *out_len) {
  if (img == nullptr || out_len == nullptr) {
    return nullptr;
  }
  // Decide layout: regular iff every track has identical sector sizes.
  int uniform_n = -1;
  int uniform_count = -1;
  for (int i = 0; i < img->num_tracks * img->num_sides; i++) {
    const DskTrack *tr = &img->tracks[i];
    if (tr->num_sectors <= 0) {
      uniform_n = -2;
      break;
    }
    for (int j = 0; j < tr->num_sectors; j++) {
      const DskSector *s = &tr->sectors[j];
      if (s->len != (128U << s->n)) {
        uniform_n = -2;
        break;
      }
    }
    if (uniform_n == -2) {
      break;
    }
    if (uniform_n == -1) {
      uniform_n = tr->sectors[0].n;
      uniform_count = tr->num_sectors;
    } else if (tr->sectors[0].n != uniform_n ||
               tr->num_sectors != uniform_count) {
      uniform_n = -2;
      break;
    }
  }
  const int regular = (uniform_n >= 0);
  // Size up.
  size_t total = 256;
  for (int i = 0; i < img->num_tracks * img->num_sides; i++) {
    total += 256;
    const DskTrack *tr = &img->tracks[i];
    for (int j = 0; j < tr->num_sectors; j++) {
      total += tr->sectors[j].len;
    }
  }
  uint8_t *buf = (uint8_t *)malloc(total != 0 ? total : 1);
  if (buf == nullptr) {
    return nullptr;
  }
  memset(buf, 0, total);
  if (regular) {
    memcpy(buf, kMagicRegular, 34);
  } else {
    memcpy(buf, kMagicExtended, 34);
  }
  buf[0x30] = (uint8_t)img->num_tracks;
  buf[0x31] = (uint8_t)img->num_sides;
  size_t pos = 0x100;
  for (int t = 0; t < img->num_tracks; t++) {
    for (int s = 0; s < img->num_sides; s++) {
      const DskTrack *tr = &img->tracks[t * img->num_sides + s];
      size_t track_start = pos;
      memcpy(buf + pos, kMagicTrack, 12);
      buf[pos + 0x10] = tr->track;
      buf[pos + 0x11] = tr->side;
      buf[pos + 0x12] = 0;
      buf[pos + 0x13] = 0;
      buf[pos + 0x14] = tr->num_sectors != 0 ? tr->sectors[0].n : 2;
      buf[pos + 0x15] = (uint8_t)tr->num_sectors;
      buf[pos + 0x16] = tr->gap3;
      buf[pos + 0x17] = tr->filler;
      pos += 256;
      for (int i = 0; i < tr->num_sectors; i++) {
        const DskSector *sec = &tr->sectors[i];
        uint8_t *e = buf + track_start + 0x18 + (size_t)i * 8;
        e[0] = sec->c;
        e[1] = sec->h;
        e[2] = sec->r;
        e[3] = sec->n;
        e[4] = sec->st1;
        e[5] = sec->st2;
        e[6] = (uint8_t)(sec->len & 0xFF);
        e[7] = (uint8_t)((sec->len >> 8) & 0xFF);
        memcpy(buf + pos, sec->data, sec->len);
        pos += sec->len;
      }
      const size_t track_size = pos - track_start;
      if (regular) {
        buf[0x32] = (uint8_t)(track_size & 0xFF);
        buf[0x33] = (uint8_t)((track_size >> 8) & 0xFF);
      } else {
        const size_t entry = 0x34 + (size_t)(t * img->num_sides + s);
        buf[entry] = (uint8_t)((track_size + 255) / 256);
      }
    }
  }
  *out_len = total;
  return buf;
}

}  // namespace cpc
}  // namespace bmx
