/* chd.h shim - CHD (MAME compressed hunks) is a Phase-1 non-goal. The
 * subset never calls chd_* (verified: no chd_ call sites in any vendored
 * .cpp); cdrom.h only holds `chd_file *` / `chd_header *` members, so
 * forward declarations satisfy compilation. */
#ifndef BMX_CHD_H_SHIM_
#define BMX_CHD_H_SHIM_

struct chd_file;
struct chd_header;

#endif /* BMX_CHD_H_SHIM_ */
