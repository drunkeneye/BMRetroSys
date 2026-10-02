//
// bootstat_zxspectrum.h
//
// The ZX Spectrum build carries its ROMs on the SD card (/roms/ZXSPECTRUM,
// staged from the vendored ZEsarUX tree), so there is no boot-time ROM
// checksum table. The config layer verifies ROM sizes at init instead.
//

#ifndef BOOTSTAT_ZXSPECTRUM
#define BOOTSTAT_ZXSPECTRUM

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
