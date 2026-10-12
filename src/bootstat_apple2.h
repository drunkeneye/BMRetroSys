//
// bootstat_apple2.h
//
// The APPLE2 build carries its ROMs on the SD card (/roms/APPLE2,
// user-supplied or fetched by tools/download_roms.py --only=apple2), so
// there is no boot-time ROM checksum table. The config layer verifies ROM
// sizes at init instead.
//

#ifndef BOOTSTAT_APPLE2
#define BOOTSTAT_APPLE2

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
