//
// bootstat_cpc.h
//
// The CPC build carries its ROMs on the SD card (/roms/CPC, user-supplied
// cpc464/cpc664/cpc6128.rom), so there is no boot-time ROM checksum table.
// The config layer verifies ROM sizes at init instead.
//

#ifndef BOOTSTAT_CPC
#define BOOTSTAT_CPC

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
