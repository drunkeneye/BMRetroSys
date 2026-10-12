//
// bootstat_bbc.h
//
// The BBC build carries its ROMs on the SD card (/roms/BBC,
// user-supplied or fetched by tools/download_roms.py --only=bbc), so
// there is no boot-time ROM checksum table. The config layer verifies ROM
// sizes at init instead.
//

#ifndef BOOTSTAT_BBC
#define BOOTSTAT_BBC

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
