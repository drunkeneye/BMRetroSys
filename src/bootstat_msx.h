//
// bootstat_msx.h
//
// The MSX build carries its ROM on the SD card (/roms/MSX/msx.rom,
// 32K BIOS+BASIC), so there is no boot-time ROM checksum table.
// The config layer verifies ROM size at init instead.
//

#ifndef BOOTSTAT_MSX
#define BOOTSTAT_MSX

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
