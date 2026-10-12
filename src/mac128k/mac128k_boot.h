//
// mac128k_boot.h
//
// Mac 128K guest bring-up: ROM scan/validate/stage, disk staging,
// backend init (Mini vMac alloc + ROM + disks + clock).
//

#ifndef mac128k_boot_h
#define mac128k_boot_h

namespace bmx {
namespace mac128k {

bool boot_init(void);

// Staged disk images (persist across reboots in this process).
const char *boot_disk_path(int drive);  // "" when empty
void boot_disk_set(int drive, const char *path);

}  // namespace mac128k
}  // namespace bmx

#endif
