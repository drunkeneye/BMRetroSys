//
// bbc_drive.h
//
// 1770 DFS drive attach/detach (drives 0/1) over insert_media. Supported:
// .ssd/.dsd (DFS), .adf (ADFS, needs the ADFS model).
//

#ifndef BMX_BBC_DRIVE_H
#define BMX_BBC_DRIVE_H

namespace bmx {
namespace bbc {

const char *drive_attached_name(int drive);
int drive_attach(int drive, const char *filename);
void drive_detach(int drive);
// Re-sends both drive slots to the core (fresh 1770 starts empty).
void drive_repush_all(void);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_DRIVE_H
